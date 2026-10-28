//
//  duke_usb_frame.c — see duke_usb_frame.h. Ported from pico_shared's
//  pio_usb_frame.c.
//
//  duke_usb_init() sets skip_alarm_pool in the PIO-USB configuration, and
//  duke_usb_frame_start() then runs the frame (pio_usb_host_frame: SOF,
//  transfers and the connection check) from the same timer Pico-PIO-USB would
//  have used: an alarm pool on hardware alarm 2, every 1000 us, on the calling
//  core.
//
//  RP2350-E9 (chips up to A2, such as the Fruit Jam's): a pad with its input
//  enabled keeps a line that was high at about 2.2 V, and the pull-down cannot
//  bring it back. Pico-PIO-USB switches the inputs off for 8 NOPs before it
//  reads the line state, too short to drain a line (21 ns at 378 MHz), and
//  reads right after switching them back on. A USB device then often stayed
//  unseen: D- was latched, so D+ and D- both read high (SE1), never an idle
//  state. So while nothing is attached, the inputs stay off between frames
//  (the lines drain through the pull-downs) and the line state is also read
//  here, with the inputs settled. While a device is attached, the lines are
//  drained for a few microseconds every 16 frames, so that Pico-PIO-USB sees
//  an unplug as SE0.
//
#ifdef HAS_USBPIO
#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/structs/pads_bank0.h"
#include "pio_usb.h"
#include "pio_usb_ll.h"
#include "duke_usb_frame.h"

#define FRAME_ALARM     2       // the hardware alarm Pico-PIO-USB uses itself
#define E9_DRAIN_FRAMES 16u     // frames between drains while a device is attached
#define E9_DRAIN_US     5u

static repeating_timer_t frame_timer;
static bool e9;                 // chip up to A2: needs the workaround
static uint32_t e9_frames;

static void __not_in_flash_func(wait_us)(uint32_t us)
{
    uint32_t start = timer_hw->timerawl;
    while (timer_hw->timerawl - start <= us)
        ;
}

static void __not_in_flash_func(set_inputs)(const root_port_t *root, bool on)
{
    io_rw_32 *dp = &pads_bank0_hw->io[root->pin_dp];
    io_rw_32 *dm = &pads_bank0_hw->io[root->pin_dm];
    if (on) {
        hw_set_bits(dp, PADS_BANK0_GPIO0_IE_BITS);
        hw_set_bits(dm, PADS_BANK0_GPIO0_IE_BITS);
    } else {
        hw_clear_bits(dp, PADS_BANK0_GPIO0_IE_BITS);
        hw_clear_bits(dm, PADS_BANK0_GPIO0_IE_BITS);
    }
    (void)*dp;                  // read back: the writes have reached the pads
    (void)*dm;
}

// Before pio_usb_host_frame: drain the lines of an attached device now and then.
static void __not_in_flash_func(e9_drain)(void)
{
    root_port_t *root = PIO_USB_ROOT_PORT(0);
    if (!(root->initialized && root->connected && !root->suspended) || ++e9_frames % E9_DRAIN_FRAMES)
        return;
    set_inputs(root, false);
    wait_us(E9_DRAIN_US);
    set_inputs(root, true);
}

// After pio_usb_host_frame: while nothing is attached, look for a J state (the
// device's pull-up on D+ or D-) as pio_usb_host_frame does, then leave the
// inputs off until the next frame.
static void __not_in_flash_func(e9_connect)(void)
{
    root_port_t *root = PIO_USB_ROOT_PORT(0);
    if (!root->initialized || root->connected)
        return;
    set_inputs(root, true);
    wait_us(1);
    bool dp = !gpio_get(root->pin_dp);  // pio_usb_host_add_port inverts the inputs
    bool dm = !gpio_get(root->pin_dm);
    if (dp != dm) {
        root->is_fullspeed = dp;
        root->connected = true;
        root->suspended = true;         // a bus reset comes first
        root->ints |= PIO_USB_INTS_CONNECT_BITS;
        return;
    }
    set_inputs(root, false);
}

static bool __not_in_flash_func(frame)(repeating_timer_t *rt)
{
    (void)rt;
    if (e9) {
        e9_drain();
        pio_usb_host_frame();
        e9_connect();
    } else {
        pio_usb_host_frame();
    }
    return true;
}

void duke_usb_frame_start(void)
{
    e9 = rp2350_chip_version() <= 2;
    alarm_pool_add_repeating_timer_us(alarm_pool_create(FRAME_ALARM, 1), -1000, frame, NULL, &frame_timer);
    printf("usb: PIO-USB frame, RP2350-E9 workaround %s\n", e9 ? "on" : "off");
}
#endif // HAS_USBPIO
