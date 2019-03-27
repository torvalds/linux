/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007-2008 David Schultz <das@FreeBSD.ORG>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <complex.h>
#include <float.h>
#include <math.h>

#include "math_private.h"

/*
 * Several thresholds require a 15-bit exponent and also the usual bias.
 * s_logl.c and e_hypotl have less hard-coding but end up requiring the
 * same for the exponent and more for the mantissa.
 */
#if LDBL_MAX_EXP != 0x4000
#error "Unsupported long double format"
#endif

/*
 * Overflow must be avoided for components >= LDBL_MAX / (1 + sqrt(2)).
 * The precise threshold is nontrivial to determine and spell, so use a
 * lower threshold of approximaely LDBL_MAX / 4, and don't use LDBL_MAX
 * to spell this since LDBL_MAX is broken on i386 (it overflows in 53-bit
 * precision).
 */
#define	THRESH	0x1p16382L

long double complex
csqrtl(long double complex z)
{
	long double complex result;
	long double a, b, rx, ry, scale, t;

	a = creall(z);
	b = cimagl(z);

	/* Handle special cases. */
	if (z == 0)
		return (CMPLXL(0, b));
	if (isinf(b))
		return (CMPLXL(INFINITY, b));
	if (isnan(a)) {
		t = (b - b) / (b - b);	/* raise invalid if b is not a NaN */
		return (CMPLXL(a + 0.0L + t, a + 0.0L + t)); /* NaN + NaN i */
	}
	if (isinf(a)) {
		/*
		 * csqrt(inf + NaN i)  = inf +  NaN i
		 * csqrt(inf + y i)    = inf +  0 i
		 * csqrt(-inf + NaN i) = NaN +- inf i
		 * csqrt(-inf + y i)   = 0   +  inf i
		 */
		if (signbit(a))
			return (CMPLXL(fabsl(b - b), copysignl(a, b)));
		else
			return (CMPLXL(a, copysignl(b - b, b)));
	}
	if (isnan(b)) {
		t = (a - a) / (a - a);	/* raise invalid */
		return (CMPLXL(b + 0.0L + t, b + 0.0L + t)); /* NaN + NaN i */
	}

	/* Scale to avoid overflow. */
	if (fabsl(a) >= THRESH || fabsl(b) >= THRESH) {
		/*
		 * Don't scale a or b if this might give (spurious)
		 * underflow.  Then the unscaled value is an equivalent
		 * infinitesmal (or 0).
		 */
		if (fabsl(a) >= 0x1p-16380L)
			a *= 0.25;
		if (fabsl(b) >= 0x1p-16380L)
			b *= 0.25;
		scale = 2;
	} else {
		scale = 1;
	}

	/* Scale to reduce inaccuracies when both components are denormal. */
	if (fabsl(a) < 0x1p-16382L && fabsl(b) < 0x1p-16382L) {
		a *= 0x1p64;
		b *= 0x1p64;
		scale = 0x1p-32;
	}

	/* Algorithm 312, CACM vol 10, Oct 1967. */
	if (a >= 0) {
		t = sqrtl((a + hypotl(a, b)) * 0.5);
		rx = scale * t;
		ry = scale * b / (2 * t);
	} else {
		t = sqrtl((-a + hypotl(a, b)) * 0.5);
		rx = scale * fabsl(b) / (2 * t);
		ry = copysignl(scale * t, b);
	}

	return (CMPLXL(rx, ry));
}
