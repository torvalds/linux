/* @(#)e_fmod.c 1.3 95/01/18 */
/*-
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunSoft, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 */

#include <sys/types.h>
#include <machine/ieee.h>

#include <float.h>
#include <math.h>
#include <stdint.h>

#include "math_private.h"

#define	BIAS (LDBL_MAX_EXP - 1)

/*
 * These macros add and remove an explicit integer bit in front of the
 * fractional mantissa, if the architecture doesn't have such a bit by
 * default already.
 */
#ifdef LDBL_IMPLICIT_NBIT
#define	LDBL_NBIT	0
#define	SET_NBIT(hx)	((hx) | (1ULL << LDBL_MANH_SIZE))
#define	HFRAC_BITS	EXT_FRACHBITS
#else
#define	LDBL_NBIT	0x80000000
#define	SET_NBIT(hx)	(hx)
#define	HFRAC_BITS	(EXT_FRACHBITS - 1)
#endif

#define	MANL_SHIFT	(EXT_FRACLBITS - 1)

static const long double Zero[] = {0.0L, -0.0L};

/*
 * Return the IEEE remainder and set *quo to the last n bits of the
 * quotient, rounded to the nearest integer.  We choose n=31 because
 * we wind up computing all the integer bits of the quotient anyway as
 * a side-effect of computing the remainder by the shift and subtract
 * method.  In practice, this is far more bits than are needed to use
 * remquo in reduction algorithms.
 *
 * Assumptions:
 * - The low part of the mantissa fits in a manl_t exactly.
 * - The high part of the mantissa fits in an int64_t with enough room
 *   for an explicit integer bit in front of the fractional bits.
 */
long double
remquol(long double x, long double y, int *quo)
{
	int64_t hx,hz;	/* We need a carry bit even if LDBL_MANH_SIZE is 32. */
	uint32_t hy;
	uint32_t lx,ly,lz;
	uint32_t esx, esy;
	int ix,iy,n,q,sx,sxy;

	GET_LDOUBLE_WORDS(esx,hx,lx,x);
	GET_LDOUBLE_WORDS(esy,hy,ly,y);
	sx = esx & 0x8000;
	sxy = sx ^ (esy & 0x8000);
	esx &= 0x7fff;				/* |x| */
	esy &= 0x7fff;				/* |y| */
	SET_LDOUBLE_EXP(x,esx);
	SET_LDOUBLE_EXP(y,esy);

    /* purge off exception values */
	if((esy|hy|ly)==0 ||			/* y=0 */
	   (esx == BIAS + LDBL_MAX_EXP) ||	/* or x not finite */
	   (esy == BIAS + LDBL_MAX_EXP &&
	    ((hy&~LDBL_NBIT)|ly)!=0))		/* or y is NaN */
	    return (x*y)/(x*y);
	if(esx<=esy) {
	    if((esx<esy) ||
	       (hx<=hy &&
		(hx<hy ||
		 lx<ly))) {
		q = 0;
		goto fixup;			/* |x|<|y| return x or x-y */
	    }
	    if(hx==hy && lx==ly) {
		*quo = 1;
		return Zero[sx!=0];		/* |x|=|y| return x*0*/
	    }
	}

    /* determine ix = ilogb(x) */
	if(esx == 0) {				/* subnormal x */
	    x *= 0x1.0p512;
	    GET_LDOUBLE_WORDS(esx,hx,lx,x);
	    ix = esx - (BIAS + 512);
	} else {
	    ix = esx - BIAS;
	}

    /* determine iy = ilogb(y) */
	if(esy == 0) {				/* subnormal y */
	    y *= 0x1.0p512;
	    GET_LDOUBLE_WORDS(esy,hy,ly,y);
	    iy = esy - (BIAS + 512);
	} else {
	    iy = esy - BIAS;
	}

    /* set up {hx,lx}, {hy,ly} and align y to x */
	hx = SET_NBIT(hx);
	lx = SET_NBIT(lx);

    /* fix point fmod */
	n = ix - iy;
	q = 0;

	while(n--) {
	    hz=hx-hy;lz=lx-ly; if(lx<ly) hz -= 1;
	    if(hz<0){hx = hx+hx+(lx>>MANL_SHIFT); lx = lx+lx;}
	    else {hx = hz+hz+(lz>>MANL_SHIFT); lx = lz+lz; q++;}
	    q <<= 1;
	}
	hz=hx-hy;lz=lx-ly; if(lx<ly) hz -= 1;
	if(hz>=0) {hx=hz;lx=lz;q++;}

    /* convert back to floating value and restore the sign */
	if((hx|lx)==0) {			/* return sign(x)*0 */
	    *quo = (sxy ? -q : q);
	    return Zero[sx!=0];
	}
	while(hx<(1ULL<<HFRAC_BITS)) {	/* normalize x */
	    hx = hx+hx+(lx>>MANL_SHIFT); lx = lx+lx;
	    iy -= 1;
	}
	if (iy < LDBL_MIN_EXP) {
	    esx = (iy + BIAS + 512) & 0x7fff;
	    SET_LDOUBLE_WORDS(x,esx,hx,lx);
	    x *= 0x1p-512;
	    GET_LDOUBLE_WORDS(esx,hx,lx,x);
	} else {
	    esx = (iy + BIAS) & 0x7fff;
	}
	SET_LDOUBLE_WORDS(x,esx,hx,lx);
fixup:
	y = fabsl(y);
	if (y < LDBL_MIN * 2) {
	    if (x+x>y || (x+x==y && (q & 1))) {
		q++;
		x-=y;
	    }
	} else if (x>0.5*y || (x==0.5*y && (q & 1))) {
	    q++;
	    x-=y;
	}

	GET_LDOUBLE_EXP(esx,x);
	esx ^= sx;
	SET_LDOUBLE_EXP(x,esx);

	q &= 0x7fffffff;
	*quo = (sxy ? -q : q);
	return x;
}
DEF_STD(remquol);
