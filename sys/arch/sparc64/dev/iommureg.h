/*	$OpenBSD: iommureg.h,v 1.21 2025/06/28 11:33:45 miod Exp $	*/
/*	$NetBSD: iommureg.h,v 1.6 2001/07/20 00:07:13 eeh Exp $	*/

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
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
 *	@(#)sbusreg.h	8.1 (Berkeley) 6/11/93
 */

#ifndef _SPARC64_DEV_IOMMUREG_H_
#define _SPARC64_DEV_IOMMUREG_H_

/*
 * UltraSPARC IOMMU registers, common to both the sbus and PCI
 * controllers.
 */

/* iommu registers */
struct iommureg {
	volatile u_int64_t	iommu_cr;	/* IOMMU control register */
	volatile u_int64_t	iommu_tsb;	/* IOMMU TSB base register */
	volatile u_int64_t	iommu_flush;	/* IOMMU flush register */
	volatile u_int64_t	iommu_ctxflush;
	volatile u_int64_t	iommu_reserved[28];
	volatile u_int64_t	iommu_cache_flush;
	volatile u_int64_t	iommu_cache_invalidate;
	volatile u_int64_t	iommu_reserved2[30];
};

/* streaming buffer registers */
struct iommu_strbuf {
	volatile u_int64_t	strbuf_ctl;	/* streaming buffer control reg */
	volatile u_int64_t	strbuf_pgflush;	/* streaming buffer page flush */
	volatile u_int64_t	strbuf_flushsync;/* streaming buffer flush sync */
};

#define IOMMUREG(x)     (offsetof(struct iommureg, x))
#define STRBUFREG(x)    (offsetof(struct iommu_strbuf, x))

/* streaming buffer control register */
#define STRBUF_EN		0x000000000000000001LL
#define STRBUF_D		0x000000000000000002LL

/* control register bits */
#define IOMMUCR_TSB1K		0x000000000000000000LL	/* Number of entries in IOTSB */
#define IOMMUCR_TSB2K		0x000000000000010000LL
#define IOMMUCR_TSB4K		0x000000000000020000LL
#define IOMMUCR_TSB8K		0x000000000000030000LL
#define IOMMUCR_TSB16K		0x000000000000040000LL
#define IOMMUCR_TSB32K		0x000000000000050000LL
#define IOMMUCR_TSB64K		0x000000000000060000LL
#define IOMMUCR_TSB128K		0x000000000000070000LL
#define IOMMUCR_TSBMASK		0xfffffffffffff8ffffLL	/* Mask for above */
#define IOMMUCR_8KPG		0x000000000000000000LL	/* 8K iommu page size */
#define IOMMUCR_64KPG		0x000000000000000004LL	/* 64K iommu page size */
#define IOMMUCR_DE		0x000000000000000002LL	/* Diag enable */
#define IOMMUCR_EN		0x000000000000000001LL	/* Enable IOMMU */

#define IOMMUCR_FIRE_PD		0x000000000000001000UL	/* Process disable */
#define IOMMUCR_FIRE_SE		0x000000000000000400UL	/* Snoop enable */
#define IOMMUCR_FIRE_CM_EN	0x000000000000000300UL  /* Cache mode enable */
#define IOMMUCR_FIRE_BE		0x000000000000000002UL	/* Bypass enable */
#define IOMMUCR_FIRE_TE		0x000000000000000001UL	/* Translation enabled */

/*
 * IOMMU stuff
 */
#define	IOTTE_V		0x8000000000000000LL	/* Entry valid */
#define IOTTE_64K	0x2000000000000000LL	/* 8K or 64K page? */
#define IOTTE_8K	0x0000000000000000LL
#define IOTTE_STREAM	0x1000000000000000LL	/* Is page streamable? */
#define	IOTTE_LOCAL	0x0800000000000000LL	/* Accesses to same bus segment? */
#define	IOTTE_CONTEXT	0x07ff800000000000LL	/* context number */
#define IOTTE_PAMASK	0x00007fffffffe000LL	/* Let's assume this is correct (bits 42..13) */
#define IOTTE_C		0x0000000000000010LL	/* Accesses to cacheable space */
#define IOTTE_W		0x0000000000000002LL	/* Writeable */
#define IOTTE_SOFTWARE	0x0000000000001f80LL	/* For software use (bits 12..7) */


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


#define IOTSB_VEND		0xffffffffU
#define IOTSB_VSTART(sz)	(u_int)(IOTSB_VEND << ((sz)+10+PGSHIFT)) 
#define IOTSB_VSIZE(sz)		(u_int)(1 << ((sz)+10+PGSHIFT))

#define MAKEIOTTE(pa,w,c,s)	(((pa)&IOTTE_PAMASK)|((w)?IOTTE_W:0)|((c)?IOTTE_C:0)|((s)?IOTTE_STREAM:0)|(IOTTE_V|IOTTE_8K))
#define IOTSBSLOT(va,sz)	((u_int)(((vaddr_t)(va))-(is->is_dvmabase))>>PGSHIFT)

#endif /* _SPARC64_DEV_IOMMUREG_H_ */
