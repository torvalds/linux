/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
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
 *
 *	@(#)fpu_sqrt.c	8.1 (Berkeley) 6/11/93
 *	$NetBSD: fpu_sqrt.c,v 1.2 1994/11/20 20:52:46 deraadt Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Perform an FPU square root (return sqrt(x)).
 */

#include <sys/types.h>

#include <machine/frame.h>
#include <machine/fp.h>

#include "fpu_arith.h"
#include "fpu_emu.h"
#include "fpu_extern.h"

/*
 * Our task is to calculate the square root of a floating point number x0.
 * This number x normally has the form:
 *
 *		    exp
 *	x = mant * 2		(where 1 <= mant < 2 and exp is an integer)
 *
 * This can be left as it stands, or the mantissa can be doubled and the
 * exponent decremented:
 *
 *			  exp-1
 *	x = (2 * mant) * 2	(where 2 <= 2 * mant < 4)
 *
 * If the exponent `exp' is even, the square root of the number is best
 * handled using the first form, and is by definition equal to:
 *
 *				exp/2
 *	sqrt(x) = sqrt(mant) * 2
 *
 * If exp is odd, on the other hand, it is convenient to use the second
 * form, giving:
 *
 *				    (exp-1)/2
 *	sqrt(x) = sqrt(2 * mant) * 2
 *
 * In the first case, we have
 *
 *	1 <= mant < 2
 *
 * and therefore
 *
 *	sqrt(1) <= sqrt(mant) < sqrt(2)
 *
 * while in the second case we have
 *
 *	2 <= 2*mant < 4
 *
 * and therefore
 *
 *	sqrt(2) <= sqrt(2*mant) < sqrt(4)
 *
 * so that in any case, we are sure that
 *
 *	sqrt(1) <= sqrt(n * mant) < sqrt(4),	n = 1 or 2
 *
 * or
 *
 *	1 <= sqrt(n * mant) < 2,		n = 1 or 2.
 *
 * This root is therefore a properly formed mantissa for a floating
 * point number.  The exponent of sqrt(x) is either exp/2 or (exp-1)/2
 * as above.  This leaves us with the problem of finding the square root
 * of a fixed-point number in the range [1..4).
 *
 * Though it may not be instantly obvious, the following square root
 * algorithm works for any integer x of an even number of bits, provided
 * that no overflows occur:
 *
 *	let q = 0
 *	for k = NBITS-1 to 0 step -1 do -- for each digit in the answer...
 *		x *= 2			-- multiply by radix, for next digit
 *		if x >= 2q + 2^k then	-- if adding 2^k does not
 *			x -= 2q + 2^k	-- exceed the correct root,
 *			q += 2^k	-- add 2^k and adjust x
 *		fi
 *	done
 *	sqrt = q / 2^(NBITS/2)		-- (and any remainder is in x)
 *
 * If NBITS is odd (so that k is initially even), we can just add another
 * zero bit at the top of x.  Doing so means that q is not going to acquire
 * a 1 bit in the first trip around the loop (since x0 < 2^NBITS).  If the
 * final value in x is not needed, or can be off by a factor of 2, this is
 * equivalant to moving the `x *= 2' step to the bottom of the loop:
 *
 *	for k = NBITS-1 to 0 step -1 do if ... fi; x *= 2; done
 *
 * and the result q will then be sqrt(x0) * 2^floor(NBITS / 2).
 * (Since the algorithm is destructive on x, we will call x's initial
 * value, for which q is some power of two times its square root, x0.)
 *
 * If we insert a loop invariant y = 2q, we can then rewrite this using
 * C notation as:
 *
 *	q = y = 0; x = x0;
 *	for (k = NBITS; --k >= 0;) {
 * #if (NBITS is even)
 *		x *= 2;
 * #endif
 *		t = y + (1 << k);
 *		if (x >= t) {
 *			x -= t;
 *			q += 1 << k;
 *			y += 1 << (k + 1);
 *		}
 * #if (NBITS is odd)
 *		x *= 2;
 * #endif
 *	}
 *
 * If x0 is fixed point, rather than an integer, we can simply alter the
 * scale factor between q and sqrt(x0).  As it happens, we can easily arrange
 * for the scale factor to be 2**0 or 1, so that sqrt(x0) == q.
 *
 * In our case, however, x0 (and therefore x, y, q, and t) are multiword
 * integers, which adds some complication.  But note that q is built one
 * bit at a time, from the top down, and is not used itself in the loop
 * (we use 2q as held in y instead).  This means we can build our answer
 * in an integer, one word at a time, which saves a bit of work.  Also,
 * since 1 << k is always a `new' bit in q, 1 << k and 1 << (k+1) are
 * `new' bits in y and we can set them with an `or' operation rather than
 * a full-blown multiword add.
 *
 * We are almost done, except for one snag.  We must prove that none of our
 * intermediate calculations can overflow.  We know that x0 is in [1..4)
 * and therefore the square root in q will be in [1..2), but what about x,
 * y, and t?
 *
 * We know that y = 2q at the beginning of each loop.  (The relation only
 * fails temporarily while y and q are being updated.)  Since q < 2, y < 4.
 * The sum in t can, in our case, be as much as y+(1<<1) = y+2 < 6, and.
 * Furthermore, we can prove with a bit of work that x never exceeds y by
 * more than 2, so that even after doubling, 0 <= x < 8.  (This is left as
 * an exercise to the reader, mostly because I have become tired of working
 * on this comment.)
 *
 * If our floating point mantissas (which are of the form 1.frac) occupy
 * B+1 bits, our largest intermediary needs at most B+3 bits, or two extra.
 * In fact, we want even one more bit (for a carry, to avoid compares), or
 * three extra.  There is a comment in fpu_emu.h reminding maintainers of
 * this, so we have some justification in assuming it.
 */
struct fpn *
__fpu_sqrt(fe)
	struct fpemu *fe;
{
	struct fpn *x = &fe->fe_f1;
	u_int bit, q, tt;
	u_int x0, x1, x2, x3;
	u_int y0, y1, y2, y3;
	u_int d0, d1, d2, d3;
	int e;

	/*
	 * Take care of special cases first.  In order:
	 *
	 *	sqrt(NaN) = NaN
	 *	sqrt(+0) = +0
	 *	sqrt(-0) = -0
	 *	sqrt(x < 0) = NaN	(including sqrt(-Inf))
	 *	sqrt(+Inf) = +Inf
	 *
	 * Then all that remains are numbers with mantissas in [1..2).
	 */
	if (ISNAN(x) || ISZERO(x))
		return (x);
	if (x->fp_sign)
		return (__fpu_newnan(fe));
	if (ISINF(x))
		return (x);

	/*
	 * Calculate result exponent.  As noted above, this may involve
	 * doubling the mantissa.  We will also need to double x each
	 * time around the loop, so we define a macro for this here, and
	 * we break out the multiword mantissa.
	 */
#ifdef FPU_SHL1_BY_ADD
#define	DOUBLE_X { \
	FPU_ADDS(x3, x3, x3); FPU_ADDCS(x2, x2, x2); \
	FPU_ADDCS(x1, x1, x1); FPU_ADDC(x0, x0, x0); \
}
#else
#define	DOUBLE_X { \
	x0 = (x0 << 1) | (x1 >> 31); x1 = (x1 << 1) | (x2 >> 31); \
	x2 = (x2 << 1) | (x3 >> 31); x3 <<= 1; \
}
#endif
#if (FP_NMANT & 1) != 0
# define ODD_DOUBLE	DOUBLE_X
# define EVEN_DOUBLE	/* nothing */
#else
# define ODD_DOUBLE	/* nothing */
# define EVEN_DOUBLE	DOUBLE_X
#endif
	x0 = x->fp_mant[0];
	x1 = x->fp_mant[1];
	x2 = x->fp_mant[2];
	x3 = x->fp_mant[3];
	e = x->fp_exp;
	if (e & 1)		/* exponent is odd; use sqrt(2mant) */
		DOUBLE_X;
	/* THE FOLLOWING ASSUMES THAT RIGHT SHIFT DOES SIGN EXTENSION */
	x->fp_exp = e >> 1;	/* calculates (e&1 ? (e-1)/2 : e/2 */

	/*
	 * Now calculate the mantissa root.  Since x is now in [1..4),
	 * we know that the first trip around the loop will definitely
	 * set the top bit in q, so we can do that manually and start
	 * the loop at the next bit down instead.  We must be sure to
	 * double x correctly while doing the `known q=1.0'.
	 *
	 * We do this one mantissa-word at a time, as noted above, to
	 * save work.  To avoid `(1U << 31) << 1', we also do the top bit
	 * outside of each per-word loop.
	 *
	 * The calculation `t = y + bit' breaks down into `t0 = y0, ...,
	 * t3 = y3, t? |= bit' for the appropriate word.  Since the bit
	 * is always a `new' one, this means that three of the `t?'s are
	 * just the corresponding `y?'; we use `#define's here for this.
	 * The variable `tt' holds the actual `t?' variable.
	 */

	/* calculate q0 */
#define	t0 tt
	bit = FP_1;
	EVEN_DOUBLE;
	/* if (x >= (t0 = y0 | bit)) { */	/* always true */
		q = bit;
		x0 -= bit;
		y0 = bit << 1;
	/* } */
	ODD_DOUBLE;
	while ((bit >>= 1) != 0) {	/* for remaining bits in q0 */
		EVEN_DOUBLE;
		t0 = y0 | bit;		/* t = y + bit */
		if (x0 >= t0) {		/* if x >= t then */
			x0 -= t0;	/*	x -= t */
			q |= bit;	/*	q += bit */
			y0 |= bit << 1;	/*	y += bit << 1 */
		}
		ODD_DOUBLE;
	}
	x->fp_mant[0] = q;
#undef t0

	/* calculate q1.  note (y0&1)==0. */
#define t0 y0
#define t1 tt
	q = 0;
	y1 = 0;
	bit = 1 << 31;
	EVEN_DOUBLE;
	t1 = bit;
	FPU_SUBS(d1, x1, t1);
	FPU_SUBC(d0, x0, t0);		/* d = x - t */
	if ((int)d0 >= 0) {		/* if d >= 0 (i.e., x >= t) then */
		x0 = d0, x1 = d1;	/*	x -= t */
		q = bit;		/*	q += bit */
		y0 |= 1;		/*	y += bit << 1 */
	}
	ODD_DOUBLE;
	while ((bit >>= 1) != 0) {	/* for remaining bits in q1 */
		EVEN_DOUBLE;		/* as before */
		t1 = y1 | bit;
		FPU_SUBS(d1, x1, t1);
		FPU_SUBC(d0, x0, t0);
		if ((int)d0 >= 0) {
			x0 = d0, x1 = d1;
			q |= bit;
			y1 |= bit << 1;
		}
		ODD_DOUBLE;
	}
	x->fp_mant[1] = q;
#undef t1

	/* calculate q2.  note (y1&1)==0; y0 (aka t0) is fixed. */
#define t1 y1
#define t2 tt
	q = 0;
	y2 = 0;
	bit = 1 << 31;
	EVEN_DOUBLE;
	t2 = bit;
	FPU_SUBS(d2, x2, t2);
	FPU_SUBCS(d1, x1, t1);
	FPU_SUBC(d0, x0, t0);
	if ((int)d0 >= 0) {
		x0 = d0, x1 = d1, x2 = d2;
		q = bit;
		y1 |= 1;		/* now t1, y1 are set in concrete */
	}
	ODD_DOUBLE;
	while ((bit >>= 1) != 0) {
		EVEN_DOUBLE;
		t2 = y2 | bit;
		FPU_SUBS(d2, x2, t2);
		FPU_SUBCS(d1, x1, t1);
		FPU_SUBC(d0, x0, t0);
		if ((int)d0 >= 0) {
			x0 = d0, x1 = d1, x2 = d2;
			q |= bit;
			y2 |= bit << 1;
		}
		ODD_DOUBLE;
	}
	x->fp_mant[2] = q;
#undef t2

	/* calculate q3.  y0, t0, y1, t1 all fixed; y2, t2, almost done. */
#define t2 y2
#define t3 tt
	q = 0;
	y3 = 0;
	bit = 1 << 31;
	EVEN_DOUBLE;
	t3 = bit;
	FPU_SUBS(d3, x3, t3);
	FPU_SUBCS(d2, x2, t2);
	FPU_SUBCS(d1, x1, t1);
	FPU_SUBC(d0, x0, t0);
	if ((int)d0 >= 0) {
		x0 = d0, x1 = d1, x2 = d2; x3 = d3;
		q = bit;
		y2 |= 1;
	}
	ODD_DOUBLE;
	while ((bit >>= 1) != 0) {
		EVEN_DOUBLE;
		t3 = y3 | bit;
		FPU_SUBS(d3, x3, t3);
		FPU_SUBCS(d2, x2, t2);
		FPU_SUBCS(d1, x1, t1);
		FPU_SUBC(d0, x0, t0);
		if ((int)d0 >= 0) {
			x0 = d0, x1 = d1, x2 = d2; x3 = d3;
			q |= bit;
			y3 |= bit << 1;
		}
		ODD_DOUBLE;
	}
	x->fp_mant[3] = q;

	/*
	 * The result, which includes guard and round bits, is exact iff
	 * x is now zero; any nonzero bits in x represent sticky bits.
	 */
	x->fp_sticky = x0 | x1 | x2 | x3;
	return (x);
}
