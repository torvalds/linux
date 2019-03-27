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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <float.h>
#include <stdint.h>

#include "fpmath.h"
#include "math.h"
#include "math_private.h"

#define	BIAS (LDBL_MAX_EXP - 1)

#if LDBL_MANL_SIZE > 32
typedef	uint64_t manl_t;
#else
typedef	uint32_t manl_t;
#endif

#if LDBL_MANH_SIZE > 32
typedef	uint64_t manh_t;
#else
typedef	uint32_t manh_t;
#endif

/*
 * These macros add and remove an explicit integer bit in front of the
 * fractional mantissa, if the architecture doesn't have such a bit by
 * default already.
 */
#ifdef LDBL_IMPLICIT_NBIT
#define	SET_NBIT(hx)	((hx) | (1ULL << LDBL_MANH_SIZE))
#define	HFRAC_BITS	LDBL_MANH_SIZE
#else
#define	SET_NBIT(hx)	(hx)
#define	HFRAC_BITS	(LDBL_MANH_SIZE - 1)
#endif

#define	MANL_SHIFT	(LDBL_MANL_SIZE - 1)

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
	union IEEEl2bits ux, uy;
	int64_t hx,hz;	/* We need a carry bit even if LDBL_MANH_SIZE is 32. */
	manh_t hy;
	manl_t lx,ly,lz;
	int ix,iy,n,q,sx,sxy;

	ux.e = x;
	uy.e = y;
	sx = ux.bits.sign;
	sxy = sx ^ uy.bits.sign;
	ux.bits.sign = 0;	/* |x| */
	uy.bits.sign = 0;	/* |y| */

    /* purge off exception values */
	if((uy.bits.exp|uy.bits.manh|uy.bits.manl)==0 || /* y=0 */
	   (ux.bits.exp == BIAS + LDBL_MAX_EXP) ||	 /* or x not finite */
	   (uy.bits.exp == BIAS + LDBL_MAX_EXP &&
	    ((uy.bits.manh&~LDBL_NBIT)|uy.bits.manl)!=0)) /* or y is NaN */
	    return nan_mix_op(x, y, *)/nan_mix_op(x, y, *);
	if(ux.bits.exp<=uy.bits.exp) {
	    if((ux.bits.exp<uy.bits.exp) ||
	       (ux.bits.manh<=uy.bits.manh &&
		(ux.bits.manh<uy.bits.manh ||
		 ux.bits.manl<uy.bits.manl))) {
		q = 0;
		goto fixup;	/* |x|<|y| return x or x-y */
	    }
	    if(ux.bits.manh==uy.bits.manh && ux.bits.manl==uy.bits.manl) {
		*quo = (sxy ? -1 : 1);
		return Zero[sx];	/* |x|=|y| return x*0*/
	    }
	}

    /* determine ix = ilogb(x) */
	if(ux.bits.exp == 0) {	/* subnormal x */
	    ux.e *= 0x1.0p512;
	    ix = ux.bits.exp - (BIAS + 512);
	} else {
	    ix = ux.bits.exp - BIAS;
	}

    /* determine iy = ilogb(y) */
	if(uy.bits.exp == 0) {	/* subnormal y */
	    uy.e *= 0x1.0p512;
	    iy = uy.bits.exp - (BIAS + 512);
	} else {
	    iy = uy.bits.exp - BIAS;
	}

    /* set up {hx,lx}, {hy,ly} and align y to x */
	hx = SET_NBIT(ux.bits.manh);
	hy = SET_NBIT(uy.bits.manh);
	lx = ux.bits.manl;
	ly = uy.bits.manl;

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
	    q &= 0x7fffffff;
	    *quo = (sxy ? -q : q);
	    return Zero[sx];
	}
	while(hx<(1ULL<<HFRAC_BITS)) {	/* normalize x */
	    hx = hx+hx+(lx>>MANL_SHIFT); lx = lx+lx;
	    iy -= 1;
	}
	ux.bits.manh = hx; /* The integer bit is truncated here if needed. */
	ux.bits.manl = lx;
	if (iy < LDBL_MIN_EXP) {
	    ux.bits.exp = iy + (BIAS + 512);
	    ux.e *= 0x1p-512;
	} else {
	    ux.bits.exp = iy + BIAS;
	}
fixup:
	x = ux.e;		/* |x| */
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
	ux.e = x;
	ux.bits.sign ^= sx;
	x = ux.e;
	q &= 0x7fffffff;
	*quo = (sxy ? -q : q);
	return x;
}
