/*	$OpenBSD: s_ccoshf.c,v 1.2 2010/07/18 18:42:26 guenther Exp $	*/
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

/*							ccoshf
 *
 *	Complex hyperbolic cosine
 *
 *
 *
 * SYNOPSIS:
 *
 * float complex ccoshf();
 * float complex z, w;
 *
 * w = ccoshf (z);
 *
 *
 *
 * DESCRIPTION:
 *
 * ccosh(z) = cosh x  cos y + i sinh x sin y .
 *
 * ACCURACY:
 *
 *                      Relative error:
 * arithmetic   domain     # trials      peak         rms
 *    IEEE      -10,+10     30000       2.9e-16     8.1e-17
 *
 */

#include <complex.h>
#include <math.h>

float complex
ccoshf(float complex z)
{
	float complex w;
	float x, y;

	x = crealf(z);
	y = cimagf(z);
	w = coshf (x) * cosf (y)  +  (sinhf (x) * sinf (y)) * I;
	return (w);
}
