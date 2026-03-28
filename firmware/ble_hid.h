#ifndef BLE_HID_H
#define BLE_HID_H

#include <stdbool.h>
#include <stdint.h>

void ble_hid_main(void);           // runs on core 1
bool ble_hid_send_report(const uint8_t *report, uint8_t len);

#endif
