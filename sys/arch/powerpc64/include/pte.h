/*	$OpenBSD: pte.h,v 1.8 2023/01/25 09:53:53 kettenis Exp $	*/

/*
 * Copyright (c) 2020 Mark Kettenis <kettenis@openbsd.org>
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

#ifndef _MACHINE_PTE_H_
#define _MACHINE_PTE_H_

/*
 * Page Table Entry bits that should work for all 64-bit POWER CPUs as
 * well as the PowerPC 970.
 */

struct pte {
	uint64_t pte_hi;
	uint64_t pte_lo;
};

/* High doubleword: */
#define PTE_VALID		0x0000000000000001ULL
#define PTE_HID			0x0000000000000002ULL
#define PTE_WIRED		0x0000000000000008ULL /* SW */
#define PTE_AVPN		0x3fffffffffffff80ULL
#define PTE_VSID_SHIFT		12

/* Low doubleword: */
#define PTE_PP			0x0000000000000003ULL
#define PTE_RO			0x0000000000000003ULL
#define PTE_RW			0x0000000000000002ULL
#define PTE_N			0x0000000000000004ULL
#define PTE_G			0x0000000000000008ULL
#define PTE_M			0x0000000000000010ULL
#define PTE_I			0x0000000000000020ULL
#define PTE_W			0x0000000000000040ULL
#define PTE_CHG			0x0000000000000080ULL
#define PTE_REF			0x0000000000000100ULL
#define PTE_AC			0x0000000000000200ULL
#define PTE_RPGN		0x0ffffffffffff000ULL

#define ADDR_PIDX		0x000000000ffff000ULL
#define ADDR_PIDX_SHIFT		12
#define ADDR_ESID_SHIFT		28
#define ADDR_VSID_SHIFT		28

struct pate {
	uint64_t pate_htab;
	uint64_t pate_prt;
};

#define SLBE_ESID_SHIFT	28
#define SLBE_VALID	0x0000000008000000UL

#define SLBV_VSID_SHIFT	12

struct slb {
	uint64_t slb_slbe;
	uint64_t slb_slbv;
};

#define VSID_VRMA	0x1ffffff

#define USER_ADDR	0xcfffffff00000000ULL
#define USER_ESID	(USER_ADDR >> ADDR_ESID_SHIFT)
#define SEGMENT_SIZE	(256 * 1024 * 1024ULL)
#define SEGMENT_MASK 	(SEGMENT_SIZE - 1)

#endif /* _MACHINE_PTE_H_ */
