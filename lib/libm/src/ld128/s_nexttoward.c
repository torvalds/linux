/* @(#)s_nextafter.c 5.1 93/09/24 */
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

/* IEEE functions
 *	nexttoward(x,y)
 *	return the next machine floating-point number of x in the
 *	direction toward y.
 *   Special cases:
 */

#include <math.h>
#include <float.h>

#include "math_private.h"

double
nexttoward(double x, long double y)
{
	int32_t hx,ix;
	int64_t hy,iy;
	u_int32_t lx;
	u_int64_t ly;

	EXTRACT_WORDS(hx,lx,x);
	GET_LDOUBLE_WORDS64(hy,ly,y);
	ix = hx&0x7fffffff;		/* |x| */
	iy = hy&0x7fffffffffffffffLL;	/* |y| */

	if(((ix>=0x7ff00000)&&((ix-0x7ff00000)|lx)!=0) ||   /* x is nan */
	   ((iy>=0x7fff000000000000LL)&&((iy-0x7fff000000000000LL)|ly)!=0))
							    /* y is nan */
	   return x+y;
	if((long double) x==y) return y;	/* x=y, return y */
	if((ix|lx)==0) {			/* x == 0 */
	    volatile double u;
	    INSERT_WORDS(x,(u_int32_t)((hy>>32)&0x80000000),1);/* return +-minsub */
	    u = x;
	    u = u * u;				/* raise underflow flag */
	    return x;
	}
	if(hx>=0) {				/* x > 0 */
	    if (hy<0||(ix>>20)>(iy>>48)-0x3c00
		|| ((ix>>20)==(iy>>48)-0x3c00
		    && (((((int64_t)hx)<<28)|(lx>>4))>(hy&0x0000ffffffffffffLL)
			|| (((((int64_t)hx)<<28)|(lx>>4))==(hy&0x0000ffffffffffffLL)
			    && (lx&0xf)>(ly>>60))))) {	/* x > y, x -= ulp */
		if(lx==0) hx -= 1;
		lx -= 1;
	    } else {				/* x < y, x += ulp */
		lx += 1;
		if(lx==0) hx += 1;
	    }
	} else {				/* x < 0 */
	    if (hy>=0||(ix>>20)>(iy>>48)-0x3c00
		|| ((ix>>20)==(iy>>48)-0x3c00
		    && (((((int64_t)hx)<<28)|(lx>>4))>(hy&0x0000ffffffffffffLL)
			|| (((((int64_t)hx)<<28)|(lx>>4))==(hy&0x0000ffffffffffffLL)
			    && (lx&0xf)>(ly>>60))))) {	/* x < y, x -= ulp */
		if(lx==0) hx -= 1;
		lx -= 1;
	    } else {				/* x > y, x += ulp */
		lx += 1;
		if(lx==0) hx += 1;
	    }
	}
	hy = hx&0x7ff00000;
	if(hy>=0x7ff00000) {
	  x = x+x;	/* overflow  */
	  return x;
	}
	if(hy<0x00100000) {
	    volatile double u = x*x;		/* underflow */
	}
	INSERT_WORDS(x,hx,lx);
	return x;
}
