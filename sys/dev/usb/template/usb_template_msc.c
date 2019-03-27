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
 * This file contains the USB templates for an USB Mass Storage Device.
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

enum {
	MSC_LANG_INDEX,
	MSC_INTERFACE_INDEX,
	MSC_CONFIGURATION_INDEX,
	MSC_MANUFACTURER_INDEX,
	MSC_PRODUCT_INDEX,
	MSC_SERIAL_NUMBER_INDEX,
	MSC_MAX_INDEX,
};

#define	MSC_DEFAULT_VENDOR_ID		USB_TEMPLATE_VENDOR
#define	MSC_DEFAULT_PRODUCT_ID		0x27df
#define	MSC_DEFAULT_INTERFACE		"USB Mass Storage Interface"
#define	MSC_DEFAULT_CONFIGURATION	"Default Config"
#define	MSC_DEFAULT_MANUFACTURER	USB_TEMPLATE_MANUFACTURER
#define	MSC_DEFAULT_PRODUCT		"USB Memory Stick"
#define	MSC_DEFAULT_SERIAL_NUMBER	"March 2008"

static struct usb_string_descriptor	msc_interface;
static struct usb_string_descriptor	msc_configuration;
static struct usb_string_descriptor	msc_manufacturer;
static struct usb_string_descriptor	msc_product;
static struct usb_string_descriptor	msc_serial_number;

static struct sysctl_ctx_list		msc_ctx_list;

/* prototypes */

static usb_temp_get_string_desc_t msc_get_string_desc;

static const struct usb_temp_packet_size bulk_mps = {
	.mps[USB_SPEED_FULL] = 64,
	.mps[USB_SPEED_HIGH] = 512,
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

static const struct usb_temp_endpoint_desc bulk_out_ep = {
	.pPacketSize = &bulk_mps,
#ifdef USB_HIP_OUT_EP_0
	.bEndpointAddress = USB_HIP_OUT_EP_0,
#else
	.bEndpointAddress = UE_DIR_OUT,
#endif
	.bmAttributes = UE_BULK,
};

static const struct usb_temp_endpoint_desc *msc_data_endpoints[] = {
	&bulk_in_ep,
	&bulk_out_ep,
	NULL,
};

static const struct usb_temp_interface_desc msc_data_interface = {
	.ppEndpoints = msc_data_endpoints,
	.bInterfaceClass = UICLASS_MASS,
	.bInterfaceSubClass = UISUBCLASS_SCSI,
	.bInterfaceProtocol = UIPROTO_MASS_BBB,
	.iInterface = MSC_INTERFACE_INDEX,
};

static const struct usb_temp_interface_desc *msc_interfaces[] = {
	&msc_data_interface,
	NULL,
};

static const struct usb_temp_config_desc msc_config_desc = {
	.ppIfaceDesc = msc_interfaces,
	.bmAttributes = 0,
	.bMaxPower = 0,
	.iConfiguration = MSC_CONFIGURATION_INDEX,
};

static const struct usb_temp_config_desc *msc_configs[] = {
	&msc_config_desc,
	NULL,
};

struct usb_temp_device_desc usb_template_msc = {
	.getStringDesc = &msc_get_string_desc,
	.ppConfigDesc = msc_configs,
	.idVendor = MSC_DEFAULT_VENDOR_ID,
	.idProduct = MSC_DEFAULT_PRODUCT_ID,
	.bcdDevice = 0x0100,
	.bDeviceClass = UDCLASS_COMM,
	.bDeviceSubClass = 0,
	.bDeviceProtocol = 0,
	.iManufacturer = MSC_MANUFACTURER_INDEX,
	.iProduct = MSC_PRODUCT_INDEX,
	.iSerialNumber = MSC_SERIAL_NUMBER_INDEX,
};

/*------------------------------------------------------------------------*
 *	msc_get_string_desc
 *
 * Return values:
 * NULL: Failure. No such string.
 * Else: Success. Pointer to string descriptor is returned.
 *------------------------------------------------------------------------*/
static const void *
msc_get_string_desc(uint16_t lang_id, uint8_t string_index)
{
	static const void *ptr[MSC_MAX_INDEX] = {
		[MSC_LANG_INDEX] = &usb_string_lang_en,
		[MSC_INTERFACE_INDEX] = &msc_interface,
		[MSC_CONFIGURATION_INDEX] = &msc_configuration,
		[MSC_MANUFACTURER_INDEX] = &msc_manufacturer,
		[MSC_PRODUCT_INDEX] = &msc_product,
		[MSC_SERIAL_NUMBER_INDEX] = &msc_serial_number,
	};

	if (string_index == 0) {
		return (&usb_string_lang_en);
	}
	if (lang_id != 0x0409) {
		return (NULL);
	}
	if (string_index < MSC_MAX_INDEX) {
		return (ptr[string_index]);
	}
	return (NULL);
}

static void
msc_init(void *arg __unused)
{
	struct sysctl_oid *parent;
	char parent_name[3];

	usb_make_str_desc(&msc_interface, sizeof(msc_interface),
	    MSC_DEFAULT_INTERFACE);
	usb_make_str_desc(&msc_configuration, sizeof(msc_configuration),
	    MSC_DEFAULT_CONFIGURATION);
	usb_make_str_desc(&msc_manufacturer, sizeof(msc_manufacturer),
	    MSC_DEFAULT_MANUFACTURER);
	usb_make_str_desc(&msc_product, sizeof(msc_product),
	    MSC_DEFAULT_PRODUCT);
	usb_make_str_desc(&msc_serial_number, sizeof(msc_serial_number),
	    MSC_DEFAULT_SERIAL_NUMBER);

	snprintf(parent_name, sizeof(parent_name), "%d", USB_TEMP_MSC);
	sysctl_ctx_init(&msc_ctx_list);

	parent = SYSCTL_ADD_NODE(&msc_ctx_list,
	    SYSCTL_STATIC_CHILDREN(_hw_usb_templates), OID_AUTO,
	    parent_name, CTLFLAG_RW,
	    0, "USB Mass Storage device side template");
	SYSCTL_ADD_U16(&msc_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "vendor_id", CTLFLAG_RWTUN,
	    &usb_template_msc.idVendor, 1, "Vendor identifier");
	SYSCTL_ADD_U16(&msc_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "product_id", CTLFLAG_RWTUN,
	    &usb_template_msc.idProduct, 1, "Product identifier");
#if 0
	SYSCTL_ADD_PROC(&msc_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "interface", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &msc_interface, sizeof(msc_interface), usb_temp_sysctl,
	    "A", "Interface string");
	SYSCTL_ADD_PROC(&msc_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "configuration", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &msc_configuration, sizeof(msc_configuration), usb_temp_sysctl,
	    "A", "Configuration string");
#endif
	SYSCTL_ADD_PROC(&msc_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "manufacturer", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &msc_manufacturer, sizeof(msc_manufacturer), usb_temp_sysctl,
	    "A", "Manufacturer string");
	SYSCTL_ADD_PROC(&msc_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "product", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &msc_product, sizeof(msc_product), usb_temp_sysctl,
	    "A", "Product string");
	SYSCTL_ADD_PROC(&msc_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "serial_number", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &msc_serial_number, sizeof(msc_serial_number), usb_temp_sysctl,
	    "A", "Serial number string");
}

static void
msc_uninit(void *arg __unused)
{

	sysctl_ctx_free(&msc_ctx_list);
}

SYSINIT(msc_init, SI_SUB_LOCK, SI_ORDER_FIRST, msc_init, NULL);
SYSUNINIT(msc_uninit, SI_SUB_LOCK, SI_ORDER_FIRST, msc_uninit, NULL);
