/*	$OpenBSD: pte.h,v 1.10 2021/03/11 11:16:55 jsg Exp $	*/
/*	$NetBSD: pte.h,v 1.6 2003/04/18 11:08:28 scw Exp $	*/

/*
 * Copyright (c) 2001, 2002 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Jason R. Thorpe for Wasabi Systems, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed for the NetBSD Project by
 *	Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _ARM_PTE_H_
#define	_ARM_PTE_H_

/*
 * The ARM MMU architecture was introduced with ARM v3 (previous ARM
 * architecture versions used an optional off-CPU memory controller
 * to perform address translation).
 *
 * The ARM MMU consists of a TLB and translation table walking logic.
 * There is typically one TLB per memory interface (or, put another
 * way, one TLB per software-visible cache).
 *
 * The ARM MMU is capable of mapping memory in the following chunks:
 *
 *	1M	Sections (L1 table)
 *
 *	64K	Large Pages (L2 table)
 *
 *	4K	Small Pages (L2 table)
 *
 *	1K	Tiny Pages (L2 table)
 *
 * There are two types of L2 tables: Coarse Tables and Fine Tables.
 * Coarse Tables can map Large and Small Pages.  Fine Tables can
 * map Tiny Pages.
 *
 * Coarse Tables can define 4 Subpages within Large and Small pages.
 * Subpages define different permissions for each Subpage within
 * a Page.
 *
 * Coarse Tables are 1K in length.  Fine tables are 4K in length.
 *
 * The Translation Table Base register holds the pointer to the
 * L1 Table.  The L1 Table is a 16K contiguous chunk of memory
 * aligned to a 16K boundary.  Each entry in the L1 Table maps
 * 1M of virtual address space, either via a Section mapping or
 * via an L2 Table.
 *
 * In addition, the Fast Context Switching Extension (FCSE) is available
 * on some ARM v4 and ARM v5 processors.  FCSE is a way of eliminating
 * TLB/cache flushes on context switch by use of a smaller address space
 * and a "process ID" that modifies the virtual address before being
 * presented to the translation logic.
 */

#ifndef _LOCORE
typedef uint32_t	pd_entry_t;	/* L1 table entry */
typedef uint32_t	pt_entry_t;	/* L2 table entry */
#endif /* _LOCORE */

#define	L1_S_SIZE	0x00100000	/* 1M */
#define	L1_S_OFFSET	(L1_S_SIZE - 1)
#define	L1_S_FRAME	(~L1_S_OFFSET)
#define	L1_S_SHIFT	20

#define	L2_L_SIZE	0x00010000	/* 64K */
#define	L2_L_OFFSET	(L2_L_SIZE - 1)
#define	L2_L_FRAME	(~L2_L_OFFSET)
#define	L2_L_SHIFT	16

#define	L2_S_SIZE	0x00001000	/* 4K */
#define	L2_S_OFFSET	(L2_S_SIZE - 1)
#define	L2_S_FRAME	(~L2_S_OFFSET)
#define	L2_S_SHIFT	12

#define	L2_T_SIZE	0x00000400	/* 1K */
#define	L2_T_OFFSET	(L2_T_SIZE - 1)
#define	L2_T_FRAME	(~L2_T_OFFSET)
#define	L2_T_SHIFT	10

/*
 * The NetBSD VM implementation only works on whole pages (4K),
 * whereas the ARM MMU's Coarse tables are sized in terms of 1K
 * (16K L1 table, 1K L2 table).
 *
 * So, we allocate L2 tables 4 at a time, thus yielding a 4K L2
 * table.
 */
#define	L1_ADDR_BITS	0xfff00000	/* L1 PTE address bits */
#define	L2_ADDR_BITS	0x000ff000	/* L2 PTE address bits */

#define	L1_TABLE_SIZE	0x4000		/* 16K */
#define	L2_TABLE_SIZE	0x1000		/* 4K */
/*
 * The new pmap deals with the 1KB coarse L2 tables by
 * allocating them from a pool. Until every port has been converted,
 * keep the old L2_TABLE_SIZE define lying around. Converted ports
 * should use L2_TABLE_SIZE_REAL until then.
 */
#define	L2_TABLE_SIZE_REAL	0x400	/* 1K */

/*
 * ARM L1 Descriptors
 */

#define	L1_TYPE_INV	0x00		/* Invalid (fault) */
#define	L1_TYPE_C	0x01		/* Coarse L2 */
#define	L1_TYPE_S	0x02		/* Section or Supersection */
#define	L1_TYPE_F	0x03		/* Fine L2 (pre-V7) */
#define	L1_TYPE_MASK	0x03		/* mask of type bits */

/* L1 Section Descriptor */
#define	L1_S_B		0x00000004	/* bufferable Section */
#define	L1_S_C		0x00000008	/* cacheable Section */
#define	L1_S_IMP	0x00000010	/* implementation defined */
#define	L1_S_DOM(x)	((x) << 5)	/* domain */
#define	L1_S_DOM_MASK	L1_S_DOM(0xf)
#define	L1_S_AP(x)	((x) << 10)	/* access permissions */
#define	L1_S_ADDR_MASK	0xfff00000	/* phys address of section */

#define	L1_S_V7_TEX(x)	(((x) & 0x7) << 12)	/* Type Extension */
#define	L1_S_V7_TEX_MASK	(0x7 << 12)	/* Type Extension */
#define	L1_S_V7_NS	0x00080000	/* Non-secure */
#define	L1_S_V7_SS	0x00040000	/* Supersection */
#define	L1_S_V7_nG	0x00020000	/* not Global */
#define	L1_S_V7_S	0x00010000	/* Shareable */
#define	L1_S_V7_AP(x)	((((x) & 0x4) << 13) | (((x) & 0x2) << 10))	/* AP */
#define	L1_S_V7_AF	0x00000400	/* Access Flag */
#define	L1_S_V7_IMP	0x00000200	/* implementation defined */
#define	L1_S_V7_XN	0x00000010	/* eXecute Never */
#define	L1_S_V7_PXN	0x00000001	/* Privileged eXecute Never */

/* L1 Coarse Descriptor */
#define	L1_C_IMP0	0x00000004	/* implementation defined */
#define	L1_C_IMP1	0x00000008	/* implementation defined */
#define	L1_C_IMP2	0x00000010	/* implementation defined */
#define	L1_C_DOM(x)	((x) << 5)	/* domain */
#define	L1_C_DOM_MASK	L1_C_DOM(0xf)
#define	L1_C_ADDR_MASK	0xfffffc00	/* phys address of L2 Table */

#define	L1_C_V7_IMP	0x00000200	/* implementation defined */
#define	L1_C_V7_NS	0x00000008	/* Non-secure */
#define	L1_C_V7_PXN	0x00000004	/* Privileged eXecute Never */

/* L1 Fine Descriptor */
#define	L1_F_IMP0	0x00000004	/* implementation defined */
#define	L1_F_IMP1	0x00000008	/* implementation defined */
#define	L1_F_IMP2	0x00000010	/* implementation defined */
#define	L1_F_DOM(x)	((x) << 5)	/* domain */
#define	L1_F_DOM_MASK	L1_F_DOM(0xf)
#define	L1_F_ADDR_MASK	0xfffff000	/* phys address of L2 Table */

/*
 * ARM L2 Descriptors
 */

#define	L2_TYPE_INV	0x00		/* Invalid (fault) */
#define	L2_TYPE_L	0x01		/* Large Page */
#define	L2_TYPE_S	0x02		/* Small Page */
#define	L2_TYPE_T	0x03		/* Tiny Page (pre-V7) */
#define	L2_TYPE_MASK	0x03		/* mask of type bits */

#define	L2_B		0x00000004	/* Bufferable page */
#define	L2_C		0x00000008	/* Cacheable page */
#define	L2_AP0(x)	((x) << 4)	/* access permissions (sp 0) */
#define	L2_AP1(x)	((x) << 6)	/* access permissions (sp 1) */
#define	L2_AP2(x)	((x) << 8)	/* access permissions (sp 2) */
#define	L2_AP3(x)	((x) << 10)	/* access permissions (sp 3) */
#define	L2_AP(x)	(L2_AP0(x) | L2_AP1(x) | L2_AP2(x) | L2_AP3(x))

#define	L2_V7_L_TEX(x)	(((x) & 0x7) << 12)	/* Type Extension */
#define	L2_V7_L_TEX_MASK	(0x7 << 12)	/* Type Extension */
#define	L2_V7_L_XN	0x00008000	/* eXecute Never */
#define	L2_V7_S_TEX(x)	(((x) & 0x7) << 6)	/* Type Extension */
#define	L2_V7_S_TEX_MASK	(0x7 << 6)	/* Type Extension */
#define	L2_V7_S_XN	0x00000001	/* eXecute Never */

#define	L2_V7_AP(x)	((((x) & 0x4) << 7) | (((x) & 0x2) << 4))	/* AP */
#define	L2_V7_AF	0x00000010	/* Access Flag */
#define	L2_V7_S		0x00000400	/* Shareable */
#define	L2_V7_nG	0x00000800	/* not Global */

/*
 * Short-hand for common AP_* constants.
 *
 * Note: These values assume the S (System) bit is set and
 * the R (ROM) bit is clear in CP15 register 1.
 */
#define	AP_KR		0x00		/* kernel read */
#define	AP_V7_KR	0x05
#define	AP_KRW		0x01		/* kernel read/write */
#define	AP_KRWUR	0x02		/* kernel read/write usr read */
#define	AP_V7_KRUR	0x07		/* kernel read usr read */
#define	AP_KRWURW	0x03		/* kernel read/write usr read/write */

/*
 * Domain Types for the Domain Access Control Register.
 */
#define	DOMAIN_FAULT	0x00		/* no access */
#define	DOMAIN_CLIENT	0x01		/* client */
#define	DOMAIN_RESERVED	0x02		/* reserved */
#define	DOMAIN_MANAGER	0x03		/* manager */

#endif /* _ARM_PTE_H_ */
