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

#include "fpmath.h"
#include "math.h"
#include "math_private.h"

#define	MANT_DIG	FLT_MANT_DIG
#define	MAX_EXP		FLT_MAX_EXP
#define	MIN_EXP		FLT_MIN_EXP

static const float
ln2f_hi =  6.9314575195e-1,		/*  0xb17200.0p-24 */
ln2f_lo =  1.4286067653e-6;		/*  0xbfbe8e.0p-43 */

float complex
clogf(float complex z)
{
	float_t ax, ax2h, ax2l, axh, axl, ay, ay2h, ay2l, ayh, ayl, sh, sl, t;
	float x, y, v;
	uint32_t hax, hay;
	int kx, ky;

	x = crealf(z);
	y = cimagf(z);
	v = atan2f(y, x);

	ax = fabsf(x);
	ay = fabsf(y);
	if (ax < ay) {
		t = ax;
		ax = ay;
		ay = t;
	}

	GET_FLOAT_WORD(hax, ax);
	kx = (hax >> 23) - 127;
	GET_FLOAT_WORD(hay, ay);
	ky = (hay >> 23) - 127;

	/* Handle NaNs and Infs using the general formula. */
	if (kx == MAX_EXP || ky == MAX_EXP)
		return (CMPLXF(logf(hypotf(x, y)), v));

	/* Avoid spurious underflow, and reduce inaccuracies when ax is 1. */
	if (hax == 0x3f800000) {
		if (ky < (MIN_EXP - 1) / 2)
			return (CMPLXF((ay / 2) * ay, v));
		return (CMPLXF(log1pf(ay * ay) / 2, v));
	}

	/* Avoid underflow when ax is not small.  Also handle zero args. */
	if (kx - ky > MANT_DIG || hay == 0)
		return (CMPLXF(logf(ax), v));

	/* Avoid overflow. */
	if (kx >= MAX_EXP - 1)
		return (CMPLXF(logf(hypotf(x * 0x1p-126F, y * 0x1p-126F)) +
		    (MAX_EXP - 2) * ln2f_lo + (MAX_EXP - 2) * ln2f_hi, v));
	if (kx >= (MAX_EXP - 1) / 2)
		return (CMPLXF(logf(hypotf(x, y)), v));

	/* Reduce inaccuracies and avoid underflow when ax is denormal. */
	if (kx <= MIN_EXP - 2)
		return (CMPLXF(logf(hypotf(x * 0x1p127F, y * 0x1p127F)) +
		    (MIN_EXP - 2) * ln2f_lo + (MIN_EXP - 2) * ln2f_hi, v));

	/* Avoid remaining underflows (when ax is small but not denormal). */
	if (ky < (MIN_EXP - 1) / 2 + MANT_DIG)
		return (CMPLXF(logf(hypotf(x, y)), v));

	/* Calculate ax*ax and ay*ay exactly using Dekker's algorithm. */
	t = (float)(ax * (0x1p12F + 1));
	axh = (float)(ax - t) + t;
	axl = ax - axh;
	ax2h = ax * ax;
	ax2l = axh * axh - ax2h + 2 * axh * axl + axl * axl;
	t = (float)(ay * (0x1p12F + 1));
	ayh = (float)(ay - t) + t;
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
	if (sh < 0.5F || sh >= 3)
		return (CMPLXF(logf(ay2l + ax2l + sl + sh) / 2, v));
	sh -= 1;
	_2sum(sh, sl);
	_2sum(ax2l, ay2l);
	/* Briggs-Kahan algorithm (except we discard the final low term): */
	_2sum(sh, ax2l);
	_2sum(sl, ay2l);
	t = ax2l + sl;
	_2sumF(sh, t);
	return (CMPLXF(log1pf(ay2l + t + sh) / 2, v));
}
