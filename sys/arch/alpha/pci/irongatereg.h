/*	$OpenBSD: irongatereg.h,v 1.3 2008/06/26 05:42:08 ray Exp $	*/
/* $NetBSD: irongatereg.h,v 1.2 2000/06/26 02:42:10 thorpej Exp $ */

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
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
 * Register definitions for the AMD 751 (``Irongate'') core logic
 * chipset.
 */

/*
 * Address map.
 *
 * This from the Tsunami address map:
 * EV6 has a new superpage which can pass through 44 address bits.  (Umm, a
 * superduperpage?) But, the firmware doesn't turn it on, so we use the old
 * one and let the HW sign extend va/pa<40> to get us the pa<43> that makes
 * the needed I/O space access. This is just as well; it means we don't have
 * to worry about which GENERIC code might get called on other CPU models.
 *
 *	E.g., we want this:		0x0801##fc00##0000
 *	We use this:			0x0101##fc00##0000
 *	...mix in the old SP:     0xffff##fc00##0000##0000
 *	...after PA sign ext:     0xffff##ff01##fc00##0000
 *	(PA<42:41> ignored)
 *
 * PCI memory and RAM:			0000.0000.0000
 * IACK					0001.f800.0000
 * PCI I/O:				0001.fc00.0000
 * AMD 751 (also in PCI config space):	0001.fe00.0000
 */

/*
 * This hack allows us to map the I/O address space without using
 * the KSEG sign extension hack.
 */
#define	IRONGATE_PHYSADDR(x)						\
	(((x) & ~0x0100##0000##0000) | 0x0800##0000##0000)

#define	IRONGATE_KSEG_BIAS	0x0100##0000##0000UL

#define	IRONGATE_MEM_BASE	(IRONGATE_KSEG_BIAS | 0x0000##0000##0000UL)
#define	IRONGATE_IACK_BASE	(IRONGATE_KSEG_BIAS | 0x0001##f800##0000UL)
#define	IRONGATE_IO_BASE	(IRONGATE_KSEG_BIAS | 0x0001##fc00##0000UL)
#define	IRONGATE_SELF_BASE	(IRONGATE_KSEG_BIAS | 0x0001##fe00##0000UL)

/*
 * PCI configuration register access using done by using
 * ``configuration mode 1'' (in PC lingo), using the I/O
 * space addresses described in the PCI Local Bus Specification
 * Revision 2.2.
 */
#define	IRONGATE_CONFADDR	0x0cf8
#define	IRONGATE_CONFDATA	0x0cfc

#define	CONFADDR_ENABLE		0x80000000U

/*
 * The AMD 751 PCI-Host bridge is located at device 0, and the
 * AGP controller (seen as a PCI-PCI bridge) is at device 1.
 */
#define	IRONGATE_PCIHOST_DEV	0
#define	IRONGATE_PCIAGP_DEV	1
