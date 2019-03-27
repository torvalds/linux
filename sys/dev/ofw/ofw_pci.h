/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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
 *
 *	from: NetBSD: ofw_pci.h,v 1.5 2003/10/22 09:04:39 mjl Exp
 *
 * $FreeBSD$
 */

#ifndef _DEV_OFW_OFW_PCI_H_
#define	_DEV_OFW_OFW_PCI_H_

/*
 * PCI Bus Binding to:
 *
 * IEEE Std 1275-1994
 * Standard for Boot (Initialization Configuration) Firmware
 *
 * Revision 2.1
 */

/*
 * Section 2.2.1. Physical Address Formats
 *
 * A PCI physical address is represented by 3 address cells:
 *
 *	phys.hi cell:	npt000ss bbbbbbbb dddddfff rrrrrrrr
 *	phys.mid cell:	hhhhhhhh hhhhhhhh hhhhhhhh hhhhhhhh
 *	phys.lo cell:	llllllll llllllll llllllll llllllll
 *
 *	n	nonrelocatable
 *	p	prefetchable
 *	t	aliased below 1MB (memory) or 64k (i/o)
 *	ss	space code
 *	b	bus number
 *	d	device number
 *	f	function number
 *	r	register number
 *	h	high 32-bits of PCI address
 *	l	low 32-bits of PCI address
 */

#define	OFW_PCI_PHYS_HI_NONRELOCATABLE	0x80000000
#define	OFW_PCI_PHYS_HI_PREFETCHABLE	0x40000000
#define	OFW_PCI_PHYS_HI_ALIASED		0x20000000
#define	OFW_PCI_PHYS_HI_SPACEMASK	0x03000000
#define	OFW_PCI_PHYS_HI_BUSMASK		0x00ff0000
#define	OFW_PCI_PHYS_HI_BUSSHIFT	16
#define	OFW_PCI_PHYS_HI_DEVICEMASK	0x0000f800
#define	OFW_PCI_PHYS_HI_DEVICESHIFT	11
#define	OFW_PCI_PHYS_HI_FUNCTIONMASK	0x00000700
#define	OFW_PCI_PHYS_HI_FUNCTIONSHIFT	8
#define	OFW_PCI_PHYS_HI_REGISTERMASK	0x000000ff

#define	OFW_PCI_PHYS_HI_SPACE_CONFIG	0x00000000
#define	OFW_PCI_PHYS_HI_SPACE_IO	0x01000000
#define	OFW_PCI_PHYS_HI_SPACE_MEM32	0x02000000
#define	OFW_PCI_PHYS_HI_SPACE_MEM64	0x03000000

#define OFW_PCI_PHYS_HI_BUS(hi) \
	(((hi) & OFW_PCI_PHYS_HI_BUSMASK) >> OFW_PCI_PHYS_HI_BUSSHIFT)
#define OFW_PCI_PHYS_HI_DEVICE(hi) \
	(((hi) & OFW_PCI_PHYS_HI_DEVICEMASK) >> OFW_PCI_PHYS_HI_DEVICESHIFT)
#define OFW_PCI_PHYS_HI_FUNCTION(hi) \
	(((hi) & OFW_PCI_PHYS_HI_FUNCTIONMASK) >> OFW_PCI_PHYS_HI_FUNCTIONSHIFT)

/*
 * This has the 3 32bit cell values, plus 2 more to make up a 64-bit size.
 */
struct ofw_pci_register {
	u_int32_t	phys_hi;
	u_int32_t	phys_mid;
	u_int32_t	phys_lo;
	u_int32_t	size_hi;
	u_int32_t	size_lo;
};

#endif /* _DEV_OFW_OFW_PCI_H_ */
