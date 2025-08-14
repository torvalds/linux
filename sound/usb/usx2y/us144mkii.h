/* SPDX-License-Identifier: GPL-2.0-only */
// Copyright (c) 2025 Šerif Rami <ramiserifpersia@gmail.com>

#ifndef __US144MKII_H
#define __US144MKII_H

#include <linux/usb.h>
#include <sound/core.h>
#include <sound/initval.h>

#define DRIVER_NAME "us144mkii"

/* --- USB Device Identification --- */
#define USB_VID_TASCAM 0x0644
#define USB_PID_TASCAM_US144 0x800f
#define USB_PID_TASCAM_US144MKII 0x8020

/* --- USB Control Message Protocol --- */
#define RT_D2H_VENDOR_DEV (USB_DIR_IN|USB_TYPE_VENDOR|USB_RECIP_DEVICE)
#define VENDOR_REQ_MODE_CONTROL 0x49
#define MODE_VAL_HANDSHAKE_READ 0x0000
#define USB_CTRL_TIMEOUT_MS 1000

/**
 * struct tascam_card - Driver data structure for TASCAM US-144MKII.
 * @dev: Pointer to the USB device.
 * @iface0: Pointer to USB interface 0 (audio).
 * @iface1: Pointer to USB interface 1 (MIDI).
 * @card: Pointer to the ALSA sound card instance.
 */
struct tascam_card {
	struct usb_device *dev;
	struct usb_interface *iface0;
	struct usb_interface *iface1;
	struct snd_card *card;
};

 #endif /* __US144MKII_H */
