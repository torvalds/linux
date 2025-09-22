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
 *	nextafterl(x,y)
 *	return the next machine floating-point number of x in the
 *	direction toward y.
 *   Special cases:
 */

#include <math.h>

#include "math_private.h"

long double
nextafterl(long double x, long double y)
{
	int32_t hx,hy,ix,iy;
	u_int32_t lx,ly;
	int32_t esx,esy;

	GET_LDOUBLE_WORDS(esx,hx,lx,x);
	GET_LDOUBLE_WORDS(esy,hy,ly,y);
	ix = esx&0x7fff;		/* |x| */
	iy = esy&0x7fff;		/* |y| */

	if (((ix==0x7fff)&&(((hx&0x7fffffff)|lx)!=0)) ||   /* x is nan */
	    ((iy==0x7fff)&&(((hy&0x7fffffff)|ly)!=0)))     /* y is nan */
	   return x+y;
	if(x==y) return y;		/* x=y, return y */
	if((ix|hx|lx)==0) {			/* x == 0 */
	    volatile long double u;
	    SET_LDOUBLE_WORDS(x,esy&0x8000,0,1);/* return +-minsubnormal */
	    u = x;
	    u = u * u;				/* raise underflow flag */
	    return x;
	}
	if(esx>=0) {			/* x > 0 */
	    if(esx>esy||((esx==esy) && (hx>hy||((hx==hy)&&(lx>ly))))) {
	      /* x > y, x -= ulp */
		if(lx==0) {
		    if ((hx&0x7fffffff)==0) esx -= 1;
		    hx = (hx - 1) | (hx & 0x80000000);
		}
		lx -= 1;
	    } else {				/* x < y, x += ulp */
		lx += 1;
		if(lx==0) {
		    hx = (hx + 1) | (hx & 0x80000000);
		    if ((hx&0x7fffffff)==0) esx += 1;
		}
	    }
	} else {				/* x < 0 */
	    if(esy>=0||(esx>esy||((esx==esy)&&(hx>hy||((hx==hy)&&(lx>ly)))))){
	      /* x < y, x -= ulp */
		if(lx==0) {
		    if ((hx&0x7fffffff)==0) esx -= 1;
		    hx = (hx - 1) | (hx & 0x80000000);
		}
		lx -= 1;
	    } else {				/* x > y, x += ulp */
		lx += 1;
		if(lx==0) {
		    hx = (hx + 1) | (hx & 0x80000000);
		    if ((hx&0x7fffffff)==0) esx += 1;
		}
	    }
	}
	esy = esx&0x7fff;
	if(esy==0x7fff) return x+x;		/* overflow  */
	if(esy==0) {
	    volatile long double u = x*x;	/* underflow */
	    if(u==x) {
		SET_LDOUBLE_WORDS(x,esx,hx,lx);
		return x;
	    }
	}
	SET_LDOUBLE_WORDS(x,esx,hx,lx);
	return x;
}
DEF_STD(nextafterl);
MAKE_UNUSED_CLONE(nexttowardl, nextafterl);
