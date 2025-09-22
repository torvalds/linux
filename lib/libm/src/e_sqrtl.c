/*	$OpenBSD: e_sqrtl.c,v 1.3 2016/09/12 19:47:02 guenther Exp $	*/
/*-
 * Copyright (c) 2007 Steven G. Kargl
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
 */

#include <sys/types.h>
#include <machine/ieee.h>	/* for struct ieee_ext */
#include <fenv.h>
#include <float.h>
#include <math.h>

#ifdef EXT_IMPLICIT_NBIT
#define	LDBL_NBIT	0
#else /* EXT_IMPLICIT_NBIT */
#define	LDBL_NBIT	0x80000000
#endif /* EXT_IMPLICIT_NBIT */

/* Return (x + ulp) for normal positive x. Assumes no overflow. */
static inline long double
inc(long double x)
{
	struct ieee_ext *p = (struct ieee_ext *)&x;

#ifdef EXT_FRACHMBITS
	uint64_t frach;

	frach = ((uint64_t)p->ext_frach << EXT_FRACHMBITS) | p->ext_frachm;
	frach++;
	p->ext_frach = frach >> EXT_FRACHMBITS;
	p->ext_frachm = frach & 0x00000000ffffffff;
#else /* EXT_FRACHMBITS */
	uint32_t frach;

	p->ext_frach++;
	frach = p->ext_frach;
#endif /* EXT_FRACHMBITS */

	if (frach == 0) {

#ifdef EXT_FRACLMBITS
		uint64_t fracl;

		fracl = ((uint64_t)p->ext_fraclm << EXT_FRACLBITS) |
			p->ext_fracl;
		fracl++;
		p->ext_fraclm = fracl >> EXT_FRACLBITS;
		p->ext_fracl = fracl & 0x00000000ffffffff;
#else /* EXT_FRACLMBITS */
		uint32_t fracl;

		p->ext_fracl++;
		fracl = p->ext_fracl;
#endif /* EXT_FRACLMBITS */

		if (fracl == 0) {
			p->ext_exp++;
			p->ext_frach |= LDBL_NBIT;
		}
	}

	return x;
}

/* Return (x - ulp) for normal positive x. Assumes no underflow. */
static inline long double
dec(long double x)
{
	struct ieee_ext *p = (struct ieee_ext *)&x;

#ifdef EXT_FRACLMBITS
	uint64_t fracl;

	fracl = ((uint64_t)p->ext_fraclm << EXT_FRACLBITS) | p->ext_fracl;
	fracl--;
	p->ext_fraclm = fracl >> EXT_FRACLBITS;
	p->ext_fracl = fracl & 0x00000000ffffffff;
#else /* EXT_FRACLMBITS */
	uint32_t fracl;

	p->ext_fracl--;
	fracl = p->ext_fracl;
#endif /* EXT_FRACLMBITS */

	if (fracl == 0) {

#ifdef EXT_FRACHMBITS
		uint64_t frach;

		frach = ((uint64_t)p->ext_frach << EXT_FRACHMBITS) |
			p->ext_frachm;
		frach--;
		p->ext_frach = frach >> EXT_FRACHMBITS;
		p->ext_frachm = frach & 0x00000000ffffffff;
#else /* EXT_FRACHMBITS */
		uint32_t frach;

		p->ext_frach--;
		frach = p->ext_frach;
#endif /* EXT_FRACHMBITS */

		if (frach == LDBL_NBIT) {
			p->ext_exp--;
			p->ext_frach |= LDBL_NBIT;
		}
	}

	return x;
}

/*
 * This is slow, but simple and portable. You should use hardware sqrt
 * if possible.
 */

long double
sqrtl(long double x)
{
	union {
		long double e;
		struct ieee_ext bits;
	} u;
	int k, r;
	long double lo, xn;

	u.e = x;

	/* If x = NaN, then sqrt(x) = NaN. */
	/* If x = Inf, then sqrt(x) = Inf. */
	/* If x = -Inf, then sqrt(x) = NaN. */
	if (u.bits.ext_exp == LDBL_MAX_EXP * 2 - 1)
		return (x * x + x);

	/* If x = +-0, then sqrt(x) = +-0. */
	if ((u.bits.ext_frach
#ifdef EXT_FRACHMBITS
		| u.bits.ext_frachm
#endif /* EXT_FRACHMBITS */
#ifdef EXT_FRACLMBITS
		| u.bits.ext_fraclm
#endif /* EXT_FRACLMBITS */
		| u.bits.ext_fracl | u.bits.ext_exp) == 0)
		return (x);

	/* If x < 0, then raise invalid and return NaN */
	if (u.bits.ext_sign)
		return ((x - x) / (x - x));

	if (u.bits.ext_exp == 0) {
		/* Adjust subnormal numbers. */
		u.e *= 0x1.0p514;
		k = -514;
	} else {
		k = 0;
	}
	/*
	 * u.e is a normal number, so break it into u.e = e*2^n where
	 * u.e = (2*e)*2^2k for odd n and u.e = (4*e)*2^2k for even n.
	 */
	if ((u.bits.ext_exp - 0x3ffe) & 1) {	/* n is odd.     */
		k += u.bits.ext_exp - 0x3fff;	/* 2k = n - 1.   */
		u.bits.ext_exp = 0x3fff;	/* u.e in [1,2). */
	} else {
		k += u.bits.ext_exp - 0x4000;	/* 2k = n - 2.   */
		u.bits.ext_exp = 0x4000;	/* u.e in [2,4). */
	}

	/*
	 * Newton's iteration.
	 * Split u.e into a high and low part to achieve additional precision.
	 */
	xn = sqrt(u.e);			/* 53-bit estimate of sqrtl(x). */
#if LDBL_MANT_DIG > 100
	xn = (xn + (u.e / xn)) * 0.5;	/* 106-bit estimate. */
#endif
	lo = u.e;
	u.bits.ext_fracl = 0;		/* Zero out lower bits. */
#ifdef EXT_FRACLMBITS
	u.bits.ext_fraclm = 0;
#endif /* EXT_FRACLMBITS */
	lo = (lo - u.e) / xn;		/* Low bits divided by xn. */
	xn = xn + (u.e / xn);		/* High portion of estimate. */
	u.e = xn + lo;			/* Combine everything. */
	u.bits.ext_exp += (k >> 1) - 1;

	feclearexcept(FE_INEXACT);
	r = fegetround();
	fesetround(FE_TOWARDZERO);	/* Set to round-toward-zero. */
	xn = x / u.e;			/* Chopped quotient (inexact?). */

	if (!fetestexcept(FE_INEXACT)) { /* Quotient is exact. */
		if (xn == u.e) {
			fesetround(r);
			return (u.e);
		}
		/* Round correctly for inputs like x = y**2 - ulp. */
		xn = dec(xn);		/* xn = xn - ulp. */
	}

	if (r == FE_TONEAREST) {
		xn = inc(xn);		/* xn = xn + ulp. */
	} else if (r == FE_UPWARD) {
		u.e = inc(u.e);		/* u.e = u.e + ulp. */
		xn = inc(xn);		/* xn  = xn + ulp. */
	}
	u.e = u.e + xn;			/* Chopped sum. */
	fesetround(r);			/* Restore env and raise inexact */
	u.bits.ext_exp--;
	return (u.e);
}
DEF_STD(sqrtl);
