/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)sbusreg.h	8.1 (Berkeley) 6/11/93
 *	from: NetBSD: iommureg.h,v 1.6 2001/07/20 00:07:13 eeh Exp
 *
 * $FreeBSD$
 */

#ifndef _MACHINE_IOMMUREG_H_
#define	_MACHINE_IOMMUREG_H_

/*
 * UltraSPARC IOMMU registers, common to both the PCI and SBus
 * controllers.
 */

/* IOMMU registers */
#define	IMR_CTL		0x0000	/* IOMMU control register */
#define	IMR_TSB		0x0008	/* IOMMU TSB base register */
#define	IMR_FLUSH	0x0010	/* IOMMU flush register */
/* The TTE Cache is Fire and Oberon only. */
#define	IMR_CACHE_FLUSH	0x0100	/* IOMMU TTE cache flush address register */
#define	IMR_CACHE_INVAL	0x0108	/* IOMMU TTE cache invalidate register */

/* streaming buffer registers */
#define	ISR_CTL		0x0000	/* streaming buffer control reg */
#define	ISR_PGFLUSH	0x0008	/* streaming buffer page flush */
#define	ISR_FLUSHSYNC	0x0010	/* streaming buffer flush sync */

/* streaming buffer diagnostics registers */
#define	ISD_DATA_DIAG	0x0000	/* streaming buffer data RAM diag 0..127 */
#define	ISD_ERROR_DIAG	0x0400	/* streaming buffer error status diag 0..127 */
#define	ISD_PG_TAG_DIAG	0x0800	/* streaming buffer page tag diag 0..15 */
#define	ISD_LN_TAG_DIAG	0x0900	/* streaming buffer line tag diag 0..15 */

/* streaming buffer control register */
#define	STRBUF_EN		0x0000000000000001UL
#define	STRBUF_D		0x0000000000000002UL
#define	STRBUF_RR_DIS		0x0000000000000004UL

#define	IOMMU_MAXADDR(bits)	((1UL << (bits)) - 1)

/*
 * control register bits
 */
/* Nummber of entries in the IOTSB - pre-Fire only */
#define	IOMMUCR_TSBSZ_MASK	0x0000000000070000UL
#define	IOMMUCR_TSBSZ_SHIFT	16
/* TSB cache snoop enable */
#define	IOMMUCR_SE		0x0000000000000400UL
/* Cache modes - Fire and Oberon */
#define	IOMMUCR_CM_NC_TLB_TBW	0x0000000000000000UL
#define	IOMMUCR_CM_LC_NTLB_NTBW	0x0000000000000100UL
#define	IOMMUCR_CM_LC_TLB_TBW	0x0000000000000200UL
#define	IOMMUCR_CM_C_TLB_TBW	0x0000000000000300UL
/* IOMMU page size - pre-Fire only */
#define	IOMMUCR_8KPG		0x0000000000000000UL
#define	IOMMUCR_64KPG		0x0000000000000004UL
/* Bypass enable - Fire and Oberon */
#define	IOMMUCR_BE		0x0000000000000002UL
/* Diagnostic mode enable - pre-Fire only */
#define	IOMMUCR_DE		0x0000000000000002UL
/* IOMMU/translation enable */
#define	IOMMUCR_EN		0x0000000000000001UL

/*
 * TSB base register bits
 */
 /* TSB base address */
#define	IOMMUTB_TB_MASK		0x000007ffffffe000UL
#define	IOMMUTB_TB_SHIFT	13
/* IOMMU page size - Fire and Oberon */
#define	IOMMUTB_8KPG		0x0000000000000000UL
#define	IOMMUTB_64KPG		0x0000000000000100UL
/* Nummber of entries in the IOTSB - Fire and Oberon */
#define	IOMMUTB_TSBSZ_MASK	0x0000000000000004UL
#define	IOMMUTB_TSBSZ_SHIFT	0

/*
 * TSB size definitions for both control and TSB base register */
#define	IOMMU_TSB1K		0
#define	IOMMU_TSB2K		1
#define	IOMMU_TSB4K		2
#define	IOMMU_TSB8K		3
#define	IOMMU_TSB16K		4
#define	IOMMU_TSB32K		5
#define	IOMMU_TSB64K		6
#define	IOMMU_TSB128K		7
/* Fire and Oberon */
#define	IOMMU_TSB256K		8
/* Fire and Oberon */
#define	IOMMU_TSB512K		9
#define	IOMMU_TSBENTRIES(tsbsz)						\
	((1 << (tsbsz)) << (IO_PAGE_SHIFT - IOTTE_SHIFT))

/*
 * Diagnostic register definitions
 */
#define	IOMMU_DTAG_VPNBITS	19
#define	IOMMU_DTAG_VPNMASK	((1 << IOMMU_DTAG_VPNBITS) - 1)
#define	IOMMU_DTAG_VPNSHIFT	13
#define	IOMMU_DTAG_ERRBITS	3
#define	IOMMU_DTAG_ERRSHIFT	22
#define	IOMMU_DTAG_ERRMASK						\
	(((1 << IOMMU_DTAG_ERRBITS) - 1) << IOMMU_DTAG_ERRSHIFT)

#define	IOMMU_DDATA_PGBITS	21
#define	IOMMU_DDATA_PGMASK	((1 << IOMMU_DDATA_PGBITS) - 1)
#define	IOMMU_DDATA_PGSHIFT	13
#define	IOMMU_DDATA_C		(1 << 28)
#define	IOMMU_DDATA_V		(1 << 30)

/*
 * IOMMU stuff
 */
/* Entry valid */
#define	IOTTE_V			0x8000000000000000UL
/* Page size - pre-Fire only */
#define	IOTTE_64K		0x2000000000000000UL
#define	IOTTE_8K		0x0000000000000000UL
/* Streamable page - streaming buffer equipped variants only */
#define	IOTTE_STREAM		0x1000000000000000UL
/* Accesses to the same bus segment - SBus only */
#define	IOTTE_LOCAL		0x0800000000000000UL
/* Physical address mask (based on Oberon) */
#define	IOTTE_PAMASK		0x00007fffffffe000UL
/* Accesses to cacheable space - pre-Fire only */
#define	IOTTE_C			0x0000000000000010UL
/* Writeable */
#define	IOTTE_W			0x0000000000000002UL

/* log2 of the IOMMU TTE size */
#define	IOTTE_SHIFT		3

/* Streaming buffer line size */
#define	STRBUF_LINESZ		64

/*
 * Number of bytes written by a stream buffer flushsync operation to indicate
 * completion.
 */
#define	STRBUF_FLUSHSYNC_NBYTES	STRBUF_LINESZ

/*
 * On sun4u each bus controller has a separate IOMMU.  The IOMMU has
 * a TSB which must be page aligned and physically contiguous.  Mappings
 * can be of 8K IOMMU pages or 64K IOMMU pages.  We use 8K for compatibility
 * with the CPU's MMU.
 *
 * On sysio, psycho, and psycho+, IOMMU TSBs using 8K pages can map the
 * following size segments:
 *
 *	VA size		VA base		TSB size	tsbsize
 *	--------	--------	---------	-------
 *	8MB		ff800000	8K		0
 *	16MB		ff000000	16K		1
 *	32MB		fe000000	32K		2
 *	64MB		fc000000	64K		3
 *	128MB		f8000000	128K		4
 *	256MB		f0000000	256K		5
 *	512MB		e0000000	512K		6
 *	1GB		c0000000	1MB		7
 *
 * Unfortunately, sabres on UltraSPARC IIi and IIe processors does not use
 * this scheme to determine the IOVA base address.  Instead, bits 31-29 are
 * used to check against the Target Address Space register in the IIi and
 * the IOMMU is used if they hit.  God knows what goes on in the IIe.
 *
 */

#define	IOTSB_BASESZ		(1024 << IOTTE_SHIFT)
#define	IOTSB_VEND		(~IO_PAGE_MASK)
#define	IOTSB_VSTART(sz)	(u_int)(IOTSB_VEND << ((sz) + 10))

#define	MAKEIOTTE(pa, w, c, s)						\
	(((pa) & IOTTE_PAMASK) | ((w) ? IOTTE_W : 0) |			\
	((c) ? IOTTE_C : 0) | ((s) ? IOTTE_STREAM : 0) |		\
	(IOTTE_V | IOTTE_8K))
#define	IOTSBSLOT(va)							\
	((u_int)(((vm_offset_t)(va)) - (is->is_dvmabase)) >> IO_PAGE_SHIFT)

#endif /* !_MACHINE_IOMMUREG_H_ */
