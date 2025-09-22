/*	$OpenBSD: s_catanf.c,v 1.3 2016/09/12 19:47:02 guenther Exp $	*/
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

/*							catanf()
 *
 *	Complex circular arc tangent
 *
 *
 *
 * SYNOPSIS:
 *
 * float complex catanf();
 * float complex z, w;
 *
 * w = catanf( z );
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
 *    IEEE      -10,+10     30000        2.3e-6      5.2e-8
 *
 */

#include <complex.h>
#include <math.h>

#define MAXNUMF 1.0e38F

static const double DP1 = 3.140625;
static const double DP2 = 9.67502593994140625E-4;
static const double DP3 = 1.509957990978376432E-7;

static float
_redupif(float xx)
{
	float x, t;
	long i;

	x = xx;
	t = x/(float)M_PI;
	if(t >= 0.0)
		t += 0.5;
	else
		t -= 0.5;

	i = t;	/* the multiple */
	t = i;
	t = ((x - t * DP1) - t * DP2) - t * DP3;
	return(t);
}

float complex
catanf(float complex z)
{
	float complex w;
	float a, t, x, x2, y;

	x = crealf(z);
	y = cimagf(z);

	if((x == 0.0f) && (y > 1.0f))
		goto ovrf;

	x2 = x * x;
	a = 1.0f - x2 - (y * y);
	if (a == 0.0f)
		goto ovrf;

	t = 0.5f * atan2f(2.0f * x, a);
	w = _redupif(t);

	t = y - 1.0f;
	a = x2 + (t * t);
	if(a == 0.0f)
		goto ovrf;

	t = y + 1.0f;
	a = (x2 + (t * t))/a;
	w = w + (0.25f * logf (a)) * I;
	return (w);

ovrf:
	/*mtherr( "catanf", OVERFLOW );*/
	w = MAXNUMF + MAXNUMF * I;
	return (w);
}
DEF_STD(catanf);
