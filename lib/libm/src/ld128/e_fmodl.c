/* @(#)e_fmod.c 5.1 93/09/24 */
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

/*
 * fmodl(x,y)
 * Return x mod y in exact arithmetic
 * Method: shift and subtract
 */

#include <math.h>

#include "math_private.h"

static const long double one = 1.0, Zero[] = {0.0, -0.0,};

long double
fmodl(long double x, long double y)
{
	int64_t n,hx,hy,hz,ix,iy,sx,i;
	u_int64_t lx,ly,lz;

	GET_LDOUBLE_WORDS64(hx,lx,x);
	GET_LDOUBLE_WORDS64(hy,ly,y);
	sx = hx&0x8000000000000000ULL;		/* sign of x */
	hx ^=sx;				/* |x| */
	hy &= 0x7fffffffffffffffLL;		/* |y| */

    /* purge off exception values */
	if((hy|ly)==0||(hx>=0x7fff000000000000LL)|| /* y=0,or x not finite */
	  ((hy|((ly|-ly)>>63))>0x7fff000000000000LL))	/* or y is NaN */
	    return (x*y)/(x*y);
	if(hx<=hy) {
	    if((hx<hy)||(lx<ly)) return x;	/* |x|<|y| return x */
	    if(lx==ly)
		return Zero[(u_int64_t)sx>>63];	/* |x|=|y| return x*0*/
	}

    /* determine ix = ilogb(x) */
	if(hx<0x0001000000000000LL) {	/* subnormal x */
	    if(hx==0) {
		for (ix = -16431, i=lx; i>0; i<<=1) ix -=1;
	    } else {
		for (ix = -16382, i=hx<<15; i>0; i<<=1) ix -=1;
	    }
	} else ix = (hx>>48)-0x3fff;

    /* determine iy = ilogb(y) */
	if(hy<0x0001000000000000LL) {	/* subnormal y */
	    if(hy==0) {
		for (iy = -16431, i=ly; i>0; i<<=1) iy -=1;
	    } else {
		for (iy = -16382, i=hy<<15; i>0; i<<=1) iy -=1;
	    }
	} else iy = (hy>>48)-0x3fff;

    /* set up {hx,lx}, {hy,ly} and align y to x */
	if(ix >= -16382)
	    hx = 0x0001000000000000LL|(0x0000ffffffffffffLL&hx);
	else {		/* subnormal x, shift x to normal */
	    n = -16382-ix;
	    if(n<=63) {
		hx = (hx<<n)|(lx>>(64-n));
		lx <<= n;
	    } else {
		hx = lx<<(n-64);
		lx = 0;
	    }
	}
	if(iy >= -16382)
	    hy = 0x0001000000000000LL|(0x0000ffffffffffffLL&hy);
	else {		/* subnormal y, shift y to normal */
	    n = -16382-iy;
	    if(n<=63) {
		hy = (hy<<n)|(ly>>(64-n));
		ly <<= n;
	    } else {
		hy = ly<<(n-64);
		ly = 0;
	    }
	}

    /* fix point fmod */
	n = ix - iy;
	while(n--) {
	    hz=hx-hy;lz=lx-ly; if(lx<ly) hz -= 1;
	    if(hz<0){hx = hx+hx+(lx>>63); lx = lx+lx;}
	    else {
		if((hz|lz)==0)		/* return sign(x)*0 */
		    return Zero[(u_int64_t)sx>>63];
		hx = hz+hz+(lz>>63); lx = lz+lz;
	    }
	}
	hz=hx-hy;lz=lx-ly; if(lx<ly) hz -= 1;
	if(hz>=0) {hx=hz;lx=lz;}

    /* convert back to floating value and restore the sign */
	if((hx|lx)==0)			/* return sign(x)*0 */
	    return Zero[(u_int64_t)sx>>63];	
	while(hx<0x0001000000000000LL) {	/* normalize x */
	    hx = hx+hx+(lx>>63); lx = lx+lx;
	    iy -= 1;
	}
	if(iy>= -16382) {	/* normalize output */
	    hx = ((hx-0x0001000000000000LL)|((iy+16383)<<48));
	    SET_LDOUBLE_WORDS64(x,hx|sx,lx);
	} else {		/* subnormal output */
	    n = -16382 - iy;
	    if(n<=48) {
		lx = (lx>>n)|((u_int64_t)hx<<(64-n));
		hx >>= n;
	    } else if (n<=63) {
		lx = (hx<<(64-n))|(lx>>n); hx = sx;
	    } else {
		lx = hx>>(n-64); hx = sx;
	    }
	    SET_LDOUBLE_WORDS64(x,hx|sx,lx);
	    x *= one;		/* create necessary signal */
	}
	return x;		/* exact output */
}
