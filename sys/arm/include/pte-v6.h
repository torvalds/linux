/*-
 * Copyright 2014 Svatopluk Kraus <onwahe@gmail.com>
 * Copyright 2014 Michal Meloun <meloun@miracle.cz>
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _MACHINE_PTE_V6_H_
#define _MACHINE_PTE_V6_H_

/*
 * Domain Types	for the	Domain Access Control Register.
 */
#define	DOMAIN_FAULT	0x00	/* no access */
#define	DOMAIN_CLIENT	0x01	/* client */
#define	DOMAIN_RESERVED	0x02	/* reserved */
#define	DOMAIN_MANAGER	0x03	/* manager */

/*
 * TEX remap registers attributes
 */
#define	PRRR_SO		0	/* Strongly ordered memory */
#define	PRRR_DEV	1	/* Device memory */
#define	PRRR_MEM	2	/* Normal memory */
#define	PRRR_DS0	(1 << 16) /* Shared bit for Device, S = 0 */
#define	PRRR_DS1	(1 << 17) /* Shared bit for Device, S = 1 */
#define	PRRR_NS0	(1 << 18) /* Shared bit for Normal, S = 0 */
#define	PRRR_NS1	(1 << 19) /* Shared bit for Normal, S = 1 */
#define	PRRR_NOS_SHIFT	24 	/* base shif for Not Outer Shared bits */

#define	NMRR_NC		0	/* Noncachable*/
#define	NMRR_WB_WA	1	/* Write Back, Write Allocate */
#define	NMRR_WT		2	/* Write Through, Non-Write Allocate */
#define	NMRR_WB		3	/* Write Back, Non-Write Allocate */

/*
 *
 * The ARM MMU is capable of mapping memory in the following chunks:
 *
 *	16M	Supersections (L1 table)
 *
 *	1M	Sections (L1 table)
 *
 *	64K	Large Pages (L2	table)
 *
 *	4K	Small Pages (L2	table)
 *
 *
 * Coarse Tables can map Large and Small Pages.
 * Coarse Tables are 1K in length.
 *
 * The Translation Table Base register holds the pointer to the
 * L1 Table.  The L1 Table is a 16K contiguous chunk of memory
 * aligned to a 16K boundary.  Each entry in the L1 Table maps
 * 1M of virtual address space, either via a Section mapping or
 * via an L2 Table.
 *
 */
#define	L1_TABLE_SIZE	0x4000		/* 16K */
#define	L1_ENTRIES	0x1000		/*  4K */
#define	L2_TABLE_SIZE	0x0400		/*  1K */
#define	L2_ENTRIES	0x0100		/* 256 */

/* ARMv6 super-sections. */
#define	L1_SUP_SIZE	0x01000000	/* 16M */
#define	L1_SUP_OFFSET	(L1_SUP_SIZE - 1)
#define	L1_SUP_FRAME	(~L1_SUP_OFFSET)
#define	L1_SUP_SHIFT	24

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

/*
 * ARM MMU L1 Descriptors
 */
#define	L1_TYPE_INV	0x00		/* Invalid (fault) */
#define	L1_TYPE_C	0x01		/* Coarse L2 */
#define	L1_TYPE_S	0x02		/* Section */
#define	L1_TYPE_MASK	0x03		/* Mask	of type	bits */

/* L1 Section Descriptor */
#define	L1_S_B		0x00000004	/* bufferable Section */
#define	L1_S_C		0x00000008	/* cacheable Section */
#define	L1_S_NX		0x00000010	/* not executeable */
#define	L1_S_DOM(x)	((x) <<	5)	/* domain */
#define	L1_S_DOM_MASK	L1_S_DOM(0xf)
#define	L1_S_P		0x00000200	/* ECC enable for this section */
#define	L1_S_AP(x)	((x) <<	10)	/* access permissions */
#define	L1_S_AP0	0x00000400	/* access permissions bit 0 */
#define	L1_S_AP1	0x00000800	/* access permissions bit 1 */
#define	L1_S_TEX(x)	((x) <<	12)	/* type	extension */
#define	L1_S_TEX0	0x00001000	/* type	extension bit 0	*/
#define	L1_S_TEX1	0x00002000	/* type	extension bit 1	*/
#define	L1_S_TEX2	0x00004000	/* type	extension bit 2	*/
#define	L1_S_AP2	0x00008000	/* access permissions bit 2 */
#define	L1_S_SHARED	0x00010000	/* shared */
#define	L1_S_NG		0x00020000	/* not global */
#define	L1_S_SUPERSEC	0x00040000	/* Section is a	super-section. */
#define	L1_S_ADDR_MASK	0xfff00000	/* phys	address	of section */

/* L1 Coarse Descriptor	*/
#define	L1_C_DOM(x)	((x) <<	5)	/* domain */
#define	L1_C_DOM_MASK	L1_C_DOM(0xf)
#define	L1_C_P		0x00000200	/* ECC enable for this section */
#define	L1_C_ADDR_MASK	0xfffffc00	/* phys	address	of L2 Table */

/*
 * ARM MMU L2 Descriptors
 */
#define	L2_TYPE_INV	0x00		/* Invalid (fault) */
#define	L2_TYPE_L	0x01		/* Large Page  - 64k - not used yet*/
#define	L2_TYPE_S	0x02		/* Small Page  - 4 */
#define	L2_TYPE_MASK	0x03

#define	L2_NX		0x00000001	/* Not executable */
#define	L2_B		0x00000004	/* Bufferable page */
#define	L2_C		0x00000008	/* Cacheable page */
#define	L2_CB_SHIFT		2	/* C,B bit field shift */
#define	L2_AP(x)	((x) <<	4)
#define	L2_AP0		0x00000010	/* access permissions bit 0*/
#define	L2_AP1		0x00000020	/* access permissions bit 1*/
#define	L2_TEX_SHIFT		6	/* type extension field shift */
#define	L2_TEX(x)	((x) <<	L2_TEX_SHIFT)	/* type	extension */
#define	L2_TEX0		0x00000040	/* type	extension bit 0	*/
#define	L2_TEX1		0x00000080	/* type	extension bit 1	*/
#define	L2_TEX2		0x00000100	/* type	extension bit 2	*/
#define	L2_AP2		0x00000200	/* access permissions  bit 2*/
#define	L2_SHARED	0x00000400	/* shared */
#define	L2_NG		0x00000800	/* not global */

/*
 * TEX classes encoding
 */
#define	TEX1_CLASS_0	 (			    0)
#define	TEX1_CLASS_1	 (		       L1_S_B)
#define	TEX1_CLASS_2	 (	      L1_S_C	     )
#define	TEX1_CLASS_3	 (	      L1_S_C | L1_S_B)
#define	TEX1_CLASS_4	 (L1_S_TEX0		     )
#define	TEX1_CLASS_5	 (L1_S_TEX0 |	       L1_S_B)
#define	TEX1_CLASS_6	 (L1_S_TEX0 | L1_S_C	     )	/* Reserved for	ARM11 */
#define	TEX1_CLASS_7	 (L1_S_TEX0 | L1_S_C | L1_S_B)

#define	TEX2_CLASS_0	 (		      0)
#define	TEX2_CLASS_1	 (		   L2_B)
#define	TEX2_CLASS_2	 (	    L2_C       )
#define	TEX2_CLASS_3	 (	    L2_C | L2_B)
#define	TEX2_CLASS_4	 (L2_TEX0	       )
#define	TEX2_CLASS_5	 (L2_TEX0 |	   L2_B)
#define	TEX2_CLASS_6	 (L2_TEX0 | L2_C       )	/* Reserved for	ARM11 */
#define	TEX2_CLASS_7	 (L2_TEX0 | L2_C | L2_B)

/* L1 table definitions. */
#define	NB_IN_PT1	L1_TABLE_SIZE
#define	NPTE1_IN_PT1	L1_ENTRIES

/* L2 table definitions. */
#define	NB_IN_PT2	L2_TABLE_SIZE
#define	NPTE2_IN_PT2	L2_ENTRIES

/*
 * Map memory attributes to TEX	classes
 */
#define	PTE2_ATTR_WB_WA		TEX2_CLASS_0
#define	PTE2_ATTR_NOCACHE	TEX2_CLASS_1
#define	PTE2_ATTR_DEVICE	TEX2_CLASS_2
#define	PTE2_ATTR_SO		TEX2_CLASS_3
#define	PTE2_ATTR_WT		TEX2_CLASS_4
/*
 * Software defined bits for L1	descriptors
 *  - L1_AP0 is	used as	page accessed bit
 *  - L1_AP2 (RO / not RW) is used as page not modified	bit
 *  - L1_TEX0 is used as software emulated RO bit
 */
#define	PTE1_V		L1_TYPE_S	/* Valid bit */
#define	PTE1_A		L1_S_AP0	/* Accessed - software emulated	*/
#define	PTE1_NM		L1_S_AP2	/* not modified	bit - software emulated
					 * used	as real	write enable bit */
#define	PTE1_M		0		/* Modified (dummy) */
#define	PTE1_S		L1_S_SHARED	/* Shared */
#define	PTE1_NG		L1_S_NG		/* Not global */
#define	PTE1_G		0		/* Global (dummy) */
#define	PTE1_NX		L1_S_NX		/* Not executable */
#define	PTE1_X		0		/* Executable (dummy) */
#define	PTE1_RO		L1_S_TEX1	/* Read	Only */
#define	PTE1_RW		0		/* Read-Write (dummy) */
#define	PTE1_U		L1_S_AP1	/* User	*/
#define	PTE1_NU		0		/* Not user (kernel only) (dummy) */
#define	PTE1_W		L1_S_TEX2	/* Wired */

#define	PTE1_SHIFT	L1_S_SHIFT
#define	PTE1_SIZE	L1_S_SIZE
#define	PTE1_OFFSET	L1_S_OFFSET
#define	PTE1_FRAME	L1_S_FRAME

#define	PTE1_ATTR_MASK	(L1_S_TEX0 | L1_S_C | L1_S_B)

#define	PTE1_AP_KR	(PTE1_RO | PTE1_NM)
#define	PTE1_AP_KRW	0
#define	PTE1_AP_KRUR	(PTE1_RO | PTE1_NM | PTE1_U)
#define	PTE1_AP_KRWURW	PTE1_U

/*
 * PTE1	descriptors creation macros.
 */
#define	PTE1_PA(pa)	((pa) &	PTE1_FRAME)
#define	PTE1_AP_COMMON	(PTE1_V	| PTE1_S)

#define	PTE1(pa, ap, attr)	(PTE1_PA(pa) | (ap) | (attr) | PTE1_AP_COMMON)

#define	PTE1_KERN(pa, ap, attr)		PTE1(pa, (ap) |	PTE1_A | PTE1_G, attr)
#define	PTE1_KERN_NG(pa, ap, attr)	PTE1(pa, (ap) |	PTE1_A | PTE1_NG, attr)

#define	PTE1_LINK(pa)	(((pa) & L1_C_ADDR_MASK) | L1_TYPE_C)

/*
 * Software defined bits for L2	descriptors
 *  - L2_AP0 is	used as	page accessed bit
 *  - L2_AP2 (RO / not RW) is used as page not modified	bit
 *  - L2_TEX0 is used as software emulated RO bit
 */
#define	PTE2_V		L2_TYPE_S	/* Valid bit */
#define	PTE2_A		L2_AP0		/* Accessed - software emulated	*/
#define	PTE2_NM		L2_AP2		/* not modified	bit - software emulated
					 * used	as real	write enable bit */
#define	PTE2_M		0		/* Modified (dummy) */
#define	PTE2_S		L2_SHARED	/* Shared */
#define	PTE2_NG		L2_NG		/* Not global */
#define	PTE2_G		0		/* Global (dummy) */
#define	PTE2_NX		L2_NX		/* Not executable */
#define	PTE2_X		0		/* Not executable (dummy) */
#define	PTE2_RO		L2_TEX1		/* Read	Only */
#define	PTE2_U		L2_AP1		/* User	*/
#define	PTE2_NU		0		/* Not user (kernel only) (dummy) */
#define	PTE2_W		L2_TEX2		/* Wired */

#define	PTE2_SHIFT	L2_S_SHIFT
#define	PTE2_SIZE	L2_S_SIZE
#define	PTE2_OFFSET	L2_S_OFFSET
#define	PTE2_FRAME	L2_S_FRAME

#define	PTE2_ATTR_MASK		(L2_TEX0 | L2_C | L2_B)
/* PTE2 attributes to TEX class index: (TEX0 C B)  */
#define	PTE2_ATTR2IDX(attr)					\
    ((((attr) & (L2_C | L2_B)) >> L2_CB_SHIFT) |		\
    (((attr) & L2_TEX0) >> (L2_TEX_SHIFT - L2_CB_SHIFT)))

#define	PTE2_AP_KR	(PTE2_RO | PTE2_NM)
#define	PTE2_AP_KRW	0
#define	PTE2_AP_KRUR	(PTE2_RO | PTE2_NM | PTE2_U)
#define	PTE2_AP_KRWURW	PTE2_U

/*
 * PTE2	descriptors creation macros.
 */
#define	PTE2_PA(pa)	((pa) &	PTE2_FRAME)
#define	PTE2_AP_COMMON	(PTE2_V	| PTE2_S)

#define	PTE2(pa, ap, attr)	(PTE2_PA(pa) | (ap) | (attr) | PTE2_AP_COMMON)

#define	PTE2_KERN(pa, ap, attr)		PTE2(pa, (ap) |	PTE2_A | PTE2_G, attr)
#define	PTE2_KERN_NG(pa, ap, attr)	PTE2(pa, (ap) |	PTE2_A | PTE2_NG, attr)

#endif /* !_MACHINE_PTE_V6_H_ */
