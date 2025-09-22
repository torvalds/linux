/*	$OpenBSD: e_atan2l.c,v 1.2 2016/09/12 19:47:02 guenther Exp $	*/
/* @(#)e_atan2.c 1.3 95/01/18 */
/* FreeBSD: head/lib/msun/src/e_atan2.c 176451 2008-02-22 02:30:36Z das */
/*
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunSoft, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice 
 * is preserved.
 * ====================================================
 *
 */

/*
 * See comments in e_atan2.c.
 * Converted to long double by David Schultz <das@FreeBSD.ORG>.
 * Adapted for OpenBSD by Martynas Venckus <martynas@openbsd.org>.
 */

#include <float.h>
#include <math.h>

#include "invtrig.h"
#include "math.h"
#include "math_private.h"

#ifdef EXT_IMPLICIT_NBIT
#define	LDBL_NBIT	0
#else /* EXT_IMPLICIT_NBIT */
#define	LDBL_NBIT	0x80000000
#endif /* EXT_IMPLICIT_NBIT */

static volatile long double
tiny  = 1.0e-300;
static const long double
zero  = 0.0;

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
atan2l(long double y, long double x)
{
	union {
		long double e;
		struct ieee_ext bits;
	} ux, uy;
	long double z;
	int32_t k,m;
	int16_t exptx, expsignx, expty, expsigny;

	uy.e = y;
	expsigny = (uy.bits.ext_sign << 15) | uy.bits.ext_exp;
	expty = expsigny & 0x7fff;
	ux.e = x;
	expsignx = (ux.bits.ext_sign << 15) | ux.bits.ext_exp;
	exptx = expsignx & 0x7fff;

	if ((exptx==BIAS+LDBL_MAX_EXP &&
	     ((ux.bits.ext_frach&~LDBL_NBIT)
#ifdef EXT_FRACHMBITS
		| ux.bits.ext_frachm
#endif /* EXT_FRACHMBITS */
#ifdef EXT_FRACLMBITS
		| ux.bits.ext_fraclm
#endif /* EXT_FRACLMBITS */
		| ux.bits.ext_fracl)!=0) ||	/* x is NaN */
	    (expty==BIAS+LDBL_MAX_EXP &&
	     ((uy.bits.ext_frach&~LDBL_NBIT)
#ifdef EXT_FRACHMBITS
		| uy.bits.ext_frachm
#endif /* EXT_FRACHMBITS */
#ifdef EXT_FRACLMBITS
		| uy.bits.ext_fraclm
#endif /* EXT_FRACLMBITS */
		| uy.bits.ext_fracl)!=0))	/* y is NaN */
	    return x+y;
	if (expsignx==BIAS && ((ux.bits.ext_frach&~LDBL_NBIT)
#ifdef EXT_FRACHMBITS
		| ux.bits.ext_frachm
#endif /* EXT_FRACHMBITS */
#ifdef EXT_FRACLMBITS
		| ux.bits.ext_fraclm
#endif /* EXT_FRACLMBITS */
		| ux.bits.ext_fracl)==0)
	    return atanl(y);					/* x=1.0 */
	m = ((expsigny>>15)&1)|((expsignx>>14)&2);	/* 2*sign(x)+sign(y) */

    /* when y = 0 */
	if(expty==0 && ((uy.bits.ext_frach&~LDBL_NBIT)
#ifdef EXT_FRACHMBITS
		| uy.bits.ext_frachm
#endif /* EXT_FRACHMBITS */
#ifdef EXT_FRACLMBITS
		| uy.bits.ext_fraclm
#endif /* EXT_FRACLMBITS */
		| uy.bits.ext_fracl)==0) {
	    switch(m) {
		case 0: 
		case 1: return y; 	/* atan(+-0,+anything)=+-0 */
		case 2: return  pi+tiny;/* atan(+0,-anything) = pi */
		case 3: return -pi-tiny;/* atan(-0,-anything) =-pi */
	    }
	}
    /* when x = 0 */
	if(exptx==0 && ((ux.bits.ext_frach&~LDBL_NBIT)
#ifdef EXT_FRACHMBITS
		| ux.bits.ext_frachm
#endif /* EXT_FRACHMBITS */
#ifdef EXT_FRACLMBITS
		| ux.bits.ext_fraclm
#endif /* EXT_FRACLMBITS */
		| ux.bits.ext_fracl)==0)
	    return (expsigny<0)?  -pio2_hi-tiny: pio2_hi+tiny;

    /* when x is INF */
	if(exptx==BIAS+LDBL_MAX_EXP) {
	    if(expty==BIAS+LDBL_MAX_EXP) {
		switch(m) {
		    case 0: return  pio2_hi*0.5+tiny;/* atan(+INF,+INF) */
		    case 1: return -pio2_hi*0.5-tiny;/* atan(-INF,+INF) */
		    case 2: return  1.5*pio2_hi+tiny;/*atan(+INF,-INF)*/
		    case 3: return -1.5*pio2_hi-tiny;/*atan(-INF,-INF)*/
		}
	    } else {
		switch(m) {
		    case 0: return  zero  ;	/* atan(+...,+INF) */
		    case 1: return -zero  ;	/* atan(-...,+INF) */
		    case 2: return  pi+tiny  ;	/* atan(+...,-INF) */
		    case 3: return -pi-tiny  ;	/* atan(-...,-INF) */
		}
	    }
	}
    /* when y is INF */
	if(expty==BIAS+LDBL_MAX_EXP)
	    return (expsigny<0)? -pio2_hi-tiny: pio2_hi+tiny;

    /* compute y/x */
	k = expty-exptx;
	if(k > LDBL_MANT_DIG+2) {			/* |y/x| huge */
	    z=pio2_hi+pio2_lo;
	    m&=1;
	}
	else if(expsignx<0&&k<-LDBL_MANT_DIG-2) z=0.0; 	/* |y/x| tiny, x<0 */
	else z=atanl(fabsl(y/x));		/* safe to do y/x */
	switch (m) {
	    case 0: return       z  ;	/* atan(+,+) */
	    case 1: return      -z  ;	/* atan(-,+) */
	    case 2: return  pi-(z-pi_lo);/* atan(+,-) */
	    default: /* case 3 */
	    	    return  (z-pi_lo)-pi;/* atan(-,-) */
	}
}
DEF_STD(atan2l);
