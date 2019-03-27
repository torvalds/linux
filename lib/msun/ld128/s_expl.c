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

/*
 * ld128 version of s_expl.c.  See ../ld80/s_expl.c for most comments.
 */

#include <float.h>

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

static const long double
/* log(2**16384 - 0.5) rounded towards zero: */
/* log(2**16384 - 0.5 + 1) rounded towards zero for expm1l() is the same: */
o_threshold =  11356.523406294143949491931077970763428L,
/* log(2**(-16381-64-1)) rounded towards zero: */
u_threshold = -11433.462743336297878837243843452621503L;

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
			if (hx & 0x8000)  /* x is -Inf or -NaN */
				RETURNP(-1 / x);
			RETURNP(x + x);	/* x is +Inf or +NaN */
		}
		if (x > o_threshold)
			RETURNP(huge * huge);
		if (x < u_threshold)
			RETURNP(tiny * tiny);
	} else if (ix < BIAS - 114) {	/* |x| < 0x1p-114 */
		RETURN2P(1, x);		/* 1 with inexact iff x != 0 */
	}

	ENTERI();

	twopk = 1;
	__k_expl(x, &hi, &lo, &k);
	t = SUM2P(hi, lo);

	/* Scale by 2**k. */
	/* XXX sparc64 multiplication is so slow that scalbnl() is faster. */
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

/*
 * Our T1 and T2 are chosen to be approximately the points where method
 * A and method B have the same accuracy.  Tang's T1 and T2 are the
 * points where method A's accuracy changes by a full bit.  For Tang,
 * this drop in accuracy makes method A immediately less accurate than
 * method B, but our larger INTERVALS makes method A 2 bits more
 * accurate so it remains the most accurate method significantly
 * closer to the origin despite losing the full bit in our extended
 * range for it.
 *
 * Split the interval [T1, T2] into two intervals [T1, T3] and [T3, T2].
 * Setting T3 to 0 would require the |x| < 0x1p-113 condition to appear
 * in both subintervals, so set T3 = 2**-5, which places the condition
 * into the [T1, T3] interval.
 *
 * XXX we now do this more to (partially) balance the number of terms
 * in the C and D polys than to avoid checking the condition in both
 * intervals.
 *
 * XXX these micro-optimizations are excessive.
 */
static const double
T1 = -0.1659,				/* ~-30.625/128 * log(2) */
T2 =  0.1659,				/* ~30.625/128 * log(2) */
T3 =  0.03125;

/*
 * Domain [-0.1659, 0.03125], range ~[2.9134e-44, 1.8404e-37]:
 * |(exp(x)-1-x-x**2/2)/x - p(x)| < 2**-122.03
 *
 * XXX none of the long double C or D coeffs except C10 is correctly printed.
 * If you re-print their values in %.35Le format, the result is always
 * different.  For example, the last 2 digits in C3 should be 59, not 67.
 * 67 is apparently from rounding an extra-precision value to 36 decimal
 * places.
 */
static const long double
C3  =  1.66666666666666666666666666666666667e-1L,
C4  =  4.16666666666666666666666666666666645e-2L,
C5  =  8.33333333333333333333333333333371638e-3L,
C6  =  1.38888888888888888888888888891188658e-3L,
C7  =  1.98412698412698412698412697235950394e-4L,
C8  =  2.48015873015873015873015112487849040e-5L,
C9  =  2.75573192239858906525606685484412005e-6L,
C10 =  2.75573192239858906612966093057020362e-7L,
C11 =  2.50521083854417203619031960151253944e-8L,
C12 =  2.08767569878679576457272282566520649e-9L,
C13 =  1.60590438367252471783548748824255707e-10L;

/*
 * XXX this has 1 more coeff than needed.
 * XXX can start the double coeffs but not the double mults at C10.
 * With my coeffs (C10-C17 double; s = best_s):
 * Domain [-0.1659, 0.03125], range ~[-1.1976e-37, 1.1976e-37]:
 * |(exp(x)-1-x-x**2/2)/x - p(x)| ~< 2**-122.65
 */
static const double
C14 =  1.1470745580491932e-11,		/*  0x1.93974a81dae30p-37 */
C15 =  7.6471620181090468e-13,		/*  0x1.ae7f3820adab1p-41 */
C16 =  4.7793721460260450e-14,		/*  0x1.ae7cd18a18eacp-45 */
C17 =  2.8074757356658877e-15,		/*  0x1.949992a1937d9p-49 */
C18 =  1.4760610323699476e-16;		/*  0x1.545b43aabfbcdp-53 */

/*
 * Domain [0.03125, 0.1659], range ~[-2.7676e-37, -1.0367e-38]:
 * |(exp(x)-1-x-x**2/2)/x - p(x)| < 2**-121.44
 */
static const long double
D3  =  1.66666666666666666666666666666682245e-1L,
D4  =  4.16666666666666666666666666634228324e-2L,
D5  =  8.33333333333333333333333364022244481e-3L,
D6  =  1.38888888888888888888887138722762072e-3L,
D7  =  1.98412698412698412699085805424661471e-4L,
D8  =  2.48015873015873015687993712101479612e-5L,
D9  =  2.75573192239858944101036288338208042e-6L,
D10 =  2.75573192239853161148064676533754048e-7L,
D11 =  2.50521083855084570046480450935267433e-8L,
D12 =  2.08767569819738524488686318024854942e-9L,
D13 =  1.60590442297008495301927448122499313e-10L;

/*
 * XXX this has 1 more coeff than needed.
 * XXX can start the double coeffs but not the double mults at D11.
 * With my coeffs (D11-D16 double):
 * Domain [0.03125, 0.1659], range ~[-1.1980e-37, 1.1980e-37]:
 * |(exp(x)-1-x-x**2/2)/x - p(x)| ~< 2**-122.65
 */
static const double
D14 =  1.1470726176204336e-11,		/*  0x1.93971dc395d9ep-37 */
D15 =  7.6478532249581686e-13,		/*  0x1.ae892e3D16fcep-41 */
D16 =  4.7628892832607741e-14,		/*  0x1.ad00Dfe41feccp-45 */
D17 =  3.0524857220358650e-15;		/*  0x1.D7e8d886Df921p-49 */

long double
expm1l(long double x)
{
	union IEEEl2bits u, v;
	long double hx2_hi, hx2_lo, q, r, r1, t, twomk, twopk, x_hi;
	long double x_lo, x2;
	double dr, dx, fn, r2;
	int k, n, n2;
	uint16_t hx, ix;

	DOPRINT_START(&x);

	/* Filter out exceptional cases. */
	u.e = x;
	hx = u.xbits.expsign;
	ix = hx & 0x7fff;
	if (ix >= BIAS + 7) {		/* |x| >= 128 or x is NaN */
		if (ix == BIAS + LDBL_MAX_EXP) {
			if (hx & 0x8000)  /* x is -Inf or -NaN */
				RETURNP(-1 / x - 1);
			RETURNP(x + x);	/* x is +Inf or +NaN */
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
		if (hx & 0x8000)	/* x <= -128 */
			RETURN2P(tiny, -1);	/* good for x < -114ln2 - eps */
	}

	ENTERI();

	if (T1 < x && x < T2) {
		x2 = x * x;
		dx = x;

		if (x < T3) {
			if (ix < BIAS - 113) {	/* |x| < 0x1p-113 */
				/* x (rounded) with inexact if x != 0: */
				RETURNPI(x == 0 ? x :
				    (0x1p200 * x + fabsl(x)) * 0x1p-200);
			}
			q = x * x2 * C3 + x2 * x2 * (C4 + x * (C5 + x * (C6 +
			    x * (C7 + x * (C8 + x * (C9 + x * (C10 +
			    x * (C11 + x * (C12 + x * (C13 +
			    dx * (C14 + dx * (C15 + dx * (C16 +
			    dx * (C17 + dx * C18))))))))))))));
		} else {
			q = x * x2 * D3 + x2 * x2 * (D4 + x * (D5 + x * (D6 +
			    x * (D7 + x * (D8 + x * (D9 + x * (D10 +
			    x * (D11 + x * (D12 + x * (D13 +
			    dx * (D14 + dx * (D15 + dx * (D16 +
			    dx * D17)))))))))))));
		}

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
	fn = rnint((double)x * INV_L);
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
	dr = r;
	q = r2 + r * r * (A2 + r * (A3 + r * (A4 + r * (A5 + r * (A6 +
	    dr * (A7 + dr * (A8 + dr * (A9 + dr * A10))))))));

	t = tbl[n2].lo + tbl[n2].hi;

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
