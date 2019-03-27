/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015 Hans Petter Selasky
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
 * This file contains the USB template for an USB MIDI Device.
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
#endif					/* USB_GLOBAL_INCLUDE_FILE */

enum {
	MIDI_LANG_INDEX,
	MIDI_INTERFACE_INDEX,
	MIDI_MANUFACTURER_INDEX,
	MIDI_PRODUCT_INDEX,
	MIDI_SERIAL_NUMBER_INDEX,
	MIDI_MAX_INDEX,
};

#define	MIDI_DEFAULT_VENDOR_ID		USB_TEMPLATE_VENDOR
#define	MIDI_DEFAULT_PRODUCT_ID		0x27de
#define	MIDI_DEFAULT_INTERFACE		"MIDI interface"
#define	MIDI_DEFAULT_MANUFACTURER	USB_TEMPLATE_MANUFACTURER
#define	MIDI_DEFAULT_PRODUCT		"MIDI Test Device"
#define	MIDI_DEFAULT_SERIAL_NUMBER	"March 2008"

static struct usb_string_descriptor	midi_interface;
static struct usb_string_descriptor	midi_manufacturer;
static struct usb_string_descriptor	midi_product;
static struct usb_string_descriptor	midi_serial_number;

static struct sysctl_ctx_list		midi_ctx_list;

/* prototypes */

static const uint8_t midi_desc_raw_0[9] = {
	0x09, 0x24, 0x01, 0x00, 0x01, 0x09, 0x00, 0x01, 0x01
};

static const void *midi_descs_0[] = {
	&midi_desc_raw_0,
	NULL
};

static const struct usb_temp_interface_desc midi_iface_0 = {
	.ppEndpoints = NULL,		/* no endpoints */
	.ppRawDesc = midi_descs_0,
	.bInterfaceClass = UICLASS_AUDIO,
	.bInterfaceSubClass = UISUBCLASS_AUDIOCONTROL,
	.bInterfaceProtocol = 0,
	.iInterface = MIDI_INTERFACE_INDEX,
};

static const struct usb_temp_packet_size midi_mps = {
	.mps[USB_SPEED_LOW] = 8,
	.mps[USB_SPEED_FULL] = 64,
	.mps[USB_SPEED_HIGH] = 512,
};

static const uint8_t midi_desc_raw_7[5] = {
	0x05, 0x25, 0x01, 0x01, 0x01
};

static const void *midi_descs_2[] = {
	&midi_desc_raw_7,
	NULL
};

static const struct usb_temp_endpoint_desc midi_bulk_out_ep = {
	.ppRawDesc = midi_descs_2,
	.pPacketSize = &midi_mps,
	.bEndpointAddress = UE_DIR_OUT,
	.bmAttributes = UE_BULK,
};

static const uint8_t midi_desc_raw_6[5] = {
	0x05, 0x25, 0x01, 0x01, 0x03,
};

static const void *midi_descs_3[] = {
	&midi_desc_raw_6,
	NULL
};

static const struct usb_temp_endpoint_desc midi_bulk_in_ep = {
	.ppRawDesc = midi_descs_3,
	.pPacketSize = &midi_mps,
	.bEndpointAddress = UE_DIR_IN,
	.bmAttributes = UE_BULK,
};

static const struct usb_temp_endpoint_desc *midi_iface_1_ep[] = {
	&midi_bulk_out_ep,
	&midi_bulk_in_ep,
	NULL,
};

static const uint8_t midi_desc_raw_1[7] = {
	0x07, 0x24, 0x01, 0x00, 0x01, /* wTotalLength: */ 0x41, 0x00
};

static const uint8_t midi_desc_raw_2[6] = {
	0x06, 0x24, 0x02, 0x01, 0x01, 0x00
};

static const uint8_t midi_desc_raw_3[6] = {
	0x06, 0x24, 0x02, 0x02, 0x02, 0x00
};

static const uint8_t midi_desc_raw_4[9] = {
	0x09, 0x24, 0x03, 0x01, 0x03, 0x01, 0x02, 0x01, 0x00
};

static const uint8_t midi_desc_raw_5[9] = {
	0x09, 0x24, 0x03, 0x02, 0x04, 0x01, 0x01, 0x01, 0x00
};

static const void *midi_descs_1[] = {
	&midi_desc_raw_1,
	&midi_desc_raw_2,
	&midi_desc_raw_3,
	&midi_desc_raw_4,
	&midi_desc_raw_5,
	NULL
};

static const struct usb_temp_interface_desc midi_iface_1 = {
	.ppRawDesc = midi_descs_1,
	.ppEndpoints = midi_iface_1_ep,
	.bInterfaceClass = UICLASS_AUDIO,
	.bInterfaceSubClass = UISUBCLASS_MIDISTREAM,
	.bInterfaceProtocol = 0,
	.iInterface = MIDI_INTERFACE_INDEX,
};

static const struct usb_temp_interface_desc *midi_interfaces[] = {
	&midi_iface_0,
	&midi_iface_1,
	NULL,
};

static const struct usb_temp_config_desc midi_config_desc = {
	.ppIfaceDesc = midi_interfaces,
	.bmAttributes = 0,
	.bMaxPower = 0,
	.iConfiguration = MIDI_PRODUCT_INDEX,
};

static const struct usb_temp_config_desc *midi_configs[] = {
	&midi_config_desc,
	NULL,
};

static usb_temp_get_string_desc_t midi_get_string_desc;

struct usb_temp_device_desc usb_template_midi = {
	.getStringDesc = &midi_get_string_desc,
	.ppConfigDesc = midi_configs,
	.idVendor = MIDI_DEFAULT_VENDOR_ID,
	.idProduct = MIDI_DEFAULT_PRODUCT_ID,
	.bcdDevice = 0x0100,
	.bDeviceClass = 0,
	.bDeviceSubClass = 0,
	.bDeviceProtocol = 0,
	.iManufacturer = MIDI_MANUFACTURER_INDEX,
	.iProduct = MIDI_PRODUCT_INDEX,
	.iSerialNumber = MIDI_SERIAL_NUMBER_INDEX,
};

/*------------------------------------------------------------------------*
 *	midi_get_string_desc
 *
 * Return values:
 * NULL: Failure. No such string.
 * Else: Success. Pointer to string descriptor is returned.
 *------------------------------------------------------------------------*/
static const void *
midi_get_string_desc(uint16_t lang_id, uint8_t string_index)
{
	static const void *ptr[MIDI_MAX_INDEX] = {
		[MIDI_LANG_INDEX] = &usb_string_lang_en,
		[MIDI_INTERFACE_INDEX] = &midi_interface,
		[MIDI_MANUFACTURER_INDEX] = &midi_manufacturer,
		[MIDI_PRODUCT_INDEX] = &midi_product,
		[MIDI_SERIAL_NUMBER_INDEX] = &midi_serial_number,
	};

	if (string_index == 0) {
		return (&usb_string_lang_en);
	}
	if (lang_id != 0x0409) {
		return (NULL);
	}
	if (string_index < MIDI_MAX_INDEX) {
		return (ptr[string_index]);
	}
	return (NULL);
}

static void
midi_init(void *arg __unused)
{
	struct sysctl_oid *parent;
	char parent_name[3];

	usb_make_str_desc(&midi_interface, sizeof(midi_interface),
	    MIDI_DEFAULT_INTERFACE);
	usb_make_str_desc(&midi_manufacturer, sizeof(midi_manufacturer),
	    MIDI_DEFAULT_MANUFACTURER);
	usb_make_str_desc(&midi_product, sizeof(midi_product),
	    MIDI_DEFAULT_PRODUCT);
	usb_make_str_desc(&midi_serial_number, sizeof(midi_serial_number),
	    MIDI_DEFAULT_SERIAL_NUMBER);

	snprintf(parent_name, sizeof(parent_name), "%d", USB_TEMP_MIDI);
	sysctl_ctx_init(&midi_ctx_list);

	parent = SYSCTL_ADD_NODE(&midi_ctx_list,
	    SYSCTL_STATIC_CHILDREN(_hw_usb_templates), OID_AUTO,
	    parent_name, CTLFLAG_RW,
	    0, "USB MIDI device side template");
	SYSCTL_ADD_U16(&midi_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "vendor_id", CTLFLAG_RWTUN,
	    &usb_template_midi.idVendor, 1, "Vendor identifier");
	SYSCTL_ADD_U16(&midi_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "product_id", CTLFLAG_RWTUN,
	    &usb_template_midi.idProduct, 1, "Product identifier");
#if 0
	SYSCTL_ADD_PROC(&midi_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "interface", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &midi_interface, sizeof(midi_interface), usb_temp_sysctl,
	    "A", "Interface string");
#endif
	SYSCTL_ADD_PROC(&midi_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "manufacturer", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &midi_manufacturer, sizeof(midi_manufacturer), usb_temp_sysctl,
	    "A", "Manufacturer string");
	SYSCTL_ADD_PROC(&midi_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "product", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &midi_product, sizeof(midi_product), usb_temp_sysctl,
	    "A", "Product string");
	SYSCTL_ADD_PROC(&midi_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "serial_number", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &midi_serial_number, sizeof(midi_serial_number), usb_temp_sysctl,
	    "A", "Serial number string");
}

static void
midi_uninit(void *arg __unused)
{

	sysctl_ctx_free(&midi_ctx_list);
}

SYSINIT(midi_init, SI_SUB_LOCK, SI_ORDER_FIRST, midi_init, NULL);
SYSUNINIT(midi_uninit, SI_SUB_LOCK, SI_ORDER_FIRST, midi_uninit, NULL);
