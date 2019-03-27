/*-
 * Copyright 2014 Svatopluk Kraus <onwahe@gmail.com>
 * Copyright 2014 Michal Meloun <meloun@miracle.cz>
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
#include "assym.inc"

#include <machine/asm.h>
#include <machine/asmacros.h>
#include <machine/armreg.h>
#include <machine/sysreg.h>

#define GET_PCB(tmp) \
	mrc	CP15_TPIDRPRW(tmp); \
	add	tmp, tmp, #(TD_PCB)

/*
 * Define cache functions used by startup code, which counts on the fact that
 * only r0-r3,r12 (ip) are modified and no stack space is used.  These functions
 * must be called with interrupts disabled.  Moreover, these work only with
 * caches integrated to CPU (accessible via CP15); systems with an external L2
 * cache controller such as a PL310 need separate calls to that device driver
 * to affect L2 caches.  This is not a factor during early kernel startup, as
 * any external L2 cache controller has not been enabled yet.
 */

/* Invalidate D cache to PoC. (aka all cache levels)*/
ASENTRY_NP(dcache_inv_poc_all)
#if __ARM_ARCH == 6
	mcr	CP15_DCIALL
	DSB
	bx	lr
#else
	mrc	CP15_CLIDR(r0)
	ands	r0, r0, #0x07000000
	mov	r0, r0, lsr #23		/* Get LoC 'naturally' aligned for */
	beq	4f			/* use in the CSSELR register below */

1:	sub	r0, #2
	mcr	CP15_CSSELR(r0)		/* set cache level */
	isb
	mrc	CP15_CCSIDR(r0)		/* read CCSIDR */

	ubfx	r2, r0, #13, #15	/* get num sets - 1 from CCSIDR */
	ubfx	r3, r0, #3, #10		/* get num ways - 1 from CCSIDR */
	clz	r1, r3			/* number of bits to MSB of way */
	lsl	r3, r3, r1		/* shift into position  */
	mov	ip, #1
	lsl	ip, ip, r1		/* ip now contains the way decr  */

	ubfx	r0, r0, #0, #3		/* get linesize from CCSIDR  */
	add	r0, r0, #4		/* apply bias  */
	lsl	r2, r2, r0		/* shift sets by log2(linesize)  */
	add	r3, r3, r2		/* merge numsets - 1 with numways - 1 */
	sub	ip, ip, r2		/* subtract numsets - 1 from way decr */
	mov	r1, #1
	lsl	r1, r1, r0		/* r1 now contains the set decr */
	mov	r2, ip			/* r2 now contains set way decr */

	/* r3 = ways/sets, r2 = way decr, r1 = set decr, r0 and ip are free */
2:	mcr	CP15_DCISW(r3)		/* invalidate line */
	movs	r0, r3			/* get current way/set */
	beq	3f			/* at 0 means we are done */
	movs	r0, r0, lsl #10		/* clear way bits leaving only set bits*/
	subne	r3, r3, r1		/* non-zero?, decrement set */
	subeq	r3, r3, r2		/* zero?, decrement way  and restore set count */
	b	2b

3:
	mrc	CP15_CSSELR(r0)		/* get cache level */
	teq	r0, #0
	bne	1b

4:	dsb				/* wait for stores to finish */
	mov	r0, #0
	mcr	CP15_CSSELR(r0)
	isb
	bx	lr
#endif /* __ARM_ARCH == 6 */
END(dcache_inv_poc_all)

/* Invalidate D cache to PoU. (aka L1 cache only)*/
ASENTRY_NP(dcache_inv_pou_all)
#if __ARM_ARCH == 6
	mcr	CP15_DCIALL
	DSB
	bx	lr
#else
	mrc	CP15_CLIDR(r0)
	ands	r0, r0, #0x38000000
	mov	r0, r0, lsr #26		/* Get LoUU (naturally aligned) */
	beq	4f

1:	sub	r0, #2
	mcr	CP15_CSSELR(r0)		/* set cache level */
	isb
	mrc	CP15_CCSIDR(r0)		/* read CCSIDR */

	ubfx	r2, r0, #13, #15	/* get num sets - 1 from CCSIDR */
	ubfx	r3, r0, #3, #10		/* get num ways - 1 from CCSIDR */
	clz	r1, r3			/* number of bits to MSB of way */
	lsl	r3, r3, r1		/* shift into position  */
	mov	ip, #1
	lsl	ip, ip, r1		/* ip now contains the way decr  */

	ubfx	r0, r0, #0, #3		/* get linesize from CCSIDR  */
	add	r0, r0, #4		/* apply bias  */
	lsl	r2, r2, r0		/* shift sets by log2(linesize)  */
	add	r3, r3, r2		/* merge numsets - 1 with numways - 1 */
	sub	ip, ip, r2		/* subtract numsets - 1 from way decr */
	mov	r1, #1
	lsl	r1, r1, r0		/* r1 now contains the set decr */
	mov	r2, ip			/* r2 now contains set way decr */

	/* r3 = ways/sets, r2 = way decr, r1 = set decr, r0 and ip are free */
2:	mcr	CP15_DCISW(r3)		/* invalidate line */
	movs	r0, r3			/* get current way/set */
	beq	3f			/* at 0 means we are done */
	movs	r0, r0, lsl #10		/* clear way bits leaving only set bits*/
	subne	r3, r3, r1		/* non-zero?, decrement set */
	subeq	r3, r3, r2		/* zero?, decrement way  and restore set count */
	b	2b

3:
	mrc	CP15_CSSELR(r0)		/* get cache level */
	teq	r0, #0
	bne	1b

4:	dsb				/* wait for stores to finish */
	mov	r0, #0
	mcr	CP15_CSSELR(r0)
	bx	lr
#endif
END(dcache_inv_pou_all)

/* Write back and Invalidate D cache to PoC. */
ASENTRY_NP(dcache_wbinv_poc_all)
#if __ARM_ARCH == 6
	mcr	CP15_DCCIALL
	DSB
	bx	lr
#else
	mrc	CP15_CLIDR(r0)
	ands	r0, r0, #0x07000000
	beq	4f
	mov	r0, #0			/* Clean from inner to outer levels */

1:	mcr	CP15_CSSELR(r0)		/* set cache level */
	isb
	mrc	CP15_CCSIDR(r0)		/* read CCSIDR */

	ubfx	r2, r0, #13, #15	/* get num sets - 1 from CCSIDR */
	ubfx	r3, r0, #3, #10		/* get num ways - 1 from CCSIDR */
	clz	r1, r3			/* number of bits to MSB of way */
	lsl	r3, r3, r1		/* shift into position  */
	mov	ip, #1
	lsl	ip, ip, r1		/* ip now contains the way decr  */

	ubfx	r0, r0, #0, #3		/* get linesize from CCSIDR  */
	add	r0, r0, #4		/* apply bias  */
	lsl	r2, r2, r0		/* shift sets by log2(linesize)  */
	add	r3, r3, r2		/* merge numsets - 1 with numways - 1 */
	sub	ip, ip, r2		/* subtract numsets - 1 from way decr */
	mov	r1, #1
	lsl	r1, r1, r0		/* r1 now contains the set decr */
	mov	r2, ip			/* r2 now contains set way decr */

	/* r3 = ways/sets, r2 = way decr, r1 = set decr, r0 and ip are free */
2:	mcr	CP15_DCCISW(r3)		/* clean and invalidate line */
	movs	r0, r3			/* get current way/set */
	beq	3f			/* at 0 means we are done */
	movs	r0, r0, lsl #10		/* clear way bits leaving only set bits*/
	subne	r3, r3, r1		/* non-zero?, decrement set */
	subeq	r3, r3, r2		/* zero?, decrement way  and restore set count */
	b	2b

3:
	mrc	CP15_CSSELR(r0)		/* get cache level */
	add	r0, r0, #2		/* next level */
	mrc	CP15_CLIDR(r1)
	ands	r1, r1, #0x07000000
	mov	r1, r1, lsr #23		/* Get LoC (naturally aligned) */
	cmp 	r1, r0
	bne	1b

4:	dsb				/* wait for stores to finish */
	mov	r0, #0
	mcr	CP15_CSSELR(r0)
	bx	lr
#endif /* __ARM_ARCH == 6 */
END(dcache_wbinv_poc_all)

ASENTRY_NP(dcache_wb_pou_checked)
	ldr	ip, .Lcpuinfo
	ldr	ip, [ip, #DCACHE_LINE_SIZE]

	GET_PCB(r2)
	ldr	r2, [r2]

	adr	r3, _C_LABEL(cachebailout)
	str	r3, [r2, #PCB_ONFAULT]
1:
	mcr	CP15_DCCMVAC(r0)
	add	r0, r0, ip
	subs	r1, r1, ip
	bhi	1b
	DSB
	mov	r0, #0
	str	r0, [r2, #PCB_ONFAULT]
	mov	r0, #1			/* cannot be faulting address */
	RET

.Lcpuinfo:
	.word	cpuinfo
END(dcache_wb_pou_checked)

ASENTRY_NP(icache_inv_pou_checked)
	ldr	ip, .Lcpuinfo
	ldr	ip, [ip, #ICACHE_LINE_SIZE]

	GET_PCB(r2)
	ldr	r2, [r2]

	adr	r3, _C_LABEL(cachebailout)
	str	r3, [r2, #PCB_ONFAULT]

1:
	mcr	CP15_ICIMVAU(r0)
	add	r0, r0, ip
	subs	r1, r1, ip
	bhi	1b
	DSB
	ISB
	mov	r0, #0
	str	r0, [r2, #PCB_ONFAULT]
	mov	r0, #1			/* cannot be faulting address */
	RET
END(icache_inv_pou_checked)

/* label must be global as trap-v6.c references it */
	.global	_C_LABEL(cachebailout)
_C_LABEL(cachebailout):
	DSB
	ISB
	mov	r1, #0
	str	r1, [r2, #PCB_ONFAULT]
	RET
