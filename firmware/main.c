#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "tusb.h"
#include "usb_host.h"
#include "ble_hid.h"

// BLE runs on core 1
static void core1_entry(void) {
    ble_hid_main();
}

int main(void) {
    stdio_init_all();

    // Launch BLE on core 1
    multicore_launch_core1(core1_entry);

    // Init USB host on core 0
    usb_host_init();

    while (true) {
        tuh_task();  // process USB host events
    }

    return 0;
}
