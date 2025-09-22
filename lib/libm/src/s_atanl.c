/*	$OpenBSD: s_atanl.c,v 1.2 2016/09/12 19:47:02 guenther Exp $	*/
/* @(#)s_atan.c 5.1 93/09/24 */
/* FreeBSD: head/lib/msun/src/s_atan.c 176451 2008-02-22 02:30:36Z das */
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
 * See comments in s_atan.c.
 * Converted to long double by David Schultz <das@FreeBSD.ORG>.
 * Adapted for OpenBSD by Martynas Venckus <martynas@openbsd.org>.
 */

#include <float.h>
#include <math.h>

#include "invtrig.h"
#include "math_private.h"

#ifdef EXT_IMPLICIT_NBIT
#define	LDBL_NBIT	0
#else /* EXT_IMPLICIT_NBIT */
#define	LDBL_NBIT	0x80000000
#endif /* EXT_IMPLICIT_NBIT */

static const long double
one   = 1.0,
huge   = 1.0e300;

long double
atanl(long double x)
{
	union {
		long double e;
		struct ieee_ext bits;
	} u;
	long double w,s1,s2,z;
	int id;
	int16_t expsign, expt;
	int32_t expman;

	u.e = x;
	expsign = (u.bits.ext_sign << 15) | u.bits.ext_exp;
	expt = expsign & 0x7fff;
	if(expt >= ATAN_CONST) {	/* if |x| is large, atan(x)~=pi/2 */
	    if(expt == BIAS + LDBL_MAX_EXP &&
	       ((u.bits.ext_frach&~LDBL_NBIT)
#ifdef EXT_FRACHMBITS
		| u.bits.ext_frachm
#endif /* EXT_FRACHMBITS */
#ifdef EXT_FRACLMBITS
		| u.bits.ext_fraclm
#endif /* EXT_FRACLMBITS */
		| u.bits.ext_fracl)!=0)
		return x+x;		/* NaN */
	    if(expsign>0) return  atanhi[3]+atanlo[3];
	    else     return -atanhi[3]-atanlo[3];
	}
	/* Extract the exponent and the first few bits of the mantissa. */
	/* XXX There should be a more convenient way to do this. */
	expman = (expt << 8) |
		((u.bits.ext_frach >> (EXT_FRACHBITS - 9)) & 0xff);
	if (expman < ((BIAS - 2) << 8) + 0xc0) {	/* |x| < 0.4375 */
	    if (expt < ATAN_LINEAR) {	/* if |x| is small, atanl(x)~=x */
		if(huge+x>one) return x;	/* raise inexact */
	    }
	    id = -1;
	} else {
	x = fabsl(x);
	if (expman < (BIAS << 8) + 0x30) {		/* |x| < 1.1875 */
	    if (expman < ((BIAS - 1) << 8) + 0x60) {	/* 7/16 <=|x|<11/16 */
		id = 0; x = (2.0*x-one)/(2.0+x);
	    } else {			/* 11/16<=|x|< 19/16 */
		id = 1; x  = (x-one)/(x+one);
	    }
	} else {
	    if (expman < ((BIAS + 1) << 8) + 0x38) {	/* |x| < 2.4375 */
		id = 2; x  = (x-1.5)/(one+1.5*x);
	    } else {			/* 2.4375 <= |x| < 2^ATAN_CONST */
		id = 3; x  = -1.0/x;
	    }
	}}
    /* end of argument reduction */
	z = x*x;
	w = z*z;
    /* break sum aT[i]z**(i+1) into odd and even poly */
	s1 = z*T_even(w);
	s2 = w*T_odd(w);
	if (id<0) return x - x*(s1+s2);
	else {
	    z = atanhi[id] - ((x*(s1+s2) - atanlo[id]) - x);
	    return (expsign<0)? -z:z;
	}
}
DEF_STD(atanl);
