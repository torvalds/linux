/* @(#)e_hypot.c 5.1 93/09/24 */
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

/* hypotl(x,y)
 *
 * Method :
 *	If (assume round-to-nearest) z=x*x+y*y
 *	has error less than sqrt(2)/2 ulp, than
 *	sqrt(z) has error less than 1 ulp (exercise).
 *
 *	So, compute sqrt(x*x+y*y) with some care as
 *	follows to get the error below 1 ulp:
 *
 *	Assume x>y>0;
 *	(if possible, set rounding to round-to-nearest)
 *	1. if x > 2y  use
 *		x1*x1+(y*y+(x2*(x+x1))) for x*x+y*y
 *	where x1 = x with lower 32 bits cleared, x2 = x-x1; else
 *	2. if x <= 2y use
 *		t1*yy1+((x-y)*(x-y)+(t1*y2+t2*y))
 *	where t1 = 2x with lower 32 bits cleared, t2 = 2x-t1,
 *	yy1= y with lower 32 bits chopped, y2 = y-yy1.
 *
 *	NOTE: scaling may be necessary if some argument is too
 *	      large or too tiny
 *
 * Special cases:
 *	hypot(x,y) is INF if x or y is +INF or -INF; else
 *	hypot(x,y) is NAN if x or y is NAN.
 *
 * Accuracy:
 *	hypot(x,y) returns sqrt(x^2+y^2) with error less
 *	than 1 ulps (units in the last place)
 */

#include <math.h>

#include "math_private.h"

long double
hypotl(long double x, long double y)
{
	long double a,b,t1,t2,yy1,y2,w;
	u_int32_t j,k,ea,eb;

	GET_LDOUBLE_EXP(ea,x);
	ea &= 0x7fff;
	GET_LDOUBLE_EXP(eb,y);
	eb &= 0x7fff;
	if(eb > ea) {a=y;b=x;j=ea; ea=eb;eb=j;} else {a=x;b=y;}
	SET_LDOUBLE_EXP(a,ea);	/* a <- |a| */
	SET_LDOUBLE_EXP(b,eb);	/* b <- |b| */
	if((ea-eb)>0x46) {return a+b;} /* x/y > 2**70 */
	k=0;
	if(ea > 0x5f3f) {	/* a>2**8000 */
	   if(ea == 0x7fff) {	/* Inf or NaN */
	       u_int32_t es,high,low;
	       w = a+b;			/* for sNaN */
	       GET_LDOUBLE_WORDS(es,high,low,a);
	       if(((high&0x7fffffff)|low)==0) w = a;
	       GET_LDOUBLE_WORDS(es,high,low,b);
	       if(((eb^0x7fff)|(high&0x7fffffff)|low)==0) w = b;
	       return w;
	   }
	   /* scale a and b by 2**-9600 */
	   ea -= 0x2580; eb -= 0x2580;	k += 9600;
	   SET_LDOUBLE_EXP(a,ea);
	   SET_LDOUBLE_EXP(b,eb);
	}
	if(eb < 0x20bf) {	/* b < 2**-8000 */
	    if(eb == 0) {	/* subnormal b or 0 */
		u_int32_t es,high,low;
		GET_LDOUBLE_WORDS(es,high,low,b);
		if((high|low)==0) return a;
		SET_LDOUBLE_WORDS(t1, 0x7ffd, 0, 0);	/* t1=2^16382 */
		b *= t1;
		a *= t1;
		k -= 16382;
	    } else {		/* scale a and b by 2^9600 */
		ea += 0x2580;	/* a *= 2^9600 */
		eb += 0x2580;	/* b *= 2^9600 */
		k -= 9600;
		SET_LDOUBLE_EXP(a,ea);
		SET_LDOUBLE_EXP(b,eb);
	    }
	}
    /* medium size a and b */
	w = a-b;
	if (w>b) {
	    u_int32_t high;
	    GET_LDOUBLE_MSW(high,a);
	    SET_LDOUBLE_WORDS(t1,ea,high,0);
	    t2 = a-t1;
	    w  = sqrtl(t1*t1-(b*(-b)-t2*(a+t1)));
	} else {
	    u_int32_t high;
	    GET_LDOUBLE_MSW(high,b);
	    a  = a+a;
	    SET_LDOUBLE_WORDS(yy1,eb,high,0);
	    y2 = b - yy1;
	    GET_LDOUBLE_MSW(high,a);
	    SET_LDOUBLE_WORDS(t1,ea+1,high,0);
	    t2 = a - t1;
	    w  = sqrtl(t1*yy1-(w*(-w)-(t1*y2+t2*b)));
	}
	if(k!=0) {
	    u_int32_t es;
	    t1 = 1.0;
	    GET_LDOUBLE_EXP(es,t1);
	    SET_LDOUBLE_EXP(t1,es+k);
	    return t1*w;
	} else return w;
}
DEF_STD(hypotl);
