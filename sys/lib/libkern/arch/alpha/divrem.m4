/*	$OpenBSD: divrem.m4,v 1.3 1996/10/31 00:43:17 niklas Exp $	*/
/*	$NetBSD: divrem.m4,v 1.5 1996/10/17 04:26:25 cgd Exp $	*/

/*
 * Copyright (c) 1994, 1995 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Chris G. Demetriou
 * 
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" 
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND 
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

/*
 * Division and remainder.
 *
 * The use of m4 is modeled after the sparc code, but the algorithm is
 * simple binary long division.
 *
 * Note that the loops could probably benefit from unrolling.
 */

/*
 * M4 Parameters
 * NAME		name of function to generate
 * OP		OP=div: t10 / t11 -> t12; OP=rem: t10 % t11 -> t12
 * S		S=true: signed; S=false: unsigned
 * WORDSIZE	total number of bits
 */

define(A, `t10')
define(B, `t11')
define(RESULT, `t12')

define(BIT, `t0')
define(I, `t1')
define(CC, `t2')
define(T_0, `t3')
ifelse(S, `true', `define(NEG, `t4')')

#include <machine/asm.h>

LEAF(NAME, 0)					/* XXX */
	lda	sp, -64(sp)
	stq	BIT, 0(sp)
	stq	I, 8(sp)
	stq	CC, 16(sp)
	stq	T_0, 24(sp)
ifelse(S, `true',
`	stq	NEG, 32(sp)')
	stq	A, 40(sp)
	stq	B, 48(sp)
	mov	zero, RESULT			/* Initialize result to zero */

ifelse(S, `true',
`
	/* Compute sign of result.  If either is negative, this is easy.  */
	or	A, B, NEG			/* not the sign, but... */
	srl	NEG, WORDSIZE - 1, NEG		/* rather, or of high bits */
	blbc	NEG, Ldoit			/* neither negative? do it! */

ifelse(OP, `div',
`	xor	A, B, NEG			/* THIS is the sign! */
', `	mov	A, NEG				/* sign follows A. */
')
	srl	NEG, WORDSIZE - 1, NEG		/* make negation the low bit. */

	srl	A, WORDSIZE - 1, I		/* is A negative? */
	blbc	I, LnegB			/* no. */
	/* A is negative; flip it. */
ifelse(WORDSIZE, `32', `
	/* top 32 bits may be random junk */
	zap	A, 0xf0, A
')
	subq	zero, A, A
	srl	B, WORDSIZE - 1, I		/* is B negative? */
	blbc	I, Ldoit			/* no. */
LnegB:
	/* B is definitely negative, no matter how we got here. */
ifelse(WORDSIZE, `32', `
	/* top 32 bits may be random junk */
	zap	B, 0xf0, B
')
	subq	zero, B, B
Ldoit:
')
ifelse(WORDSIZE, `32', `
	/*
	 * Clear the top 32 bits of each operand, as they may
	 * sign extension (if negated above), or random junk.
	 */
	zap	A, 0xf0, A
	zap	B, 0xf0, B
')

	/* kill the special cases. */
	beq	B, Ldotrap			/* division by zero! */

	cmpult	A, B, CC			/* A < B? */
	/* RESULT is already zero, from above.  A is untouched. */
	bne	CC, Lret_result

	cmpeq	A, B, CC			/* A == B? */
	cmovne	CC, 1, RESULT
	cmovne	CC, zero, A
	bne	CC, Lret_result

	/*
	 * Find out how many bits of zeros are at the beginning of the divisor.
	 */
LBbits:
	ldiq	T_0, 1				/* I = 0; BIT = 1<<WORDSIZE-1 */
	mov	zero, I
	sll	T_0, WORDSIZE-1, BIT
LBloop:
	and	B, BIT, CC			/* if bit in B is set, done. */
	bne	CC, LAbits
	addq	I, 1, I				/* increment I, shift bit */
	srl	BIT, 1, BIT
	cmplt	I, WORDSIZE-1, CC		/* if I leaves one bit, done. */
	bne	CC, LBloop

LAbits:
	beq	I, Ldodiv			/* If I = 0, divide now.  */
	ldiq	T_0, 1				/* BIT = 1<<WORDSIZE-1 */
	sll	T_0, WORDSIZE-1, BIT

LAloop:
	and	A, BIT, CC			/* if bit in A is set, done. */
	bne	CC, Ldodiv
	subq	I, 1, I				/* decrement I, shift bit */
	srl     BIT, 1, BIT 
	bne	I, LAloop			/* If I != 0, loop again */

Ldodiv:
	sll	B, I, B				/* B <<= i */
	ldiq	T_0, 1
	sll	T_0, I, BIT

Ldivloop:
	cmpult	A, B, CC
	or	RESULT, BIT, T_0
	cmoveq	CC, T_0, RESULT
	subq	A, B, T_0
	cmoveq	CC, T_0, A
	srl	BIT, 1, BIT	
	srl	B, 1, B
	beq	A, Lret_result
	bne	BIT, Ldivloop

Lret_result:
ifelse(OP, `div',
`', `	mov	A, RESULT
')
ifelse(S, `true',
`
	/* Check to see if we should negate it. */
	subqv	zero, RESULT, T_0
	cmovlbs	NEG, T_0, RESULT
')

	ldq	BIT, 0(sp)
	ldq	I, 8(sp)
	ldq	CC, 16(sp)
	ldq	T_0, 24(sp)
ifelse(S, `true',
`	ldq	NEG, 32(sp)')
	ldq	A, 40(sp)
	ldq	B, 48(sp)
	lda	sp, 64(sp)
	ret	zero, (t9), 1

Ldotrap:
	ldiq	a0, -2			/* This is the signal to SIGFPE! */
	call_pal PAL_gentrap
ifelse(OP, `div',
`', `	mov	zero, A			/* so that zero will be returned */
')
	br	zero, Lret_result

END(NAME)
