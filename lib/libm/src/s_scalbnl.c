/*	$OpenBSD: s_scalbnl.c,v 1.4 2016/09/12 19:47:02 guenther Exp $	*/
/* @(#)s_scalbn.c 5.1 93/09/24 */
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
 * scalbnl (long double x, int n)
 * scalbnl(x,n) returns x* 2**n  computed by  exponent
 * manipulation rather than by actually performing an
 * exponentiation or a multiplication.
 */

/*
 * We assume that a long double has a 15-bit exponent.  On systems
 * where long double is the same as double, scalbnl() is an alias
 * for scalbn(), so we don't use this routine.
 */

#include <sys/types.h>
#include <machine/ieee.h>
#include <float.h>
#include <math.h>

#if LDBL_MAX_EXP != 0x4000
#error "Unsupported long double format"
#endif

static const long double
huge = 0x1p16000L,
tiny = 0x1p-16000L;

long double
scalbnl (long double x, int n)
{
	union {
		long double e;
		struct ieee_ext bits;
	} u;
	int k;
	u.e = x;
        k = u.bits.ext_exp;			/* extract exponent */
        if (k==0) {				/* 0 or subnormal x */
            if ((u.bits.ext_frach
#ifdef EXT_FRACHMBITS
		| u.bits.ext_frachm
#endif /* EXT_FRACHMBITS */
#ifdef EXT_FRACLMBITS
		| u.bits.ext_fraclm
#endif /* EXT_FRACLMBITS */
		| u.bits.ext_fracl)==0) return x;	/* +-0 */
	    u.e *= 0x1p+128;
	    k = u.bits.ext_exp - 128;
            if (n< -50000) return tiny*x; 	/*underflow*/
	    }
        if (k==0x7fff) return x+x;		/* NaN or Inf */
        k = k+n;
        if (k >= 0x7fff) return huge*copysignl(huge,x); /* overflow  */
        if (k > 0) 				/* normal result */
	    {u.bits.ext_exp = k; return u.e;}
        if (k <= -128) {
            if (n > 50000) 	/* in case integer overflow in n+k */
		return huge*copysign(huge,x);	/*overflow*/
	    else return tiny*copysign(tiny,x); 	/*underflow*/
	}
        k += 128;				/* subnormal result */
	u.bits.ext_exp = k;
        return u.e*0x1p-128;
}
DEF_STD(scalbnl);

long double
ldexpl(long double x, int n)
{
	return scalbnl(x, n);
}
DEF_STD(ldexpl);
