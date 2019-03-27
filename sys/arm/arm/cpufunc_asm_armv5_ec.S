/*	$NetBSD: cpufunc_asm_armv5_ec.S,v 1.1 2007/01/06 00:50:54 christos Exp $	*/

/*
 * Copyright (c) 2002, 2005 ARM Limited
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
 * 3. The name of the company may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * ARMv5 assembly functions for manipulating caches.
 * These routines can be used by any core that supports both the set/index
 * operations and the test and clean operations for efficiently cleaning the
 * entire DCache.  If a core does not have the test and clean operations, but
 * does have the set/index operations, use the routines in cpufunc_asm_armv5.S.
 * This source was derived from that file.
 */

#include <machine/asm.h>
__FBSDID("$FreeBSD$");

#ifndef ELF_TRAMPOLINE
/*
 * Functions to set the MMU Translation Table Base register
 *
 * We need to clean and flush the cache as it uses virtual
 * addresses that are about to change.
 */
ENTRY(armv5_ec_setttb)
	/*
	 * Some other ARM ports save registers on the stack, call the
	 * idcache_wbinv_all function and then restore the registers from the
	 * stack before setting the TTB.  I observed that this caused a
	 * problem when the old and new translation table entries' buffering
	 * bits were different.  If I saved the registers in other registers
	 * or invalidated the caches when I returned from idcache_wbinv_all,
	 * it worked fine.  If not, I ended up executing at an invalid PC.
	 * For armv5_ec_settb, the idcache_wbinv_all is simple enough, I just
	 * do it directly and entirely avoid the problem.
	 */
	mcr	p15, 0, r0, c7, c5, 0	/* Invalidate ICache */
1:	mrc	p15, 0, APSR_nzcv, c7, c14, 3	/* Test, clean and invalidate DCache */
	bne	1b			/* More to do? */
	mcr	p15, 0, r0, c7, c10, 4	/* drain the write buffer */

	mcr	p15, 0, r0, c2, c0, 0	/* load new TTB */

	mcr	p15, 0, r0, c8, c7, 0	/* invalidate I+D TLBs */
	RET
END(armv5_ec_setttb)

/*
 * Cache operations.  For the entire cache we use the enhanced cache
 * operations.
 */

ENTRY_NP(armv5_ec_icache_sync_range)
	ldr	ip, .Larmv5_ec_line_size
	cmp	r1, #0x4000
	bcs	.Larmv5_ec_icache_sync_all
	ldr	ip, [ip]
	sub	r1, r1, #1		/* Don't overrun */
	sub	r3, ip, #1
	and	r2, r0, r3
	add	r1, r1, r2
	bic	r0, r0, r3
1:
	mcr	p15, 0, r0, c7, c5, 1	/* Invalidate I cache SE with VA */
	mcr	p15, 0, r0, c7, c10, 1	/* Clean D cache SE with VA */
	add	r0, r0, ip
	subs	r1, r1, ip
	bpl	1b
	mcr	p15, 0, r0, c7, c10, 4	/* drain the write buffer */
	RET

.Larmv5_ec_icache_sync_all:
	/*
	 * We assume that the code here can never be out of sync with the
	 * dcache, so that we can safely flush the Icache and fall through
	 * into the Dcache cleaning code.
	 */
	mcr	p15, 0, r0, c7, c5, 0	/* Flush I cache */
	/* Fall through to clean Dcache. */

.Larmv5_ec_dcache_wb:
1:
	mrc	p15, 0, APSR_nzcv, c7, c10, 3	/* Test and clean (don't invalidate) */
	bne	1b			/* More to do? */
	mcr	p15, 0, r0, c7, c10, 4	/* drain the write buffer */
	RET
END(armv5_ec_icache_sync_range)

.Larmv5_ec_line_size:
	.word	_C_LABEL(arm_pdcache_line_size)

ENTRY(armv5_ec_dcache_wb_range)
	ldr	ip, .Larmv5_ec_line_size
	cmp	r1, #0x4000
	bcs	.Larmv5_ec_dcache_wb
	ldr	ip, [ip]
	sub	r1, r1, #1		/* Don't overrun */
	sub	r3, ip, #1
	and	r2, r0, r3
	add	r1, r1, r2
	bic	r0, r0, r3
1:
	mcr	p15, 0, r0, c7, c10, 1	/* Clean D cache SE with VA */
	add	r0, r0, ip
	subs	r1, r1, ip
	bpl	1b
	mcr	p15, 0, r0, c7, c10, 4	/* drain the write buffer */
	RET
END(armv5_ec_dcache_wb_range)

ENTRY(armv5_ec_dcache_wbinv_range)
	ldr	ip, .Larmv5_ec_line_size
	cmp	r1, #0x4000
	bcs	.Larmv5_ec_dcache_wbinv_all
	ldr	ip, [ip]
	sub	r1, r1, #1		/* Don't overrun */
	sub	r3, ip, #1
	and	r2, r0, r3
	add	r1, r1, r2
	bic	r0, r0, r3
1:
	mcr	p15, 0, r0, c7, c14, 1	/* Purge D cache SE with VA */
	add	r0, r0, ip
	subs	r1, r1, ip
	bpl	1b
	mcr	p15, 0, r0, c7, c10, 4	/* drain the write buffer */
	RET
END(armv5_ec_dcache_wbinv_range)

/*
 * Note, we must not invalidate everything.  If the range is too big we
 * must use wb-inv of the entire cache.
 */
ENTRY(armv5_ec_dcache_inv_range)
	ldr	ip, .Larmv5_ec_line_size
	cmp	r1, #0x4000
	bcs	.Larmv5_ec_dcache_wbinv_all
	ldr	ip, [ip]
	sub	r1, r1, #1		/* Don't overrun */
	sub	r3, ip, #1
	and	r2, r0, r3
	add	r1, r1, r2
	bic	r0, r0, r3
1:
	mcr	p15, 0, r0, c7, c6, 1	/* Invalidate D cache SE with VA */
	add	r0, r0, ip
	subs	r1, r1, ip
	bpl	1b
	mcr	p15, 0, r0, c7, c10, 4	/* drain the write buffer */
	RET
END(armv5_ec_dcache_inv_range)

ENTRY(armv5_ec_idcache_wbinv_range)
	ldr	ip, .Larmv5_ec_line_size
	cmp	r1, #0x4000
	bcs	.Larmv5_ec_idcache_wbinv_all
	ldr	ip, [ip]
	sub	r1, r1, #1		/* Don't overrun */
	sub	r3, ip, #1
	and	r2, r0, r3
	add	r1, r1, r2
	bic	r0, r0, r3
1:
	mcr	p15, 0, r0, c7, c5, 1	/* Invalidate I cache SE with VA */
	mcr	p15, 0, r0, c7, c14, 1	/* Purge D cache SE with VA */
	add	r0, r0, ip
	subs	r1, r1, ip
	bpl	1b
	mcr	p15, 0, r0, c7, c10, 4	/* drain the write buffer */
	RET
END(armv5_ec_idcache_wbinv_range)
#endif /* !ELF_TRAMPOLINE */

ENTRY_NP(armv5_ec_idcache_wbinv_all)
.Larmv5_ec_idcache_wbinv_all:
	/*
	 * We assume that the code here can never be out of sync with the
	 * dcache, so that we can safely flush the Icache and fall through
	 * into the Dcache purging code.
	 */
	mcr	p15, 0, r0, c7, c5, 0	/* Invalidate ICache */
	/* Fall through to purge Dcache. */
END(armv5_ec_idcache_wbinv_all)

#ifndef ELF_TRAMPOLINE
ENTRY(armv5_ec_dcache_wbinv_all)
.Larmv5_ec_dcache_wbinv_all:
1:	mrc	p15, 0, APSR_nzcv, c7, c14, 3	/* Test, clean and invalidate DCache */
	bne	1b			/* More to do? */
	mcr	p15, 0, r0, c7, c10, 4	/* drain the write buffer */
	RET
END(armv5_ec_dcache_wbinv_all)
#endif
