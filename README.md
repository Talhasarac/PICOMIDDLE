PicoMiddle

Turns a wired USB keyboard into a BLE keyboard with a Raspberry Pi Pico W.

Device name: `PicoKeyboard`

Needs:
1. Raspberry Pi Pico W
2. Wired USB keyboard
3. Micro b otg cable
4. 5 V power to vbus and grd of pcio 

Build:

```bash
cd firmware
mkdir -p build
cd build
cmake .. \
  -DCMAKE_C_COMPILER=$(which arm-none-eabi-gcc) \
  -DCMAKE_CXX_COMPILER=$(which arm-none-eabi-g++) \
  -DCMAKE_ASM_COMPILER=$(which arm-none-eabi-gcc)
cmake --build . -j4
```

Flash:

Copy `firmware/build/pico_ble_keyboard.uf2` to the `RPI-RP2` drive.

Note:

Standard keyboard input works. Media keys are not supported yet.
