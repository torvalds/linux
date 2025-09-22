/*	$OpenBSD: modf.c,v 1.7 2023/08/13 06:55:37 miod Exp $	*/
/*	$NetBSD: modf.c,v 1.1 1995/02/10 17:50:25 cgd Exp $	*/

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

#include <sys/types.h>
#include <machine/ieee.h>
#include <errno.h>
#include <float.h>
#include <math.h>

/*
 * double modf(double val, double *iptr)
 * returns: f and i such that |f| < 1.0, (f + i) = val, and
 *	sign(f) == sign(i) == sign(val).
 *
 * Beware signedness when doing subtraction, and also operand size!
 */
double
modf(double val, double *iptr)
{
	union doub {
		double v;
		struct ieee_double s;
	} u, v;
	u_int64_t frac;

	/*
	 * If input is +/-Inf or NaN, return +/-0 or NaN.
	 */
	u.v = val;
	if (u.s.dbl_exp == DBL_EXP_INFNAN) {
		*iptr = u.v;
		return (0.0 / u.v);
	}

	/*
	 * If input can't have a fractional part, return
	 * (appropriately signed) zero, and make i be the input.
	 */
	if ((int)u.s.dbl_exp - DBL_EXP_BIAS > DBL_FRACBITS - 1) {
		*iptr = u.v;
		v.v = 0.0;
		v.s.dbl_sign = u.s.dbl_sign;
		return (v.v);
	}

	/*
	 * If |input| < 1.0, return it, and set i to the appropriately
	 * signed zero.
	 */
	if (u.s.dbl_exp < DBL_EXP_BIAS) {
		v.v = 0.0;
		v.s.dbl_sign = u.s.dbl_sign;
		*iptr = v.v;
		return (u.v);
	}

	/*
	 * There can be a fractional part of the input.
	 * If you look at the math involved for a few seconds, it's
	 * plain to see that the integral part is the input, with the
	 * low (DBL_FRACBITS - (exponent - DBL_EXP_BIAS)) bits zeroed,
	 * the fractional part is the part with the rest of the
	 * bits zeroed.  Just zeroing the high bits to get the
	 * fractional part would yield a fraction in need of
	 * normalization.  Therefore, we take the easy way out, and
	 * just use subtraction to get the fractional part.
	 */
	v.v = u.v;
	/* Zero the low bits of the fraction, the sleazy way. */
	frac = ((u_int64_t)v.s.dbl_frach << 32) + v.s.dbl_fracl;
	frac >>= DBL_FRACBITS - (u.s.dbl_exp - DBL_EXP_BIAS);
	frac <<= DBL_FRACBITS - (u.s.dbl_exp - DBL_EXP_BIAS);
	v.s.dbl_fracl = frac & 0xffffffff;
	v.s.dbl_frach = frac >> 32;
	*iptr = v.v;

	u.v -= v.v;
	u.s.dbl_sign = v.s.dbl_sign;
	return (u.v);
}

#if	LDBL_MANT_DIG == DBL_MANT_DIG
__strong_alias(modfl, modf);
#endif	/* LDBL_MANT_DIG == DBL_MANT_DIG */
