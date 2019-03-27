/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Stephen Montgomery-Smith <stephen@FreeBSD.ORG>
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

/*
 * The algorithm is very close to that in "Implementing the complex arcsine
 * and arccosine functions using exception handling" by T. E. Hull, Thomas F.
 * Fairgrieve, and Ping Tak Peter Tang, published in ACM Transactions on
 * Mathematical Software, Volume 23 Issue 3, 1997, Pages 299-335,
 * http://dl.acm.org/citation.cfm?id=275324.
 *
 * See catrig.c for complete comments.
 *
 * XXX comments were removed automatically, and even short ones on the right
 * of statements were removed (all of them), contrary to normal style.  Only
 * a few comments on the right of declarations remain.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <complex.h>
#include <float.h>

#include "math.h"
#include "math_private.h"

#undef isinf
#define isinf(x)	(fabsf(x) == INFINITY)
#undef isnan
#define isnan(x)	((x) != (x))
#define	raise_inexact()	do { volatile float junk __unused = 1 + tiny; } while(0)
#undef signbit
#define signbit(x)	(__builtin_signbitf(x))

static const float
A_crossover =		10,
B_crossover =		0.6417,
FOUR_SQRT_MIN =		0x1p-61,
QUARTER_SQRT_MAX =	0x1p61,
m_e =			2.7182818285e0,		/*  0xadf854.0p-22 */
m_ln2 =			6.9314718056e-1,	/*  0xb17218.0p-24 */
pio2_hi =		1.5707962513e0,		/*  0xc90fda.0p-23 */
RECIP_EPSILON =		1 / FLT_EPSILON,
SQRT_3_EPSILON =	5.9801995673e-4,	/*  0x9cc471.0p-34 */
SQRT_6_EPSILON =	8.4572793338e-4,	/*  0xddb3d7.0p-34 */
SQRT_MIN =		0x1p-63;

static const volatile float
pio2_lo =		7.5497899549e-8,	/*  0xa22169.0p-47 */
tiny =			0x1p-100;

static float complex clog_for_large_values(float complex z);

static inline float
f(float a, float b, float hypot_a_b)
{
	if (b < 0)
		return ((hypot_a_b - b) / 2);
	if (b == 0)
		return (a / 2);
	return (a * a / (hypot_a_b + b) / 2);
}

static inline void
do_hard_work(float x, float y, float *rx, int *B_is_usable, float *B,
    float *sqrt_A2my2, float *new_y)
{
	float R, S, A;
	float Am1, Amy;

	R = hypotf(x, y + 1);
	S = hypotf(x, y - 1);

	A = (R + S) / 2;
	if (A < 1)
		A = 1;

	if (A < A_crossover) {
		if (y == 1 && x < FLT_EPSILON * FLT_EPSILON / 128) {
			*rx = sqrtf(x);
		} else if (x >= FLT_EPSILON * fabsf(y - 1)) {
			Am1 = f(x, 1 + y, R) + f(x, 1 - y, S);
			*rx = log1pf(Am1 + sqrtf(Am1 * (A + 1)));
		} else if (y < 1) {
			*rx = x / sqrtf((1 - y) * (1 + y));
		} else {
			*rx = log1pf((y - 1) + sqrtf((y - 1) * (y + 1)));
		}
	} else {
		*rx = logf(A + sqrtf(A * A - 1));
	}

	*new_y = y;

	if (y < FOUR_SQRT_MIN) {
		*B_is_usable = 0;
		*sqrt_A2my2 = A * (2 / FLT_EPSILON);
		*new_y = y * (2 / FLT_EPSILON);
		return;
	}

	*B = y / A;
	*B_is_usable = 1;

	if (*B > B_crossover) {
		*B_is_usable = 0;
		if (y == 1 && x < FLT_EPSILON / 128) {
			*sqrt_A2my2 = sqrtf(x) * sqrtf((A + y) / 2);
		} else if (x >= FLT_EPSILON * fabsf(y - 1)) {
			Amy = f(x, y + 1, R) + f(x, y - 1, S);
			*sqrt_A2my2 = sqrtf(Amy * (A + y));
		} else if (y > 1) {
			*sqrt_A2my2 = x * (4 / FLT_EPSILON / FLT_EPSILON) * y /
			    sqrtf((y + 1) * (y - 1));
			*new_y = y * (4 / FLT_EPSILON / FLT_EPSILON);
		} else {
			*sqrt_A2my2 = sqrtf((1 - y) * (1 + y));
		}
	}
}

float complex
casinhf(float complex z)
{
	float x, y, ax, ay, rx, ry, B, sqrt_A2my2, new_y;
	int B_is_usable;
	float complex w;

	x = crealf(z);
	y = cimagf(z);
	ax = fabsf(x);
	ay = fabsf(y);

	if (isnan(x) || isnan(y)) {
		if (isinf(x))
			return (CMPLXF(x, y + y));
		if (isinf(y))
			return (CMPLXF(y, x + x));
		if (y == 0)
			return (CMPLXF(x + x, y));
		return (CMPLXF(nan_mix(x, y), nan_mix(x, y)));
	}

	if (ax > RECIP_EPSILON || ay > RECIP_EPSILON) {
		if (signbit(x) == 0)
			w = clog_for_large_values(z) + m_ln2;
		else
			w = clog_for_large_values(-z) + m_ln2;
		return (CMPLXF(copysignf(crealf(w), x),
		    copysignf(cimagf(w), y)));
	}

	if (x == 0 && y == 0)
		return (z);

	raise_inexact();

	if (ax < SQRT_6_EPSILON / 4 && ay < SQRT_6_EPSILON / 4)
		return (z);

	do_hard_work(ax, ay, &rx, &B_is_usable, &B, &sqrt_A2my2, &new_y);
	if (B_is_usable)
		ry = asinf(B);
	else
		ry = atan2f(new_y, sqrt_A2my2);
	return (CMPLXF(copysignf(rx, x), copysignf(ry, y)));
}

float complex
casinf(float complex z)
{
	float complex w = casinhf(CMPLXF(cimagf(z), crealf(z)));

	return (CMPLXF(cimagf(w), crealf(w)));
}

float complex
cacosf(float complex z)
{
	float x, y, ax, ay, rx, ry, B, sqrt_A2mx2, new_x;
	int sx, sy;
	int B_is_usable;
	float complex w;

	x = crealf(z);
	y = cimagf(z);
	sx = signbit(x);
	sy = signbit(y);
	ax = fabsf(x);
	ay = fabsf(y);

	if (isnan(x) || isnan(y)) {
		if (isinf(x))
			return (CMPLXF(y + y, -INFINITY));
		if (isinf(y))
			return (CMPLXF(x + x, -y));
		if (x == 0)
			return (CMPLXF(pio2_hi + pio2_lo, y + y));
		return (CMPLXF(nan_mix(x, y), nan_mix(x, y)));
	}

	if (ax > RECIP_EPSILON || ay > RECIP_EPSILON) {
		w = clog_for_large_values(z);
		rx = fabsf(cimagf(w));
		ry = crealf(w) + m_ln2;
		if (sy == 0)
			ry = -ry;
		return (CMPLXF(rx, ry));
	}

	if (x == 1 && y == 0)
		return (CMPLXF(0, -y));

	raise_inexact();

	if (ax < SQRT_6_EPSILON / 4 && ay < SQRT_6_EPSILON / 4)
		return (CMPLXF(pio2_hi - (x - pio2_lo), -y));

	do_hard_work(ay, ax, &ry, &B_is_usable, &B, &sqrt_A2mx2, &new_x);
	if (B_is_usable) {
		if (sx == 0)
			rx = acosf(B);
		else
			rx = acosf(-B);
	} else {
		if (sx == 0)
			rx = atan2f(sqrt_A2mx2, new_x);
		else
			rx = atan2f(sqrt_A2mx2, -new_x);
	}
	if (sy == 0)
		ry = -ry;
	return (CMPLXF(rx, ry));
}

float complex
cacoshf(float complex z)
{
	float complex w;
	float rx, ry;

	w = cacosf(z);
	rx = crealf(w);
	ry = cimagf(w);
	if (isnan(rx) && isnan(ry))
		return (CMPLXF(ry, rx));
	if (isnan(rx))
		return (CMPLXF(fabsf(ry), rx));
	if (isnan(ry))
		return (CMPLXF(ry, ry));
	return (CMPLXF(fabsf(ry), copysignf(rx, cimagf(z))));
}

static float complex
clog_for_large_values(float complex z)
{
	float x, y;
	float ax, ay, t;

	x = crealf(z);
	y = cimagf(z);
	ax = fabsf(x);
	ay = fabsf(y);
	if (ax < ay) {
		t = ax;
		ax = ay;
		ay = t;
	}

	if (ax > FLT_MAX / 2)
		return (CMPLXF(logf(hypotf(x / m_e, y / m_e)) + 1,
		    atan2f(y, x)));

	if (ax > QUARTER_SQRT_MAX || ay < SQRT_MIN)
		return (CMPLXF(logf(hypotf(x, y)), atan2f(y, x)));

	return (CMPLXF(logf(ax * ax + ay * ay) / 2, atan2f(y, x)));
}

static inline float
sum_squares(float x, float y)
{

	if (y < SQRT_MIN)
		return (x * x);

	return (x * x + y * y);
}

static inline float
real_part_reciprocal(float x, float y)
{
	float scale;
	uint32_t hx, hy;
	int32_t ix, iy;

	GET_FLOAT_WORD(hx, x);
	ix = hx & 0x7f800000;
	GET_FLOAT_WORD(hy, y);
	iy = hy & 0x7f800000;
#define	BIAS	(FLT_MAX_EXP - 1)
#define	CUTOFF	(FLT_MANT_DIG / 2 + 1)
	if (ix - iy >= CUTOFF << 23 || isinf(x))
		return (1 / x);
	if (iy - ix >= CUTOFF << 23)
		return (x / y / y);
	if (ix <= (BIAS + FLT_MAX_EXP / 2 - CUTOFF) << 23)
		return (x / (x * x + y * y));
	SET_FLOAT_WORD(scale, 0x7f800000 - ix);
	x *= scale;
	y *= scale;
	return (x / (x * x + y * y) * scale);
}

float complex
catanhf(float complex z)
{
	float x, y, ax, ay, rx, ry;

	x = crealf(z);
	y = cimagf(z);
	ax = fabsf(x);
	ay = fabsf(y);

	if (y == 0 && ax <= 1)
		return (CMPLXF(atanhf(x), y));

	if (x == 0)
		return (CMPLXF(x, atanf(y)));

	if (isnan(x) || isnan(y)) {
		if (isinf(x))
			return (CMPLXF(copysignf(0, x), y + y));
		if (isinf(y))
			return (CMPLXF(copysignf(0, x),
			    copysignf(pio2_hi + pio2_lo, y)));
		return (CMPLXF(nan_mix(x, y), nan_mix(x, y)));
	}

	if (ax > RECIP_EPSILON || ay > RECIP_EPSILON)
		return (CMPLXF(real_part_reciprocal(x, y),
		    copysignf(pio2_hi + pio2_lo, y)));

	if (ax < SQRT_3_EPSILON / 2 && ay < SQRT_3_EPSILON / 2) {
		raise_inexact();
		return (z);
	}

	if (ax == 1 && ay < FLT_EPSILON)
		rx = (m_ln2 - logf(ay)) / 2;
	else
		rx = log1pf(4 * ax / sum_squares(ax - 1, ay)) / 4;

	if (ax == 1)
		ry = atan2f(2, -ay) / 2;
	else if (ay < FLT_EPSILON)
		ry = atan2f(2 * ay, (1 - ax) * (1 + ax)) / 2;
	else
		ry = atan2f(2 * ay, (1 - ax) * (1 + ax) - ay * ay) / 2;

	return (CMPLXF(copysignf(rx, x), copysignf(ry, y)));
}

float complex
catanf(float complex z)
{
	float complex w = catanhf(CMPLXF(cimagf(z), crealf(z)));

	return (CMPLXF(cimagf(w), crealf(w)));
}
