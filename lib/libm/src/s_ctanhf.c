/*	$OpenBSD: s_ctanhf.c,v 1.2 2010/07/18 18:42:26 guenther Exp $	*/
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

/*							ctanhf
 *
 *	Complex hyperbolic tangent
 *
 *
 *
 * SYNOPSIS:
 *
 * float complex ctanhf();
 * float complex z, w;
 *
 * w = ctanhf (z);
 *
 *
 *
 * DESCRIPTION:
 *
 * tanh z = (sinh 2x  +  i sin 2y) / (cosh 2x + cos 2y) .
 *
 * ACCURACY:
 *
 *                      Relative error:
 * arithmetic   domain     # trials      peak         rms
 *    IEEE      -10,+10     30000       1.7e-14     2.4e-16
 *
 */

#include <complex.h>
#include <math.h>

float complex
ctanhf(float complex z)
{
	float complex w;
	float x, y, d;

	x = crealf(z);
	y = cimagf(z);
	d = coshf (2.0f * x) + cosf (2.0f * y);
	w = sinhf (2.0f * x) / d  +  (sinf (2.0f * y) / d) * I;
	return (w);
}
