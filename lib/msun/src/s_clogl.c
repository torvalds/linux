/*-
 * Copyright (c) 2013 Bruce D. Evans
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <complex.h>
#include <float.h>
#ifdef __i386__
#include <ieeefp.h>
#endif

#include "fpmath.h"
#include "math.h"
#include "math_private.h"

#define	MANT_DIG	LDBL_MANT_DIG
#define	MAX_EXP		LDBL_MAX_EXP
#define	MIN_EXP		LDBL_MIN_EXP

static const double
ln2_hi = 6.9314718055829871e-1;		/*  0x162e42fefa0000.0p-53 */

#if LDBL_MANT_DIG == 64
#define	MULT_REDUX	0x1p32		/* exponent MANT_DIG / 2 rounded up */
static const double
ln2l_lo = 1.6465949582897082e-12;	/*  0x1cf79abc9e3b3a.0p-92 */
#elif LDBL_MANT_DIG == 113
#define	MULT_REDUX	0x1p57
static const long double
ln2l_lo = 1.64659495828970812809844307550013433e-12L;	/*  0x1cf79abc9e3b39803f2f6af40f343.0p-152L */
#else
#error "Unsupported long double format"
#endif

long double complex
clogl(long double complex z)
{
	long double ax, ax2h, ax2l, axh, axl, ay, ay2h, ay2l, ayh, ayl;
	long double sh, sl, t;
	long double x, y, v;
	uint16_t hax, hay;
	int kx, ky;

	ENTERIT(long double complex);

	x = creall(z);
	y = cimagl(z);
	v = atan2l(y, x);

	ax = fabsl(x);
	ay = fabsl(y);
	if (ax < ay) {
		t = ax;
		ax = ay;
		ay = t;
	}

	GET_LDBL_EXPSIGN(hax, ax);
	kx = hax - 16383;
	GET_LDBL_EXPSIGN(hay, ay);
	ky = hay - 16383;

	/* Handle NaNs and Infs using the general formula. */
	if (kx == MAX_EXP || ky == MAX_EXP)
		RETURNI(CMPLXL(logl(hypotl(x, y)), v));

	/* Avoid spurious underflow, and reduce inaccuracies when ax is 1. */
	if (ax == 1) {
		if (ky < (MIN_EXP - 1) / 2)
			RETURNI(CMPLXL((ay / 2) * ay, v));
		RETURNI(CMPLXL(log1pl(ay * ay) / 2, v));
	}

	/* Avoid underflow when ax is not small.  Also handle zero args. */
	if (kx - ky > MANT_DIG || ay == 0)
		RETURNI(CMPLXL(logl(ax), v));

	/* Avoid overflow. */
	if (kx >= MAX_EXP - 1)
		RETURNI(CMPLXL(logl(hypotl(x * 0x1p-16382L, y * 0x1p-16382L)) +
		    (MAX_EXP - 2) * ln2l_lo + (MAX_EXP - 2) * ln2_hi, v));
	if (kx >= (MAX_EXP - 1) / 2)
		RETURNI(CMPLXL(logl(hypotl(x, y)), v));

	/* Reduce inaccuracies and avoid underflow when ax is denormal. */
	if (kx <= MIN_EXP - 2)
		RETURNI(CMPLXL(logl(hypotl(x * 0x1p16383L, y * 0x1p16383L)) +
		    (MIN_EXP - 2) * ln2l_lo + (MIN_EXP - 2) * ln2_hi, v));

	/* Avoid remaining underflows (when ax is small but not denormal). */
	if (ky < (MIN_EXP - 1) / 2 + MANT_DIG)
		RETURNI(CMPLXL(logl(hypotl(x, y)), v));

	/* Calculate ax*ax and ay*ay exactly using Dekker's algorithm. */
	t = (long double)(ax * (MULT_REDUX + 1));
	axh = (long double)(ax - t) + t;
	axl = ax - axh;
	ax2h = ax * ax;
	ax2l = axh * axh - ax2h + 2 * axh * axl + axl * axl;
	t = (long double)(ay * (MULT_REDUX + 1));
	ayh = (long double)(ay - t) + t;
	ayl = ay - ayh;
	ay2h = ay * ay;
	ay2l = ayh * ayh - ay2h + 2 * ayh * ayl + ayl * ayl;

	/*
	 * When log(|z|) is far from 1, accuracy in calculating the sum
	 * of the squares is not very important since log() reduces
	 * inaccuracies.  We depended on this to use the general
	 * formula when log(|z|) is very far from 1.  When log(|z|) is
	 * moderately far from 1, we go through the extra-precision
	 * calculations to reduce branches and gain a little accuracy.
	 *
	 * When |z| is near 1, we subtract 1 and use log1p() and don't
	 * leave it to log() to subtract 1, since we gain at least 1 bit
	 * of accuracy in this way.
	 *
	 * When |z| is very near 1, subtracting 1 can cancel almost
	 * 3*MANT_DIG bits.  We arrange that subtracting 1 is exact in
	 * doubled precision, and then do the rest of the calculation
	 * in sloppy doubled precision.  Although large cancellations
	 * often lose lots of accuracy, here the final result is exact
	 * in doubled precision if the large calculation occurs (because
	 * then it is exact in tripled precision and the cancellation
	 * removes enough bits to fit in doubled precision).  Thus the
	 * result is accurate in sloppy doubled precision, and the only
	 * significant loss of accuracy is when it is summed and passed
	 * to log1p().
	 */
	sh = ax2h;
	sl = ay2h;
	_2sumF(sh, sl);
	if (sh < 0.5 || sh >= 3)
		RETURNI(CMPLXL(logl(ay2l + ax2l + sl + sh) / 2, v));
	sh -= 1;
	_2sum(sh, sl);
	_2sum(ax2l, ay2l);
	/* Briggs-Kahan algorithm (except we discard the final low term): */
	_2sum(sh, ax2l);
	_2sum(sl, ay2l);
	t = ax2l + sl;
	_2sumF(sh, t);
	RETURNI(CMPLXL(log1pl(ay2l + t + sh) / 2, v));
}
