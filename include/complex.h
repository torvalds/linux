/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001-2011 The FreeBSD Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _COMPLEX_H
#define	_COMPLEX_H

#include <sys/cdefs.h>

#ifdef __GNUC__
#if __STDC_VERSION__ < 199901
#define	_Complex	__complex__
#endif
#define	_Complex_I	((float _Complex)1.0i)
#endif

#ifdef __generic
_Static_assert(__generic(_Complex_I, float _Complex, 1, 0),
    "_Complex_I must be of type float _Complex");
#endif

#define	complex		_Complex
#define	I		_Complex_I

#if __ISO_C_VISIBLE >= 2011
#ifdef __clang__
#define	CMPLX(x, y)	((double complex){ x, y })
#define	CMPLXF(x, y)	((float complex){ x, y })
#define	CMPLXL(x, y)	((long double complex){ x, y })
#elif __GNUC_PREREQ__(4, 7)
#define	CMPLX(x, y)	__builtin_complex((double)(x), (double)(y))
#define	CMPLXF(x, y)	__builtin_complex((float)(x), (float)(y))
#define	CMPLXL(x, y)	__builtin_complex((long double)(x), (long double)(y))
#endif
#endif /* __ISO_C_VISIBLE >= 2011 */

__BEGIN_DECLS

double		cabs(double complex);
float		cabsf(float complex);
long double	cabsl(long double complex);
double complex	cacos(double complex);
float complex	cacosf(float complex);
double complex	cacosh(double complex);
float complex	cacoshf(float complex);
long double complex
		cacoshl(long double complex);
long double complex
		cacosl(long double complex);
double		carg(double complex);
float		cargf(float complex);
long double	cargl(long double complex);
double complex	casin(double complex);
float complex	casinf(float complex);
double complex	casinh(double complex);
float complex	casinhf(float complex);
long double complex
		casinhl(long double complex);
long double complex
		casinl(long double complex);
double complex	catan(double complex);
float complex	catanf(float complex);
double complex	catanh(double complex);
float complex	catanhf(float complex);
long double complex
		catanhl(long double complex);
long double complex
		catanl(long double complex);
double complex	ccos(double complex);
float complex	ccosf(float complex);
double complex	ccosh(double complex);
float complex	ccoshf(float complex);
double complex	cexp(double complex);
float complex	cexpf(float complex);
double		cimag(double complex) __pure2;
float		cimagf(float complex) __pure2;
long double	cimagl(long double complex) __pure2;
double complex	clog(double complex);
float complex	clogf(float complex);
long double complex
		clogl(long double complex);
double complex	conj(double complex) __pure2;
float complex	conjf(float complex) __pure2;
long double complex
		conjl(long double complex) __pure2;
float complex	cpowf(float complex, float complex);
double complex	cpow(double complex, double complex);
long double complex
		cpowl(long double complex, long double complex);
float complex	cprojf(float complex) __pure2;
double complex	cproj(double complex) __pure2;
long double complex
		cprojl(long double complex) __pure2;
double		creal(double complex) __pure2;
float		crealf(float complex) __pure2;
long double	creall(long double complex) __pure2;
double complex	csin(double complex);
float complex	csinf(float complex);
double complex	csinh(double complex);
float complex	csinhf(float complex);
double complex	csqrt(double complex);
float complex	csqrtf(float complex);
long double complex
		csqrtl(long double complex);
double complex	ctan(double complex);
float complex	ctanf(float complex);
double complex	ctanh(double complex);
float complex	ctanhf(float complex);

__END_DECLS

#endif /* _COMPLEX_H */
