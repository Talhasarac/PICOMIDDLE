#include "tusb.h"
#include "usb_host.h"
#include "ble_hid.h"
#include <string.h>
#include "hardware/sync.h"

static bool keyboard_attached = false;
static uint8_t keyboard_dev_addr = 0;
static uint8_t keyboard_instance = 0;
static bool keyboard_has_report_ids = false;
static uint8_t current_report[8] = {0};
static uint8_t last_forwarded_report[8] = {0};

void usb_host_init(void) {
    tuh_init(0);
}

static bool descriptor_is_keyboard(uint8_t const *desc, uint16_t len) {
    if (desc == NULL || len < 4) return false;

    for (uint16_t i = 0; i + 3 < len; i++) {
        if (desc[i] == 0x05 && desc[i + 1] == 0x01 &&
            desc[i + 2] == 0x09 && desc[i + 3] == 0x06) {
            return true;
        }
    }

    return false;
}

// Scan HID descriptor for REPORT_ID tag (0x85) to detect if report IDs are used
static bool descriptor_has_report_ids(uint8_t const *desc, uint16_t len) {
    if (desc == NULL) return false;

    for (uint16_t i = 0; i < len; i++) {
        if (desc[i] == 0x85) return true;  // REPORT_ID tag
    }
    return false;
}

static bool is_keyboard_interface(uint8_t dev_addr, uint8_t instance,
                                  uint8_t const *desc_report, uint16_t desc_len) {
    uint8_t protocol = tuh_hid_interface_protocol(dev_addr, instance);
    return protocol == HID_ITF_PROTOCOL_KEYBOARD ||
           descriptor_is_keyboard(desc_report, desc_len);
}

static bool is_active_keyboard(uint8_t dev_addr, uint8_t instance) {
    return keyboard_attached &&
           keyboard_dev_addr == dev_addr &&
           keyboard_instance == instance;
}

static void clear_keyboard_state(void) {
    keyboard_attached = false;
    keyboard_dev_addr = 0;
    keyboard_instance = 0;
    keyboard_has_report_ids = false;
    memset(current_report, 0, sizeof(current_report));
    memset(last_forwarded_report, 0, sizeof(last_forwarded_report));
    __dmb();
}

static void forward_keyboard_report(uint8_t const *report, uint16_t len) {
    const uint8_t *data = report;
    uint16_t data_len = len;
    uint8_t normalized[8] = {0};

    if (keyboard_has_report_ids) {
        if (len <= 1) return;
        data = report + 1;
        data_len = len - 1;
    }

    if (data_len == 0) return;

    uint8_t copy = data_len < sizeof(normalized) ? data_len : sizeof(normalized);
    memcpy(normalized, data, copy);
    memcpy(current_report, normalized, sizeof(current_report));
    __dmb();

    if (memcmp(normalized, last_forwarded_report, sizeof(normalized)) == 0) return;

    if (ble_hid_send_report(normalized, sizeof(normalized))) {
        memcpy(last_forwarded_report, normalized, sizeof(normalized));
    }
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance,
                      uint8_t const *desc_report, uint16_t desc_len) {
    if (!keyboard_attached &&
        is_keyboard_interface(dev_addr, instance, desc_report, desc_len)) {
        keyboard_attached = true;
        keyboard_dev_addr = dev_addr;
        keyboard_instance = instance;
        keyboard_has_report_ids = descriptor_has_report_ids(desc_report, desc_len);
        memset(current_report, 0, sizeof(current_report));
        memset(last_forwarded_report, 0, sizeof(last_forwarded_report));
    }

    tuh_hid_receive_report(dev_addr, instance);
}

void usb_host_get_current_report(uint8_t out_report[8]) {
    __dmb();
    memcpy(out_report, current_report, sizeof(current_report));
}

void usb_host_reset_forwarded_state(void) {
    memset(last_forwarded_report, 0, sizeof(last_forwarded_report));
    __dmb();
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    static const uint8_t zero_report[8] = {0};

    if (is_active_keyboard(dev_addr, instance)) {
        ble_hid_send_report(zero_report, sizeof(zero_report));
        clear_keyboard_state();
    }
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                 uint8_t const *report, uint16_t len) {
    if (is_active_keyboard(dev_addr, instance)) {
        forward_keyboard_report(report, len);
    }

    tuh_hid_receive_report(dev_addr, instance);
}
