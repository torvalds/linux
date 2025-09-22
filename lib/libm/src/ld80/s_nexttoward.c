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
	int32_t hx,ix,iy;
	u_int32_t lx,hy,ly,esy;

	EXTRACT_WORDS(hx,lx,x);
	GET_LDOUBLE_WORDS(esy,hy,ly,y);
	ix = hx&0x7fffffff;		/* |x| */
	iy = esy&0x7fff;		/* |y| */

	if(((ix>=0x7ff00000)&&((ix-0x7ff00000)|lx)!=0) ||   /* x is nan */
	   ((iy>=0x7fff)&&(hy|ly)!=0))		/* y is nan */
	   return x+y;
	if((long double) x==y) return y;	/* x=y, return y */
	if((ix|lx)==0) {			/* x == 0 */
	    volatile double u;
	    INSERT_WORDS(x,(esy&0x8000)<<16,1); /* return +-minsub */
	    u = x;
	    u = u * u;				/* raise underflow flag */
	    return x;
	}
	if(hx>=0) {				/* x > 0 */
	    if (esy>=0x8000||((ix>>20)&0x7ff)>iy-0x3c00
		|| (((ix>>20)&0x7ff)==iy-0x3c00
		    && (((hx<<11)|(lx>>21))>(hy&0x7fffffff)
			|| (((hx<<11)|(lx>>21))==(hy&0x7fffffff)
			    && (lx<<11)>ly)))) {	/* x > y, x -= ulp */
		if(lx==0) hx -= 1;
		lx -= 1;
	    } else {				/* x < y, x += ulp */
		lx += 1;
		if(lx==0) hx += 1;
	    }
	} else {				/* x < 0 */
	    if (esy<0x8000||((ix>>20)&0x7ff)>iy-0x3c00
		|| (((ix>>20)&0x7ff)==iy-0x3c00
		    && (((hx<<11)|(lx>>21))>(hy&0x7fffffff)
			|| (((hx<<11)|(lx>>21))==(hy&0x7fffffff)
			    && (lx<<11)>ly))))	{/* x < y, x -= ulp */
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
	    if(u==x) {
		INSERT_WORDS(x,hx,lx);
		return x;
	    }
	}
	INSERT_WORDS(x,hx,lx);
	return x;
}
