/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 David Schultz <das@FreeBSD.ORG>
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

#include "math.h"
#include "math_private.h"

static const uint32_t k = 235;			/* constant for reduction */
static const float kln2 =  162.88958740F;	/* k * ln2 */

/*
 * See k_exp.c for details.
 *
 * Input:  ln(FLT_MAX) <= x < ln(2 * FLT_MAX / FLT_MIN_DENORM) ~= 192.7
 * Output: 2**127 <= y < 2**128
 */
static float
__frexp_expf(float x, int *expt)
{
	float exp_x;
	uint32_t hx;

	exp_x = expf(x - kln2);
	GET_FLOAT_WORD(hx, exp_x);
	*expt = (hx >> 23) - (0x7f + 127) + k;
	SET_FLOAT_WORD(exp_x, (hx & 0x7fffff) | ((0x7f + 127) << 23));
	return (exp_x);
}

float
__ldexp_expf(float x, int expt)
{
	float exp_x, scale;
	int ex_expt;

	exp_x = __frexp_expf(x, &ex_expt);
	expt += ex_expt;
	SET_FLOAT_WORD(scale, (0x7f + expt) << 23);
	return (exp_x * scale);
}

float complex
__ldexp_cexpf(float complex z, int expt)
{
	float x, y, exp_x, scale1, scale2;
	int ex_expt, half_expt;

	x = crealf(z);
	y = cimagf(z);
	exp_x = __frexp_expf(x, &ex_expt);
	expt += ex_expt;

	half_expt = expt / 2;
	SET_FLOAT_WORD(scale1, (0x7f + half_expt) << 23);
	half_expt = expt - half_expt;
	SET_FLOAT_WORD(scale2, (0x7f + half_expt) << 23);

	return (CMPLXF(cosf(y) * exp_x * scale1 * scale2,
	    sinf(y) * exp_x * scale1 * scale2));
}
