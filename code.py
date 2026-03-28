import time
import usb.core
import adafruit_ble
from adafruit_ble.advertising.standard import ProvideServicesAdvertisement
from adafruit_ble.services.standard.hid import HIDService
from adafruit_hid.keyboard import Keyboard

# ── BLE setup ────────────────────────────────────────────────────────────────
hid = HIDService()
ble = adafruit_ble.BLERadio()
ble.name = "PicoKeyboard"
advertisement = ProvideServicesAdvertisement(hid)
ble_keyboard = Keyboard(hid.devices)

print("Advertising as 'PicoKeyboard'...")
ble.start_advertising(advertisement)

# ── Helpers ───────────────────────────────────────────────────────────────────
def find_keyboard():
    """Return the first USB HID keyboard found, or None."""
    for dev in usb.core.find(find_all=True):
        try:
            if dev.bDeviceClass in (0x00, 0x03):  # HID or interface-defined
                dev.set_configuration()
                return dev
        except Exception:
            continue
    return None


def send_report(report):
    """Forward an 8-byte USB HID keyboard report over BLE."""
    # Byte 0: modifier keys (Shift, Ctrl, Alt, GUI)
    # Byte 1: reserved
    # Bytes 2-7: up to 6 simultaneous keycodes
    ble_keyboard.report[0] = report[0]
    ble_keyboard.report[2:8] = report[2:8]


# ── Main loop ─────────────────────────────────────────────────────────────────
while True:
    # Wait for a BLE host to connect
    while not ble.connected:
        time.sleep(0.1)

    print("BLE connected — looking for USB keyboard...")

    kbd = None
    while kbd is None and ble.connected:
        kbd = find_keyboard()
        time.sleep(0.5)

    if kbd is None:
        continue

    print("USB keyboard found — forwarding keys.")

    prev_report = bytes(8)

    while ble.connected:
        try:
            report = bytes(kbd.read(8, timeout=10) or b"")
            if len(report) == 8 and report != prev_report:
                send_report(report)
                prev_report = report
        except usb.core.USBTimeoutError:
            pass  # no keypress, normal
        except Exception as e:
            print("USB error:", e)
            break  # keyboard disconnected — re-scan

    print("Disconnected — re-advertising...")
    ble.start_advertising(advertisement)
