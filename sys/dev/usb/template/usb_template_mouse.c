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
 * This file contains the USB template for an USB Mouse Device.
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
	MOUSE_LANG_INDEX,
	MOUSE_INTERFACE_INDEX,
	MOUSE_MANUFACTURER_INDEX,
	MOUSE_PRODUCT_INDEX,
	MOUSE_SERIAL_NUMBER_INDEX,
	MOUSE_MAX_INDEX,
};

#define	MOUSE_DEFAULT_VENDOR_ID		USB_TEMPLATE_VENDOR
#define	MOUSE_DEFAULT_PRODUCT_ID	0x27da
#define	MOUSE_DEFAULT_INTERFACE		"Mouse interface"
#define	MOUSE_DEFAULT_MANUFACTURER	USB_TEMPLATE_MANUFACTURER
#define	MOUSE_DEFAULT_PRODUCT		"Mouse Test Interface"
#define	MOUSE_DEFAULT_SERIAL_NUMBER	"March 2008"

static struct usb_string_descriptor	mouse_interface;
static struct usb_string_descriptor	mouse_manufacturer;
static struct usb_string_descriptor	mouse_product;
static struct usb_string_descriptor	mouse_serial_number;

static struct sysctl_ctx_list		mouse_ctx_list;

/* prototypes */

/* The following HID descriptor was dumped from a HP mouse. */

static uint8_t mouse_hid_descriptor[] = {
	0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x09, 0x01,
	0xa1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x03,
	0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01,
	0x81, 0x02, 0x95, 0x05, 0x81, 0x03, 0x05, 0x01,
	0x09, 0x30, 0x09, 0x31, 0x09, 0x38, 0x15, 0x81,
	0x25, 0x7f, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06,
	0xc0, 0xc0
};

static const struct usb_temp_packet_size mouse_intr_mps = {
	.mps[USB_SPEED_LOW] = 8,
	.mps[USB_SPEED_FULL] = 8,
	.mps[USB_SPEED_HIGH] = 8,
};

static const struct usb_temp_interval mouse_intr_interval = {
	.bInterval[USB_SPEED_LOW] = 2,		/* 2ms */
	.bInterval[USB_SPEED_FULL] = 2,		/* 2ms */
	.bInterval[USB_SPEED_HIGH] = 5,		/* 2ms */
};

static const struct usb_temp_endpoint_desc mouse_ep_0 = {
	.ppRawDesc = NULL,		/* no raw descriptors */
	.pPacketSize = &mouse_intr_mps,
	.pIntervals = &mouse_intr_interval,
	.bEndpointAddress = UE_DIR_IN,
	.bmAttributes = UE_INTERRUPT,
};

static const struct usb_temp_endpoint_desc *mouse_endpoints[] = {
	&mouse_ep_0,
	NULL,
};

static const uint8_t mouse_raw_desc[] = {
	0x09, 0x21, 0x10, 0x01, 0x00, 0x01, 0x22, sizeof(mouse_hid_descriptor),
	0x00
};

static const void *mouse_iface_0_desc[] = {
	mouse_raw_desc,
	NULL,
};

static const struct usb_temp_interface_desc mouse_iface_0 = {
	.ppRawDesc = mouse_iface_0_desc,
	.ppEndpoints = mouse_endpoints,
	.bInterfaceClass = UICLASS_HID,
	.bInterfaceSubClass = UISUBCLASS_BOOT,
	.bInterfaceProtocol = UIPROTO_MOUSE,
	.iInterface = MOUSE_INTERFACE_INDEX,
};

static const struct usb_temp_interface_desc *mouse_interfaces[] = {
	&mouse_iface_0,
	NULL,
};

static const struct usb_temp_config_desc mouse_config_desc = {
	.ppIfaceDesc = mouse_interfaces,
	.bmAttributes = 0,
	.bMaxPower = 0,
	.iConfiguration = MOUSE_INTERFACE_INDEX,
};

static const struct usb_temp_config_desc *mouse_configs[] = {
	&mouse_config_desc,
	NULL,
};

static usb_temp_get_string_desc_t mouse_get_string_desc;
static usb_temp_get_vendor_desc_t mouse_get_vendor_desc;

struct usb_temp_device_desc usb_template_mouse = {
	.getStringDesc = &mouse_get_string_desc,
	.getVendorDesc = &mouse_get_vendor_desc,
	.ppConfigDesc = mouse_configs,
	.idVendor = MOUSE_DEFAULT_VENDOR_ID,
	.idProduct = MOUSE_DEFAULT_PRODUCT_ID,
	.bcdDevice = 0x0100,
	.bDeviceClass = UDCLASS_COMM,
	.bDeviceSubClass = 0,
	.bDeviceProtocol = 0,
	.iManufacturer = MOUSE_MANUFACTURER_INDEX,
	.iProduct = MOUSE_PRODUCT_INDEX,
	.iSerialNumber = MOUSE_SERIAL_NUMBER_INDEX,
};

/*------------------------------------------------------------------------*
 *      mouse_get_vendor_desc
 *
 * Return values:
 * NULL: Failure. No such vendor descriptor.
 * Else: Success. Pointer to vendor descriptor is returned.
 *------------------------------------------------------------------------*/
static const void *
mouse_get_vendor_desc(const struct usb_device_request *req, uint16_t *plen)
{
	if ((req->bmRequestType == 0x81) && (req->bRequest == 0x06) &&
	    (req->wValue[0] == 0x00) && (req->wValue[1] == 0x22) &&
	    (req->wIndex[1] == 0) && (req->wIndex[0] == 0)) {

		*plen = sizeof(mouse_hid_descriptor);
		return (mouse_hid_descriptor);
	}
	return (NULL);
}

/*------------------------------------------------------------------------*
 *	mouse_get_string_desc
 *
 * Return values:
 * NULL: Failure. No such string.
 * Else: Success. Pointer to string descriptor is returned.
 *------------------------------------------------------------------------*/
static const void *
mouse_get_string_desc(uint16_t lang_id, uint8_t string_index)
{
	static const void *ptr[MOUSE_MAX_INDEX] = {
		[MOUSE_LANG_INDEX] = &usb_string_lang_en,
		[MOUSE_INTERFACE_INDEX] = &mouse_interface,
		[MOUSE_MANUFACTURER_INDEX] = &mouse_manufacturer,
		[MOUSE_PRODUCT_INDEX] = &mouse_product,
		[MOUSE_SERIAL_NUMBER_INDEX] = &mouse_serial_number,
	};

	if (string_index == 0) {
		return (&usb_string_lang_en);
	}
	if (lang_id != 0x0409) {
		return (NULL);
	}
	if (string_index < MOUSE_MAX_INDEX) {
		return (ptr[string_index]);
	}
	return (NULL);
}

static void
mouse_init(void *arg __unused)
{
	struct sysctl_oid *parent;
	char parent_name[3];

	usb_make_str_desc(&mouse_interface, sizeof(mouse_interface),
	    MOUSE_DEFAULT_INTERFACE);
	usb_make_str_desc(&mouse_manufacturer, sizeof(mouse_manufacturer),
	    MOUSE_DEFAULT_MANUFACTURER);
	usb_make_str_desc(&mouse_product, sizeof(mouse_product),
	    MOUSE_DEFAULT_PRODUCT);
	usb_make_str_desc(&mouse_serial_number, sizeof(mouse_serial_number),
	    MOUSE_DEFAULT_SERIAL_NUMBER);

	snprintf(parent_name, sizeof(parent_name), "%d", USB_TEMP_MOUSE);
	sysctl_ctx_init(&mouse_ctx_list);

	parent = SYSCTL_ADD_NODE(&mouse_ctx_list,
	    SYSCTL_STATIC_CHILDREN(_hw_usb_templates), OID_AUTO,
	    parent_name, CTLFLAG_RW,
	    0, "USB Mouse device side template");
	SYSCTL_ADD_U16(&mouse_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "vendor_id", CTLFLAG_RWTUN,
	    &usb_template_mouse.idVendor, 1, "Vendor identifier");
	SYSCTL_ADD_U16(&mouse_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "product_id", CTLFLAG_RWTUN,
	    &usb_template_mouse.idProduct, 1, "Product identifier");
#if 0
	SYSCTL_ADD_PROC(&mouse_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "interface", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &mouse_interface, sizeof(mouse_interface), usb_temp_sysctl,
	    "A", "Interface string");
#endif
	SYSCTL_ADD_PROC(&mouse_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "manufacturer", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &mouse_manufacturer, sizeof(mouse_manufacturer), usb_temp_sysctl,
	    "A", "Manufacturer string");
	SYSCTL_ADD_PROC(&mouse_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "product", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &mouse_product, sizeof(mouse_product), usb_temp_sysctl,
	    "A", "Product string");
	SYSCTL_ADD_PROC(&mouse_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "serial_number", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &mouse_serial_number, sizeof(mouse_serial_number), usb_temp_sysctl,
	    "A", "Serial number string");
}

static void
mouse_uninit(void *arg __unused)
{

	sysctl_ctx_free(&mouse_ctx_list);
}

SYSINIT(mouse_init, SI_SUB_LOCK, SI_ORDER_FIRST, mouse_init, NULL);
SYSUNINIT(mouse_uninit, SI_SUB_LOCK, SI_ORDER_FIRST, mouse_uninit, NULL);
