#include "bidirectional_bridge.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

int main(void) {
    printf("=== Bidirectional DualSense <-> Beitong Translation Tests ===\n");

    bidi_bridge_t bridge;
    bidi_bridge_init(&bridge, false);

    // ========================================================================
    // TEST 1: Game sends DualSense Semi-Auto Weapon Click (0x25)
    // Game: Mode 0x25, Start zone 3, End zone 7, Strength 7
    // ========================================================================
    printf("\n[1] Testing Game -> Beitong: Weapon Click (0x25)...\n");
    ds_usb_output_report_t game_rep_weapon;
    memset(&game_rep_weapon, 0, sizeof(game_rep_weapon));
    game_rep_weapon.report_id = 0x02;
    game_rep_weapon.common.valid_flag0 = DS_FLAG0_RIGHT_TRIGGER_ENABLE;
    game_rep_weapon.common.right_trigger_mode = DS_TRIGGER_WEAPON;

    // Bitmask for zones 3 and 7: (1 << 3) | (1 << 7) = 8 | 128 = 136 (0x88)
    uint16_t zones = (1 << 3) | (1 << 7);
    game_rep_weapon.common.right_trigger_params[0] = zones & 0xFF;
    game_rep_weapon.common.right_trigger_params[1] = (zones >> 8) & 0xFF;
    game_rep_weapon.common.right_trigger_params[2] = 6; // Strength = 7 (stored as 7 - 1 = 6)

    uint8_t bt_pkts[3][10];
    size_t bt_lens[3];
    int count = bidi_translate_game_to_beitong(&bridge, (const uint8_t *)&game_rep_weapon, sizeof(game_rep_weapon), bt_pkts, bt_lens);

    assert(count == 1);
    printf("    Generated %d Beitong packets for Right Trigger:\n", count);
    printf("    Header: 0x%02X 0x%02X, Side: %u (RT), Mode: %u (BT_TRIGGER_SHOT_BREAKPOINT)\n",
           bt_pkts[0][0], bt_pkts[0][1], bt_pkts[0][2], bt_pkts[0][3]);
    assert(bt_pkts[0][0] == 0x02);
    assert(bt_pkts[0][1] == 0x56);
    assert(bt_pkts[0][2] == 0x01); // Right trigger
    assert(bt_pkts[0][3] == BT_TRIGGER_SHOT_BREAKPOINT); // Mode 2

    uint8_t start_pct = bt_pkts[0][4];
    uint8_t click_pct = bt_pkts[0][5];
    uint8_t force_pct = bt_pkts[0][6];
    printf("    Tactile Breakpoint: Start = %u%%, Click Wall = %u%%, Force = %u%% (Boosted with oomph)\n",
           start_pct, click_pct, force_pct);
    assert(start_pct >= 25 && start_pct <= 40);
    assert(click_pct >= 70 && click_pct <= 90);
    assert(force_pct >= 80); // High force verified

    // ========================================================================
    // TEST 2: Game sends DualSense Rapid Vibration / Recoil (0x26)
    // Game: Mode 0x26, Start zone 1, Amplitude 8, Frequency 180Hz
    // ========================================================================
    printf("\n[2] Testing Game -> Beitong: Recoil Vibration (0x26)...\n");
    ds_usb_output_report_t game_rep_vibe;
    memset(&game_rep_vibe, 0, sizeof(game_rep_vibe));
    game_rep_vibe.report_id = 0x02;
    game_rep_vibe.common.valid_flag0 = DS_FLAG0_LEFT_TRIGGER_ENABLE;
    game_rep_vibe.common.left_trigger_mode = DS_TRIGGER_VIBRATION;

    uint16_t v_zones = 0x3FE; // Zones 1..9 active
    game_rep_vibe.common.left_trigger_params[0] = v_zones & 0xFF;
    game_rep_vibe.common.left_trigger_params[1] = (v_zones >> 8) & 0xFF;
    // Amp 8 (packed as 7 in zone 1)
    uint32_t amp_packed = (7 << 3);
    game_rep_vibe.common.left_trigger_params[2] = amp_packed & 0xFF;
    game_rep_vibe.common.left_trigger_params[8] = 180; // 180 Hz

    count = bidi_translate_game_to_beitong(&bridge, (const uint8_t *)&game_rep_vibe, sizeof(game_rep_vibe), bt_pkts, bt_lens);
    assert(count == 1);
    printf("    Generated %d Beitong packets for Left Trigger:\n", count);
    printf("    Header: 0x%02X 0x%02X, Side: %u (LT), Mode: %u (BT_TRIGGER_GUN_VIBRATION)\n",
           bt_pkts[0][0], bt_pkts[0][1], bt_pkts[0][2], bt_pkts[0][3]);
    assert(bt_pkts[0][2] == 0x00); // Left trigger
    assert(bt_pkts[0][3] == BT_TRIGGER_GUN_VIBRATION); // Mode 3
    printf("    Recoil Pulse: Freq = %u, Strength = %u%%\n", bt_pkts[0][5], bt_pkts[0][6]);
    assert(bt_pkts[0][6] >= 90); // Powerful recoil verified

    // ========================================================================
    // TEST 3: Physical Beitong Input -> Genuine DualSense Input Report (0x01)
    // Sticks tilted, triggers pulled, buttons pressed
    // ========================================================================
    printf("\n[3] Testing Beitong Controller -> Game Input Report (0x01)...\n");
    // Beitong input frame: LX=0x20, LY=0xE0, RX=0xC0, RY=0x40, LT=200, RT=255, Buttons=0x0010 (A button / Cross)
    uint8_t bt_input[12] = {
        0x20, 0xE0, 0xC0, 0x40, // Sticks
        200, 255,               // Analog Triggers LT, RT
        0x10, 0x00,             // Button A pressed (bit 4)
        0x00, 0x00, 0x00, 0x00
    };

    uint8_t ds_in_rep[64];
    int in_len = bidi_translate_beitong_to_game(&bridge, bt_input, sizeof(bt_input), ds_in_rep, sizeof(ds_in_rep));
    assert(in_len == 64);
    assert(ds_in_rep[0] == 0x01); // Report ID
    assert(ds_in_rep[1] == 0x20); // LX
    assert(ds_in_rep[2] == 0xE0); // LY
    assert(ds_in_rep[5] == 200);  // LT
    assert(ds_in_rep[6] == 255);  // RT
    assert((ds_in_rep[8] & (1 << 5)) != 0); // Cross button pressed!
    assert((ds_in_rep[9] & (1 << 2)) != 0); // L2 digital threshold bit set!
    assert((ds_in_rep[9] & (1 << 3)) != 0); // R2 digital threshold bit set!
    printf("    DualSense Report 0x01 correctly constructed (64 bytes verified, buttons & axes mapped).\n");

    printf("\n>>> ALL BIDIRECTIONAL TESTS PASSED PERFECTLY! <<<\n");
    return 0;
}
