/* $OpenBSD: cpufunc_asm_armv7.S,v 1.19 2022/12/08 01:25:44 guenther Exp $ */
/*
 * Copyright (c) 2008 Dale Rahn <drahn@openbsd.org>
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

#include <machine/asm.h>
#include <arm/armreg.h>
#include <arm/sysreg.h>

ENTRY(armv7_cpu_sleep)
	wfi
	mov	pc, lr

ENTRY(armv7_drain_writebuf)
	dsb	sy
	isb	sy
	mov	pc, lr

/*
 * Function to read the MPCore base address
 */
ENTRY(armv7_periphbase)
	mrc	CP15_CBAR(r0)	
	mov	pc, lr

/* Suitable default for a uniprocessor kernel. */
#define TTBR_DEFAULT	(TTBR_IRGN_WBNWA | TTBR_RGN_WBNWA)

/*
 * Functions to set the MMU Translation Table Base register
 */
ENTRY(armv7_setttb)
	mcr	CP15_ICIALLU		/* Flush I cache */
	dsb	sy
	isb	sy

	orr	r0, r0, #TTBR_DEFAULT
	mcr	CP15_TTBR0(r0)		/* load new TTB */
	mcr	CP15_TLBIALL(r0)	/* invalidate unified TLB */
	dsb	sy
	isb	sy

	mov	pc, lr

/*
 * TLB functions
 */
ENTRY(armv7_tlb_flushID_SE)
	mcr	CP15_TLBIMVA(r0)	/* flush unified tlb single entry */
	mcr	CP15_BPIMVA		/* flush va from BP */
	dsb	sy
	isb	sy
	mov	pc, lr

ENTRY(armv7_tlb_flushID)
	mcr	CP15_TLBIALL(r0)	/* flush unified tlb */
	mcr	CP15_BPIALL		/* Flush BP cache */
	dsb	sy
	isb	sy
	mov	pc, lr

ENTRY(armv7_tlb_flushD_SE)
	mcr	CP15_TLBIMVA(r0)	/* flush unified tlb single entry */
	dsb	sy
	isb	sy
	mov	pc, lr

ENTRY(armv7_tlb_flushD)
	mcr	CP15_TLBIALL(r0)	/* flush unified tlb */
	dsb	sy
	isb	sy
	mov	pc, lr


/*
 * Cache operations.  For the entire cache we use the set/index
 * operations.
 */
.Larmv7_dcache_line_size:
	.word	arm_dcache_min_line_size
.Larmv7_icache_line_size:
	.word	arm_icache_min_line_size
.Larmv7_idcache_line_size:
	.word	arm_idcache_min_line_size

	s_max	.req r0
	i_max	.req r1
	s_inc	.req r2
	i_inc	.req r3
ENTRY(armv7_icache_sync_range)
	ldr	ip, .Larmv7_icache_line_size
	ldr	ip, [ip]
	sub	r3, ip, #1
	and	r2, r0, r3
	add	r1, r1, r2
	bic	r0, r0, r3
1:
	mcr	CP15_DCCMVAU(r0)	/* Clean D cache SE with VA to PoU */
	mcr	CP15_ICIMVAU(r0)	/* Invalidate I cache SE with VA */
	add	r0, r0, ip
	subs	r1, r1, ip
	bhi	1b
	mcr	CP15_BPIALL		/* Flush BP cache */
	dsb	sy
	isb	sy
	mov	pc, lr

ENTRY(armv7_icache_sync_all)
.Larmv7_icache_sync_all:
	/*
	 * We assume that the code here can never be out of sync with the
	 * dcache, so that we can safely flush the Icache and fall through
	 * into the Dcache cleaning code.
	 */
	mcr	CP15_ICIALLU		/* Flush I cache */
	isb	sy
	mov	pc, lr

ENTRY(armv7_dcache_wb_range)
	ldr	ip, .Larmv7_dcache_line_size
	ldr	ip, [ip]
	sub	r3, ip, #1
	and	r2, r0, r3
	add	r1, r1, r2
	bic	r0, r0, r3
1:
	mcr	CP15_DCCMVAC(r0)	/* Clean D cache SE with VA */
	add	r0, r0, ip
	subs	r1, r1, ip
	bhi	1b
	dsb	sy
	isb	sy
	mov	pc, lr

ENTRY(armv7_idcache_wbinv_range)
	ldr	ip, .Larmv7_idcache_line_size
	ldr	ip, [ip]
	sub	r3, ip, #1
	and	r2, r0, r3
	add	r1, r1, r2
	bic	r0, r0, r3
1:
	mcr	CP15_DCCMVAU(r0)	/* Clean D cache SE with VA to PoU */
	mcr	CP15_ICIMVAU(r0)	/* Invalidate I cache SE with VA */
	mcr	CP15_DCCIMVAC(r0)	/* Purge D cache SE with VA */
	add	r0, r0, ip
	subs	r1, r1, ip
	bhi	1b
	mcr	CP15_BPIALL		/* Flush BP cache */
	dsb	sy
	isb	sy
	mov	pc, lr

ENTRY(armv7_dcache_wbinv_range)
	ldr	ip, .Larmv7_dcache_line_size
	ldr	ip, [ip]
	sub	r3, ip, #1
	and	r2, r0, r3
	add	r1, r1, r2
	bic	r0, r0, r3
1:
	mcr	CP15_DCCMVAU(r0)	/* Clean D cache SE with VA to PoU */
	mcr	CP15_ICIMVAU(r0)	/* Invalidate I cache SE with VA */
	mcr	CP15_DCCIMVAC(r0)	/* Purge D cache SE with VA */
	add	r0, r0, ip
	subs	r1, r1, ip
	bhi	1b
	dsb	sy
	isb	sy
	mov	pc, lr

ENTRY(armv7_dcache_inv_range)
	ldr	ip, .Larmv7_dcache_line_size
	ldr	ip, [ip]
	sub	r3, ip, #1
	and	r2, r0, r3
	add	r1, r1, r2
	bic	r0, r0, r3
1:
	mcr	CP15_DCCMVAU(r0)	/* Clean D cache SE with VA to PoU */
	mcr	CP15_ICIMVAU(r0)	/* Invalidate I cache SE with VA */
	mcr	CP15_DCIMVAC(r0)	/* Invalidate D cache SE with VA */
	add	r0, r0, ip
	subs	r1, r1, ip
	bhi	1b
	dsb	sy
	isb	sy
	mov	pc, lr


/*
 * BTB functions.
 */
ENTRY(armv7_flush_bp)
	mcr	CP15_BPIALL
	mov	pc, lr

ENTRY(cortex_a15_flush_bp)
	mcr	CP15_ICIALLU		/* Heavy hammer; BPIALL is a no-op */
	mov	pc, lr

/*
 * Context switch.
 *
 * These is the CPU-specific parts of the context switcher cpu_switch()
 * These functions actually perform the TTB reload.
 *
 * NOTE: Special calling convention
 *	r1, r4-r13 must be preserved
 */
ENTRY(armv7_context_switch)
	/*
	 * We can assume that the caches will only contain kernel addresses
	 * at this point.  So no need to flush them again.
	 */
	mcr	CP15_ICIALLU		/* Flush I cache */
	dsb	sy
	isb	sy

	orr	r0, r0, #TTBR_DEFAULT
	mcr	CP15_TTBR0(r0)		/* set the new TTB */
	mcr	CP15_TLBIALL(r0)	/* and flush the unified tlb */
	dsb	sy
	isb	sy
	mov	pc, lr

/* XXX The following macros should probably be moved to asm.h */
#define _DATA_OBJECT(x) .globl x; .type x,_ASM_TYPE_OBJECT; x:
#define C_OBJECT(x)	_DATA_OBJECT(x)

	.align 2
C_OBJECT(armv7_dcache_sets_max)
	.word	0
C_OBJECT(armv7_dcache_index_max)
	.word	0
C_OBJECT(armv7_dcache_sets_inc)
	.word	0
C_OBJECT(armv7_dcache_index_inc)
	.word	0
