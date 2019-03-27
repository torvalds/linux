/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Hans Petter Selasky. All rights reserved.
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

#ifndef _USB_QUIRK_H_
#define	_USB_QUIRK_H_

enum {
	/*
	 * Keep in sync with usb_quirk_str in usb_quirk.c, and with
	 * share/man/man4/usb_quirk.4
	 */
	UQ_NONE,		/* not a valid quirk */

	UQ_MATCH_VENDOR_ONLY,	/* match quirk on vendor only */

	/* Various quirks */

	UQ_AUDIO_SWAP_LR,	/* left and right sound channels are swapped */
	UQ_AU_INP_ASYNC,	/* input is async despite claim of adaptive */
	UQ_AU_NO_FRAC,		/* don't adjust for fractional samples */
	UQ_AU_NO_XU,		/* audio device has broken extension unit */
	UQ_BAD_ADC,		/* bad audio spec version number */
	UQ_BAD_AUDIO,		/* device claims audio class, but isn't */
	UQ_BROKEN_BIDIR,	/* printer has broken bidir mode */
	UQ_BUS_POWERED,		/* device is bus powered, despite claim */
	UQ_HID_IGNORE,		/* device should be ignored by hid class */
	UQ_KBD_IGNORE,		/* device should be ignored by kbd class */
	UQ_KBD_BOOTPROTO,	/* device should set the boot protocol */
	UQ_UMS_IGNORE,          /* device should be ignored by ums class */
	UQ_MS_BAD_CLASS,	/* doesn't identify properly */
	UQ_MS_LEADING_BYTE,	/* mouse sends an unknown leading byte */
	UQ_MS_REVZ,		/* mouse has Z-axis reversed */
	UQ_NO_STRINGS,		/* string descriptors are broken */
	UQ_POWER_CLAIM,		/* hub lies about power status */
	UQ_SPUR_BUT_UP,		/* spurious mouse button up events */
	UQ_SWAP_UNICODE,	/* has some Unicode strings swapped */
	UQ_CFG_INDEX_1,		/* select configuration index 1 by default */
	UQ_CFG_INDEX_2,		/* select configuration index 2 by default */
	UQ_CFG_INDEX_3,		/* select configuration index 3 by default */
	UQ_CFG_INDEX_4,		/* select configuration index 4 by default */
	UQ_CFG_INDEX_0,		/* select configuration index 0 by default */
	UQ_ASSUME_CM_OVER_DATA,	/* assume cm over data feature */

	/*
	 * USB Mass Storage Quirks. See "storage/umass.c" for a
	 * detailed description.
	 */
	UQ_MSC_NO_TEST_UNIT_READY,	/* send start/stop instead of TUR */
	UQ_MSC_NO_RS_CLEAR_UA,		/* does not reset Unit Att. */
	UQ_MSC_NO_START_STOP,		/* does not support start/stop */
	UQ_MSC_NO_GETMAXLUN,		/* does not support get max LUN */
	UQ_MSC_NO_INQUIRY,		/* fake generic inq response */
	UQ_MSC_NO_INQUIRY_EVPD,		/* does not support inq EVPD */
	UQ_MSC_NO_PREVENT_ALLOW,	/* does not support medium removal */ 
	UQ_MSC_NO_SYNC_CACHE,		/* does not support sync cache */ 
	UQ_MSC_SHUTTLE_INIT,		/* requires Shuttle init sequence */
	UQ_MSC_ALT_IFACE_1,		/* switch to alternate interface 1 */
	UQ_MSC_FLOPPY_SPEED,		/* does floppy speeds (20kb/s) */
	UQ_MSC_IGNORE_RESIDUE,		/* gets residue wrong */
	UQ_MSC_WRONG_CSWSIG,		/* uses wrong CSW signature */
	UQ_MSC_RBC_PAD_TO_12,		/* pad RBC requests to 12 bytes */
	UQ_MSC_READ_CAP_OFFBY1,		/* reports sector count, not max sec. */
	UQ_MSC_FORCE_SHORT_INQ,		/* does not support full inq. */
	UQ_MSC_FORCE_WIRE_BBB,		/* force BBB wire protocol */
	UQ_MSC_FORCE_WIRE_CBI,		/* force CBI wire protocol */
	UQ_MSC_FORCE_WIRE_CBI_I,	/* force CBI with int. wire protocol */
	UQ_MSC_FORCE_PROTO_SCSI,	/* force SCSI command protocol */
	UQ_MSC_FORCE_PROTO_ATAPI,	/* force ATAPI command protocol */
	UQ_MSC_FORCE_PROTO_UFI,		/* force UFI command protocol */
	UQ_MSC_FORCE_PROTO_RBC,		/* force RBC command protocol */

	/* Ejection of mass storage (driver disk) */
	UQ_MSC_EJECT_HUAWEI,		/* ejects after Huawei USB command */
	UQ_MSC_EJECT_SIERRA,		/* ejects after Sierra USB command */
	UQ_MSC_EJECT_SCSIEJECT,		/* ejects after SCSI eject command */
	UQ_MSC_EJECT_REZERO,		/* ejects after SCSI rezero command */
	UQ_MSC_EJECT_ZTESTOR,		/* ejects after ZTE SCSI command */
	UQ_MSC_EJECT_CMOTECH,		/* ejects after C-motech SCSI cmd */
	UQ_MSC_EJECT_WAIT,		/* wait for the device to eject */
	UQ_MSC_EJECT_SAEL_M460,		/* ejects after Sael USB commands */ 
	UQ_MSC_EJECT_HUAWEISCSI,	/* ejects after Huawei SCSI command */
	UQ_MSC_EJECT_HUAWEISCSI2,	/* ejects after Huawei SCSI 2 command */
	UQ_MSC_EJECT_TCT,		/* ejects after TCT SCSI command */

	UQ_BAD_MIDI,		/* device claims MIDI class, but isn't */
	UQ_AU_VENDOR_CLASS,	/* audio device uses vendor and not audio class */
	UQ_SINGLE_CMD_MIDI,	/* at most one command per USB packet */
	UQ_MSC_DYMO_EJECT,	/* ejects Dymo MSC device */
	UQ_AU_SET_SPDIF_CM6206,	/* enable S/PDIF audio output */
	UQ_WMT_IGNORE,          /* device should be ignored by wmt driver */

	USB_QUIRK_MAX
};

uint8_t	usb_test_quirk(const struct usb_attach_arg *uaa, uint16_t quirk);

#endif					/* _USB_QUIRK_H_ */
