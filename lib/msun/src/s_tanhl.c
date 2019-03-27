/* from: FreeBSD: head/lib/msun/src/s_tanhl.c XXX */

/* @(#)s_tanh.c 5.1 93/09/24 */
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
 * See s_tanh.c for complete comments.
 *
 * Converted to long double by Bruce D. Evans.
 */

#include <float.h>
#ifdef __i386__
#include <ieeefp.h>
#endif

#include "math.h"
#include "math_private.h"
#include "fpmath.h"
#include "k_expl.h"

#if LDBL_MAX_EXP != 0x4000
/* We also require the usual expsign encoding. */
#error "Unsupported long double format"
#endif

#define	BIAS	(LDBL_MAX_EXP - 1)

static const volatile double tiny = 1.0e-300;
static const double one = 1.0;
#if LDBL_MANT_DIG == 64
/*
 * Domain [-0.25, 0.25], range ~[-1.6304e-22, 1.6304e-22]:
 * |tanh(x)/x - t(x)| < 2**-72.3
 */
static const union IEEEl2bits
T3u = LD80C(0xaaaaaaaaaaaaaa9f, -2, -3.33333333333333333017e-1L);
#define	T3	T3u.e
static const double
T5  =  1.3333333333333314e-1,		/*  0x1111111111110a.0p-55 */
T7  = -5.3968253968210485e-2,		/* -0x1ba1ba1ba1a1a1.0p-57 */
T9  =  2.1869488531393817e-2,		/*  0x1664f488172022.0p-58 */
T11 = -8.8632352345964591e-3,		/* -0x1226e34bc138d5.0p-59 */
T13 =  3.5921169709993771e-3,		/*  0x1d6d371d3e400f.0p-61 */
T15 = -1.4555786415756001e-3,		/* -0x17d923aa63814d.0p-62 */
T17 =  5.8645267876296793e-4,		/*  0x13378589b85aa7.0p-63 */
T19 = -2.1121033571392224e-4;		/* -0x1baf0af80c4090.0p-65 */
#elif LDBL_MANT_DIG == 113
/*
 * Domain [-0.25, 0.25], range ~[-2.4211e-37, 2.4211e-37]:
 * |tanh(x)/x - t(x)| < 2**121.6
 */
static const long double
T3 = -3.33333333333333333333333333333332980e-1L,	/* -0x1555555555555555555555555554e.0p-114L */
T5  =  1.33333333333333333333333333332707260e-1L,	/*  0x1111111111111111111111110ab7b.0p-115L */
T7  = -5.39682539682539682539682535723482314e-2L,	/* -0x1ba1ba1ba1ba1ba1ba1ba17b5fc98.0p-117L */
T9  =  2.18694885361552028218693591149061717e-2L,	/*  0x1664f4882c10f9f32d6b1a12a25e5.0p-118L */
T11 = -8.86323552990219656883762347736381851e-3L,	/* -0x1226e355e6c23c8f5a5a0f386cb4d.0p-119L */
T13 =  3.59212803657248101358314398220822722e-3L,	/*  0x1d6d3d0e157ddfb403ad3637442c6.0p-121L */
T15 = -1.45583438705131796512568010348874662e-3L;	/* -0x17da36452b75e150c44cc34253b34.0p-122L */
static const double
T17 =  5.9002744094556621e-4,		/*  0x1355824803668e.0p-63 */
T19 = -2.3912911424260516e-4,		/* -0x1f57d7734c8dde.0p-65 */
T21 =  9.6915379535512898e-5,		/*  0x1967e18ad6a6ca.0p-66 */
T23 = -3.9278322983156353e-5,		/* -0x1497d8e6b75729.0p-67 */
T25 =  1.5918887220143869e-5,		/*  0x10b1319998cafa.0p-68 */
T27 = -6.4514295231630956e-6,		/* -0x1b0f2b71b218eb.0p-70 */
T29 =  2.6120754043964365e-6,		/*  0x15e963a3cf3a39.0p-71 */
T31 = -1.0407567231003314e-6,		/* -0x1176041e656869.0p-72 */
T33 =  3.4744117554063574e-7;		/*  0x1750fe732cab9c.0p-74 */
#endif /* LDBL_MANT_DIG == 64 */

static inline long double
divl(long double a, long double b, long double c, long double d,
    long double e, long double f)
{
	long double inv, r;
	float fr, fw;

	_2sumF(a, c);
	b = b + c;
	_2sumF(d, f);
	e = e + f;

	inv = 1 / (d + e);

	r = (a + b) * inv;
	fr = r;
	r = fr;

	fw = d + e;
	e = d - fw + e;
	d = fw;

	r = r + (a - d * r + b - e * r) * inv;

	return r;
}

long double
tanhl(long double x)
{
	long double hi,lo,s,x2,x4,z;
#if LDBL_MANT_DIG == 113
	double dx2;
#endif
	int16_t jx,ix;

	GET_LDBL_EXPSIGN(jx,x);
	ix = jx&0x7fff;

    /* x is INF or NaN */
	if(ix>=0x7fff) {
	    if (jx>=0) return one/x+one;    /* tanh(+-inf)=+-1 */
	    else       return one/x-one;    /* tanh(NaN) = NaN */
	}

	ENTERI();

    /* |x| < 40 */
	if (ix < 0x4004 || fabsl(x) < 40) {	/* |x|<40 */
	    if (__predict_false(ix<BIAS-(LDBL_MANT_DIG+1)/2)) {	/* |x|<TINY */
		/* tanh(+-0) = +0; tanh(tiny) = tiny(-+) with inexact: */
		return (x == 0 ? x : (0x1p200 * x - x) * 0x1p-200);
	    }
	    if (ix<0x3ffd) {		/* |x|<0.25 */
		x2 = x*x;
#if LDBL_MANT_DIG == 64
		x4 = x2*x2;
		RETURNI(((T19*x2 + T17)*x4 + (T15*x2 + T13))*(x2*x*x2*x4*x4) +
		    ((T11*x2 + T9)*x4 + (T7*x2 + T5))*(x2*x*x2) +
		    T3*(x2*x) + x);
#elif LDBL_MANT_DIG == 113
		dx2 = x2;
#if 0
		RETURNI(((((((((((((((T33*dx2 + T31)*dx2 + T29)*dx2 + T27)*dx2 +
		    T25)*x2 + T23)*x2 + T21)*x2 + T19)*x2 + T17)*x2 +
		    T15)*x2 + T13)*x2 + T11)*x2 + T9)*x2 + T7)*x2 + T5)*
		    (x2*x*x2) +
		    T3*(x2*x) + x);
#else
		long double q = ((((((((((((((T33*dx2 + T31)*dx2 + T29)*dx2 + T27)*dx2 +
		    T25)*x2 + T23)*x2 + T21)*x2 + T19)*x2 + T17)*x2 +
		    T15)*x2 + T13)*x2 + T11)*x2 + T9)*x2 + T7)*x2 + T5)*
		    (x2*x*x2);
		RETURNI(q + T3*(x2*x) + x);
#endif
#endif
	    }
	    k_hexpl(2*fabsl(x), &hi, &lo);
	    if (ix<0x4001 && fabsl(x) < 1.5)	/* |x|<1.5 */
		z = divl(hi, lo, -0.5, hi, lo, 0.5);
	    else
		z = one - one/(lo+0.5+hi);
    /* |x| >= 40, return +-1 */
	} else {
	    z = one - tiny;		/* raise inexact flag */
	}
	s = 1;
	if (jx<0) s = -1;
	RETURNI(s*z);
}
