/*	$OpenBSD: mmu_sh4.c,v 1.3 2016/03/05 17:16:33 tobiasu Exp $	*/
/*	$NetBSD: mmu_sh4.c,v 1.11 2006/03/04 01:13:35 uwe Exp $	*/

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
#include <sh/mmu_sh4.h>

#define	SH4_MMU_HAZARD	__asm volatile("nop;nop;nop;nop;nop;nop;nop;nop;")

static inline void __sh4_itlb_invalidate_all(void);

static inline void
__sh4_itlb_invalidate_all(void)
{
	_reg_write_4(SH4_ITLB_AA, 0);
	_reg_write_4(SH4_ITLB_AA | (1 << SH4_ITLB_E_SHIFT), 0);
	_reg_write_4(SH4_ITLB_AA | (2 << SH4_ITLB_E_SHIFT), 0);
	_reg_write_4(SH4_ITLB_AA | (3 << SH4_ITLB_E_SHIFT), 0);
}

void
sh4_mmu_start(void)
{
	/* Zero clear all TLB entry */
	_reg_write_4(SH4_MMUCR, 0);	/* zero wired entry */
	sh_tlb_invalidate_all();

	/* Set current ASID to 0 */
	sh_tlb_set_asid(0);

	/*
	 * User can't access store queue
	 * make wired entry for u-area.
	 */
	_reg_write_4(SH4_MMUCR, SH4_MMUCR_AT | SH4_MMUCR_TI | SH4_MMUCR_SQMD |
	    (SH4_UTLB_ENTRY - UPAGES) << SH4_MMUCR_URB_SHIFT);

	SH4_MMU_HAZARD;
}

void
sh4_tlb_invalidate_addr(int asid, vaddr_t va)
{
	uint32_t pteh;
	int s;

	va &= SH4_PTEH_VPN_MASK;
	s = _cpu_exception_suspend();

	/* Save current ASID */
	pteh = _reg_read_4(SH4_PTEH);
	/* Set ASID for associative write */
	_reg_write_4(SH4_PTEH, asid);

	/* Associative write(UTLB/ITLB). not required ITLB invalidate. */
	RUN_P2;
	_reg_write_4(SH4_UTLB_AA | SH4_UTLB_A, va); /* Clear D, V */
	RUN_P1;
	/* Restore ASID */
	_reg_write_4(SH4_PTEH, pteh);

	_cpu_exception_resume(s);
}

void
sh4_tlb_invalidate_asid(int asid)
{
	uint32_t a;
	int e, s;

	s = _cpu_exception_suspend();
	/* Invalidate entry attribute to ASID */
	RUN_P2;
	for (e = 0; e < SH4_UTLB_ENTRY; e++) {
		a = SH4_UTLB_AA | (e << SH4_UTLB_E_SHIFT);
		if ((_reg_read_4(a) & SH4_UTLB_AA_ASID_MASK) == asid)
			_reg_write_4(a, 0);
	}

	__sh4_itlb_invalidate_all();
	RUN_P1;
	_cpu_exception_resume(s);
}

void
sh4_tlb_invalidate_all(void)
{
	uint32_t a;
	int e, eend, s;

	s = _cpu_exception_suspend();
	/* If non-wired entry limit is zero, clear all entry. */
	a = _reg_read_4(SH4_MMUCR) & SH4_MMUCR_URB_MASK;
	eend = a ? (a >> SH4_MMUCR_URB_SHIFT) : SH4_UTLB_ENTRY;

	RUN_P2;
	for (e = 0; e < eend; e++) {
		a = SH4_UTLB_AA | (e << SH4_UTLB_E_SHIFT);
		_reg_write_4(a, 0);
		a = SH4_UTLB_DA1 | (e << SH4_UTLB_E_SHIFT);
		_reg_write_4(a, 0);
	}
	__sh4_itlb_invalidate_all();
	_reg_write_4(SH4_ITLB_DA1, 0);
	_reg_write_4(SH4_ITLB_DA1 | (1 << SH4_ITLB_E_SHIFT), 0);
	_reg_write_4(SH4_ITLB_DA1 | (2 << SH4_ITLB_E_SHIFT), 0);
	_reg_write_4(SH4_ITLB_DA1 | (3 << SH4_ITLB_E_SHIFT), 0);
	RUN_P1;
	_cpu_exception_resume(s);
}

void
sh4_tlb_update(int asid, vaddr_t va, uint32_t pte)
{
	uint32_t oasid;
	uint32_t ptel;
	int s;

	KDASSERT(asid < 0x100 && (pte & ~PGOFSET) != 0 && va != 0);

	s = _cpu_exception_suspend();
	/* Save old ASID */
	oasid = _reg_read_4(SH4_PTEH) & SH4_PTEH_ASID_MASK;

	/* Invalidate old entry (if any) */
	sh4_tlb_invalidate_addr(asid, va);

	_reg_write_4(SH4_PTEH, asid);
	/* Load new entry */
	_reg_write_4(SH4_PTEH, (va & ~PGOFSET) | asid);
	ptel = pte & PG_HW_BITS;
	if (pte & _PG_PCMCIA) {
		_reg_write_4(SH4_PTEA,
		    (pte >> _PG_PCMCIA_SHIFT) & SH4_PTEA_SA_MASK);
	} else {
		_reg_write_4(SH4_PTEA, 0);
	}
	_reg_write_4(SH4_PTEL, ptel);
	__asm volatile("ldtlb; nop");

	/* Restore old ASID */
	if (asid != oasid)
		_reg_write_4(SH4_PTEH, oasid);
	_cpu_exception_resume(s);
}
