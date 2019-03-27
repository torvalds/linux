/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2010 Juli Mallett <jmallett@FreeBSD.org>
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

#ifndef	_MACHINE_PTE_H_
#define	_MACHINE_PTE_H_

#ifndef _LOCORE
#if defined(__mips_n64) || defined(__mips_n32) /*  PHYSADDR_64_BIT */
typedef	uint64_t pt_entry_t;
#else
typedef	uint32_t pt_entry_t;
#endif
typedef	pt_entry_t *pd_entry_t;
#endif

/*
 * TLB and PTE management.  Most things operate within the context of
 * EntryLo0,1, and begin with TLBLO_.  Things which work with EntryHi
 * start with TLBHI_.  PTE bits begin with PTE_.
 *
 * Note that we use the same size VM and TLB pages.
 */
#define	TLB_PAGE_SHIFT	(PAGE_SHIFT)
#define	TLB_PAGE_SIZE	(1 << TLB_PAGE_SHIFT)
#define	TLB_PAGE_MASK	(TLB_PAGE_SIZE - 1)

/*
 * TLB PageMask register.  Has mask bits set above the default, 4K, page mask.
 */
#define	TLBMASK_SHIFT	(13)
#define	TLBMASK_MASK	((PAGE_MASK >> TLBMASK_SHIFT) << TLBMASK_SHIFT)

/*
 * FreeBSD/mips page-table entries take a near-identical format to MIPS TLB
 * entries, each consisting of two 32-bit or 64-bit values ("EntryHi" and
 * "EntryLo").  MIPS4k and MIPS64 both define certain bits in TLB entries as
 * reserved, and these must be zero-filled by software.  We overload these
 * bits in PTE entries to hold  PTE_ flags such as RO, W, and MANAGED.
 * However, we must mask these out when writing to TLB entries to ensure that
 * they do not become visible to hardware -- especially on MIPS64r2 which has
 * an extended physical memory space.
 *
 * When using n64 and n32, shift software-defined bits into the MIPS64r2
 * reserved range, which runs from bit 55 ... 63.  In other configurations
 * (32-bit MIPS4k and compatible), shift them out to bits 29 ... 31.
 *
 * NOTE: This means that for 32-bit use of CP0, we aren't able to set the top
 * bit of PFN to a non-zero value, as software is using it!  This physical
 * memory size limit may not be sufficiently enforced elsewhere.
 */
#if defined(__mips_n64) || defined(__mips_n32) /*  PHYSADDR_64_BIT */
#define	TLBLO_SWBITS_SHIFT	(55)
#define	TLBLO_SWBITS_CLEAR_SHIFT	(9)
#define	TLBLO_PFN_MASK		0x3FFFFFFC0ULL
#else
#define	TLBLO_SWBITS_SHIFT	(29)
#define	TLBLO_SWBITS_CLEAR_SHIFT	(3)
#define	TLBLO_PFN_MASK		(0x1FFFFFC0)
#endif
#define	TLBLO_PFN_SHIFT		(6)
#define	TLBLO_SWBITS_MASK	((pt_entry_t)0x7 << TLBLO_SWBITS_SHIFT)
#define	TLBLO_PA_TO_PFN(pa)	((((pa) >> TLB_PAGE_SHIFT) << TLBLO_PFN_SHIFT) & TLBLO_PFN_MASK)
#define	TLBLO_PFN_TO_PA(pfn)	((vm_paddr_t)((pfn) >> TLBLO_PFN_SHIFT) << TLB_PAGE_SHIFT)
#define	TLBLO_PTE_TO_PFN(pte)	((pte) & TLBLO_PFN_MASK)
#define	TLBLO_PTE_TO_PA(pte)	(TLBLO_PFN_TO_PA(TLBLO_PTE_TO_PFN((pte))))

/*
 * XXX This comment is not correct for anything more modern than R4K.
 *
 * VPN for EntryHi register.  Upper two bits select user, supervisor,
 * or kernel.  Bits 61 to 40 copy bit 63.  VPN2 is bits 39 and down to
 * as low as 13, down to PAGE_SHIFT, to index 2 TLB pages*.  From bit 12
 * to bit 8 there is a 5-bit 0 field.  Low byte is ASID.
 *
 * XXX This comment is not correct for FreeBSD.
 * Note that in FreeBSD, we map 2 TLB pages is equal to 1 VM page.
 */
#define	TLBHI_ASID_MASK		(0xff)
#if defined(__mips_n64)
#define	TLBHI_R_SHIFT		62
#define	TLBHI_R_USER		(0x00UL << TLBHI_R_SHIFT)
#define	TLBHI_R_SUPERVISOR	(0x01UL << TLBHI_R_SHIFT)
#define	TLBHI_R_KERNEL		(0x03UL << TLBHI_R_SHIFT)
#define	TLBHI_R_MASK		(0x03UL << TLBHI_R_SHIFT)
#define	TLBHI_VA_R(va)		((va) & TLBHI_R_MASK)
#define	TLBHI_FILL_SHIFT	40
#define	TLBHI_VPN2_SHIFT	(TLB_PAGE_SHIFT + 1)
#define	TLBHI_VPN2_MASK		(((~((1UL << TLBHI_VPN2_SHIFT) - 1)) << (63 - TLBHI_FILL_SHIFT)) >> (63 - TLBHI_FILL_SHIFT))
#define	TLBHI_VA_TO_VPN2(va)	((va) & TLBHI_VPN2_MASK)
#define	TLBHI_ENTRY(va, asid)	((TLBHI_VA_R((va))) /* Region. */ | \
				 (TLBHI_VA_TO_VPN2((va))) /* VPN2. */ | \
				 ((asid) & TLBHI_ASID_MASK))
#else /* !defined(__mips_n64) */
#define	TLBHI_PAGE_MASK		(2 * PAGE_SIZE - 1)
#define	TLBHI_ENTRY(va, asid)	(((va) & ~TLBHI_PAGE_MASK) | ((asid) & TLBHI_ASID_MASK))
#endif /* defined(__mips_n64) */

/*
 * TLB flags managed in hardware:
 * 	C:	Cache attribute.
 * 	D:	Dirty bit.  This means a page is writable.  It is not
 * 		set at first, and a write is trapped, and the dirty
 * 		bit is set.  See also PTE_RO.
 * 	V:	Valid bit.  Obvious, isn't it?
 * 	G:	Global bit.  This means that this mapping is present
 * 		in EVERY address space, and to ignore the ASID when
 * 		it is matched.
 */
#define	PTE_C(attr)		((attr & 0x07) << 3)
#define	PTE_C_MASK		(PTE_C(0x07))
#define	PTE_C_UNCACHED		(PTE_C(MIPS_CCA_UNCACHED))
#define	PTE_C_CACHE		(PTE_C(MIPS_CCA_CACHED))
#define	PTE_C_WC		(PTE_C(MIPS_CCA_WC))
#define	PTE_D			0x04
#define	PTE_V			0x02
#define	PTE_G			0x01

/*
 * VM flags managed in software:
 * 	RO:	Read only.  Never set PTE_D on this page, and don't
 * 		listen to requests to write to it.
 * 	W:	Wired.  ???
 *	MANAGED:Managed.  This PTE maps a managed page.
 *
 * These bits should not be written into the TLB, so must first be masked out
 * explicitly in C, or using CLEAR_PTE_SWBITS() in assembly.
 */
#define	PTE_RO			((pt_entry_t)0x01 << TLBLO_SWBITS_SHIFT)
#define	PTE_W			((pt_entry_t)0x02 << TLBLO_SWBITS_SHIFT)
#define	PTE_MANAGED		((pt_entry_t)0x04 << TLBLO_SWBITS_SHIFT)

/*
 * PTE management functions for bits defined above.
 */
#define	pte_clear(pte, bit)	(*(pte) &= ~(bit))
#define	pte_set(pte, bit)	(*(pte) |= (bit))
#define	pte_test(pte, bit)	((*(pte) & (bit)) == (bit))
#define	pte_cache_bits(pte)	((*(pte) >> 3) & 0x07)

/* Assembly support for PTE access*/
#ifdef LOCORE
#if defined(__mips_n64) || defined(__mips_n32) /*  PHYSADDR_64_BIT */
#define	PTESHIFT		3
#define	PTE2MASK		0xff0	/* for the 2-page lo0/lo1 */
#define	PTEMASK			0xff8
#define	PTESIZE			8
#define	PTE_L			ld
#define	PTE_MTC0		dmtc0
#define	CLEAR_PTE_SWBITS(pr)
#else
#define	PTESHIFT		2
#define	PTE2MASK		0xff8	/* for the 2-page lo0/lo1 */
#define	PTEMASK			0xffc
#define	PTESIZE			4
#define	PTE_L			lw
#define	PTE_MTC0		mtc0
#define	CLEAR_PTE_SWBITS(r)	LONG_SLL r, TLBLO_SWBITS_CLEAR_SHIFT; LONG_SRL r, TLBLO_SWBITS_CLEAR_SHIFT /* remove swbits */
#endif /* defined(__mips_n64) || defined(__mips_n32) */

#if defined(__mips_n64)
#define	PTRSHIFT		3
#define	PDEPTRMASK		0xff8
#else
#define	PTRSHIFT		2
#define	PDEPTRMASK		0xffc
#endif

#endif /* LOCORE */

/* PageMask Register (CP0 Register 5, Select 0) Values */
#define	MIPS3_PGMASK_MASKX	0x00001800
#define	MIPS3_PGMASK_4K		0x00000000
#define	MIPS3_PGMASK_16K	0x00006000
#define	MIPS3_PGMASK_64K	0x0001e000
#define	MIPS3_PGMASK_256K	0x0007e000
#define	MIPS3_PGMASK_1M		0x001fe000
#define	MIPS3_PGMASK_4M		0x007fe000
#define	MIPS3_PGMASK_16M	0x01ffe000
#define	MIPS3_PGMASK_64M	0x07ffe000
#define	MIPS3_PGMASK_256M	0x1fffe000

#endif /* !_MACHINE_PTE_H_ */
