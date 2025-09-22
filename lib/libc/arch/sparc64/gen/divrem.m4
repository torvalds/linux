/*	$OpenBSD: divrem.m4,v 1.3 2011/03/12 18:50:07 deraadt Exp $	*/
/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
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

/*
 * Division and remainder, from Appendix E of the Sparc Version 8
 * Architecture Manual, with fixes from Gordon Irlam.
 */

/*
 * Input: dividend and divisor in %o0 and %o1 respectively.
 *
 * m4 parameters:
 *  NAME	name of function to generate
 *  OP		OP=div => %o0 / %o1; OP=rem => %o0 % %o1
 *  S		S=true => signed; S=false => unsigned
 *
 * Algorithm parameters:
 *  N		how many bits per iteration we try to get (4)
 *  WORDSIZE	total number of bits (32)
 *
 * Derived constants:
 *  TWOSUPN	2^N, for label generation (m4 exponentiation currently broken)
 *  TOPBITS	number of bits in the top `decade' of a number
 *
 * Important variables:
 *  Q		the partial quotient under development (initially 0)
 *  R		the remainder so far, initially the dividend
 *  ITER	number of main division loop iterations required;
 *		equal to ceil(log2(quotient) / N).  Note that this
 *		is the log base (2^N) of the quotient.
 *  V		the current comparand, initially divisor*2^(ITER*N-1)
 *
 * Cost:
 *  Current estimate for non-large dividend is
 *	ceil(log2(quotient) / N) * (10 + 7N/2) + C
 *  A large dividend is one greater than 2^(31-TOPBITS) and takes a
 *  different path, as the upper bits of the quotient must be developed
 *  one bit at a time.
 */

define(N, `4')
define(TWOSUPN, `16')
define(WORDSIZE, `32')
define(TOPBITS, eval(WORDSIZE - N*((WORDSIZE-1)/N)))

define(dividend, `%o0')
define(divisor, `%o1')
define(Q, `%o2')
define(R, `%o3')
define(ITER, `%o4')
define(V, `%o5')

/* m4 reminder: ifelse(a,b,c,d) => if a is b, then c, else d */
define(T, `%g1')
define(SC, `%g5')
ifelse(S, `true', `define(SIGN, `%g6')')

/*
 * This is the recursive definition for developing quotient digits.
 *
 * Parameters:
 *  $1	the current depth, 1 <= $1 <= N
 *  $2	the current accumulation of quotient bits
 *  N	max depth
 *
 * We add a new bit to $2 and either recurse or insert the bits in
 * the quotient.  R, Q, and V are inputs and outputs as defined above;
 * the condition codes are expected to reflect the input R, and are
 * modified to reflect the output R.
 */
define(DEVELOP_QUOTIENT_BITS,
`	! depth $1, accumulated bits $2
	bl	L.$1.eval(TWOSUPN+$2)
	srl	V,1,V
	! remainder is positive
	subcc	R,V,R
	ifelse($1, N,
	`	b	9f
		add	Q, ($2*2+1), Q
	', `	DEVELOP_QUOTIENT_BITS(incr($1), `eval(2*$2+1)')')
L.$1.eval(TWOSUPN+$2):
	! remainder is negative
	addcc	R,V,R
	ifelse($1, N,
	`	b	9f
		add	Q, ($2*2-1), Q
	', `	DEVELOP_QUOTIENT_BITS(incr($1), `eval(2*$2-1)')')
	ifelse($1, 1, `9:')')

#include <machine/asm.h>
#include <machine/trap.h>

FUNC(NAME)
ifelse(S, `true',
`	! compute sign of result; if neither is negative, no problem
	orcc	divisor, dividend, %g0	! either negative?
	bge	2f			! no, go do the divide
	ifelse(OP, `div',
		`xor	divisor, dividend, SIGN',
		`mov	dividend, SIGN')	! compute sign in any case
	tst	divisor
	bge	1f
	tst	dividend
	! divisor is definitely negative; dividend might also be negative
	bge	2f			! if dividend not negative...
	neg	divisor			! in any case, make divisor nonneg
1:	! dividend is negative, divisor is nonnegative
	neg	dividend		! make dividend nonnegative
2:
')
	! Ready to divide.  Compute size of quotient; scale comparand.
	orcc	divisor, %g0, V
	bnz	1f
	mov	dividend, R

		! Divide by zero trap.  If it returns, return 0 (about as
		! wrong as possible, but that is what SunOS does...).
		t	ST_DIV0
		retl
		clr	%o0

1:
	cmp	R, V			! if divisor exceeds dividend, done
	blu	Lgot_result		! (and algorithm fails otherwise)
	clr	Q
	sethi	%hi(1 << (WORDSIZE - TOPBITS - 1)), T
	cmp	R, T
	blu	Lnot_really_big
	clr	ITER

	! `Here the dividend is >= 2^(31-N) or so.  We must be careful here,
	! as our usual N-at-a-shot divide step will cause overflow and havoc.
	! The number of bits in the result here is N*ITER+SC, where SC <= N.
	! Compute ITER in an unorthodox manner: know we need to shift V into
	! the top decade: so do not even bother to compare to R.'
	1:
		cmp	V, T
		bgeu	3f
		mov	1, SC
		sll	V, N, V
		b	1b
		inc	ITER

	! Now compute SC.
	2:	addcc	V, V, V
		bcc	Lnot_too_big
		inc	SC

		! We get here if the divisor overflowed while shifting.
		! This means that R has the high-order bit set.
		! Restore V and subtract from R.
		sll	T, TOPBITS, T	! high order bit
		srl	V, 1, V		! rest of V
		add	V, T, V
		b	Ldo_single_div
		dec	SC

	Lnot_too_big:
	3:	cmp	V, R
		blu	2b
		nop
		be	Ldo_single_div
		nop
	/* NB: these are commented out in the V8-Sparc manual as well */
	/* (I do not understand this) */
	! V > R: went too far: back up 1 step
	!	srl	V, 1, V
	!	dec	SC
	! do single-bit divide steps
	!
	! We have to be careful here.  We know that R >= V, so we can do the
	! first divide step without thinking.  BUT, the others are conditional,
	! and are only done if R >= 0.  Because both R and V may have the high-
	! order bit set in the first step, just falling into the regular
	! division loop will mess up the first time around.
	! So we unroll slightly...
	Ldo_single_div:
		deccc	SC
		bl	Lend_regular_divide
		nop
		sub	R, V, R
		mov	1, Q
		b	Lend_single_divloop
		nop
	Lsingle_divloop:
		sll	Q, 1, Q
		bl	1f
		srl	V, 1, V
		! R >= 0
		sub	R, V, R
		b	2f
		inc	Q
	1:	! R < 0
		add	R, V, R
		dec	Q
	2:
	Lend_single_divloop:
		deccc	SC
		bge	Lsingle_divloop
		tst	R
		b,a	Lend_regular_divide

Lnot_really_big:
1:
	sll	V, N, V
	cmp	V, R
	bleu	1b
	inccc	ITER
	be	Lgot_result
	dec	ITER

	tst	R	! set up for initial iteration
Ldivloop:
	sll	Q, N, Q
	DEVELOP_QUOTIENT_BITS(1, 0)
Lend_regular_divide:
	deccc	ITER
	bge	Ldivloop
	tst	R
	bl,a	Lgot_result
	! non-restoring fixup here (one instruction only!)
ifelse(OP, `div',
`	dec	Q
', `	add	R, divisor, R
')

Lgot_result:
ifelse(S, `true',
`	! check to see if answer should be < 0
	tst	SIGN
	bl,a	1f
	ifelse(OP, `div', `neg Q', `neg R')
1:')
	retl
	ifelse(OP, `div', `mov Q, %o0', `mov R, %o0')
