/*	$OpenBSD: uhcireg.h,v 1.16 2016/12/27 14:41:45 kettenis Exp $ */
/*	$NetBSD: uhcireg.h,v 1.16 2002/07/11 21:14:29 augustss Exp $	*/
/*	$FreeBSD: src/sys/dev/usb/uhcireg.h,v 1.12 1999/11/17 22:33:42 n_hibma Exp $ */

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

#ifndef _DEV_PCI_UHCIREG_H_
#define _DEV_PCI_UHCIREG_H_

/*** PCI config registers ***/

#define PCI_USBREV		0x60	/* USB protocol revision */
#define  PCI_USBREV_MASK	0xff
#define  PCI_USBREV_PRE_1_0	0x00
#define  PCI_USBREV_1_0		0x10
#define  PCI_USBREV_1_1		0x11

#define PCI_LEGSUP		0xc0	/* Legacy Support register */
#define  PCI_LEGSUP_USBPIRQDEN	0x2000	/* USB PIRQ D Enable */

#define PCI_CBIO		0x20	/* configuration base IO */

#define PCI_INTERFACE_UHCI	0x00

/*** UHCI registers ***/

#define UHCI_CMD		0x00
#define  UHCI_CMD_RS		0x0001
#define  UHCI_CMD_HCRESET	0x0002
#define  UHCI_CMD_GRESET	0x0004
#define  UHCI_CMD_EGSM		0x0008
#define  UHCI_CMD_FGR		0x0010
#define  UHCI_CMD_SWDBG		0x0020
#define  UHCI_CMD_CF		0x0040
#define  UHCI_CMD_MAXP		0x0080

#define UHCI_STS		0x02
#define  UHCI_STS_USBINT	0x0001
#define  UHCI_STS_USBEI		0x0002
#define  UHCI_STS_RD		0x0004
#define  UHCI_STS_HSE		0x0008
#define  UHCI_STS_HCPE		0x0010
#define  UHCI_STS_HCH		0x0020
#define  UHCI_STS_ALLINTRS	0x003f

#define UHCI_INTR		0x04
#define  UHCI_INTR_TOCRCIE	0x0001
#define  UHCI_INTR_RIE		0x0002
#define  UHCI_INTR_IOCE		0x0004
#define  UHCI_INTR_SPIE		0x0008

#define UHCI_FRNUM		0x06
#define  UHCI_FRNUM_MASK	0x03ff


#define UHCI_FLBASEADDR		0x08

#define UHCI_SOF		0x0c
#define  UHCI_SOF_MASK		0x7f

#define UHCI_PORTSC1      	0x010
#define UHCI_PORTSC2      	0x012
#define UHCI_PORTSC_CCS		0x0001
#define UHCI_PORTSC_CSC		0x0002
#define UHCI_PORTSC_PE		0x0004
#define UHCI_PORTSC_POEDC	0x0008
#define UHCI_PORTSC_LS		0x0030
#define UHCI_PORTSC_LS_SHIFT	4
#define UHCI_PORTSC_RD		0x0040
#define UHCI_PORTSC_LSDA	0x0100
#define UHCI_PORTSC_PR		0x0200
#define UHCI_PORTSC_OCI		0x0400
#define UHCI_PORTSC_OCIC	0x0800
#define UHCI_PORTSC_SUSP	0x1000

#define URWMASK(x) \
  ((x) & (UHCI_PORTSC_SUSP | UHCI_PORTSC_PR | UHCI_PORTSC_RD | UHCI_PORTSC_PE))

#define UHCI_FRAMELIST_COUNT	1024
#define UHCI_FRAMELIST_ALIGN	4096

#define UHCI_TD_ALIGN		16
#define UHCI_QH_ALIGN		16

typedef u_int32_t uhci_physaddr_t;
#define UHCI_PTR_T		0x00000001
#define UHCI_PTR_TD		0x00000000
#define UHCI_PTR_QH		0x00000002
#define UHCI_PTR_VF		0x00000004

/*
 * Wait this long after a QH has been removed.  This gives that HC a
 * chance to stop looking at it before it's recycled.
 */
#define UHCI_QH_REMOVE_DELAY	5

/*
 * The Queue Heads and Transfer Descriptors are accessed
 * by both the CPU and the USB controller which run
 * concurrently.  This means that they have to be accessed
 * with great care.  As long as the data structures are
 * not linked into the controller's frame list they cannot
 * be accessed by it and anything goes.  As soon as a
 * TD is accessible by the controller it "owns" the td_status
 * field; it will not be written by the CPU.  Similarly
 * the controller "owns" the qh_elink field.
 */

struct uhci_td {
	uhci_physaddr_t td_link;
	u_int32_t td_status;
#define UHCI_TD_GET_ACTLEN(s)	(((s) + 1) & 0x3ff)
#define UHCI_TD_ZERO_ACTLEN(t)	((t) | 0x3ff)
#define UHCI_TD_BITSTUFF	0x00020000
#define UHCI_TD_CRCTO		0x00040000
#define UHCI_TD_NAK		0x00080000
#define UHCI_TD_BABBLE		0x00100000
#define UHCI_TD_DBUFFER		0x00200000
#define UHCI_TD_STALLED		0x00400000
#define UHCI_TD_ACTIVE		0x00800000
#define UHCI_TD_IOC		0x01000000
#define UHCI_TD_IOS		0x02000000
#define UHCI_TD_LS		0x04000000
#define UHCI_TD_GET_ERRCNT(s)	(((s) >> 27) & 3)
#define UHCI_TD_SET_ERRCNT(n)	((n) << 27)
#define UHCI_TD_SPD		0x20000000
	u_int32_t td_token;
#define UHCI_TD_PID_IN		0x00000069
#define UHCI_TD_PID_OUT		0x000000e1
#define UHCI_TD_PID_SETUP	0x0000002d
#define UHCI_TD_GET_PID(s)	((s) & 0xff)
#define UHCI_TD_SET_DEVADDR(a)	((a) << 8)
#define UHCI_TD_GET_DEVADDR(s)	(((s) >> 8) & 0x7f)
#define UHCI_TD_SET_ENDPT(e)	(((e)&0xf) << 15)
#define UHCI_TD_GET_ENDPT(s)	(((s) >> 15) & 0xf)
#define UHCI_TD_SET_DT(t)	((t) << 19)
#define UHCI_TD_GET_DT(s)	(((s) >> 19) & 1)
#define UHCI_TD_SET_MAXLEN(l)	(((uint32_t)(l)-1) << 21)
#define UHCI_TD_GET_MAXLEN(s)	((((s) >> 21) + 1) & 0x7ff)
#define UHCI_TD_MAXLEN_MASK	0xffe00000
	u_int32_t td_buffer;
};

#define UHCI_TD_ERROR (UHCI_TD_BITSTUFF|UHCI_TD_CRCTO|UHCI_TD_BABBLE|UHCI_TD_DBUFFER|UHCI_TD_STALLED)

#define UHCI_TD_SETUP(len, endp, dev) (UHCI_TD_SET_MAXLEN(len) | \
     UHCI_TD_SET_ENDPT(endp) | UHCI_TD_SET_DEVADDR(dev) | UHCI_TD_PID_SETUP)
#define UHCI_TD_OUT(len, endp, dev, dt) (UHCI_TD_SET_MAXLEN(len) | \
     UHCI_TD_SET_ENDPT(endp) | UHCI_TD_SET_DEVADDR(dev) | \
     UHCI_TD_PID_OUT | UHCI_TD_SET_DT(dt))
#define UHCI_TD_IN(len, endp, dev, dt) (UHCI_TD_SET_MAXLEN(len) | \
     UHCI_TD_SET_ENDPT(endp) | UHCI_TD_SET_DEVADDR(dev) | UHCI_TD_PID_IN | \
     UHCI_TD_SET_DT(dt))

struct uhci_qh {
	uhci_physaddr_t qh_hlink;
	uhci_physaddr_t qh_elink;
};

#endif /* _DEV_PCI_UHCIREG_H_ */
