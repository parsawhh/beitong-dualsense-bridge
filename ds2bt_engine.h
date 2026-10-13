#pragma once

#include "dualsense_proto.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Haptic & Vibration Curve Presets (from Betop VIBRATION_MAP)
// ============================================================================
typedef enum {
    CURVE_LINEAR    = 0, // Uniform 1:1 progression
    CURVE_INTENSE   = 1, // Steep ramp, high torque and immediate hit
    CURVE_GENTLE    = 2, // Damped, smooth gradual buildup
    CURVE_CUSTOM    = 3  // User-defined 5-point spline
} curve_type_t;

typedef struct {
    uint8_t x[5];
    uint8_t y[5];
} vibration_spline_t;

// Complete Translation Engine State: Game DualSense -> Beitong Hardware
typedef struct {
    // Mode
    bool bluetooth_mode;
    uint8_t bt_sequence;

    // Haptic & Recoil Force Multipliers (User-requested "Oomph" tuning)
    float trigger_force_gain;   // Multiplier for resistance / tactile walls (default: 1.45f)
    float trigger_vibe_gain;    // Multiplier for burst recoil amplitude (default: 1.50f)
    float rumble_gain;          // Multiplier for grip motors (default: 1.35f)
    uint8_t min_force_floor;    // Minimum force floor so walls never feel mushy (default: 25%)

    // Spline curve profile
    curve_type_t curve_type;
    vibration_spline_t custom_spline;

    // Cached Game Output State
    uint8_t last_ds_rt_mode;
    uint8_t last_ds_rt_params[10];
    uint8_t last_ds_lt_mode;
    uint8_t last_ds_lt_params[10];

    // Translated Beitong Packets ready to write to hardware
    uint8_t bt_rt_pkt[10];
    uint8_t bt_lt_pkt[10];
    uint8_t bt_rumble_pkt[6];
} dualsense_to_beitong_t;

// Initialize translation engine
void ds2bt_init(dualsense_to_beitong_t *t, bool bluetooth_mode);

// Set gain levels for maximum force / oomph
void ds2bt_set_gains(dualsense_to_beitong_t *t, float force_gain, float vibe_gain, float rumble_gain, uint8_t min_floor);

// Set haptic curve
void ds2bt_set_curve(dualsense_to_beitong_t *t, curve_type_t ctype, const vibration_spline_t *custom);

// Main Translation Pipeline:
// Game sends DualSense Output Report (0x02 USB or 0x31 BT) ->
// Engine generates Beitong HID Output frames (02 56 trigger effects and 02 76 rumble)
// Returns number of frames generated (0 to 3: RT, LT, Rumble)
int ds2bt_translate_output_report(dualsense_to_beitong_t *t,
                                  const uint8_t *ds_report,
                                  size_t ds_len,
                                  uint8_t out_packets[3][10],
                                  size_t out_lens[3]);

#ifdef __cplusplus
}
#endif
