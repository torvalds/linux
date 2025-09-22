/* $OpenBSD: ldexp.S,v 1.9 2019/01/05 12:16:59 visa Exp $ */
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

#include "SYS.h"

#define DEXP_INF	0x7ff
#define DEXP_BIAS	1023
#define DEXP_MIN	-1022
#define DEXP_MAX	1023
#define DFRAC_BITS	52
#define DIMPL_ONE	0x00100000
#define DLEAD_ZEROS	63 - 52
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
LEAF(ldexp, 0)
	.set	reorder
	dmfc1	t3, $f12		# get x
	dsll	t1, t3, 1		# get x exponent
	dsrl	t1, t1, 64 - 11
	beq	t1, DEXP_INF, 9f	# is it a NAN or infinity?
	beq	t1, zero, 1f		# zero or denormalized number?
	daddu	t1, a1			# scale exponent
	dsll	v0, a1, 52		# position N for addition
	bge	t1, DEXP_INF, 8f	# overflow?
	daddu	v0, t3, v0		# multiply by (2**N)
	ble	t1, zero, 4f		# underflow?
	dmtc1	v0, $f0			# save result
	j	ra
1:
	dsll	t2, t3, 64 - 52		# get x fraction
	dsrl	t2, t2, 64 - 52
	dsrl	t0, t3, 63		# get x sign
	beq	t2, zero, 9f		# result is zero
/*
 * Find out how many leading zero bits are in t2 and put in t9.
 */
	move	v0, t2
	move	t9, zero
	dsrl	ta0, v0, 32
	bne	ta0, zero, 1f
	daddu	t9, 32
	dsll	v0, 32
1:
	dsrl	ta0, v0, 16
	bne	ta0, zero, 1f
	daddu	t9, 16
	dsll	v0, 16
1:
	dsrl	ta0, v0, 24
	bne	ta0, zero, 1f
	daddu	t9, 8
	dsll	v0, 8
1:
	dsrl	ta0, v0, 28
	bne	ta0, zero, 1f
	daddu	t9, 4
	dsll	v0, 4
1:
	dsrl	ta0, v0, 30
	bne	ta0, zero, 1f
	daddu	t9, 2
	dsll	v0, 2
1:
	dsrl	ta0, v0, 31
	bne	ta0, zero, 1f
	daddu	t9, 1
/*
 * Now shift t2 the correct number of bits.
 */
1:
	dsubu	t9, t9, DLEAD_ZEROS	# dont count normal leading zeros
	li	t1, DEXP_MIN + DEXP_BIAS
	subu	t1, t1, t9		# adjust exponent
	addu	t1, t1, a2		# scale exponent
	dsll	t2, t9

	bge	t1, DEXP_INF, 8f	# overflow?
	ble	t1, zero, 4f		# underflow?
	dsll	t2, t2, 64 - 52		# clear implied one bit
	dsrl	t2, t2, 64 - 52

	dsll	t1, t1, 63 - 11		# reposition exponent
	dsll	t0, t0, 63		# reposition sign
	or	t0, t0, t1		# put result back together
	or	t0, t0, t2
	dmtc1	t0, $f0			# save result
	j	ra
4:
	dli	v0, 0x8000000000000000
	ble	t1, -52, 7f		# is result too small for denorm?
	dsll	t2, t3, 63 - 52		# clear exponent, extract fraction
	or	t2, t2, v0		# set implied one bit
	dsrl	t2, t2, 63 - 52		# shift fraction back to normal position
	subu	t1, t1, 1
	dsrl	t8, t2, t1		# save bits shifted out
	negu	t1
	dsrl	t2, t2, t1
	bge	t8, zero, 1f		# does result need to be rounded?
	daddu	t2, t2, 1		# round result
	dsll	t8, t8, 1
	bne	t8, zero, 1f		# round result to nearest
	ori	t2, 1
	xori	t2, 1
1:
	dmtc1	t2, $f0			# save denormalized result (LSW)
	bge	t3, zero, 1f		# should result be negative?
	neg.d	$f0, $f0		# negate result
1:
	j	ra
7:
	dmtc1	zero, $f0		# result is zero
	bge	t3, zero, 1f		# is result positive?
	neg.d	$f0, $f0		# negate result
1:
	j	ra
8:
	dli	t1, 0x7ff0000000000000	# result is infinity (MSW)
	dmtc1	t1, $f0	
	bge	t3, zero, 1f		# should result be negative infinity?
	neg.d	$f0, $f0		# result is negative infinity
1:
	add.d	$f0, $f0, $f0		# cause overflow faults if enabled
	j	ra
9:
	mov.d	$f0, $f12		# yes, result is just x
	j	ra
END_STRONG(ldexp)
