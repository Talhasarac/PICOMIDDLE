#ifndef USB_HOST_H
#define USB_HOST_H

#include <stdint.h>

void usb_host_init(void);
void usb_host_get_current_report(uint8_t out_report[8]);
void usb_host_reset_forwarded_state(void);

#endif
