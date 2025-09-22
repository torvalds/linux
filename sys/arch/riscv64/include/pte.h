/*	$OpenBSD: pte.h,v 1.4 2024/10/14 12:02:16 jsg Exp $	*/

/*
 * Copyright (c) 2019 Brian Bamsch <bbamsch@google.com>
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
#ifndef _RISCV64_PTE_H_
#define _RISCV64_PTE_H_

#define Lx_TABLE_ALIGN	(4096)

/* Block and Page attributes */
/* Bits 9:8 are reserved for software */
#define	PTE_ATTR_MASK	(0x3ffUL)
#define	PTE_SW_MANAGED	(1 << 9)
#define	PTE_SW_WIRED	(1 << 8)
#define	PTE_D		(1 << 7) /* Dirty */
#define	PTE_A		(1 << 6) /* Accessed */
#define	PTE_G		(1 << 5) /* Global */
#define	PTE_U		(1 << 4) /* User */
#define	PTE_X		(1 << 3) /* Execute */
#define	PTE_W		(1 << 2) /* Write */
#define	PTE_R		(1 << 1) /* Read */
#define	PTE_V		(1 << 0) /* Valid */
#define	PTE_RWX		(PTE_R | PTE_W | PTE_X)
#define	PTE_RX		(PTE_R | PTE_X)
#define	PTE_KERN	(PTE_V | PTE_R | PTE_W | PTE_A | PTE_D)

/* T-Head extended page attributes */
#define	PTE_THEAD_SO	(1ULL << 63)
#define	PTE_THEAD_C	(1ULL << 62)
#define	PTE_THEAD_B	(1ULL << 61)
#define	PTE_THEAD_SH	(1ULL << 60)

/* Level 0 table, 512GiB per entry */
#define	 L0_SHIFT	39

/* Level 1 table, 1GiB per entry */
#define	 L1_SHIFT	30
#define	 L1_SIZE	(1UL << L1_SHIFT)
#define	 L1_OFFSET	(L1_SIZE - 1)

/* Level 2 table, 2MiB per entry */
#define	 L2_SHIFT	21
#define	 L2_SIZE	(1UL << L2_SHIFT)
#define	 L2_OFFSET	(L2_SIZE - 1)

/* Level 3 table, 4KiB per entry */
#define	 L3_SHIFT	12
#define	 L3_SIZE	(1UL << L3_SHIFT)
#define	 L3_OFFSET	(L3_SIZE - 1)

/* page mapping */
#define	 Ln_ENTRIES_SHIFT 9
#define	 Ln_ENTRIES	(1 << Ln_ENTRIES_SHIFT)
#define	 Ln_ADDR_MASK	(Ln_ENTRIES - 1)
#define	 Ln_TABLE_MASK	((1 << 12) - 1)

/* physical page number mask */
#define PTE_RPGN (((1ULL << 56) - 1) & ~PAGE_MASK)

#define	PTE_PPN0_S	10
#define	PTE_PPN1_S	19
#define	PTE_PPN2_S	28
#define	PTE_PPN3_S	37
#define	PTE_SIZE	8

#ifndef _LOCORE
typedef	uint64_t	pt_entry_t;		/* page table entry */
typedef	uint64_t	pn_t;			/* page number */
#endif /* !_LOCORE */

#endif /* _RISCV64_PTE_H_ */
