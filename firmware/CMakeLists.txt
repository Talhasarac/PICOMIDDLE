cmake_minimum_required(VERSION 3.13)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(pico_ble_keyboard C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(pico_ble_keyboard
    main.c
    usb_host.c
    ble_hid.c
    ${PICO_SDK_PATH}/lib/btstack/src/ble/gatt-service/hids_device.c
    ${PICO_SDK_PATH}/lib/btstack/src/ble/gatt-service/battery_service_server.c
    ${PICO_SDK_PATH}/lib/btstack/src/ble/gatt-service/device_information_service_server.c
)

target_include_directories(pico_ble_keyboard PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

pico_btstack_make_gatt_header(pico_ble_keyboard PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/keyboard.gatt"
)

target_link_libraries(pico_ble_keyboard
    pico_stdlib
    pico_multicore
    tinyusb_host
    tinyusb_board
    pico_cyw43_arch_none
    pico_btstack_ble
    pico_btstack_cyw43
)

pico_add_extra_outputs(pico_ble_keyboard)
