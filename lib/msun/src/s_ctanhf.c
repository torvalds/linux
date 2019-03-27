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
 * Hyperbolic tangent of a complex argument z.  See s_ctanh.c for details.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <complex.h>
#include <math.h>

#include "math_private.h"

float complex
ctanhf(float complex z)
{
	float x, y;
	float t, beta, s, rho, denom;
	uint32_t hx, ix;

	x = crealf(z);
	y = cimagf(z);

	GET_FLOAT_WORD(hx, x);
	ix = hx & 0x7fffffff;

	if (ix >= 0x7f800000) {
		if (ix & 0x7fffff)
			return (CMPLXF(nan_mix(x, y),
			    y == 0 ? y : nan_mix(x, y)));
		SET_FLOAT_WORD(x, hx - 0x40000000);
		return (CMPLXF(x,
		    copysignf(0, isinf(y) ? y : sinf(y) * cosf(y))));
	}

	if (!isfinite(y))
		return (CMPLXF(y - y, y - y));

	if (ix >= 0x41300000) {	/* |x| >= 11 */
		float exp_mx = expf(-fabsf(x));
		return (CMPLXF(copysignf(1, x),
		    4 * sinf(y) * cosf(y) * exp_mx * exp_mx));
	}

	t = tanf(y);
	beta = 1.0 + t * t;
	s = sinhf(x);
	rho = sqrtf(1 + s * s);
	denom = 1 + beta * s * s;
	return (CMPLXF((beta * rho * s) / denom, t / denom));
}

float complex
ctanf(float complex z)
{

	z = ctanhf(CMPLXF(cimagf(z), crealf(z)));
	return (CMPLXF(cimagf(z), crealf(z)));
}

