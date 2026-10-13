#define _GNU_SOURCE
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include "dualsense_proto.h"
#include "bidirectional_bridge.h"

// ============================================================================
// Windows In-Process Proxy DLL for hidapi.dll
// Dropped directly into the game folder (Cyberpunk, Spider-Man, Death Stranding, etc.)
// ============================================================================

typedef void* hid_device;

struct hid_device_info {
    char *path;
    unsigned short vendor_id;
    unsigned short product_id;
    wchar_t *serial_number;
    unsigned short release_number;
    wchar_t *manufacturer_string;
    wchar_t *product_string;
    unsigned short usage_page;
    unsigned short usage;
    int interface_number;
    struct hid_device_info *next;
};

// Function pointer signatures matching real hidapi.dll
typedef struct hid_device_info* (*pfn_hid_enumerate)(unsigned short vendor_id, unsigned short product_id);
typedef void (*pfn_hid_free_enumeration)(struct hid_device_info *devs);
typedef hid_device* (*pfn_hid_open)(unsigned short vendor_id, unsigned short product_id, const wchar_t *serial_number);
typedef hid_device* (*pfn_hid_open_path)(const char *path);
typedef int (*pfn_hid_write)(hid_device *dev, const unsigned char *data, size_t length);
typedef int (*pfn_hid_read_timeout)(hid_device *dev, unsigned char *data, size_t length, int milliseconds);
typedef int (*pfn_hid_read)(hid_device *dev, unsigned char *data, size_t length);
typedef void (*pfn_hid_close)(hid_device *dev);
typedef int (*pfn_hid_init)(void);
typedef int (*pfn_hid_exit)(void);

static HMODULE g_real_hidapi = NULL;
static pfn_hid_enumerate        orig_hid_enumerate = NULL;
static pfn_hid_free_enumeration orig_hid_free_enumeration = NULL;
static pfn_hid_open             orig_hid_open = NULL;
static pfn_hid_open_path        orig_hid_open_path = NULL;
static pfn_hid_write            orig_hid_write = NULL;
static pfn_hid_read_timeout     orig_hid_read_timeout = NULL;
static pfn_hid_read             orig_hid_read = NULL;
static pfn_hid_close            orig_hid_close = NULL;
static pfn_hid_init             orig_hid_init = NULL;
static pfn_hid_exit             orig_hid_exit = NULL;

static bidi_bridge_t g_bridge;
static hid_device *g_real_beitong_dev = NULL;
static bool g_is_virtual_dualsense_open = false;

static void load_real_hidapi(void) {
    if (g_real_hidapi) return;

    // Load original hidapi from System32 or fallback name
    char system_path[MAX_PATH];
    GetSystemDirectoryA(system_path, MAX_PATH);
    strcat(system_path, "\\hidapi.dll");
    g_real_hidapi = LoadLibraryA(system_path);

    if (!g_real_hidapi) {
        g_real_hidapi = LoadLibraryA("hidapi_orig.dll");
    }

    if (g_real_hidapi) {
        orig_hid_enumerate        = (pfn_hid_enumerate)GetProcAddress(g_real_hidapi, "hid_enumerate");
        orig_hid_free_enumeration = (pfn_hid_free_enumeration)GetProcAddress(g_real_hidapi, "hid_free_enumeration");
        orig_hid_open             = (pfn_hid_open)GetProcAddress(g_real_hidapi, "hid_open");
        orig_hid_open_path        = (pfn_hid_open_path)GetProcAddress(g_real_hidapi, "hid_open_path");
        orig_hid_write            = (pfn_hid_write)GetProcAddress(g_real_hidapi, "hid_write");
        orig_hid_read_timeout     = (pfn_hid_read_timeout)GetProcAddress(g_real_hidapi, "hid_read_timeout");
        orig_hid_read             = (pfn_hid_read)GetProcAddress(g_real_hidapi, "hid_read");
        orig_hid_close            = (pfn_hid_close)GetProcAddress(g_real_hidapi, "hid_close");
        orig_hid_init             = (pfn_hid_init)GetProcAddress(g_real_hidapi, "hid_init");
        orig_hid_exit             = (pfn_hid_exit)GetProcAddress(g_real_hidapi, "hid_exit");
    }

    bidi_bridge_init(&g_bridge, false);
}

// ============================================================================
// Exported Hook: hid_enumerate
// When games search for Sony DualSense (VID 0x054C, PID 0x0CE6), we intercept
// and present our Virtual DualSense directly to the game!
// ============================================================================
__declspec(dllexport) struct hid_device_info* hid_enumerate(unsigned short vendor_id, unsigned short product_id) {
    load_real_hidapi();

    // Check if the game is querying for DualSense or all devices
    if ((vendor_id == 0x0000 || vendor_id == SONY_VENDOR_ID) &&
        (product_id == 0x0000 || product_id == DUALSENSE_PRODUCT_ID || product_id == DUALSENSE_EDGE_PRODUCT_ID)) {
        
        struct hid_device_info *info = (struct hid_device_info *)calloc(1, sizeof(struct hid_device_info));
        info->path = strdup("\\\\?\\hid#virtual_dualsense_bridge#0001");
        info->vendor_id = SONY_VENDOR_ID;
        info->product_id = DUALSENSE_PRODUCT_ID;
        info->serial_number = wcsdup(L"DS5W00000001");
        info->manufacturer_string = wcsdup(L"Sony Interactive Entertainment");
        info->product_string = wcsdup(L"Wireless Controller");
        info->usage_page = 0x0001;
        info->usage = 0x0005; // Gamepad
        info->interface_number = 3;
        info->next = NULL;

        return info;
    }

    if (orig_hid_enumerate) {
        return orig_hid_enumerate(vendor_id, product_id);
    }
    return NULL;
}

__declspec(dllexport) void hid_free_enumeration(struct hid_device_info *devs) {
    load_real_hidapi();
    if (devs && strcmp(devs->path, "\\\\?\\hid#virtual_dualsense_bridge#0001") == 0) {
        free(devs->path);
        free(devs->serial_number);
        free(devs->manufacturer_string);
        free(devs->product_string);
        free(devs);
        return;
    }
    if (orig_hid_free_enumeration) {
        orig_hid_free_enumeration(devs);
    }
}

// ============================================================================
// Exported Hook: hid_open / hid_open_path
// ============================================================================
__declspec(dllexport) hid_device* hid_open(unsigned short vendor_id, unsigned short product_id, const wchar_t *serial_number) {
    load_real_hidapi();

    if (vendor_id == SONY_VENDOR_ID && (product_id == DUALSENSE_PRODUCT_ID || product_id == DUALSENSE_EDGE_PRODUCT_ID)) {
        // Open the physical Beitong controller in the background
        if (orig_hid_open) {
            g_real_beitong_dev = orig_hid_open(BEITONG_VENDOR_ID, BEITONG_PID_KP70C2_PAD, NULL);
            if (!g_real_beitong_dev) {
                g_real_beitong_dev = orig_hid_open(BEITONG_VENDOR_ID, BEITONG_PID_ASURA_KP70, NULL);
            }
        }
        g_is_virtual_dualsense_open = true;
        return (hid_device*)0xDEADBEEF; // Return active virtual handle
    }

    if (orig_hid_open) {
        return orig_hid_open(vendor_id, product_id, serial_number);
    }
    return NULL;
}

__declspec(dllexport) hid_device* hid_open_path(const char *path) {
    load_real_hidapi();
    if (path && strcmp(path, "\\\\?\\hid#virtual_dualsense_bridge#0001") == 0) {
        return hid_open(SONY_VENDOR_ID, DUALSENSE_PRODUCT_ID, NULL);
    }
    if (orig_hid_open_path) {
        return orig_hid_open_path(path);
    }
    return NULL;
}

// ============================================================================
// Exported Hook: hid_write (GAME -> DUALSENSE OUTPUT REPORT)
// Game sends trigger effects & rumble to the virtual DualSense.
// We intercept with 0ms latency, translate to Beitong packets, and write to Beitong!
// ============================================================================
__declspec(dllexport) int hid_write(hid_device *dev, const unsigned char *data, size_t length) {
    load_real_hidapi();

    if (dev == (hid_device*)0xDEADBEEF || g_is_virtual_dualsense_open) {
        // Translate DualSense Report 0x02 / 0x31 to Beitong Report 0x02
        uint8_t bt_pkts[3][10];
        size_t bt_lens[3];
        int count = bidi_translate_game_to_beitong(&g_bridge, data, length, bt_pkts, bt_lens);

        if (g_real_beitong_dev && orig_hid_write) {
            for (int i = 0; i < count; i++) {
                orig_hid_write(g_real_beitong_dev, bt_pkts[i], bt_lens[i]);
            }
        }
        return (int)length;
    }

    if (orig_hid_write) {
        return orig_hid_write(dev, data, length);
    }
    return -1;
}

// ============================================================================
// Exported Hook: hid_read (CONTROLLER -> GAME INPUT REPORT)
// Game polls DualSense buttons, sticks, and triggers.
// We read raw Beitong inputs, translate to Report 0x01, and pass to game!
// ============================================================================
__declspec(dllexport) int hid_read_timeout(hid_device *dev, unsigned char *data, size_t length, int milliseconds) {
    load_real_hidapi();

    if (dev == (hid_device*)0xDEADBEEF || g_is_virtual_dualsense_open) {
        if (g_real_beitong_dev && orig_hid_read_timeout) {
            uint8_t bt_raw[64];
            int res = orig_hid_read_timeout(g_real_beitong_dev, bt_raw, sizeof(bt_raw), milliseconds);
            if (res > 0) {
                // Translate raw Beitong packet into 64-byte DualSense Report 0x01
                uint8_t ds_in[64];
                bidi_translate_beitong_to_game(&g_bridge, bt_raw, (size_t)res, ds_in, sizeof(ds_in));
                size_t copy_sz = length < 64 ? length : 64;
                memcpy(data, ds_in, copy_sz);
                return (int)copy_sz;
            }
        }
        return 0;
    }

    if (orig_hid_read_timeout) {
        return orig_hid_read_timeout(dev, data, length, milliseconds);
    }
    return -1;
}

__declspec(dllexport) int hid_read(hid_device *dev, unsigned char *data, size_t length) {
    return hid_read_timeout(dev, data, length, -1);
}

__declspec(dllexport) void hid_close(hid_device *dev) {
    load_real_hidapi();
    if (dev == (hid_device*)0xDEADBEEF) {
        if (g_real_beitong_dev && orig_hid_close) {
            orig_hid_close(g_real_beitong_dev);
            g_real_beitong_dev = NULL;
        }
        g_is_virtual_dualsense_open = false;
        return;
    }
    if (orig_hid_close) {
        orig_hid_close(dev);
    }
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    (void)hinstDLL;
    (void)lpvReserved;
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        load_real_hidapi();
    }
    return TRUE;
}
