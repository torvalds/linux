/* @(#)e_atanh.c 5.1 93/09/24 */
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

/* atanhl(x)
 * Method :
 *    1.Reduced x to positive by atanh(-x) = -atanh(x)
 *    2.For x>=0.5
 *                   1              2x                          x
 *	atanhl(x) = --- * log(1 + -------) = 0.5 * log1p(2 * --------)
 *                   2             1 - x                      1 - x
 *
 *	For x<0.5
 *	atanhl(x) = 0.5*log1pl(2x+2x*x/(1-x))
 *
 * Special cases:
 *	atanhl(x) is NaN if |x| > 1 with signal;
 *	atanhl(NaN) is that NaN with no signal;
 *	atanhl(+-1) is +-INF with signal.
 *
 */

#include <math.h>

#include "math_private.h"

static const long double one = 1.0, huge = 1e4900L;

static const long double zero = 0.0;

long double
atanhl(long double x)
{
	long double t;
	int32_t ix;
	u_int32_t se,i0,i1;
	GET_LDOUBLE_WORDS(se,i0,i1,x);
	ix = se&0x7fff;
	if ((ix+((((i0&0x7fffffff)|i1)|(-((i0&0x7fffffff)|i1)))>>31))>0x3fff)
	  /* |x|>1 */
	    return (x-x)/(x-x);
	if(ix==0x3fff)
	    return x/zero;
	if(ix<0x3fe3&&(huge+x)>zero) return x;	/* x<2**-28 */
	SET_LDOUBLE_EXP(x,ix);
	if(ix<0x3ffe) {		/* x < 0.5 */
	    t = x+x;
	    t = 0.5*log1pl(t+t*x/(one-x));
	} else
	    t = 0.5*log1pl((x+x)/(one-x));
	if(se<=0x7fff) return t; else return -t;
}
