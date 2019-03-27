/* from: FreeBSD: head/lib/msun/src/e_acosh.c 176451 2008-02-22 02:30:36Z das */

/* @(#)s_asinh.c 5.1 93/09/24 */
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * See s_asinh.c for complete comments.
 *
 * Converted to long double by David Schultz <das@FreeBSD.ORG> and
 * Bruce D. Evans.
 */

#include <float.h>
#ifdef __i386__
#include <ieeefp.h>
#endif

#include "fpmath.h"
#include "math.h"
#include "math_private.h"

/* EXP_LARGE is the threshold above which we use asinh(x) ~= log(2x). */
/* EXP_TINY is the threshold below which we use asinh(x) ~= x. */
#if LDBL_MANT_DIG == 64
#define	EXP_LARGE	34
#define	EXP_TINY	-34
#elif LDBL_MANT_DIG == 113
#define	EXP_LARGE	58
#define	EXP_TINY	-58
#else
#error "Unsupported long double format"
#endif

#if LDBL_MAX_EXP != 0x4000
/* We also require the usual expsign encoding. */
#error "Unsupported long double format"
#endif

#define	BIAS	(LDBL_MAX_EXP - 1)

static const double
one =  1.00000000000000000000e+00, /* 0x3FF00000, 0x00000000 */
huge=  1.00000000000000000000e+300;

#if LDBL_MANT_DIG == 64
static const union IEEEl2bits
u_ln2 =  LD80C(0xb17217f7d1cf79ac, -1, 6.93147180559945309417e-1L);
#define	ln2	u_ln2.e
#elif LDBL_MANT_DIG == 113
static const long double
ln2 =  6.93147180559945309417232121458176568e-1L;	/* 0x162e42fefa39ef35793c7673007e6.0p-113 */
#else
#error "Unsupported long double format"
#endif

long double
asinhl(long double x)
{
	long double t, w;
	uint16_t hx, ix;

	ENTERI();
	GET_LDBL_EXPSIGN(hx, x);
	ix = hx & 0x7fff;
	if (ix >= 0x7fff) RETURNI(x+x);	/* x is inf, NaN or misnormal */
	if (ix < BIAS + EXP_TINY) {	/* |x| < TINY, or misnormal */
	    if (huge + x > one) RETURNI(x);	/* return x inexact except 0 */
	}
	if (ix >= BIAS + EXP_LARGE) {	/* |x| >= LARGE, or misnormal */
	    w = logl(fabsl(x))+ln2;
	} else if (ix >= 0x4000) {	/* LARGE > |x| >= 2.0, or misnormal */
	    t = fabsl(x);
	    w = logl(2.0*t+one/(sqrtl(x*x+one)+t));
	} else {		/* 2.0 > |x| >= TINY, or misnormal */
	    t = x*x;
	    w =log1pl(fabsl(x)+t/(one+sqrtl(one+t)));
	}
	RETURNI((hx & 0x8000) == 0 ? w : -w);
}
