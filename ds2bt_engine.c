#include "ds2bt_engine.h"
#include <string.h>

// Standard Betop 5-Point Spline Curves (VIBRATION_MAP / VIBRATION_MAP1)
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

void ds2bt_init(dualsense_to_beitong_t *t, bool bluetooth_mode) {
    memset(t, 0, sizeof(*t));
    t->bluetooth_mode = bluetooth_mode;
    t->bt_sequence = 0;

    // High performance authoritative force profile
    t->trigger_force_gain = 1.45f;
    t->trigger_vibe_gain  = 1.50f;
    t->rumble_gain        = 1.35f;
    t->min_force_floor    = 25; // 25% minimum floor so walls and resistance never feel weak

    t->curve_type = CURVE_INTENSE;

    t->last_ds_rt_mode = DS_TRIGGER_OFF;
    t->last_ds_lt_mode = DS_TRIGGER_OFF;
}

void ds2bt_set_gains(dualsense_to_beitong_t *t, float force_gain, float vibe_gain, float rumble_gain, uint8_t min_floor) {
    if (force_gain > 0.1f) t->trigger_force_gain = force_gain;
    if (vibe_gain > 0.1f)  t->trigger_vibe_gain = vibe_gain;
    if (rumble_gain > 0.1f) t->rumble_gain = rumble_gain;
    t->min_force_floor = min_floor;
}

void ds2bt_set_curve(dualsense_to_beitong_t *t, curve_type_t ctype, const vibration_spline_t *custom) {
    t->curve_type = ctype;
    if (ctype == CURVE_CUSTOM && custom) {
        t->custom_spline = *custom;
    }
}

static void translate_single_trigger(dualsense_to_beitong_t *t, uint8_t side, uint8_t ds_mode, const uint8_t *ds_params,
                                     uint8_t *out_pkt, size_t *out_len) {
    memset(out_pkt, 0, 10);
    out_pkt[0] = 0x02; // Report ID
    out_pkt[1] = 0x56; // Trigger Effect Command
    out_pkt[2] = side; // 0 = LT, 1 = RT

    if (ds_mode == DS_TRIGGER_OFF || ds_mode == 0) {
        out_pkt[3] = BT_TRIGGER_OFF;
        *out_len = 9;
        return;
    }

    // 1. Sony FEEDBACK (0x21) & BOW (0x22) -> Beitong Continuous Resistance (Mode 1)
    if (ds_mode == DS_TRIGGER_FEEDBACK || ds_mode == DS_TRIGGER_BOW) {
        out_pkt[3] = BT_TRIGGER_RACE_RESISTANCE;

        uint16_t active_zones = (uint16_t)ds_params[0] | ((uint16_t)ds_params[1] << 8);
        int start_zone = 0;
        for (int z = 0; z < 10; z++) {
            if (active_zones & (1 << z)) { start_zone = z; break; }
        }
        uint8_t bt_start = (uint8_t)scale_range(start_zone, 0, 9, 0, 100);

        uint32_t force_zones = (uint32_t)ds_params[2] | ((uint32_t)ds_params[3] << 8) |
                               ((uint32_t)ds_params[4] << 16) | ((uint32_t)ds_params[5] << 24);
        uint8_t ds_strength = ((force_zones >> (3 * start_zone)) & 0x07) + 1; // 1..8
        int bt_raw_force = scale_range(ds_strength, 1, 8, 20, 100);

        int boosted_force = (int)((float)bt_raw_force * t->trigger_force_gain + 0.5f);
        if (boosted_force < t->min_force_floor) boosted_force = t->min_force_floor;
        uint8_t bt_force = clamp_u8(boosted_force, 0, 100);

        out_pkt[4] = bt_start;
        out_pkt[5] = bt_force;
        out_pkt[6] = 0;
        *out_len = 9;
        return;
    }

    // 2. Sony WEAPON (0x25) -> Beitong Two-Stage Tactile Wall (Mode 2)
    if (ds_mode == DS_TRIGGER_WEAPON) {
        out_pkt[3] = BT_TRIGGER_SHOT_BREAKPOINT;

        uint16_t zones = (uint16_t)ds_params[0] | ((uint16_t)ds_params[1] << 8);
        int start_zone = 2;
        int end_zone = 7;
        int found_first = 0;
        for (int z = 0; z < 10; z++) {
            if (zones & (1 << z)) {
                if (!found_first) { start_zone = z; found_first = 1; }
                else { end_zone = z; break; }
            }
        }

        uint8_t ds_str = (ds_params[2] & 0x07) + 1; // 1..8
        uint8_t bt_start = (uint8_t)scale_range(start_zone, 0, 9, 0, 100);
        uint8_t bt_click = (uint8_t)scale_range(end_zone, 0, 9, bt_start + 10, 100);

        int bt_raw_force = scale_range(ds_str, 1, 8, 30, 100);
        int boosted_force = (int)((float)bt_raw_force * t->trigger_force_gain + 0.5f);
        if (boosted_force < t->min_force_floor) boosted_force = t->min_force_floor;
        uint8_t bt_force = clamp_u8(boosted_force, 0, 100);

        out_pkt[4] = bt_start;
        out_pkt[5] = bt_click;
        out_pkt[6] = bt_force;
        out_pkt[7] = bt_force / 2; // Return snap strength
        out_pkt[8] = 1;
        *out_len = 9;
        return;
    }

    // 3. Sony VIBRATION (0x26), MACHINE (0x27), GALLOPING (0x23) -> Beitong Recoil Pulses (Mode 3)
    if (ds_mode == DS_TRIGGER_VIBRATION || ds_mode == DS_TRIGGER_MACHINE || ds_mode == DS_TRIGGER_GALLOPING) {
        out_pkt[3] = BT_TRIGGER_GUN_VIBRATION;

        uint16_t active_zones = (uint16_t)ds_params[0] | ((uint16_t)ds_params[1] << 8);
        int start_zone = 0;
        for (int z = 0; z < 10; z++) {
            if (active_zones & (1 << z)) { start_zone = z; break; }
        }
        uint8_t bt_start = (uint8_t)scale_range(start_zone, 0, 9, 0, 100);

        uint32_t amp_zones = (uint32_t)ds_params[2] | ((uint32_t)ds_params[3] << 8) |
                             ((uint32_t)ds_params[4] << 16) | ((uint32_t)ds_params[5] << 24);
        uint8_t ds_amp = ((amp_zones >> (3 * start_zone)) & 0x07) + 1; // 1..8
        uint8_t ds_freq = ds_params[8];

        int bt_raw_amp = scale_range(ds_amp, 1, 8, 25, 100);
        int boosted_amp = (int)((float)bt_raw_amp * t->trigger_vibe_gain + 0.5f);
        if (boosted_amp < t->min_force_floor) boosted_amp = t->min_force_floor;
        uint8_t bt_amp = clamp_u8(boosted_amp, 0, 100);

        uint8_t bt_freq = (uint8_t)scale_range(ds_freq > 0 ? ds_freq : 100, 10, 240, 5, 100);

        out_pkt[4] = bt_start;
        out_pkt[5] = bt_freq;
        out_pkt[6] = bt_amp;
        out_pkt[7] = 0;
        *out_len = 9;
        return;
    }

    out_pkt[3] = BT_TRIGGER_OFF;
    *out_len = 9;
}

int ds2bt_translate_output_report(dualsense_to_beitong_t *t,
                                  const uint8_t *ds_report,
                                  size_t ds_len,
                                  uint8_t out_packets[3][10],
                                  size_t out_lens[3]) {
    if (!ds_report || ds_len < 32) return 0;

    const ds_output_common_t *common = NULL;

    if (ds_report[0] == 0x02) {
        if (ds_len < sizeof(ds_usb_output_report_t)) return 0;
        const ds_usb_output_report_t *rep = (const ds_usb_output_report_t *)ds_report;
        common = &rep->common;
    } else if (ds_report[0] == 0x31) {
        if (ds_len < sizeof(ds_bt_output_report_t)) return 0;
        const ds_bt_output_report_t *rep = (const ds_bt_output_report_t *)ds_report;
        common = &rep->common;
    } else {
        return 0;
    }

    int pkt_count = 0;

    // 1. Right Trigger
    if (common->valid_flag0 & DS_FLAG0_RIGHT_TRIGGER_ENABLE) {
        translate_single_trigger(t, 1, common->right_trigger_mode, common->right_trigger_params,
                                 out_packets[pkt_count], &out_lens[pkt_count]);
        memcpy(t->bt_rt_pkt, out_packets[pkt_count], 10);
        t->last_ds_rt_mode = common->right_trigger_mode;
        memcpy(t->last_ds_rt_params, common->right_trigger_params, 10);
        pkt_count++;
    }

    // 2. Left Trigger
    if (common->valid_flag0 & DS_FLAG0_LEFT_TRIGGER_ENABLE) {
        translate_single_trigger(t, 0, common->left_trigger_mode, common->left_trigger_params,
                                 out_packets[pkt_count], &out_lens[pkt_count]);
        memcpy(t->bt_lt_pkt, out_packets[pkt_count], 10);
        t->last_ds_lt_mode = common->left_trigger_mode;
        memcpy(t->last_ds_lt_params, common->left_trigger_params, 10);
        pkt_count++;
    }

    // 3. Grip Motors
    if (common->valid_flag0 & DS_FLAG0_COMPATIBLE_VIBRATION) {
        uint8_t max_motor = common->motor_left > common->motor_right ? common->motor_left : common->motor_right;
        const vibration_spline_t *cur_spline = (t->curve_type == CURVE_CUSTOM) ? &t->custom_spline : &SPLINE_PRESETS[t->curve_type];
        uint8_t mapped_val = apply_spline_curve(cur_spline, max_motor);
        int boosted_rumble = (int)((float)mapped_val * t->rumble_gain + 0.5f);
        uint8_t final_rumble = clamp_u8(boosted_rumble, 0, 100);

        out_packets[pkt_count][0] = 0x02;
        out_packets[pkt_count][1] = 0x76;
        out_packets[pkt_count][2] = 0x01;
        out_packets[pkt_count][3] = 0x04;
        out_packets[pkt_count][4] = final_rumble;
        out_packets[pkt_count][5] = 0x00;
        out_lens[pkt_count] = 6;
        memcpy(t->bt_rumble_pkt, out_packets[pkt_count], 6);
        pkt_count++;
    }

    return pkt_count;
}
