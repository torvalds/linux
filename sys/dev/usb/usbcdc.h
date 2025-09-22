/*	$OpenBSD: usbcdc.h,v 1.10 2022/01/09 05:43:02 jsg Exp $ */
/*	$NetBSD: usbcdc.h,v 1.8 2001/02/16 20:15:57 kenh Exp $	*/
/*	$FreeBSD: src/sys/dev/usb/usbcdc.h,v 1.7 1999/11/17 22:33:48 n_hibma Exp $	*/

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

#ifndef _USBCDC_H_
#define _USBCDC_H_

#define UDESCSUB_CDC_HEADER	0
#define UDESCSUB_CDC_CM		1 /* Call Management */
#define UDESCSUB_CDC_ACM	2 /* Abstract Control Model */
#define UDESCSUB_CDC_DLM	3 /* Direct Line Management */
#define UDESCSUB_CDC_TRF	4 /* Telephone Ringer */
#define UDESCSUB_CDC_TCLSR	5 /* Telephone Call ... */
#define UDESCSUB_CDC_UNION	6
#define UDESCSUB_CDC_CS		7 /* Country Selection */
#define UDESCSUB_CDC_TOM	8 /* Telephone Operational Modes */
#define UDESCSUB_CDC_USBT	9 /* USB Terminal */
#define UDESCSUB_CDC_NCT	10 /* Network Channel Terminal */
#define UDESCSUB_CDC_PUF	11 /* Protocol Unit */
#define UDESCSUB_CDC_EUF	12 /* Extension Unit */
#define UDESCSUB_CDC_MCMF	13 /* Multi-Channel Management */
#define UDESCSUB_CDC_CCMF	14 /* CAPI Control Management */
#define UDESCSUB_CDC_ENF	15 /* Ethernet Networking */
#define UDESCSUB_CDC_ANF	16 /* ATM Networking */

struct usb_cdc_header_descriptor {
	uByte		bLength;
	uByte		bDescriptorType;
	uByte		bDescriptorSubtype;
	uWord		bcdCDC;
} __packed;

struct usb_cdc_cm_descriptor {
	uByte		bLength;
	uByte		bDescriptorType;
	uByte		bDescriptorSubtype;
	uByte		bmCapabilities;
#define USB_CDC_CM_DOES_CM		0x01
#define USB_CDC_CM_OVER_DATA		0x02
	uByte		bDataInterface;
} __packed;

struct usb_cdc_acm_descriptor {
	uByte		bLength;
	uByte		bDescriptorType;
	uByte		bDescriptorSubtype;
	uByte		bmCapabilities;
#define USB_CDC_ACM_HAS_FEATURE		0x01
#define USB_CDC_ACM_HAS_LINE		0x02
#define USB_CDC_ACM_HAS_BREAK		0x04
#define USB_CDC_ACM_HAS_NETWORK_CONN	0x08
} __packed;

struct usb_cdc_union_descriptor {
	uByte		bLength;
	uByte		bDescriptorType;
	uByte		bDescriptorSubtype;
	uByte		bMasterInterface;
	uByte		bSlaveInterface[1];
} __packed;

struct usb_cdc_ethernet_descriptor {
	uByte		bLength;
	uByte		bDescriptorType;
	uByte		bDescriptorSubtype;
	uByte		iMacAddress;
	uDWord		bmEthernetStatistics;
	uWord		wMaxSegmentSize;
	uWord		wNumberMCFilters;
	uByte		bNumberPowerFilters;
} __packed;

#define UCDC_SEND_ENCAPSULATED_COMMAND	0x00
#define UCDC_GET_ENCAPSULATED_RESPONSE	0x01
#define UCDC_SET_COMM_FEATURE		0x02
#define UCDC_GET_COMM_FEATURE		0x03
#define  UCDC_ABSTRACT_STATE		0x01
#define  UCDC_COUNTRY_SETTING		0x02
#define UCDC_CLEAR_COMM_FEATURE		0x04
#define UCDC_SET_LINE_CODING		0x20
#define UCDC_GET_LINE_CODING		0x21
#define UCDC_SET_CONTROL_LINE_STATE	0x22
#define  UCDC_LINE_DTR			0x0001
#define  UCDC_LINE_RTS			0x0002
#define UCDC_SEND_BREAK			0x23
#define  UCDC_BREAK_ON			0xffff
#define  UCDC_BREAK_OFF			0x0000

struct usb_cdc_abstract_state {
	uWord	wState;
#define UCDC_IDLE_SETTING		0x0001
#define UCDC_DATA_MULTIPLEXED		0x0002
} __packed;
#define UCDC_ABSTRACT_STATE_LENGTH 2

struct usb_cdc_line_state {
	uDWord	dwDTERate;
	uByte	bCharFormat;
#define UCDC_STOP_BIT_1			0
#define UCDC_STOP_BIT_1_5		1
#define UCDC_STOP_BIT_2			2
	uByte	bParityType;
#define UCDC_PARITY_NONE		0
#define UCDC_PARITY_ODD			1
#define UCDC_PARITY_EVEN		2
#define UCDC_PARITY_MARK		3
#define UCDC_PARITY_SPACE		4
	uByte	bDataBits;
} __packed;
#define UCDC_LINE_STATE_LENGTH 7

struct usb_cdc_notification {
	uByte	bmRequestType;
#define UCDC_NOTIFICATION		0xa1
	uByte	bNotification;
#define UCDC_N_NETWORK_CONNECTION	0x00
#define UCDC_N_RESPONSE_AVAILABLE	0x01
#define UCDC_N_AUX_JACK_HOOK_STATE	0x08
#define UCDC_N_RING_DETECT		0x09
#define UCDC_N_SERIAL_STATE		0x20
#define UCDC_N_CALL_STATE_CHANGED	0x28
#define UCDC_N_LINE_STATE_CHANGED	0x29
#define UCDC_N_CONNECTION_SPEED_CHANGE	0x2a
	uWord	wValue;
	uWord	wIndex;
	uWord	wLength;
	uByte	data[16];
} __packed;
#define UCDC_NOTIFICATION_LENGTH 8

struct usb_cdc_connection_speed {
	uDWord	dwUSBitRate;
	uDWord	dwDSBitRate;
} __packed;
#define UCDC_CONNECTION_SPEED_LENGTH 8

/*
 * Bits set in the SERIAL STATE notification (first byte of data)
 */

#define UCDC_N_SERIAL_OVERRUN		0x40
#define UCDC_N_SERIAL_PARITY		0x20
#define UCDC_N_SERIAL_FRAMING		0x10
#define UCDC_N_SERIAL_RI		0x08
#define UCDC_N_SERIAL_BREAK		0x04
#define UCDC_N_SERIAL_DSR		0x02
#define UCDC_N_SERIAL_DCD		0x01

#endif /* _USBCDC_H_ */
