/*	$OpenBSD: usbhid.h,v 1.21 2016/01/09 04:10:36 jcs Exp $ */
/*	$NetBSD: usbhid.h,v 1.11 2001/12/28 00:20:24 augustss Exp $	*/
/*	$FreeBSD: src/sys/dev/usb/usbhid.h,v 1.7 1999/11/17 22:33:51 n_hibma Exp $ */

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


#ifndef _USBHID_H_
#define _USBHID_H_

#include <dev/hid/hid.h>

#define UR_GET_HID_DESCRIPTOR	0x06
#define  UDESC_HID		0x21
#define  UDESC_REPORT		0x22
#define  UDESC_PHYSICAL		0x23
#define UR_SET_HID_DESCRIPTOR	0x07
#define UR_GET_REPORT		0x01
#define UR_SET_REPORT		0x09
#define UR_GET_IDLE		0x02
#define UR_SET_IDLE		0x0a
#define UR_GET_PROTOCOL		0x03
#define UR_SET_PROTOCOL		0x0b

struct usb_hid_descriptor {
	uByte		bLength;
	uByte		bDescriptorType;
	uWord		bcdHID;
	uByte		bCountryCode;
	uByte		bNumDescriptors;
	struct {
		uByte		bDescriptorType;
		uWord		wDescriptorLength;
	} descrs[1];
} __packed;
#define USB_HID_DESCRIPTOR_SIZE(n) (9+(n)*3)

#define UHID_INPUT_REPORT	0x01
#define UHID_OUTPUT_REPORT	0x02
#define UHID_FEATURE_REPORT	0x03

#endif /* _USBHID_H_ */
