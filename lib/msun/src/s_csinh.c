/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Bruce D. Evans and Steven G. Kargl
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Hyperbolic sine of a complex argument z = x + i y.
 *
 * sinh(z) = sinh(x+iy)
 *         = sinh(x) cos(y) + i cosh(x) sin(y).
 *
 * Exceptional values are noted in the comments within the source code.
 * These values and the return value were taken from n1124.pdf.
 * The sign of the result for some exceptional values is unspecified but
 * must satisfy both sinh(conj(z)) == conj(sinh(z)) and sinh(-z) == -sinh(z).
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <complex.h>
#include <math.h>

#include "math_private.h"

static const double huge = 0x1p1023;

double complex
csinh(double complex z)
{
	double x, y, h;
	int32_t hx, hy, ix, iy, lx, ly;

	x = creal(z);
	y = cimag(z);

	EXTRACT_WORDS(hx, lx, x);
	EXTRACT_WORDS(hy, ly, y);

	ix = 0x7fffffff & hx;
	iy = 0x7fffffff & hy;

	/* Handle the nearly-non-exceptional cases where x and y are finite. */
	if (ix < 0x7ff00000 && iy < 0x7ff00000) {
		if ((iy | ly) == 0)
			return (CMPLX(sinh(x), y));
		if (ix < 0x40360000)	/* |x| < 22: normal case */
			return (CMPLX(sinh(x) * cos(y), cosh(x) * sin(y)));

		/* |x| >= 22, so cosh(x) ~= exp(|x|) */
		if (ix < 0x40862e42) {
			/* x < 710: exp(|x|) won't overflow */
			h = exp(fabs(x)) * 0.5;
			return (CMPLX(copysign(h, x) * cos(y), h * sin(y)));
		} else if (ix < 0x4096bbaa) {
			/* x < 1455: scale to avoid overflow */
			z = __ldexp_cexp(CMPLX(fabs(x), y), -1);
			return (CMPLX(creal(z) * copysign(1, x), cimag(z)));
		} else {
			/* x >= 1455: the result always overflows */
			h = huge * x;
			return (CMPLX(h * cos(y), h * h * sin(y)));
		}
	}

	/*
	 * sinh(+-0 +- I Inf) = +-0 + I dNaN.
	 * The sign of 0 in the result is unspecified.  Choice = same sign
	 * as the argument.  Raise the invalid floating-point exception.
	 *
	 * sinh(+-0 +- I NaN) = +-0 + I d(NaN).
	 * The sign of 0 in the result is unspecified.  Choice = same sign
	 * as the argument.
	 */
	if ((ix | lx) == 0)		/* && iy >= 0x7ff00000 */
		return (CMPLX(x, y - y));

	/*
	 * sinh(+-Inf +- I 0) = +-Inf + I +-0.
	 *
	 * sinh(NaN +- I 0)   = d(NaN) + I +-0.
	 */
	if ((iy | ly) == 0)		/* && ix >= 0x7ff00000 */
		return (CMPLX(x + x, y));

	/*
	 * sinh(x +- I Inf) = dNaN + I dNaN.
	 * Raise the invalid floating-point exception for finite nonzero x.
	 *
	 * sinh(x + I NaN) = d(NaN) + I d(NaN).
	 * Optionally raises the invalid floating-point exception for finite
	 * nonzero x.  Choice = don't raise (except for signaling NaNs).
	 */
	if (ix < 0x7ff00000)		/* && iy >= 0x7ff00000 */
		return (CMPLX(y - y, y - y));

	/*
	 * sinh(+-Inf + I NaN)  = +-Inf + I d(NaN).
	 * The sign of Inf in the result is unspecified.  Choice = same sign
	 * as the argument.
	 *
	 * sinh(+-Inf +- I Inf) = +-Inf + I dNaN.
	 * The sign of Inf in the result is unspecified.  Choice = same sign
	 * as the argument.  Raise the invalid floating-point exception.
	 *
	 * sinh(+-Inf + I y)   = +-Inf cos(y) + I Inf sin(y)
	 */
	if (ix == 0x7ff00000 && lx == 0) {
		if (iy >= 0x7ff00000)
			return (CMPLX(x, y - y));
		return (CMPLX(x * cos(y), INFINITY * sin(y)));
	}

	/*
	 * sinh(NaN1 + I NaN2) = d(NaN1, NaN2) + I d(NaN1, NaN2).
	 *
	 * sinh(NaN +- I Inf)  = d(NaN, dNaN) + I d(NaN, dNaN).
	 * Optionally raises the invalid floating-point exception.
	 * Choice = raise.
	 *
	 * sinh(NaN + I y)     = d(NaN) + I d(NaN).
	 * Optionally raises the invalid floating-point exception for finite
	 * nonzero y.  Choice = don't raise (except for signaling NaNs).
	 */
	return (CMPLX(((long double)x + x) * (y - y),
	    ((long double)x * x) * (y - y)));
}

double complex
csin(double complex z)
{

	/* csin(z) = -I * csinh(I * z) = I * conj(csinh(I * conj(z))). */
	z = csinh(CMPLX(cimag(z), creal(z)));
	return (CMPLX(cimag(z), creal(z)));
}
