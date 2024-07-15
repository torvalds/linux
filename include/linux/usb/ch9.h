/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file holds USB constants and structures that are needed for
 * USB device APIs.  These are used by the USB device model, which is
 * defined in chapter 9 of the USB 2.0 specification and in the
 * Wireless USB 1.0 spec (now defunct).  Linux has several APIs in C that
 * need these:
 *
 * - the host side Linux-USB kernel driver API;
 * - the "usbfs" user space API; and
 * - the Linux "gadget" device/peripheral side driver API.
 *
 * USB 2.0 adds an additional "On The Go" (OTG) mode, which lets systems
 * act either as a USB host or as a USB device.  That means the host and
 * device side APIs benefit from working well together.
 *
 * Note all descriptors are declared '__attribute__((packed))' so that:
 *
 * [a] they never get padded, either internally (USB spec writers
 *     probably handled that) or externally;
 *
 * [b] so that accessing bigger-than-a-bytes fields will never
 *     generate bus errors on any platform, even when the location of
 *     its descriptor inside a bundle isn't "naturally aligned", and
 *
 * [c] for consistency, removing all doubt even when it appears to
 *     someone that the two other points are non-issues for that
 *     particular descriptor type.
 */
#ifndef __LINUX_USB_CH9_H
#define __LINUX_USB_CH9_H

#include <uapi/linux/usb/ch9.h>

/* USB 3.2 SuperSpeed Plus phy signaling rate generation and lane count */

enum usb_ssp_rate {
	USB_SSP_GEN_UNKNOWN = 0,
	USB_SSP_GEN_2x1,
	USB_SSP_GEN_1x2,
	USB_SSP_GEN_2x2,
};

struct device;

extern const char *usb_ep_type_string(int ep_type);
extern const char *usb_speed_string(enum usb_device_speed speed);
extern enum usb_device_speed usb_get_maximum_speed(struct device *dev);
extern enum usb_ssp_rate usb_get_maximum_ssp_rate(struct device *dev);
extern const char *usb_state_string(enum usb_device_state state);
unsigned int usb_decode_interval(const struct usb_endpoint_descriptor *epd,
				 enum usb_device_speed speed);

#ifdef CONFIG_TRACING
extern const char *usb_decode_ctrl(char *str, size_t size, __u8 bRequestType,
				   __u8 bRequest, __u16 wValue, __u16 wIndex,
				   __u16 wLength);
#endif

#endif /* __LINUX_USB_CH9_H */
