/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Hans Petter Selasky
 * Copyright (c) 2018 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Edward Tomasz Napierala
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * This file contains the USB template for an USB Keyboard Device.
 */

#ifdef USB_GLOBAL_INCLUDE_FILE
#include USB_GLOBAL_INCLUDE_FILE
#else
#include <sys/stdint.h>
#include <sys/stddef.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/sysctl.h>
#include <sys/sx.h>
#include <sys/unistd.h>
#include <sys/callout.h>
#include <sys/malloc.h>
#include <sys/priv.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usb_core.h>
#include <dev/usb/usb_cdc.h>
#include <dev/usb/usb_ioctl.h>
#include <dev/usb/usb_util.h>

#include <dev/usb/template/usb_template.h>
#endif			/* USB_GLOBAL_INCLUDE_FILE */

enum {
	KBD_LANG_INDEX,
	KBD_INTERFACE_INDEX,
	KBD_MANUFACTURER_INDEX,
	KBD_PRODUCT_INDEX,
	KBD_SERIAL_NUMBER_INDEX,
	KBD_MAX_INDEX,
};

#define	KBD_DEFAULT_VENDOR_ID		USB_TEMPLATE_VENDOR
#define	KBD_DEFAULT_PRODUCT_ID		0x27db
#define	KBD_DEFAULT_INTERFACE		"Keyboard Interface"
#define	KBD_DEFAULT_MANUFACTURER	USB_TEMPLATE_MANUFACTURER
#define	KBD_DEFAULT_PRODUCT		"Keyboard Test Device"
#define	KBD_DEFAULT_SERIAL_NUMBER	"March 2008"

static struct usb_string_descriptor	kbd_interface;
static struct usb_string_descriptor	kbd_manufacturer;
static struct usb_string_descriptor	kbd_product;
static struct usb_string_descriptor	kbd_serial_number;

static struct sysctl_ctx_list		kbd_ctx_list;

/* prototypes */

static const struct usb_temp_packet_size keyboard_intr_mps = {
	.mps[USB_SPEED_LOW] = 16,
	.mps[USB_SPEED_FULL] = 16,
	.mps[USB_SPEED_HIGH] = 16,
};

static const struct usb_temp_interval keyboard_intr_interval = {
	.bInterval[USB_SPEED_LOW] = 2,	/* 2 ms */
	.bInterval[USB_SPEED_FULL] = 2,	/* 2 ms */
	.bInterval[USB_SPEED_HIGH] = 5,	/* 2 ms */
};

/* The following HID descriptor was dumped from a HP keyboard. */

static uint8_t keyboard_hid_descriptor[] = {
	0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x05, 0x07,
	0x19, 0xe0, 0x29, 0xe7, 0x15, 0x00, 0x25, 0x01,
	0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01,
	0x75, 0x08, 0x81, 0x01, 0x95, 0x03, 0x75, 0x01,
	0x05, 0x08, 0x19, 0x01, 0x29, 0x03, 0x91, 0x02,
	0x95, 0x05, 0x75, 0x01, 0x91, 0x01, 0x95, 0x06,
	0x75, 0x08, 0x15, 0x00, 0x26, 0xff, 0x00, 0x05,
	0x07, 0x19, 0x00, 0x2a, 0xff, 0x00, 0x81, 0x00,
	0xc0
};

static const struct usb_temp_endpoint_desc keyboard_ep_0 = {
	.ppRawDesc = NULL,		/* no raw descriptors */
	.pPacketSize = &keyboard_intr_mps,
	.pIntervals = &keyboard_intr_interval,
	.bEndpointAddress = UE_DIR_IN,
	.bmAttributes = UE_INTERRUPT,
};

static const struct usb_temp_endpoint_desc *keyboard_endpoints[] = {
	&keyboard_ep_0,
	NULL,
};

static const uint8_t keyboard_raw_desc[] = {
	0x09, 0x21, 0x10, 0x01, 0x00, 0x01, 0x22, sizeof(keyboard_hid_descriptor),
	0x00
};

static const void *keyboard_iface_0_desc[] = {
	keyboard_raw_desc,
	NULL,
};

static const struct usb_temp_interface_desc keyboard_iface_0 = {
	.ppRawDesc = keyboard_iface_0_desc,
	.ppEndpoints = keyboard_endpoints,
	.bInterfaceClass = UICLASS_HID,
	.bInterfaceSubClass = UISUBCLASS_BOOT,
	.bInterfaceProtocol = UIPROTO_BOOT_KEYBOARD,
	.iInterface = KBD_INTERFACE_INDEX,
};

static const struct usb_temp_interface_desc *keyboard_interfaces[] = {
	&keyboard_iface_0,
	NULL,
};

static const struct usb_temp_config_desc keyboard_config_desc = {
	.ppIfaceDesc = keyboard_interfaces,
	.bmAttributes = 0,
	.bMaxPower = 0,
	.iConfiguration = KBD_PRODUCT_INDEX,
};

static const struct usb_temp_config_desc *keyboard_configs[] = {
	&keyboard_config_desc,
	NULL,
};

static usb_temp_get_string_desc_t keyboard_get_string_desc;
static usb_temp_get_vendor_desc_t keyboard_get_vendor_desc;

struct usb_temp_device_desc usb_template_kbd = {
	.getStringDesc = &keyboard_get_string_desc,
	.getVendorDesc = &keyboard_get_vendor_desc,
	.ppConfigDesc = keyboard_configs,
	.idVendor = KBD_DEFAULT_VENDOR_ID,
	.idProduct = KBD_DEFAULT_PRODUCT_ID,
	.bcdDevice = 0x0100,
	.bDeviceClass = UDCLASS_COMM,
	.bDeviceSubClass = 0,
	.bDeviceProtocol = 0,
	.iManufacturer = KBD_MANUFACTURER_INDEX,
	.iProduct = KBD_PRODUCT_INDEX,
	.iSerialNumber = KBD_SERIAL_NUMBER_INDEX,
};

/*------------------------------------------------------------------------*
 *      keyboard_get_vendor_desc
 *
 * Return values:
 * NULL: Failure. No such vendor descriptor.
 * Else: Success. Pointer to vendor descriptor is returned.
 *------------------------------------------------------------------------*/
static const void *
keyboard_get_vendor_desc(const struct usb_device_request *req, uint16_t *plen)
{
	if ((req->bmRequestType == 0x81) && (req->bRequest == 0x06) &&
	    (req->wValue[0] == 0x00) && (req->wValue[1] == 0x22) &&
	    (req->wIndex[1] == 0) && (req->wIndex[0] == 0)) {

		*plen = sizeof(keyboard_hid_descriptor);
		return (keyboard_hid_descriptor);
	}
	return (NULL);
}

/*------------------------------------------------------------------------*
 *	keyboard_get_string_desc
 *
 * Return values:
 * NULL: Failure. No such string.
 * Else: Success. Pointer to string descriptor is returned.
 *------------------------------------------------------------------------*/
static const void *
keyboard_get_string_desc(uint16_t lang_id, uint8_t string_index)
{
	static const void *ptr[KBD_MAX_INDEX] = {
		[KBD_LANG_INDEX] = &usb_string_lang_en,
		[KBD_INTERFACE_INDEX] = &kbd_interface,
		[KBD_MANUFACTURER_INDEX] = &kbd_manufacturer,
		[KBD_PRODUCT_INDEX] = &kbd_product,
		[KBD_SERIAL_NUMBER_INDEX] = &kbd_serial_number,
	};

	if (string_index == 0) {
		return (&usb_string_lang_en);
	}
	if (lang_id != 0x0409) {
		return (NULL);
	}
	if (string_index < KBD_MAX_INDEX) {
		return (ptr[string_index]);
	}
	return (NULL);
}

static void
kbd_init(void *arg __unused)
{
	struct sysctl_oid *parent;
	char parent_name[3];

	usb_make_str_desc(&kbd_interface, sizeof(kbd_interface),
	    KBD_DEFAULT_INTERFACE);
	usb_make_str_desc(&kbd_manufacturer, sizeof(kbd_manufacturer),
	    KBD_DEFAULT_MANUFACTURER);
	usb_make_str_desc(&kbd_product, sizeof(kbd_product),
	    KBD_DEFAULT_PRODUCT);
	usb_make_str_desc(&kbd_serial_number, sizeof(kbd_serial_number),
	    KBD_DEFAULT_SERIAL_NUMBER);

	snprintf(parent_name, sizeof(parent_name), "%d", USB_TEMP_KBD);
	sysctl_ctx_init(&kbd_ctx_list);

	parent = SYSCTL_ADD_NODE(&kbd_ctx_list,
	    SYSCTL_STATIC_CHILDREN(_hw_usb_templates), OID_AUTO,
	    parent_name, CTLFLAG_RW,
	    0, "USB Keyboard device side template");
	SYSCTL_ADD_U16(&kbd_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "vendor_id", CTLFLAG_RWTUN,
	    &usb_template_kbd.idVendor, 1, "Vendor identifier");
	SYSCTL_ADD_U16(&kbd_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "product_id", CTLFLAG_RWTUN,
	    &usb_template_kbd.idProduct, 1, "Product identifier");
#if 0
	SYSCTL_ADD_PROC(&kbd_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "interface", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &kbd_interface, sizeof(kbd_interface), usb_temp_sysctl,
	    "A", "Interface string");
#endif
	SYSCTL_ADD_PROC(&kbd_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "manufacturer", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &kbd_manufacturer, sizeof(kbd_manufacturer), usb_temp_sysctl,
	    "A", "Manufacturer string");
	SYSCTL_ADD_PROC(&kbd_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "product", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &kbd_product, sizeof(kbd_product), usb_temp_sysctl,
	    "A", "Product string");
	SYSCTL_ADD_PROC(&kbd_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "serial_number", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &kbd_serial_number, sizeof(kbd_serial_number), usb_temp_sysctl,
	    "A", "Serial number string");
}

static void
kbd_uninit(void *arg __unused)
{

	sysctl_ctx_free(&kbd_ctx_list);
}

SYSINIT(kbd_init, SI_SUB_LOCK, SI_ORDER_FIRST, kbd_init, NULL);
SYSUNINIT(kbd_uninit, SI_SUB_LOCK, SI_ORDER_FIRST, kbd_uninit, NULL);
