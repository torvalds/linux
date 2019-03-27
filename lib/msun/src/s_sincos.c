/*-
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 * 
 * s_sin.c and s_cos.c merged by Steven G. Kargl.  Descriptions of the
 * algorithms are contained in the original files.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <float.h>

#include "math.h"
#define INLINE_REM_PIO2
#include "math_private.h"
#include "e_rem_pio2.c"
#include "k_sincos.h"

void
sincos(double x, double *sn, double *cs)
{
	double y[2];
	int32_t n, ix;

	/* High word of x. */
	GET_HIGH_WORD(ix, x);

	/* |x| ~< pi/4 */
	ix &= 0x7fffffff;
	if (ix <= 0x3fe921fb) {
		if (ix < 0x3e400000) {		/* |x| < 2**-27 */
			if ((int)x == 0) {	/* Generate inexact. */
				*sn = x;
				*cs = 1;
				return;
			}
		}
		__kernel_sincos(x, 0, 0, sn, cs);
		return;
	}

	/* If x = Inf or NaN, then sin(x) = NaN and cos(x) = NaN. */
	if (ix >= 0x7ff00000) {
		*sn = x - x;
		*cs = x - x;
		return;
	}

	/* Argument reduction. */
	n = __ieee754_rem_pio2(x, y);

	switch(n & 3) {
	case 0:
		__kernel_sincos(y[0], y[1], 1, sn, cs);
		break;
	case 1:
		__kernel_sincos(y[0], y[1], 1, cs, sn);
		*cs = -*cs;
		break;
	case 2:
		__kernel_sincos(y[0], y[1], 1, sn, cs);
		*sn = -*sn;
		*cs = -*cs;
		break;
	default:
		__kernel_sincos(y[0], y[1], 1, cs, sn);
		*sn = -*sn;
	}
}

#if (LDBL_MANT_DIG == 53)
__weak_reference(sincos, sincosl);
#endif
