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
#define	HFRAC_BITS	(EXT_FRACHBITS + EXT_FRACHMBITS)
#else
#define	LDBL_NBIT	0x80000000
#define	SET_NBIT(hx)	(hx)
#define	HFRAC_BITS	(EXT_FRACHBITS + EXT_FRACHMBITS - 1)
#endif

#define	MANL_SHIFT	(EXT_FRACLMBITS + EXT_FRACLBITS - 1)

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
	int64_t hx,hz,hy,_hx;
	uint64_t lx,ly,lz;
	uint64_t sx,sxy;
	int ix,iy,n,q;

	GET_LDOUBLE_WORDS64(hx,lx,x);
	GET_LDOUBLE_WORDS64(hy,ly,y);
	sx = (hx>>48)&0x8000;
	sxy = sx ^ ((hy>>48)&0x8000);
	hx &= 0x7fffffffffffffffLL;	/* |x| */
	hy &= 0x7fffffffffffffffLL;	/* |y| */
	SET_LDOUBLE_WORDS64(x,hx,lx);
	SET_LDOUBLE_WORDS64(y,hy,ly);

    /* purge off exception values */
	if((hy|ly)==0 || /* y=0 */
	   ((hx>>48) == BIAS + LDBL_MAX_EXP) ||	 /* or x not finite */
	   ((hy>>48) == BIAS + LDBL_MAX_EXP &&
	    (((hy&0x0000ffffffffffffLL)&~LDBL_NBIT)|ly)!=0)) /* or y is NaN */
	    return (x*y)/(x*y);
	if((hx>>48)<=(hy>>48)) {
	    if(((hx>>48)<(hy>>48)) ||
	       ((hx&0x0000ffffffffffffLL)<=(hy&0x0000ffffffffffffLL) &&
		((hx&0x0000ffffffffffffLL)<(hy&0x0000ffffffffffffLL) ||
		 lx<ly))) {
		q = 0;
		goto fixup;	/* |x|<|y| return x or x-y */
	    }
	    if((hx&0x0000ffffffffffffLL)==(hy&0x0000ffffffffffffLL) &&
		lx==ly) {
		*quo = 1;
		return Zero[sx!=0];	/* |x|=|y| return x*0*/
	    }
	}

    /* determine ix = ilogb(x) */
	if((hx>>48) == 0) {	/* subnormal x */
	    x *= 0x1.0p512;
	    GET_LDOUBLE_WORDS64(hx,lx,x);
	    ix = (hx>>48) - (BIAS + 512);
	} else {
	    ix = (hx>>48) - BIAS;
	}

    /* determine iy = ilogb(y) */
	if((hy>>48) == 0) {	/* subnormal y */
	    y *= 0x1.0p512;
	    GET_LDOUBLE_WORDS64(hy,ly,y);
	    iy = (hy>>48) - (BIAS + 512);
	} else {
	    iy = (hy>>48) - BIAS;
	}

    /* set up {hx,lx}, {hy,ly} and align y to x */
	_hx = SET_NBIT(hx) & 0x0000ffffffffffffLL;
	hy = SET_NBIT(hy);

    /* fix point fmod */
	n = ix - iy;
	q = 0;

	while(n--) {
	    hz=_hx-hy;lz=lx-ly; if(lx<ly) hz -= 1;
	    if(hz<0){_hx = _hx+_hx+(lx>>MANL_SHIFT); lx = lx+lx;}
	    else {_hx = hz+hz+(lz>>MANL_SHIFT); lx = lz+lz; q++;}
	    q <<= 1;
	}
	hz=_hx-hy;lz=lx-ly; if(lx<ly) hz -= 1;
	if(hz>=0) {_hx=hz;lx=lz;q++;}

    /* convert back to floating value and restore the sign */
	if((_hx|lx)==0) {			/* return sign(x)*0 */
	    *quo = (sxy ? -q : q);
	    return Zero[sx!=0];
	}
	while(_hx<(1ULL<<HFRAC_BITS)) {	/* normalize x */
	    _hx = _hx+_hx+(lx>>MANL_SHIFT); lx = lx+lx;
	    iy -= 1;
	}
	hx = (hx&0xffff000000000000LL) | (_hx&0x0000ffffffffffffLL);
	if (iy < LDBL_MIN_EXP) {
	    hx = (hx&0x0000ffffffffffffLL) | (uint64_t)(iy + BIAS + 512)<<48;
	    SET_LDOUBLE_WORDS64(x,hx,lx);
	    x *= 0x1p-512;
	    GET_LDOUBLE_WORDS64(hx,lx,x);
	} else {
	    hx = (hx&0x0000ffffffffffffLL) | (uint64_t)(iy + BIAS)<<48;
	}
	hx &= 0x7fffffffffffffffLL;
	SET_LDOUBLE_WORDS64(x,hx,lx);
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

	GET_LDOUBLE_MSW64(hx,x);
	hx ^= sx;
	SET_LDOUBLE_MSW64(x,hx);

	q &= 0x7fffffff;
	*quo = (sxy ? -q : q);
	return x;
}
DEF_STD(remquol);
