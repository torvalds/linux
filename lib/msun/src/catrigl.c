/*-
 * Copyright (c) 2012 Stephen Montgomery-Smith <stephen@FreeBSD.ORG>
 * Copyright (c) 2017 Mahdi Mokhtari <mmokhi@FreeBSD.org>
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

#include "invtrig.h"
#include "math.h"
#include "math_private.h"

#undef isinf
#define isinf(x)	(fabsl(x) == INFINITY)
#undef isnan
#define isnan(x)	((x) != (x))
#define	raise_inexact()	do { volatile float junk __unused = 1 + tiny; } while(0)
#undef signbit
#define signbit(x)	(__builtin_signbitl(x))

#if LDBL_MAX_EXP != 0x4000
#error "Unsupported long double format"
#endif

static const long double
A_crossover =		10,
B_crossover =		0.6417,
FOUR_SQRT_MIN =		0x1p-8189L,
HALF_MAX =		0x1p16383L,
QUARTER_SQRT_MAX =	0x1p8189L,
RECIP_EPSILON =		1 / LDBL_EPSILON,
SQRT_MIN =		0x1p-8191L;

#if LDBL_MANT_DIG == 64
static const union IEEEl2bits
um_e =		LD80C(0xadf85458a2bb4a9b,  1, 2.71828182845904523536e+0L),
um_ln2 =	LD80C(0xb17217f7d1cf79ac, -1, 6.93147180559945309417e-1L);
#define		m_e	um_e.e
#define		m_ln2	um_ln2.e
static const long double
/* The next 2 literals for non-i386.  Misrounding them on i386 is harmless. */
SQRT_3_EPSILON = 5.70316273435758915310e-10,	/*  0x9cc470a0490973e8.0p-94 */
SQRT_6_EPSILON = 8.06549008734932771664e-10;	/*  0xddb3d742c265539e.0p-94 */
#elif LDBL_MANT_DIG == 113
static const long double
m_e =		2.71828182845904523536028747135266250e0L,	/* 0x15bf0a8b1457695355fb8ac404e7a.0p-111 */
m_ln2 =		6.93147180559945309417232121458176568e-1L,	/* 0x162e42fefa39ef35793c7673007e6.0p-113 */
SQRT_3_EPSILON = 2.40370335797945490975336727199878124e-17,	/*  0x1bb67ae8584caa73b25742d7078b8.0p-168 */
SQRT_6_EPSILON = 3.39934988877629587239082586223300391e-17;	/*  0x13988e1409212e7d0321914321a55.0p-167 */
#else
#error "Unsupported long double format"
#endif

static const volatile float
tiny =			0x1p-100;

static long double complex clog_for_large_values(long double complex z);

static inline long double
f(long double a, long double b, long double hypot_a_b)
{
	if (b < 0)
		return ((hypot_a_b - b) / 2);
	if (b == 0)
		return (a / 2);
	return (a * a / (hypot_a_b + b) / 2);
}

static inline void
do_hard_work(long double x, long double y, long double *rx, int *B_is_usable,
    long double *B, long double *sqrt_A2my2, long double *new_y)
{
	long double R, S, A;
	long double Am1, Amy;

	R = hypotl(x, y + 1);
	S = hypotl(x, y - 1);

	A = (R + S) / 2;
	if (A < 1)
		A = 1;

	if (A < A_crossover) {
		if (y == 1 && x < LDBL_EPSILON * LDBL_EPSILON / 128) {
			*rx = sqrtl(x);
		} else if (x >= LDBL_EPSILON * fabsl(y - 1)) {
			Am1 = f(x, 1 + y, R) + f(x, 1 - y, S);
			*rx = log1pl(Am1 + sqrtl(Am1 * (A + 1)));
		} else if (y < 1) {
			*rx = x / sqrtl((1 - y) * (1 + y));
		} else {
			*rx = log1pl((y - 1) + sqrtl((y - 1) * (y + 1)));
		}
	} else {
		*rx = logl(A + sqrtl(A * A - 1));
	}

	*new_y = y;

	if (y < FOUR_SQRT_MIN) {
		*B_is_usable = 0;
		*sqrt_A2my2 = A * (2 / LDBL_EPSILON);
		*new_y = y * (2 / LDBL_EPSILON);
		return;
	}

	*B = y / A;
	*B_is_usable = 1;

	if (*B > B_crossover) {
		*B_is_usable = 0;
		if (y == 1 && x < LDBL_EPSILON / 128) {
			*sqrt_A2my2 = sqrtl(x) * sqrtl((A + y) / 2);
		} else if (x >= LDBL_EPSILON * fabsl(y - 1)) {
			Amy = f(x, y + 1, R) + f(x, y - 1, S);
			*sqrt_A2my2 = sqrtl(Amy * (A + y));
		} else if (y > 1) {
			*sqrt_A2my2 = x * (4 / LDBL_EPSILON / LDBL_EPSILON) * y /
			    sqrtl((y + 1) * (y - 1));
			*new_y = y * (4 / LDBL_EPSILON / LDBL_EPSILON);
		} else {
			*sqrt_A2my2 = sqrtl((1 - y) * (1 + y));
		}
	}
}

long double complex
casinhl(long double complex z)
{
	long double x, y, ax, ay, rx, ry, B, sqrt_A2my2, new_y;
	int B_is_usable;
	long double complex w;

	x = creall(z);
	y = cimagl(z);
	ax = fabsl(x);
	ay = fabsl(y);

	if (isnan(x) || isnan(y)) {
		if (isinf(x))
			return (CMPLXL(x, y + y));
		if (isinf(y))
			return (CMPLXL(y, x + x));
		if (y == 0)
			return (CMPLXL(x + x, y));
		return (CMPLXL(nan_mix(x, y), nan_mix(x, y)));
	}

	if (ax > RECIP_EPSILON || ay > RECIP_EPSILON) {
		if (signbit(x) == 0)
			w = clog_for_large_values(z) + m_ln2;
		else
			w = clog_for_large_values(-z) + m_ln2;
		return (CMPLXL(copysignl(creall(w), x),
		    copysignl(cimagl(w), y)));
	}

	if (x == 0 && y == 0)
		return (z);

	raise_inexact();

	if (ax < SQRT_6_EPSILON / 4 && ay < SQRT_6_EPSILON / 4)
		return (z);

	do_hard_work(ax, ay, &rx, &B_is_usable, &B, &sqrt_A2my2, &new_y);
	if (B_is_usable)
		ry = asinl(B);
	else
		ry = atan2l(new_y, sqrt_A2my2);
	return (CMPLXL(copysignl(rx, x), copysignl(ry, y)));
}

long double complex
casinl(long double complex z)
{
	long double complex w;

	w = casinhl(CMPLXL(cimagl(z), creall(z)));
	return (CMPLXL(cimagl(w), creall(w)));
}

long double complex
cacosl(long double complex z)
{
	long double x, y, ax, ay, rx, ry, B, sqrt_A2mx2, new_x;
	int sx, sy;
	int B_is_usable;
	long double complex w;

	x = creall(z);
	y = cimagl(z);
	sx = signbit(x);
	sy = signbit(y);
	ax = fabsl(x);
	ay = fabsl(y);

	if (isnan(x) || isnan(y)) {
		if (isinf(x))
			return (CMPLXL(y + y, -INFINITY));
		if (isinf(y))
			return (CMPLXL(x + x, -y));
		if (x == 0)
			return (CMPLXL(pio2_hi + pio2_lo, y + y));
		return (CMPLXL(nan_mix(x, y), nan_mix(x, y)));
	}

	if (ax > RECIP_EPSILON || ay > RECIP_EPSILON) {
		w = clog_for_large_values(z);
		rx = fabsl(cimagl(w));
		ry = creall(w) + m_ln2;
		if (sy == 0)
			ry = -ry;
		return (CMPLXL(rx, ry));
	}

	if (x == 1 && y == 0)
		return (CMPLXL(0, -y));

	raise_inexact();

	if (ax < SQRT_6_EPSILON / 4 && ay < SQRT_6_EPSILON / 4)
		return (CMPLXL(pio2_hi - (x - pio2_lo), -y));

	do_hard_work(ay, ax, &ry, &B_is_usable, &B, &sqrt_A2mx2, &new_x);
	if (B_is_usable) {
		if (sx == 0)
			rx = acosl(B);
		else
			rx = acosl(-B);
	} else {
		if (sx == 0)
			rx = atan2l(sqrt_A2mx2, new_x);
		else
			rx = atan2l(sqrt_A2mx2, -new_x);
	}
	if (sy == 0)
		ry = -ry;
	return (CMPLXL(rx, ry));
}

long double complex
cacoshl(long double complex z)
{
	long double complex w;
	long double rx, ry;

	w = cacosl(z);
	rx = creall(w);
	ry = cimagl(w);
	if (isnan(rx) && isnan(ry))
		return (CMPLXL(ry, rx));
	if (isnan(rx))
		return (CMPLXL(fabsl(ry), rx));
	if (isnan(ry))
		return (CMPLXL(ry, ry));
	return (CMPLXL(fabsl(ry), copysignl(rx, cimagl(z))));
}

static long double complex
clog_for_large_values(long double complex z)
{
	long double x, y;
	long double ax, ay, t;

	x = creall(z);
	y = cimagl(z);
	ax = fabsl(x);
	ay = fabsl(y);
	if (ax < ay) {
		t = ax;
		ax = ay;
		ay = t;
	}

	if (ax > HALF_MAX)
		return (CMPLXL(logl(hypotl(x / m_e, y / m_e)) + 1,
		    atan2l(y, x)));

	if (ax > QUARTER_SQRT_MAX || ay < SQRT_MIN)
		return (CMPLXL(logl(hypotl(x, y)), atan2l(y, x)));

	return (CMPLXL(logl(ax * ax + ay * ay) / 2, atan2l(y, x)));
}

static inline long double
sum_squares(long double x, long double y)
{

	if (y < SQRT_MIN)
		return (x * x);

	return (x * x + y * y);
}

static inline long double
real_part_reciprocal(long double x, long double y)
{
	long double scale;
	uint16_t hx, hy;
	int16_t ix, iy;

	GET_LDBL_EXPSIGN(hx, x);
	ix = hx & 0x7fff;
	GET_LDBL_EXPSIGN(hy, y);
	iy = hy & 0x7fff;
#define	BIAS	(LDBL_MAX_EXP - 1)
#define	CUTOFF	(LDBL_MANT_DIG / 2 + 1)
	if (ix - iy >= CUTOFF || isinf(x))
		return (1 / x);
	if (iy - ix >= CUTOFF)
		return (x / y / y);
	if (ix <= BIAS + LDBL_MAX_EXP / 2 - CUTOFF)
		return (x / (x * x + y * y));
	scale = 1;
	SET_LDBL_EXPSIGN(scale, 0x7fff - ix);
	x *= scale;
	y *= scale;
	return (x / (x * x + y * y) * scale);
}

long double complex
catanhl(long double complex z)
{
	long double x, y, ax, ay, rx, ry;

	x = creall(z);
	y = cimagl(z);
	ax = fabsl(x);
	ay = fabsl(y);

	if (y == 0 && ax <= 1)
		return (CMPLXL(atanhl(x), y));

	if (x == 0)
		return (CMPLXL(x, atanl(y)));

	if (isnan(x) || isnan(y)) {
		if (isinf(x))
			return (CMPLXL(copysignl(0, x), y + y));
		if (isinf(y))
			return (CMPLXL(copysignl(0, x),
			    copysignl(pio2_hi + pio2_lo, y)));
		return (CMPLXL(nan_mix(x, y), nan_mix(x, y)));
	}

	if (ax > RECIP_EPSILON || ay > RECIP_EPSILON)
		return (CMPLXL(real_part_reciprocal(x, y),
		    copysignl(pio2_hi + pio2_lo, y)));

	if (ax < SQRT_3_EPSILON / 2 && ay < SQRT_3_EPSILON / 2) {
		raise_inexact();
		return (z);
	}

	if (ax == 1 && ay < LDBL_EPSILON)
		rx = (m_ln2 - logl(ay)) / 2;
	else
		rx = log1pl(4 * ax / sum_squares(ax - 1, ay)) / 4;

	if (ax == 1)
		ry = atan2l(2, -ay) / 2;
	else if (ay < LDBL_EPSILON)
		ry = atan2l(2 * ay, (1 - ax) * (1 + ax)) / 2;
	else
		ry = atan2l(2 * ay, (1 - ax) * (1 + ax) - ay * ay) / 2;

	return (CMPLXL(copysignl(rx, x), copysignl(ry, y)));
}

long double complex
catanl(long double complex z)
{
	long double complex w;

	w = catanhl(CMPLXL(cimagl(z), creall(z)));
	return (CMPLXL(cimagl(w), creall(w)));
}
