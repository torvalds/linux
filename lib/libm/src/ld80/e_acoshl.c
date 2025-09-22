/* @(#)e_acosh.c 5.1 93/09/24 */
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

/* acoshl(x)
 * Method :
 *	Based on
 *		acoshl(x) = logl [ x + sqrtl(x*x-1) ]
 *	we have
 *		acoshl(x) := logl(x)+ln2,	if x is large; else
 *		acoshl(x) := logl(2x-1/(sqrtl(x*x-1)+x)) if x>2; else
 *		acoshl(x) := log1pl(t+sqrtl(2.0*t+t*t)); where t=x-1.
 *
 * Special cases:
 *	acoshl(x) is NaN with signal if x<1.
 *	acoshl(NaN) is NaN without signal.
 */

#include <math.h>

#include "math_private.h"

static const long double
one	= 1.0,
ln2	= 6.931471805599453094287e-01L; /* 0x3FFE, 0xB17217F7, 0xD1CF79AC */

long double
acoshl(long double x)
{
	long double t;
	u_int32_t se,i0,i1;
	GET_LDOUBLE_WORDS(se,i0,i1,x);
	if(se<0x3fff || se & 0x8000) {	/* x < 1 */
	    return (x-x)/(x-x);
	} else if(se >=0x401d) {	/* x > 2**30 */
	    if(se >=0x7fff) {		/* x is inf of NaN */
		return x+x;
	    } else
		return logl(x)+ln2;	/* acoshl(huge)=logl(2x) */
	} else if(((se-0x3fff)|i0|i1)==0) {
	    return 0.0;			/* acosh(1) = 0 */
	} else if (se > 0x4000) {	/* 2**28 > x > 2 */
	    t=x*x;
	    return logl(2.0*x-one/(x+sqrtl(t-one)));
	} else {			/* 1<x<2 */
	    t = x-one;
	    return log1pl(t+sqrtl(2.0*t+t*t));
	}
}
