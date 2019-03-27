/* from: FreeBSD: head/lib/msun/src/e_sinhl.c XXX */

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
 * See e_sinh.c for complete comments.
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

static const long double shuge = 0x1p16383L;
#if LDBL_MANT_DIG == 64
/*
 * Domain [-1, 1], range ~[-6.6749e-22, 6.6749e-22]:
 * |sinh(x)/x - s(x)| < 2**-70.3
 */
static const union IEEEl2bits
S3u = LD80C(0xaaaaaaaaaaaaaaaa, -3,  1.66666666666666666658e-1L);
#define	S3	S3u.e
static const double
S5  =  8.3333333333333332e-3,		/*  0x11111111111111.0p-59 */
S7  =  1.9841269841270074e-4,		/*  0x1a01a01a01a070.0p-65 */
S9  =  2.7557319223873889e-6,		/*  0x171de3a5565fe6.0p-71 */
S11 =  2.5052108406704084e-8,		/*  0x1ae6456857530f.0p-78 */
S13 =  1.6059042748655297e-10,		/*  0x161245fa910697.0p-85 */
S15 =  7.6470006914396920e-13,		/*  0x1ae7ce4eff2792.0p-93 */
S17 =  2.8346142308424267e-15;		/*  0x19882ce789ffc6.0p-101 */
#elif LDBL_MANT_DIG == 113
/*
 * Domain [-1, 1], range ~[-2.9673e-36, 2.9673e-36]:
 * |sinh(x)/x - s(x)| < 2**-118.0
 */
static const long double
S3  =  1.66666666666666666666666666666666033e-1L,	/*  0x1555555555555555555555555553b.0p-115L */
S5  =  8.33333333333333333333333333337643193e-3L,	/*  0x111111111111111111111111180f5.0p-119L */
S7  =  1.98412698412698412698412697391263199e-4L,	/*  0x1a01a01a01a01a01a01a0176aad11.0p-125L */
S9  =  2.75573192239858906525574406205464218e-6L,	/*  0x171de3a556c7338faac243aaa9592.0p-131L */
S11 =  2.50521083854417187749675637460977997e-8L,	/*  0x1ae64567f544e38fe59b3380d7413.0p-138L */
S13 =  1.60590438368216146368737762431552702e-10L,	/*  0x16124613a86d098059c7620850fc2.0p-145L */
S15 =  7.64716373181980539786802470969096440e-13L,	/*  0x1ae7f3e733b814193af09ce723043.0p-153L */
S17 =  2.81145725434775409870584280722701574e-15L;	/*  0x1952c77030c36898c3fd0b6dfc562.0p-161L */
static const double
S19=  8.2206352435411005e-18,		/*  0x12f49b4662b86d.0p-109 */
S21=  1.9572943931418891e-20,		/*  0x171b8f2fab9628.0p-118 */
S23 =  3.8679983530666939e-23,		/*  0x17617002b73afc.0p-127 */
S25 =  6.5067867911512749e-26;		/*  0x1423352626048a.0p-136 */
#else
#error "Unsupported long double format"
#endif /* LDBL_MANT_DIG == 64 */

/* log(2**16385 - 0.5) rounded up: */
static const float
o_threshold =  1.13572168e4;		/*  0xb174de.0p-10 */

long double
sinhl(long double x)
{
	long double hi,lo,x2,x4;
#if LDBL_MANT_DIG == 113
	double dx2;
#endif
	double s;
	int16_t ix,jx;

	GET_LDBL_EXPSIGN(jx,x);
	ix = jx&0x7fff;

    /* x is INF or NaN */
	if(ix>=0x7fff) return x+x;

	ENTERI();

	s = 1;
	if (jx<0) s = -1;

    /* |x| < 64, return x, s(x), or accurate s*(exp(|x|)/2-1/exp(|x|)/2) */
	if (ix<0x4005) {		/* |x|<64 */
	    if (ix<BIAS-(LDBL_MANT_DIG+1)/2) 	/* |x|<TINY */
		if(shuge+x>1) RETURNI(x);  /* sinh(tiny) = tiny with inexact */
	    if (ix<0x3fff) {		/* |x|<1 */
		x2 = x*x;
#if LDBL_MANT_DIG == 64
		x4 = x2*x2;
		RETURNI(((S17*x2 + S15)*x4 + (S13*x2 + S11))*(x2*x*x4*x4) +
		    ((S9*x2 + S7)*x2 + S5)*(x2*x*x2) + S3*(x2*x) + x);
#elif LDBL_MANT_DIG == 113
		dx2 = x2;
		RETURNI(((((((((((S25*dx2 + S23)*dx2 +
		    S21)*x2 + S19)*x2 +
		    S17)*x2 + S15)*x2 + S13)*x2 + S11)*x2 + S9)*x2 + S7)*x2 +
		    S5)* (x2*x*x2) +
		    S3*(x2*x) + x);
#endif
	    }
	    k_hexpl(fabsl(x), &hi, &lo);
	    RETURNI(s*(lo - 0.25/(hi + lo) + hi));
	}

    /* |x| in [64, o_threshold], return correctly-overflowing s*exp(|x|)/2 */
	if (fabsl(x) <= o_threshold)
	    RETURNI(s*hexpl(fabsl(x)));

    /* |x| > o_threshold, sinh(x) overflow */
	return x*shuge;
}
