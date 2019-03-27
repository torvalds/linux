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
 * This file contains the USB template for an USB Modem Device.
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
	MODEM_LANG_INDEX,
	MODEM_INTERFACE_INDEX,
	MODEM_MANUFACTURER_INDEX,
	MODEM_PRODUCT_INDEX,
	MODEM_SERIAL_NUMBER_INDEX,
	MODEM_MAX_INDEX,
};

#define	MODEM_DEFAULT_VENDOR_ID		USB_TEMPLATE_VENDOR
#define	MODEM_DEFAULT_PRODUCT_ID	0x27dd
#define	MODEM_DEFAULT_INTERFACE		"Virtual serial port"
#define	MODEM_DEFAULT_MANUFACTURER	USB_TEMPLATE_MANUFACTURER
#define	MODEM_DEFAULT_PRODUCT		"Virtual serial port"
/*
 * The reason for this being called like this is that OSX
 * derives the device node name from it, resulting in a somewhat
 * user-friendly "/dev/cu.usbmodemFreeBSD1".  And yes, the "1"
 * needs to be there, otherwise OSX will mangle it.
 */
#define	MODEM_DEFAULT_SERIAL_NUMBER	"FreeBSD1"

static struct usb_string_descriptor	modem_interface;
static struct usb_string_descriptor	modem_manufacturer;
static struct usb_string_descriptor	modem_product;
static struct usb_string_descriptor	modem_serial_number;

static struct sysctl_ctx_list		modem_ctx_list;

#define	MODEM_IFACE_0 0
#define	MODEM_IFACE_1 1

/* prototypes */

static const struct usb_temp_packet_size modem_bulk_mps = {
	.mps[USB_SPEED_LOW] = 8,
	.mps[USB_SPEED_FULL] = 64,
	.mps[USB_SPEED_HIGH] = 512,
};

static const struct usb_temp_packet_size modem_intr_mps = {
	.mps[USB_SPEED_LOW] = 8,
	.mps[USB_SPEED_FULL] = 8,
	.mps[USB_SPEED_HIGH] = 8,
};

static const struct usb_temp_interval modem_intr_interval = {
	.bInterval[USB_SPEED_LOW] = 8,	/* 8ms */
	.bInterval[USB_SPEED_FULL] = 8,	/* 8ms */
	.bInterval[USB_SPEED_HIGH] = 7,	/* 8ms */
};

static const struct usb_temp_endpoint_desc modem_ep_0 = {
	.pPacketSize = &modem_intr_mps,
	.pIntervals = &modem_intr_interval,
	.bEndpointAddress = UE_DIR_IN,
	.bmAttributes = UE_INTERRUPT,
};

static const struct usb_temp_endpoint_desc modem_ep_1 = {
	.pPacketSize = &modem_bulk_mps,
	.bEndpointAddress = UE_DIR_OUT,
	.bmAttributes = UE_BULK,
};

static const struct usb_temp_endpoint_desc modem_ep_2 = {
	.pPacketSize = &modem_bulk_mps,
	.bEndpointAddress = UE_DIR_IN,
	.bmAttributes = UE_BULK,
};

static const struct usb_temp_endpoint_desc *modem_iface_0_ep[] = {
	&modem_ep_0,
	NULL,
};

static const struct usb_temp_endpoint_desc *modem_iface_1_ep[] = {
	&modem_ep_1,
	&modem_ep_2,
	NULL,
};

static const uint8_t modem_raw_desc_0[] = {
	0x05, 0x24, 0x00, 0x10, 0x01
};

static const uint8_t modem_raw_desc_1[] = {
	0x05, 0x24, 0x06, MODEM_IFACE_0, MODEM_IFACE_1
};

static const uint8_t modem_raw_desc_2[] = {
	0x05, 0x24, 0x01, 0x03, MODEM_IFACE_1
};

static const uint8_t modem_raw_desc_3[] = {
	0x04, 0x24, 0x02, 0x07
};

static const void *modem_iface_0_desc[] = {
	&modem_raw_desc_0,
	&modem_raw_desc_1,
	&modem_raw_desc_2,
	&modem_raw_desc_3,
	NULL,
};

static const struct usb_temp_interface_desc modem_iface_0 = {
	.ppRawDesc = modem_iface_0_desc,
	.ppEndpoints = modem_iface_0_ep,
	.bInterfaceClass = UICLASS_CDC,
	.bInterfaceSubClass = UISUBCLASS_ABSTRACT_CONTROL_MODEL,
	.bInterfaceProtocol = UIPROTO_CDC_NONE,
	.iInterface = MODEM_INTERFACE_INDEX,
};

static const struct usb_temp_interface_desc modem_iface_1 = {
	.ppEndpoints = modem_iface_1_ep,
	.bInterfaceClass = UICLASS_CDC_DATA,
	.bInterfaceSubClass = UISUBCLASS_DATA,
	.bInterfaceProtocol = UIPROTO_CDC_NONE,
	.iInterface = MODEM_INTERFACE_INDEX,
};

static const struct usb_temp_interface_desc *modem_interfaces[] = {
	&modem_iface_0,
	&modem_iface_1,
	NULL,
};

static const struct usb_temp_config_desc modem_config_desc = {
	.ppIfaceDesc = modem_interfaces,
	.bmAttributes = 0,
	.bMaxPower = 0,
	.iConfiguration = MODEM_PRODUCT_INDEX,
};

static const struct usb_temp_config_desc *modem_configs[] = {
	&modem_config_desc,
	NULL,
};

static usb_temp_get_string_desc_t modem_get_string_desc;
static usb_temp_get_vendor_desc_t modem_get_vendor_desc;

struct usb_temp_device_desc usb_template_modem = {
	.getStringDesc = &modem_get_string_desc,
	.getVendorDesc = &modem_get_vendor_desc,
	.ppConfigDesc = modem_configs,
	.idVendor = MODEM_DEFAULT_VENDOR_ID,
	.idProduct = MODEM_DEFAULT_PRODUCT_ID,
	.bcdDevice = 0x0100,
	.bDeviceClass = UDCLASS_COMM,
	.bDeviceSubClass = 0,
	.bDeviceProtocol = 0,
	.iManufacturer = MODEM_MANUFACTURER_INDEX,
	.iProduct = MODEM_PRODUCT_INDEX,
	.iSerialNumber = MODEM_SERIAL_NUMBER_INDEX,
};

/*------------------------------------------------------------------------*
 *      modem_get_vendor_desc
 *
 * Return values:
 * NULL: Failure. No such vendor descriptor.
 * Else: Success. Pointer to vendor descriptor is returned.
 *------------------------------------------------------------------------*/
static const void *
modem_get_vendor_desc(const struct usb_device_request *req, uint16_t *plen)
{
	return (NULL);
}

/*------------------------------------------------------------------------*
 *	modem_get_string_desc
 *
 * Return values:
 * NULL: Failure. No such string.
 * Else: Success. Pointer to string descriptor is returned.
 *------------------------------------------------------------------------*/
static const void *
modem_get_string_desc(uint16_t lang_id, uint8_t string_index)
{
	static const void *ptr[MODEM_MAX_INDEX] = {
		[MODEM_LANG_INDEX] = &usb_string_lang_en,
		[MODEM_INTERFACE_INDEX] = &modem_interface,
		[MODEM_MANUFACTURER_INDEX] = &modem_manufacturer,
		[MODEM_PRODUCT_INDEX] = &modem_product,
		[MODEM_SERIAL_NUMBER_INDEX] = &modem_serial_number,
	};

	if (string_index == 0) {
		return (&usb_string_lang_en);
	}
	if (lang_id != 0x0409) {
		return (NULL);
	}
	if (string_index < MODEM_MAX_INDEX) {
		return (ptr[string_index]);
	}
	return (NULL);
}

static void
modem_init(void *arg __unused)
{
	struct sysctl_oid *parent;
	char parent_name[3];

	usb_make_str_desc(&modem_interface, sizeof(modem_interface),
	    MODEM_DEFAULT_INTERFACE);
	usb_make_str_desc(&modem_manufacturer, sizeof(modem_manufacturer),
	    MODEM_DEFAULT_MANUFACTURER);
	usb_make_str_desc(&modem_product, sizeof(modem_product),
	    MODEM_DEFAULT_PRODUCT);
	usb_make_str_desc(&modem_serial_number, sizeof(modem_serial_number),
	    MODEM_DEFAULT_SERIAL_NUMBER);

	snprintf(parent_name, sizeof(parent_name), "%d", USB_TEMP_MODEM);
	sysctl_ctx_init(&modem_ctx_list);

	parent = SYSCTL_ADD_NODE(&modem_ctx_list,
	    SYSCTL_STATIC_CHILDREN(_hw_usb_templates), OID_AUTO,
	    parent_name, CTLFLAG_RW,
	    0, "Virtual serial port device side template");
	SYSCTL_ADD_U16(&modem_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "vendor_id", CTLFLAG_RWTUN,
	    &usb_template_modem.idVendor, 1, "Vendor identifier");
	SYSCTL_ADD_U16(&modem_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "product_id", CTLFLAG_RWTUN,
	    &usb_template_modem.idProduct, 1, "Product identifier");
#if 0
	SYSCTL_ADD_PROC(&modem_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "keyboard", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &modem_interface, sizeof(modem_interface), usb_temp_sysctl,
	    "A", "Interface string");
#endif
	SYSCTL_ADD_PROC(&modem_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "manufacturer", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &modem_manufacturer, sizeof(modem_manufacturer), usb_temp_sysctl,
	    "A", "Manufacturer string");
	SYSCTL_ADD_PROC(&modem_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "product", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &modem_product, sizeof(modem_product), usb_temp_sysctl,
	    "A", "Product string");
	SYSCTL_ADD_PROC(&modem_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "serial_number", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &modem_serial_number, sizeof(modem_serial_number), usb_temp_sysctl,
	    "A", "Serial number string");
}

static void
modem_uninit(void *arg __unused)
{

	sysctl_ctx_free(&modem_ctx_list);
}

SYSINIT(modem_init, SI_SUB_LOCK, SI_ORDER_FIRST, modem_init, NULL);
SYSUNINIT(modem_uninit, SI_SUB_LOCK, SI_ORDER_FIRST, modem_uninit, NULL);
