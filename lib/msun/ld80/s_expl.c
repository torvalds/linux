/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2013 Steven G. Kargl
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Optimized by Bruce D. Evans.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/**
 * Compute the exponential of x for Intel 80-bit format.  This is based on:
 *
 *   PTP Tang, "Table-driven implementation of the exponential function
 *   in IEEE floating-point arithmetic," ACM Trans. Math. Soft., 15,
 *   144-157 (1989).
 *
 * where the 32 table entries have been expanded to INTERVALS (see below).
 */

#include <float.h>

#ifdef __i386__
#include <ieeefp.h>
#endif

#include "fpmath.h"
#include "math.h"
#include "math_private.h"
#include "k_expl.h"

/* XXX Prevent compilers from erroneously constant folding these: */
static const volatile long double
huge = 0x1p10000L,
tiny = 0x1p-10000L;

static const long double
twom10000 = 0x1p-10000L;

static const union IEEEl2bits
/* log(2**16384 - 0.5) rounded towards zero: */
/* log(2**16384 - 0.5 + 1) rounded towards zero for expm1l() is the same: */
o_thresholdu = LD80C(0xb17217f7d1cf79ab, 13,  11356.5234062941439488L),
#define o_threshold	 (o_thresholdu.e)
/* log(2**(-16381-64-1)) rounded towards zero: */
u_thresholdu = LD80C(0xb21dfe7f09e2baa9, 13, -11399.4985314888605581L);
#define u_threshold	 (u_thresholdu.e)

long double
expl(long double x)
{
	union IEEEl2bits u;
	long double hi, lo, t, twopk;
	int k;
	uint16_t hx, ix;

	DOPRINT_START(&x);

	/* Filter out exceptional cases. */
	u.e = x;
	hx = u.xbits.expsign;
	ix = hx & 0x7fff;
	if (ix >= BIAS + 13) {		/* |x| >= 8192 or x is NaN */
		if (ix == BIAS + LDBL_MAX_EXP) {
			if (hx & 0x8000)  /* x is -Inf, -NaN or unsupported */
				RETURNP(-1 / x);
			RETURNP(x + x);	/* x is +Inf, +NaN or unsupported */
		}
		if (x > o_threshold)
			RETURNP(huge * huge);
		if (x < u_threshold)
			RETURNP(tiny * tiny);
	} else if (ix < BIAS - 75) {	/* |x| < 0x1p-75 (includes pseudos) */
		RETURN2P(1, x);		/* 1 with inexact iff x != 0 */
	}

	ENTERI();

	twopk = 1;
	__k_expl(x, &hi, &lo, &k);
	t = SUM2P(hi, lo);

	/* Scale by 2**k. */
	if (k >= LDBL_MIN_EXP) {
		if (k == LDBL_MAX_EXP)
			RETURNI(t * 2 * 0x1p16383L);
		SET_LDBL_EXPSIGN(twopk, BIAS + k);
		RETURNI(t * twopk);
	} else {
		SET_LDBL_EXPSIGN(twopk, BIAS + k + 10000);
		RETURNI(t * twopk * twom10000);
	}
}

/**
 * Compute expm1l(x) for Intel 80-bit format.  This is based on:
 *
 *   PTP Tang, "Table-driven implementation of the Expm1 function
 *   in IEEE floating-point arithmetic," ACM Trans. Math. Soft., 18,
 *   211-222 (1992).
 */

/*
 * Our T1 and T2 are chosen to be approximately the points where method
 * A and method B have the same accuracy.  Tang's T1 and T2 are the
 * points where method A's accuracy changes by a full bit.  For Tang,
 * this drop in accuracy makes method A immediately less accurate than
 * method B, but our larger INTERVALS makes method A 2 bits more
 * accurate so it remains the most accurate method significantly
 * closer to the origin despite losing the full bit in our extended
 * range for it.
 */
static const double
T1 = -0.1659,				/* ~-30.625/128 * log(2) */
T2 =  0.1659;				/* ~30.625/128 * log(2) */

/*
 * Domain [-0.1659, 0.1659], range ~[-2.6155e-22, 2.5507e-23]:
 * |(exp(x)-1-x-x**2/2)/x - p(x)| < 2**-71.6
 *
 * XXX the coeffs aren't very carefully rounded, and I get 2.8 more bits,
 * but unlike for ld128 we can't drop any terms.
 */
static const union IEEEl2bits
B3 = LD80C(0xaaaaaaaaaaaaaaab, -3,  1.66666666666666666671e-1L),
B4 = LD80C(0xaaaaaaaaaaaaaaac, -5,  4.16666666666666666712e-2L);

static const double
B5  =  8.3333333333333245e-3,		/*  0x1.111111111110cp-7 */
B6  =  1.3888888888888861e-3,		/*  0x1.6c16c16c16c0ap-10 */
B7  =  1.9841269841532042e-4,		/*  0x1.a01a01a0319f9p-13 */
B8  =  2.4801587302069236e-5,		/*  0x1.a01a01a03cbbcp-16 */
B9  =  2.7557316558468562e-6,		/*  0x1.71de37fd33d67p-19 */
B10 =  2.7557315829785151e-7,		/*  0x1.27e4f91418144p-22 */
B11 =  2.5063168199779829e-8,		/*  0x1.ae94fabdc6b27p-26 */
B12 =  2.0887164654459567e-9;		/*  0x1.1f122d6413fe1p-29 */

long double
expm1l(long double x)
{
	union IEEEl2bits u, v;
	long double fn, hx2_hi, hx2_lo, q, r, r1, r2, t, twomk, twopk, x_hi;
	long double x_lo, x2, z;
	long double x4;
	int k, n, n2;
	uint16_t hx, ix;

	DOPRINT_START(&x);

	/* Filter out exceptional cases. */
	u.e = x;
	hx = u.xbits.expsign;
	ix = hx & 0x7fff;
	if (ix >= BIAS + 6) {		/* |x| >= 64 or x is NaN */
		if (ix == BIAS + LDBL_MAX_EXP) {
			if (hx & 0x8000)  /* x is -Inf, -NaN or unsupported */
				RETURNP(-1 / x - 1);
			RETURNP(x + x);	/* x is +Inf, +NaN or unsupported */
		}
		if (x > o_threshold)
			RETURNP(huge * huge);
		/*
		 * expm1l() never underflows, but it must avoid
		 * unrepresentable large negative exponents.  We used a
		 * much smaller threshold for large |x| above than in
		 * expl() so as to handle not so large negative exponents
		 * in the same way as large ones here.
		 */
		if (hx & 0x8000)	/* x <= -64 */
			RETURN2P(tiny, -1);	/* good for x < -65ln2 - eps */
	}

	ENTERI();

	if (T1 < x && x < T2) {
		if (ix < BIAS - 74) {	/* |x| < 0x1p-74 (includes pseudos) */
			/* x (rounded) with inexact if x != 0: */
			RETURNPI(x == 0 ? x :
			    (0x1p100 * x + fabsl(x)) * 0x1p-100);
		}

		x2 = x * x;
		x4 = x2 * x2;
		q = x4 * (x2 * (x4 *
		    /*
		     * XXX the number of terms is no longer good for
		     * pairwise grouping of all except B3, and the
		     * grouping is no longer from highest down.
		     */
		    (x2 *            B12  + (x * B11 + B10)) +
		    (x2 * (x * B9 +  B8) +  (x * B7 +  B6))) +
			  (x * B5 +  B4.e)) + x2 * x * B3.e;

		x_hi = (float)x;
		x_lo = x - x_hi;
		hx2_hi = x_hi * x_hi / 2;
		hx2_lo = x_lo * (x + x_hi) / 2;
		if (ix >= BIAS - 7)
			RETURN2PI(hx2_hi + x_hi, hx2_lo + x_lo + q);
		else
			RETURN2PI(x, hx2_lo + q + hx2_hi);
	}

	/* Reduce x to (k*ln2 + endpoint[n2] + r1 + r2). */
	fn = rnintl(x * INV_L);
	n = irint(fn);
	n2 = (unsigned)n % INTERVALS;
	k = n >> LOG2_INTERVALS;
	r1 = x - fn * L1;
	r2 = fn * -L2;
	r = r1 + r2;

	/* Prepare scale factor. */
	v.e = 1;
	v.xbits.expsign = BIAS + k;
	twopk = v.e;

	/*
	 * Evaluate lower terms of
	 * expl(endpoint[n2] + r1 + r2) = tbl[n2] * expl(r1 + r2).
	 */
	z = r * r;
	q = r2 + z * (A2 + r * A3) + z * z * (A4 + r * A5) + z * z * z * A6;

	t = (long double)tbl[n2].lo + tbl[n2].hi;

	if (k == 0) {
		t = SUM2P(tbl[n2].hi - 1, tbl[n2].lo * (r1 + 1) + t * q +
		    tbl[n2].hi * r1);
		RETURNI(t);
	}
	if (k == -1) {
		t = SUM2P(tbl[n2].hi - 2, tbl[n2].lo * (r1 + 1) + t * q +
		    tbl[n2].hi * r1);
		RETURNI(t / 2);
	}
	if (k < -7) {
		t = SUM2P(tbl[n2].hi, tbl[n2].lo + t * (q + r1));
		RETURNI(t * twopk - 1);
	}
	if (k > 2 * LDBL_MANT_DIG - 1) {
		t = SUM2P(tbl[n2].hi, tbl[n2].lo + t * (q + r1));
		if (k == LDBL_MAX_EXP)
			RETURNI(t * 2 * 0x1p16383L - 1);
		RETURNI(t * twopk - 1);
	}

	v.xbits.expsign = BIAS - k;
	twomk = v.e;

	if (k > LDBL_MANT_DIG - 1)
		t = SUM2P(tbl[n2].hi, tbl[n2].lo - twomk + t * (q + r1));
	else
		t = SUM2P(tbl[n2].hi - twomk, tbl[n2].lo + t * (q + r1));
	RETURNI(t * twopk);
}
