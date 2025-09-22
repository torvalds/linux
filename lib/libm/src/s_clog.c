/*	$OpenBSD: s_clog.c,v 1.7 2016/09/12 19:47:02 guenther Exp $	*/
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

/*							clog.c
 *
 *	Complex natural logarithm
 *
 *
 *
 * SYNOPSIS:
 *
 * double complex clog();
 * double complex z, w;
 *
 * w = clog (z);
 *
 *
 *
 * DESCRIPTION:
 *
 * Returns complex logarithm to the base e (2.718...) of
 * the complex argument x.
 *
 * If z = x + iy, r = sqrt( x**2 + y**2 ),
 * then
 *       w = log(r) + i arctan(y/x).
 * 
 * The arctangent ranges from -PI to +PI.
 *
 *
 * ACCURACY:
 *
 *                      Relative error:
 * arithmetic   domain     # trials      peak         rms
 *    DEC       -10,+10      7000       8.5e-17     1.9e-17
 *    IEEE      -10,+10     30000       5.0e-15     1.1e-16
 *
 * Larger relative error can be observed for z near 1 +i0.
 * In IEEE arithmetic the peak absolute error is 5.2e-16, rms
 * absolute error 1.0e-16.
 */

#include <complex.h>
#include <float.h>
#include <math.h>

double complex
clog(double complex z)
{
	double complex w;
	double p, rr;

	/*rr = sqrt( z->r * z->r  +  z->i * z->i );*/
	rr = cabs(z);
	p = log(rr);
	rr = atan2 (cimag (z), creal (z));
	w = p + rr * I;
	return (w);
}
DEF_STD(clog);
LDBL_MAYBE_CLONE(clog);
