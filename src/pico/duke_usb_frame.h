//
//  duke_usb_frame.h — the 1 ms PIO-USB frame, run here instead of by
//  Pico-PIO-USB, with a workaround for erratum RP2350-E9. See duke_usb_frame.c.
//
#ifndef DUKE_USB_FRAME_H
#define DUKE_USB_FRAME_H

#ifdef __cplusplus
extern "C" {
#endif

// Call once, after tusb_init() has brought up the PIO-USB host. Needs
// skip_alarm_pool in the PIO-USB configuration (duke_usb_init).
void duke_usb_frame_start(void);

#ifdef __cplusplus
}
#endif

#endif // DUKE_USB_FRAME_H
