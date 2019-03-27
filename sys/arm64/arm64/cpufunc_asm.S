/*-
 * Copyright (c) 2014 Robin Randhawa
 * Copyright (c) 2015 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Andrew Turner
 * under sponsorship from the FreeBSD Foundation
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
 */

#include <sys/errno.h>
#include <machine/asm.h>
#include <machine/param.h>

#include "assym.inc"

__FBSDID("$FreeBSD$");

/*
 * FIXME:
 * Need big.LITTLE awareness at some point.
 * Using arm64_p[id]cache_line_size may not be the best option.
 * Need better SMP awareness.
 */
	.text
	.align	2

.Lpage_mask:
	.word	PAGE_MASK

/*
 * Macro to handle the cache. This takes the start address in x0, length
 * in x1. It will corrupt x0, x1, x2, and x3.
 */
.macro cache_handle_range dcop = 0, ic = 0, icop = 0
.if \ic == 0
	ldr	x3, =dcache_line_size	/* Load the D cache line size */
.else
	ldr	x3, =idcache_line_size	/* Load the I & D cache line size */
.endif
	ldr	x3, [x3]
	sub	x4, x3, #1		/* Get the address mask */
	and	x2, x0, x4		/* Get the low bits of the address */
	add	x1, x1, x2		/* Add these to the size */
	bic	x0, x0, x4		/* Clear the low bit of the address */
1:
	dc	\dcop, x0
	dsb	ish
.if \ic != 0
	ic	\icop, x0
	dsb	ish
.endif
	add	x0, x0, x3		/* Move to the next line */
	subs	x1, x1, x3		/* Reduce the size */
	b.hi	1b			/* Check if we are done */
.if \ic != 0
	isb
.endif
.endm

ENTRY(arm64_nullop)
	ret
END(arm64_nullop)

/*
 * Generic functions to read/modify/write the internal coprocessor registers
 */

ENTRY(arm64_setttb)
	dsb	ish
	msr	ttbr0_el1, x0
	dsb	ish
	isb
	ret
END(arm64_setttb)

ENTRY(arm64_tlb_flushID)
	dsb	ishst
#ifdef SMP
	tlbi	vmalle1is
#else
	tlbi	vmalle1
#endif
	dsb	ish
	isb
	ret
END(arm64_tlb_flushID)

/*
 * void arm64_dcache_wb_range(vm_offset_t, vm_size_t)
 */
ENTRY(arm64_dcache_wb_range)
	cache_handle_range	dcop = cvac
	ret
END(arm64_dcache_wb_range)

/*
 * void arm64_dcache_wbinv_range(vm_offset_t, vm_size_t)
 */
ENTRY(arm64_dcache_wbinv_range)
	cache_handle_range	dcop = civac
	ret
END(arm64_dcache_wbinv_range)

/*
 * void arm64_dcache_inv_range(vm_offset_t, vm_size_t)
 *
 * Note, we must not invalidate everything.  If the range is too big we
 * must use wb-inv of the entire cache.
 */
ENTRY(arm64_dcache_inv_range)
	cache_handle_range	dcop = ivac
	ret
END(arm64_dcache_inv_range)

/*
 * void arm64_idcache_wbinv_range(vm_offset_t, vm_size_t)
 */
ENTRY(arm64_idcache_wbinv_range)
	cache_handle_range	dcop = civac, ic = 1, icop = ivau
	ret
END(arm64_idcache_wbinv_range)

/*
 * void arm64_icache_sync_range(vm_offset_t, vm_size_t)
 */
ENTRY(arm64_icache_sync_range)
	/*
	 * XXX Temporary solution - I-cache flush should be range based for
	 * PIPT cache or IALLUIS for VIVT or VIPT caches
	 */
/*	cache_handle_range	dcop = cvau, ic = 1, icop = ivau */
	cache_handle_range	dcop = cvau
	ic	ialluis
	dsb	ish
	isb
	ret
END(arm64_icache_sync_range)

/*
 * int arm64_icache_sync_range_checked(vm_offset_t, vm_size_t)
 */
ENTRY(arm64_icache_sync_range_checked)
	adr	x5, cache_maint_fault
	SET_FAULT_HANDLER(x5, x6)
	/* XXX: See comment in arm64_icache_sync_range */
	cache_handle_range	dcop = cvau
	ic	ialluis
	dsb	ish
	isb
	SET_FAULT_HANDLER(xzr, x6)
	mov	x0, #0
	ret
END(arm64_icache_sync_range_checked)

ENTRY(cache_maint_fault)
	SET_FAULT_HANDLER(xzr, x1)
	mov	x0, #EFAULT
	ret
END(cache_maint_fault)
