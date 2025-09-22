/* s_ilogbl.c -- long double version of s_ilogb.c.
 * Conversion to 80-bit long double by Mark Kettenis, kettenis@openbsd.org.
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
	int32_t esx,hx,lx,ix;

	GET_LDOUBLE_WORDS(esx,hx,lx,x);
	esx &= 0x7fff;
	if(esx==0) {
	    if((hx|lx)==0) 
		return FP_ILOGB0;	/* ilogb(0) = FP_ILOGB0 */
	    else			/* subnormal x */
		if(hx==0) {
		    for (ix = -16414; lx>0; lx<<=1) ix -=1;
		} else {
		    for (ix = -16382; hx>0; hx<<=1) ix -=1;
		}
	    return ix;
	}
	else if (esx<0x7fff) return (esx)-16383;
	else if ((hx&0x7fffffff|lx)!=0) return FP_ILOGBNAN;
	else return INT_MAX;
}
DEF_STD(ilogbl);
