/*	$OpenBSD: s_casin.c,v 1.8 2016/09/12 19:47:02 guenther Exp $	*/
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

/*							casin()
 *
 *	Complex circular arc sine
 *
 *
 *
 * SYNOPSIS:
 *
 * double complex casin();
 * double complex z, w;
 *
 * w = casin (z);
 *
 *
 *
 * DESCRIPTION:
 *
 * Inverse complex sine:
 *
 *                               2
 * w = -i clog( iz + csqrt( 1 - z ) ).
 *
 * casin(z) = -i casinh(iz)
 *
 * ACCURACY:
 *
 *                      Relative error:
 * arithmetic   domain     # trials      peak         rms
 *    DEC       -10,+10     10100       2.1e-15     3.4e-16
 *    IEEE      -10,+10     30000       2.2e-14     2.7e-15
 * Larger relative error can be observed for z near zero.
 * Also tested by csin(casin(z)) = z.
 */

#include <complex.h>
#include <float.h>
#include <math.h>

double complex
casin(double complex z)
{
	double complex w;
	static double complex ca, ct, zz, z2;
	double x, y;

	x = creal (z);
	y = cimag (z);

#if 0
	if (y == 0.0) {
		if (fabs(x) > 1.0) {
			w = M_PI_2 + 0.0 * I;
			/*mtherr ("casin", DOMAIN);*/
		}
		else {
			w = asin (x) + 0.0 * I;
		}
		return (w);
	}
#endif

	/* Power series expansion */
	/*
	b = cabs(z);
	if( b < 0.125 ) {
		z2.r = (x - y) * (x + y);
		z2.i = 2.0 * x * y;

		cn = 1.0;
		n = 1.0;
		ca.r = x;
		ca.i = y;
		sum.r = x;
		sum.i = y;
		do {
			ct.r = z2.r * ca.r  -  z2.i * ca.i;
			ct.i = z2.r * ca.i  +  z2.i * ca.r;
			ca.r = ct.r;
			ca.i = ct.i;

			cn *= n;
			n += 1.0;
			cn /= n;
			n += 1.0;
			b = cn/n;

			ct.r *= b;
			ct.i *= b;
			sum.r += ct.r;
			sum.i += ct.i;
			b = fabs(ct.r) + fabs(ct.i);
		}
		while( b > MACHEP );
		w->r = sum.r;
		w->i = sum.i;
		return;
	}
	*/

	ca = x + y * I;
	ct = ca * I;
	/* sqrt( 1 - z*z) */
	/* cmul( &ca, &ca, &zz ) */
	/*x * x  -  y * y */
	zz = (x - y) * (x + y) + (2.0 * x * y) * I;

	zz = 1.0 - creal(zz) - cimag(zz) * I;
	z2 = csqrt (zz);

	zz = ct + z2;
	zz = clog (zz);
	/* multiply by 1/i = -i */
	w = zz * (-1.0 * I);
	return (w);
}
DEF_STD(casin);
LDBL_MAYBE_CLONE(casin);
