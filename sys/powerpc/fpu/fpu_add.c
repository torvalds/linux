/*	$NetBSD: fpu_add.c,v 1.4 2005/12/11 12:18:42 christos Exp $ */

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 *	@(#)fpu_add.c	8.1 (Berkeley) 6/11/93
 */

/*
 * Perform an FPU add (return x + y).
 *
 * To subtract, negate y and call add.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/systm.h>

#include <machine/fpu.h>
#include <machine/ieeefp.h>
#include <machine/reg.h>

#include <powerpc/fpu/fpu_arith.h>
#include <powerpc/fpu/fpu_emu.h>

struct fpn *
fpu_add(struct fpemu *fe)
{
	struct fpn *x = &fe->fe_f1, *y = &fe->fe_f2, *r;
	u_int r0, r1, r2, r3;
	int rd;

	/*
	 * Put the `heavier' operand on the right (see fpu_emu.h).
	 * Then we will have one of the following cases, taken in the
	 * following order:
	 *
	 *  - y = NaN.  Implied: if only one is a signalling NaN, y is.
	 *	The result is y.
	 *  - y = Inf.  Implied: x != NaN (is 0, number, or Inf: the NaN
	 *    case was taken care of earlier).
	 *	If x = -y, the result is NaN.  Otherwise the result
	 *	is y (an Inf of whichever sign).
	 *  - y is 0.  Implied: x = 0.
	 *	If x and y differ in sign (one positive, one negative),
	 *	the result is +0 except when rounding to -Inf.  If same:
	 *	+0 + +0 = +0; -0 + -0 = -0.
	 *  - x is 0.  Implied: y != 0.
	 *	Result is y.
	 *  - other.  Implied: both x and y are numbers.
	 *	Do addition a la Hennessey & Patterson.
	 */
	DPRINTF(FPE_REG, ("fpu_add:\n"));
	DUMPFPN(FPE_REG, x);
	DUMPFPN(FPE_REG, y);
	DPRINTF(FPE_REG, ("=>\n"));
	ORDER(x, y);
	if (ISNAN(y)) {
		fe->fe_cx |= FPSCR_VXSNAN;
		DUMPFPN(FPE_REG, y);
		return (y);
	}
	if (ISINF(y)) {
		if (ISINF(x) && x->fp_sign != y->fp_sign) {
			fe->fe_cx |= FPSCR_VXISI;
			return (fpu_newnan(fe));
		}
		DUMPFPN(FPE_REG, y);
		return (y);
	}
	rd = ((fe->fe_fpscr) & FPSCR_RN);
	if (ISZERO(y)) {
		if (rd != FP_RM)	/* only -0 + -0 gives -0 */
			y->fp_sign &= x->fp_sign;
		else			/* any -0 operand gives -0 */
			y->fp_sign |= x->fp_sign;
		DUMPFPN(FPE_REG, y);
		return (y);
	}
	if (ISZERO(x)) {
		DUMPFPN(FPE_REG, y);
		return (y);
	}
	/*
	 * We really have two numbers to add, although their signs may
	 * differ.  Make the exponents match, by shifting the smaller
	 * number right (e.g., 1.011 => 0.1011) and increasing its
	 * exponent (2^3 => 2^4).  Note that we do not alter the exponents
	 * of x and y here.
	 */
	r = &fe->fe_f3;
	r->fp_class = FPC_NUM;
	if (x->fp_exp == y->fp_exp) {
		r->fp_exp = x->fp_exp;
		r->fp_sticky = 0;
	} else {
		if (x->fp_exp < y->fp_exp) {
			/*
			 * Try to avoid subtract case iii (see below).
			 * This also guarantees that x->fp_sticky = 0.
			 */
			SWAP(x, y);
		}
		/* now x->fp_exp > y->fp_exp */
		r->fp_exp = x->fp_exp;
		r->fp_sticky = fpu_shr(y, x->fp_exp - y->fp_exp);
	}
	r->fp_sign = x->fp_sign;
	if (x->fp_sign == y->fp_sign) {
		FPU_DECL_CARRY

		/*
		 * The signs match, so we simply add the numbers.  The result
		 * may be `supernormal' (as big as 1.111...1 + 1.111...1, or
		 * 11.111...0).  If so, a single bit shift-right will fix it
		 * (but remember to adjust the exponent).
		 */
		/* r->fp_mant = x->fp_mant + y->fp_mant */
		FPU_ADDS(r->fp_mant[3], x->fp_mant[3], y->fp_mant[3]);
		FPU_ADDCS(r->fp_mant[2], x->fp_mant[2], y->fp_mant[2]);
		FPU_ADDCS(r->fp_mant[1], x->fp_mant[1], y->fp_mant[1]);
		FPU_ADDC(r0, x->fp_mant[0], y->fp_mant[0]);
		if ((r->fp_mant[0] = r0) >= FP_2) {
			(void) fpu_shr(r, 1);
			r->fp_exp++;
		}
	} else {
		FPU_DECL_CARRY

		/*
		 * The signs differ, so things are rather more difficult.
		 * H&P would have us negate the negative operand and add;
		 * this is the same as subtracting the negative operand.
		 * This is quite a headache.  Instead, we will subtract
		 * y from x, regardless of whether y itself is the negative
		 * operand.  When this is done one of three conditions will
		 * hold, depending on the magnitudes of x and y:
		 *   case i)   |x| > |y|.  The result is just x - y,
		 *	with x's sign, but it may need to be normalized.
		 *   case ii)  |x| = |y|.  The result is 0 (maybe -0)
		 *	so must be fixed up.
		 *   case iii) |x| < |y|.  We goofed; the result should
		 *	be (y - x), with the same sign as y.
		 * We could compare |x| and |y| here and avoid case iii,
		 * but that would take just as much work as the subtract.
		 * We can tell case iii has occurred by an overflow.
		 *
		 * N.B.: since x->fp_exp >= y->fp_exp, x->fp_sticky = 0.
		 */
		/* r->fp_mant = x->fp_mant - y->fp_mant */
		FPU_SET_CARRY(y->fp_sticky);
		FPU_SUBCS(r3, x->fp_mant[3], y->fp_mant[3]);
		FPU_SUBCS(r2, x->fp_mant[2], y->fp_mant[2]);
		FPU_SUBCS(r1, x->fp_mant[1], y->fp_mant[1]);
		FPU_SUBC(r0, x->fp_mant[0], y->fp_mant[0]);
		if (r0 < FP_2) {
			/* cases i and ii */
			if ((r0 | r1 | r2 | r3) == 0) {
				/* case ii */
				r->fp_class = FPC_ZERO;
				r->fp_sign = rd == FP_RM;
				return (r);
			}
		} else {
			/*
			 * Oops, case iii.  This can only occur when the
			 * exponents were equal, in which case neither
			 * x nor y have sticky bits set.  Flip the sign
			 * (to y's sign) and negate the result to get y - x.
			 */
#ifdef DIAGNOSTIC
			if (x->fp_exp != y->fp_exp || r->fp_sticky)
				panic("fpu_add");
#endif
			r->fp_sign = y->fp_sign;
			FPU_SUBS(r3, 0, r3);
			FPU_SUBCS(r2, 0, r2);
			FPU_SUBCS(r1, 0, r1);
			FPU_SUBC(r0, 0, r0);
		}
		r->fp_mant[3] = r3;
		r->fp_mant[2] = r2;
		r->fp_mant[1] = r1;
		r->fp_mant[0] = r0;
		if (r0 < FP_1)
			fpu_norm(r);
	}
	DUMPFPN(FPE_REG, r);
	return (r);
}
