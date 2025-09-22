/*	$OpenBSD: s_ctanhl.c,v 1.2 2011/07/20 19:28:33 martynas Exp $	*/

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

/*							ctanhl
 *
 *	Complex hyperbolic tangent
 *
 *
 *
 * SYNOPSIS:
 *
 * long double complex ctanhl();
 * long double complex z, w;
 *
 * w = ctanhl (z);
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

long double complex
ctanhl(long double complex z)
{
	long double complex w;
	long double x, y, d;

	x = creall(z);
	y = cimagl(z);
	d = coshl(2.0L * x) + cosl(2.0L * y);
	w = sinhl(2.0L * x) / d + (sinl(2.0L * y) / d) * I;
	return (w);
}
