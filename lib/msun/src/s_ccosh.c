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
 * Hyperbolic cosine of a complex argument z = x + i y.
 *
 * cosh(z) = cosh(x+iy)
 *         = cosh(x) cos(y) + i sinh(x) sin(y).
 *
 * Exceptional values are noted in the comments within the source code.
 * These values and the return value were taken from n1124.pdf.
 * The sign of the result for some exceptional values is unspecified but
 * must satisfy both cosh(conj(z)) == conj(cosh(z)) and cosh(-z) == cosh(z).
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <complex.h>
#include <math.h>

#include "math_private.h"

static const double huge = 0x1p1023;

double complex
ccosh(double complex z)
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
			return (CMPLX(cosh(x), x * y));
		if (ix < 0x40360000)	/* |x| < 22: normal case */
			return (CMPLX(cosh(x) * cos(y), sinh(x) * sin(y)));

		/* |x| >= 22, so cosh(x) ~= exp(|x|) */
		if (ix < 0x40862e42) {
			/* x < 710: exp(|x|) won't overflow */
			h = exp(fabs(x)) * 0.5;
			return (CMPLX(h * cos(y), copysign(h, x) * sin(y)));
		} else if (ix < 0x4096bbaa) {
			/* x < 1455: scale to avoid overflow */
			z = __ldexp_cexp(CMPLX(fabs(x), y), -1);
			return (CMPLX(creal(z), cimag(z) * copysign(1, x)));
		} else {
			/* x >= 1455: the result always overflows */
			h = huge * x;
			return (CMPLX(h * h * cos(y), h * sin(y)));
		}
	}

	/*
	 * cosh(+-0 +- I Inf) = dNaN + I (+-)(+-)0.
	 * The sign of 0 in the result is unspecified.  Choice = product
	 * of the signs of the argument.  Raise the invalid floating-point
	 * exception.
	 *
	 * cosh(+-0 +- I NaN) = d(NaN) + I (+-)(+-)0.
	 * The sign of 0 in the result is unspecified.  Choice = product
	 * of the signs of the argument.
	 */
	if ((ix | lx) == 0)		/* && iy >= 0x7ff00000 */
		return (CMPLX(y - y, x * copysign(0, y)));

	/*
	 * cosh(+-Inf +- I 0) = +Inf + I (+-)(+-)0.
	 *
	 * cosh(NaN +- I 0)   = d(NaN) + I (+-)(+-)0.
	 * The sign of 0 in the result is unspecified.  Choice = product
	 * of the signs of the argument.
	 */
	if ((iy | ly) == 0)		/* && ix >= 0x7ff00000 */
		return (CMPLX(x * x, copysign(0, x) * y));

	/*
	 * cosh(x +- I Inf) = dNaN + I dNaN.
	 * Raise the invalid floating-point exception for finite nonzero x.
	 *
	 * cosh(x + I NaN) = d(NaN) + I d(NaN).
	 * Optionally raises the invalid floating-point exception for finite
	 * nonzero x.  Choice = don't raise (except for signaling NaNs).
	 */
	if (ix < 0x7ff00000)		/* && iy >= 0x7ff00000 */
		return (CMPLX(y - y, x * (y - y)));

	/*
	 * cosh(+-Inf + I NaN)  = +Inf + I d(NaN).
	 *
	 * cosh(+-Inf +- I Inf) = +Inf + I dNaN.
	 * The sign of Inf in the result is unspecified.  Choice = always +.
	 * Raise the invalid floating-point exception.
	 *
	 * cosh(+-Inf + I y)   = +Inf cos(y) +- I Inf sin(y)
	 */
	if (ix == 0x7ff00000 && lx == 0) {
		if (iy >= 0x7ff00000)
			return (CMPLX(INFINITY, x * (y - y)));
		return (CMPLX(INFINITY * cos(y), x * sin(y)));
	}

	/*
	 * cosh(NaN + I NaN)  = d(NaN) + I d(NaN).
	 *
	 * cosh(NaN +- I Inf) = d(NaN) + I d(NaN).
	 * Optionally raises the invalid floating-point exception.
	 * Choice = raise.
	 *
	 * cosh(NaN + I y)    = d(NaN) + I d(NaN).
	 * Optionally raises the invalid floating-point exception for finite
	 * nonzero y.  Choice = don't raise (except for signaling NaNs).
	 */
	return (CMPLX(((long double)x * x) * (y - y),
	    ((long double)x + x) * (y - y)));
}

double complex
ccos(double complex z)
{

	/* ccos(z) = ccosh(I * z) */
	return (ccosh(CMPLX(-cimag(z), creal(z))));
}
