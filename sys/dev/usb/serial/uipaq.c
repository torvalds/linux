/*	$NetBSD: uipaq.c,v 1.4 2006/11/16 01:33:27 christos Exp $	*/
/*	$OpenBSD: uipaq.c,v 1.1 2005/06/17 23:50:33 deraadt Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2000-2005 The NetBSD Foundation, Inc.
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

/*
 * iPAQ driver
 *
 * 19 July 2003:	Incorporated changes suggested by Sam Lawrance from
 * 			the uppc module
 *
 *
 * Contact isis@cs.umd.edu if you have any questions/comments about this driver
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

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
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usb_cdc.h>
#include "usbdevs.h"

#define	USB_DEBUG_VAR usb_debug
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_process.h>

#include <dev/usb/serial/usb_serial.h>

#define	UIPAQ_CONFIG_INDEX	0	/* config number 1 */
#define	UIPAQ_IFACE_INDEX	0

#define	UIPAQ_BUF_SIZE		1024

enum {
	UIPAQ_BULK_DT_WR,
	UIPAQ_BULK_DT_RD,
	UIPAQ_N_TRANSFER,
};

struct uipaq_softc {
	struct ucom_super_softc sc_super_ucom;
	struct ucom_softc sc_ucom;

	struct usb_xfer *sc_xfer[UIPAQ_N_TRANSFER];
	struct usb_device *sc_udev;
	struct mtx sc_mtx;

	uint16_t sc_line;

	uint8_t	sc_lsr;			/* local status register */
	uint8_t	sc_msr;			/* modem status register */
};

static device_probe_t uipaq_probe;
static device_attach_t uipaq_attach;
static device_detach_t uipaq_detach;
static void uipaq_free_softc(struct uipaq_softc *);

static usb_callback_t uipaq_write_callback;
static usb_callback_t uipaq_read_callback;

static void	uipaq_free(struct ucom_softc *);
static void	uipaq_start_read(struct ucom_softc *);
static void	uipaq_stop_read(struct ucom_softc *);
static void	uipaq_start_write(struct ucom_softc *);
static void	uipaq_stop_write(struct ucom_softc *);
static void	uipaq_cfg_set_dtr(struct ucom_softc *, uint8_t);
static void	uipaq_cfg_set_rts(struct ucom_softc *, uint8_t);
static void	uipaq_cfg_set_break(struct ucom_softc *, uint8_t);
static void	uipaq_poll(struct ucom_softc *ucom);

static const struct usb_config uipaq_config_data[UIPAQ_N_TRANSFER] = {

	[UIPAQ_BULK_DT_WR] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.bufsize = UIPAQ_BUF_SIZE,
		.flags = {.pipe_bof = 1,.force_short_xfer = 1,},
		.callback = &uipaq_write_callback,
	},

	[UIPAQ_BULK_DT_RD] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = UIPAQ_BUF_SIZE,
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.callback = &uipaq_read_callback,
	},
};

static const struct ucom_callback uipaq_callback = {
	.ucom_cfg_set_dtr = &uipaq_cfg_set_dtr,
	.ucom_cfg_set_rts = &uipaq_cfg_set_rts,
	.ucom_cfg_set_break = &uipaq_cfg_set_break,
	.ucom_start_read = &uipaq_start_read,
	.ucom_stop_read = &uipaq_stop_read,
	.ucom_start_write = &uipaq_start_write,
	.ucom_stop_write = &uipaq_stop_write,
	.ucom_poll = &uipaq_poll,
	.ucom_free = &uipaq_free,
};

/*
 * Much of this list is generated from lists of other drivers that
 * support the same hardware. Numeric values are used where no usbdevs
 * entries exist.
 */
static const STRUCT_USB_HOST_ID uipaq_devs[] = {
	/* Socket USB Sync */
	{USB_VPI(0x0104, 0x00be, 0)},
	/* USB Sync 0301 */
	{USB_VPI(0x04ad, 0x0301, 0)},
	/* USB Sync 0302 */
	{USB_VPI(0x04ad, 0x0302, 0)},
	/* USB Sync 0303 */
	{USB_VPI(0x04ad, 0x0303, 0)},
	/* GPS Pocket PC USB Sync */
	{USB_VPI(0x04ad, 0x0306, 0)},
	/* HHP PDT */
	{USB_VPI(0x0536, 0x01a0, 0)},
	/* Intermec Mobile Computer */
	{USB_VPI(0x067e, 0x1001, 0)},
	/* Linkup Systems USB Sync */
	{USB_VPI(0x094b, 0x0001, 0)},
	/* BCOM USB Sync 0065 */
	{USB_VPI(0x0960, 0x0065, 0)},
	/* BCOM USB Sync 0066 */
	{USB_VPI(0x0960, 0x0066, 0)},
	/* BCOM USB Sync 0067 */
	{USB_VPI(0x0960, 0x0067, 0)},
	/* Portatec USB Sync */
	{USB_VPI(0x0961, 0x0010, 0)},
	/* Trimble GeoExplorer */
	{USB_VPI(0x099e, 0x0052, 0)},
	/* TDS Data Collector */
	{USB_VPI(0x099e, 0x4000, 0)},
	/* Motorola iDEN Smartphone */
	{USB_VPI(0x0c44, 0x03a2, 0)},
	/* Cesscom Luxian Series */
	{USB_VPI(0x0c8e, 0x6000, 0)},
	/* Motorola PowerPad Pocket PCDevice */
	{USB_VPI(0x0cad, 0x9001, 0)},
	/* Freedom Scientific USB Sync */
	{USB_VPI(0x0f4e, 0x0200, 0)},
	/* Cyberbank USB Sync */
	{USB_VPI(0x0f98, 0x0201, 0)},
	/* Wistron USB Sync */
	{USB_VPI(0x0fb8, 0x3001, 0)},
	/* Wistron USB Sync */
	{USB_VPI(0x0fb8, 0x3002, 0)},
	/* Wistron USB Sync */
	{USB_VPI(0x0fb8, 0x3003, 0)},
	/* Wistron USB Sync */
	{USB_VPI(0x0fb8, 0x4001, 0)},
	/* E-TEN USB Sync */
	{USB_VPI(0x1066, 0x00ce, 0)},
	/* E-TEN P3XX Pocket PC */
	{USB_VPI(0x1066, 0x0300, 0)},
	/* E-TEN P5XX Pocket PC */
	{USB_VPI(0x1066, 0x0500, 0)},
	/* E-TEN P6XX Pocket PC */
	{USB_VPI(0x1066, 0x0600, 0)},
	/* E-TEN P7XX Pocket PC */
	{USB_VPI(0x1066, 0x0700, 0)},
	/* Psion Teklogix Sync 753x */
	{USB_VPI(0x1114, 0x0001, 0)},
	/* Psion Teklogix Sync netBookPro */
	{USB_VPI(0x1114, 0x0004, 0)},
	/* Psion Teklogix Sync 7525 */
	{USB_VPI(0x1114, 0x0006, 0)},
	/* VES USB Sync */
	{USB_VPI(0x1182, 0x1388, 0)},
	/* Rugged Pocket PC 2003 */
	{USB_VPI(0x11d9, 0x1002, 0)},
	/* Rugged Pocket PC 2003 */
	{USB_VPI(0x11d9, 0x1003, 0)},
	/* USB Sync 03 */
	{USB_VPI(0x1231, 0xce01, 0)},
	/* USB Sync 03 */
	{USB_VPI(0x1231, 0xce02, 0)},
	/* Mio DigiWalker PPC StrongARM */
	{USB_VPI(0x3340, 0x011c, 0)},
	/* Mio DigiWalker 338 */
	{USB_VPI(0x3340, 0x0326, 0)},
	/* Mio DigiWalker 338 */
	{USB_VPI(0x3340, 0x0426, 0)},
	/* Mio DigiWalker USB Sync */
	{USB_VPI(0x3340, 0x043a, 0)},
	/* MiTAC USB Sync 528 */
	{USB_VPI(0x3340, 0x051c, 0)},
	/* Mio DigiWalker SmartPhone USB Sync */
	{USB_VPI(0x3340, 0x053a, 0)},
	/* MiTAC USB Sync */
	{USB_VPI(0x3340, 0x071c, 0)},
	/* Generic PPC StrongARM */
	{USB_VPI(0x3340, 0x0b1c, 0)},
	/* Generic PPC USB Sync */
	{USB_VPI(0x3340, 0x0e3a, 0)},
	/* Itautec USB Sync */
	{USB_VPI(0x3340, 0x0f1c, 0)},
	/* Generic SmartPhone USB Sync */
	{USB_VPI(0x3340, 0x0f3a, 0)},
	/* Itautec USB Sync */
	{USB_VPI(0x3340, 0x1326, 0)},
	/* YAKUMO USB Sync */
	{USB_VPI(0x3340, 0x191c, 0)},
	/* Vobis USB Sync */
	{USB_VPI(0x3340, 0x2326, 0)},
	/* MEDION Winodws Moble USB Sync */
	{USB_VPI(0x3340, 0x3326, 0)},
	/* Legend USB Sync */
	{USB_VPI(0x3708, 0x20ce, 0)},
	/* Lenovo USB Sync */
	{USB_VPI(0x3708, 0x21ce, 0)},
	/* Mobile Media Technology USB Sync */
	{USB_VPI(0x4113, 0x0210, 0)},
	/* Mobile Media Technology USB Sync */
	{USB_VPI(0x4113, 0x0211, 0)},
	/* Mobile Media Technology USB Sync */
	{USB_VPI(0x4113, 0x0400, 0)},
	/* Mobile Media Technology USB Sync */
	{USB_VPI(0x4113, 0x0410, 0)},
	/* Smartphone */
	{USB_VPI(0x4505, 0x0010, 0)},
	/* SAGEM Wireless Assistant */
	{USB_VPI(0x5e04, 0xce00, 0)},
	/* c10 Series */
	{USB_VPI(USB_VENDOR_ACER, 0x1631, 0)},
	/* c20 Series */
	{USB_VPI(USB_VENDOR_ACER, 0x1632, 0)},
	/* Acer n10 Handheld USB Sync */
	{USB_VPI(USB_VENDOR_ACER, 0x16e1, 0)},
	/* Acer n20 Handheld USB Sync */
	{USB_VPI(USB_VENDOR_ACER, 0x16e2, 0)},
	/* Acer n30 Handheld USB Sync */
	{USB_VPI(USB_VENDOR_ACER, 0x16e3, 0)},
	/* ASUS USB Sync */
	{USB_VPI(USB_VENDOR_ASUS, 0x4200, 0)},
	/* ASUS USB Sync */
	{USB_VPI(USB_VENDOR_ASUS, 0x4201, 0)},
	/* ASUS USB Sync */
	{USB_VPI(USB_VENDOR_ASUS, 0x4202, 0)},
	/* ASUS USB Sync */
	{USB_VPI(USB_VENDOR_ASUS, 0x9200, 0)},
	/* ASUS USB Sync */
	{USB_VPI(USB_VENDOR_ASUS, 0x9202, 0)},
	/**/
	{USB_VPI(USB_VENDOR_ASUS, USB_PRODUCT_ASUS_P535, 0)},
	/* CASIO USB Sync 2001 */
	{USB_VPI(USB_VENDOR_CASIO, 0x2001, 0)},
	/* CASIO USB Sync 2003 */
	{USB_VPI(USB_VENDOR_CASIO, 0x2003, 0)},
	/**/
	{USB_VPI(USB_VENDOR_CASIO, USB_PRODUCT_CASIO_BE300, 0)},
	/* MyGuide 7000 XL USB Sync */
	{USB_VPI(USB_VENDOR_COMPAL, 0x0531, 0)},
	/* Compaq iPAQ USB Sync */
	{USB_VPI(USB_VENDOR_COMPAQ, 0x0032, 0)},
	/**/
	{USB_VPI(USB_VENDOR_COMPAQ, USB_PRODUCT_COMPAQ_IPAQPOCKETPC, 0)},
	/* Dell Axim USB Sync */
	{USB_VPI(USB_VENDOR_DELL, 0x4001, 0)},
	/* Dell Axim USB Sync */
	{USB_VPI(USB_VENDOR_DELL, 0x4002, 0)},
	/* Dell Axim USB Sync */
	{USB_VPI(USB_VENDOR_DELL, 0x4003, 0)},
	/* Dell Axim USB Sync */
	{USB_VPI(USB_VENDOR_DELL, 0x4004, 0)},
	/* Dell Axim USB Sync */
	{USB_VPI(USB_VENDOR_DELL, 0x4005, 0)},
	/* Dell Axim USB Sync */
	{USB_VPI(USB_VENDOR_DELL, 0x4006, 0)},
	/* Dell Axim USB Sync */
	{USB_VPI(USB_VENDOR_DELL, 0x4007, 0)},
	/* Dell Axim USB Sync */
	{USB_VPI(USB_VENDOR_DELL, 0x4008, 0)},
	/* Dell Axim USB Sync */
	{USB_VPI(USB_VENDOR_DELL, 0x4009, 0)},
	/* Fujitsu Siemens Computers USB Sync */
	{USB_VPI(USB_VENDOR_FSC, 0x1001, 0)},
	/* FUJITSU USB Sync */
	{USB_VPI(USB_VENDOR_FUJITSU, 0x1058, 0)},
	/* FUJITSU USB Sync */
	{USB_VPI(USB_VENDOR_FUJITSU, 0x1079, 0)},
	/* Askey USB Sync */
	{USB_VPI(USB_VENDOR_GIGASET, 0x0601, 0)},
	/* Hitachi USB Sync */
	{USB_VPI(USB_VENDOR_HITACHI, 0x0014, 0)},
	/* HP USB Sync 1612 */
	{USB_VPI(USB_VENDOR_HP, 0x1216, 0)},
	/* HP USB Sync 1620 */
	{USB_VPI(USB_VENDOR_HP, 0x2016, 0)},
	/* HP USB Sync 1621 */
	{USB_VPI(USB_VENDOR_HP, 0x2116, 0)},
	/* HP USB Sync 1622 */
	{USB_VPI(USB_VENDOR_HP, 0x2216, 0)},
	/* HP USB Sync 1630 */
	{USB_VPI(USB_VENDOR_HP, 0x3016, 0)},
	/* HP USB Sync 1631 */
	{USB_VPI(USB_VENDOR_HP, 0x3116, 0)},
	/* HP USB Sync 1632 */
	{USB_VPI(USB_VENDOR_HP, 0x3216, 0)},
	/* HP USB Sync 1640 */
	{USB_VPI(USB_VENDOR_HP, 0x4016, 0)},
	/* HP USB Sync 1641 */
	{USB_VPI(USB_VENDOR_HP, 0x4116, 0)},
	/* HP USB Sync 1642 */
	{USB_VPI(USB_VENDOR_HP, 0x4216, 0)},
	/* HP USB Sync 1650 */
	{USB_VPI(USB_VENDOR_HP, 0x5016, 0)},
	/* HP USB Sync 1651 */
	{USB_VPI(USB_VENDOR_HP, 0x5116, 0)},
	/* HP USB Sync 1652 */
	{USB_VPI(USB_VENDOR_HP, 0x5216, 0)},
	/**/
	{USB_VPI(USB_VENDOR_HP, USB_PRODUCT_HP_2215, 0)},
	/**/
	{USB_VPI(USB_VENDOR_HP, USB_PRODUCT_HP_568J, 0)},
	/* HTC USB Modem */
	{USB_VPI(USB_VENDOR_HTC, 0x00cf, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a01, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a02, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a03, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a04, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a05, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a06, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a07, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a08, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a09, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a0a, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a0b, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a0c, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a0d, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a0e, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a0f, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a10, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a11, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a12, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a13, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a14, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a15, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a16, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a17, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a18, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a19, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a1a, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a1b, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a1c, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a1d, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a1e, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a1f, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a20, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a21, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a22, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a23, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a24, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a25, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a26, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a27, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a28, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a29, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a2a, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a2b, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a2c, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a2d, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a2e, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a2f, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a30, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a31, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a32, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a33, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a34, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a35, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a36, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a37, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a38, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a39, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a3a, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a3b, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a3c, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a3d, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a3e, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a3f, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a40, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a41, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a42, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a43, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a44, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a45, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a46, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a47, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a48, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a49, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a4a, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a4b, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a4c, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a4d, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a4e, 0)},
	/* PocketPC USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a4f, 0)},
	/* HTC SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a50, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a52, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a53, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a54, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a55, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a56, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a57, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a58, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a59, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a5a, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a5b, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a5c, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a5d, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a5e, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a5f, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a60, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a61, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a62, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a63, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a64, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a65, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a66, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a67, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a68, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a69, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a6a, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a6b, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a6c, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a6d, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a6e, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a6f, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a70, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a71, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a72, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a73, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a74, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a75, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a76, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a77, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a78, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a79, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a7a, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a7b, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a7c, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a7d, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a7e, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a7f, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a80, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a81, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a82, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a83, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a84, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a85, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a86, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a87, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a88, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a89, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a8a, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a8b, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a8c, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a8d, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a8e, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a8f, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a90, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a91, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a92, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a93, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a94, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a95, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a96, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a97, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a98, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a99, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a9a, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a9b, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a9c, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a9d, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a9e, 0)},
	/* SmartPhone USB Sync */
	{USB_VPI(USB_VENDOR_HTC, 0x0a9f, 0)},
	/**/
	{USB_VPI(USB_VENDOR_HTC, USB_PRODUCT_HTC_PPC6700MODEM, 0)},
	/**/
	{USB_VPI(USB_VENDOR_HTC, USB_PRODUCT_HTC_SMARTPHONE, 0)},
	/**/
	{USB_VPI(USB_VENDOR_HTC, USB_PRODUCT_HTC_WINMOBILE, 0)},
	/* High Tech Computer Wizard Smartphone */
	{USB_VPI(USB_VENDOR_HTC, USB_PRODUCT_HTC_WIZARD, 0)},
	/* JVC USB Sync */
	{USB_VPI(USB_VENDOR_JVC, 0x3011, 0)},
	/* JVC USB Sync */
	{USB_VPI(USB_VENDOR_JVC, 0x3012, 0)},
	/* LGE USB Sync */
	{USB_VPI(USB_VENDOR_LG, 0x9c01, 0)},
	/* Microsoft USB Sync */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x00ce, 0)},
	/* Windows Pocket PC 2002 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0400, 0)},
	/* Windows Pocket PC 2002 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0401, 0)},
	/* Windows Pocket PC 2002 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0402, 0)},
	/* Windows Pocket PC 2002 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0403, 0)},
	/* Windows Pocket PC 2002 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0404, 0)},
	/* Windows Pocket PC 2002 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0405, 0)},
	/* Windows Pocket PC 2002 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0406, 0)},
	/* Windows Pocket PC 2002 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0407, 0)},
	/* Windows Pocket PC 2002 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0408, 0)},
	/* Windows Pocket PC 2002 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0409, 0)},
	/* Windows Pocket PC 2002 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x040a, 0)},
	/* Windows Pocket PC 2002 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x040b, 0)},
	/* Windows Pocket PC 2002 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x040c, 0)},
	/* Windows Pocket PC 2002 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x040d, 0)},
	/* Windows Pocket PC 2002 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x040e, 0)},
	/* Windows Pocket PC 2002 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x040f, 0)},
	/* Windows Pocket PC 2002 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0410, 0)},
	/* Windows Pocket PC 2002 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0411, 0)},
	/* Windows Pocket PC 2002 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0412, 0)},
	/* Windows Pocket PC 2002 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0413, 0)},
	/* Windows Pocket PC 2002 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0414, 0)},
	/* Windows Pocket PC 2002 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0415, 0)},
	/* Windows Pocket PC 2002 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0416, 0)},
	/* Windows Pocket PC 2002 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0417, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0432, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0433, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0434, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0435, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0436, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0437, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0438, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0439, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x043a, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x043b, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x043c, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x043d, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x043e, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x043f, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0440, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0441, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0442, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0443, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0444, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0445, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0446, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0447, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0448, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0449, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x044a, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x044b, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x044c, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x044d, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x044e, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x044f, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0450, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0451, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0452, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0453, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0454, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0455, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0456, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0457, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0458, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0459, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x045a, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x045b, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x045c, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x045d, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x045e, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x045f, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0460, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0461, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0462, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0463, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0464, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0465, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0466, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0467, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0468, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0469, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x046a, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x046b, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x046c, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x046d, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x046e, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x046f, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0470, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0471, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0472, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0473, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0474, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0475, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0476, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0477, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0478, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x0479, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x047a, 0)},
	/* Windows Pocket PC 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x047b, 0)},
	/* Windows Smartphone 2002 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x04c8, 0)},
	/* Windows Smartphone 2002 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x04c9, 0)},
	/* Windows Smartphone 2002 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x04ca, 0)},
	/* Windows Smartphone 2002 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x04cb, 0)},
	/* Windows Smartphone 2002 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x04cc, 0)},
	/* Windows Smartphone 2002 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x04cd, 0)},
	/* Windows Smartphone 2002 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x04ce, 0)},
	/* Windows Smartphone 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x04d7, 0)},
	/* Windows Smartphone 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x04d8, 0)},
	/* Windows Smartphone 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x04d9, 0)},
	/* Windows Smartphone 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x04da, 0)},
	/* Windows Smartphone 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x04db, 0)},
	/* Windows Smartphone 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x04dc, 0)},
	/* Windows Smartphone 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x04dd, 0)},
	/* Windows Smartphone 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x04de, 0)},
	/* Windows Smartphone 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x04df, 0)},
	/* Windows Smartphone 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x04e0, 0)},
	/* Windows Smartphone 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x04e1, 0)},
	/* Windows Smartphone 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x04e2, 0)},
	/* Windows Smartphone 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x04e3, 0)},
	/* Windows Smartphone 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x04e4, 0)},
	/* Windows Smartphone 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x04e5, 0)},
	/* Windows Smartphone 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x04e6, 0)},
	/* Windows Smartphone 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x04e7, 0)},
	/* Windows Smartphone 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x04e8, 0)},
	/* Windows Smartphone 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x04e9, 0)},
	/* Windows Smartphone 2003 */
	{USB_VPI(USB_VENDOR_MICROSOFT, 0x04ea, 0)},
	/* Motorola MPx200 Smartphone */
	{USB_VPI(USB_VENDOR_MOTOROLA2, 0x4204, 0)},
	/* Motorola MPc GSM */
	{USB_VPI(USB_VENDOR_MOTOROLA2, 0x4214, 0)},
	/* Motorola MPx220 Smartphone */
	{USB_VPI(USB_VENDOR_MOTOROLA2, 0x4224, 0)},
	/* Motorola MPc CDMA */
	{USB_VPI(USB_VENDOR_MOTOROLA2, 0x4234, 0)},
	/* Motorola MPx100 Smartphone */
	{USB_VPI(USB_VENDOR_MOTOROLA2, 0x4244, 0)},
	/* NEC USB Sync */
	{USB_VPI(USB_VENDOR_NEC, 0x00d5, 0)},
	/* NEC USB Sync */
	{USB_VPI(USB_VENDOR_NEC, 0x00d6, 0)},
	/* NEC USB Sync */
	{USB_VPI(USB_VENDOR_NEC, 0x00d7, 0)},
	/* NEC USB Sync */
	{USB_VPI(USB_VENDOR_NEC, 0x8024, 0)},
	/* NEC USB Sync */
	{USB_VPI(USB_VENDOR_NEC, 0x8025, 0)},
	/* Panasonic USB Sync */
	{USB_VPI(USB_VENDOR_PANASONIC, 0x2500, 0)},
	/* Samsung NEXiO USB Sync */
	{USB_VPI(USB_VENDOR_SAMSUNG, 0x5f00, 0)},
	/* Samsung NEXiO USB Sync */
	{USB_VPI(USB_VENDOR_SAMSUNG, 0x5f01, 0)},
	/* Samsung NEXiO USB Sync */
	{USB_VPI(USB_VENDOR_SAMSUNG, 0x5f02, 0)},
	/* Samsung NEXiO USB Sync */
	{USB_VPI(USB_VENDOR_SAMSUNG, 0x5f03, 0)},
	/* Samsung NEXiO USB Sync */
	{USB_VPI(USB_VENDOR_SAMSUNG, 0x5f04, 0)},
	/* Samsung MITs USB Sync */
	{USB_VPI(USB_VENDOR_SAMSUNG, 0x6611, 0)},
	/* Samsung MITs USB Sync */
	{USB_VPI(USB_VENDOR_SAMSUNG, 0x6613, 0)},
	/* Samsung MITs USB Sync */
	{USB_VPI(USB_VENDOR_SAMSUNG, 0x6615, 0)},
	/* Samsung MITs USB Sync */
	{USB_VPI(USB_VENDOR_SAMSUNG, 0x6617, 0)},
	/* Samsung MITs USB Sync */
	{USB_VPI(USB_VENDOR_SAMSUNG, 0x6619, 0)},
	/* Samsung MITs USB Sync */
	{USB_VPI(USB_VENDOR_SAMSUNG, 0x661b, 0)},
	/* Samsung MITs USB Sync */
	{USB_VPI(USB_VENDOR_SAMSUNG, 0x662e, 0)},
	/* Samsung MITs USB Sync */
	{USB_VPI(USB_VENDOR_SAMSUNG, 0x6630, 0)},
	/* Samsung MITs USB Sync */
	{USB_VPI(USB_VENDOR_SAMSUNG, 0x6632, 0)},
	/* SHARP WS003SH USB Modem */
	{USB_VPI(USB_VENDOR_SHARP, 0x9102, 0)},
	/* SHARP WS004SH USB Modem */
	{USB_VPI(USB_VENDOR_SHARP, 0x9121, 0)},
	/* SHARP S01SH USB Modem */
	{USB_VPI(USB_VENDOR_SHARP, 0x9151, 0)},
	/**/
	{USB_VPI(USB_VENDOR_SHARP, USB_PRODUCT_SHARP_WZERO3ES, 0)},
	/**/
	{USB_VPI(USB_VENDOR_SHARP, USB_PRODUCT_SHARP_WZERO3ADES, 0)},
	/**/
	{USB_VPI(USB_VENDOR_SHARP, USB_PRODUCT_SHARP_WILLCOM03, 0)},
	/* Symbol USB Sync */
	{USB_VPI(USB_VENDOR_SYMBOL, 0x2000, 0)},
	/* Symbol USB Sync 0x2001 */
	{USB_VPI(USB_VENDOR_SYMBOL, 0x2001, 0)},
	/* Symbol USB Sync 0x2002 */
	{USB_VPI(USB_VENDOR_SYMBOL, 0x2002, 0)},
	/* Symbol USB Sync 0x2003 */
	{USB_VPI(USB_VENDOR_SYMBOL, 0x2003, 0)},
	/* Symbol USB Sync 0x2004 */
	{USB_VPI(USB_VENDOR_SYMBOL, 0x2004, 0)},
	/* Symbol USB Sync 0x2005 */
	{USB_VPI(USB_VENDOR_SYMBOL, 0x2005, 0)},
	/* Symbol USB Sync 0x2006 */
	{USB_VPI(USB_VENDOR_SYMBOL, 0x2006, 0)},
	/* Symbol USB Sync 0x2007 */
	{USB_VPI(USB_VENDOR_SYMBOL, 0x2007, 0)},
	/* Symbol USB Sync 0x2008 */
	{USB_VPI(USB_VENDOR_SYMBOL, 0x2008, 0)},
	/* Symbol USB Sync 0x2009 */
	{USB_VPI(USB_VENDOR_SYMBOL, 0x2009, 0)},
	/* Symbol USB Sync 0x200a */
	{USB_VPI(USB_VENDOR_SYMBOL, 0x200a, 0)},
	/* TOSHIBA USB Sync 0700 */
	{USB_VPI(USB_VENDOR_TOSHIBA, 0x0700, 0)},
	/* TOSHIBA Pocket PC e310 */
	{USB_VPI(USB_VENDOR_TOSHIBA, 0x0705, 0)},
	/* TOSHIBA Pocket PC e330 Series */
	{USB_VPI(USB_VENDOR_TOSHIBA, 0x0707, 0)},
	/* TOSHIBA Pocket PC e350Series */
	{USB_VPI(USB_VENDOR_TOSHIBA, 0x0708, 0)},
	/* TOSHIBA Pocket PC e750 Series */
	{USB_VPI(USB_VENDOR_TOSHIBA, 0x0709, 0)},
	/* TOSHIBA Pocket PC e400 Series */
	{USB_VPI(USB_VENDOR_TOSHIBA, 0x070a, 0)},
	/* TOSHIBA Pocket PC e800 Series */
	{USB_VPI(USB_VENDOR_TOSHIBA, 0x070b, 0)},
	/* TOSHIBA Pocket PC e740 */
	{USB_VPI(USB_VENDOR_TOSHIBA, USB_PRODUCT_TOSHIBA_POCKETPC_E740, 0)},
	/* ViewSonic Color Pocket PC V35 */
	{USB_VPI(USB_VENDOR_VIEWSONIC, 0x0ed9, 0)},
	/* ViewSonic Color Pocket PC V36 */
	{USB_VPI(USB_VENDOR_VIEWSONIC, 0x1527, 0)},
	/* ViewSonic Color Pocket PC V37 */
	{USB_VPI(USB_VENDOR_VIEWSONIC, 0x1529, 0)},
	/* ViewSonic Color Pocket PC V38 */
	{USB_VPI(USB_VENDOR_VIEWSONIC, 0x152b, 0)},
	/* ViewSonic Pocket PC */
	{USB_VPI(USB_VENDOR_VIEWSONIC, 0x152e, 0)},
	/* ViewSonic Communicator Pocket PC */
	{USB_VPI(USB_VENDOR_VIEWSONIC, 0x1921, 0)},
	/* ViewSonic Smartphone */
	{USB_VPI(USB_VENDOR_VIEWSONIC, 0x1922, 0)},
	/* ViewSonic Pocket PC V30 */
	{USB_VPI(USB_VENDOR_VIEWSONIC, 0x1923, 0)},
};

static device_method_t uipaq_methods[] = {
	DEVMETHOD(device_probe, uipaq_probe),
	DEVMETHOD(device_attach, uipaq_attach),
	DEVMETHOD(device_detach, uipaq_detach),
	DEVMETHOD_END
};

static devclass_t uipaq_devclass;

static driver_t uipaq_driver = {
	.name = "uipaq",
	.methods = uipaq_methods,
	.size = sizeof(struct uipaq_softc),
};

DRIVER_MODULE(uipaq, uhub, uipaq_driver, uipaq_devclass, NULL, 0);
MODULE_DEPEND(uipaq, ucom, 1, 1, 1);
MODULE_DEPEND(uipaq, usb, 1, 1, 1);
MODULE_VERSION(uipaq, 1);
USB_PNP_HOST_INFO(uipaq_devs);

static int
uipaq_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	if (uaa->usb_mode != USB_MODE_HOST) {
		return (ENXIO);
	}
	if (uaa->info.bConfigIndex != UIPAQ_CONFIG_INDEX) {
		return (ENXIO);
	}
	if (uaa->info.bIfaceIndex != UIPAQ_IFACE_INDEX) {
		return (ENXIO);
	}
	if (uaa->info.bInterfaceClass == UICLASS_IAD) {
		DPRINTF("IAD detected - not UIPAQ serial device\n");
		return (ENXIO);
	}
	return (usbd_lookup_id_by_uaa(uipaq_devs, sizeof(uipaq_devs), uaa));
}

static int
uipaq_attach(device_t dev)
{
	struct usb_device_request req;
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct uipaq_softc *sc = device_get_softc(dev);
	int error;
	uint8_t iface_index;
	uint8_t i;

	sc->sc_udev = uaa->device;

	device_set_usb_desc(dev);
	mtx_init(&sc->sc_mtx, "uipaq", NULL, MTX_DEF);
	ucom_ref(&sc->sc_super_ucom);

	/*
	 * Send magic bytes, cribbed from Linux ipaq driver that
	 * claims to have sniffed them from Win98. Wait for driver to
	 * become ready on device side?
	 */
	req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
	req.bRequest = UCDC_SET_CONTROL_LINE_STATE;
	USETW(req.wValue, UCDC_LINE_DTR);
	USETW(req.wIndex, 0x0);
	USETW(req.wLength, 0);
	for (i = 0; i != 64; i++) {
		error =
		    usbd_do_request_flags(uaa->device, NULL, &req,
		    NULL, 0, NULL, 100);
		if (error == 0)
			break;
		usb_pause_mtx(NULL, hz / 10);
	}

	iface_index = UIPAQ_IFACE_INDEX;
	error = usbd_transfer_setup(uaa->device, &iface_index,
	    sc->sc_xfer, uipaq_config_data,
	    UIPAQ_N_TRANSFER, sc, &sc->sc_mtx);

	if (error) {
		goto detach;
	}
	/* clear stall at first run */
	mtx_lock(&sc->sc_mtx);
	usbd_xfer_set_stall(sc->sc_xfer[UIPAQ_BULK_DT_WR]);
	usbd_xfer_set_stall(sc->sc_xfer[UIPAQ_BULK_DT_RD]);
	mtx_unlock(&sc->sc_mtx);

	error = ucom_attach(&sc->sc_super_ucom, &sc->sc_ucom, 1, sc,
	    &uipaq_callback, &sc->sc_mtx);
	if (error) {
		goto detach;
	}
	ucom_set_pnpinfo_usb(&sc->sc_super_ucom, dev);

	return (0);

detach:
	uipaq_detach(dev);
	return (ENXIO);
}

int
uipaq_detach(device_t dev)
{
	struct uipaq_softc *sc = device_get_softc(dev);

	ucom_detach(&sc->sc_super_ucom, &sc->sc_ucom);
	usbd_transfer_unsetup(sc->sc_xfer, UIPAQ_N_TRANSFER);

	device_claim_softc(dev);

	uipaq_free_softc(sc);

	return (0);
}

UCOM_UNLOAD_DRAIN(uipaq);

static void
uipaq_free_softc(struct uipaq_softc *sc)
{
	if (ucom_unref(&sc->sc_super_ucom)) {
		mtx_destroy(&sc->sc_mtx);
		device_free_softc(sc);
	}
}

static void
uipaq_free(struct ucom_softc *ucom)
{
	uipaq_free_softc(ucom->sc_parent);
}

static void
uipaq_start_read(struct ucom_softc *ucom)
{
	struct uipaq_softc *sc = ucom->sc_parent;

	/* start read endpoint */
	usbd_transfer_start(sc->sc_xfer[UIPAQ_BULK_DT_RD]);
}

static void
uipaq_stop_read(struct ucom_softc *ucom)
{
	struct uipaq_softc *sc = ucom->sc_parent;

	/* stop read endpoint */
	usbd_transfer_stop(sc->sc_xfer[UIPAQ_BULK_DT_RD]);
}

static void
uipaq_start_write(struct ucom_softc *ucom)
{
	struct uipaq_softc *sc = ucom->sc_parent;

	usbd_transfer_start(sc->sc_xfer[UIPAQ_BULK_DT_WR]);
}

static void
uipaq_stop_write(struct ucom_softc *ucom)
{
	struct uipaq_softc *sc = ucom->sc_parent;

	usbd_transfer_stop(sc->sc_xfer[UIPAQ_BULK_DT_WR]);
}

static void
uipaq_cfg_set_dtr(struct ucom_softc *ucom, uint8_t onoff)
{
	struct uipaq_softc *sc = ucom->sc_parent;
	struct usb_device_request req;

	DPRINTF("onoff=%d\n", onoff);

	if (onoff)
		sc->sc_line |= UCDC_LINE_DTR;
	else
		sc->sc_line &= ~UCDC_LINE_DTR;

	req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
	req.bRequest = UCDC_SET_CONTROL_LINE_STATE;
	USETW(req.wValue, sc->sc_line);
	req.wIndex[0] = UIPAQ_IFACE_INDEX;
	req.wIndex[1] = 0;
	USETW(req.wLength, 0);

	ucom_cfg_do_request(sc->sc_udev, &sc->sc_ucom, 
	    &req, NULL, 0, 1000);
}

static void
uipaq_cfg_set_rts(struct ucom_softc *ucom, uint8_t onoff)
{
	struct uipaq_softc *sc = ucom->sc_parent;
	struct usb_device_request req;

	DPRINTF("onoff=%d\n", onoff);

	if (onoff)
		sc->sc_line |= UCDC_LINE_RTS;
	else
		sc->sc_line &= ~UCDC_LINE_RTS;

	req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
	req.bRequest = UCDC_SET_CONTROL_LINE_STATE;
	USETW(req.wValue, sc->sc_line);
	req.wIndex[0] = UIPAQ_IFACE_INDEX;
	req.wIndex[1] = 0;
	USETW(req.wLength, 0);

	ucom_cfg_do_request(sc->sc_udev, &sc->sc_ucom, 
	    &req, NULL, 0, 1000);
}

static void
uipaq_cfg_set_break(struct ucom_softc *ucom, uint8_t onoff)
{
	struct uipaq_softc *sc = ucom->sc_parent;
	struct usb_device_request req;
	uint16_t temp;

	temp = onoff ? UCDC_BREAK_ON : UCDC_BREAK_OFF;

	req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
	req.bRequest = UCDC_SEND_BREAK;
	USETW(req.wValue, temp);
	req.wIndex[0] = UIPAQ_IFACE_INDEX;
	req.wIndex[1] = 0;
	USETW(req.wLength, 0);

	ucom_cfg_do_request(sc->sc_udev, &sc->sc_ucom, 
	    &req, NULL, 0, 1000);
}

static void
uipaq_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct uipaq_softc *sc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	uint32_t actlen;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_SETUP:
	case USB_ST_TRANSFERRED:
tr_setup:
		pc = usbd_xfer_get_frame(xfer, 0);
		if (ucom_get_data(&sc->sc_ucom, pc, 0,
		    UIPAQ_BUF_SIZE, &actlen)) {
			usbd_xfer_set_frame_len(xfer, 0, actlen);
			usbd_transfer_submit(xfer);
		}
		return;

	default:			/* Error */
		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		return;
	}
}

static void
uipaq_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct uipaq_softc *sc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		pc = usbd_xfer_get_frame(xfer, 0);
		ucom_put_data(&sc->sc_ucom, pc, 0, actlen);

	case USB_ST_SETUP:
tr_setup:
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		return;

	default:			/* Error */
		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		return;
	}
}

static void
uipaq_poll(struct ucom_softc *ucom)
{
	struct uipaq_softc *sc = ucom->sc_parent;
	usbd_transfer_poll(sc->sc_xfer, UIPAQ_N_TRANSFER);
}
