

/*	$OpenBSD: __divlu.S,v 1.1 2007/11/25 18:25:34 deraadt Exp $	*/
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
 * __divlu		name of function to generate
 * div		div=div: t10 / t11 -> t12; div=rem: t10 % t11 -> t12
 * false		false=true: signed; false=false: unsigned
 * 32	total number of bits
 */











#include <machine/asm.h>

LEAF(__divlu, 0)					/* XXX */
	lda	sp, -64(sp)
	stq	t0, 0(sp)
	stq	t1, 8(sp)
	stq	t2, 16(sp)
	stq	t3, 24(sp)

	stq	t10, 40(sp)
	stq	t11, 48(sp)
	mov	zero, t12			/* Initialize result to zero */



	/*
	 * Clear the top 32 bits of each operand, as they may
	 * sign extension (if negated above), or random junk.
	 */
	zap	t10, 0xf0, t10
	zap	t11, 0xf0, t11


	/* kill the special cases. */
	beq	t11, Ldotrap			/* division by zero! */

	cmpult	t10, t11, t2			/* t10 < t11? */
	/* t12 is already zero, from above.  t10 is untouched. */
	bne	t2, Lret_result

	cmpeq	t10, t11, t2			/* t10 == t11? */
	cmovne	t2, 1, t12
	cmovne	t2, zero, t10
	bne	t2, Lret_result

	/*
	 * Find out how many bits of zeros are at the beginning of the divisor.
	 */
LBbits:
	ldiq	t3, 1				/* t1 = 0; t0 = 1<<32-1 */
	mov	zero, t1
	sll	t3, 32-1, t0
LBloop:
	and	t11, t0, t2			/* if bit in t11 is set, done. */
	bne	t2, LAbits
	addq	t1, 1, t1				/* increment t1,  bit */
	srl	t0, 1, t0
	cmplt	t1, 32-1, t2		/* if t1 leaves one bit, done. */
	bne	t2, LBloop

LAbits:
	beq	t1, Ldodiv			/* If t1 = 0, divide now.  */
	ldiq	t3, 1				/* t0 = 1<<32-1 */
	sll	t3, 32-1, t0

LAloop:
	and	t10, t0, t2			/* if bit in t10 is set, done. */
	bne	t2, Ldodiv
	subq	t1, 1, t1				/* decrement t1,  bit */
	srl     t0, 1, t0 
	bne	t1, LAloop			/* If t1 != 0, loop again */

Ldodiv:
	sll	t11, t1, t11				/* t11 <<= i */
	ldiq	t3, 1
	sll	t3, t1, t0

Ldivloop:
	cmpult	t10, t11, t2
	or	t12, t0, t3
	cmoveq	t2, t3, t12
	subq	t10, t11, t3
	cmoveq	t2, t3, t10
	srl	t0, 1, t0	
	srl	t11, 1, t11
	beq	t10, Lret_result
	bne	t0, Ldivloop

Lret_result:



	ldq	t0, 0(sp)
	ldq	t1, 8(sp)
	ldq	t2, 16(sp)
	ldq	t3, 24(sp)

	ldq	t10, 40(sp)
	ldq	t11, 48(sp)
	lda	sp, 64(sp)
	ret	zero, (t9), 1

Ldotrap:
	ldiq	a0, -2			/* This is the signal to SIGFPE! */
	call_pal PAL_gentrap

	br	zero, Lret_result

END(__divlu)
