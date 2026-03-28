#!/bin/bash
# Copies project files to the Pico W once it appears as CIRCUITPY drive

PICO="/Volumes/CIRCUITPY"
LIBS_URL="https://github.com/adafruit/Adafruit_CircuitPython_Bundle/releases/latest"

echo "Waiting for Pico W to mount as CIRCUITPY..."
while [ ! -d "$PICO" ]; do
  sleep 1
done
echo "Found CIRCUITPY!"

echo "Copying boot.py and code.py..."
cp boot.py "$PICO/boot.py"
cp code.py "$PICO/code.py"

# Check if required libs are present
MISSING=()
[ ! -d "$PICO/lib/adafruit_ble" ]  && MISSING+=("adafruit_ble")
[ ! -d "$PICO/lib/adafruit_hid" ]  && MISSING+=("adafruit_hid")

if [ ${#MISSING[@]} -gt 0 ]; then
  echo ""
  echo "Missing libraries: ${MISSING[*]}"
  echo "Download the CircuitPython 9.x bundle from:"
  echo "  https://circuitpython.org/libraries"
  echo "Then copy these folders into $PICO/lib/:"
  for lib in "${MISSING[@]}"; do
    echo "  - $lib"
  done
else
  echo "All libraries present."
  echo ""
  echo "Done! Eject CIRCUITPY, switch to VSYS power, plug in keyboard, pair Bluetooth."
fi
