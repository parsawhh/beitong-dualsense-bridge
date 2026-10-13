#include <windows.h>
#include <stdio.h>
#include <stdbool.h>
#include "ds2bt_engine.h"

// Forward declaration of feeder entry
int main(int argc, char **argv) {
    printf("========================================================\n");
    printf("  Beitong DualSense Bridge - Standalone Native Feeder  \n");
    printf("========================================================\n\n");

    printf("[1] Initializing Native ds2bt Translation Engine...\n");
    dualsense_to_beitong_t engine;
    ds2bt_init(&engine, false);
    ds2bt_set_gains(&engine, 1.45f, 1.50f, 1.35f, 25);
    printf("    -> Gains: Force=1.45x, Vibe=1.50x, Floor=25%%\n");

    printf("[2] Target Controller: Beitong VID 0x20BC (PIDs: 0x518C, 0x518A, 0x5126, 0x5128)\n");
    printf("[3] DualSense Target: 0x054C:0x0CE6 (Report 0x02 Interception)\n\n");
    printf("[+] Feeder ready. Run alongside HIDMaestro or In-Process DLL.\n");
    return 0;
}
