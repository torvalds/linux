/*	$OpenBSD: s_cacoshf.c,v 1.2 2015/07/16 13:29:11 martynas Exp $	*/
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

/*							cacoshf
 *
 *	Complex inverse hyperbolic cosine
 *
 *
 *
 * SYNOPSIS:
 *
 * float complex cacoshf();
 * float complex z, w;
 *
 * w = cacoshf (z);
 *
 *
 *
 * DESCRIPTION:
 *
 * acosh z = i acos z .
 *
 * ACCURACY:
 *
 *                      Relative error:
 * arithmetic   domain     # trials      peak         rms
 *    IEEE      -10,+10     30000       1.6e-14     2.1e-15
 *
 */

#include <complex.h>
#include <math.h>

float complex
cacoshf(float complex z)
{
	float complex w;

	w = clogf(z + csqrtf(z + 1) * csqrtf(z - 1));
	return (w);
}
