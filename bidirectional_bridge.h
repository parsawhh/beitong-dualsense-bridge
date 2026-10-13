#pragma once

#include "dualsense_proto.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Haptic Splines & Gain Tuning
// ============================================================================
typedef enum {
    CURVE_LINEAR    = 0,
    CURVE_INTENSE   = 1, // Steep ramp, heavy punch early
    CURVE_GENTLE    = 2,
    CURVE_CUSTOM    = 3
} haptic_curve_t;

typedef struct {
    uint8_t x[5];
    uint8_t y[5];
} haptic_spline_t;

// ============================================================================
// Bidirectional State & Configuration
// ============================================================================
typedef struct {
    // Mode
    bool bluetooth_mode;
    uint8_t bt_sequence;

    // Force / Recoil / Rumble Multipliers ("Oomph" tuning)
    float trigger_force_gain;   // Default: 1.40f (amplifies resistance & breakpoint wall)
    float trigger_vibe_gain;    // Default: 1.45f (amplifies recoil pulse amplitude)
    float grip_rumble_gain;     // Default: 1.30f (amplifies grip motors)
    uint8_t min_force_floor;    // Default: 20 (out of 100%, ensures crisp non-mushy tactile wall)

    // Haptic response curve
    haptic_curve_t curve_type;
    haptic_spline_t custom_spline;

    // Cross-motor coupling
    bool sync_trigger_with_grip;

    // Physical calibration
    uint8_t lt_deadzone;
    uint8_t rt_deadzone;

    // Cached Game -> Beitong Output State (packets ready to send to Beitong controller)
    uint8_t last_bt_lt_pkt[10]; // 02 56 00 <mode> <params...>
    uint8_t last_bt_rt_pkt[10]; // 02 56 01 <mode> <params...>
    uint8_t last_bt_rumble[6];  // 02 76 01 <subcmd> <val>

    // Cached Controller -> Game Input State (Report 0x01 ready for game read)
    uint8_t ds_input_report[64];
} bidi_bridge_t;

// Initialize bridge
void bidi_bridge_init(bidi_bridge_t *b, bool bluetooth_mode);

// Set gain levels
void bidi_bridge_set_gains(bidi_bridge_t *b, float force_gain, float vibe_gain, float rumble_gain, uint8_t min_floor);

// ----------------------------------------------------------------------------
// 1. GAME -> BEITONG TRANSLATION (Adaptive Triggers & Haptics)
// ----------------------------------------------------------------------------
// Translates a DualSense Output Report (0x02 USB or 0x31 BT) sent by the game
// into Beitong HID command frames (02 56 trigger effects and 02 76 rumble).
// Returns number of Beitong packets generated (0 to 3: LT, RT, Rumble).
// Generated packets are written into out_packets array (each buffer must be >= 10 bytes).
int bidi_translate_game_to_beitong(bidi_bridge_t *b,
                                  const uint8_t *ds_report,
                                  size_t ds_len,
                                  uint8_t out_packets[3][10],
                                  size_t out_lens[3]);

// ----------------------------------------------------------------------------
// 2. BEITONG -> GAME TRANSLATION (Controller Inputs -> DualSense Input Report)
// ----------------------------------------------------------------------------
// Translates physical Beitong stick/button/trigger inputs into a genuine
// 64-byte Sony DualSense Input Report (Report ID 0x01).
// Returns 64 on success.
int bidi_translate_beitong_to_game(bidi_bridge_t *b,
                                  const uint8_t *bt_input_data,
                                  size_t bt_len,
                                  uint8_t *out_ds_input_report,
                                  size_t out_max_len);

// Helper for Bluetooth CRC32
uint32_t bidi_dualsense_crc32(uint8_t seed, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
