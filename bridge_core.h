#pragma once

#include "dualsense_proto.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Haptic & Vibration Curve Presets (from Betop VIBRATION_MAP)
typedef enum {
    CURVE_LINEAR    = 0, // Uniform 1:1 progression
    CURVE_INTENSE   = 1, // Steep ramp, high torque and immediate hit
    CURVE_GENTLE    = 2, // Damped, smooth gradual buildup
    CURVE_CUSTOM    = 3  // User-defined 5-point spline
} curve_type_t;

// 5-point coordinate sensitivity spline
typedef struct {
    uint8_t x[5];
    uint8_t y[5];
} vibration_spline_t;

// Complete State & Tuning for the Bridge
typedef struct {
    bool is_bluetooth;
    uint8_t bt_sequence;

    // Physical Trigger Calibration & Deadzones (0-255 raw -> 0-100% normalized)
    uint8_t lt_deadzone_start;
    uint8_t lt_deadzone_end;
    uint8_t rt_deadzone_start;
    uint8_t rt_deadzone_end;

    // Haptic & Recoil Force Multipliers (Default 1.25x - 1.5x for authoritative "oomph")
    float trigger_force_gain;   // Multiplier for resistance / tactile walls (default: 1.35f)
    float trigger_vibe_gain;    // Multiplier for burst recoil amplitude (default: 1.40f)
    float rumble_gain;          // Multiplier for grip motors (default: 1.25f)
    uint8_t min_force_floor;    // Never drop active effects below physical tactile threshold (default: 2)

    // Spline curve profile
    curve_type_t curve_type;
    vibration_spline_t custom_spline;

    // Cross-motor sync (sync_motor)
    bool sync_motor_with_grip;

    // Active state buffers
    uint8_t motor_right;
    uint8_t motor_left;
    uint8_t rt_mode;
    uint8_t rt_params[10];
    uint8_t lt_mode;
    uint8_t lt_params[10];
} dualsense_bridge_t;

// Initialization with full mechanical calibration & authoritative force profile
void bridge_init(dualsense_bridge_t *b, bool is_bluetooth);

// Customization methods
void bridge_set_gains(dualsense_bridge_t *b, float force_gain, float vibe_gain, float rumble_gain);
void bridge_set_curve(dualsense_bridge_t *b, curve_type_t ctype, const vibration_spline_t *custom);
void bridge_set_deadzones(dualsense_bridge_t *b, uint8_t lt_start, uint8_t lt_end, uint8_t rt_start, uint8_t rt_end);

// Translates incoming Beitong HID buffer (Report 0x02) into DualSense Output Report
// Returns length of output report (64 for USB, 78 for BT), or 0 if ignored.
int bridge_translate_packet(dualsense_bridge_t *b, const uint8_t *in_data, size_t in_len, uint8_t *out_data, size_t out_max_len);

// Build current DualSense report without new input
int bridge_build_current_report(dualsense_bridge_t *b, uint8_t *out_data, size_t out_max_len);

// Seeded CRC32 calculation for Bluetooth packets
uint32_t dualsense_crc32(uint8_t seed, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
