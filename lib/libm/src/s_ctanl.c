/*	$OpenBSD: s_ctanl.c,v 1.3 2011/07/20 21:02:51 martynas Exp $	*/

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

/*							ctanl()
 *
 *	Complex circular tangent
 *
 *
 *
 * SYNOPSIS:
 *
 * long double complex ctanl();
 * long double complex z, w;
 *
 * w = ctanl( z );
 *
 *
 *
 * DESCRIPTION:
 *
 * If
 *     z = x + iy,
 *
 * then
 *
 *           sin 2x  +  i sinh 2y
 *     w  =  --------------------.
 *            cos 2x  +  cosh 2y
 *
 * On the real axis the denominator is zero at odd multiples
 * of PI/2.  The denominator is evaluated by its Taylor
 * series near these points.
 *
 *
 * ACCURACY:
 *
 *                      Relative error:
 * arithmetic   domain     # trials      peak         rms
 *    DEC       -10,+10      5200       7.1e-17     1.6e-17
 *    IEEE      -10,+10     30000       7.2e-16     1.2e-16
 * Also tested by ctan * ccot = 1 and catan(ctan(z))  =  z.
 */

#include <complex.h>
#include <float.h>
#include <math.h>

#if	LDBL_MANT_DIG == 64
static const long double MACHEPL= 5.42101086242752217003726400434970855712890625E-20L;
#elif	LDBL_MANT_DIG == 113
static const long double MACHEPL = 9.629649721936179265279889712924636592690508e-35L;
#endif

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

static long double
ctansl(long double complex z)
{
	long double f, x, x2, y, y2, rn, t;
	long double d;

	x = fabsl(2.0L * creall(z));
	y = fabsl(2.0L * cimagl(z));

	x = redupil(x);

	x = x * x;
	y = y * y;
	x2 = 1.0L;
	y2 = 1.0L;
	f = 1.0L;
	rn = 0.0L;
	d = 0.0L;
	do {
		rn += 1.0L;
		f *= rn;
		rn += 1.0L;
		f *= rn;
		x2 *= x;
		y2 *= y;
		t = y2 + x2;
		t /= f;
		d += t;

		rn += 1.0L;
		f *= rn;
		rn += 1.0L;
		f *= rn;
		x2 *= x;
		y2 *= y;
		t = y2 - x2;
		t /= f;
		d += t;
	}
	while (fabsl(t/d) > MACHEPL);
	return(d);
}

long double complex
ctanl(long double complex z)
{
	long double complex w;
	long double d, x, y;

	x = creall(z);
	y = cimagl(z);
	d = cosl(2.0L * x) + coshl(2.0L * y);

	if (fabsl(d) < 0.25L) {
		d = fabsl(d);
		d = ctansl(z);
	}
	if (d == 0.0L) {
		/*mtherr( "ctan", OVERFLOW );*/
		w = LDBL_MAX + LDBL_MAX * I;
		return (w);
	}

	w = sinl(2.0L * x) / d + (sinhl(2.0L * y) / d) * I;
	return (w);
}
