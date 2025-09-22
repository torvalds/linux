/*-
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2009-2011, Bruce D. Evans, Steven G. Kargl, David Schultz.
 *
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 *
 * The argument reduction and testing for exceptional cases was
 * written by Steven G. Kargl with input from Bruce D. Evans
 * and David A. Schultz.
 */

#include <float.h>
#include <ieeefp.h>
#include <math.h>

#include "math_private.h"

#define	BIAS	(LDBL_MAX_EXP - 1)

static const unsigned
    B1 = 709958130;	/* B1 = (127-127.0/3-0.03306235651)*2**23 */

long double
cbrtl(long double x)
{
	long double v, r, s, t, w;
	double dr, dt, dx;
	float ft, fx;
	uint32_t hx, lx;
	uint16_t expsign, es;
	int k;
	volatile double vd1, vd2;

	GET_LDOUBLE_EXP(expsign,x);
	k = expsign & 0x7fff;

	/*
	 * If x = +-Inf, then cbrt(x) = +-Inf.
	 * If x = NaN, then cbrt(x) = NaN.
	 */
	if (k == BIAS + LDBL_MAX_EXP)
		return (x + x);

	if (k == 0) {
		/* If x = +-0, then cbrt(x) = +-0. */
		GET_LDOUBLE_WORDS(es,hx,lx,x);
		if ((hx|lx) == 0) {
			return (x);
		}
		/* Adjust subnormal numbers. */
		x *= 0x1.0p514;
		GET_LDOUBLE_EXP(k,x);
		k &= 0x7fff;
		k -= BIAS + 514;
	} else
		k -= BIAS;
	SET_LDOUBLE_EXP(x,BIAS);
	v = 1;

	switch (k % 3) {
	case 1:
	case -2:
		x = 2*x;
		k--;
		break;
	case 2:
	case -1:
		x = 4*x;
		k -= 2;
		break;
	}
	SET_LDOUBLE_EXP(v, (expsign & 0x8000) | (BIAS + k / 3));

	/*
	 * The following is the guts of s_cbrtf, with the handling of
	 * special values removed and extra care for accuracy not taken,
	 * but with most of the extra accuracy not discarded.
	 */

	/* ~5-bit estimate: */
	fx = x;
	GET_FLOAT_WORD(hx, fx);
	SET_FLOAT_WORD(ft, ((hx & 0x7fffffff) / 3 + B1));

	/* ~16-bit estimate: */
	dx = x;
	dt = ft;
	dr = dt * dt * dt;
	dt = dt * (dx + dx + dr) / (dx + dr + dr);

	/* ~47-bit estimate: */
	dr = dt * dt * dt;
	dt = dt * (dx + dx + dr) / (dx + dr + dr);

	/*
	 * dt is cbrtl(x) to ~47 bits (after x has been reduced to 1 <= x < 8).
	 * Round it away from zero to 32 bits (32 so that t*t is exact, and
	 * away from zero for technical reasons).
	 */
	vd2 = 0x1.0p32;
	vd1 = 0x1.0p-31;
	#define vd ((long double)vd2 + vd1)

	t = dt + vd - 0x1.0p32;

	/*
	 * Final step Newton iteration to 64 or 113 bits with
	 * error < 0.667 ulps
	 */
	s=t*t;				/* t*t is exact */
	r=x/s;				/* error <= 0.5 ulps; |r| < |t| */
	w=t+t;				/* t+t is exact */
	r=(r-t)/(w+r);			/* r-t is exact; w+r ~= 3*t */
	t=t+t*r;			/* error <= 0.5 + 0.5/3 + epsilon */

	t *= v;
	return (t);
}
