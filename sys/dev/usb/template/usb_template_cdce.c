/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007 Hans Petter Selasky <hselasky@FreeBSD.org>
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
 * This file contains the USB templates for a CDC USB ethernet device.
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
	ETH_LANG_INDEX,
	ETH_MAC_INDEX,
	ETH_CONTROL_INDEX,
	ETH_DATA_INDEX,
	ETH_CONFIGURATION_INDEX,
	ETH_MANUFACTURER_INDEX,
	ETH_PRODUCT_INDEX,
	ETH_SERIAL_NUMBER_INDEX,
	ETH_MAX_INDEX,
};

#define	ETH_DEFAULT_VENDOR_ID		USB_TEMPLATE_VENDOR
#define	ETH_DEFAULT_PRODUCT_ID		0x27e1
#define	ETH_DEFAULT_MAC			"2A02030405060789AB"
#define	ETH_DEFAULT_CONTROL		"USB Ethernet Comm Interface"
#define	ETH_DEFAULT_DATA		"USB Ethernet Data Interface"
#define	ETH_DEFAULT_CONFIG		"Default Config"
#define	ETH_DEFAULT_MANUFACTURER	USB_TEMPLATE_MANUFACTURER
#define	ETH_DEFAULT_PRODUCT		"USB Ethernet Adapter"
#define	ETH_DEFAULT_SERIAL_NUMBER	"December 2007"

static struct usb_string_descriptor	eth_mac;
static struct usb_string_descriptor	eth_control;
static struct usb_string_descriptor	eth_data;
static struct usb_string_descriptor	eth_configuration;
static struct usb_string_descriptor	eth_manufacturer;
static struct usb_string_descriptor	eth_product;
static struct usb_string_descriptor	eth_serial_number;

static struct sysctl_ctx_list		eth_ctx_list;

/* prototypes */

static usb_temp_get_string_desc_t eth_get_string_desc;

static const struct usb_cdc_union_descriptor eth_union_desc = {
	.bLength = sizeof(eth_union_desc),
	.bDescriptorType = UDESC_CS_INTERFACE,
	.bDescriptorSubtype = UDESCSUB_CDC_UNION,
	.bMasterInterface = 0,		/* this is automatically updated */
	.bSlaveInterface[0] = 1,	/* this is automatically updated */
};

static const struct usb_cdc_header_descriptor eth_header_desc = {
	.bLength = sizeof(eth_header_desc),
	.bDescriptorType = UDESC_CS_INTERFACE,
	.bDescriptorSubtype = UDESCSUB_CDC_HEADER,
	.bcdCDC[0] = 0x10,
	.bcdCDC[1] = 0x01,
};

static const struct usb_cdc_ethernet_descriptor eth_enf_desc = {
	.bLength = sizeof(eth_enf_desc),
	.bDescriptorType = UDESC_CS_INTERFACE,
	.bDescriptorSubtype = UDESCSUB_CDC_ENF,
	.iMacAddress = ETH_MAC_INDEX,
	.bmEthernetStatistics = {0, 0, 0, 0},
	.wMaxSegmentSize = {0xEA, 0x05},/* 1514 bytes */
	.wNumberMCFilters = {0, 0},
	.bNumberPowerFilters = 0,
};

static const void *eth_control_if_desc[] = {
	&eth_union_desc,
	&eth_header_desc,
	&eth_enf_desc,
	NULL,
};

static const struct usb_temp_packet_size bulk_mps = {
	.mps[USB_SPEED_FULL] = 64,
	.mps[USB_SPEED_HIGH] = 512,
};

static const struct usb_temp_packet_size intr_mps = {
	.mps[USB_SPEED_FULL] = 8,
	.mps[USB_SPEED_HIGH] = 8,
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

static const struct usb_temp_endpoint_desc intr_in_ep = {
	.pPacketSize = &intr_mps,
	.bEndpointAddress = UE_DIR_IN,
	.bmAttributes = UE_INTERRUPT,
};

static const struct usb_temp_endpoint_desc *eth_intr_endpoints[] = {
	&intr_in_ep,
	NULL,
};

static const struct usb_temp_interface_desc eth_control_interface = {
	.ppEndpoints = eth_intr_endpoints,
	.ppRawDesc = eth_control_if_desc,
	.bInterfaceClass = UICLASS_CDC,
	.bInterfaceSubClass = UISUBCLASS_ETHERNET_NETWORKING_CONTROL_MODEL,
	.bInterfaceProtocol = 0,
	.iInterface = ETH_CONTROL_INDEX,
};

static const struct usb_temp_endpoint_desc *eth_data_endpoints[] = {
	&bulk_in_ep,
	&bulk_out_ep,
	NULL,
};

static const struct usb_temp_interface_desc eth_data_null_interface = {
	.ppEndpoints = NULL,		/* no endpoints */
	.bInterfaceClass = UICLASS_CDC_DATA,
	.bInterfaceSubClass = 0,
	.bInterfaceProtocol = 0,
	.iInterface = ETH_DATA_INDEX,
};

static const struct usb_temp_interface_desc eth_data_interface = {
	.ppEndpoints = eth_data_endpoints,
	.bInterfaceClass = UICLASS_CDC_DATA,
	.bInterfaceSubClass = UISUBCLASS_DATA,
	.bInterfaceProtocol = 0,
	.iInterface = ETH_DATA_INDEX,
	.isAltInterface = 1,		/* this is an alternate setting */
};

static const struct usb_temp_interface_desc *eth_interfaces[] = {
	&eth_control_interface,
	&eth_data_null_interface,
	&eth_data_interface,
	NULL,
};

static const struct usb_temp_config_desc eth_config_desc = {
	.ppIfaceDesc = eth_interfaces,
	.bmAttributes = 0,
	.bMaxPower = 0,
	.iConfiguration = ETH_CONFIGURATION_INDEX,
};

static const struct usb_temp_config_desc *eth_configs[] = {
	&eth_config_desc,
	NULL,
};

struct usb_temp_device_desc usb_template_cdce = {
	.getStringDesc = &eth_get_string_desc,
	.ppConfigDesc = eth_configs,
	.idVendor = ETH_DEFAULT_VENDOR_ID,
	.idProduct = ETH_DEFAULT_PRODUCT_ID,
	.bcdDevice = 0x0100,
	.bDeviceClass = UDCLASS_COMM,
	.bDeviceSubClass = 0,
	.bDeviceProtocol = 0,
	.iManufacturer = ETH_MANUFACTURER_INDEX,
	.iProduct = ETH_PRODUCT_INDEX,
	.iSerialNumber = ETH_SERIAL_NUMBER_INDEX,
};

/*------------------------------------------------------------------------*
 *	eth_get_string_desc
 *
 * Return values:
 * NULL: Failure. No such string.
 * Else: Success. Pointer to string descriptor is returned.
 *------------------------------------------------------------------------*/
static const void *
eth_get_string_desc(uint16_t lang_id, uint8_t string_index)
{
	static const void *ptr[ETH_MAX_INDEX] = {
		[ETH_LANG_INDEX] = &usb_string_lang_en,
		[ETH_MAC_INDEX] = &eth_mac,
		[ETH_CONTROL_INDEX] = &eth_control,
		[ETH_DATA_INDEX] = &eth_data,
		[ETH_CONFIGURATION_INDEX] = &eth_configuration,
		[ETH_MANUFACTURER_INDEX] = &eth_manufacturer,
		[ETH_PRODUCT_INDEX] = &eth_product,
		[ETH_SERIAL_NUMBER_INDEX] = &eth_serial_number,
	};

	if (string_index == 0) {
		return (&usb_string_lang_en);
	}
	if (lang_id != 0x0409) {
		return (NULL);
	}
	if (string_index < ETH_MAX_INDEX) {
		return (ptr[string_index]);
	}
	return (NULL);
}

static void
eth_init(void *arg __unused)
{
	struct sysctl_oid *parent;
	char parent_name[3];

	usb_make_str_desc(&eth_mac, sizeof(eth_mac),
	    ETH_DEFAULT_MAC);
	usb_make_str_desc(&eth_control, sizeof(eth_control),
	    ETH_DEFAULT_CONTROL);
	usb_make_str_desc(&eth_data, sizeof(eth_data),
	    ETH_DEFAULT_DATA);
	usb_make_str_desc(&eth_configuration, sizeof(eth_configuration),
	    ETH_DEFAULT_CONFIG);
	usb_make_str_desc(&eth_manufacturer, sizeof(eth_manufacturer),
	    ETH_DEFAULT_MANUFACTURER);
	usb_make_str_desc(&eth_product, sizeof(eth_product),
	    ETH_DEFAULT_PRODUCT);
	usb_make_str_desc(&eth_serial_number, sizeof(eth_serial_number),
	    ETH_DEFAULT_SERIAL_NUMBER);

	snprintf(parent_name, sizeof(parent_name), "%d", USB_TEMP_CDCE);
	sysctl_ctx_init(&eth_ctx_list);

	parent = SYSCTL_ADD_NODE(&eth_ctx_list,
	    SYSCTL_STATIC_CHILDREN(_hw_usb_templates), OID_AUTO,
	    parent_name, CTLFLAG_RW,
	    0, "USB CDC Ethernet device side template");
	SYSCTL_ADD_U16(&eth_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "vendor_id", CTLFLAG_RWTUN,
	    &usb_template_cdce.idVendor, 1, "Vendor identifier");
	SYSCTL_ADD_U16(&eth_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "product_id", CTLFLAG_RWTUN,
	    &usb_template_cdce.idProduct, 1, "Product identifier");
	SYSCTL_ADD_PROC(&eth_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "mac", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &eth_mac, sizeof(eth_mac), usb_temp_sysctl,
	    "A", "MAC address string");
#if 0
	SYSCTL_ADD_PROC(&eth_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "control", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &eth_control, sizeof(eth_control), usb_temp_sysctl,
	    "A", "Control interface string");
	SYSCTL_ADD_PROC(&eth_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "data", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &eth_data, sizeof(eth_data), usb_temp_sysctl,
	    "A", "Data interface string");
	SYSCTL_ADD_PROC(&eth_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "configuration", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &eth_configuration, sizeof(eth_configuration), usb_temp_sysctl,
	    "A", "Configuration string");
#endif
	SYSCTL_ADD_PROC(&eth_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "manufacturer", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &eth_manufacturer, sizeof(eth_manufacturer), usb_temp_sysctl,
	    "A", "Manufacturer string");
	SYSCTL_ADD_PROC(&eth_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "product", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &eth_product, sizeof(eth_product), usb_temp_sysctl,
	    "A", "Product string");
	SYSCTL_ADD_PROC(&eth_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "serial_number", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &eth_serial_number, sizeof(eth_serial_number), usb_temp_sysctl,
	    "A", "Serial number string");
}

static void
eth_uninit(void *arg __unused)
{

	sysctl_ctx_free(&eth_ctx_list);
}

SYSINIT(eth_init, SI_SUB_LOCK, SI_ORDER_FIRST, eth_init, NULL);
SYSUNINIT(eth_uninit, SI_SUB_LOCK, SI_ORDER_FIRST, eth_uninit, NULL);
