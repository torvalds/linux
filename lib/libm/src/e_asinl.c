/*	$OpenBSD: e_asinl.c,v 1.2 2016/09/12 19:47:02 guenther Exp $	*/
/* @(#)e_asin.c 1.3 95/01/18 */
/* FreeBSD: head/lib/msun/src/e_asin.c 176451 2008-02-22 02:30:36Z das */
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
 * See comments in e_asin.c.
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
one =  1.00000000000000000000e+00,
huge = 1.000e+300;

long double
asinl(long double x)
{
	union {
		long double e;
		struct ieee_ext bits;
	} u;
	long double t=0.0,w,p,q,c,r,s;
	int16_t expsign, expt;
	u.e = x;
	expsign = (u.bits.ext_sign << 15) | u.bits.ext_exp;
	expt = expsign & 0x7fff;
	if(expt >= BIAS) {		/* |x|>= 1 */
		if(expt==BIAS && ((u.bits.ext_frach&~LDBL_NBIT)
#ifdef EXT_FRACHMBITS
			| u.bits.ext_frachm
#endif /* EXT_FRACHMBITS */
#ifdef EXT_FRACLMBITS
			| u.bits.ext_fraclm
#endif /* EXT_FRACLMBITS */
			| u.bits.ext_fracl)==0)
		    /* asin(1)=+-pi/2 with inexact */
		    return x*pio2_hi+x*pio2_lo;	
	    return (x-x)/(x-x);		/* asin(|x|>1) is NaN */   
	} else if (expt<BIAS-1) {	/* |x|<0.5 */
	    if(expt<ASIN_LINEAR) {	/* if |x| is small, asinl(x)=x */
		if(huge+x>one) return x;/* return x with inexact if x!=0*/
	    }
	    t = x*x;
	    p = P(t);
	    q = Q(t);
	    w = p/q;
	    return x+x*w;
	}
	/* 1> |x|>= 0.5 */
	w = one-fabsl(x);
	t = w*0.5;
	p = P(t);
	q = Q(t);
	s = sqrtl(t);
#ifdef EXT_FRACHMBITS
	if((((uint64_t)u.bits.ext_frach << EXT_FRACHMBITS)
		| u.bits.ext_frachm) >= THRESH) {
						/* if |x| is close to 1 */
#else /* EXT_FRACHMBITS */
	if(u.bits.ext_frach>=THRESH) {		/* if |x| is close to 1 */
#endif /* EXT_FRACHMBITS */
	    w = p/q;
	    t = pio2_hi-(2.0*(s+s*w)-pio2_lo);
	} else {
	    u.e = s;
	    u.bits.ext_fracl = 0;
#ifdef EXT_FRACLMBITS
	    u.bits.ext_fraclm = 0;
#endif /* EXT_FRACLMBITS */
	    w = u.e;
	    c  = (t-w*w)/(s+w);
	    r  = p/q;
	    p  = 2.0*s*r-(pio2_lo-2.0*c);
	    q  = pio4_hi-2.0*w;
	    t  = pio4_hi-(p-q);
	}    
	if(expsign>0) return t; else return -t;    
}
DEF_STD(asinl);
