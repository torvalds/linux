/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 David Schultz
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
 * Hyperbolic tangent of a complex argument z = x + I y.
 *
 * The algorithm is from:
 *
 *   W. Kahan.  Branch Cuts for Complex Elementary Functions or Much
 *   Ado About Nothing's Sign Bit.  In The State of the Art in
 *   Numerical Analysis, pp. 165 ff.  Iserles and Powell, eds., 1987.
 *
 * Method:
 *
 *   Let t    = tan(x)
 *       beta = 1/cos^2(y)
 *       s    = sinh(x)
 *       rho  = cosh(x)
 *
 *   We have:
 *
 *   tanh(z) = sinh(z) / cosh(z)
 *
 *             sinh(x) cos(y) + I cosh(x) sin(y)
 *           = ---------------------------------
 *             cosh(x) cos(y) + I sinh(x) sin(y)
 *
 *             cosh(x) sinh(x) / cos^2(y) + I tan(y)
 *           = -------------------------------------
 *                    1 + sinh^2(x) / cos^2(y)
 *
 *             beta rho s + I t
 *           = ----------------
 *               1 + beta s^2
 *
 * Modifications:
 *
 *   I omitted the original algorithm's handling of overflow in tan(x) after
 *   verifying with nearpi.c that this can't happen in IEEE single or double
 *   precision.  I also handle large x differently.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <complex.h>
#include <math.h>

#include "math_private.h"

double complex
ctanh(double complex z)
{
	double x, y;
	double t, beta, s, rho, denom;
	uint32_t hx, ix, lx;

	x = creal(z);
	y = cimag(z);

	EXTRACT_WORDS(hx, lx, x);
	ix = hx & 0x7fffffff;

	/*
	 * ctanh(NaN +- I 0) = d(NaN) +- I 0
	 *
	 * ctanh(NaN + I y) = d(NaN,y) + I d(NaN,y)	for y != 0
	 *
	 * The imaginary part has the sign of x*sin(2*y), but there's no
	 * special effort to get this right.
	 *
	 * ctanh(+-Inf +- I Inf) = +-1 +- I 0
	 *
	 * ctanh(+-Inf + I y) = +-1 + I 0 sin(2y)	for y finite
	 *
	 * The imaginary part of the sign is unspecified.  This special
	 * case is only needed to avoid a spurious invalid exception when
	 * y is infinite.
	 */
	if (ix >= 0x7ff00000) {
		if ((ix & 0xfffff) | lx)	/* x is NaN */
			return (CMPLX(nan_mix(x, y),
			    y == 0 ? y : nan_mix(x, y)));
		SET_HIGH_WORD(x, hx - 0x40000000);	/* x = copysign(1, x) */
		return (CMPLX(x, copysign(0, isinf(y) ? y : sin(y) * cos(y))));
	}

	/*
	 * ctanh(x + I NaN) = d(NaN) + I d(NaN)
	 * ctanh(x +- I Inf) = dNaN + I dNaN
	 */
	if (!isfinite(y))
		return (CMPLX(y - y, y - y));

	/*
	 * ctanh(+-huge +- I y) ~= +-1 +- I 2sin(2y)/exp(2x), using the
	 * approximation sinh^2(huge) ~= exp(2*huge) / 4.
	 * We use a modified formula to avoid spurious overflow.
	 */
	if (ix >= 0x40360000) {	/* |x| >= 22 */
		double exp_mx = exp(-fabs(x));
		return (CMPLX(copysign(1, x),
		    4 * sin(y) * cos(y) * exp_mx * exp_mx));
	}

	/* Kahan's algorithm */
	t = tan(y);
	beta = 1.0 + t * t;	/* = 1 / cos^2(y) */
	s = sinh(x);
	rho = sqrt(1 + s * s);	/* = cosh(x) */
	denom = 1 + beta * s * s;
	return (CMPLX((beta * rho * s) / denom, t / denom));
}

double complex
ctan(double complex z)
{

	/* ctan(z) = -I * ctanh(I * z) = I * conj(ctanh(I * conj(z))) */
	z = ctanh(CMPLX(cimag(z), creal(z)));
	return (CMPLX(cimag(z), creal(z)));
}
