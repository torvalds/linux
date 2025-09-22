/*	$OpenBSD: e_acosl.c,v 1.1 2008/12/09 20:00:35 martynas Exp $	*/
/* @(#)e_acos.c 1.3 95/01/18 */
/* FreeBSD: head/lib/msun/src/e_acos.c 176451 2008-02-22 02:30:36Z das */
/*
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunSoft, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice 
 * is preserved.
 * ====================================================
 */

/*
 * See comments in e_acos.c.
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
one=  1.00000000000000000000e+00;

#if defined(__amd64__) || defined(__i386__)
/* XXX Work around the fact that gcc truncates long double constants on i386 */
static volatile double
pi1 =  3.14159265358979311600e+00,	/*  0x1.921fb54442d18p+1  */
pi2 =  1.22514845490862001043e-16;	/*  0x1.1a80000000000p-53 */
#define	pi	((long double)pi1 + pi2)
#else
static const long double
pi =  3.14159265358979323846264338327950280e+00L;
#endif

long double
acosl(long double x)
{
	union {
		long double e;
		struct ieee_ext bits;
	} u;
	long double z,p,q,r,w,s,c,df;
	int16_t expsign, expt;
	u.e = x;
	expsign = (u.bits.ext_sign << 15) | u.bits.ext_exp;
	expt = expsign & 0x7fff;
	if(expt >= BIAS) {	/* |x| >= 1 */
	    if(expt==BIAS && ((u.bits.ext_frach&~LDBL_NBIT)
#ifdef EXT_FRACHMBITS
		| u.bits.ext_frachm
#endif /* EXT_FRACHMBITS */
#ifdef EXT_FRACLMBITS
		| u.bits.ext_fraclm
#endif /* EXT_FRACLMBITS */
		| u.bits.ext_fracl)==0) {
		if (expsign>0) return 0.0;	/* acos(1) = 0  */
		else return pi+2.0*pio2_lo;	/* acos(-1)= pi */
	    }
	    return (x-x)/(x-x);		/* acos(|x|>1) is NaN */
	}
	if(expt<BIAS-1) {	/* |x| < 0.5 */
	    if(expt<ACOS_CONST) return pio2_hi+pio2_lo;/*x tiny: acosl=pi/2*/
	    z = x*x;
	    p = P(z);
	    q = Q(z);
	    r = p/q;
	    return pio2_hi - (x - (pio2_lo-x*r));
	} else  if (expsign<0) {	/* x < -0.5 */
	    z = (one+x)*0.5;
	    p = P(z);
	    q = Q(z);
	    s = sqrtl(z);
	    r = p/q;
	    w = r*s-pio2_lo;
	    return pi - 2.0*(s+w);
	} else {			/* x > 0.5 */
	    z = (one-x)*0.5;
	    s = sqrtl(z);
	    u.e = s;
	    u.bits.ext_fracl = 0;
#ifdef EXT_FRACLMBITS
	    u.bits.ext_fraclm = 0;
#endif /* EXT_FRACLMBITS */
	    df = u.e;
	    c  = (z-df*df)/(s+df);
	    p = P(z);
	    q = Q(z);
	    r = p/q;
	    w = r*s+c;
	    return 2.0*(df+w);
	}
}
