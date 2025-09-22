/*	$OpenBSD: s_casinl.c,v 1.5 2016/09/12 19:47:02 guenther Exp $	*/

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

/*							casinl()
 *
 *	Complex circular arc sine
 *
 *
 *
 * SYNOPSIS:
 *
 * long double complex casinl();
 * long double complex z, w;
 *
 * w = casinl( z );
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

#if	LDBL_MANT_DIG == 64
static const long double MACHEPL= 5.42101086242752217003726400434970855712890625E-20L;
#elif	LDBL_MANT_DIG == 113
static const long double MACHEPL = 9.629649721936179265279889712924636592690508e-35L;
#endif

static const long double PIO2L = 1.570796326794896619231321691639751442098585L;

long double complex
casinl(long double complex z)
{
	long double complex w;
	long double x, y, b;
	static long double complex ca, ct, zz, z2;

	x = creall(z);
	y = cimagl(z);

#if 0
	if (y == 0.0L) {
		if (fabsl(x) > 1.0L) {
			w = PIO2L + 0.0L * I;
			/*mtherr( "casinl", DOMAIN );*/
		}
		else {
			w = asinl(x) + 0.0L * I;
		}
		return (w);
	}
#endif

	/* Power series expansion */
	b = cabsl(z);
	if (b < 0.125L) {
		long double complex sum;
		long double n, cn;

		z2 = (x - y) * (x + y) + (2.0L * x * y) * I;
		cn = 1.0L;
		n = 1.0L;
		ca = x + y * I;
		sum = x + y * I;
		do {
			ct = z2 * ca;
			ca = ct;

			cn *= n;
			n += 1.0L;
			cn /= n;
			n += 1.0L;
			b = cn/n;

			ct *= b;
			sum += ct;
			b = cabsl(ct);
		}

		while (b > MACHEPL);
		w = sum;
		return w;
	}

	ca = x + y * I;
	ct = ca * I;	/* iz */
	/* sqrt(1 - z*z) */
	/* cmul(&ca, &ca, &zz) */
	/* x * x  -  y * y */
	zz = (x - y) * (x + y) + (2.0L * x * y) * I;
	zz = 1.0L - creall(zz) - cimagl(zz) * I;
	z2 = csqrtl(zz);

	zz = ct + z2;
	zz = clogl(zz);
	/* multiply by 1/i = -i */
	w = zz * (-1.0L * I);
	return (w);
}
DEF_STD(casinl);
