#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// DualSense Hardware & Identity Constants
// ============================================================================
#define SONY_VENDOR_ID                   0x054C
#define DUALSENSE_PRODUCT_ID             0x0CE6
#define DUALSENSE_EDGE_PRODUCT_ID        0x0DF2

#define BEITONG_VENDOR_ID                0x20BC
#define BEITONG_ALTER_VENDOR_ID          0x20DD

#define BEITONG_PID_ZEUS_KP70            0x5125
#define BEITONG_PID_ASURA_KP70           0x5126
#define BEITONG_PID_KP70_WIRELESS        0x5128
#define BEITONG_PID_KP70C1_PAD           0x518A
#define BEITONG_PID_KP70C1_DONGLE        0x518B
#define BEITONG_PID_KP70C2_PAD           0x518C
#define BEITONG_PID_KP70C2_DONGLE        0x518D

// ============================================================================
// Official Sony DualSense Firmware Trigger Modes (Report 0x02 / 0x31)
// ============================================================================
typedef enum {
    DS_TRIGGER_OFF                       = 0x05, // Resets stops to neutral
    DS_TRIGGER_FEEDBACK                  = 0x21, // Continuous resistance curve
    DS_TRIGGER_BOW                       = 0x22, // Tension build + snap-back
    DS_TRIGGER_GALLOPING                 = 0x23, // Rhythmic step oscillation
    DS_TRIGGER_WEAPON                    = 0x25, // Semi-auto weapon click & sudden release
    DS_TRIGGER_VIBRATION                 = 0x26, // High-frequency cycling vibration
    DS_TRIGGER_MACHINE                   = 0x27  // Dual-frequency mechanical rumble
} ds_trigger_mode_t;

// ============================================================================
// DualSense Output Report Valid Flags
// ============================================================================
#define DS_FLAG0_COMPATIBLE_VIBRATION    (1 << 0) // Motor right / left rumble
#define DS_FLAG0_HAPTICS_SELECT          (1 << 1)
#define DS_FLAG0_RIGHT_TRIGGER_ENABLE    (1 << 2) // Right adaptive trigger
#define DS_FLAG0_LEFT_TRIGGER_ENABLE     (1 << 3) // Left adaptive trigger
#define DS_FLAG0_HEADPHONE_VOLUME        (1 << 4)
#define DS_FLAG0_SPEAKER_VOLUME          (1 << 5)
#define DS_FLAG0_MIC_VOLUME              (1 << 6)
#define DS_FLAG0_AUDIO_CONTROL           (1 << 7)

#define DS_FLAG1_MIC_MUTE_LED            (1 << 0)
#define DS_FLAG1_POWER_SAVE              (1 << 1)
#define DS_FLAG1_LIGHTBAR_CONTROL        (1 << 2)
#define DS_FLAG1_RELEASE_LEDS            (1 << 3)
#define DS_FLAG1_PLAYER_LEDS             (1 << 4)

#define DS_OUTPUT_TAG_BT                 0x10
#define DS_BT_CRC_SEED                   0xA2

// ============================================================================
// Beitong Adaptive Trigger Protocol
// ============================================================================
// Format: 02 56 <side> <mode> <params...>
// side: 0x00 = Left Trigger (LT), 0x01 = Right Trigger (RT)
typedef enum {
    BT_TRIGGER_OFF                       = 0x00,
    BT_TRIGGER_RACE_RESISTANCE           = 0x01, // Continuous damping (truck, kart, sports, lock)
    BT_TRIGGER_SHOT_BREAKPOINT           = 0x02, // Two-stage tactile wall (pistol, shotgun, sniper)
    BT_TRIGGER_GUN_VIBRATION             = 0x03  // Recoil pulsing (SMG, MG, rifle, bow, charge)
} bt_trigger_mode_t;

// ============================================================================
// DualSense USB & Bluetooth Output Report Structs
// ============================================================================
#pragma pack(push, 1)

typedef struct {
    uint8_t valid_flag0;
    uint8_t valid_flag1;
    uint8_t motor_right;                 // High frequency / light grip rumble (0-255)
    uint8_t motor_left;                  // Low frequency / heavy grip rumble (0-255)
    uint8_t headphone_volume;
    uint8_t speaker_volume;
    uint8_t internal_mic_volume;
    uint8_t audio_flags;
    uint8_t mute_button_led;
    uint8_t power_save_control;

    // Right Trigger Motor Config (11 bytes: 1 mode + 10 params)
    uint8_t right_trigger_mode;
    uint8_t right_trigger_params[10];

    // Left Trigger Motor Config (11 bytes: 1 mode + 10 params)
    uint8_t left_trigger_mode;
    uint8_t left_trigger_params[10];

    uint32_t host_timestamp;
    uint8_t reduce_motor_power;
    uint8_t audio_flags2;
    uint8_t valid_flag2;
    uint8_t haptics_flags;
    uint8_t reserved3;
    uint8_t lightbar_setup;
    uint8_t led_brightness;
    uint8_t player_leds;
    uint8_t lightbar_red;
    uint8_t lightbar_green;
    uint8_t lightbar_blue;
} ds_output_common_t;

typedef struct {
    uint8_t report_id;                   // 0x02
    ds_output_common_t common;
    uint8_t padding[16];                 // Pads to 64 bytes total
} ds_usb_output_report_t;

typedef struct {
    uint8_t report_id;                   // 0x31
    uint8_t seq_tag;                     // (seq << 4) | 0x00
    uint8_t tag;                         // 0x10
    ds_output_common_t common;
    uint8_t padding[24];
    uint32_t crc32;                      // 0xA2 Seeded CRC32
} ds_bt_output_report_t;

#pragma pack(pop)

#ifdef __cplusplus
}
#endif
