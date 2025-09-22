/*	$OpenBSD: s_nearbyint.c,v 1.1 2011/04/28 18:05:39 martynas Exp $	*/

/*
 * Copyright (c) 2011 Martynas Venckus <martynas@openbsd.org>
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

#include <fenv.h>
#include <math.h>

double
nearbyint(double x)
{
	fenv_t fenv;
	double res;

	feholdexcept(&fenv);
	res = rint(x);
	fesetenv(&fenv);

	return (res);
}

float
nearbyintf(float x)
{
	fenv_t fenv;
	float res;

	feholdexcept(&fenv);
	res = rintf(x);
	fesetenv(&fenv);

	return (res);
}

long double
nearbyintl(long double x)
{
	fenv_t fenv;
	long double res;

	feholdexcept(&fenv);
	res = rintl(x);
	fesetenv(&fenv);

	return (res);
}
