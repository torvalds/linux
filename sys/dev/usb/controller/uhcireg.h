/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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

#ifndef _UHCIREG_H_
#define	_UHCIREG_H_

#define	PCI_UHCI_BASE_REG	0x20

/* PCI config registers  */
#define	PCI_USBREV		0x60	/* USB protocol revision */
#define	PCI_USB_REV_MASK		0xff
#define	PCI_USB_REV_PRE_1_0	0x00
#define	PCI_USB_REV_1_0		0x10
#define	PCI_USB_REV_1_1		0x11
#define	PCI_LEGSUP		0xc0	/* Legacy Support register */
#define	PCI_LEGSUP_USBPIRQDEN	0x2000	/* USB PIRQ D Enable */
#define	PCI_CBIO		0x20	/* configuration base IO */
#define	PCI_INTERFACE_UHCI	0x00

/* UHCI registers */
#define	UHCI_CMD		0x00
#define	UHCI_CMD_RS		0x0001
#define	UHCI_CMD_HCRESET	0x0002
#define	UHCI_CMD_GRESET		0x0004
#define	UHCI_CMD_EGSM		0x0008
#define	UHCI_CMD_FGR		0x0010
#define	UHCI_CMD_SWDBG		0x0020
#define	UHCI_CMD_CF		0x0040
#define	UHCI_CMD_MAXP		0x0080
#define	UHCI_STS		0x02
#define	UHCI_STS_USBINT		0x0001
#define	UHCI_STS_USBEI		0x0002
#define	UHCI_STS_RD		0x0004
#define	UHCI_STS_HSE		0x0008
#define	UHCI_STS_HCPE		0x0010
#define	UHCI_STS_HCH		0x0020
#define	UHCI_STS_ALLINTRS	0x003f
#define	UHCI_INTR		0x04
#define	UHCI_INTR_TOCRCIE	0x0001
#define	UHCI_INTR_RIE		0x0002
#define	UHCI_INTR_IOCE		0x0004
#define	UHCI_INTR_SPIE		0x0008
#define	UHCI_FRNUM		0x06
#define	UHCI_FRNUM_MASK		0x03ff
#define	UHCI_FLBASEADDR		0x08
#define	UHCI_SOF		0x0c
#define	UHCI_SOF_MASK		0x7f
#define	UHCI_PORTSC1      	0x010
#define	UHCI_PORTSC2      	0x012
#define	UHCI_PORTSC_CCS		0x0001
#define	UHCI_PORTSC_CSC		0x0002
#define	UHCI_PORTSC_PE		0x0004
#define	UHCI_PORTSC_POEDC	0x0008
#define	UHCI_PORTSC_LS		0x0030
#define	UHCI_PORTSC_LS_SHIFT	4
#define	UHCI_PORTSC_RD		0x0040
#define	UHCI_PORTSC_LSDA	0x0100
#define	UHCI_PORTSC_PR		0x0200
#define	UHCI_PORTSC_OCI		0x0400
#define	UHCI_PORTSC_OCIC	0x0800
#define	UHCI_PORTSC_SUSP	0x1000

#define	URWMASK(x)		((x) & (UHCI_PORTSC_SUSP |		\
				UHCI_PORTSC_PR | UHCI_PORTSC_RD |	\
				UHCI_PORTSC_PE))

#endif	/* _UHCIREG_H_ */
