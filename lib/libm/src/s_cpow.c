/*	$OpenBSD: s_cpow.c,v 1.7 2016/09/12 19:47:02 guenther Exp $	*/
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

/*							cpow
 *
 *	Complex power function
 *
 *
 *
 * SYNOPSIS:
 *
 * double complex cpow();
 * double complex a, z, w;
 *
 * w = cpow (a, z);
 *
 *
 *
 * DESCRIPTION:
 *
 * Raises complex A to the complex Zth power.
 * Definition is per AMS55 # 4.2.8,
 * analytically equivalent to cpow(a,z) = cexp(z clog(a)).
 *
 * ACCURACY:
 *
 *                      Relative error:
 * arithmetic   domain     # trials      peak         rms
 *    IEEE      -10,+10     30000       9.4e-15     1.5e-15
 *
 */

#include <complex.h>
#include <float.h>
#include <math.h>

double complex
cpow(double complex a, double complex z)
{
	double complex w;
	double x, y, r, theta, absa, arga;

	x = creal (z);
	y = cimag (z);
	absa = cabs (a);
	if (absa == 0.0) {
		return (0.0 + 0.0 * I);
	}
	arga = carg (a);
	r = pow (absa, x);
	theta = x * arga;
	if (y != 0.0) {
		r = r * exp (-y * arga);
		theta = theta + y * log (absa);
	}
	w = r * cos (theta) + (r * sin (theta)) * I;
	return (w);
}
DEF_STD(cpow);
LDBL_MAYBE_UNUSED_CLONE(cpow);
