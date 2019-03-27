/* @(#)s_cbrt.c 5.1 93/09/24 */
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
 * Optimized by Bruce D. Evans.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <float.h>
#include "math.h"
#include "math_private.h"

/* cbrt(x)
 * Return cube root of x
 */
static const u_int32_t
	B1 = 715094163, /* B1 = (1023-1023/3-0.03306235651)*2**20 */
	B2 = 696219795; /* B2 = (1023-1023/3-54/3-0.03306235651)*2**20 */

/* |1/cbrt(x) - p(x)| < 2**-23.5 (~[-7.93e-8, 7.929e-8]). */
static const double
P0 =  1.87595182427177009643,		/* 0x3ffe03e6, 0x0f61e692 */
P1 = -1.88497979543377169875,		/* 0xbffe28e0, 0x92f02420 */
P2 =  1.621429720105354466140,		/* 0x3ff9f160, 0x4a49d6c2 */
P3 = -0.758397934778766047437,		/* 0xbfe844cb, 0xbee751d9 */
P4 =  0.145996192886612446982;		/* 0x3fc2b000, 0xd4e4edd7 */

double
cbrt(double x)
{
	int32_t	hx;
	union {
	    double value;
	    uint64_t bits;
	} u;
	double r,s,t=0.0,w;
	u_int32_t sign;
	u_int32_t high,low;

	EXTRACT_WORDS(hx,low,x);
	sign=hx&0x80000000; 		/* sign= sign(x) */
	hx  ^=sign;
	if(hx>=0x7ff00000) return(x+x); /* cbrt(NaN,INF) is itself */

    /*
     * Rough cbrt to 5 bits:
     *    cbrt(2**e*(1+m) ~= 2**(e/3)*(1+(e%3+m)/3)
     * where e is integral and >= 0, m is real and in [0, 1), and "/" and
     * "%" are integer division and modulus with rounding towards minus
     * infinity.  The RHS is always >= the LHS and has a maximum relative
     * error of about 1 in 16.  Adding a bias of -0.03306235651 to the
     * (e%3+m)/3 term reduces the error to about 1 in 32. With the IEEE
     * floating point representation, for finite positive normal values,
     * ordinary integer division of the value in bits magically gives
     * almost exactly the RHS of the above provided we first subtract the
     * exponent bias (1023 for doubles) and later add it back.  We do the
     * subtraction virtually to keep e >= 0 so that ordinary integer
     * division rounds towards minus infinity; this is also efficient.
     */
	if(hx<0x00100000) { 		/* zero or subnormal? */
	    if((hx|low)==0)
		return(x);		/* cbrt(0) is itself */
	    SET_HIGH_WORD(t,0x43500000); /* set t= 2**54 */
	    t*=x;
	    GET_HIGH_WORD(high,t);
	    INSERT_WORDS(t,sign|((high&0x7fffffff)/3+B2),0);
	} else
	    INSERT_WORDS(t,sign|(hx/3+B1),0);

    /*
     * New cbrt to 23 bits:
     *    cbrt(x) = t*cbrt(x/t**3) ~= t*P(t**3/x)
     * where P(r) is a polynomial of degree 4 that approximates 1/cbrt(r)
     * to within 2**-23.5 when |r - 1| < 1/10.  The rough approximation
     * has produced t such than |t/cbrt(x) - 1| ~< 1/32, and cubing this
     * gives us bounds for r = t**3/x.
     *
     * Try to optimize for parallel evaluation as in k_tanf.c.
     */
	r=(t*t)*(t/x);
	t=t*((P0+r*(P1+r*P2))+((r*r)*r)*(P3+r*P4));

    /*
     * Round t away from zero to 23 bits (sloppily except for ensuring that
     * the result is larger in magnitude than cbrt(x) but not much more than
     * 2 23-bit ulps larger).  With rounding towards zero, the error bound
     * would be ~5/6 instead of ~4/6.  With a maximum error of 2 23-bit ulps
     * in the rounded t, the infinite-precision error in the Newton
     * approximation barely affects third digit in the final error
     * 0.667; the error in the rounded t can be up to about 3 23-bit ulps
     * before the final error is larger than 0.667 ulps.
     */
	u.value=t;
	u.bits=(u.bits+0x80000000)&0xffffffffc0000000ULL;
	t=u.value;

    /* one step Newton iteration to 53 bits with error < 0.667 ulps */
	s=t*t;				/* t*t is exact */
	r=x/s;				/* error <= 0.5 ulps; |r| < |t| */
	w=t+t;				/* t+t is exact */
	r=(r-t)/(w+r);			/* r-t is exact; w+r ~= 3*t */
	t=t+t*r;			/* error <= 0.5 + 0.5/3 + epsilon */

	return(t);
}

#if (LDBL_MANT_DIG == 53)
__weak_reference(cbrt, cbrtl);
#endif
