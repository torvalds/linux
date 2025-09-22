/*	$OpenBSD: mmu.c,v 1.4 2010/06/02 05:35:17 jasper Exp $	*/
/*	$NetBSD: mmu.c,v 1.15 2006/02/12 02:30:55 uwe Exp $	*/

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

#include <sh/mmu.h>
#include <sh/mmu_sh3.h>
#include <sh/mmu_sh4.h>

#if defined(SH3) && defined(SH4)
void (*__sh_mmu_start)(void);
void (*__sh_tlb_invalidate_addr)(int, vaddr_t);
void (*__sh_tlb_invalidate_asid)(int);
void (*__sh_tlb_invalidate_all)(void);
void (*__sh_tlb_update)(int, vaddr_t, uint32_t);
#endif /* SH3 && SH4 */

void
sh_mmu_init(void)
{
	/*
	 * Assign function hooks but only if both SH3 and SH4 are defined.
	 * They are called directly otherwise.  See <sh3/mmu.h>.
	 */
#if defined(SH3) && defined(SH4)
	if (CPU_IS_SH3) {
		__sh_mmu_start = sh3_mmu_start;
		__sh_tlb_invalidate_addr = sh3_tlb_invalidate_addr;
		__sh_tlb_invalidate_asid = sh3_tlb_invalidate_asid;
		__sh_tlb_invalidate_all = sh3_tlb_invalidate_all;
		__sh_tlb_update = sh3_tlb_update;
	}
	else if (CPU_IS_SH4) {
		__sh_mmu_start = sh4_mmu_start;
		__sh_tlb_invalidate_addr = sh4_tlb_invalidate_addr;
		__sh_tlb_invalidate_asid = sh4_tlb_invalidate_asid;
		__sh_tlb_invalidate_all = sh4_tlb_invalidate_all;
		__sh_tlb_update = sh4_tlb_update;
	}
#endif /* SH3 && SH4 */
}

void
sh_mmu_information(void)
{
#ifdef DEBUG
	uint32_t r;
#ifdef SH3
	if (CPU_IS_SH3) {
		printf("cpu0: 4-way set-associative 128 TLB entries\n");
		r = _reg_read_4(SH3_MMUCR);
		printf("cpu0: %s mode, %s virtual storage mode\n",
		    r & SH3_MMUCR_IX ? "ASID+VPN" : "VPN",
		    r & SH3_MMUCR_SV ? "single" : "multiple");
	}
#endif
#ifdef SH4
	if (CPU_IS_SH4) {
		unsigned int urb;
		printf("cpu0: fully-associative 4 ITLB, 64 UTLB entries\n");
		r = _reg_read_4(SH4_MMUCR);
		urb = (r & SH4_MMUCR_URB_MASK) >> SH4_MMUCR_URB_SHIFT;
		printf("cpu0: %s virtual storage mode, SQ access: kernel%s, ",
		    r & SH3_MMUCR_SV ? "single" : "multiple",
		    r & SH4_MMUCR_SQMD ? "" : "/user");
		printf("wired %d\n",
		    urb ? 64 - urb : 0);
	}
#endif
#endif /* DEBUG */
}

void
sh_tlb_set_asid(int asid)
{
	_reg_write_4(SH_(PTEH), asid);
}
