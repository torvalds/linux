/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Hans Petter Selasky <hselasky@FreeBSD.org>
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
 * This file contains the USB templates for an USB Media Transfer
 * Protocol device.
 *
 * NOTE: It is common practice that MTP devices use some dummy
 * descriptor cludges to be automatically detected by the host
 * operating system. These descriptors are documented in the LibMTP
 * library at sourceforge.net. The alternative is to supply the host
 * operating system the VID and PID of your device.
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
#include <dev/usb/usb_ioctl.h>
#include <dev/usb/usb_util.h>

#include <dev/usb/template/usb_template.h>
#endif			/* USB_GLOBAL_INCLUDE_FILE */

#define	MTP_BREQUEST 0x08

enum {
	MTP_LANG_INDEX,
	MTP_INTERFACE_INDEX,
	MTP_CONFIGURATION_INDEX,
	MTP_MANUFACTURER_INDEX,
	MTP_PRODUCT_INDEX,
	MTP_SERIAL_NUMBER_INDEX,
	MTP_MAX_INDEX,
};

#define	MTP_DEFAULT_VENDOR_ID		USB_TEMPLATE_VENDOR
#define	MTP_DEFAULT_PRODUCT_ID		0x27e2
#define	MTP_DEFAULT_INTERFACE		"USB MTP Interface"
#define	MTP_DEFAULT_CONFIGURATION	"Default Config"
#define	MTP_DEFAULT_MANUFACTURER	USB_TEMPLATE_MANUFACTURER
#define	MTP_DEFAULT_PRODUCT		"USB MTP"
#define	MTP_DEFAULT_SERIAL_NUMBER	"June 2008"

static struct usb_string_descriptor	mtp_interface;
static struct usb_string_descriptor	mtp_configuration;
static struct usb_string_descriptor	mtp_manufacturer;
static struct usb_string_descriptor	mtp_product;
static struct usb_string_descriptor	mtp_serial_number;

static struct sysctl_ctx_list		mtp_ctx_list;

/* prototypes */

static usb_temp_get_string_desc_t mtp_get_string_desc;
static usb_temp_get_vendor_desc_t mtp_get_vendor_desc;

static const struct usb_temp_packet_size bulk_mps = {
	.mps[USB_SPEED_FULL] = 64,
	.mps[USB_SPEED_HIGH] = 512,
};

static const struct usb_temp_packet_size intr_mps = {
	.mps[USB_SPEED_FULL] = 64,
	.mps[USB_SPEED_HIGH] = 64,
};

static const struct usb_temp_endpoint_desc bulk_out_ep = {
	.pPacketSize = &bulk_mps,
#ifdef USB_HIP_OUT_EP_0
	.bEndpointAddress = USB_HIP_OUT_EP_0,
#else
	.bEndpointAddress = UE_DIR_OUT,
#endif
	.bmAttributes = UE_BULK,
};

static const struct usb_temp_endpoint_desc intr_in_ep = {
	.pPacketSize = &intr_mps,
	.bEndpointAddress = UE_DIR_IN,
	.bmAttributes = UE_INTERRUPT,
};

static const struct usb_temp_endpoint_desc bulk_in_ep = {
	.pPacketSize = &bulk_mps,
#ifdef USB_HIP_IN_EP_0
	.bEndpointAddress = USB_HIP_IN_EP_0,
#else
	.bEndpointAddress = UE_DIR_IN,
#endif
	.bmAttributes = UE_BULK,
};

static const struct usb_temp_endpoint_desc *mtp_data_endpoints[] = {
	&bulk_in_ep,
	&bulk_out_ep,
	&intr_in_ep,
	NULL,
};

static const struct usb_temp_interface_desc mtp_data_interface = {
	.ppEndpoints = mtp_data_endpoints,
	.bInterfaceClass = UICLASS_IMAGE,
	.bInterfaceSubClass = UISUBCLASS_SIC,	/* Still Image Class */
	.bInterfaceProtocol = 1,	/* PIMA 15740 */
	.iInterface = MTP_INTERFACE_INDEX,
};

static const struct usb_temp_interface_desc *mtp_interfaces[] = {
	&mtp_data_interface,
	NULL,
};

static const struct usb_temp_config_desc mtp_config_desc = {
	.ppIfaceDesc = mtp_interfaces,
	.bmAttributes = 0,
	.bMaxPower = 0,
	.iConfiguration = MTP_CONFIGURATION_INDEX,
};

static const struct usb_temp_config_desc *mtp_configs[] = {
	&mtp_config_desc,
	NULL,
};

struct usb_temp_device_desc usb_template_mtp = {
	.getStringDesc = &mtp_get_string_desc,
	.getVendorDesc = &mtp_get_vendor_desc,
	.ppConfigDesc = mtp_configs,
	.idVendor = MTP_DEFAULT_VENDOR_ID,
	.idProduct = MTP_DEFAULT_PRODUCT_ID,
	.bcdDevice = 0x0100,
	.bDeviceClass = 0,
	.bDeviceSubClass = 0,
	.bDeviceProtocol = 0,
	.iManufacturer = MTP_MANUFACTURER_INDEX,
	.iProduct = MTP_PRODUCT_INDEX,
	.iSerialNumber = MTP_SERIAL_NUMBER_INDEX,
};

/*------------------------------------------------------------------------*
 *	mtp_get_vendor_desc
 *
 * Return values:
 * NULL: Failure. No such vendor descriptor.
 * Else: Success. Pointer to vendor descriptor is returned.
 *------------------------------------------------------------------------*/
static const void *
mtp_get_vendor_desc(const struct usb_device_request *req, uint16_t *plen)
{
	static const uint8_t dummy_desc[0x28] = {
		0x28, 0, 0, 0, 0, 1, 4, 0,
		1, 0, 0, 0, 0, 0, 0, 0,
		0, 1, 0x4D, 0x54, 0x50, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
	};

	if ((req->bmRequestType == UT_READ_VENDOR_DEVICE) &&
	    (req->bRequest == MTP_BREQUEST) && (req->wValue[0] == 0) &&
	    (req->wValue[1] == 0) && (req->wIndex[1] == 0) &&
	    ((req->wIndex[0] == 4) || (req->wIndex[0] == 5))) {
		/*
		 * By returning this descriptor LibMTP will
		 * automatically pickup our device.
		 */
		return (dummy_desc);
	}
	return (NULL);
}

/*------------------------------------------------------------------------*
 *	mtp_get_string_desc
 *
 * Return values:
 * NULL: Failure. No such string.
 * Else: Success. Pointer to string descriptor is returned.
 *------------------------------------------------------------------------*/
static const void *
mtp_get_string_desc(uint16_t lang_id, uint8_t string_index)
{
	static const void *ptr[MTP_MAX_INDEX] = {
		[MTP_LANG_INDEX] = &usb_string_lang_en,
		[MTP_INTERFACE_INDEX] = &mtp_interface,
		[MTP_CONFIGURATION_INDEX] = &mtp_configuration,
		[MTP_MANUFACTURER_INDEX] = &mtp_manufacturer,
		[MTP_PRODUCT_INDEX] = &mtp_product,
		[MTP_SERIAL_NUMBER_INDEX] = &mtp_serial_number,
	};

	static const uint8_t dummy_desc[0x12] = {
		0x12, 0x03, 0x4D, 0x00, 0x53, 0x00, 0x46, 0x00,
		0x54, 0x00, 0x31, 0x00, 0x30, 0x00, 0x30, 0x00,
		MTP_BREQUEST, 0x00,
	};

	if (string_index == 0xEE) {
		/*
		 * By returning this string LibMTP will automatically
		 * pickup our device.
		 */
		return (dummy_desc);
	}
	if (string_index == 0) {
		return (&usb_string_lang_en);
	}
	if (lang_id != 0x0409) {
		return (NULL);
	}
	if (string_index < MTP_MAX_INDEX) {
		return (ptr[string_index]);
	}
	return (NULL);
}

static void
mtp_init(void *arg __unused)
{
	struct sysctl_oid *parent;
	char parent_name[3];

	usb_make_str_desc(&mtp_interface, sizeof(mtp_interface),
	    MTP_DEFAULT_INTERFACE);
	usb_make_str_desc(&mtp_configuration, sizeof(mtp_configuration),
	    MTP_DEFAULT_CONFIGURATION);
	usb_make_str_desc(&mtp_manufacturer, sizeof(mtp_manufacturer),
	    MTP_DEFAULT_MANUFACTURER);
	usb_make_str_desc(&mtp_product, sizeof(mtp_product),
	    MTP_DEFAULT_PRODUCT);
	usb_make_str_desc(&mtp_serial_number, sizeof(mtp_serial_number),
	    MTP_DEFAULT_SERIAL_NUMBER);

	snprintf(parent_name, sizeof(parent_name), "%d", USB_TEMP_MTP);
	sysctl_ctx_init(&mtp_ctx_list);

	parent = SYSCTL_ADD_NODE(&mtp_ctx_list,
	    SYSCTL_STATIC_CHILDREN(_hw_usb_templates), OID_AUTO,
	    parent_name, CTLFLAG_RW,
	    0, "USB Media Transfer Protocol device side template");
	SYSCTL_ADD_U16(&mtp_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "vendor_id", CTLFLAG_RWTUN,
	    &usb_template_mtp.idVendor, 1, "Vendor identifier");
	SYSCTL_ADD_U16(&mtp_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "product_id", CTLFLAG_RWTUN,
	    &usb_template_mtp.idProduct, 1, "Product identifier");
#if 0
	SYSCTL_ADD_PROC(&mtp_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "interface", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &mtp_interface, sizeof(mtp_interface), usb_temp_sysctl,
	    "A", "Interface string");
	SYSCTL_ADD_PROC(&mtp_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "configuration", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &mtp_configuration, sizeof(mtp_configuration), usb_temp_sysctl,
	    "A", "Configuration string");
#endif
	SYSCTL_ADD_PROC(&mtp_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "manufacturer", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &mtp_manufacturer, sizeof(mtp_manufacturer), usb_temp_sysctl,
	    "A", "Manufacturer string");
	SYSCTL_ADD_PROC(&mtp_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "product", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &mtp_product, sizeof(mtp_product), usb_temp_sysctl,
	    "A", "Product string");
	SYSCTL_ADD_PROC(&mtp_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "serial_number", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &mtp_serial_number, sizeof(mtp_serial_number), usb_temp_sysctl,
	    "A", "Serial number string");
}

static void
mtp_uninit(void *arg __unused)
{

	sysctl_ctx_free(&mtp_ctx_list);
}

SYSINIT(mtp_init, SI_SUB_LOCK, SI_ORDER_FIRST, mtp_init, NULL);
SYSUNINIT(mtp_uninit, SI_SUB_LOCK, SI_ORDER_FIRST, mtp_uninit, NULL);
