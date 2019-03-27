/*-
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 */

/* s_sincosf.c -- float version of s_sincos.c.
 * Conversion to float by Ian Lance Taylor, Cygnus Support, ian@cygnus.com.
 * Optimized by Bruce D. Evans.
 * Merged s_sinf.c and s_cosf.c by Steven G. Kargl.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <float.h>

#include "math.h"
#define INLINE_REM_PIO2F
#include "math_private.h"
#include "e_rem_pio2f.c"
#include "k_sincosf.h"

/* Small multiples of pi/2 rounded to double precision. */
static const double
p1pio2 = 1*M_PI_2,			/* 0x3FF921FB, 0x54442D18 */
p2pio2 = 2*M_PI_2,			/* 0x400921FB, 0x54442D18 */
p3pio2 = 3*M_PI_2,			/* 0x4012D97C, 0x7F3321D2 */
p4pio2 = 4*M_PI_2;			/* 0x401921FB, 0x54442D18 */

void
sincosf(float x, float *sn, float *cs)
{
	float c, s;
	double y;
	int32_t n, hx, ix;

	GET_FLOAT_WORD(hx, x);
	ix = hx & 0x7fffffff;

	if (ix <= 0x3f490fda) {		/* |x| ~<= pi/4 */
		if (ix < 0x39800000) {	/* |x| < 2**-12 */
			if ((int)x == 0) {
				*sn = x;	/* x with inexact if x != 0 */
				*cs = 1;
				return;
			}
		}
		__kernel_sincosdf(x, sn, cs);
		return;
	}

	if (ix <= 0x407b53d1) {		/* |x| ~<= 5*pi/4 */
		if (ix <= 0x4016cbe3) {	/* |x| ~<= 3pi/4 */
			if (hx > 0) {
				__kernel_sincosdf(x - p1pio2, cs, sn);
				*cs = -*cs;
			} else {
				__kernel_sincosdf(x + p1pio2, cs, sn);
				*sn = -*sn;
			}
		} else {
			if (hx > 0)
				__kernel_sincosdf(x - p2pio2, sn, cs);
			else
				__kernel_sincosdf(x + p2pio2, sn, cs);
			*sn = -*sn;
			*cs = -*cs;
		}
		return;
	}

	if (ix <= 0x40e231d5) {		/* |x| ~<= 9*pi/4 */
		if (ix <= 0x40afeddf) {	/* |x| ~<= 7*pi/4 */
			if (hx > 0) {
				__kernel_sincosdf(x - p3pio2, cs, sn);
				*sn = -*sn;
			} else {
				__kernel_sincosdf(x + p3pio2, cs, sn);
				*cs = -*cs;
			}
		} else {
			if (hx > 0)
				__kernel_sincosdf(x - p4pio2, sn, cs);
			else
				__kernel_sincosdf(x + p4pio2, sn, cs);
		}
		return;
	}

	/* If x = Inf or NaN, then sin(x) = NaN and cos(x) = NaN. */
	if (ix >= 0x7f800000) {
		*sn = x - x;
		*cs = x - x;
		return;
	}

	/* Argument reduction. */
	n = __ieee754_rem_pio2f(x, &y);
	__kernel_sincosdf(y, &s, &c);

	switch(n & 3) {
	case 0:
		*sn = s;
		*cs = c;
		break;
	case 1:
		*sn = c;
		*cs = -s;
		break;
	case 2:
		*sn = -s;
		*cs = -c;
		break;
	default:
		*sn = -c;
		*cs = s;
	}
}


