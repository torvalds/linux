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
 */

#include <float.h>
#include <math.h>

#include "math_private.h"

/* cbrt(x)
 * Return cube root of x
 */
static const u_int32_t
	B1 = 715094163, /* B1 = (682-0.03306235651)*2**20 */
	B2 = 696219795; /* B2 = (664-0.03306235651)*2**20 */

static const double
C =  5.42857142857142815906e-01, /* 19/35     = 0x3FE15F15, 0xF15F15F1 */
D = -7.05306122448979611050e-01, /* -864/1225 = 0xBFE691DE, 0x2532C834 */
E =  1.41428571428571436819e+00, /* 99/70     = 0x3FF6A0EA, 0x0EA0EA0F */
F =  1.60714285714285720630e+00, /* 45/28     = 0x3FF9B6DB, 0x6DB6DB6E */
G =  3.57142857142857150787e-01; /* 5/14      = 0x3FD6DB6D, 0xB6DB6DB7 */

double
cbrt(double x) 
{
	int32_t	hx;
	double r,s,t=0.0,w;
	u_int32_t sign;
	u_int32_t high,low;

	GET_HIGH_WORD(hx,x);
	sign=hx&0x80000000; 		/* sign= sign(x) */
	hx  ^=sign;
	if(hx>=0x7ff00000) return(x+x); /* cbrt(NaN,INF) is itself */
	GET_LOW_WORD(low,x);
	if((hx|low)==0) 
	    return(x);		/* cbrt(0) is itself */

	SET_HIGH_WORD(x,hx);	/* x <- |x| */
    /* rough cbrt to 5 bits */
	if(hx<0x00100000) 		/* subnormal number */
	  {SET_HIGH_WORD(t,0x43500000);	/* set t= 2**54 */
	   t*=x; GET_HIGH_WORD(high,t); SET_HIGH_WORD(t,high/3+B2);
	  }
	else
	  SET_HIGH_WORD(t,hx/3+B1);


    /* new cbrt to 23 bits, may be implemented in single precision */
	r=t*t/x;
	s=C+r*t;
	t*=G+F/(s+E+D/s);	

    /* chopped to 20 bits and make it larger than cbrt(x) */ 
	GET_HIGH_WORD(high,t);
	INSERT_WORDS(t,high+0x00000001,0);


    /* one step newton iteration to 53 bits with error less than 0.667 ulps */
	s=t*t;		/* t*t is exact */
	r=x/s;
	w=t+t;
	r=(r-t)/(w+r);	/* r-s is exact */
	t=t+t*r;

    /* restore the sign bit */
	GET_HIGH_WORD(high,t);
	SET_HIGH_WORD(t,high|sign);
	return(t);
}
DEF_STD(cbrt);
LDBL_MAYBE_UNUSED_CLONE(cbrt);
