/*	$OpenBSD: mmu.h,v 1.3 2022/02/21 12:16:55 jsg Exp $	*/
/*	$NetBSD: mmu.h,v 1.9 2006/03/04 01:55:03 uwe Exp $	*/

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

#ifndef _SH_MMU_H_
#define	_SH_MMU_H_

/*
 * Initialize routines.
 *	sh_mmu_init		Assign function vector. Don't access hardware.
 *				Call as early as possible.
 *	sh_mmu_start		Reset TLB entry, set default ASID, and start to
 *				translate addresses.
 *				Call after exception vector was installed.
 *
 * TLB access ops.
 *	sh_tlb_invalidate_addr	invalidate TLB entries for given
 *				virtual addr with ASID.
 *	sh_tlb_invalidate_asid	invalidate TLB entries for given ASID.
 *	sh_tlb_invalidate_all	invalidate all non-wired TLB entries.
 *	sh_tlb_set_asid		set ASID.
 *	sh_tlb_update		load new PTE to TLB.
 *
 */

void sh_mmu_init(void);
void sh_mmu_information(void);
void sh_tlb_set_asid(int);

#ifdef SH3
void sh3_mmu_start(void);
void sh3_tlb_invalidate_addr(int, vaddr_t);
void sh3_tlb_invalidate_asid(int);
void sh3_tlb_invalidate_all(void);
void sh3_tlb_update(int, vaddr_t, uint32_t);
#endif

#ifdef SH4
void sh4_mmu_start(void);
void sh4_tlb_invalidate_addr(int, vaddr_t);
void sh4_tlb_invalidate_asid(int);
void sh4_tlb_invalidate_all(void);
void sh4_tlb_update(int, vaddr_t, uint32_t);
#endif


#if defined(SH3) && defined(SH4)
extern uint32_t __sh_PTEH;

extern void (*__sh_mmu_start)(void);
extern void (*__sh_tlb_invalidate_addr)(int, vaddr_t);
extern void (*__sh_tlb_invalidate_asid)(int);
extern void (*__sh_tlb_invalidate_all)(void);
extern void (*__sh_tlb_update)(int, vaddr_t, uint32_t);

#define	sh_mmu_start()			(*__sh_mmu_start)()
#define	sh_tlb_invalidate_addr(a, va)	(*__sh_tlb_invalidate_addr)(a, va)
#define	sh_tlb_invalidate_asid(a)	(*__sh_tlb_invalidate_asid)(a)
#define	sh_tlb_invalidate_all()		(*__sh_tlb_invalidate_all)()
#define	sh_tlb_update(a, va, pte)	(*__sh_tlb_update)(a, va, pte)

#elif defined(SH3)

#define	sh_mmu_start()			sh3_mmu_start()
#define	sh_tlb_invalidate_addr(a, va)	sh3_tlb_invalidate_addr(a, va)
#define	sh_tlb_invalidate_asid(a)	sh3_tlb_invalidate_asid(a)
#define	sh_tlb_invalidate_all()		sh3_tlb_invalidate_all()
#define	sh_tlb_update(a, va, pte)	sh3_tlb_update(a, va, pte)

#elif defined(SH4)

#define	sh_mmu_start()			sh4_mmu_start()
#define	sh_tlb_invalidate_addr(a, va)	sh4_tlb_invalidate_addr(a, va)
#define	sh_tlb_invalidate_asid(a)	sh4_tlb_invalidate_asid(a)
#define	sh_tlb_invalidate_all()		sh4_tlb_invalidate_all()
#define	sh_tlb_update(a, va, pte)	sh4_tlb_update(a, va, pte)

#endif

#endif /* !_SH_MMU_H_ */
