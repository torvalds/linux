/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2014 Hans Petter Selasky
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
 * This file contains the USB template for an USB phone device.
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
	PHONE_LANG_INDEX,
	PHONE_MIXER_INDEX,
	PHONE_RECORD_INDEX,
	PHONE_PLAYBACK_INDEX,
	PHONE_HID_INDEX,
	PHONE_MANUFACTURER_INDEX,
	PHONE_PRODUCT_INDEX,
	PHONE_SERIAL_NUMBER_INDEX,
	PHONE_MAX_INDEX,
};

#define	PHONE_DEFAULT_VENDOR_ID		USB_TEMPLATE_VENDOR
#define	PHONE_DEFAULT_PRODUCT_ID	0x05dc
#define	PHONE_DEFAULT_MIXER		"Mixer interface"
#define	PHONE_DEFAULT_RECORD		"Record interface"
#define	PHONE_DEFAULT_PLAYBACK		"Playback interface"
#define	PHONE_DEFAULT_HID		"HID interface"
#define	PHONE_DEFAULT_MANUFACTURER	USB_TEMPLATE_MANUFACTURER
#define	PHONE_DEFAULT_PRODUCT		"USB Phone Device"
#define	PHONE_DEFAULT_SERIAL_NUMBER	"March 2008"

static struct usb_string_descriptor	phone_mixer;
static struct usb_string_descriptor	phone_record;
static struct usb_string_descriptor	phone_playback;
static struct usb_string_descriptor	phone_hid;
static struct usb_string_descriptor	phone_manufacturer;
static struct usb_string_descriptor	phone_product;
static struct usb_string_descriptor	phone_serial_number;

static struct sysctl_ctx_list		phone_ctx_list;

/* prototypes */

/*
 * Phone Mixer description structures
 *
 * Some of the phone descriptors were dumped from no longer in
 * production Yealink VOIP USB phone adapter:
 */
static uint8_t phone_hid_descriptor[] = {
	0x05, 0x0b, 0x09, 0x01, 0xa1, 0x01, 0x05, 0x09,
	0x19, 0x01, 0x29, 0x3f, 0x15, 0x00, 0x25, 0x01,
	0x75, 0x01, 0x95, 0x80, 0x81, 0x00, 0x05, 0x08,
	0x19, 0x01, 0x29, 0x10, 0x15, 0x00, 0x25, 0x01,
	0x75, 0x01, 0x95, 0x80, 0x91, 0x00, 0xc0
};

static const uint8_t phone_raw_desc_0[] = {
	0x0a, 0x24, 0x01, 0x00, 0x01, 0x4a, 0x00, 0x02,
	0x01, 0x02
};

static const uint8_t phone_raw_desc_1[] = {
	0x0c, 0x24, 0x02, 0x01, 0x01, 0x02, 0x00, 0x01,
	0x00, 0x00, 0x00, 0x00
};

static const uint8_t phone_raw_desc_2[] = {
	0x0c, 0x24, 0x02, 0x02, 0x01, 0x01, 0x00, 0x01,
	0x00, 0x00, 0x00, 0x00
};

static const uint8_t phone_raw_desc_3[] = {
	0x09, 0x24, 0x03, 0x03, 0x01, 0x03, 0x00, 0x06,
	0x00
};

static const uint8_t phone_raw_desc_4[] = {
	0x09, 0x24, 0x03, 0x04, 0x01, 0x01, 0x00, 0x05,
	0x00
};

static const uint8_t phone_raw_desc_5[] = {
	0x0b, 0x24, 0x06, 0x05, 0x01, 0x02, 0x03, 0x00,
	0x03, 0x00, 0x00
};

static const uint8_t phone_raw_desc_6[] = {
	0x0b, 0x24, 0x06, 0x06, 0x02, 0x02, 0x03, 0x00,
	0x03, 0x00, 0x00
};

static const void *phone_raw_iface_0_desc[] = {
	phone_raw_desc_0,
	phone_raw_desc_1,
	phone_raw_desc_2,
	phone_raw_desc_3,
	phone_raw_desc_4,
	phone_raw_desc_5,
	phone_raw_desc_6,
	NULL,
};

static const struct usb_temp_interface_desc phone_iface_0 = {
	.ppEndpoints = NULL,		/* no endpoints */
	.ppRawDesc = phone_raw_iface_0_desc,
	.bInterfaceClass = UICLASS_AUDIO,
	.bInterfaceSubClass = UISUBCLASS_AUDIOCONTROL,
	.bInterfaceProtocol = 0,
	.iInterface = PHONE_MIXER_INDEX,
};

static const uint8_t phone_raw_desc_20[] = {
	0x07, 0x24, 0x01, 0x04, 0x01, 0x01, 0x00
};

static const uint8_t phone_raw_desc_21[] = {
	0x0b, 0x24, 0x02, 0x01, 0x01, 0x02, 0x10, 0x01,
	/* 8kHz */
	0x40, 0x1f, 0x00
};

static const uint8_t phone_raw_desc_22[] = {
	0x07, 0x25, 0x01, 0x00, 0x00, 0x00, 0x00
};

static const void *phone_raw_iface_1_desc[] = {
	phone_raw_desc_20,
	phone_raw_desc_21,
	NULL,
};

static const void *phone_raw_ep_1_desc[] = {
	phone_raw_desc_22,
	NULL,
};

static const struct usb_temp_packet_size phone_isoc_mps = {
	.mps[USB_SPEED_FULL] = 0x10,
	.mps[USB_SPEED_HIGH] = 0x10,
};

static const struct usb_temp_interval phone_isoc_interval = {
	.bInterval[USB_SPEED_FULL] = 1,	/* 1:1 */
	.bInterval[USB_SPEED_HIGH] = 4,	/* 1:8 */
};

static const struct usb_temp_endpoint_desc phone_isoc_in_ep = {
	.ppRawDesc = phone_raw_ep_1_desc,
	.pPacketSize = &phone_isoc_mps,
	.pIntervals = &phone_isoc_interval,
	.bEndpointAddress = UE_DIR_IN,
	.bmAttributes = UE_ISOCHRONOUS,
};

static const struct usb_temp_endpoint_desc *phone_iface_1_ep[] = {
	&phone_isoc_in_ep,
	NULL,
};

static const struct usb_temp_interface_desc phone_iface_1_alt_0 = {
	.ppEndpoints = NULL,		/* no endpoints */
	.ppRawDesc = NULL,		/* no raw descriptors */
	.bInterfaceClass = UICLASS_AUDIO,
	.bInterfaceSubClass = UISUBCLASS_AUDIOSTREAM,
	.bInterfaceProtocol = 0,
	.iInterface = PHONE_PLAYBACK_INDEX,
};

static const struct usb_temp_interface_desc phone_iface_1_alt_1 = {
	.ppEndpoints = phone_iface_1_ep,
	.ppRawDesc = phone_raw_iface_1_desc,
	.bInterfaceClass = UICLASS_AUDIO,
	.bInterfaceSubClass = UISUBCLASS_AUDIOSTREAM,
	.bInterfaceProtocol = 0,
	.iInterface = PHONE_PLAYBACK_INDEX,
	.isAltInterface = 1,		/* this is an alternate setting */
};

static const uint8_t phone_raw_desc_30[] = {
	0x07, 0x24, 0x01, 0x02, 0x01, 0x01, 0x00
};

static const uint8_t phone_raw_desc_31[] = {
	0x0b, 0x24, 0x02, 0x01, 0x01, 0x02, 0x10, 0x01,
	/* 8kHz */
	0x40, 0x1f, 0x00
};

static const uint8_t phone_raw_desc_32[] = {
	0x07, 0x25, 0x01, 0x00, 0x00, 0x00, 0x00
};

static const void *phone_raw_iface_2_desc[] = {
	phone_raw_desc_30,
	phone_raw_desc_31,
	NULL,
};

static const void *phone_raw_ep_2_desc[] = {
	phone_raw_desc_32,
	NULL,
};

static const struct usb_temp_endpoint_desc phone_isoc_out_ep = {
	.ppRawDesc = phone_raw_ep_2_desc,
	.pPacketSize = &phone_isoc_mps,
	.pIntervals = &phone_isoc_interval,
	.bEndpointAddress = UE_DIR_OUT,
	.bmAttributes = UE_ISOCHRONOUS,
};

static const struct usb_temp_endpoint_desc *phone_iface_2_ep[] = {
	&phone_isoc_out_ep,
	NULL,
};

static const struct usb_temp_interface_desc phone_iface_2_alt_0 = {
	.ppEndpoints = NULL,		/* no endpoints */
	.ppRawDesc = NULL,		/* no raw descriptors */
	.bInterfaceClass = UICLASS_AUDIO,
	.bInterfaceSubClass = UISUBCLASS_AUDIOSTREAM,
	.bInterfaceProtocol = 0,
	.iInterface = PHONE_RECORD_INDEX,
};

static const struct usb_temp_interface_desc phone_iface_2_alt_1 = {
	.ppEndpoints = phone_iface_2_ep,
	.ppRawDesc = phone_raw_iface_2_desc,
	.bInterfaceClass = UICLASS_AUDIO,
	.bInterfaceSubClass = UISUBCLASS_AUDIOSTREAM,
	.bInterfaceProtocol = 0,
	.iInterface = PHONE_RECORD_INDEX,
	.isAltInterface = 1,		/* this is an alternate setting */
};

static const uint8_t phone_hid_raw_desc_0[] = {
	0x09, 0x21, 0x00, 0x01, 0x00, 0x01, 0x22, sizeof(phone_hid_descriptor),
	0x00
};

static const void *phone_hid_desc_0[] = {
	phone_hid_raw_desc_0,
	NULL,
};

static const struct usb_temp_packet_size phone_hid_mps = {
	.mps[USB_SPEED_FULL] = 0x10,
	.mps[USB_SPEED_HIGH] = 0x10,
};

static const struct usb_temp_interval phone_hid_interval = {
	.bInterval[USB_SPEED_FULL] = 2,		/* 2ms */
	.bInterval[USB_SPEED_HIGH] = 2,		/* 2ms */
};

static const struct usb_temp_endpoint_desc phone_hid_in_ep = {
	.pPacketSize = &phone_hid_mps,
	.pIntervals = &phone_hid_interval,
	.bEndpointAddress = UE_DIR_IN,
	.bmAttributes = UE_INTERRUPT,
};

static const struct usb_temp_endpoint_desc *phone_iface_3_ep[] = {
	&phone_hid_in_ep,
	NULL,
};

static const struct usb_temp_interface_desc phone_iface_3 = {
	.ppEndpoints = phone_iface_3_ep,
	.ppRawDesc = phone_hid_desc_0,
	.bInterfaceClass = UICLASS_HID,
	.bInterfaceSubClass = 0,
	.bInterfaceProtocol = 0,
	.iInterface = PHONE_HID_INDEX,
};

static const struct usb_temp_interface_desc *phone_interfaces[] = {
	&phone_iface_0,
	&phone_iface_1_alt_0,
	&phone_iface_1_alt_1,
	&phone_iface_2_alt_0,
	&phone_iface_2_alt_1,
	&phone_iface_3,
	NULL,
};

static const struct usb_temp_config_desc phone_config_desc = {
	.ppIfaceDesc = phone_interfaces,
	.bmAttributes = 0,
	.bMaxPower = 0,
	.iConfiguration = PHONE_PRODUCT_INDEX,
};

static const struct usb_temp_config_desc *phone_configs[] = {
	&phone_config_desc,
	NULL,
};

static usb_temp_get_string_desc_t phone_get_string_desc;
static usb_temp_get_vendor_desc_t phone_get_vendor_desc;

struct usb_temp_device_desc usb_template_phone = {
	.getStringDesc = &phone_get_string_desc,
	.getVendorDesc = &phone_get_vendor_desc,
	.ppConfigDesc = phone_configs,
	.idVendor = PHONE_DEFAULT_VENDOR_ID,
	.idProduct = PHONE_DEFAULT_PRODUCT_ID,
	.bcdDevice = 0x0100,
	.bDeviceClass = UDCLASS_IN_INTERFACE,
	.bDeviceSubClass = 0,
	.bDeviceProtocol = 0,
	.iManufacturer = PHONE_MANUFACTURER_INDEX,
	.iProduct = PHONE_PRODUCT_INDEX,
	.iSerialNumber = PHONE_SERIAL_NUMBER_INDEX,
};

/*------------------------------------------------------------------------*
 *      phone_get_vendor_desc
 *
 * Return values:
 * NULL: Failure. No such vendor descriptor.
 * Else: Success. Pointer to vendor descriptor is returned.
 *------------------------------------------------------------------------*/
static const void *
phone_get_vendor_desc(const struct usb_device_request *req, uint16_t *plen)
{
	if ((req->bmRequestType == 0x81) && (req->bRequest == 0x06) &&
	    (req->wValue[0] == 0x00) && (req->wValue[1] == 0x22) &&
	    (req->wIndex[1] == 0) && (req->wIndex[0] == 3 /* iface */)) {

		*plen = sizeof(phone_hid_descriptor);
		return (phone_hid_descriptor);
	}
	return (NULL);
}

/*------------------------------------------------------------------------*
 *	phone_get_string_desc
 *
 * Return values:
 * NULL: Failure. No such string.
 * Else: Success. Pointer to string descriptor is returned.
 *------------------------------------------------------------------------*/
static const void *
phone_get_string_desc(uint16_t lang_id, uint8_t string_index)
{
	static const void *ptr[PHONE_MAX_INDEX] = {
		[PHONE_LANG_INDEX] = &usb_string_lang_en,
		[PHONE_MIXER_INDEX] = &phone_mixer,
		[PHONE_RECORD_INDEX] = &phone_record,
		[PHONE_PLAYBACK_INDEX] = &phone_playback,
		[PHONE_HID_INDEX] = &phone_hid,
		[PHONE_MANUFACTURER_INDEX] = &phone_manufacturer,
		[PHONE_PRODUCT_INDEX] = &phone_product,
		[PHONE_SERIAL_NUMBER_INDEX] = &phone_serial_number,
	};

	if (string_index == 0) {
		return (&usb_string_lang_en);
	}
	if (lang_id != 0x0409) {
		return (NULL);
	}
	if (string_index < PHONE_MAX_INDEX) {
		return (ptr[string_index]);
	}
	return (NULL);
}

static void
phone_init(void *arg __unused)
{
	struct sysctl_oid *parent;
	char parent_name[3];

	usb_make_str_desc(&phone_mixer, sizeof(phone_mixer),
	    PHONE_DEFAULT_MIXER);
	usb_make_str_desc(&phone_record, sizeof(phone_record),
	    PHONE_DEFAULT_RECORD);
	usb_make_str_desc(&phone_playback, sizeof(phone_playback),
	    PHONE_DEFAULT_PLAYBACK);
	usb_make_str_desc(&phone_hid, sizeof(phone_hid),
	    PHONE_DEFAULT_HID);
	usb_make_str_desc(&phone_manufacturer, sizeof(phone_manufacturer),
	    PHONE_DEFAULT_MANUFACTURER);
	usb_make_str_desc(&phone_product, sizeof(phone_product),
	    PHONE_DEFAULT_PRODUCT);
	usb_make_str_desc(&phone_serial_number, sizeof(phone_serial_number),
	    PHONE_DEFAULT_SERIAL_NUMBER);

	snprintf(parent_name, sizeof(parent_name), "%d", USB_TEMP_PHONE);
	sysctl_ctx_init(&phone_ctx_list);

	parent = SYSCTL_ADD_NODE(&phone_ctx_list,
	    SYSCTL_STATIC_CHILDREN(_hw_usb_templates), OID_AUTO,
	    parent_name, CTLFLAG_RW,
	    0, "USB Phone device side template");
	SYSCTL_ADD_U16(&phone_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "vendor_id", CTLFLAG_RWTUN,
	    &usb_template_cdce.idVendor, 1, "Vendor identifier");
	SYSCTL_ADD_U16(&phone_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "product_id", CTLFLAG_RWTUN,
	    &usb_template_cdce.idProduct, 1, "Product identifier");
#if 0
	SYSCTL_ADD_PROC(&phone_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "mixer", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &phone_mixer, sizeof(phone_mixer), usb_temp_sysctl,
	    "A", "Mixer interface string");
	SYSCTL_ADD_PROC(&phone_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "record", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &phone_record, sizeof(phone_record), usb_temp_sysctl,
	    "A", "Record interface string");
	SYSCTL_ADD_PROC(&phone_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "playback", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &phone_playback, sizeof(phone_playback), usb_temp_sysctl,
	    "A", "Playback interface string");
	SYSCTL_ADD_PROC(&phone_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "hid", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &phone_hid, sizeof(phone_hid), usb_temp_sysctl,
	    "A", "HID interface string");
#endif
	SYSCTL_ADD_PROC(&phone_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "manufacturer", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &phone_manufacturer, sizeof(phone_manufacturer), usb_temp_sysctl,
	    "A", "Manufacturer string");
	SYSCTL_ADD_PROC(&phone_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "product", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &phone_product, sizeof(phone_product), usb_temp_sysctl,
	    "A", "Product string");
	SYSCTL_ADD_PROC(&phone_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "serial_number", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &phone_serial_number, sizeof(phone_serial_number), usb_temp_sysctl,
	    "A", "Serial number string");
}

static void
phone_uninit(void *arg __unused)
{

	sysctl_ctx_free(&phone_ctx_list);
}

SYSINIT(phone_init, SI_SUB_LOCK, SI_ORDER_FIRST, phone_init, NULL);
SYSUNINIT(phone_uninit, SI_SUB_LOCK, SI_ORDER_FIRST, phone_uninit, NULL);
