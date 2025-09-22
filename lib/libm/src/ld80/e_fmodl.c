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

static const long double one = 1.0, Zero[] = {0.0, -0.0,};

/*
 * fmodl(x,y)
 * Return x mod y in exact arithmetic
 * Method: shift and subtract
 *
 * Assumptions:
 * - The low part of the mantissa fits in a manl_t exactly.
 * - The high part of the mantissa fits in an int64_t with enough room
 *   for an explicit integer bit in front of the fractional bits.
 */
long double
fmodl(long double x, long double y)
{
	union {
		long double e;
		struct ieee_ext bits;
	} ux, uy;
	int64_t hx,hz;	/* We need a carry bit even if LDBL_MANH_SIZE is 32. */
	uint32_t hy;
	uint32_t lx,ly,lz;
	int ix,iy,n,sx;

	ux.e = x;
	uy.e = y;
	sx = ux.bits.ext_sign;

    /* purge off exception values */
	if((uy.bits.ext_exp|uy.bits.ext_frach|uy.bits.ext_fracl)==0 || /* y=0 */
	   (ux.bits.ext_exp == BIAS + LDBL_MAX_EXP) ||	 /* or x not finite */
	   (uy.bits.ext_exp == BIAS + LDBL_MAX_EXP &&
	    ((uy.bits.ext_frach&~LDBL_NBIT)|uy.bits.ext_fracl)!=0)) /* or y is NaN */
	    return (x*y)/(x*y);
	if(ux.bits.ext_exp<=uy.bits.ext_exp) {
	    if((ux.bits.ext_exp<uy.bits.ext_exp) ||
	       (ux.bits.ext_frach<=uy.bits.ext_frach &&
		(ux.bits.ext_frach<uy.bits.ext_frach ||
		 ux.bits.ext_fracl<uy.bits.ext_fracl))) {
		return x;		/* |x|<|y| return x or x-y */
	    }
	    if(ux.bits.ext_frach==uy.bits.ext_frach &&
		ux.bits.ext_fracl==uy.bits.ext_fracl) {
		return Zero[sx];	/* |x|=|y| return x*0*/
	    }
	}

    /* determine ix = ilogb(x) */
	if(ux.bits.ext_exp == 0) {	/* subnormal x */
	    ux.e *= 0x1.0p512;
	    ix = ux.bits.ext_exp - (BIAS + 512);
	} else {
	    ix = ux.bits.ext_exp - BIAS;
	}

    /* determine iy = ilogb(y) */
	if(uy.bits.ext_exp == 0) {	/* subnormal y */
	    uy.e *= 0x1.0p512;
	    iy = uy.bits.ext_exp - (BIAS + 512);
	} else {
	    iy = uy.bits.ext_exp - BIAS;
	}

    /* set up {hx,lx}, {hy,ly} and align y to x */
	hx = SET_NBIT(ux.bits.ext_frach);
	hy = SET_NBIT(uy.bits.ext_frach);
	lx = ux.bits.ext_fracl;
	ly = uy.bits.ext_fracl;

    /* fix point fmod */
	n = ix - iy;

	while(n--) {
	    hz=hx-hy;lz=lx-ly; if(lx<ly) hz -= 1;
	    if(hz<0){hx = hx+hx+(lx>>MANL_SHIFT); lx = lx+lx;}
	    else {
		if ((hz|lz)==0)		/* return sign(x)*0 */
		    return Zero[sx];
		hx = hz+hz+(lz>>MANL_SHIFT); lx = lz+lz;
	    }
	}
	hz=hx-hy;lz=lx-ly; if(lx<ly) hz -= 1;
	if(hz>=0) {hx=hz;lx=lz;}

    /* convert back to floating value and restore the sign */
	if((hx|lx)==0)			/* return sign(x)*0 */
	    return Zero[sx];
	while(hx<(1ULL<<HFRAC_BITS)) {	/* normalize x */
	    hx = hx+hx+(lx>>MANL_SHIFT); lx = lx+lx;
	    iy -= 1;
	}
	ux.bits.ext_frach = hx; /* The mantissa is truncated here if needed. */
	ux.bits.ext_fracl = lx;
	if (iy < LDBL_MIN_EXP) {
	    ux.bits.ext_exp = iy + (BIAS + 512);
	    ux.e *= 0x1p-512;
	} else {
	    ux.bits.ext_exp = iy + BIAS;
	}
	x = ux.e * one;		/* create necessary signal */
	return x;		/* exact output */
}
