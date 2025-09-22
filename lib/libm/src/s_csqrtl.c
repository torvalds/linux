/*	$OpenBSD: s_csqrtl.c,v 1.4 2016/09/12 19:47:02 guenther Exp $	*/

/*
 * Copyright (c) 2008 Stephen L. Moshier <steve@moshier.net>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*							csqrtl()
 *
 *	Complex square root
 *
 *
 *
 * SYNOPSIS:
 *
 * long double complex csqrtl();
 * long double complex z, w;
 *
 * w = csqrtl( z );
 *
 *
 *
 * DESCRIPTION:
 *
 *
 * If z = x + iy,  r = |z|, then
 *
 *                       1/2
 * Re w  =  [ (r + x)/2 ]   ,
 *
 *                       1/2
 * Im w  =  [ (r - x)/2 ]   .
 *
 * Cancellation error in r-x or r+x is avoided by using the
 * identity  2 Re w Im w  =  y.
 *
 * Note that -w is also a square root of z.  The root chosen
 * is always in the right half plane and Im w has the same sign as y.
 *
 *
 *
 * ACCURACY:
 *
 *                      Relative error:
 * arithmetic   domain     # trials      peak         rms
 *    IEEE      -10,+10     500000      1.1e-19     3.0e-20
 *
 */

#include <complex.h>
#include <math.h>

long double complex
csqrtl(long double complex z)
{
	long double complex w;
	long double x, y, r, t, scale;

	x = creall(z);
	y = cimagl(z);

	if (y == 0.0L) {
		if (x < 0.0L) {
			w = 0.0L + copysign(sqrtl(-x), y) * I;
			return (w);
		}
		else {
			w = sqrtl(x) + 0.0L * I;
			return (w);
		}
	}

	if (x == 0.0L) {
		r = fabsl(y);
		r = sqrtl(0.5L * r);
		if (y > 0.0L)
			w = r + r * I;
		else
			w = r - r * I;
		return (w);
	}

	/* Rescale to avoid internal overflow or underflow.  */
	if ((fabsl(x) > 4.0L) || (fabsl(y) > 4.0L)) {
		x *= 0.25L;
		y *= 0.25L;
		scale = 2.0L;
	}
	else {
#if 1
		x *= 7.3786976294838206464e19;  /* 2^66 */
		y *= 7.3786976294838206464e19;
		scale = 1.16415321826934814453125e-10;  /* 2^-33 */
#else
		x *= 4.0L;
		y *= 4.0L;
		scale = 0.5L;
#endif
	}
	w = x + y * I;
	r = cabsl(w);
	if (x > 0) {
		t = sqrtl(0.5L * r + 0.5L * x);
		r = scale * fabsl((0.5L * y) / t);
		t *= scale;
	}
	else {
		r = sqrtl(0.5L * r - 0.5L * x);
		t = scale * fabsl((0.5L * y) / r);
		r *= scale;
	}
	if (y < 0)
		w = t - r * I;
	else
		w = t + r * I;
	return (w);
}
DEF_STD(csqrtl);
