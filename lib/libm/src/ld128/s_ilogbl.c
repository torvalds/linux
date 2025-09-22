/* s_ilogbl.c -- long double version of s_ilogb.c.
 * Conversion to 128-bit long double by Mark Kettenis, kettenis@openbsd.org.
 */

/*
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice 
 * is preserved.
 * ====================================================
 */

/* ilogbl(long double x)
 * return the binary exponent of non-zero x
 * ilogb(0) = FP_ILOGB0
 * ilogb(NaN) = FP_ILOGBNAN (no signal is raised)
 * ilogb(inf) = INT_MAX (no signal is raised)
 */

#include <float.h>
#include <math.h>

#include "math_private.h"

int
ilogbl(long double x)
{
	int64_t hx,lx;
	int32_t ix;

	GET_LDOUBLE_WORDS64(hx,lx,x);
	hx &= 0x7fffffffffffffffLL;
	if(hx<0x0001000000000000LL) {
	    if((hx|lx)==0) 
		return FP_ILOGB0;	/* ilogb(0) = FP_ILOGB0 */
	    else			/* subnormal x */
		if(hx==0) {
		    for (ix = -16431; lx>0; lx<<=1) ix -=1;
		} else {
		    for (ix = -16382, hx<<=15; hx>0; hx<<=1) ix -=1;
		}
	    return ix;
	}
	else if (hx<0x7fff000000000000LL) return (hx>>48)-16383;
	else if (hx>0x7fff000000000000LL || lx!=0) return FP_ILOGBNAN;
	else return INT_MAX;
}
DEF_STD(ilogbl);
