/*
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 *
 * From: @(#)s_floor.c 5.1 93/09/24
 */

/*
 * truncl(x)
 * Return x rounded toward 0 to integral value
 * Method:
 *	Bit twiddling.
 * Exception:
 *	Inexact flag raised if x not equal to truncl(x).
 */

#include <sys/types.h>
#include <machine/ieee.h>

#include <float.h>
#include <math.h>
#include <stdint.h>

#include "math_private.h"

#ifdef LDBL_IMPLICIT_NBIT
#define	MANH_SIZE	(EXT_FRACHBITS + EXT_FRACHMBITS + 1)
#else
#define	MANH_SIZE	(EXT_FRACHBITS + EXT_FRACHMBITS)
#endif

static const long double huge = 1.0e300;
static const float zero[] = { 0.0, -0.0 };

long double
truncl(long double x)
{
	int e;
	int64_t ix0, ix1;

	GET_LDOUBLE_WORDS64(ix0,ix1,x);
	e = ((ix0>>48)&0x7fff) - LDBL_MAX_EXP + 1;

	if (e < MANH_SIZE - 1) {
		if (e < 0) {			/* raise inexact if x != 0 */
			if (huge + x > 0.0)
				return (zero[((ix0>>48)&0x8000)!=0]);
		} else {
			uint64_t m = ((1llu << MANH_SIZE) - 1) >> (e + 1);
			if (((ix0 & m) | ix1) == 0)
				return (x);	/* x is integral */
			if (huge + x > 0.0) {	/* raise inexact flag */
				ix0 &= ~m;
				ix1 = 0;
			}
		}
	} else if (e < LDBL_MANT_DIG - 1) {
		uint64_t m = (uint64_t)-1 >> (64 - LDBL_MANT_DIG + e + 1);
		if ((ix1 & m) == 0)
			return (x);	/* x is integral */
		if (huge + x > 0.0)		/* raise inexact flag */
			ix1 &= ~m;
	}
	SET_LDOUBLE_WORDS64(x,ix0,ix1);
	return (x);
}
