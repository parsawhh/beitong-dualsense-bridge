#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

// ============================================================================
// HidHide Windows Filter Driver IOCTL Definitions (Nefarius HidHide)
// ============================================================================
// HidHide is an upper filter driver on the HIDClass setup class GUID:
// {745A17A0-74D3-11D0-B6FE-00A0C90F57DA}
// Control device path: \\\\.\\HidHide

#define HIDHIDE_CONTROL_DEVICE_PATH "\\\\.\\HidHide"

#define IOCTL_HIDHIDE_BASE 0x8000

#define IOCTL_HIDHIDE_GET_WHITELIST \
    CTL_CODE(IOCTL_HIDHIDE_BASE, 0x801, METHOD_BUFFERED, FILE_READ_ACCESS)

#define IOCTL_HIDHIDE_SET_WHITELIST \
    CTL_CODE(IOCTL_HIDHIDE_BASE, 0x802, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define IOCTL_HIDHIDE_GET_BLACKLIST \
    CTL_CODE(IOCTL_HIDHIDE_BASE, 0x803, METHOD_BUFFERED, FILE_READ_ACCESS)

#define IOCTL_HIDHIDE_SET_BLACKLIST \
    CTL_CODE(IOCTL_HIDHIDE_BASE, 0x804, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define IOCTL_HIDHIDE_GET_ACTIVE \
    CTL_CODE(IOCTL_HIDHIDE_BASE, 0x805, METHOD_BUFFERED, FILE_READ_ACCESS)

#define IOCTL_HIDHIDE_SET_ACTIVE \
    CTL_CODE(IOCTL_HIDHIDE_BASE, 0x806, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

// HidHide lists are multi-string buffers (REG_MULTI_SZ style: strings terminated with '\0', ending with double '\0')

// Hides the physical Beitong controller from all applications except this bridge process.
// Hardware ID pattern to block: HID\\VID_20BC&PID_518C, HID\\VID_20BC&PID_5126, etc.
bool hidhide_cloak_beitong(const wchar_t *current_exe_path) {
    HANDLE hDevice = CreateFileA(
        HIDHIDE_CONTROL_DEVICE_PATH,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("[HidHide] Driver not present or not installed. Running without filter cloak.\n");
        return false;
    }

    DWORD bytesReturned = 0;

    // 1. Whitelist current executable so our bridge process can open and write to the physical Beitong controller
    // Multi-string buffer: <exe_path>\0\0
    size_t exe_len = wcslen(current_exe_path);
    size_t wl_size = (exe_len + 2) * sizeof(wchar_t);
    wchar_t *wl_buffer = (wchar_t *)malloc(wl_size);
    if (wl_buffer) {
        wcscpy(wl_buffer, current_exe_path);
        wl_buffer[exe_len + 1] = L'\0'; // Double null

        BOOL ok = DeviceIoControl(
            hDevice,
            IOCTL_HIDHIDE_SET_WHITELIST,
            wl_buffer,
            (DWORD)wl_size,
            NULL,
            0,
            &bytesReturned,
            NULL
        );
        free(wl_buffer);
        if (ok) {
            printf("[HidHide] Successfully whitelisted bridge application: %ls\n", current_exe_path);
        }
    }

    // 2. Blacklist / Cloak Beitong Hardware IDs (VID 0x20BC & 0x20DD)
    // All games and Windows apps will no longer see the raw Beitong device
    const wchar_t *blacklist_ids = 
        L"HID\\VID_20BC&PID_5126\0"
        L"HID\\VID_20BC&PID_5128\0"
        L"HID\\VID_20BC&PID_5125\0"
        L"HID\\VID_20BC&PID_518C\0"
        L"HID\\VID_20BC&PID_518D\0"
        L"HID\\VID_20DD&PID_518C\0\0";

    size_t bl_size = 0;
    const wchar_t *p = blacklist_ids;
    while (*p || *(p + 1)) { p++; }
    bl_size = (p - blacklist_ids + 2) * sizeof(wchar_t);

    BOOL ok_bl = DeviceIoControl(
        hDevice,
        IOCTL_HIDHIDE_SET_BLACKLIST,
        (LPVOID)blacklist_ids,
        (DWORD)bl_size,
        NULL,
        0,
        &bytesReturned,
        NULL
    );

    // 3. Enable Cloaking
    BOOLEAN enable_cloaking = TRUE;
    DeviceIoControl(
        hDevice,
        IOCTL_HIDHIDE_SET_ACTIVE,
        &enable_cloaking,
        sizeof(enable_cloaking),
        NULL,
        0,
        &bytesReturned,
        NULL
    );

    CloseHandle(hDevice);
    printf("[HidHide] Cloaking active: Beitong controller hidden from games.\n");
    return true;
}
