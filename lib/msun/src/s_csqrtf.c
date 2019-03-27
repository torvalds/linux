/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007 David Schultz <das@FreeBSD.ORG>
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
#include <math.h>

#include "math_private.h"

float complex
csqrtf(float complex z)
{
	double t;
	float a, b;

	a = creal(z);
	b = cimag(z);

	/* Handle special cases. */
	if (z == 0)
		return (CMPLXF(0, b));
	if (isinf(b))
		return (CMPLXF(INFINITY, b));
	if (isnan(a)) {
		t = (b - b) / (b - b);	/* raise invalid if b is not a NaN */
		return (CMPLXF(a + 0.0L + t, a + 0.0L + t)); /* NaN + NaN i */
	}
	if (isinf(a)) {
		/*
		 * csqrtf(inf + NaN i)  = inf +  NaN i
		 * csqrtf(inf + y i)    = inf +  0 i
		 * csqrtf(-inf + NaN i) = NaN +- inf i
		 * csqrtf(-inf + y i)   = 0   +  inf i
		 */
		if (signbit(a))
			return (CMPLXF(fabsf(b - b), copysignf(a, b)));
		else
			return (CMPLXF(a, copysignf(b - b, b)));
	}
	if (isnan(b)) {
		t = (a - a) / (a - a);	/* raise invalid */
		return (CMPLXF(b + 0.0L + t, b + 0.0L + t)); /* NaN + NaN i */
	}

	/*
	 * We compute t in double precision to avoid overflow and to
	 * provide correct rounding in nearly all cases.
	 * This is Algorithm 312, CACM vol 10, Oct 1967.
	 */
	if (a >= 0) {
		t = sqrt((a + hypot(a, b)) * 0.5);
		return (CMPLXF(t, b / (2 * t)));
	} else {
		t = sqrt((-a + hypot(a, b)) * 0.5);
		return (CMPLXF(fabsf(b) / (2 * t), copysignf(t, b)));
	}
}
