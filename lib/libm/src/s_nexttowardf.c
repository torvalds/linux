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
 *	nexttowardf(x,y)
 *	return the next machine floating-point number of x in the
 *	direction toward y.
 * This is for machines which use the same binary type for double and
 * long double.
 *   Special cases:
 */

#include <math.h>
#include <float.h>

#include "math_private.h"

float
nexttowardf(float x, long double y)
{
	int32_t hx,hy,ix,iy;
	u_int32_t ly;

	GET_FLOAT_WORD(hx,x);
	EXTRACT_WORDS(hy,ly,y);
	ix = hx&0x7fffffff;		/* |x| */
	iy = hy&0x7fffffff;		/* |y| */

	if((ix>0x7f800000) ||				   /* x is nan */
	   ((iy>=0x7ff00000)&&((iy-0x7ff00000)|ly)!=0))    /* y is nan */
	   return x+y;
	if((long double) x==y) return y;	/* x=y, return y */
	if(ix==0) {				/* x == 0 */
	    volatile float u;
	    SET_FLOAT_WORD(x,(u_int32_t)(hy&0x80000000)|1);/* return +-minsub*/
	    u = x;
	    u = u * u;				/* raise underflow flag */
	    return x;
	}
	if(hx>=0) {				/* x > 0 */
	    if(hy<0||(ix>>23)>(iy>>20)-0x380
	       || ((ix>>23)==(iy>>20)-0x380
		   && (ix&0x7fffff)>(((hy<<3)|(ly>>29))&0x7fffff)))	/* x > y, x -= ulp */
		hx -= 1;
	    else				/* x < y, x += ulp */
		hx += 1;
	} else {				/* x < 0 */
	    if(hy>=0||(ix>>23)>(iy>>20)-0x380
	       || ((ix>>23)==(iy>>20)-0x380
		   && (ix&0x7fffff)>(((hy<<3)|(ly>>29))&0x7fffff)))	/* x < y, x -= ulp */
		hx -= 1;
	    else				/* x > y, x += ulp */
		hx += 1;
	}
	hy = hx&0x7f800000;
	if(hy>=0x7f800000) {
	  x = x+x;	/* overflow  */
	  return x;
	}
	if(hy<0x00800000) {
	    volatile float u = x*x;		/* underflow */
	    if(u==x) {
		SET_FLOAT_WORD(x,hx);
		return x;
	    }
	}
	SET_FLOAT_WORD(x,hx);
	return x;
}
