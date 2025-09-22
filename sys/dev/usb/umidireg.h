/*	$OpenBSD: umidireg.h,v 1.8 2013/04/15 09:23:02 mglocker Exp $	*/
/*	$NetBSD: umidireg.h,v 1.3 2003/12/04 13:57:31 keihan Exp $	*/
/*
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Takuya SHIOZAKI (tshiozak@NetBSD.org).
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

/* Jack Descriptor */
#define UMIDI_MS_HEADER	0x01
#define UMIDI_IN_JACK	0x02
#define UMIDI_OUT_JACK	0x03

/* Jack Type */
#define UMIDI_EMBEDDED	0x01
#define UMIDI_EXTERNAL	0x02

struct umidi_cs_interface_descriptor {
	uByte		bLength;
	uByte		bDescriptorType;
	uByte		bDescriptorSubtype;
	uWord		bcdMSC;
	uWord		wTotalLength;
} __packed;
#define UMIDI_CS_INTERFACE_DESCRIPTOR_SIZE 7

struct umidi_cs_endpoint_descriptor {
	uByte		bLength;
	uByte		bDescriptorType;
	uByte		bDescriptorSubType;
	uByte		bNumEmbMIDIJack;
} __packed;
#define UMIDI_CS_ENDPOINT_DESCRIPTOR_SIZE 4

struct umidi_jack_descriptor {
	uByte		bLength;
	uByte		bDescriptorType;
	uByte		bDescriptorSubtype;
	uByte		bJackType;
	uByte		bJackID;
} __packed;
#define	UMIDI_JACK_DESCRIPTOR_SIZE	5


#define TO_D(p) ((usb_descriptor_t *)(p))
#define NEXT_D(desc) TO_D((caddr_t)(desc)+(desc)->bLength)
#define TO_IFD(desc) ((usb_interface_descriptor_t *)(desc))
#define TO_CSIFD(desc) ((struct umidi_cs_interface_descriptor *)(desc))
#define TO_EPD(desc) ((usb_endpoint_descriptor_t *)(desc))
#define TO_CSEPD(desc) ((struct umidi_cs_endpoint_descriptor *)(desc))
