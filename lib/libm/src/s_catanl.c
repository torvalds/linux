/*	$OpenBSD: s_catanl.c,v 1.4 2016/09/12 19:47:02 guenther Exp $	*/

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

/*							catanl()
 *
 *	Complex circular arc tangent
 *
 *
 *
 * SYNOPSIS:
 *
 * long double complex catanl();
 * long double complex z, w;
 *
 * w = catanl( z );
 *
 *
 *
 * DESCRIPTION:
 *
 * If
 *     z = x + iy,
 *
 * then
 *          1       (    2x     )
 * Re w  =  - arctan(-----------)  +  k PI
 *          2       (     2    2)
 *                  (1 - x  - y )
 *
 *               ( 2         2)
 *          1    (x  +  (y+1) )
 * Im w  =  - log(------------)
 *          4    ( 2         2)
 *               (x  +  (y-1) )
 *
 * Where k is an arbitrary integer.
 *
 *
 *
 * ACCURACY:
 *
 *                      Relative error:
 * arithmetic   domain     # trials      peak         rms
 *    DEC       -10,+10      5900       1.3e-16     7.8e-18
 *    IEEE      -10,+10     30000       2.3e-15     8.5e-17
 * The check catan( ctan(z) )  =  z, with |x| and |y| < PI/2,
 * had peak relative error 1.5e-16, rms relative error
 * 2.9e-17.  See also clog().
 */

#include <complex.h>
#include <float.h>
#include <math.h>

static const long double PIL = 3.141592653589793238462643383279502884197169L;
static const long double DP1 = 3.14159265358979323829596852490908531763125L;
static const long double DP2 = 1.6667485837041756656403424829301998703007e-19L;
static const long double DP3 = 1.8830410776607851167459095484560349402753e-39L;

static long double
redupil(long double x)
{
	long double t;
	long i;

	t = x / PIL;
	if (t >= 0.0L)
		t += 0.5L;
	else
		t -= 0.5L;

	i = t;	/* the multiple */
	t = i;
	t = ((x - t * DP1) - t * DP2) - t * DP3;
	return (t);
}

long double complex
catanl(long double complex z)
{
	long double complex w;
	long double a, t, x, x2, y;

	x = creall(z);
	y = cimagl(z);

	if ((x == 0.0L) && (y > 1.0L))
		goto ovrf;

	x2 = x * x;
	a = 1.0L - x2 - (y * y);
	if (a == 0.0L)
		goto ovrf;

	t = atan2l(2.0L * x, a) * 0.5L;
	w = redupil(t);

	t = y - 1.0L;
	a = x2 + (t * t);
	if (a == 0.0L)
		goto ovrf;

	t = y + 1.0L;
	a = (x2 + (t * t)) / a;
	w = w + (0.25L * logl(a)) * I;
	return (w);

ovrf:
	/*mtherr( "catanl", OVERFLOW );*/
	w = LDBL_MAX + LDBL_MAX * I;
	return (w);
}
DEF_STD(catanl);
