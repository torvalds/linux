/*	$OpenBSD: s_ccosf.c,v 1.2 2010/07/18 18:42:26 guenther Exp $	*/
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

/*							ccosf()
 *
 *	Complex circular cosine
 *
 *
 *
 * SYNOPSIS:
 *
 * void ccosf();
 * cmplxf z, w;
 *
 * ccosf( &z, &w );
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
 *     w = cos x  cosh y  -  i sin x sinh y.
 *
 *
 *
 * ACCURACY:
 *
 *                      Relative error:
 * arithmetic   domain     # trials      peak         rms
 *    IEEE      -10,+10     30000       1.8e-7       5.5e-8
 */

#include <complex.h>
#include <math.h>

/* calculate cosh and sinh */

static void
_cchshf(float xx, float *c, float *s)
{
	float x, e, ei;

	x = xx;
	if(fabsf(x) <= 0.5f) {
		*c = coshf(x);
		*s = sinhf(x);
	}
	else {
		e = expf(x);
		ei = 0.5f/e;
		e = 0.5f * e;
		*s = e - ei;
		*c = e + ei;
	}
}

float complex
ccosf(float complex z)
{
	float complex w;
	float ch, sh;

	_cchshf( cimagf(z), &ch, &sh );
	w = cosf( crealf(z) ) * ch + ( -sinf( crealf(z) ) * sh) * I;
	return (w);
}
