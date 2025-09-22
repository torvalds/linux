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

#include <math.h>
#include <float.h>

#include "math_private.h"

float
nexttowardf(float x, long double y)
{
	int32_t hx,ix,iy;
	u_int32_t hy,ly,esy;

	GET_FLOAT_WORD(hx,x);
	GET_LDOUBLE_WORDS(esy,hy,ly,y);
	ix = hx&0x7fffffff;		/* |x| */
	iy = esy&0x7fff;		/* |y| */

	if((ix>0x7f800000) ||			/* x is nan */
	   (iy>=0x7fff&&((hy|ly)!=0)))		/* y is nan */
	   return x+y;
	if((long double) x==y) return y;	/* x=y, return y */
	if(ix==0) {				/* x == 0 */
	    volatile float u;
	    SET_FLOAT_WORD(x,((esy&0x8000)<<16)|1);/* return +-minsub*/
	    u = x;
	    u = u * u;				/* raise underflow flag */
	    return x;
	}
	if(hx>=0) {				/* x > 0 */
	    if(esy>=0x8000||((ix>>23)&0xff)>iy-0x3f80
	       || (((ix>>23)&0xff)==iy-0x3f80
		   && ((ix&0x7fffff)<<8)>(hy&0x7fffffff))) {/* x > y, x -= ulp */
		hx -= 1;
	    } else {				/* x < y, x += ulp */
		hx += 1;
	    }
	} else {				/* x < 0 */
	    if(esy<0x8000||((ix>>23)&0xff)>iy-0x3f80
	       || (((ix>>23)&0xff)==iy-0x3f80
		   && ((ix&0x7fffff)<<8)>(hy&0x7fffffff))) {/* x < y, x -= ulp */
		hx -= 1;
	    } else {				/* x > y, x += ulp */
		hx += 1;
	    }
	}
	hy = hx&0x7f800000;
	if(hy>=0x7f800000) {
	  x = x+x;	/* overflow  */
	  return x;
	}
	if(hy<0x00800000) {
	    volatile float u = x*x;		/* underflow */
	}
	SET_FLOAT_WORD(x,hx);
	return x;
}
