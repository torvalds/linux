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
 *	has error less than sqrtl(2)/2 ulp, than
 *	sqrtl(z) has error less than 1 ulp (exercise).
 *
 *	So, compute sqrtl(x*x+y*y) with some care as
 *	follows to get the error below 1 ulp:
 *
 *	Assume x>y>0;
 *	(if possible, set rounding to round-to-nearest)
 *	1. if x > 2y  use
 *		x1*x1+(y*y+(x2*(x+x1))) for x*x+y*y
 *	where x1 = x with lower 64 bits cleared, x2 = x-x1; else
 *	2. if x <= 2y use
 *		t1*yy1+((x-y)*(x-y)+(t1*y2+t2*y))
 *	where t1 = 2x with lower 64 bits cleared, t2 = 2x-t1,
 *	yy1= y with lower 64 bits chopped, y2 = y-yy1.
 *
 *	NOTE: scaling may be necessary if some argument is too
 *	      large or too tiny
 *
 * Special cases:
 *	hypotl(x,y) is INF if x or y is +INF or -INF; else
 *	hypotl(x,y) is NAN if x or y is NAN.
 *
 * Accuracy:
 * 	hypotl(x,y) returns sqrtl(x^2+y^2) with error less
 * 	than 1 ulps (units in the last place)
 */

#include <math.h>

#include "math_private.h"

long double
hypotl(long double x, long double y)
{
	long double a,b,t1,t2,yy1,y2,w;
	int64_t j,k,ha,hb;

	GET_LDOUBLE_MSW64(ha,x);
	ha &= 0x7fffffffffffffffLL;
	GET_LDOUBLE_MSW64(hb,y);
	hb &= 0x7fffffffffffffffLL;
	if(hb > ha) {a=y;b=x;j=ha; ha=hb;hb=j;} else {a=x;b=y;}
	SET_LDOUBLE_MSW64(a,ha);	/* a <- |a| */
	SET_LDOUBLE_MSW64(b,hb);	/* b <- |b| */
	if((ha-hb)>0x78000000000000LL) {return a+b;} /* x/y > 2**120 */
	k=0;
	if(ha > 0x5f3f000000000000LL) {	/* a>2**8000 */
	   if(ha >= 0x7fff000000000000LL) {	/* Inf or NaN */
	       u_int64_t low;
	       w = a+b;			/* for sNaN */
	       GET_LDOUBLE_LSW64(low,a);
	       if(((ha&0xffffffffffffLL)|low)==0) w = a;
	       GET_LDOUBLE_LSW64(low,b);
	       if(((hb^0x7fff000000000000LL)|low)==0) w = b;
	       return w;
	   }
	   /* scale a and b by 2**-9600 */
	   ha -= 0x2580000000000000LL;
	   hb -= 0x2580000000000000LL;	k += 9600;
	   SET_LDOUBLE_MSW64(a,ha);
	   SET_LDOUBLE_MSW64(b,hb);
	}
	if(hb < 0x20bf000000000000LL) {	/* b < 2**-8000 */
	    if(hb <= 0x0000ffffffffffffLL) {	/* subnormal b or 0 */
		u_int64_t low;
		GET_LDOUBLE_LSW64(low,b);
		if((hb|low)==0) return a;
		t1=0;
		SET_LDOUBLE_MSW64(t1,0x7ffd000000000000LL); /* t1=2^16382 */
		b *= t1;
		a *= t1;
		k -= 16382;
	    } else {		/* scale a and b by 2^9600 */
		ha += 0x2580000000000000LL;	/* a *= 2^9600 */
		hb += 0x2580000000000000LL;	/* b *= 2^9600 */
		k -= 9600;
		SET_LDOUBLE_MSW64(a,ha);
		SET_LDOUBLE_MSW64(b,hb);
	    }
	}
    /* medium size a and b */
	w = a-b;
	if (w>b) {
	    t1 = 0;
	    SET_LDOUBLE_MSW64(t1,ha);
	    t2 = a-t1;
	    w  = sqrtl(t1*t1-(b*(-b)-t2*(a+t1)));
	} else {
	    a  = a+a;
	    yy1 = 0;
	    SET_LDOUBLE_MSW64(yy1,hb);
	    y2 = b - yy1;
	    t1 = 0;
	    SET_LDOUBLE_MSW64(t1,ha+0x0001000000000000LL);
	    t2 = a - t1;
	    w  = sqrtl(t1*yy1-(w*(-w)-(t1*y2+t2*b)));
	}
	if(k!=0) {
	    u_int64_t high;
	    t1 = 1.0L;
	    GET_LDOUBLE_MSW64(high,t1);
	    SET_LDOUBLE_MSW64(t1,high+(k<<48));
	    return t1*w;
	} else return w;
}
DEF_STD(hypotl);
