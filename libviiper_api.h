#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// libVIIPER Pure C API Declarations (as used in DualSenseClient)
// ============================================================================

typedef uintptr_t USBServerHandle;
typedef uintptr_t DSDeviceHandle;

typedef struct {
    const char *addr; // e.g. "localhost:3245"
} USBServerConfig;

typedef enum {
    VIIPER_LOG_DEBUG = -4,
    VIIPER_LOG_INFO  = 0,
    VIIPER_LOG_WARN  = 4,
    VIIPER_LOG_ERROR = 8
} VIIPERLogLevel;

typedef void (*VIIPERLogCallback)(VIIPERLogLevel level, const char *msg);

// Raw input state pushed from physical controller to game
typedef struct {
    int8_t   LX;
    int8_t   LY;
    int8_t   RX;
    int8_t   RY;
    uint32_t Buttons;
    uint8_t  DPad;
    uint8_t  L2;
    uint8_t  R2;
    uint16_t Touch1X;
    uint16_t Touch1Y;
    uint8_t  Touch1Active;
    uint16_t Touch2X;
    uint16_t Touch2Y;
    uint8_t  Touch2Active;
    int16_t  GyroX;
    int16_t  GyroY;
    int16_t  GyroZ;
    int16_t  AccelX;
    int16_t  AccelY;
    int16_t  AccelZ;
} DSDeviceState;

typedef struct {
    const char* SerialNumber;
    const char* MACAddress;
    const char* Board;
    uint8_t     BatteryStatus;
    double      TemperatureCelsius;
    double      BatteryVoltage;
    const char* ShellColor;
} DSMetaState;

// Callback fired when game sends output report (rumble / triggers / LEDs)
typedef void (*DSOutputCallback)(
    DSDeviceHandle handle,
    uint8_t rumbleSmall,
    uint8_t rumbleLarge,
    uint8_t ledRed,
    uint8_t ledGreen,
    uint8_t ledBlue,
    uint8_t playerLeds
);

// Extended callback signature for native 0x02 Output Report streams (hbashton/VIIPER & DualSenseClient)
typedef void (*DSExtendedOutputCallback)(
    DSDeviceHandle handle,
    const uint8_t *reportData,
    size_t reportLength
);

// Function pointer signatures for dynamic loading of libVIIPER.dll
typedef bool (*pfn_NewUSBServer)(const USBServerConfig *conf, USBServerHandle *serverHandle, VIIPERLogCallback logCb);
typedef bool (*pfn_CloseUSBServer)(USBServerHandle serverHandle);
typedef bool (*pfn_CreateUSBBus)(USBServerHandle serverHandle, uint32_t *busID);
typedef bool (*pfn_CreateDualSenseDevice)(USBServerHandle serverHandle, DSDeviceHandle *deviceHandle, uint32_t busID, bool autoAttach, const DSMetaState *meta);
typedef bool (*pfn_SetDualSenseDeviceState)(DSDeviceHandle handle, const DSDeviceState *state);
typedef bool (*pfn_SetDualSenseOutputCallback)(DSDeviceHandle handle, DSOutputCallback cb);
typedef bool (*pfn_SetDualSenseExtendedOutputCallback)(DSDeviceHandle handle, DSExtendedOutputCallback cb);
typedef bool (*pfn_RemoveDualSenseDevice)(DSDeviceHandle handle);

#ifdef __cplusplus
}
#endif
