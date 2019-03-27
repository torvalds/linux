/*	$NetBSD: pte.h,v 1.1 2001/11/23 17:39:04 thorpej Exp $	*/

/*-
 * Copyright (c) 1994 Mark Brinicombe.
 * All rights reserved.
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
 *	This product includes software developed by the RiscBSD team.
 * 4. The name "RiscBSD" nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY RISCBSD ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL RISCBSD OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _MACHINE_PTE_V4_H_
#define _MACHINE_PTE_V4_H_

#ifndef LOCORE
typedef	uint32_t	pd_entry_t;		/* page directory entry */
typedef	uint32_t	pt_entry_t;		/* page table entry */
typedef	pt_entry_t	pt2_entry_t;		/* compatibility with v6 */
#endif

#define PG_FRAME	0xfffff000

/* The PT_SIZE definition is misleading... A page table is only 0x400
 * bytes long. But since VM mapping can only be done to 0x1000 a single
 * 1KB blocks cannot be steered to a va by itself. Therefore the
 * pages tables are allocated in blocks of 4. i.e. if a 1 KB block
 * was allocated for a PT then the other 3KB would also get mapped
 * whenever the 1KB was mapped.
 */

#define PT_RSIZE	0x0400		/* Real page table size */
#define PT_SIZE		0x1000
#define PD_SIZE		0x4000

/* Page table types and masks */
#define L1_PAGE		0x01	/* L1 page table mapping */
#define L1_SECTION	0x02	/* L1 section mapping */
#define L1_FPAGE	0x03	/* L1 fine page mapping */
#define L1_MASK		0x03	/* Mask for L1 entry type */
#define L2_LPAGE	0x01	/* L2 large page (64KB) */
#define L2_SPAGE	0x02	/* L2 small page (4KB) */
#define L2_MASK		0x03	/* Mask for L2 entry type */
#define L2_INVAL	0x00	/* L2 invalid type */

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

/* ARMv6 super-sections. */
#define L1_SUP_SIZE	0x01000000	/* 16M */
#define L1_SUP_OFFSET	(L1_SUP_SIZE - 1)
#define L1_SUP_FRAME	(~L1_SUP_OFFSET)
#define L1_SUP_SHIFT	24

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
#define	L1_TABLE_SIZE	0x4000		/* 16K */
#define	L2_TABLE_SIZE	0x1000		/* 4K */
/*
 * The new pmap deals with the 1KB coarse L2 tables by
 * allocating them from a pool. Until every port has been converted,
 * keep the old L2_TABLE_SIZE define lying around. Converted ports
 * should use L2_TABLE_SIZE_REAL until then.
 */
#define	L2_TABLE_SIZE_REAL	0x400	/* 1K */

/* Total number of page table entries in L2 table */
#define	L2_PTE_NUM_TOTAL	(L2_TABLE_SIZE_REAL / sizeof(pt_entry_t))

/*
 * ARM L1 Descriptors
 */

#define	L1_TYPE_INV	0x00		/* Invalid (fault) */
#define	L1_TYPE_C	0x01		/* Coarse L2 */
#define	L1_TYPE_S	0x02		/* Section */
#define	L1_TYPE_F	0x03		/* Fine L2 */
#define	L1_TYPE_MASK	0x03		/* mask of type bits */

/* L1 Section Descriptor */
#define	L1_S_B		0x00000004	/* bufferable Section */
#define	L1_S_C		0x00000008	/* cacheable Section */
#define	L1_S_IMP	0x00000010	/* implementation defined */
#define	L1_S_XN		(1 << 4)	/* execute not */
#define	L1_S_DOM(x)	((x) << 5)	/* domain */
#define	L1_S_DOM_MASK	L1_S_DOM(0xf)
#define	L1_S_AP(x)	((x) << 10)	/* access permissions */
#define	L1_S_ADDR_MASK	0xfff00000	/* phys address of section */
#define	L1_S_TEX(x)	(((x) & 0x7) << 12)	/* Type Extension */
#define	L1_S_TEX_MASK	(0x7 << 12)	/* Type Extension */
#define	L1_S_APX	(1 << 15)
#define	L1_SHARED	(1 << 16)

#define	L1_S_XSCALE_P	0x00000200	/* ECC enable for this section */
#define	L1_S_XSCALE_TEX(x) ((x) << 12)	/* Type Extension */

#define L1_S_SUPERSEC	((1) << 18)	/* Section is a super-section. */

/* L1 Coarse Descriptor */
#define	L1_C_IMP0	0x00000004	/* implementation defined */
#define	L1_C_IMP1	0x00000008	/* implementation defined */
#define	L1_C_IMP2	0x00000010	/* implementation defined */
#define	L1_C_DOM(x)	((x) << 5)	/* domain */
#define	L1_C_DOM_MASK	L1_C_DOM(0xf)
#define	L1_C_ADDR_MASK	0xfffffc00	/* phys address of L2 Table */

#define	L1_C_XSCALE_P	0x00000200	/* ECC enable for this section */

/* L1 Fine Descriptor */
#define	L1_F_IMP0	0x00000004	/* implementation defined */
#define	L1_F_IMP1	0x00000008	/* implementation defined */
#define	L1_F_IMP2	0x00000010	/* implementation defined */
#define	L1_F_DOM(x)	((x) << 5)	/* domain */
#define	L1_F_DOM_MASK	L1_F_DOM(0xf)
#define	L1_F_ADDR_MASK	0xfffff000	/* phys address of L2 Table */

#define	L1_F_XSCALE_P	0x00000200	/* ECC enable for this section */

/*
 * ARM L2 Descriptors
 */

#define	L2_TYPE_INV	0x00		/* Invalid (fault) */
#define	L2_TYPE_L	0x01		/* Large Page */
#define	L2_TYPE_S	0x02		/* Small Page */
#define	L2_TYPE_T	0x03		/* Tiny Page */
#define	L2_TYPE_MASK	0x03		/* mask of type bits */

	/*
	 * This L2 Descriptor type is available on XScale processors
	 * when using a Coarse L1 Descriptor.  The Extended Small
	 * Descriptor has the same format as the XScale Tiny Descriptor,
	 * but describes a 4K page, rather than a 1K page.
	 */
#define	L2_TYPE_XSCALE_XS 0x03		/* XScale Extended Small Page */

#define	L2_B		0x00000004	/* Bufferable page */
#define	L2_C		0x00000008	/* Cacheable page */
#define	L2_AP0(x)	((x) << 4)	/* access permissions (sp 0) */
#define	L2_AP1(x)	((x) << 6)	/* access permissions (sp 1) */
#define	L2_AP2(x)	((x) << 8)	/* access permissions (sp 2) */
#define	L2_AP3(x)	((x) << 10)	/* access permissions (sp 3) */

#define	L2_SHARED	(1 << 10)
#define	L2_APX		(1 << 9)
#define	L2_XN		(1 << 0)
#define	L2_L_TEX_MASK	(0x7 << 12)	/* Type Extension */
#define	L2_L_TEX(x)	(((x) & 0x7) << 12)
#define	L2_S_TEX_MASK	(0x7 << 6)	/* Type Extension */
#define	L2_S_TEX(x)	(((x) & 0x7) << 6)

#define	L2_XSCALE_L_TEX(x) ((x) << 12)	/* Type Extension */
#define L2_XSCALE_L_S(x)   (1 << 15)	/* Shared */
#define	L2_XSCALE_T_TEX(x) ((x) << 6)	/* Type Extension */

/*
 * Access Permissions for L1 and L2 Descriptors.
 */
#define	AP_W		0x01		/* writable */
#define	AP_REF		0x01		/* referenced flag */
#define	AP_U		0x02		/* user */

/*
 * Short-hand for common AP_* constants.
 *
 * Note: These values assume the S (System) bit is set and
 * the R (ROM) bit is clear in CP15 register 1.
 */
#define	AP_KR		0x00		/* kernel read */
#define	AP_KRW		0x01		/* kernel read/write */
#define	AP_KRWUR	0x02		/* kernel read/write usr read */
#define	AP_KRWURW	0x03		/* kernel read/write usr read/write */

/*
 * Domain Types for the Domain Access Control Register.
 */
#define	DOMAIN_FAULT	0x00		/* no access */
#define	DOMAIN_CLIENT	0x01		/* client */
#define	DOMAIN_RESERVED	0x02		/* reserved */
#define	DOMAIN_MANAGER	0x03		/* manager */

/*
 * Type Extension bits for XScale processors.
 *
 * Behavior of C and B when X == 0:
 *
 * C B  Cacheable  Bufferable  Write Policy  Line Allocate Policy
 * 0 0      N          N            -                 -
 * 0 1      N          Y            -                 -
 * 1 0      Y          Y       Write-through    Read Allocate
 * 1 1      Y          Y        Write-back      Read Allocate
 *
 * Behavior of C and B when X == 1:
 * C B  Cacheable  Bufferable  Write Policy  Line Allocate Policy
 * 0 0      -          -            -                 -           DO NOT USE
 * 0 1      N          Y            -                 -
 * 1 0  Mini-Data      -            -                 -
 * 1 1      Y          Y        Write-back       R/W Allocate
 */
#define	TEX_XSCALE_X	0x01		/* X modifies C and B */
#define TEX_XSCALE_E	0x02
#define TEX_XSCALE_T	0x04

/* Xscale core 3 */

/*
 *
 * Cache attributes with L2 present, S = 0
 * T E X C B   L1 i-cache L1 d-cache L1 DC WP  L2 cacheable write coalesce
 * 0 0 0 0 0 	N	  N 		- 	N		N
 * 0 0 0 0 1	N	  N		-	N		Y
 * 0 0 0 1 0	Y	  Y		WT	N		Y
 * 0 0 0 1 1	Y	  Y		WB	Y		Y
 * 0 0 1 0 0	N	  N		-	Y		Y
 * 0 0 1 0 1	N	  N		-	N		N
 * 0 0 1 1 0	Y	  Y		-	-		N
 * 0 0 1 1 1	Y	  Y		WT	Y		Y
 * 0 1 0 0 0	N	  N		-	N		N
 * 0 1 0 0 1	N/A	N/A		N/A	N/A		N/A
 * 0 1 0 1 0	N/A	N/A		N/A	N/A		N/A
 * 0 1 0 1 1	N/A	N/A		N/A	N/A		N/A
 * 0 1 1 X X	N/A	N/A		N/A	N/A		N/A
 * 1 X 0 0 0	N	  N		-	N		Y
 * 1 X 0 0 1	Y	  N		WB	N		Y
 * 1 X 0 1 0	Y	  N		WT	N		Y
 * 1 X 0 1 1	Y	  N		WB	Y		Y
 * 1 X 1 0 0	N	  N		-	Y		Y
 * 1 X 1 0 1	Y	  Y		WB	Y		Y
 * 1 X 1 1 0	Y	  Y		WT	Y		Y
 * 1 X 1 1 1	Y	  Y		WB	Y		Y
 *
 *
 *
 *
  * Cache attributes with L2 present, S = 1
 * T E X C B   L1 i-cache L1 d-cache L1 DC WP  L2 cacheable write coalesce
 * 0 0 0 0 0 	N	  N 		- 	N		N
 * 0 0 0 0 1	N	  N		-	N		Y
 * 0 0 0 1 0	Y	  Y		-	N		Y
 * 0 0 0 1 1	Y	  Y		WT	Y		Y
 * 0 0 1 0 0	N	  N		-	Y		Y
 * 0 0 1 0 1	N	  N		-	N		N
 * 0 0 1 1 0	Y	  Y		-	-		N
 * 0 0 1 1 1	Y	  Y		WT	Y		Y
 * 0 1 0 0 0	N	  N		-	N		N
 * 0 1 0 0 1	N/A	N/A		N/A	N/A		N/A
 * 0 1 0 1 0	N/A	N/A		N/A	N/A		N/A
 * 0 1 0 1 1	N/A	N/A		N/A	N/A		N/A
 * 0 1 1 X X	N/A	N/A		N/A	N/A		N/A
 * 1 X 0 0 0	N	  N		-	N		Y
 * 1 X 0 0 1	Y	  N		-	N		Y
 * 1 X 0 1 0	Y	  N		-	N		Y
 * 1 X 0 1 1	Y	  N		-	Y		Y
 * 1 X 1 0 0	N	  N		-	Y		Y
 * 1 X 1 0 1	Y	  Y		WT	Y		Y
 * 1 X 1 1 0	Y	  Y		WT	Y		Y
 * 1 X 1 1 1	Y	  Y		WT	Y		Y
 */
#endif /* !_MACHINE_PTE_V4_H_ */

/* End of pte.h */
