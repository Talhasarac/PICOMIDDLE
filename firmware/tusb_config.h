#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#define CFG_TUSB_MCU          OPT_MCU_RP2040
#define CFG_TUSB_OS           OPT_OS_PICO
#define CFG_TUSB_DEBUG        0

// Host mode only
#define CFG_TUH_ENABLED       1
#define CFG_TUH_MAX_SPEED     OPT_MODE_FULL_SPEED

// HID host: support up to 4 HID devices
#define CFG_TUH_HID           4
#define CFG_TUH_HID_EP_BUFSIZE 64

#endif
