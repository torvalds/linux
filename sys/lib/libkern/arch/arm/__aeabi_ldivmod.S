/*-
 * Copyright (c) 2012 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Matt Thomas of 3am Software Foundry.
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

#include <machine/asm.h>

#ifdef __ARMEB__
#define	ALO	r1	/* incoming numerator, outgoing quotient */
#define	AHI	r0	/* incoming numerator, outgoing quotient */
#define	BLO	r3	/* incoming denominator, outgoing remainder */
#define	BHI	r2	/* incoming denominator, outgoing remainder */
#else
#define	ALO	r0	/* incoming numerator, outgoing quotient */
#define	AHI	r1	/* incoming numerator, outgoing quotient */
#define	BLO	r2	/* incoming denominator, outgoing remainder */
#define	BHI	r3	/* incoming denominator, outgoing remainder */
#endif

ENTRY(__aeabi_ldivmod)
	push	{r4-r6, lr}
#define	NEG	r5
	movs	NEG, #0

	cmp	BHI, #0
	bge	2f
	movs	NEG, #1		/* flip quotient sign */
	bl	.Lnegate_b
	bcs	.Lmaxdenom

2:
	cmp	AHI, #0
	eorlt	NEG, NEG, #3	/* flip quotient sign, flip remainder sign */
	bllt	.Lnegate_a

	/*
	 * Arguments are setup, allocate some stack for the remainder
	 * and call __qdivrem for the heavy lifting.
	 */
	sub	sp, sp, #16
	adds	r4, sp, #8
	str	r4, [sp]
	bl	__qdivrem
	add	sp, sp, #8

	/*
	 * The quotient is already in the right place and neither value
	 * needs its sign flipped.
	 */
	cmp	NEG, #0		/* any signs to flip? */
	beq	.Lnegate_neither

	cmp	NEG, #2		/* does remainder need to be negative? */
	beq	.Lnegate_b_only	/* 2 means b only */
	bgt	.Lnegate_both	/* 3 means both */
.Lnegate_a_only:
	bl	.Lnegate_a	/* 1 means a only */
.Lnegate_neither:
	pop	{r2-r6, pc}	/* grab b from stack */
.Lnegate_both:
	bl	.Lnegate_a
.Lnegate_b_only:
	pop	{r2-r3}		/* get remainder */
	bl	.Lnegate_b	/* negate it */
	pop	{r4-r6, pc}

	.align	0
.Lnegate_a:
	negs	ALO, ALO
	rsc	AHI, AHI, #0
	mov	pc, lr

	.align	0
.Lnegate_b:
	negs	BLO, BLO
	rsc	BHI, BHI, #0
	mov	pc, lr

	.align	0
.Lmaxdenom:
	/*
	 * We had a carry so the denominator must have INT64_MIN
	 * Also BLO and BHI never changed values so we can use
	 * them to see if the numerator has the same value.  We
	 * don't have to worry about sign.
	 */
	cmp	BHI, AHI
	cmpeq	BLO, ALO
	bne	1f

	/*
	 * They were equal, so we return a quotient of 1 and remainder of 0.
	 */
	movs	ALO, #1
	movs	AHI, #0
	movs	BLO, #0
	movs	BHI, #0
	pop	{r4-r6, pc}

	/*
	 * Our remainder must be the numerator and our quotient is 0.
	 */
	.align	0
1:	movs	BLO, ALO
	movs	BHI, AHI
	movs	ALO, #0
	movs	AHI, #0
	pop	{r4-r6, pc}
END(__aeabi_ldivmod)
