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
 * This file contains the USB template for an USB Audio Device.
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
	AUDIO_LANG_INDEX,
	AUDIO_MIXER_INDEX,
	AUDIO_RECORD_INDEX,
	AUDIO_PLAYBACK_INDEX,
	AUDIO_MANUFACTURER_INDEX,
	AUDIO_PRODUCT_INDEX,
	AUDIO_SERIAL_NUMBER_INDEX,
	AUDIO_MAX_INDEX,
};

#define	AUDIO_DEFAULT_VENDOR_ID		USB_TEMPLATE_VENDOR
#define	AUDIO_DEFAULT_PRODUCT_ID	0x27e0
#define	AUDIO_DEFAULT_MIXER		"Mixer interface"
#define	AUDIO_DEFAULT_RECORD		"Record interface"
#define	AUDIO_DEFAULT_PLAYBACK		"Playback interface"
#define	AUDIO_DEFAULT_MANUFACTURER	USB_TEMPLATE_MANUFACTURER
#define	AUDIO_DEFAULT_PRODUCT		"Audio Test Device"
#define	AUDIO_DEFAULT_SERIAL_NUMBER	"March 2008"

static struct usb_string_descriptor	audio_mixer;
static struct usb_string_descriptor	audio_record;
static struct usb_string_descriptor	audio_playback;
static struct usb_string_descriptor	audio_manufacturer;
static struct usb_string_descriptor	audio_product;
static struct usb_string_descriptor	audio_serial_number;

static struct sysctl_ctx_list		audio_ctx_list;

/* prototypes */

/*
 * Audio Mixer description structures
 *
 * Some of the audio descriptors were dumped
 * from a Creative Labs USB audio device.
 */

static const uint8_t audio_raw_desc_0[] = {
	0x0a, 0x24, 0x01, 0x00, 0x01, 0xa9, 0x00, 0x02,
	0x01, 0x02
};

static const uint8_t audio_raw_desc_1[] = {
	0x0c, 0x24, 0x02, 0x01, 0x01, 0x01, 0x00, 0x02,
	0x03, 0x00, 0x00, 0x00
};

static const uint8_t audio_raw_desc_2[] = {
	0x0c, 0x24, 0x02, 0x02, 0x01, 0x02, 0x00, 0x02,
	0x03, 0x00, 0x00, 0x00
};

static const uint8_t audio_raw_desc_3[] = {
	0x0c, 0x24, 0x02, 0x03, 0x03, 0x06, 0x00, 0x02,
	0x03, 0x00, 0x00, 0x00
};

static const uint8_t audio_raw_desc_4[] = {
	0x0c, 0x24, 0x02, 0x04, 0x05, 0x06, 0x00, 0x02,
	0x03, 0x00, 0x00, 0x00
};

static const uint8_t audio_raw_desc_5[] = {
	0x09, 0x24, 0x03, 0x05, 0x05, 0x06, 0x00, 0x01,
	0x00
};

static const uint8_t audio_raw_desc_6[] = {
	0x09, 0x24, 0x03, 0x06, 0x01, 0x03, 0x00, 0x09,
	0x00
};

static const uint8_t audio_raw_desc_7[] = {
	0x09, 0x24, 0x03, 0x07, 0x01, 0x01, 0x00, 0x08,
	0x00
};

static const uint8_t audio_raw_desc_8[] = {
	0x09, 0x24, 0x05, 0x08, 0x03, 0x0a, 0x0b, 0x0c,
	0x00
};

static const uint8_t audio_raw_desc_9[] = {
	0x0a, 0x24, 0x06, 0x09, 0x0f, 0x01, 0x01, 0x02,
	0x02, 0x00
};

static const uint8_t audio_raw_desc_10[] = {
	0x0a, 0x24, 0x06, 0x0a, 0x02, 0x01, 0x43, 0x00,
	0x00, 0x00
};

static const uint8_t audio_raw_desc_11[] = {
	0x0a, 0x24, 0x06, 0x0b, 0x03, 0x01, 0x01, 0x02,
	0x02, 0x00
};

static const uint8_t audio_raw_desc_12[] = {
	0x0a, 0x24, 0x06, 0x0c, 0x04, 0x01, 0x01, 0x00,
	0x00, 0x00
};

static const uint8_t audio_raw_desc_13[] = {
	0x0a, 0x24, 0x06, 0x0d, 0x02, 0x01, 0x03, 0x00,
	0x00, 0x00
};

static const uint8_t audio_raw_desc_14[] = {
	0x0a, 0x24, 0x06, 0x0e, 0x03, 0x01, 0x01, 0x02,
	0x02, 0x00
};

static const uint8_t audio_raw_desc_15[] = {
	0x0f, 0x24, 0x04, 0x0f, 0x03, 0x01, 0x0d, 0x0e,
	0x02, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const void *audio_raw_iface_0_desc[] = {
	audio_raw_desc_0,
	audio_raw_desc_1,
	audio_raw_desc_2,
	audio_raw_desc_3,
	audio_raw_desc_4,
	audio_raw_desc_5,
	audio_raw_desc_6,
	audio_raw_desc_7,
	audio_raw_desc_8,
	audio_raw_desc_9,
	audio_raw_desc_10,
	audio_raw_desc_11,
	audio_raw_desc_12,
	audio_raw_desc_13,
	audio_raw_desc_14,
	audio_raw_desc_15,
	NULL,
};

static const struct usb_temp_interface_desc audio_iface_0 = {
	.ppEndpoints = NULL,		/* no endpoints */
	.ppRawDesc = audio_raw_iface_0_desc,
	.bInterfaceClass = UICLASS_AUDIO,
	.bInterfaceSubClass = UISUBCLASS_AUDIOCONTROL,
	.bInterfaceProtocol = 0,
	.iInterface = AUDIO_MIXER_INDEX,
};

static const uint8_t audio_raw_desc_20[] = {
	0x07, 0x24, 0x01, 0x01, 0x03, 0x01, 0x00

};

static const uint8_t audio_raw_desc_21[] = {
	0x0b, 0x24, 0x02, 0x01, 0x02, 0x02, 0x10, 0x01,
	/* 48kHz */
	0x80, 0xbb, 0x00
};

static const uint8_t audio_raw_desc_22[] = {
	0x07, 0x25, 0x01, 0x00, 0x01, 0x04, 0x00
};

static const void *audio_raw_iface_1_desc[] = {
	audio_raw_desc_20,
	audio_raw_desc_21,
	NULL,
};

static const void *audio_raw_ep_1_desc[] = {
	audio_raw_desc_22,
	NULL,
};

static const struct usb_temp_packet_size audio_isoc_mps = {
  .mps[USB_SPEED_FULL] = 0xC8,
  .mps[USB_SPEED_HIGH] = 0xC8,
};

static const struct usb_temp_interval audio_isoc_interval = {
	.bInterval[USB_SPEED_FULL] = 1,	/* 1:1 */
	.bInterval[USB_SPEED_HIGH] = 4,	/* 1:8 */
};

static const struct usb_temp_endpoint_desc audio_isoc_out_ep = {
	.ppRawDesc = audio_raw_ep_1_desc,
	.pPacketSize = &audio_isoc_mps,
	.pIntervals = &audio_isoc_interval,
	.bEndpointAddress = UE_DIR_OUT,
	.bmAttributes = UE_ISOCHRONOUS | UE_ISO_ADAPT,
};

static const struct usb_temp_endpoint_desc *audio_iface_1_ep[] = {
	&audio_isoc_out_ep,
	NULL,
};

static const struct usb_temp_interface_desc audio_iface_1_alt_0 = {
	.ppEndpoints = NULL,		/* no endpoints */
	.ppRawDesc = NULL,		/* no raw descriptors */
	.bInterfaceClass = UICLASS_AUDIO,
	.bInterfaceSubClass = UISUBCLASS_AUDIOSTREAM,
	.bInterfaceProtocol = 0,
	.iInterface = AUDIO_PLAYBACK_INDEX,
};

static const struct usb_temp_interface_desc audio_iface_1_alt_1 = {
	.ppEndpoints = audio_iface_1_ep,
	.ppRawDesc = audio_raw_iface_1_desc,
	.bInterfaceClass = UICLASS_AUDIO,
	.bInterfaceSubClass = UISUBCLASS_AUDIOSTREAM,
	.bInterfaceProtocol = 0,
	.iInterface = AUDIO_PLAYBACK_INDEX,
	.isAltInterface = 1,		/* this is an alternate setting */
};

static const uint8_t audio_raw_desc_30[] = {
	0x07, 0x24, 0x01, 0x07, 0x01, 0x01, 0x00

};

static const uint8_t audio_raw_desc_31[] = {
	0x0b, 0x24, 0x02, 0x01, 0x02, 0x02, 0x10, 0x01,
	/* 48kHz */
	0x80, 0xbb, 0x00
};

static const uint8_t audio_raw_desc_32[] = {
	0x07, 0x25, 0x01, 0x01, 0x00, 0x00, 0x00
};

static const void *audio_raw_iface_2_desc[] = {
	audio_raw_desc_30,
	audio_raw_desc_31,
	NULL,
};

static const void *audio_raw_ep_2_desc[] = {
	audio_raw_desc_32,
	NULL,
};

static const struct usb_temp_endpoint_desc audio_isoc_in_ep = {
	.ppRawDesc = audio_raw_ep_2_desc,
	.pPacketSize = &audio_isoc_mps,
	.pIntervals = &audio_isoc_interval,
	.bEndpointAddress = UE_DIR_IN,
	.bmAttributes = UE_ISOCHRONOUS | UE_ISO_ADAPT,
};

static const struct usb_temp_endpoint_desc *audio_iface_2_ep[] = {
	&audio_isoc_in_ep,
	NULL,
};

static const struct usb_temp_interface_desc audio_iface_2_alt_0 = {
	.ppEndpoints = NULL,		/* no endpoints */
	.ppRawDesc = NULL,		/* no raw descriptors */
	.bInterfaceClass = UICLASS_AUDIO,
	.bInterfaceSubClass = UISUBCLASS_AUDIOSTREAM,
	.bInterfaceProtocol = 0,
	.iInterface = AUDIO_RECORD_INDEX,
};

static const struct usb_temp_interface_desc audio_iface_2_alt_1 = {
	.ppEndpoints = audio_iface_2_ep,
	.ppRawDesc = audio_raw_iface_2_desc,
	.bInterfaceClass = UICLASS_AUDIO,
	.bInterfaceSubClass = UISUBCLASS_AUDIOSTREAM,
	.bInterfaceProtocol = 0,
	.iInterface = AUDIO_RECORD_INDEX,
	.isAltInterface = 1,		/* this is an alternate setting */
};

static const struct usb_temp_interface_desc *audio_interfaces[] = {
	&audio_iface_0,
	&audio_iface_1_alt_0,
	&audio_iface_1_alt_1,
	&audio_iface_2_alt_0,
	&audio_iface_2_alt_1,
	NULL,
};

static const struct usb_temp_config_desc audio_config_desc = {
	.ppIfaceDesc = audio_interfaces,
	.bmAttributes = 0,
	.bMaxPower = 0,
	.iConfiguration = AUDIO_PRODUCT_INDEX,
};

static const struct usb_temp_config_desc *audio_configs[] = {
	&audio_config_desc,
	NULL,
};

static usb_temp_get_string_desc_t audio_get_string_desc;

struct usb_temp_device_desc usb_template_audio = {
	.getStringDesc = &audio_get_string_desc,
	.ppConfigDesc = audio_configs,
	.idVendor = AUDIO_DEFAULT_VENDOR_ID,
	.idProduct = AUDIO_DEFAULT_PRODUCT_ID,
	.bcdDevice = 0x0100,
	.bDeviceClass = UDCLASS_COMM,
	.bDeviceSubClass = 0,
	.bDeviceProtocol = 0,
	.iManufacturer = AUDIO_MANUFACTURER_INDEX,
	.iProduct = AUDIO_PRODUCT_INDEX,
	.iSerialNumber = AUDIO_SERIAL_NUMBER_INDEX,
};

/*------------------------------------------------------------------------*
 *	audio_get_string_desc
 *
 * Return values:
 * NULL: Failure. No such string.
 * Else: Success. Pointer to string descriptor is returned.
 *------------------------------------------------------------------------*/
static const void *
audio_get_string_desc(uint16_t lang_id, uint8_t string_index)
{
	static const void *ptr[AUDIO_MAX_INDEX] = {
		[AUDIO_LANG_INDEX] = &usb_string_lang_en,
		[AUDIO_MIXER_INDEX] = &audio_mixer,
		[AUDIO_RECORD_INDEX] = &audio_record,
		[AUDIO_PLAYBACK_INDEX] = &audio_playback,
		[AUDIO_MANUFACTURER_INDEX] = &audio_manufacturer,
		[AUDIO_PRODUCT_INDEX] = &audio_product,
		[AUDIO_SERIAL_NUMBER_INDEX] = &audio_serial_number,
	};

	if (string_index == 0) {
		return (&usb_string_lang_en);
	}
	if (lang_id != 0x0409) {
		return (NULL);
	}
	if (string_index < AUDIO_MAX_INDEX) {
		return (ptr[string_index]);
	}
	return (NULL);
}

static void
audio_init(void *arg __unused)
{
	struct sysctl_oid *parent;
	char parent_name[3];

	usb_make_str_desc(&audio_mixer, sizeof(audio_mixer),
	    AUDIO_DEFAULT_MIXER);
	usb_make_str_desc(&audio_record, sizeof(audio_record),
	    AUDIO_DEFAULT_RECORD);
	usb_make_str_desc(&audio_playback, sizeof(audio_playback),
	    AUDIO_DEFAULT_PLAYBACK);
	usb_make_str_desc(&audio_manufacturer, sizeof(audio_manufacturer),
	    AUDIO_DEFAULT_MANUFACTURER);
	usb_make_str_desc(&audio_product, sizeof(audio_product),
	    AUDIO_DEFAULT_PRODUCT);
	usb_make_str_desc(&audio_serial_number, sizeof(audio_serial_number),
	    AUDIO_DEFAULT_SERIAL_NUMBER);

	snprintf(parent_name, sizeof(parent_name), "%d", USB_TEMP_AUDIO);
	sysctl_ctx_init(&audio_ctx_list);

	parent = SYSCTL_ADD_NODE(&audio_ctx_list,
	    SYSCTL_STATIC_CHILDREN(_hw_usb_templates), OID_AUTO,
	    parent_name, CTLFLAG_RW,
	    0, "USB Audio Interface device side template");
	SYSCTL_ADD_U16(&audio_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "vendor_id", CTLFLAG_RWTUN, &usb_template_audio.idVendor,
	    1, "Vendor identifier");
	SYSCTL_ADD_U16(&audio_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "product_id", CTLFLAG_RWTUN, &usb_template_audio.idProduct,
	    1, "Product identifier");
#if 0
	SYSCTL_ADD_PROC(&audio_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "mixer", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &audio_mixer, sizeof(audio_mixer), usb_temp_sysctl,
	    "A", "Mixer interface string");
	SYSCTL_ADD_PROC(&audio_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "record", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &audio_record, sizeof(audio_record), usb_temp_sysctl,
	    "A", "Record interface string");
	SYSCTL_ADD_PROC(&audio_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "playback", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &audio_playback, sizeof(audio_playback), usb_temp_sysctl,
	    "A", "Playback interface string");
#endif
	SYSCTL_ADD_PROC(&audio_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "manufacturer", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &audio_manufacturer, sizeof(audio_manufacturer), usb_temp_sysctl,
	    "A", "Manufacturer string");
	SYSCTL_ADD_PROC(&audio_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "product", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &audio_product, sizeof(audio_product), usb_temp_sysctl,
	    "A", "Product string");
	SYSCTL_ADD_PROC(&audio_ctx_list, SYSCTL_CHILDREN(parent), OID_AUTO,
	    "serial_number", CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    &audio_serial_number, sizeof(audio_serial_number), usb_temp_sysctl,
	    "A", "Serial number string");
}

static void
audio_uninit(void *arg __unused)
{

	sysctl_ctx_free(&audio_ctx_list);
}

SYSINIT(audio_init, SI_SUB_LOCK, SI_ORDER_FIRST, audio_init, NULL);
SYSUNINIT(audio_uninit, SI_SUB_LOCK, SI_ORDER_FIRST, audio_uninit, NULL);
