/* from: FreeBSD: head/lib/msun/src/e_coshl.c XXX */

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
 * See e_cosh.c for complete comments.
 *
 * Converted to long double by Bruce D. Evans.
 */

#include <float.h>
#ifdef __i386__
#include <ieeefp.h>
#endif

#include "fpmath.h"
#include "math.h"
#include "math_private.h"
#include "k_expl.h"

#if LDBL_MAX_EXP != 0x4000
/* We also require the usual expsign encoding. */
#error "Unsupported long double format"
#endif

#define	BIAS	(LDBL_MAX_EXP - 1)

static const volatile long double huge = 0x1p10000L, tiny = 0x1p-10000L;
#if LDBL_MANT_DIG == 64
/*
 * Domain [-1, 1], range ~[-1.8211e-21, 1.8211e-21]:
 * |cosh(x) - c(x)| < 2**-68.8
 */
static const union IEEEl2bits
C4u = LD80C(0xaaaaaaaaaaaaac78, -5,  4.16666666666666682297e-2L);
#define	C4	C4u.e
static const double
C2  =  0.5,
C6  =  1.3888888888888616e-3,		/*  0x16c16c16c16b99.0p-62 */
C8  =  2.4801587301767953e-5,		/*  0x1a01a01a027061.0p-68 */
C10 =  2.7557319163300398e-7,		/*  0x127e4fb6c9b55f.0p-74 */
C12 =  2.0876768371393075e-9,		/*  0x11eed99406a3f4.0p-81 */
C14 =  1.1469537039374480e-11,		/*  0x1938c67cd18c48.0p-89 */
C16 =  4.8473490896852041e-14;		/*  0x1b49c429701e45.0p-97 */
#elif LDBL_MANT_DIG == 113
/*
 * Domain [-1, 1], range ~[-2.3194e-37, 2.3194e-37]:
 * |cosh(x) - c(x)| < 2**-121.69
 */
static const long double
C4  =  4.16666666666666666666666666666666225e-2L,	/*  0x1555555555555555555555555554e.0p-117L */
C6  =  1.38888888888888888888888888889434831e-3L,	/*  0x16c16c16c16c16c16c16c16c1dd7a.0p-122L */
C8  =  2.48015873015873015873015871870962089e-5L,	/*  0x1a01a01a01a01a01a01a017af2756.0p-128L */
C10 =  2.75573192239858906525574318600800201e-7L,	/*  0x127e4fb7789f5c72ef01c8a040640.0p-134L */
C12 =  2.08767569878680989791444691755468269e-9L,	/*  0x11eed8eff8d897b543d0679607399.0p-141L */
C14=  1.14707455977297247387801189650495351e-11L,	/*  0x193974a8c07c9d24ae169a7fa9b54.0p-149L */
C16 =  4.77947733238737883626416876486279985e-14L;	/*  0x1ae7f3e733b814d4e1b90f5727fe4.0p-157L */
static const double
C2  =  0.5,
C18 =  1.5619206968597871e-16,		/*  0x16827863b9900b.0p-105 */
C20 =  4.1103176218528049e-19,		/*  0x1e542ba3d3c269.0p-114 */
C22 =  8.8967926401641701e-22,		/*  0x10ce399542a014.0p-122 */
C24 =  1.6116681626523904e-24,		/*  0x1f2c981d1f0cb7.0p-132 */
C26 =  2.5022374732804632e-27;		/*  0x18c7ecf8b2c4a0.0p-141 */
#else
#error "Unsupported long double format"
#endif /* LDBL_MANT_DIG == 64 */

/* log(2**16385 - 0.5) rounded up: */
static const float
o_threshold =  1.13572168e4;		/*  0xb174de.0p-10 */

long double
coshl(long double x)
{
	long double hi,lo,x2,x4;
#if LDBL_MANT_DIG == 113
	double dx2;
#endif
	uint16_t ix;

	GET_LDBL_EXPSIGN(ix,x);
	ix &= 0x7fff;

    /* x is INF or NaN */
	if(ix>=0x7fff) return x*x;

	ENTERI();

    /* |x| < 1, return 1 or c(x) */
	if(ix<0x3fff) {
	    if (ix<BIAS-(LDBL_MANT_DIG+1)/2) 	/* |x| < TINY */
		RETURNI(1+tiny);	/* cosh(tiny) = 1(+) with inexact */
	    x2 = x*x;
#if LDBL_MANT_DIG == 64
	    x4 = x2*x2;
	    RETURNI(((C16*x2 + C14)*x4 + (C12*x2 + C10))*(x4*x4*x2) +
		((C8*x2 + C6)*x2 + C4)*x4 + C2*x2 + 1);
#elif LDBL_MANT_DIG == 113
	    dx2 = x2;
	    RETURNI((((((((((((C26*dx2 + C24)*dx2 + C22)*dx2 +
		C20)*x2 + C18)*x2 +
		C16)*x2 + C14)*x2 + C12)*x2 + C10)*x2 + C8)*x2 + C6)*x2 +
		C4)*(x2*x2) + C2*x2 + 1);
#endif
	}

    /* |x| in [1, 64), return accurate exp(|x|)/2+1/exp(|x|)/2 */
	if (ix < 0x4005) {
	    k_hexpl(fabsl(x), &hi, &lo);
	    RETURNI(lo + 0.25/(hi + lo) + hi);
	}

    /* |x| in [64, o_threshold], return correctly-overflowing exp(|x|)/2 */
	if (fabsl(x) <= o_threshold)
	    RETURNI(hexpl(fabsl(x)));

    /* |x| > o_threshold, cosh(x) overflow */
	RETURNI(huge*huge);
}
