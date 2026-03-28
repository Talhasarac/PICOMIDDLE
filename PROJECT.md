# PicoMiddle — USB-to-BLE Keyboard Bridge

A Raspberry Pi Pico W firmware that sits between a wired USB-C keyboard and a Mac/phone,
forwarding keystrokes over BLE HID so the keyboard works wirelessly.

---

## Hardware setup

| Connection | Detail |
|---|---|
| Keyboard → Pico W | USB-C to USB-A adapter → Pico W USB port |
| Power | Power bank → Pin 40 (VBUS), Pin 38 (GND) |
| Why VBUS (pin 40) | VBUS feeds directly to the USB port, so the keyboard gets 5 V. VSYS (pin 39) feeds through a diode and only supplies ~4.5 V — not enough for some keyboards. |

No battery management needed. The power bank powers the Pico; the Pico powers the keyboard via USB.

---

## Architecture

```
[USB Keyboard]
      |
   USB Host (TinyUSB, native RP2040 USB hardware, port 0)
      |
   Core 0  ──── ring buffer (64 slots, lock-free) ────  Core 1
   tuh_task() + HID callbacks                        btstack_run_loop_execute()
   direct enqueue from USB callback                  BLE HID (BTstack + CYW43)
```

- **Core 0**: runs TinyUSB host loop; the HID callback normalizes keyboard reports and pushes them directly into the ring buffer.
- **Core 1**: runs BTstack BLE run loop, drains the ring buffer and sends BLE HID notifications to the host.

The ring buffer is a single-producer / single-consumer lock-free queue using `__dmb()` for memory ordering. This is required because BTstack is not thread-safe — calling its APIs from core 0 is unsafe.

---

## Key files

| File | Purpose |
|---|---|
| `firmware/main.c` | Core 0 entry: USB host init, TinyUSB task loop |
| `firmware/usb_host.c` | TinyUSB HID host callbacks: keyboard detection, report normalization, direct queueing |
| `firmware/ble_hid.c` | BTstack BLE HID device: GATT setup, advertising, pairing, report send |
| `firmware/ble_hid.h` | Public API: `ble_hid_main()`, `ble_hid_send_report()` |
| `firmware/keyboard.gatt` | GATT database: GAP, Battery, Device Info, HID services |
| `firmware/btstack_config.h` | BTstack build config (must include `#define ENABLE_BLE`) |
| `firmware/tusb_config.h` | TinyUSB host-only config for RP2040 |
| `firmware/CMakeLists.txt` | Build: links TinyUSB host + BTstack BLE + CYW43, GATT header gen |

---

## Build

```bash
cd firmware
mkdir build && cd build
cmake .. \
  -DCMAKE_C_COMPILER=$(which arm-none-eabi-gcc) \
  -DCMAKE_CXX_COMPILER=$(which arm-none-eabi-g++) \
  -DCMAKE_ASM_COMPILER=$(which arm-none-eabi-gcc)
make -j4
```

Flash: hold BOOTSEL on Pico W, plug into Mac, drag `pico_ble_keyboard.uf2` to the RPI-RP2 drive in Finder.

---

## HID report ID handling

Some keyboards include a Report ID byte as byte 0 of every USB HID report. Without stripping it, the report ID value (0x01) lands in the modifier byte and the Mac behaves as if Left Control is permanently held.

`usb_host.c` scans the HID descriptor at mount time for the `0x85` (REPORT_ID) tag. If found, byte 0 is stripped before forwarding, and the remaining bytes are zero-padded to 8 bytes.

The BLE side uses `hids_device_send_input_report_for_id(con_handle, 1, payload, 8)` which expects the 8-byte payload **without** the report ID — BTstack prepends it internally.

---

## BLE pairing

- Advertises as "PicoKeyboard" with HID service UUID (0x1812) and Appearance = Keyboard (0x03C1).
- Uses Just Works pairing (`SM_EVENT_JUST_WORKS_REQUEST` confirmed automatically).
- The BLE connection handle is captured on LE connection complete; HID input notifications are considered ready after `HIDS_SUBEVENT_INPUT_REPORT_ENABLE`.
- LED on = advertising / HID notifications not ready. LED off = HID input notifications enabled.

---

## Status (as of 2026-03-28)

The first-key-after-connect failure is addressed in the current firmware.

### What changed

1. **Removed the extra core-0 pending-report slot** — `main.c` no longer copies reports through a second single-entry buffer. The USB HID callback now queues normalized reports directly into the BLE ring buffer.
2. **Stopped poisoning dedupe state on failed sends** — a keyboard report is only marked as forwarded after it is successfully queued for BLE delivery.
3. **Separated BLE link-up from HID notify-ready** — the BLE connection handle is captured on `HCI_SUBEVENT_LE_CONNECTION_COMPLETE`, so reports can queue as soon as the LE link exists, even before HID input notifications are enabled.
4. **Warm up and resync the HID notify path** — on `HIDS_SUBEVENT_INPUT_REPORT_ENABLE`, the firmware sends an initial all-zero keyboard report, snapshots the latest USB keyboard state, and then replays that live state before draining queued key events.
5. **Reduced drop risk during connect bursts** — the lock-free queue was expanded to 64 reports, and non-keyboard HID interfaces are ignored instead of being treated as keyboard input.
6. **Requested lower-latency BLE parameters** — after LE connection complete, the firmware asks for a tighter connection interval range (`7.5-15 ms`) to improve key response time when the host accepts it.

### Remaining limitations

- The BLE side still exposes a standard 8-byte keyboard input report. Consumer-control/media keys and vendor-specific HID reports are not bridged yet.
- The cross-core wakeup still uses a 1 ms BTstack timer. If you want to shave off the last millisecond of bridge latency, the next step is an inter-core FIFO/IRQ kick into core 1.
