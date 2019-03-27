/*	$NetBSD: ldexp.S,v 1.8 2003/08/07 16:42:15 agc Exp $	*/

/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ralph Campbell.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <machine/asm.h>
__FBSDID("$FreeBSD$");

#if defined(LIBC_SCCS) && !defined(lint)
	ASMSTR("from: @(#)ldexp.s	8.1 (Berkeley) 6/4/93")
	ASMSTR("$NetBSD: ldexp.S,v 1.8 2003/08/07 16:42:15 agc Exp $")
#endif /* LIBC_SCCS and not lint */

#ifdef __ABICALLS__
	.abicalls
#endif

#define DEXP_INF	0x7ff
#define DEXP_BIAS	1023
#define DEXP_MIN	-1022
#define DEXP_MAX	1023
#define DFRAC_BITS	52
#define DIMPL_ONE	0x00100000
#define DLEAD_ZEROS	31 - 20
#define STICKYBIT	1
#define GUARDBIT	0x80000000
#define DSIGNAL_NAN	0x00040000
#define DQUIET_NAN0	0x0007ffff
#define DQUIET_NAN1	0xffffffff

/*
 * double ldexp(x, N)
 *	double x; int N;
 *
 * Return x * (2**N), for integer values N.
 */
LEAF(ldexp)
	mfc1	v1, $f13		# get MSW of x
	mfc1	t3, $f12		# get LSW of x
	sll	t1, v1, 1		# get x exponent
	srl	t1, t1, 32 - 11
	beq	t1, DEXP_INF, 9f	# is it a NAN or infinity?
	beq	t1, zero, 1f		# zero or denormalized number?
	addu	t1, t1, a2		# scale exponent
	sll	v0, a2, 20		# position N for addition
	bge	t1, DEXP_INF, 8f	# overflow?
	addu	v0, v0, v1		# multiply by (2**N)
	ble	t1, zero, 4f		# underflow?
	mtc1	v0, $f1			# save MSW of result
	mtc1	t3, $f0			# save LSW of result
	j	ra
1:
	sll	t2, v1, 32 - 20		# get x fraction
	srl	t2, t2, 32 - 20
	srl	t0, v1, 31		# get x sign
	bne	t2, zero, 1f
	beq	t3, zero, 9f		# result is zero
1:
/*
 * Find out how many leading zero bits are in t2,t3 and put in t9.
 */
	move	v0, t2
	move	t9, zero
	bne	t2, zero, 1f
	move	v0, t3
	addu	t9, 32
1:
	srl	ta0, v0, 16
	bne	ta0, zero, 1f
	addu	t9, 16
	sll	v0, 16
1:
	srl	ta0, v0, 24
	bne	ta0, zero, 1f
	addu	t9, 8
	sll	v0, 8
1:
	srl	ta0, v0, 28
	bne	ta0, zero, 1f
	addu	t9, 4
	sll	v0, 4
1:
	srl	ta0, v0, 30
	bne	ta0, zero, 1f
	addu	t9, 2
	sll	v0, 2
1:
	srl	ta0, v0, 31
	bne	ta0, zero, 1f
	addu	t9, 1
/*
 * Now shift t2,t3 the correct number of bits.
 */
1:
	subu	t9, t9, DLEAD_ZEROS	# dont count normal leading zeros
	li	t1, DEXP_MIN + DEXP_BIAS
	subu	t1, t1, t9		# adjust exponent
	addu	t1, t1, a2		# scale exponent
	li	v0, 32
	blt	t9, v0, 1f
	subu	t9, t9, v0		# shift fraction left >= 32 bits
	sll	t2, t3, t9
	move	t3, zero
	b	2f
1:
	subu	v0, v0, t9		# shift fraction left < 32 bits
	sll	t2, t2, t9
	srl	ta0, t3, v0
	or	t2, t2, ta0
	sll	t3, t3, t9
2:
	bge	t1, DEXP_INF, 8f	# overflow?
	ble	t1, zero, 4f		# underflow?
	sll	t2, t2, 32 - 20		# clear implied one bit
	srl	t2, t2, 32 - 20
3:
	sll	t1, t1, 31 - 11		# reposition exponent
	sll	t0, t0, 31		# reposition sign
	or	t0, t0, t1		# put result back together
	or	t0, t0, t2
	mtc1	t0, $f1			# save MSW of result
	mtc1	t3, $f0			# save LSW of result
	j	ra
4:
	li	v0, 0x80000000
	ble	t1, -52, 7f		# is result too small for denorm?
	sll	t2, v1, 31 - 20		# clear exponent, extract fraction
	or	t2, t2, v0		# set implied one bit
	blt	t1, -30, 2f		# will all bits in t3 be shifted out?
	srl	t2, t2, 31 - 20		# shift fraction back to normal position
	subu	t1, t1, 1
	sll	ta0, t2, t1		# shift right t2,t3 based on exponent
	srl	t8, t3, t1		# save bits shifted out
	negu	t1
	srl	t3, t3, t1
	or	t3, t3, ta0
	srl	t2, t2, t1
	bge	t8, zero, 1f		# does result need to be rounded?
	addu	t3, t3, 1		# round result
	sltu	ta0, t3, 1
	sll	t8, t8, 1
	addu	t2, t2, ta0
	bne	t8, zero, 1f		# round result to nearest
	and	t3, t3, ~1
1:
	mtc1	t3, $f0			# save denormalized result (LSW)
	mtc1	t2, $f1			# save denormalized result (MSW)
	bge	v1, zero, 1f		# should result be negative?
	neg.d	$f0, $f0		# negate result
1:
	j	ra
2:
	mtc1	zero, $f1		# exponent and upper fraction
	addu	t1, t1, 20		# compute amount to shift right by
	sll	t8, t2, t1		# save bits shifted out
	negu	t1
	srl	t3, t2, t1
	bge	t8, zero, 1f		# does result need to be rounded?
	addu	t3, t3, 1		# round result
	sltu	ta0, t3, 1
	sll	t8, t8, 1
	mtc1	ta0, $f1			# exponent and upper fraction
	bne	t8, zero, 1f		# round result to nearest
	and	t3, t3, ~1
1:
	mtc1	t3, $f0
	bge	v1, zero, 1f		# is result negative?
	neg.d	$f0, $f0		# negate result
1:
	j	ra
7:
	mtc1	zero, $f0		# result is zero
	mtc1	zero, $f1
	beq	t0, zero, 1f		# is result positive?
	neg.d	$f0, $f0		# negate result
1:
	j	ra
8:
	li	t1, 0x7ff00000		# result is infinity (MSW)
	mtc1	t1, $f1	
	mtc1	zero, $f0		# result is infinity (LSW)
	bge	v1, zero, 1f		# should result be negative infinity?
	neg.d	$f0, $f0		# result is negative infinity
1:
	add.d	$f0, $f0		# cause overflow faults if enabled
	j	ra
9:
	mov.d	$f0, $f12		# yes, result is just x
	j	ra
END(ldexp)
