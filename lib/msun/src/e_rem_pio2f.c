/* e_rem_pio2f.c -- float version of e_rem_pio2.c
 * Conversion to float by Ian Lance Taylor, Cygnus Support, ian@cygnus.com.
 * Debugged and optimized by Bruce D. Evans.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/* __ieee754_rem_pio2f(x,y)
 *
 * return the remainder of x rem pi/2 in *y
 * use double precision for everything except passing x
 * use __kernel_rem_pio2() for large x
 */

#include <float.h>

#include "math.h"
#include "math_private.h"

/*
 * invpio2:  53 bits of 2/pi
 * pio2_1:   first 25 bits of pi/2
 * pio2_1t:  pi/2 - pio2_1
 */

static const double
invpio2 =  6.36619772367581382433e-01, /* 0x3FE45F30, 0x6DC9C883 */
pio2_1  =  1.57079631090164184570e+00, /* 0x3FF921FB, 0x50000000 */
pio2_1t =  1.58932547735281966916e-08; /* 0x3E5110b4, 0x611A6263 */

#ifdef INLINE_REM_PIO2F
static __inline __always_inline
#endif
int
__ieee754_rem_pio2f(float x, double *y)
{
	double w,r,fn;
	double tx[1],ty[1];
	float z;
	int32_t e0,n,ix,hx;

	GET_FLOAT_WORD(hx,x);
	ix = hx&0x7fffffff;
    /* 33+53 bit pi is good enough for medium size */
	if(ix<0x4dc90fdb) {		/* |x| ~< 2^28*(pi/2), medium size */
	    fn = rnint((float_t)x*invpio2);
	    n  = irint(fn);
	    r  = x-fn*pio2_1;
	    w  = fn*pio2_1t;
	    *y = r-w;
	    return n;
	}
    /*
     * all other (large) arguments
     */
	if(ix>=0x7f800000) {		/* x is inf or NaN */
	    *y=x-x; return 0;
	}
    /* set z = scalbn(|x|,ilogb(|x|)-23) */
	e0 = (ix>>23)-150;		/* e0 = ilogb(|x|)-23; */
	SET_FLOAT_WORD(z, ix - ((int32_t)(e0<<23)));
	tx[0] = z;
	n  =  __kernel_rem_pio2(tx,ty,e0,1,0);
	if(hx<0) {*y = -ty[0]; return -n;}
	*y = ty[0]; return n;
}
