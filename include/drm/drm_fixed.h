/*
 * Copyright 2009 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 *          Christian König
 */
#ifndef DRM_FIXED_H
#define DRM_FIXED_H

#include <linux/kernel.h>
#include <linux/math64.h>

typedef union dfixed {
	u32 full;
} fixed20_12;


#define dfixed_const(A) (u32)(((A) << 12))/*  + ((B + 0.000122)*4096)) */
#define dfixed_const_half(A) (u32)(((A) << 12) + 2048)
#define dfixed_const_666(A) (u32)(((A) << 12) + 2731)
#define dfixed_const_8(A) (u32)(((A) << 12) + 3277)
#define dfixed_mul(A, B) ((u64)((u64)(A).full * (B).full + 2048) >> 12)
#define dfixed_init(A) { .full = dfixed_const((A)) }
#define dfixed_init_half(A) { .full = dfixed_const_half((A)) }
#define dfixed_trunc(A) ((A).full >> 12)
#define dfixed_frac(A) ((A).full & ((1 << 12) - 1))

static inline u32 dfixed_floor(fixed20_12 A)
{
	u32 non_frac = dfixed_trunc(A);

	return dfixed_const(non_frac);
}

static inline u32 dfixed_ceil(fixed20_12 A)
{
	u32 non_frac = dfixed_trunc(A);

	if (A.full > dfixed_const(non_frac))
		return dfixed_const(non_frac + 1);
	else
		return dfixed_const(non_frac);
}

static inline u32 dfixed_div(fixed20_12 A, fixed20_12 B)
{
	u64 tmp = ((u64)A.full << 13);

	do_div(tmp, B.full);
	tmp += 1;
	tmp /= 2;
	return lower_32_bits(tmp);
}

#define DRM_FIXED_POINT		32
#define DRM_FIXED_ONE		(1ULL << DRM_FIXED_POINT)
#define DRM_FIXED_DECIMAL_MASK	(DRM_FIXED_ONE - 1)
#define DRM_FIXED_DIGITS_MASK	(~DRM_FIXED_DECIMAL_MASK)
#define DRM_FIXED_EPSILON	1LL
#define DRM_FIXED_ALMOST_ONE	(DRM_FIXED_ONE - DRM_FIXED_EPSILON)

static inline s64 drm_int2fixp(int a)
{
	return ((s64)a) << DRM_FIXED_POINT;
}

static inline int drm_fixp2int(s64 a)
{
	return ((s64)a) >> DRM_FIXED_POINT;
}

static inline int drm_fixp2int_round(s64 a)
{
	return drm_fixp2int(a + DRM_FIXED_ONE / 2);
}

static inline int drm_fixp2int_ceil(s64 a)
{
	if (a >= 0)
		return drm_fixp2int(a + DRM_FIXED_ALMOST_ONE);
	else
		return drm_fixp2int(a - DRM_FIXED_ALMOST_ONE);
}

static inline unsigned drm_fixp_msbset(s64 a)
{
	unsigned shift, sign = (a >> 63) & 1;

	for (shift = 62; shift > 0; --shift)
		if (((a >> shift) & 1) != sign)
			return shift;

	return 0;
}

static inline s64 drm_fixp_mul(s64 a, s64 b)
{
	unsigned shift = drm_fixp_msbset(a) + drm_fixp_msbset(b);
	s64 result;

	if (shift > 61) {
		shift = shift - 61;
		a >>= (shift >> 1) + (shift & 1);
		b >>= shift >> 1;
	} else
		shift = 0;

	result = a * b;

	if (shift > DRM_FIXED_POINT)
		return result << (shift - DRM_FIXED_POINT);

	if (shift < DRM_FIXED_POINT)
		return result >> (DRM_FIXED_POINT - shift);

	return result;
}

static inline s64 drm_fixp_div(s64 a, s64 b)
{
	unsigned shift = 62 - drm_fixp_msbset(a);
	s64 result;

	a <<= shift;

	if (shift < DRM_FIXED_POINT)
		b >>= (DRM_FIXED_POINT - shift);

	result = div64_s64(a, b);

	if (shift > DRM_FIXED_POINT)
		return result >> (shift - DRM_FIXED_POINT);

	return result;
}

static inline s64 drm_fixp_from_fraction(s64 a, s64 b)
{
	s64 res;
	bool a_neg = a < 0;
	bool b_neg = b < 0;
	u64 a_abs = a_neg ? -a : a;
	u64 b_abs = b_neg ? -b : b;
	u64 rem;

	/* determine integer part */
	u64 res_abs  = div64_u64_rem(a_abs, b_abs, &rem);

	/* determine fractional part */
	{
		u32 i = DRM_FIXED_POINT;

		do {
			rem <<= 1;
			res_abs <<= 1;
			if (rem >= b_abs) {
				res_abs |= 1;
				rem -= b_abs;
			}
		} while (--i != 0);
	}

	/* round up LSB */
	{
		u64 summand = (rem << 1) >= b_abs;

		res_abs += summand;
	}

	res = (s64) res_abs;
	if (a_neg ^ b_neg)
		res = -res;
	return res;
}

static inline s64 drm_fixp_exp(s64 x)
{
	s64 tolerance = div64_s64(DRM_FIXED_ONE, 1000000);
	s64 sum = DRM_FIXED_ONE, term, y = x;
	u64 count = 1;

	if (x < 0)
		y = -1 * x;

	term = y;

	while (term >= tolerance) {
		sum = sum + term;
		count = count + 1;
		term = drm_fixp_mul(term, div64_s64(y, count));
	}

	if (x < 0)
		sum = drm_fixp_div(DRM_FIXED_ONE, sum);

	return sum;
}

#endif
