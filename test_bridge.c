#include "bridge_core.h"
#include <stdio.h>
#include <assert.h>

int main(void) {
    printf("=== DualSense High-Performance Translation Bridge Tests ===\n");

    dualsense_bridge_t bridge_usb;
    dualsense_bridge_t bridge_bt;
    bridge_init(&bridge_usb, false);
    bridge_init(&bridge_bt, true);

    uint8_t out_buf[128];

    // 1. Test Weapon Click (Mode 2) with Punch/Boost
    // Beitong input: start 20%, end 70%, force 75%
    uint8_t bt_shot[10] = { 0x02, 0x56, 0x01, 0x02, 20, 70, 75, 50, 0, 0 };
    int len = bridge_translate_packet(&bridge_usb, bt_shot, sizeof(bt_shot), out_buf, sizeof(out_buf));
    assert(len == 64);
    ds_usb_output_report_t *rep_usb = (ds_usb_output_report_t *)out_buf;
    printf("[1] USB Report generated: %d bytes\n", len);
    printf("    RT Mode: 0x%02X (Expected DS_TRIGGER_WEAPON 0x25)\n", rep_usb->common.right_trigger_mode);
    assert(rep_usb->common.right_trigger_mode == DS_TRIGGER_WEAPON);

    // Check strength (boosted by trigger_force_gain = 1.35x)
    uint8_t str_param = rep_usb->common.right_trigger_params[2];
    printf("    Trigger snap strength packed: %u (0-7 scale)\n", str_param);
    assert(str_param >= 6); // Verifying high strength/oomph

    // 2. Test Recoil Vibration (Mode 3) over Bluetooth with Seeded CRC32
    // Beitong input: start 10%, freq 85%, amplitude 80%
    uint8_t bt_gun[10] = { 0x02, 0x56, 0x00, 0x03, 10, 85, 80, 0, 0, 0 };
    len = bridge_translate_packet(&bridge_bt, bt_gun, sizeof(bt_gun), out_buf, sizeof(out_buf));
    assert(len == 78);
    ds_bt_output_report_t *rep_bt = (ds_bt_output_report_t *)out_buf;
    printf("[2] Bluetooth Report generated: %d bytes\n", len);
    printf("    LT Mode: 0x%02X (Expected DS_TRIGGER_VIBRATION 0x26)\n", rep_bt->common.left_trigger_mode);
    assert(rep_bt->common.left_trigger_mode == DS_TRIGGER_VIBRATION);
    printf("    Bluetooth Tag: 0x%02X, CRC32: 0x%08X\n", rep_bt->tag, rep_bt->crc32);
    assert(rep_bt->tag == 0x10);
    assert(rep_bt->crc32 != 0);

    // 3. Test Grip Motor Spline Curve & Boost
    // Beitong input: subcmd 4 (Right rumble), value 50%
    uint8_t bt_rumble[6] = { 0x02, 0x76, 0x01, 0x04, 50, 0x00 };
    len = bridge_translate_packet(&bridge_usb, bt_rumble, sizeof(bt_rumble), out_buf, sizeof(out_buf));
    rep_usb = (ds_usb_output_report_t *)out_buf;
    printf("[3] Grip Motor Rumble Output: %u / 255 (Substantial torque mapped)\n", rep_usb->common.motor_right);
    assert(rep_usb->common.motor_right >= 140);

    printf("\nAll DualSense Haptic Bridge Tests Passed Successfully!\n");
    return 0;
}
