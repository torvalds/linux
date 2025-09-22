/*	$OpenBSD: complex.h,v 1.5 2014/03/16 18:38:30 guenther Exp $	*/
/*
 * Copyright (c) 2008 Martynas Venckus <martynas@openbsd.org>
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

#ifndef _COMPLEX_H_
#define	_COMPLEX_H_

#include <sys/cdefs.h>

/*
 * C99
 */
#ifdef __GNUC__
#if __STDC_VERSION__ < 199901
#define _Complex	__complex__
#endif
#define _Complex_I	1.0fi
#endif

#define complex		_Complex

/* XXX switch to _Imaginary_I */
#undef I
#define I		_Complex_I

__BEGIN_DECLS
/* 
 * Double versions of C99 functions
 */
double complex cacos(double complex);
double complex casin(double complex);
double complex catan(double complex);
double complex ccos(double complex);
double complex csin(double complex);
double complex ctan(double complex);
double complex cacosh(double complex);
double complex casinh(double complex);
double complex catanh(double complex);
double complex ccosh(double complex);
double complex csinh(double complex);
double complex ctanh(double complex);
double complex cexp(double complex);
double complex clog(double complex);
double cabs(double complex);
double complex cpow(double complex, double complex);
double complex csqrt(double complex);
double carg(double complex);
double cimag(double complex);
double complex conj(double complex);
double complex cproj(double complex);
double creal(double complex);

/* 
 * Float versions of C99 functions
 */
float complex cacosf(float complex);
float complex casinf(float complex);
float complex catanf(float complex);
float complex ccosf(float complex);
float complex csinf(float complex);
float complex ctanf(float complex);
float complex cacoshf(float complex);
float complex casinhf(float complex);
float complex catanhf(float complex);
float complex ccoshf(float complex);
float complex csinhf(float complex);
float complex ctanhf(float complex);
float complex cexpf(float complex);
float complex clogf(float complex);
float cabsf(float complex);
float complex cpowf(float complex, float complex);
float complex csqrtf(float complex);
float cargf(float complex);
float cimagf(float complex);
float complex conjf(float complex);
float complex cprojf(float complex);
float crealf(float complex);

/* 
 * Long double versions of C99 functions
 */
long double complex cacosl(long double complex);
long double complex casinl(long double complex);
long double complex catanl(long double complex);
long double complex ccosl(long double complex);
long double complex csinl(long double complex);
long double complex ctanl(long double complex);
long double complex cacoshl(long double complex);
long double complex casinhl(long double complex);
long double complex catanhl(long double complex);
long double complex ccoshl(long double complex);
long double complex csinhl(long double complex);
long double complex ctanhl(long double complex);
long double complex cexpl(long double complex);
long double complex clogl(long double complex);
long double cabsl(long double complex);
long double complex cpowl(long double complex,
	long double complex);
long double complex csqrtl(long double complex);
long double cargl(long double complex);
long double cimagl(long double complex);
long double complex conjl(long double complex);
long double complex cprojl(long double complex);
long double creall(long double complex);
__END_DECLS

#endif /* !_COMPLEX_H_ */
