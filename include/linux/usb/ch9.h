/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file holds USB constants and structures that are needed for
 * USB device APIs.  These are used by the USB device model, which is
 * defined in chapter 9 of the USB 2.0 specification and in the
 * Wireless USB 1.0 (spread around).  Linux has several APIs in C that
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
 * There's also "Wireless USB", using low power short range radios for
 * peripheral interconnection but otherwise building on the USB framework.
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

#include <linux/device.h>
#include <uapi/linux/usb/ch9.h>

/**
 * usb_ep_type_string() - Returns human readable-name of the endpoint type.
 * @ep_type: The endpoint type to return human-readable name for.  If it's not
 *   any of the types: USB_ENDPOINT_XFER_{CONTROL, ISOC, BULK, INT},
 *   usually got by usb_endpoint_type(), the string 'unknown' will be returned.
 */
extern const char *usb_ep_type_string(int ep_type);

/**
 * usb_speed_string() - Returns human readable-name of the speed.
 * @speed: The speed to return human-readable name for.  If it's not
 *   any of the speeds defined in usb_device_speed enum, string for
 *   USB_SPEED_UNKNOWN will be returned.
 */
extern const char *usb_speed_string(enum usb_device_speed speed);

/**
 * usb_get_maximum_speed - Get maximum requested speed for a given USB
 * controller.
 * @dev: Pointer to the given USB controller device
 *
 * The function gets the maximum speed string from property "maximum-speed",
 * and returns the corresponding enum usb_device_speed.
 */
extern enum usb_device_speed usb_get_maximum_speed(struct device *dev);

/**
 * usb_state_string - Returns human readable name for the state.
 * @state: The state to return a human-readable name for. If it's not
 *	any of the states devices in usb_device_state_string enum,
 *	the string UNKNOWN will be returned.
 */
extern const char *usb_state_string(enum usb_device_state state);

#ifdef CONFIG_TRACING
/**
 * usb_decode_ctrl - Returns human readable representation of control request.
 * @str: buffer to return a human-readable representation of control request.
 *       This buffer should have about 200 bytes.
 * @size: size of str buffer.
 * @bRequestType: matches the USB bmRequestType field
 * @bRequest: matches the USB bRequest field
 * @wValue: matches the USB wValue field (CPU byte order)
 * @wIndex: matches the USB wIndex field (CPU byte order)
 * @wLength: matches the USB wLength field (CPU byte order)
 *
 * Function returns decoded, formatted and human-readable description of
 * control request packet.
 *
 * The usage scenario for this is for tracepoints, so function as a return
 * use the same value as in parameters. This approach allows to use this
 * function in TP_printk
 *
 * Important: wValue, wIndex, wLength parameters before invoking this function
 * should be processed by le16_to_cpu macro.
 */
extern const char *usb_decode_ctrl(char *str, size_t size, __u8 bRequestType,
				   __u8 bRequest, __u16 wValue, __u16 wIndex,
				   __u16 wLength);
#endif

#endif /* __LINUX_USB_CH9_H */
