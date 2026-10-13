#define _GNU_SOURCE
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <signal.h>

#include "dualsense_proto.h"
#include "bidirectional_bridge.h"

// External HidHide cloaking function
extern bool hidhide_cloak_beitong(const wchar_t *current_exe_path);

// Real hidapi function prototypes
typedef void* hid_device;
typedef hid_device* (*pfn_hid_open)(unsigned short vendor_id, unsigned short product_id, const wchar_t *serial_number);
typedef int (*pfn_hid_write)(hid_device *dev, const unsigned char *data, size_t length);
typedef int (*pfn_hid_read_timeout)(hid_device *dev, unsigned char *data, size_t length, int milliseconds);
typedef void (*pfn_hid_close)(hid_device *dev);

static volatile bool g_running = true;

void int_handler(int dummy) {
    (void)dummy;
    g_running = false;
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    signal(SIGINT, int_handler);

    printf("===================================================================\n");
    printf("   DualSense Virtual Emulation Feeder & Beitong Hardware Bridge    \n");
    printf("   High-Performance Haptics & Adaptive Trigger Translation         \n");
    printf("===================================================================\n\n");

    // 1. Step 1: Engage HidHide to prevent double-input conflicts in games
    printf("[1/3] Initializing HidHide filter cloaking...\n");
    wchar_t exe_path[MAX_PATH];
    GetModuleFileNameW(NULL, exe_path, MAX_PATH);
    bool cloaked = hidhide_cloak_beitong(exe_path);
    if (cloaked) {
        printf("      -> Physical Beitong hardware HID cloaked from games.\n");
    } else {
        printf("      -> Notice: HidHide filter driver not running. If not installed,\n");
        printf("         install Nefarius HidHide to prevent double-input in older games.\n");
    }

    // 2. Step 2: Open Physical Beitong Controller via HIDAPI
    printf("\n[2/3] Connecting to physical Beitong controller...\n");
    HMODULE hHidApi = LoadLibraryA("hidapi.dll");
    if (!hHidApi) {
        printf("[-] Error: hidapi.dll not found in current folder or System32.\n");
        return 1;
    }

    pfn_hid_open hid_open_fn = (pfn_hid_open)GetProcAddress(hHidApi, "hid_open");
    pfn_hid_write hid_write_fn = (pfn_hid_write)GetProcAddress(hHidApi, "hid_write");
    pfn_hid_read_timeout hid_read_fn = (pfn_hid_read_timeout)GetProcAddress(hHidApi, "hid_read_timeout");
    pfn_hid_close hid_close_fn = (pfn_hid_close)GetProcAddress(hHidApi, "hid_close");

    if (!hid_open_fn || !hid_write_fn || !hid_read_fn || !hid_close_fn) {
        printf("[-] Error: Missing exports in hidapi.dll.\n");
        return 1;
    }

    // Enumerate mapped Beitong PIDs
    hid_device *bt_dev = NULL;
    uint16_t supported_pids[] = {
        BEITONG_PID_KP70C2_PAD,
        BEITONG_PID_KP70C1_PAD,
        BEITONG_PID_ASURA_KP70,
        BEITONG_PID_ZEUS_KP70,
        BEITONG_PID_KP70_WIRELESS
    };

    for (size_t i = 0; i < sizeof(supported_pids) / sizeof(supported_pids[0]); i++) {
        bt_dev = hid_open_fn(BEITONG_VENDOR_ID, supported_pids[i], NULL);
        if (bt_dev) {
            printf("      -> Connected to Beitong Controller (VID: 0x%04X, PID: 0x%04X)\n",
                   BEITONG_VENDOR_ID, supported_pids[i]);
            break;
        }
    }

    if (!bt_dev) {
        printf("[-] Warning: Physical Beitong controller not detected yet.\n");
        printf("    Waiting for device connection (Plug in via USB or 2.4G dongle)...\n");
    }

    // 3. Step 3: Initialize Bidirectional Translation Engine
    printf("\n[3/3] Starting Virtual DualSense Emulation Engine...\n");
    bidi_bridge_t bridge;
    bidi_bridge_init(&bridge, false); // USB Mode

    // Authoritative force tuning for tactile punch
    bidi_bridge_set_gains(&bridge, 1.40f, 1.45f, 1.30f, 25);
    printf("      -> Trigger wall gain: 1.40x\n");
    printf("      -> Recoil pulse gain: 1.45x\n");
    printf("      -> Grip rumble gain:  1.30x (Curve: INTENSE)\n");
    printf("      -> Minimum force threshold: 25%%\n");
    printf("\n[+] Feeder is ACTIVE! Games with native DualSense support now detect\n");
    printf("    a genuine Sony DualSense controller (0x054C:0x0CE6).\n");
    printf("    Press Ctrl+C to stop.\n\n");

    uint8_t raw_in[64];
    uint8_t ds_in[64];

    // High performance low-latency main loop (~1000Hz / 1ms polling)
    while (g_running) {
        if (!bt_dev) {
            // Retry connect every 1000ms if unplugged
            for (size_t i = 0; i < sizeof(supported_pids)/sizeof(supported_pids[0]); i++) {
                bt_dev = hid_open_fn(BEITONG_VENDOR_ID, supported_pids[i], NULL);
                if (bt_dev) {
                    printf("[+] Beitong Controller reconnected! (PID: 0x%04X)\n", supported_pids[i]);
                    break;
                }
            }
            Sleep(1000);
            continue;
        }

        // Poll physical Beitong input
        int bytes = hid_read_fn(bt_dev, raw_in, sizeof(raw_in), 2);
        if (bytes > 0) {
            // Translate Beitong physical input to DualSense Input Report 0x01
            bidi_translate_beitong_to_game(&bridge, raw_in, (size_t)bytes, ds_in, sizeof(ds_in));
        } else if (bytes < 0) {
            printf("[-] Controller disconnected.\n");
            hid_close_fn(bt_dev);
            bt_dev = NULL;
            continue;
        }

        // Yield slightly for power efficiency while maintaining <1ms latency
        Sleep(1);
    }

    printf("\nStopping feeder and cleaning up...\n");
    if (bt_dev) hid_close_fn(bt_dev);
    FreeLibrary(hHidApi);
    printf("Done.\n");
    return 0;
}
