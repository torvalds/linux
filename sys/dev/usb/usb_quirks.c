/*	$OpenBSD: usb_quirks.c,v 1.81 2025/03/27 14:12:38 sthen Exp $ */
/*	$NetBSD: usb_quirks.c,v 1.45 2003/05/10 17:47:14 hamajima Exp $	*/
/*	$FreeBSD: src/sys/dev/usb/usb_quirks.c,v 1.30 2003/01/02 04:15:55 imp Exp $	*/

/*
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net) at
 * Carlstedt Research & Technology.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <dev/usb/usb.h>

#include <dev/usb/usbdevs.h>
#include <dev/usb/usb_quirks.h>

#ifdef USB_DEBUG
extern int usbdebug;
#endif

#define ANY 0xffff

const struct usbd_quirk_entry {
	u_int16_t idVendor;
	u_int16_t idProduct;
	u_int16_t bcdDevice;
	struct usbd_quirks quirks;
} usb_quirks[] = {
 { USB_VENDOR_KYE, USB_PRODUCT_KYE_NICHE,	    0x100, { UQ_NO_SET_PROTO}},
 { USB_VENDOR_INSIDEOUT, USB_PRODUCT_INSIDEOUT_EDGEPORT4,
   						    0x094, { UQ_SWAP_UNICODE}},
 { USB_VENDOR_QTRONIX, USB_PRODUCT_QTRONIX_980N,    0x110, { UQ_SPUR_BUT_UP }},
 { USB_VENDOR_ALCOR2, USB_PRODUCT_ALCOR2_KBD_HUB,   0x001, { UQ_SPUR_BUT_UP }},
 { USB_VENDOR_MCT, USB_PRODUCT_MCT_HUB0100,         0x102, { UQ_BUS_POWERED }},
 { USB_VENDOR_MCT, USB_PRODUCT_MCT_USB232,          0x102, { UQ_BUS_POWERED }},
 { USB_VENDOR_METRICOM, USB_PRODUCT_METRICOM_RICOCHET_GS,
 	0x100, { UQ_ASSUME_CM_OVER_DATA }},
 { USB_VENDOR_SANYO, USB_PRODUCT_SANYO_SCP4900,
 	0x000, { UQ_ASSUME_CM_OVER_DATA }},
 { USB_VENDOR_MOTOROLA2, USB_PRODUCT_MOTOROLA2_T720C,
	0x001, { UQ_ASSUME_CM_OVER_DATA }},
 { USB_VENDOR_EICON, USB_PRODUCT_EICON_DIVA852,
	0x100, { UQ_ASSUME_CM_OVER_DATA }},
 /* YAMAHA router's ucdDevice is the version of firmware and often changes. */
 { USB_VENDOR_YAMAHA, USB_PRODUCT_YAMAHA_RTA54I,
	ANY, { UQ_ASSUME_CM_OVER_DATA }},
 { USB_VENDOR_YAMAHA, USB_PRODUCT_YAMAHA_RTA55I,
	ANY, { UQ_ASSUME_CM_OVER_DATA }},
 { USB_VENDOR_YAMAHA, USB_PRODUCT_YAMAHA_RTW65B,
	ANY, { UQ_ASSUME_CM_OVER_DATA }},
 { USB_VENDOR_YAMAHA, USB_PRODUCT_YAMAHA_RTW65I,
	ANY, { UQ_ASSUME_CM_OVER_DATA }},
 { USB_VENDOR_QUALCOMM, USB_PRODUCT_QUALCOMM_MSM_MODEM,
	ANY, { UQ_ASSUME_CM_OVER_DATA }},
 { USB_VENDOR_QUALCOMM2, USB_PRODUCT_QUALCOMM2_MSM_PHONE,
	ANY, { UQ_ASSUME_CM_OVER_DATA }},
 { USB_VENDOR_SUNTAC, USB_PRODUCT_SUNTAC_AS64LX,
	0x100, { UQ_ASSUME_CM_OVER_DATA }},
 { USB_VENDOR_CMOTECH, USB_PRODUCT_CMOTECH_CM5100P,
	ANY, { UQ_ASSUME_CM_OVER_DATA }},
 { USB_VENDOR_CMOTECH, USB_PRODUCT_CMOTECH_CCU550,
	ANY, { UQ_ASSUME_CM_OVER_DATA }},
 { USB_VENDOR_CMOTECH, USB_PRODUCT_CMOTECH_CNU550PRO,
	ANY, { UQ_ASSUME_CM_OVER_DATA }},
 { USB_VENDOR_SIEMENS2, USB_PRODUCT_SIEMENS2_ES75,
	ANY, { UQ_ASSUME_CM_OVER_DATA }},

 { USB_VENDOR_TI, USB_PRODUCT_TI_UTUSB41,	    0x110, { UQ_POWER_CLAIM }},

 /* XXX These should have a revision number, but I don't know what they are. */
 { USB_VENDOR_HP, USB_PRODUCT_HP_895C,		    ANY,   { UQ_BROKEN_BIDIR }},
 { USB_VENDOR_HP, USB_PRODUCT_HP_880C,		    ANY,   { UQ_BROKEN_BIDIR }},
 { USB_VENDOR_HP, USB_PRODUCT_HP_815C,		    ANY,   { UQ_BROKEN_BIDIR }},
 { USB_VENDOR_HP, USB_PRODUCT_HP_810C,		    ANY,   { UQ_BROKEN_BIDIR }},
 { USB_VENDOR_HP, USB_PRODUCT_HP_830C,		    ANY,   { UQ_BROKEN_BIDIR }},
 { USB_VENDOR_HP, USB_PRODUCT_HP_885C,		    ANY,   { UQ_BROKEN_BIDIR }},
 { USB_VENDOR_HP, USB_PRODUCT_HP_840C,		    ANY,   { UQ_BROKEN_BIDIR }},
 { USB_VENDOR_HP, USB_PRODUCT_HP_816C,		    ANY,   { UQ_BROKEN_BIDIR }},
 { USB_VENDOR_HP, USB_PRODUCT_HP_959C,		    ANY,   { UQ_BROKEN_BIDIR }},
 { USB_VENDOR_HP, USB_PRODUCT_HP_1220C,		    ANY,   { UQ_BROKEN_BIDIR }},
 { USB_VENDOR_NEC, USB_PRODUCT_NEC_PICTY900,	    ANY,   { UQ_BROKEN_BIDIR }},
 { USB_VENDOR_NEC, USB_PRODUCT_NEC_PICTY760,	    ANY,   { UQ_BROKEN_BIDIR }},
 { USB_VENDOR_NEC, USB_PRODUCT_NEC_PICTY920,	    ANY,   { UQ_BROKEN_BIDIR }},
 { USB_VENDOR_NEC, USB_PRODUCT_NEC_PICTY800,	    ANY,   { UQ_BROKEN_BIDIR }},

 { USB_VENDOR_APPLE, USB_PRODUCT_APPLE_IPHONE,		ANY,	{ UQ_BAD_HID }},
 { USB_VENDOR_APPLE, USB_PRODUCT_APPLE_IPHONE_3G,	ANY,	{ UQ_BAD_HID }},
 { USB_VENDOR_APPLE, USB_PRODUCT_APPLE_IPHONE_3GS,	ANY,	{ UQ_BAD_HID }},
 { USB_VENDOR_APPLE, USB_PRODUCT_APPLE_IPHONE_4_CDMA,	ANY,	{ UQ_BAD_HID }},
 { USB_VENDOR_APPLE, USB_PRODUCT_APPLE_IPHONE_4_GSM,	ANY,	{ UQ_BAD_HID }},
 { USB_VENDOR_APPLE, USB_PRODUCT_APPLE_IPHONE_4S,	ANY,	{ UQ_BAD_HID }},
 { USB_VENDOR_APPLE, USB_PRODUCT_APPLE_IPHONE_6,	ANY,	{ UQ_BAD_HID }},
 { USB_VENDOR_APPLE, USB_PRODUCT_APPLE_IPOD_TOUCH,	ANY,	{ UQ_BAD_HID }},
 { USB_VENDOR_APPLE, USB_PRODUCT_APPLE_IPOD_TOUCH_2G,	ANY,	{ UQ_BAD_HID }},
 { USB_VENDOR_APPLE, USB_PRODUCT_APPLE_IPOD_TOUCH_3G,	ANY,	{ UQ_BAD_HID }},
 { USB_VENDOR_APPLE, USB_PRODUCT_APPLE_IPOD_TOUCH_4G,	ANY,	{ UQ_BAD_HID }},
 { USB_VENDOR_APPLE, USB_PRODUCT_APPLE_IPAD,		ANY,	{ UQ_BAD_HID }},
 { USB_VENDOR_APPLE, USB_PRODUCT_APPLE_IPAD2,		ANY,	{ UQ_BAD_HID }},
 { USB_VENDOR_APPLE, USB_PRODUCT_APPLE_SPEAKERS,	ANY,	{ UQ_BAD_HID }},
 { USB_VENDOR_CYPRESS, USB_PRODUCT_CYPRESS_SISPM_OLD,	ANY,	{ UQ_BAD_HID }},
 { USB_VENDOR_CYPRESS, USB_PRODUCT_CYPRESS_SISPM,	ANY,	{ UQ_BAD_HID }},
 { USB_VENDOR_CYPRESS, USB_PRODUCT_CYPRESS_SISPM_FLASH,	ANY,	{ UQ_BAD_HID }},
 { USB_VENDOR_MICROCHIP, USB_PRODUCT_MICROCHIP_USBLCD20X2,	ANY,	{ UQ_BAD_HID }},
 { USB_VENDOR_MICROCHIP, USB_PRODUCT_MICROCHIP_USBLCD256X64,	ANY,	{ UQ_BAD_HID }},
 { USB_VENDOR_MECANIQUE, USB_PRODUCT_MECANIQUE_WISPY,	ANY,	{ UQ_BAD_HID }},
 { USB_VENDOR_METAGEEK, USB_PRODUCT_METAGEEK_WISPY24I,	ANY,	{ UQ_BAD_HID }},
 { USB_VENDOR_MUSTEK2, USB_PRODUCT_MUSTEK2_PM800,	ANY,	{ UQ_BAD_HID }},
 { USB_VENDOR_OMRON, USB_PRODUCT_OMRON_BX35F,		ANY,	{ UQ_BAD_HID }},
 { USB_VENDOR_OMRON, USB_PRODUCT_OMRON_BX50F,		ANY,	{ UQ_BAD_HID }},
 { USB_VENDOR_OMRON, USB_PRODUCT_OMRON_BY35S,		ANY,	{ UQ_BAD_HID }},
 { USB_VENDOR_TENX, USB_PRODUCT_TENX_MISSILE,		ANY,	{ UQ_BAD_HID }},
 { USB_VENDOR_TERRATEC, USB_PRODUCT_TERRATEC_AUREON,	ANY,	{ UQ_BAD_HID }},
 { USB_VENDOR_TI, USB_PRODUCT_TI_MSP430,		ANY,	{ UQ_BAD_HID }},
 { USB_VENDOR_VELLEMAN, USB_PRODUCT_VELLEMAN_K8055,	ANY,	{ UQ_BAD_HID }},
 { USB_VENDOR_DREAMLINK, USB_PRODUCT_DREAMLINK_ULMB1,	ANY,	{ UQ_BAD_HID }},

 { USB_VENDOR_HUAWEI, USB_PRODUCT_HUAWEI_E220,	ANY,	{ UQ_NO_STRINGS }},
 { USB_VENDOR_SHANTOU, USB_PRODUCT_SHANTOU_DM9601, ANY, { UQ_NO_STRINGS }},
 { USB_VENDOR_RALINK, USB_PRODUCT_RALINK_RT2573, ANY,	{ UQ_NO_STRINGS }},

 /* MS keyboards do weird things */
 { USB_VENDOR_MICROSOFT, USB_PRODUCT_MICROSOFT_WLNOTEBOOK,
	ANY, { UQ_MS_BAD_CLASS | UQ_MS_LEADING_BYTE }},
 { USB_VENDOR_MICROSOFT, USB_PRODUCT_MICROSOFT_WLNOTEBOOK2,
	ANY, { UQ_MS_BAD_CLASS | UQ_MS_LEADING_BYTE }},

 { USB_VENDOR_KENSINGTON, USB_PRODUCT_KENSINGTON_SLIMBLADE,
	ANY, { UQ_MS_VENDOR_BUTTONS }},

/* Devices that need their data pipe held open */
 { USB_VENDOR_CHERRY, USB_PRODUCT_CHERRY_MOUSE1,
	ANY,	{ UQ_ALWAYS_OPEN }},
 { USB_VENDOR_CHICONY, USB_PRODUCT_CHICONY_OPTMOUSE,
	ANY,	{ UQ_ALWAYS_OPEN }},
 { USB_VENDOR_DELL, USB_PRODUCT_DELL_PIXARTMOUSE,
	ANY,	{ UQ_ALWAYS_OPEN }},
 { USB_VENDOR_HAILUCK, USB_PRODUCT_HAILUCK_KEYBOARD,
	ANY,	{ UQ_ALWAYS_OPEN }},
 { USB_VENDOR_LENOVO, USB_PRODUCT_LENOVO_PIXARTMOUSE,
	ANY,	{ UQ_ALWAYS_OPEN }},
 { USB_VENDOR_LENOVO, USB_PRODUCT_LENOVO_OPTUSBMOUSE,
	ANY,	{ UQ_ALWAYS_OPEN }},
 { USB_VENDOR_LOGITECH, USB_PRODUCT_LOGITECH_B100_1,
	ANY,	{ UQ_ALWAYS_OPEN }},
 { USB_VENDOR_LOGITECH, USB_PRODUCT_LOGITECH_B100_2,
	ANY,	{ UQ_ALWAYS_OPEN }},
 { USB_VENDOR_MICROSOFT, USB_PRODUCT_MICROSOFT_PIXARTMOUSE,
	ANY,	{ UQ_ALWAYS_OPEN }},
 { USB_VENDOR_MICROSOFT, USB_PRODUCT_MICROSOFT_TYPECOVER,
	ANY,	{ UQ_ALWAYS_OPEN }},
 { USB_VENDOR_MICROSOFT, USB_PRODUCT_MICROSOFT_TYPECOVER2,
	ANY,	{ UQ_ALWAYS_OPEN }},
 { USB_VENDOR_PIXART, USB_PRODUCT_PIXART_RPIMOUSE,
	ANY,	{ UQ_ALWAYS_OPEN }},

 { 0, 0, 0, { 0 } }
};

#define bANY 0xff
const struct usbd_dev_quirk_entry {
	u_int8_t bDeviceClass;
	u_int8_t bDeviceSubClass;
	u_int8_t bDeviceProtocol;
	struct usbd_quirks quirks;
} usb_dev_quirks[] = {
 { 0, 0, 0, { 0 } }
};

const struct usbd_quirks usbd_no_quirk = { 0 };

const struct usbd_quirks *
usbd_find_quirk(usb_device_descriptor_t *d)
{
	const struct usbd_quirk_entry *t;
	const struct usbd_dev_quirk_entry *td;
	u_int16_t vendor = UGETW(d->idVendor);
	u_int16_t product = UGETW(d->idProduct);
	u_int16_t revision = UGETW(d->bcdDevice);

	/* search device specific quirks entry */
	for (t = usb_quirks; t->idVendor != 0; t++) {
		if (t->idVendor  == vendor &&
		    t->idProduct == product &&
		    (t->bcdDevice == ANY || t->bcdDevice == revision)) {
#ifdef USB_DEBUG
			if (usbdebug && t->quirks.uq_flags)
				printf("usbd_find_quirk for specific device 0x%04x/0x%04x/%x: %d\n",
					vendor, product, UGETW(d->bcdDevice),
					t->quirks.uq_flags);
#endif
	
			return (&t->quirks);
		}
	}
	/* no device specific quirks found, search class specific entry */
	for (td = usb_dev_quirks; td->bDeviceClass != 0; td++) {
		if (td->bDeviceClass == d->bDeviceClass &&
		    (td->bDeviceSubClass == bANY ||
		     td->bDeviceSubClass == d->bDeviceSubClass) &&
		    (td->bDeviceProtocol == bANY ||
		     td->bDeviceProtocol == d->bDeviceProtocol)) {
#ifdef USB_DEBUG
			if (usbdebug && td->quirks.uq_flags)
				printf("usbd_find_quirk for device class 0x%02x/0x%02x/%x: %d\n",
					d->bDeviceClass, d->bDeviceSubClass, 
					UGETW(d->bcdDevice),
					td->quirks.uq_flags);
#endif
			return (&td->quirks);
		}
	}

	return (&usbd_no_quirk);
}
