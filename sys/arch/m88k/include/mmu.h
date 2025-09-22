/*	$OpenBSD: mmu.h,v 1.17 2025/08/11 14:22:55 miod Exp $ */

/*
 * This file bears almost no resemblance to the original m68k file,
 * so the following copyright notice is questionable, but we are
 * nice people.
 */

/*
 * Copyright (c) 1988 University of Utah.
 * Copyright (c) 1982, 1986, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
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
 * from: Utah $Hdr: pte.h 1.13 92/01/20$
 *
 *	@(#)pte.h	8.1 (Berkeley) 6/10/93
 */

#ifndef	_M88K_MMU_H_
#define	_M88K_MMU_H_

/*
 * Parameters which determine the 'geometry' of the m88K page tables in memory.
 */

#define SDT_BITS	10		/* M88K segment table size bits */
#define PDT_BITS	10		/* M88K page table size bits */
#define PG_BITS		PAGE_SHIFT	/* M88K hardware page size bits */

/*
 * Common fields for APR, SDT and PTE
 */

/* address frame */
#define	PG_FRAME	0xfffff000
#define	PG_SHIFT	PG_BITS
#define	PG_PFNUM(x)	(((x) & PG_FRAME) >> PG_SHIFT)

/* cache control bits */
#define	CACHE_DFL	0x00000000
#define	CACHE_INH	0x00000040	/* cache inhibit */
#define	CACHE_GLOBAL	0x00000080	/* global scope */
#define	CACHE_WT	0x00000200	/* write through */

#define	CACHE_MASK	(CACHE_INH | CACHE_GLOBAL | CACHE_WT)

/*
 * Area descriptors
 */

typedef	uint32_t	apr_t;

#define	APR_V		0x00000001	/* valid bit */

/*
 * 88200 PATC (TLB)
 */

#define PATC_ENTRIES	56

/*
 * Segment table entries
 */

typedef uint32_t	sdt_entry_t;

#define	SG_V		0x00000001
#define	SG_NV		0x00000000
#define	SG_PROT		0x00000004
#define	SG_RO		0x00000004
#define	SG_RW		0x00000000
#define	SG_SO		0x00000100

#define	SDT_VALID(sdt)	(*(sdt) & SG_V)
#define	SDT_SUP(sdt)	(*(sdt) & SG_SO)
#define	SDT_WP(sdt)	(*(sdt) & SG_PROT)

/*
 * Page table entries
 */

typedef uint32_t	pt_entry_t;

#define	PG_V		0x00000001
#define	PG_NV		0x00000000
#define	PG_PROT		0x00000004
#define	PG_U		0x00000008
#define	PG_M		0x00000010
#define	PG_M_U		0x00000018
#define	PG_RO		0x00000004
#define	PG_RW		0x00000000
#define	PG_SO		0x00000100
#define	PG_W		0x00000020	/* XXX unused but reserved field */
#define	PG_U0		0x00000400	/* U0 bit for M88110 */
#define	PG_U1		0x00000800	/* U1 bit for M88110 */

#define	PDT_VALID(pte)	(*(pte) & PG_V)
#define	PDT_SUP(pte)	(*(pte) & PG_SO)
#define	PDT_WP(pte)	(*(pte) & PG_PROT)

/*
 * Number of entries in a page table.
 */

#define	SDT_ENTRIES	(1<<(SDT_BITS))
#define PDT_ENTRIES	(1<<(PDT_BITS))

/*
 * Size in bytes of a single page table.
 */

#define SDT_SIZE	(sizeof(sdt_entry_t) * SDT_ENTRIES)
#define PDT_SIZE	(sizeof(pt_entry_t) * PDT_ENTRIES)

/*
 * Shifts and masks
 */

#define SDT_SHIFT	(PDT_BITS + PG_BITS)
#define PDT_SHIFT	(PG_BITS)

#define SDT_MASK	(((1 << SDT_BITS) - 1) << SDT_SHIFT)
#define PDT_MASK	(((1 << PDT_BITS) - 1) << PDT_SHIFT)

#define	SDTIDX(va)	(((va) & SDT_MASK) >> SDT_SHIFT)
#define	PDTIDX(va)	(((va) & PDT_MASK) >> PDT_SHIFT)

/*
 * BATC entries
 */

#define	BATC_V		0x00000001
#define	BATC_PROT	0x00000002
#define	BATC_INH	0x00000004
#define	BATC_GLOBAL	0x00000008
#define	BATC_WT		0x00000010
#define	BATC_SO		0x00000020

typedef uint32_t	batc_t;

/* 8820x fixed size BATC */
#define	BATC_BLKSHIFT	19
#define	BATC_BLKBYTES	(1 << BATC_BLKSHIFT)
#define	BATC_BLKMASK	(BATC_BLKBYTES-1)
/* number of programmable BATC entries */
#define	BATC_MAX	8

/* physical and logical block address */
#define	BATC_PSHIFT	6
#define	BATC_VSHIFT	19

#define	trunc_batc(a)	((a) & ~BATC_BLKMASK)
#define	round_batc(a)	trunc_batc((a) + BATC_BLKBYTES - 1)

#endif /* __M88K_MMU_H__ */
