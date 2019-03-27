/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2011 David Schultz <das@FreeBSD.ORG>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <fenv.h>
#include <float.h>
#include <math.h>

#include "math_private.h"

/*
 * A struct dd represents a floating-point number with twice the precision
 * of a double.  We maintain the invariant that "hi" stores the 53 high-order
 * bits of the result.
 */
struct dd {
	double hi;
	double lo;
};

/*
 * Compute a+b exactly, returning the exact result in a struct dd.  We assume
 * that both a and b are finite, but make no assumptions about their relative
 * magnitudes.
 */
static inline struct dd
dd_add(double a, double b)
{
	struct dd ret;
	double s;

	ret.hi = a + b;
	s = ret.hi - a;
	ret.lo = (a - (ret.hi - s)) + (b - s);
	return (ret);
}

/*
 * Compute a+b, with a small tweak:  The least significant bit of the
 * result is adjusted into a sticky bit summarizing all the bits that
 * were lost to rounding.  This adjustment negates the effects of double
 * rounding when the result is added to another number with a higher
 * exponent.  For an explanation of round and sticky bits, see any reference
 * on FPU design, e.g.,
 *
 *     J. Coonen.  An Implementation Guide to a Proposed Standard for
 *     Floating-Point Arithmetic.  Computer, vol. 13, no. 1, Jan 1980.
 */
static inline double
add_adjusted(double a, double b)
{
	struct dd sum;
	uint64_t hibits, lobits;

	sum = dd_add(a, b);
	if (sum.lo != 0) {
		EXTRACT_WORD64(hibits, sum.hi);
		if ((hibits & 1) == 0) {
			/* hibits += (int)copysign(1.0, sum.hi * sum.lo) */
			EXTRACT_WORD64(lobits, sum.lo);
			hibits += 1 - ((hibits ^ lobits) >> 62);
			INSERT_WORD64(sum.hi, hibits);
		}
	}
	return (sum.hi);
}

/*
 * Compute ldexp(a+b, scale) with a single rounding error. It is assumed
 * that the result will be subnormal, and care is taken to ensure that
 * double rounding does not occur.
 */
static inline double
add_and_denormalize(double a, double b, int scale)
{
	struct dd sum;
	uint64_t hibits, lobits;
	int bits_lost;

	sum = dd_add(a, b);

	/*
	 * If we are losing at least two bits of accuracy to denormalization,
	 * then the first lost bit becomes a round bit, and we adjust the
	 * lowest bit of sum.hi to make it a sticky bit summarizing all the
	 * bits in sum.lo. With the sticky bit adjusted, the hardware will
	 * break any ties in the correct direction.
	 *
	 * If we are losing only one bit to denormalization, however, we must
	 * break the ties manually.
	 */
	if (sum.lo != 0) {
		EXTRACT_WORD64(hibits, sum.hi);
		bits_lost = -((int)(hibits >> 52) & 0x7ff) - scale + 1;
		if ((bits_lost != 1) ^ (int)(hibits & 1)) {
			/* hibits += (int)copysign(1.0, sum.hi * sum.lo) */
			EXTRACT_WORD64(lobits, sum.lo);
			hibits += 1 - (((hibits ^ lobits) >> 62) & 2);
			INSERT_WORD64(sum.hi, hibits);
		}
	}
	return (ldexp(sum.hi, scale));
}

/*
 * Compute a*b exactly, returning the exact result in a struct dd.  We assume
 * that both a and b are normalized, so no underflow or overflow will occur.
 * The current rounding mode must be round-to-nearest.
 */
static inline struct dd
dd_mul(double a, double b)
{
	static const double split = 0x1p27 + 1.0;
	struct dd ret;
	double ha, hb, la, lb, p, q;

	p = a * split;
	ha = a - p;
	ha += p;
	la = a - ha;

	p = b * split;
	hb = b - p;
	hb += p;
	lb = b - hb;

	p = ha * hb;
	q = ha * lb + la * hb;

	ret.hi = p + q;
	ret.lo = p - ret.hi + q + la * lb;
	return (ret);
}

/*
 * Fused multiply-add: Compute x * y + z with a single rounding error.
 *
 * We use scaling to avoid overflow/underflow, along with the
 * canonical precision-doubling technique adapted from:
 *
 *	Dekker, T.  A Floating-Point Technique for Extending the
 *	Available Precision.  Numer. Math. 18, 224-242 (1971).
 *
 * This algorithm is sensitive to the rounding precision.  FPUs such
 * as the i387 must be set in double-precision mode if variables are
 * to be stored in FP registers in order to avoid incorrect results.
 * This is the default on FreeBSD, but not on many other systems.
 *
 * Hardware instructions should be used on architectures that support it,
 * since this implementation will likely be several times slower.
 */
double
fma(double x, double y, double z)
{
	double xs, ys, zs, adj;
	struct dd xy, r;
	int oround;
	int ex, ey, ez;
	int spread;

	/*
	 * Handle special cases. The order of operations and the particular
	 * return values here are crucial in handling special cases involving
	 * infinities, NaNs, overflows, and signed zeroes correctly.
	 */
	if (x == 0.0 || y == 0.0)
		return (x * y + z);
	if (z == 0.0)
		return (x * y);
	if (!isfinite(x) || !isfinite(y))
		return (x * y + z);
	if (!isfinite(z))
		return (z);

	xs = frexp(x, &ex);
	ys = frexp(y, &ey);
	zs = frexp(z, &ez);
	oround = fegetround();
	spread = ex + ey - ez;

	/*
	 * If x * y and z are many orders of magnitude apart, the scaling
	 * will overflow, so we handle these cases specially.  Rounding
	 * modes other than FE_TONEAREST are painful.
	 */
	if (spread < -DBL_MANT_DIG) {
		feraiseexcept(FE_INEXACT);
		if (!isnormal(z))
			feraiseexcept(FE_UNDERFLOW);
		switch (oround) {
		case FE_TONEAREST:
			return (z);
		case FE_TOWARDZERO:
			if (x > 0.0 ^ y < 0.0 ^ z < 0.0)
				return (z);
			else
				return (nextafter(z, 0));
		case FE_DOWNWARD:
			if (x > 0.0 ^ y < 0.0)
				return (z);
			else
				return (nextafter(z, -INFINITY));
		default:	/* FE_UPWARD */
			if (x > 0.0 ^ y < 0.0)
				return (nextafter(z, INFINITY));
			else
				return (z);
		}
	}
	if (spread <= DBL_MANT_DIG * 2)
		zs = ldexp(zs, -spread);
	else
		zs = copysign(DBL_MIN, zs);

	fesetround(FE_TONEAREST);
	/* work around clang bug 8100 */
	volatile double vxs = xs;

	/*
	 * Basic approach for round-to-nearest:
	 *
	 *     (xy.hi, xy.lo) = x * y		(exact)
	 *     (r.hi, r.lo)   = xy.hi + z	(exact)
	 *     adj = xy.lo + r.lo		(inexact; low bit is sticky)
	 *     result = r.hi + adj		(correctly rounded)
	 */
	xy = dd_mul(vxs, ys);
	r = dd_add(xy.hi, zs);

	spread = ex + ey;

	if (r.hi == 0.0) {
		/*
		 * When the addends cancel to 0, ensure that the result has
		 * the correct sign.
		 */
		fesetround(oround);
		volatile double vzs = zs; /* XXX gcc CSE bug workaround */
		return (xy.hi + vzs + ldexp(xy.lo, spread));
	}

	if (oround != FE_TONEAREST) {
		/*
		 * There is no need to worry about double rounding in directed
		 * rounding modes.
		 */
		fesetround(oround);
		/* work around clang bug 8100 */
		volatile double vrlo = r.lo;
		adj = vrlo + xy.lo;
		return (ldexp(r.hi + adj, spread));
	}

	adj = add_adjusted(r.lo, xy.lo);
	if (spread + ilogb(r.hi) > -1023)
		return (ldexp(r.hi + adj, spread));
	else
		return (add_and_denormalize(r.hi, adj, spread));
}

#if (LDBL_MANT_DIG == 53)
__weak_reference(fma, fmal);
#endif
