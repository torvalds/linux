/*	$OpenBSD: mmu_sh3.c,v 1.3 2016/03/05 17:16:33 tobiasu Exp $	*/
/*	$NetBSD: mmu_sh3.c,v 1.11 2006/03/04 01:13:35 uwe Exp $	*/

/*-
 * Copyright (c) 2002 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by UCHIYAMA Yasushi.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <sh/pte.h>	/* OpenBSD/sh specific PTE */
#include <sh/mmu.h>
#include <sh/mmu_sh3.h>

void
sh3_mmu_start(void)
{
	/* Zero clear all TLB entry */
	sh3_tlb_invalidate_all();

	/* Set current ASID to 0 */
	sh_tlb_set_asid(0);

	_reg_write_4(SH3_MMUCR, SH3_MMUCR_AT | SH3_MMUCR_TF);
}

void
sh3_tlb_invalidate_addr(int asid, vaddr_t va)
{
	uint32_t a, d;
	int w;

	d = (va & SH3_MMUAA_D_VPN_MASK_4K) | asid;  /* 4K page */
	va = va & SH3_MMU_VPN_MASK;   /* [16:12] entry index */

	/* Probe entry and invalidate it. */
	for (w = 0; w < SH3_MMU_WAY; w++) {
		a = va | (w << SH3_MMU_WAY_SHIFT); /* way [9:8] */
		if ((_reg_read_4(SH3_MMUAA | a) &
		    (SH3_MMUAA_D_VPN_MASK_4K | SH3_MMUAA_D_ASID_MASK)) == d) {
			_reg_write_4(SH3_MMUAA | a, 0);
			break;
		}
	}
}

void
sh3_tlb_invalidate_asid(int asid)
{
	uint32_t aw, a;
	int e, w;

	/* Invalidate entry attribute to ASID */
	for (w = 0; w < SH3_MMU_WAY; w++) {
		aw = (w << SH3_MMU_WAY_SHIFT);
		for (e = 0; e < SH3_MMU_ENTRY; e++) {
			a = aw | (e << SH3_MMU_VPN_SHIFT);
			if ((_reg_read_4(SH3_MMUAA | a) &
			    SH3_MMUAA_D_ASID_MASK) == asid) {
				_reg_write_4(SH3_MMUAA | a, 0);
			}
		}
	}
}

void
sh3_tlb_invalidate_all(void)
{
	uint32_t aw, a;
	int e, w;

	/* Zero clear all TLB entry to avoid unexpected VPN match. */
	for (w = 0; w < SH3_MMU_WAY; w++) {
		aw = (w << SH3_MMU_WAY_SHIFT);
		for (e = 0; e < SH3_MMU_ENTRY; e++) {
			a = aw | (e << SH3_MMU_VPN_SHIFT);
			_reg_write_4(SH3_MMUAA | a, 0);
			_reg_write_4(SH3_MMUDA | a, 0);
		}
	}
}

void
sh3_tlb_update(int asid, vaddr_t va, uint32_t pte)
{
	uint32_t oasid;

	KDASSERT(asid < 0x100 && (pte & ~PGOFSET) != 0 && va != 0);

	/* Save old ASID */
	oasid = _reg_read_4(SH3_PTEH) & SH3_PTEH_ASID_MASK;

	/* Invalidate old entry (if any) */
	sh3_tlb_invalidate_addr(asid, va);

	/* Load new entry */
	_reg_write_4(SH3_PTEH, (va & ~PGOFSET) | asid);
	_reg_write_4(SH3_PTEL, pte & PG_HW_BITS);
	__asm volatile("ldtlb");

	/* Restore old ASID */
	if (asid != oasid)
		_reg_write_4(SH3_PTEH, oasid);
}
