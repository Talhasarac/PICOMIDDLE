#include "btstack.h"
#include "pico/cyw43_arch.h"
#include "ble/gatt-service/battery_service_server.h"
#include "ble/gatt-service/device_information_service_server.h"
#include "ble/gatt-service/hids_device.h"
#include "ble_hid.h"
#include "usb_host.h"
#include "keyboard.h"
#include <string.h>
#include <stdio.h>
#include "hardware/sync.h"

// Standard boot keyboard HID descriptor (report ID 1)
static const uint8_t hid_descriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        // Report ID 1
    // Modifier keys
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7,
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08,
    0x81, 0x02,
    // Reserved byte
    0x95, 0x01, 0x75, 0x08, 0x81, 0x03,
    // Keycodes (6 bytes)
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,
    0x05, 0x07, 0x19, 0x00, 0x29, 0x65,
    0x81, 0x00,
    0xC0               // End Collection
};

// Advertisement includes HID service UUID so hosts recognise it as a keyboard
static const uint8_t adv_data[] = {
    0x02, BLUETOOTH_DATA_TYPE_FLAGS, 0x06,
    // Complete local name: "PicoKeyboard"
    0x0D, BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME,
          'P','i','c','o','K','e','y','b','o','a','r','d',
    // HID service UUID (0x1812)
    0x03, BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS,
          ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE & 0xFF,
          ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE >> 8,
    // Appearance: keyboard (0x03C1)
    0x03, BLUETOOTH_DATA_TYPE_APPEARANCE, 0xC1, 0x03,
};

static volatile hci_con_handle_t session_handle = HCI_CON_HANDLE_INVALID;
static uint8_t battery = 100;
static bool input_report_enabled = false;
static bool send_warmup_pending = false;
static const uint8_t zero_report[8] = {0};
static bool resume_report_pending = false;
static uint8_t resume_report[8] = {0};

static btstack_packet_callback_registration_t hci_cb;
static btstack_packet_callback_registration_t sm_cb;
static hids_device_report_t report_storage[1];
static btstack_timer_source_t send_timer;

// ---------------------------------------------------------------------------
// Lock-free single-producer (core 0) / single-consumer (core 1) ring buffer.
// Core 0 writes rq_tail only.  Core 1 writes rq_head only.
// __dmb() ensures the data write is visible before the index update.
// ---------------------------------------------------------------------------
#define RQ_SIZE 64
static uint8_t          rq_buf[RQ_SIZE][8];
static volatile uint8_t rq_head = 0;   // consumer index, written by core 1 only
static volatile uint8_t rq_tail = 0;   // producer index, written by core 0 only

// Push one 8-byte report from core 0.  Returns false if full (dropped).
bool ble_hid_send_report(const uint8_t *report, uint8_t len) {
    if (session_handle == HCI_CON_HANDLE_INVALID) return false;
    uint8_t next = (rq_tail + 1) % RQ_SIZE;
    if (next == rq_head) return false;  // full — drop
    uint8_t n = len < 8 ? len : 8;
    memcpy(rq_buf[rq_tail], report, n);
    if (n < 8) memset(rq_buf[rq_tail] + n, 0, 8 - n);
    __dmb();        // data must be visible before tail advances
    rq_tail = next;
    return true;
}

// ---------------------------------------------------------------------------
// Core 1: request a CAN_SEND_NOW if there is data in the queue.
// hids_device_request_can_send_now_event is idempotent — calling it multiple
// times while one is already pending is safe (BTstack ignores duplicate calls).
// ---------------------------------------------------------------------------
static uint8_t send_buf[8];

static void try_send(void) {
    if (session_handle == HCI_CON_HANDLE_INVALID) return;
    if (!input_report_enabled) return;
    if (!send_warmup_pending && !resume_report_pending && rq_head == rq_tail) return;
    hids_device_request_can_send_now_event(session_handle);
}

static void send_timer_handler(btstack_timer_source_t *ts) {
    try_send();
    btstack_run_loop_set_timer(ts, 1);
    btstack_run_loop_add_timer(ts);
}

static void packet_handler(uint8_t type, uint16_t channel,
                            uint8_t *packet, uint16_t size) {
    (void)channel; (void)size;
    if (type != HCI_EVENT_PACKET) return;

    switch (hci_event_packet_get_type(packet)) {
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
                gap_advertisements_enable(1);
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
                printf("Advertising as PicoKeyboard\n");
            }
            break;

        case HCI_EVENT_LE_META:
            if (hci_event_le_meta_get_subevent_code(packet) == HCI_SUBEVENT_LE_CONNECTION_COMPLETE &&
                hci_subevent_le_connection_complete_get_status(packet) == ERROR_CODE_SUCCESS) {
                session_handle = hci_subevent_le_connection_complete_get_connection_handle(packet);
                __dmb();
                input_report_enabled = false;
                send_warmup_pending = false;
                resume_report_pending = false;
                memset(resume_report, 0, sizeof(resume_report));
                rq_head = rq_tail = 0;
                gap_request_connection_parameter_update(session_handle, 6, 12, 0, 200);
                printf("BLE link up, con_handle=0x%04x\n", session_handle);
            }
            break;

        case HCI_EVENT_DISCONNECTION_COMPLETE:
            session_handle = HCI_CON_HANDLE_INVALID;
            __dmb();
            input_report_enabled = false;
            send_warmup_pending = false;
            resume_report_pending = false;
            memset(resume_report, 0, sizeof(resume_report));
            rq_head = rq_tail = 0;      // flush queue on disconnect
            gap_advertisements_enable(1);
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
            printf("Disconnected — re-advertising\n");
            break;

        case SM_EVENT_JUST_WORKS_REQUEST:
            sm_just_works_confirm(sm_event_just_works_request_get_handle(packet));
            printf("Just Works pairing confirmed\n");
            break;

        case HCI_EVENT_HIDS_META:
            switch (hci_event_hids_meta_get_subevent_code(packet)) {
                case HIDS_SUBEVENT_INPUT_REPORT_ENABLE: {
                    uint8_t report_id = hids_subevent_input_report_enable_get_report_id(packet);
                    uint8_t enabled = hids_subevent_input_report_enable_get_enable(packet);

                    if (report_id != 1) break;

                    session_handle = hids_subevent_input_report_enable_get_con_handle(packet);
                    __dmb();
                    input_report_enabled = enabled != 0;
                    send_warmup_pending = input_report_enabled;
                    resume_report_pending = false;
                    memset(resume_report, 0, sizeof(resume_report));

                    if (input_report_enabled) {
                        usb_host_reset_forwarded_state();
                        usb_host_get_current_report(resume_report);
                        resume_report_pending =
                            memcmp(resume_report, zero_report, sizeof(resume_report)) != 0;
                    }

                    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN,
                                        input_report_enabled ? 0 : 1);
                    printf("HID input %s, con_handle=0x%04x\n",
                           input_report_enabled ? "enabled" : "disabled",
                           session_handle);
                    try_send();
                    break;
                }
                case HIDS_SUBEVENT_CAN_SEND_NOW:
                    if (session_handle == HCI_CON_HANDLE_INVALID ||
                        !input_report_enabled) {
                        break;
                    }

                    if (send_warmup_pending) {
                        send_warmup_pending = false;
                        hids_device_send_input_report_for_id(
                            session_handle, 1, zero_report, sizeof(zero_report));
                        try_send();
                        break;
                    }

                    if (resume_report_pending) {
                        resume_report_pending = false;
                        hids_device_send_input_report_for_id(
                            session_handle, 1, resume_report, sizeof(resume_report));
                        try_send();
                        break;
                    }

                    if (rq_head != rq_tail) {
                        memcpy(send_buf, rq_buf[rq_head], 8);
                        __dmb();
                        rq_head = (rq_head + 1) % RQ_SIZE;
                        hids_device_send_input_report_for_id(
                            session_handle, 1, send_buf, 8);
                    }
                    try_send();     // chain-send if more queued
                    break;
                default:
                    break;
            }
            break;

        default:
            break;
    }
}

void ble_hid_main(void) {
    cyw43_arch_init();

    l2cap_init();
    sm_init();
    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    sm_set_authentication_requirements(SM_AUTHREQ_BONDING);

    att_server_init(profile_data, NULL, NULL);
    battery_service_server_init(battery);
    device_information_service_server_init();
    hids_device_init_with_storage(0, hid_descriptor, sizeof(hid_descriptor),
                                  1, report_storage);

    uint16_t adv_int_min = 0x0030, adv_int_max = 0x0030;
    bd_addr_t null_addr = {0};
    gap_advertisements_set_params(adv_int_min, adv_int_max,
                                  0x00, 0, null_addr, 0x07, 0x00);
    gap_advertisements_set_data(sizeof(adv_data), (uint8_t *)adv_data);

    hci_cb.callback = &packet_handler;
    hci_add_event_handler(&hci_cb);
    sm_cb.callback = &packet_handler;
    sm_add_event_handler(&sm_cb);
    hids_device_register_packet_handler(packet_handler);

    btstack_run_loop_set_timer_handler(&send_timer, send_timer_handler);
    btstack_run_loop_set_timer(&send_timer, 1);
    btstack_run_loop_add_timer(&send_timer);

    hci_power_control(HCI_POWER_ON);
    btstack_run_loop_execute();
}
