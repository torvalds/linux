/*	$OpenBSD: s_csin.c,v 1.7 2016/09/12 19:47:02 guenther Exp $	*/
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

/*							csin()
 *
 *	Complex circular sine
 *
 *
 *
 * SYNOPSIS:
 *
 * double complex csin();
 * double complex z, w;
 *
 * w = csin (z);
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
 *     w = sin x  cosh y  +  i cos x sinh y.
 *
 * csin(z) = -i csinh(iz).
 *
 * ACCURACY:
 *
 *                      Relative error:
 * arithmetic   domain     # trials      peak         rms
 *    DEC       -10,+10      8400       5.3e-17     1.3e-17
 *    IEEE      -10,+10     30000       3.8e-16     1.0e-16
 * Also tested by csin(casin(z)) = z.
 *
 */

#include <complex.h>
#include <float.h>
#include <math.h>

/* calculate cosh and sinh */

static void
cchsh(double x, double *c, double *s)
{
	double e, ei;

	if (fabs(x) <= 0.5) {
		*c = cosh(x);
		*s = sinh(x);
	}
	else {
		e = exp(x);
		ei = 0.5/e;
		e = 0.5 * e;
		*s = e - ei;
		*c = e + ei;
	}
}

double complex
csin(double complex z)
{
	double complex w;
	double ch, sh;

	cchsh( cimag (z), &ch, &sh );
	w = sin (creal(z)) * ch + (cos (creal(z)) * sh) * I;
	return (w);
}
DEF_STD(csin);
LDBL_MAYBE_UNUSED_CLONE(csin);
