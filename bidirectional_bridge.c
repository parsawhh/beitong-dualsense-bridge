#include "bidirectional_bridge.h"
#include <string.h>

static const haptic_spline_t SPLINE_PRESETS[3] = {
    // 0: Linear
    { .x = {42, 84, 128, 167, 255}, .y = {42, 84, 128, 167, 255} },
    // 1: Intense / Hard
    { .x = {2, 60, 120, 190, 255},  .y = {40, 140, 180, 235, 255} },
    // 2: Gentle / Soft
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

static uint8_t apply_spline(const haptic_spline_t *spline, uint8_t input_val) {
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

uint32_t bidi_dualsense_crc32(uint8_t seed, const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    crc ^= seed;
    for (int b = 0; b < 8; b++) crc = (crc & 1) ? ((crc >> 1) ^ 0xEDB88320) : (crc >> 1);
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) crc = (crc & 1) ? ((crc >> 1) ^ 0xEDB88320) : (crc >> 1);
    }
    return ~crc;
}

void bidi_bridge_init(bidi_bridge_t *b, bool bluetooth_mode) {
    memset(b, 0, sizeof(*b));
    b->bluetooth_mode = bluetooth_mode;
    b->bt_sequence = 0;

    // High performance authoritative force profile
    b->trigger_force_gain = 1.40f;
    b->trigger_vibe_gain  = 1.45f;
    b->grip_rumble_gain   = 1.30f;
    b->min_force_floor    = 20; // 20% minimum floor for authoritative resistance

    b->curve_type = CURVE_INTENSE;
    b->sync_trigger_with_grip = false;
    b->lt_deadzone = 25;
    b->rt_deadzone = 25;

    // Default neutral DualSense Input Report
    b->ds_input_report[0] = 0x01; // Report ID
    b->ds_input_report[1] = 0x80; // Left Stick X center
    b->ds_input_report[2] = 0x80; // Left Stick Y center
    b->ds_input_report[3] = 0x80; // Right Stick X center
    b->ds_input_report[4] = 0x80; // Right Stick Y center
    b->ds_input_report[8] = 0x08; // D-pad neutral
}

void bidi_bridge_set_gains(bidi_bridge_t *b, float force_gain, float vibe_gain, float rumble_gain, uint8_t min_floor) {
    if (force_gain > 0.1f) b->trigger_force_gain = force_gain;
    if (vibe_gain > 0.1f)  b->trigger_vibe_gain = vibe_gain;
    if (rumble_gain > 0.1f) b->grip_rumble_gain = rumble_gain;
    b->min_force_floor = min_floor;
}

// ----------------------------------------------------------------------------
// GAME -> BEITONG TRANSLATION
// ----------------------------------------------------------------------------
static void translate_ds_trigger_block_to_bt(bidi_bridge_t *b, uint8_t side, uint8_t ds_mode, const uint8_t *ds_params,
                                             uint8_t *out_pkt, size_t *out_len) {
    memset(out_pkt, 0, 10);
    out_pkt[0] = 0x02; // Report ID
    out_pkt[1] = 0x56; // Trigger command
    out_pkt[2] = side; // 0 = LT, 1 = RT

    // Mode 0x05 or zero mode: OFF
    if (ds_mode == DS_TRIGGER_OFF || ds_mode == 0) {
        out_pkt[3] = BT_TRIGGER_OFF;
        *out_len = 9;
        return;
    }

    // 1. Sony FEEDBACK (0x21) / BOW (0x22) -> Beitong Damping (BT_TRIGGER_RACE_RESISTANCE = 1)
    if (ds_mode == DS_TRIGGER_FEEDBACK || ds_mode == DS_TRIGGER_BOW) {
        out_pkt[3] = BT_TRIGGER_RACE_RESISTANCE;

        // Extract active start zone from bitmask (bytes 0..1)
        uint16_t active_zones = (uint16_t)ds_params[0] | ((uint16_t)ds_params[1] << 8);
        int start_zone = 0;
        for (int z = 0; z < 10; z++) {
            if (active_zones & (1 << z)) {
                start_zone = z;
                break;
            }
        }
        uint8_t bt_start = (uint8_t)scale_range(start_zone, 0, 9, 0, 100);

        // Extract 3-bit strength from force_zones (bytes 2..5)
        uint32_t force_zones = (uint32_t)ds_params[2] | ((uint32_t)ds_params[3] << 8) |
                               ((uint32_t)ds_params[4] << 16) | ((uint32_t)ds_params[5] << 24);
        uint8_t ds_strength = ((force_zones >> (3 * start_zone)) & 0x07) + 1; // 1..8
        int bt_raw_force = scale_range(ds_strength, 1, 8, 15, 100);

        // Apply gain & floor
        int boosted_force = (int)((float)bt_raw_force * b->trigger_force_gain + 0.5f);
        if (boosted_force < b->min_force_floor) boosted_force = b->min_force_floor;
        uint8_t bt_force = clamp_u8(boosted_force, 0, 100);

        out_pkt[4] = bt_start;
        out_pkt[5] = bt_force;
        out_pkt[6] = 0; // Padding
        *out_len = 9;
        return;
    }

    // 2. Sony WEAPON (0x25) -> Beitong Tactile Breakpoint (BT_TRIGGER_SHOT_BREAKPOINT = 2)
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

        int bt_raw_force = scale_range(ds_str, 1, 8, 25, 100);
        int boosted_force = (int)((float)bt_raw_force * b->trigger_force_gain + 0.5f);
        if (boosted_force < b->min_force_floor) boosted_force = b->min_force_floor;
        uint8_t bt_force = clamp_u8(boosted_force, 0, 100);

        out_pkt[4] = bt_start;
        out_pkt[5] = bt_click;
        out_pkt[6] = bt_force;
        out_pkt[7] = bt_force / 2; // Return spring strength
        out_pkt[8] = 1; // Telemetry flag
        *out_len = 9;
        return;
    }

    // 3. Sony VIBRATION (0x26) / MACHINE (0x27) / GALLOPING (0x23) -> Beitong Recoil Pulse (BT_TRIGGER_GUN_VIBRATION = 3)
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
        uint8_t ds_freq = ds_params[8]; // Hertz rate

        int bt_raw_amp = scale_range(ds_amp, 1, 8, 20, 100);
        int boosted_amp = (int)((float)bt_raw_amp * b->trigger_vibe_gain + 0.5f);
        if (boosted_amp < b->min_force_floor) boosted_amp = b->min_force_floor;
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

int bidi_translate_game_to_beitong(bidi_bridge_t *b,
                                  const uint8_t *ds_report,
                                  size_t ds_len,
                                  uint8_t out_packets[3][10],
                                  size_t out_lens[3]) {
    if (!ds_report || ds_len < 32) return 0;

    const ds_output_common_t *common = NULL;

    // Report ID 0x02 (USB) or 0x31 (Bluetooth)
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
        translate_ds_trigger_block_to_bt(b, 1, common->right_trigger_mode, common->right_trigger_params,
                                         out_packets[pkt_count], &out_lens[pkt_count]);
        memcpy(b->last_bt_rt_pkt, out_packets[pkt_count], 10);
        pkt_count++;
    }

    // 2. Left Trigger
    if (common->valid_flag0 & DS_FLAG0_LEFT_TRIGGER_ENABLE) {
        translate_ds_trigger_block_to_bt(b, 0, common->left_trigger_mode, common->left_trigger_params,
                                         out_packets[pkt_count], &out_lens[pkt_count]);
        memcpy(b->last_bt_lt_pkt, out_packets[pkt_count], 10);
        pkt_count++;
    }

    // 3. Grip Rumble (motor_right / motor_left)
    if (common->valid_flag0 & DS_FLAG0_COMPATIBLE_VIBRATION) {
        uint8_t max_motor = common->motor_left > common->motor_right ? common->motor_left : common->motor_right;
        const haptic_spline_t *cur_spline = (b->curve_type == CURVE_CUSTOM) ? &b->custom_spline : &SPLINE_PRESETS[b->curve_type];
        uint8_t mapped_val = apply_spline(cur_spline, max_motor);
        int boosted_rumble = (int)((float)mapped_val * b->grip_rumble_gain + 0.5f);
        uint8_t final_rumble = clamp_u8(boosted_rumble, 0, 100); // Beitong scale 0-100%

        out_packets[pkt_count][0] = 0x02;
        out_packets[pkt_count][1] = 0x76;
        out_packets[pkt_count][2] = 0x01;
        out_packets[pkt_count][3] = 0x04; // CMD 4: Group A rumble
        out_packets[pkt_count][4] = final_rumble;
        out_packets[pkt_count][5] = 0x00;
        out_lens[pkt_count] = 6;
        memcpy(b->last_bt_rumble, out_packets[pkt_count], 6);
        pkt_count++;
    }

    return pkt_count;
}

// ----------------------------------------------------------------------------
// BEITONG -> GAME TRANSLATION (Report 0x01 Input)
// ----------------------------------------------------------------------------
int bidi_translate_beitong_to_game(bidi_bridge_t *b,
                                  const uint8_t *bt_input_data,
                                  size_t bt_len,
                                  uint8_t *out_ds_input_report,
                                  size_t out_max_len) {
    if (out_max_len < 64) return 0;
    if (!bt_input_data || bt_len < 12) return 0;

    // Standard DualSense Input Report (Report ID 0x01)
    uint8_t rep[64];
    memset(rep, 0, sizeof(rep));
    rep[0] = 0x01; // Report ID

    // Map Sticks (Beitong bytes 0..3 LX, LY, RX, RY centered at 0x80)
    rep[1] = bt_input_data[0]; // LX
    rep[2] = bt_input_data[1]; // LY
    rep[3] = bt_input_data[2]; // RX
    rep[4] = bt_input_data[3]; // RY

    // Map Analog Triggers (LT at bt[4], RT at bt[5])
    rep[5] = bt_input_data[4]; // LT (0-255)
    rep[6] = bt_input_data[5]; // RT (0-255)

    // Map Sequence Counter
    rep[7] = (uint8_t)(b->bt_sequence++);

    // Map Buttons (Beitong buttons bitmask at bt[6..7])
    // DualSense Byte 8:
    // Bits 0-3: D-pad (0=N, 1=NE, 2=E, 3=SE, 4=S, 5=SW, 6=W, 7=NW, 8=None)
    // Bit 4: Square (X on Xbox/Beitong)
    // Bit 5: Cross (A)
    // Bit 6: Circle (B)
    // Bit 7: Triangle (Y)
    uint16_t bt_btns = (uint16_t)bt_input_data[6] | ((uint16_t)bt_input_data[7] << 8);

    uint8_t dpad = 8; // Neutral
    bool up    = (bt_btns & (1 << 0)) != 0;
    bool down  = (bt_btns & (1 << 1)) != 0;
    bool left  = (bt_btns & (1 << 2)) != 0;
    bool right = (bt_btns & (1 << 3)) != 0;

    if (up && right) dpad = 1;
    else if (down && right) dpad = 3;
    else if (down && left) dpad = 5;
    else if (up && left) dpad = 7;
    else if (up) dpad = 0;
    else if (right) dpad = 2;
    else if (down) dpad = 4;
    else if (left) dpad = 6;

    rep[8] = dpad & 0x0F;
    if (bt_btns & (1 << 6)) rep[8] |= (1 << 4); // X -> Square
    if (bt_btns & (1 << 4)) rep[8] |= (1 << 5); // A -> Cross
    if (bt_btns & (1 << 5)) rep[8] |= (1 << 6); // B -> Circle
    if (bt_btns & (1 << 7)) rep[8] |= (1 << 7); // Y -> Triangle

    // Byte 9:
    // Bit 0: L1 (LB)
    // Bit 1: R1 (RB)
    // Bit 2: L2 (Digital threshold)
    // Bit 3: R2 (Digital threshold)
    // Bit 4: Create / Share (Back)
    // Bit 5: Options (Start)
    // Bit 6: L3 (LS)
    // Bit 7: R3 (RS)
    if (bt_btns & (1 << 8))  rep[9] |= (1 << 0); // LB -> L1
    if (bt_btns & (1 << 9))  rep[9] |= (1 << 1); // RB -> R1
    if (rep[5] > 0x10)       rep[9] |= (1 << 2); // L2
    if (rep[6] > 0x10)       rep[9] |= (1 << 3); // R2
    if (bt_btns & (1 << 10)) rep[9] |= (1 << 4); // Back -> Share
    if (bt_btns & (1 << 11)) rep[9] |= (1 << 5); // Start -> Options
    if (bt_btns & (1 << 12)) rep[9] |= (1 << 6); // LS -> L3
    if (bt_btns & (1 << 13)) rep[9] |= (1 << 7); // RS -> R3

    // Byte 10: PS button & Touchpad click
    if (bt_btns & (1 << 14)) rep[10] |= (1 << 0); // Home -> PS

    // Battery full state
    rep[53] = 0x0A; // 100% battery, discharging

    memcpy(out_ds_input_report, rep, 64);
    memcpy(b->ds_input_report, rep, 64);
    return 64;
}
