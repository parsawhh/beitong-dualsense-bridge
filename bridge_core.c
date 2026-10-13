#include "bridge_core.h"
#include <string.h>

// Standard Betop 5-Point Coordinate Spline Maps (VIBRATION_MAP / VIBRATION_MAP1)
static const vibration_spline_t SPLINE_PRESETS[3] = {
    // 0: Linear
    { .x = {42, 84, 128, 167, 255}, .y = {42, 84, 128, 167, 255} },
    // 1: Intense / Hard (early steep torque delivery)
    { .x = {2, 60, 120, 190, 255},  .y = {40, 140, 180, 235, 255} },
    // 2: Gentle / Soft (delayed damping)
    { .x = {30, 60, 120, 190, 255}, .y = {10, 30, 80, 190, 220} }
};

static uint8_t clamp_u8(int val, int min_val, int max_val) {
    if (val < min_val) return (uint8_t)min_val;
    if (val > max_val) return (uint8_t)max_val;
    return (uint8_t)val;
}

static int scale_range(int val, int in_min, int in_max, int out_min, int out_max) {
    if (in_max == in_min) return out_min;
    float ratio = (float)(val - in_min) / (float)(in_max - in_min);
    int scaled = out_min + (int)(ratio * (float)(out_max - out_min) + 0.5f);
    if (scaled < out_min) return out_min;
    if (scaled > out_max) return out_max;
    return scaled;
}

static uint8_t apply_spline_curve(const vibration_spline_t *spline, uint8_t input_val) {
    if (input_val <= spline->x[0]) {
        return (uint8_t)scale_range(input_val, 0, spline->x[0], 0, spline->y[0]);
    }
    for (int i = 0; i < 4; i++) {
        if (input_val >= spline->x[i] && input_val <= spline->x[i+1]) {
            return (uint8_t)scale_range(input_val, spline->x[i], spline->x[i+1], spline->y[i], spline->y[i+1]);
        }
    }
    return spline->y[4];
}

uint32_t dualsense_crc32(uint8_t seed, const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    crc ^= seed;
    for (int b = 0; b < 8; b++) {
        crc = (crc & 1) ? ((crc >> 1) ^ 0xEDB88320) : (crc >> 1);
    }
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 1) ? ((crc >> 1) ^ 0xEDB88320) : (crc >> 1);
        }
    }
    return ~crc;
}

void bridge_init(dualsense_bridge_t *b, bool is_bluetooth) {
    memset(b, 0, sizeof(*b));
    b->is_bluetooth = is_bluetooth;
    b->bt_sequence = 0;

    b->lt_deadzone_start = 25; // Default ~10% travel deadzone
    b->lt_deadzone_end = 255;
    b->rt_deadzone_start = 25;
    b->rt_deadzone_end = 255;

    // High performance defaults for tactile punch & strong feedback
    b->trigger_force_gain = 1.35f;  // 35% boost to wall resistance
    b->trigger_vibe_gain  = 1.40f;  // 40% boost to recoil pulse
    b->rumble_gain        = 1.25f;  // 25% boost to grip motors
    b->min_force_floor    = 2;      // Ensure active effects never feel mushy or weak

    b->curve_type = CURVE_INTENSE;
    b->sync_motor_with_grip = false;

    b->rt_mode = DS_TRIGGER_OFF;
    b->lt_mode = DS_TRIGGER_OFF;
}

void bridge_set_gains(dualsense_bridge_t *b, float force_gain, float vibe_gain, float rumble_gain) {
    if (force_gain > 0.1f) b->trigger_force_gain = force_gain;
    if (vibe_gain > 0.1f)  b->trigger_vibe_gain = vibe_gain;
    if (rumble_gain > 0.1f) b->rumble_gain = rumble_gain;
}

void bridge_set_curve(dualsense_bridge_t *b, curve_type_t ctype, const vibration_spline_t *custom) {
    b->curve_type = ctype;
    if (ctype == CURVE_CUSTOM && custom) {
        b->custom_spline = *custom;
    }
}

void bridge_set_deadzones(dualsense_bridge_t *b, uint8_t lt_start, uint8_t lt_end, uint8_t rt_start, uint8_t rt_end) {
    b->lt_deadzone_start = lt_start;
    b->lt_deadzone_end = lt_end;
    b->rt_deadzone_start = rt_start;
    b->rt_deadzone_end = rt_end;
}

static void encode_ds_feedback(uint8_t start_pos, uint8_t strength, uint8_t *out_mode, uint8_t *out_params) {
    if (strength == 0) {
        *out_mode = DS_TRIGGER_OFF;
        memset(out_params, 0, 10);
        return;
    }
    *out_mode = DS_TRIGGER_FEEDBACK;
    memset(out_params, 0, 10);

    uint8_t pos = clamp_u8(start_pos, 0, 9);
    uint8_t str_val = clamp_u8(strength, 1, 8) - 1; // 0..7 (3 bits)

    uint16_t active_zones = 0;
    uint32_t force_zones = 0;
    for (int i = pos; i < 10; i++) {
        active_zones |= (1 << i);
        force_zones |= ((uint32_t)str_val << (3 * i));
    }

    out_params[0] = (uint8_t)(active_zones & 0xFF);
    out_params[1] = (uint8_t)((active_zones >> 8) & 0xFF);
    out_params[2] = (uint8_t)(force_zones & 0xFF);
    out_params[3] = (uint8_t)((force_zones >> 8) & 0xFF);
    out_params[4] = (uint8_t)((force_zones >> 16) & 0xFF);
    out_params[5] = (uint8_t)((force_zones >> 24) & 0xFF);
}

static void encode_ds_weapon(uint8_t start_pos, uint8_t end_pos, uint8_t strength, uint8_t *out_mode, uint8_t *out_params) {
    if (strength == 0) {
        *out_mode = DS_TRIGGER_OFF;
        memset(out_params, 0, 10);
        return;
    }
    *out_mode = DS_TRIGGER_WEAPON;
    memset(out_params, 0, 10);

    uint8_t start = clamp_u8(start_pos, 2, 7);
    uint8_t end = clamp_u8(end_pos, start + 1, 8);
    uint8_t str_val = clamp_u8(strength, 1, 8) - 1;

    uint16_t zones = (1 << start) | (1 << end);
    out_params[0] = (uint8_t)(zones & 0xFF);
    out_params[1] = (uint8_t)((zones >> 8) & 0xFF);
    out_params[2] = (uint8_t)(str_val & 0x07);
}

static void encode_ds_vibration(uint8_t start_pos, uint8_t amplitude, uint8_t frequency, uint8_t *out_mode, uint8_t *out_params) {
    if (amplitude == 0 || frequency == 0) {
        *out_mode = DS_TRIGGER_OFF;
        memset(out_params, 0, 10);
        return;
    }
    *out_mode = DS_TRIGGER_VIBRATION;
    memset(out_params, 0, 10);

    uint8_t pos = clamp_u8(start_pos, 0, 9);
    uint8_t amp_val = clamp_u8(amplitude, 1, 8) - 1;

    uint16_t active_zones = 0;
    uint32_t amp_zones = 0;
    for (int i = pos; i < 10; i++) {
        active_zones |= (1 << i);
        amp_zones |= ((uint32_t)amp_val << (3 * i));
    }

    out_params[0] = (uint8_t)(active_zones & 0xFF);
    out_params[1] = (uint8_t)((active_zones >> 8) & 0xFF);
    out_params[2] = (uint8_t)(amp_zones & 0xFF);
    out_params[3] = (uint8_t)((amp_zones >> 8) & 0xFF);
    out_params[4] = (uint8_t)((amp_zones >> 16) & 0xFF);
    out_params[5] = (uint8_t)((amp_zones >> 24) & 0xFF);
    out_params[8] = frequency;
}

static void translate_bt_trigger(dualsense_bridge_t *b, uint8_t side, uint8_t mode, const uint8_t *params, size_t param_len,
                                 uint8_t *target_mode, uint8_t *target_params) {
    (void)side;
    if (mode == BT_TRIGGER_OFF || param_len == 0) {
        *target_mode = DS_TRIGGER_OFF;
        memset(target_params, 0, 10);
        return;
    }

    if (mode == BT_TRIGGER_RACE_RESISTANCE) {
        // Mode 1: [start_pos (0-100), force (0-100)]
        uint8_t p_pos = param_len > 0 ? params[0] : 0;
        uint8_t p_force = param_len > 1 ? params[1] : 50;

        uint8_t ds_pos = (uint8_t)scale_range(p_pos, 0, 100, 0, 9);
        int raw_str = scale_range(p_force, 0, 100, 1, 8);
        int boosted_str = (int)((float)raw_str * b->trigger_force_gain + 0.5f);
        if (boosted_str < b->min_force_floor) boosted_str = b->min_force_floor;
        uint8_t ds_str = clamp_u8(boosted_str, 1, 8);

        encode_ds_feedback(ds_pos, ds_str, target_mode, target_params);
    }
    else if (mode == BT_TRIGGER_SHOT_BREAKPOINT) {
        // Mode 2: [start_pos, click_pos, force, return_force]
        uint8_t p_start = param_len > 0 ? params[0] : 20;
        uint8_t p_end = param_len > 1 ? params[1] : 70;
        uint8_t p_force = param_len > 2 ? params[2] : 80;

        uint8_t ds_start = (uint8_t)scale_range(p_start, 0, 100, 2, 7);
        uint8_t ds_end = (uint8_t)scale_range(p_end, 0, 100, ds_start + 1, 8);
        int raw_force = scale_range(p_force, 0, 100, 1, 8);
        int boosted_force = (int)((float)raw_force * b->trigger_force_gain + 0.5f);
        if (boosted_force < b->min_force_floor) boosted_force = b->min_force_floor;
        uint8_t ds_force = clamp_u8(boosted_force, 1, 8);

        encode_ds_weapon(ds_start, ds_end, ds_force, target_mode, target_params);
    }
    else if (mode == BT_TRIGGER_GUN_VIBRATION) {
        // Mode 3: [start_pos, freq, strength, ...]
        uint8_t p_pos = param_len > 0 ? params[0] : 0;
        uint8_t p_freq = param_len > 1 ? params[1] : 50;
        uint8_t p_amp = param_len > 2 ? params[2] : 90;

        uint8_t ds_pos = (uint8_t)scale_range(p_pos, 0, 100, 0, 9);
        int raw_amp = scale_range(p_amp, 0, 100, 1, 8);
        int boosted_amp = (int)((float)raw_amp * b->trigger_vibe_gain + 0.5f);
        if (boosted_amp < b->min_force_floor) boosted_amp = b->min_force_floor;
        uint8_t ds_amp = clamp_u8(boosted_amp, 1, 8);

        uint8_t ds_freq = (uint8_t)scale_range(p_freq, 0, 100, 10, 240);
        encode_ds_vibration(ds_pos, ds_amp, ds_freq, target_mode, target_params);
    }
    else {
        *target_mode = DS_TRIGGER_OFF;
        memset(target_params, 0, 10);
    }
}

int bridge_build_current_report(dualsense_bridge_t *b, uint8_t *out_data, size_t out_max_len) {
    if (!b->is_bluetooth) {
        if (out_max_len < sizeof(ds_usb_output_report_t)) return 0;
        ds_usb_output_report_t rep;
        memset(&rep, 0, sizeof(rep));
        rep.report_id = 0x02;

        rep.common.valid_flag0 = DS_FLAG0_COMPATIBLE_VIBRATION | DS_FLAG0_RIGHT_TRIGGER_ENABLE | DS_FLAG0_LEFT_TRIGGER_ENABLE;
        rep.common.motor_right = b->motor_right;
        rep.common.motor_left = b->motor_left;

        rep.common.right_trigger_mode = b->rt_mode;
        memcpy(rep.common.right_trigger_params, b->rt_params, 10);

        rep.common.left_trigger_mode = b->lt_mode;
        memcpy(rep.common.left_trigger_params, b->lt_params, 10);

        memcpy(out_data, &rep, sizeof(rep));
        return (int)sizeof(rep);
    } else {
        if (out_max_len < sizeof(ds_bt_output_report_t)) return 0;
        ds_bt_output_report_t rep;
        memset(&rep, 0, sizeof(rep));
        rep.report_id = 0x31;
        rep.seq_tag = ((b->bt_sequence & 0x0F) << 4);
        b->bt_sequence = (b->bt_sequence + 1) % 16;
        rep.tag = DS_OUTPUT_TAG_BT;

        rep.common.valid_flag0 = DS_FLAG0_COMPATIBLE_VIBRATION | DS_FLAG0_RIGHT_TRIGGER_ENABLE | DS_FLAG0_LEFT_TRIGGER_ENABLE;
        rep.common.motor_right = b->motor_right;
        rep.common.motor_left = b->motor_left;

        rep.common.right_trigger_mode = b->rt_mode;
        memcpy(rep.common.right_trigger_params, b->rt_params, 10);

        rep.common.left_trigger_mode = b->lt_mode;
        memcpy(rep.common.left_trigger_params, b->lt_params, 10);

        rep.crc32 = dualsense_crc32(DS_BT_CRC_SEED, (const uint8_t *)&rep, 74);
        memcpy(out_data, &rep, sizeof(rep));
        return (int)sizeof(rep);
    }
}

int bridge_translate_packet(dualsense_bridge_t *b, const uint8_t *in_data, size_t in_len, uint8_t *out_data, size_t out_max_len) {
    if (!in_data || in_len < 4 || in_data[0] != 0x02) return 0;

    uint8_t cmd = in_data[1];

    // 1. Trigger Command: 02 56 <side> <mode> <params...>
    if (cmd == 0x56) {
        uint8_t side = in_data[2]; // 0 = Left, 1 = Right
        uint8_t mode = in_data[3];
        const uint8_t *params = in_data + 4;
        size_t param_len = in_len - 4;

        if (side == 0) {
            translate_bt_trigger(b, 0, mode, params, param_len, &b->lt_mode, b->lt_params);
        } else {
            translate_bt_trigger(b, 1, mode, params, param_len, &b->rt_mode, b->rt_params);
        }
        return bridge_build_current_report(b, out_data, out_max_len);
    }

    // 2. Rumble Command: 02 76 01 <subcmd> <params...>
    if (cmd == 0x76 && in_len >= 4 && in_data[2] == 0x01) {
        uint8_t sub = in_data[3];
        uint8_t val = (in_len > 4) ? in_data[4] : 0;

        // Apply spline curve + gain multiplier
        const vibration_spline_t *cur_spline = (b->curve_type == CURVE_CUSTOM) ? &b->custom_spline : &SPLINE_PRESETS[b->curve_type];
        uint8_t mapped_val = apply_spline_curve(cur_spline, val);
        int boosted_rumble = (int)((float)mapped_val * b->rumble_gain + 0.5f);
        uint8_t final_rumble = clamp_u8(boosted_rumble, 0, 255);

        if (sub == 4 || sub == 0 || sub == 2) {
            b->motor_right = final_rumble;
        } else if (sub == 5 || sub == 1 || sub == 3) {
            b->motor_left = final_rumble;
        }

        return bridge_build_current_report(b, out_data, out_max_len);
    }

    return 0;
}
