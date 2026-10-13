#include "ds2bt_engine.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

int main(void) {
    printf("=== DualSense Game -> Beitong Hardware Translation Engine Test ===\n");

    dualsense_to_beitong_t engine;
    ds2bt_init(&engine, false); // USB mode
    ds2bt_set_gains(&engine, 1.45f, 1.50f, 1.35f, 25);

    // TEST 1: Semi-Auto Weapon Click (Sony 0x25 -> Beitong 02 56 01 02)
    ds_usb_output_report_t rep_weapon;
    memset(&rep_weapon, 0, sizeof(rep_weapon));
    rep_weapon.report_id = 0x02;
    rep_weapon.common.valid_flag0 = DS_FLAG0_RIGHT_TRIGGER_ENABLE;
    rep_weapon.common.right_trigger_mode = DS_TRIGGER_WEAPON;
    uint16_t zones = (1 << 3) | (1 << 7); // Start zone 3, break zone 7
    rep_weapon.common.right_trigger_params[0] = zones & 0xFF;
    rep_weapon.common.right_trigger_params[1] = (zones >> 8) & 0xFF;
    rep_weapon.common.right_trigger_params[2] = 6; // Force level 7

    uint8_t bt_pkts[3][10];
    size_t bt_lens[3];
    int count = ds2bt_translate_output_report(&engine, (const uint8_t *)&rep_weapon, sizeof(rep_weapon), bt_pkts, bt_lens);
    assert(count == 1);
    assert(bt_pkts[0][0] == 0x02 && bt_pkts[0][1] == 0x56 && bt_pkts[0][2] == 0x01);
    assert(bt_pkts[0][3] == BT_TRIGGER_SHOT_BREAKPOINT); // Mode 2
    printf("[1] Weapon Click (0x25) -> Beitong Mode 2 Breakpoint: Start=%u%%, End=%u%%, Force=%u%%\n",
           bt_pkts[0][4], bt_pkts[0][5], bt_pkts[0][6]);
    assert(bt_pkts[0][6] >= 85); // Authoritative oomph verified

    // TEST 2: Recoil Vibration (Sony 0x26 -> Beitong 02 56 00 03)
    ds_usb_output_report_t rep_vibe;
    memset(&rep_vibe, 0, sizeof(rep_vibe));
    rep_vibe.report_id = 0x02;
    rep_vibe.common.valid_flag0 = DS_FLAG0_LEFT_TRIGGER_ENABLE;
    rep_vibe.common.left_trigger_mode = DS_TRIGGER_VIBRATION;
    uint16_t v_zones = 0x3FE;
    rep_vibe.common.left_trigger_params[0] = v_zones & 0xFF;
    rep_vibe.common.left_trigger_params[1] = (v_zones >> 8) & 0xFF;
    rep_vibe.common.left_trigger_params[2] = (7 << 3); // Amplitude 8
    rep_vibe.common.left_trigger_params[8] = 180;      // 180 Hz

    count = ds2bt_translate_output_report(&engine, (const uint8_t *)&rep_vibe, sizeof(rep_vibe), bt_pkts, bt_lens);
    assert(count == 1);
    assert(bt_pkts[0][2] == 0x00); // Left trigger
    assert(bt_pkts[0][3] == BT_TRIGGER_GUN_VIBRATION); // Mode 3
    printf("[2] Recoil Pulse (0x26) -> Beitong Mode 3 Recoil: Freq=%u, Amplitude=%u%%\n",
           bt_pkts[0][5], bt_pkts[0][6]);
    assert(bt_pkts[0][6] >= 90); // Solid recoil verified

    // TEST 3: Grip Motors Rumble with 5-point Spline
    ds_usb_output_report_t rep_rumble;
    memset(&rep_rumble, 0, sizeof(rep_rumble));
    rep_rumble.report_id = 0x02;
    rep_rumble.common.valid_flag0 = DS_FLAG0_COMPATIBLE_VIBRATION;
    rep_rumble.common.motor_left = 200;

    count = ds2bt_translate_output_report(&engine, (const uint8_t *)&rep_rumble, sizeof(rep_rumble), bt_pkts, bt_lens);
    assert(count == 1);
    assert(bt_pkts[0][0] == 0x02 && bt_pkts[0][1] == 0x76);
    printf("[3] DualSense Grip Motors -> Beitong Rumble 02 76 01: Value=%u%%\n", bt_pkts[0][4]);
    assert(bt_pkts[0][4] >= 90);

    printf("\nAll One-Way Translation Tests (Game -> Beitong) Passed Successfully!\n");
    return 0;
}
