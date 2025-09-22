/* $OpenBSD: pte.h,v 1.10 2024/10/14 12:02:16 jsg Exp $ */
/*
 * Copyright (c) 2014 Dale Rahn <drahn@dalerahn.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef _ARM_PTE_H_
#define _ARM_PTE_H_

/*  level X descriptor */
#define	Lx_TYPE_MASK	(0x00000003)	/* mask of type bits */
#define Lx_TYPE_S 	(0x00000001)
#define Lx_TYPE_PT 	(0x00000003)
// XXX need to investigate use of these
#define Lx_PT_NS	(1ULL<<63)
#define Lx_PT_AP00	(0ULL<<61)
#define Lx_PT_AP01	(1ULL<<61)
#define Lx_PT_AP10	(2ULL<<61)
#define Lx_PT_AP11	(3ULL<<61)
#define Lx_PT_XN	(1ULL<<60)
#define Lx_PT_PXN	(1ULL<<59)
#define	Lx_TABLE_ALIGN	(4096)

/* Block and Page attributes */
/* TODO: Add the upper attributes */
#define		ATTR_MASK_H	(0xfff0000000000000ULL)
#define		ATTR_MASK_L	(0x0000000000000fffULL)
#define		ATTR_MASK	(ATTR_MASK_H | ATTR_MASK_L)
/* Bits 58:55 are reserved for software */
#define		ATTR_SW_MANAGED	(1UL << 56)
#define		ATTR_SW_WIRED	(1UL << 55)
#define		ATTR_UXN	(1UL << 54)
#define		ATTR_PXN	(1UL << 53)
#define		ATTR_GP		(1UL << 50)
#define		ATTR_nG		(1 << 11)
#define		ATTR_AF		(1 << 10)
#define		ATTR_SH(x)	((x) << 8)
#define		ATTR_AP_RW_BIT	(1 << 7)
#define		ATTR_AP(x)	((x) << 6)
#define		ATTR_AP_MASK	ATTR_AP(3)
#define		ATTR_NS		(1 << 5)
#define		ATTR_IDX(x)	((x) << 2)
#define		ATTR_IDX_MASK	(7 << 2)

#define		PTE_ATTR_DEV_NGNRNE	0
#define		PTE_ATTR_DEV_NGNRE	1
#define		PTE_ATTR_CI		2
#define		PTE_ATTR_WB		3
#define		PTE_ATTR_WT		4

#define		PTE_MEMATTR_DEV_NGNRNE	0x0
#define		PTE_MEMATTR_DEV_NGNRE	0x1
#define		PTE_MEMATTR_CI		0x5
#define		PTE_MEMATTR_WB		0xf
#define		PTE_MEMATTR_WT		0xa

#define		SH_INNER	3
#define		SH_OUTER	2
#define		SH_NONE		0

/* Level 0 table, 512GiB per entry */
#define		L0_SHIFT	39
#define		L0_INVAL	0x0 /* An invalid address */
#define		L0_BLOCK	0x1 /* A block */
	/* 0x2 also marks an invalid address */
#define		L0_TABLE	0x3 /* A next-level table */

/* Level 1 table, 1GiB per entry */
#define		L1_SHIFT	30
#define		L1_SIZE		(1 << L1_SHIFT)
#define		L1_OFFSET	(L1_SIZE - 1)
#define		L1_INVAL	L0_INVAL
#define		L1_BLOCK	L0_BLOCK
#define		L1_TABLE	L0_TABLE

/* Level 2 table, 2MiB per entry */
#define		L2_SHIFT	21
#define		L2_SIZE		(1 << L2_SHIFT)
#define		L2_OFFSET	(L2_SIZE - 1)
#define		L2_INVAL	L0_INVAL
#define		L2_BLOCK	L0_BLOCK
#define		L2_TABLE	L0_TABLE

/* page mapping */
#define		L3_P		0x3

#define		Ln_ENTRIES	(1 << 9)
#define		Ln_ADDR_MASK	(Ln_ENTRIES - 1)
#define		Ln_TABLE_MASK	((1 << 12) - 1)

/* physical page mask */
#define PTE_RPGN (((1ULL << 48) - 1) & ~PAGE_MASK)

#endif /* _ARM_PTE_H_ */
