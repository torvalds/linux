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
 *	@(#)fpu_div.c	8.1 (Berkeley) 6/11/93
 *	$NetBSD: fpu_div.c,v 1.2 1994/11/20 20:52:38 deraadt Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Perform an FPU divide (return x / y).
 */

#include <sys/types.h>

#include <machine/frame.h>
#include <machine/fp.h>
#include <machine/fsr.h>

#include "fpu_arith.h"
#include "fpu_emu.h"
#include "fpu_extern.h"

/*
 * Division of normal numbers is done as follows:
 *
 * x and y are floating point numbers, i.e., in the form 1.bbbb * 2^e.
 * If X and Y are the mantissas (1.bbbb's), the quotient is then:
 *
 *	q = (X / Y) * 2^((x exponent) - (y exponent))
 *
 * Since X and Y are both in [1.0,2.0), the quotient's mantissa (X / Y)
 * will be in [0.5,2.0).  Moreover, it will be less than 1.0 if and only
 * if X < Y.  In that case, it will have to be shifted left one bit to
 * become a normal number, and the exponent decremented.  Thus, the
 * desired exponent is:
 *
 *	left_shift = x->fp_mant < y->fp_mant;
 *	result_exp = x->fp_exp - y->fp_exp - left_shift;
 *
 * The quotient mantissa X/Y can then be computed one bit at a time
 * using the following algorithm:
 *
 *	Q = 0;			-- Initial quotient.
 *	R = X;			-- Initial remainder,
 *	if (left_shift)		--   but fixed up in advance.
 *		R *= 2;
 *	for (bit = FP_NMANT; --bit >= 0; R *= 2) {
 *		if (R >= Y) {
 *			Q |= 1 << bit;
 *			R -= Y;
 *		}
 *	}
 *
 * The subtraction R -= Y always removes the uppermost bit from R (and
 * can sometimes remove additional lower-order 1 bits); this proof is
 * left to the reader.
 *
 * This loop correctly calculates the guard and round bits since they are
 * included in the expanded internal representation.  The sticky bit
 * is to be set if and only if any other bits beyond guard and round
 * would be set.  From the above it is obvious that this is true if and
 * only if the remainder R is nonzero when the loop terminates.
 *
 * Examining the loop above, we can see that the quotient Q is built
 * one bit at a time ``from the top down''.  This means that we can
 * dispense with the multi-word arithmetic and just build it one word
 * at a time, writing each result word when it is done.
 *
 * Furthermore, since X and Y are both in [1.0,2.0), we know that,
 * initially, R >= Y.  (Recall that, if X < Y, R is set to X * 2 and
 * is therefore at in [2.0,4.0).)  Thus Q is sure to have bit FP_NMANT-1
 * set, and R can be set initially to either X - Y (when X >= Y) or
 * 2X - Y (when X < Y).  In addition, comparing R and Y is difficult,
 * so we will simply calculate R - Y and see if that underflows.
 * This leads to the following revised version of the algorithm:
 *
 *	R = X;
 *	bit = FP_1;
 *	D = R - Y;
 *	if (D >= 0) {
 *		result_exp = x->fp_exp - y->fp_exp;
 *		R = D;
 *		q = bit;
 *		bit >>= 1;
 *	} else {
 *		result_exp = x->fp_exp - y->fp_exp - 1;
 *		q = 0;
 *	}
 *	R <<= 1;
 *	do  {
 *		D = R - Y;
 *		if (D >= 0) {
 *			q |= bit;
 *			R = D;
 *		}
 *		R <<= 1;
 *	} while ((bit >>= 1) != 0);
 *	Q[0] = q;
 *	for (i = 1; i < 4; i++) {
 *		q = 0, bit = 1 << 31;
 *		do {
 *			D = R - Y;
 *			if (D >= 0) {
 *				q |= bit;
 *				R = D;
 *			}
 *			R <<= 1;
 *		} while ((bit >>= 1) != 0);
 *		Q[i] = q;
 *	}
 *
 * This can be refined just a bit further by moving the `R <<= 1'
 * calculations to the front of the do-loops and eliding the first one.
 * The process can be terminated immediately whenever R becomes 0, but
 * this is relatively rare, and we do not bother.
 */

struct fpn *
__fpu_div(fe)
	struct fpemu *fe;
{
	struct fpn *x = &fe->fe_f1, *y = &fe->fe_f2;
	u_int q, bit;
	u_int r0, r1, r2, r3, d0, d1, d2, d3, y0, y1, y2, y3;
	FPU_DECL_CARRY

	/*
	 * Since divide is not commutative, we cannot just use ORDER.
	 * Check either operand for NaN first; if there is at least one,
	 * order the signalling one (if only one) onto the right, then
	 * return it.  Otherwise we have the following cases:
	 *
	 *	Inf / Inf = NaN, plus NV exception
	 *	Inf / num = Inf [i.e., return x #]
	 *	Inf / 0   = Inf [i.e., return x #]
	 *	0 / Inf = 0 [i.e., return x #]
	 *	0 / num = 0 [i.e., return x #]
	 *	0 / 0   = NaN, plus NV exception
	 *	num / Inf = 0 #
	 *	num / num = num (do the divide)
	 *	num / 0   = Inf #, plus DZ exception
	 *
	 * # Sign of result is XOR of operand signs.
	 */
	if (ISNAN(x) || ISNAN(y)) {
		ORDER(x, y);
		return (y);
	}
	if (ISINF(x) || ISZERO(x)) {
		if (x->fp_class == y->fp_class)
			return (__fpu_newnan(fe));
		x->fp_sign ^= y->fp_sign;
		return (x);
	}

	x->fp_sign ^= y->fp_sign;
	if (ISINF(y)) {
		x->fp_class = FPC_ZERO;
		return (x);
	}
	if (ISZERO(y)) {
		fe->fe_cx = FSR_DZ;
		x->fp_class = FPC_INF;
		return (x);
	}

	/*
	 * Macros for the divide.  See comments at top for algorithm.
	 * Note that we expand R, D, and Y here.
	 */

#define	SUBTRACT		/* D = R - Y */ \
	FPU_SUBS(d3, r3, y3); FPU_SUBCS(d2, r2, y2); \
	FPU_SUBCS(d1, r1, y1); FPU_SUBC(d0, r0, y0)

#define	NONNEGATIVE		/* D >= 0 */ \
	((int)d0 >= 0)

#ifdef FPU_SHL1_BY_ADD
#define	SHL1			/* R <<= 1 */ \
	FPU_ADDS(r3, r3, r3); FPU_ADDCS(r2, r2, r2); \
	FPU_ADDCS(r1, r1, r1); FPU_ADDC(r0, r0, r0)
#else
#define	SHL1 \
	r0 = (r0 << 1) | (r1 >> 31), r1 = (r1 << 1) | (r2 >> 31), \
	r2 = (r2 << 1) | (r3 >> 31), r3 <<= 1
#endif

#define	LOOP			/* do ... while (bit >>= 1) */ \
	do { \
		SHL1; \
		SUBTRACT; \
		if (NONNEGATIVE) { \
			q |= bit; \
			r0 = d0, r1 = d1, r2 = d2, r3 = d3; \
		} \
	} while ((bit >>= 1) != 0)

#define	WORD(r, i)			/* calculate r->fp_mant[i] */ \
	q = 0; \
	bit = 1 << 31; \
	LOOP; \
	(x)->fp_mant[i] = q

	/* Setup.  Note that we put our result in x. */
	r0 = x->fp_mant[0];
	r1 = x->fp_mant[1];
	r2 = x->fp_mant[2];
	r3 = x->fp_mant[3];
	y0 = y->fp_mant[0];
	y1 = y->fp_mant[1];
	y2 = y->fp_mant[2];
	y3 = y->fp_mant[3];

	bit = FP_1;
	SUBTRACT;
	if (NONNEGATIVE) {
		x->fp_exp -= y->fp_exp;
		r0 = d0, r1 = d1, r2 = d2, r3 = d3;
		q = bit;
		bit >>= 1;
	} else {
		x->fp_exp -= y->fp_exp + 1;
		q = 0;
	}
	LOOP;
	x->fp_mant[0] = q;
	WORD(x, 1);
	WORD(x, 2);
	WORD(x, 3);
	x->fp_sticky = r0 | r1 | r2 | r3;

	return (x);
}
