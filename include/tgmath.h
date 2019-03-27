/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Stefan Farfeleder.
 * All rights reserved.
 *
 * Copyright (c) 2012 Ed Schouten <ed@FreeBSD.org>
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

#ifndef _TGMATH_H_
#define	_TGMATH_H_

#include <complex.h>
#include <math.h>

/*
 * This implementation of <tgmath.h> uses the two following macros,
 * which are based on the macros described in C11 proposal N1404:
 * __tg_impl_simple(x, y, z, fnl, fn, fnf, ...)
 *	Invokes fnl() if the corresponding real type of x, y or z is long
 *	double, fn() if it is double or any has an integer type, and fnf()
 *	otherwise.
 * __tg_impl_full(x, y, cfnl, cfn, cfnf, fnl, fn, fnf, ...)
 *	Invokes [c]fnl() if the corresponding real type of x or y is long
 *	double, [c]fn() if it is double or any has an integer type, and
 *	[c]fnf() otherwise.  The function with the 'c' prefix is called if
 *	any of x or y is a complex number.
 * Both macros call the chosen function with all additional arguments passed
 * to them, as given by __VA_ARGS__.
 *
 * Note that these macros cannot be implemented with C's ?: operator,
 * because the return type of the whole expression would incorrectly be long
 * double complex regardless of the argument types.
 *
 * The structure of the C11 implementation of these macros can in
 * principle be reused for non-C11 compilers, but due to an integer
 * promotion bug for complex types in GCC 4.2, simply let non-C11
 * compilers use an inefficient yet reliable version.
 */

#if (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L) || \
    __has_extension(c_generic_selections)
#define	__tg_generic(x, cfnl, cfn, cfnf, fnl, fn, fnf)			\
	_Generic(x,							\
		long double _Complex: cfnl,				\
		double _Complex: cfn,					\
		float _Complex: cfnf,					\
		long double: fnl,					\
		default: fn,						\
		float: fnf						\
	)
#define	__tg_type(x)							\
	__tg_generic(x, (long double _Complex)0, (double _Complex)0,	\
	    (float _Complex)0, (long double)0, (double)0, (float)0)
#define	__tg_impl_simple(x, y, z, fnl, fn, fnf, ...)			\
	__tg_generic(							\
	    __tg_type(x) + __tg_type(y) + __tg_type(z),			\
	    fnl, fn, fnf, fnl, fn, fnf)(__VA_ARGS__)
#define	__tg_impl_full(x, y, cfnl, cfn, cfnf, fnl, fn, fnf, ...)	\
	__tg_generic(							\
	    __tg_type(x) + __tg_type(y),				\
	    cfnl, cfn, cfnf, fnl, fn, fnf)(__VA_ARGS__)
#elif defined(__generic)
#define	__tg_generic_simple(x, fnl, fn, fnf)				\
	__generic(x, long double _Complex, fnl,				\
	    __generic(x, double _Complex, fn,				\
	        __generic(x, float _Complex, fnf,			\
	            __generic(x, long double, fnl,			\
	                __generic(x, float, fnf, fn)))))
#define	__tg_impl_simple(x, y, z, fnl, fn, fnf, ...)			\
	__tg_generic_simple(x,						\
	    __tg_generic_simple(y,					\
	        __tg_generic_simple(z, fnl, fnl, fnl),			\
	        __tg_generic_simple(z, fnl, fnl, fnl),			\
	        __tg_generic_simple(z, fnl, fnl, fnl)),			\
	    __tg_generic_simple(y,					\
	        __tg_generic_simple(z, fnl, fnl, fnl),			\
	        __tg_generic_simple(z, fnl, fn , fn ),			\
	        __tg_generic_simple(z, fnl, fn , fn )),			\
	    __tg_generic_simple(y,					\
	        __tg_generic_simple(z, fnl, fnl, fnl),			\
	        __tg_generic_simple(z, fnl, fn , fn ),			\
	        __tg_generic_simple(z, fnl, fn , fnf)))(__VA_ARGS__)
#define	__tg_generic_full(x, cfnl, cfn, cfnf, fnl, fn, fnf)		\
	__generic(x, long double _Complex, cfnl,			\
	    __generic(x, double _Complex, cfn,				\
	        __generic(x, float _Complex, cfnf,			\
	            __generic(x, long double, fnl,			\
	                __generic(x, float, fnf, fn)))))
#define	__tg_impl_full(x, y, cfnl, cfn, cfnf, fnl, fn, fnf, ...)	\
	__tg_generic_full(x,						\
	    __tg_generic_full(y, cfnl, cfnl, cfnl, cfnl, cfnl, cfnl),	\
	    __tg_generic_full(y, cfnl, cfn , cfn , cfnl, cfn , cfn ),	\
	    __tg_generic_full(y, cfnl, cfn , cfnf, cfnl, cfn , cfnf),	\
	    __tg_generic_full(y, cfnl, cfnl, cfnl, fnl , fnl , fnl ),	\
	    __tg_generic_full(y, cfnl, cfn , cfn , fnl , fn  , fn  ),	\
	    __tg_generic_full(y, cfnl, cfn , cfnf, fnl , fn  , fnf ))	\
	    (__VA_ARGS__)
#else
#error "<tgmath.h> not implemented for this compiler"
#endif

/* Macros to save lots of repetition below */
#define	__tg_simple(x, fn)						\
	__tg_impl_simple(x, x, x, fn##l, fn, fn##f, x)
#define	__tg_simple2(x, y, fn)						\
	__tg_impl_simple(x, x, y, fn##l, fn, fn##f, x, y)
#define	__tg_simple3(x, y, z, fn)					\
	__tg_impl_simple(x, y, z, fn##l, fn, fn##f, x, y, z)
#define	__tg_simplev(x, fn, ...)					\
	__tg_impl_simple(x, x, x, fn##l, fn, fn##f, __VA_ARGS__)
#define	__tg_full(x, fn)						\
	__tg_impl_full(x, x, c##fn##l, c##fn, c##fn##f, fn##l, fn, fn##f, x)
#define	__tg_full2(x, y, fn)						\
	__tg_impl_full(x, y, c##fn##l, c##fn, c##fn##f, fn##l, fn, fn##f, x, y)

/* 7.22#4 -- These macros expand to real or complex functions, depending on
 * the type of their arguments. */
#define	acos(x)		__tg_full(x, acos)
#define	asin(x)		__tg_full(x, asin)
#define	atan(x)		__tg_full(x, atan)
#define	acosh(x)	__tg_full(x, acosh)
#define	asinh(x)	__tg_full(x, asinh)
#define	atanh(x)	__tg_full(x, atanh)
#define	cos(x)		__tg_full(x, cos)
#define	sin(x)		__tg_full(x, sin)
#define	tan(x)		__tg_full(x, tan)
#define	cosh(x)		__tg_full(x, cosh)
#define	sinh(x)		__tg_full(x, sinh)
#define	tanh(x)		__tg_full(x, tanh)
#define	exp(x)		__tg_full(x, exp)
#define	log(x)		__tg_full(x, log)
#define	pow(x, y)	__tg_full2(x, y, pow)
#define	sqrt(x)		__tg_full(x, sqrt)

/* "The corresponding type-generic macro for fabs and cabs is fabs." */
#define	fabs(x)		__tg_impl_full(x, x, cabsl, cabs, cabsf,	\
    			    fabsl, fabs, fabsf, x)

/* 7.22#5 -- These macros are only defined for arguments with real type. */
#define	atan2(x, y)	__tg_simple2(x, y, atan2)
#define	cbrt(x)		__tg_simple(x, cbrt)
#define	ceil(x)		__tg_simple(x, ceil)
#define	copysign(x, y)	__tg_simple2(x, y, copysign)
#define	erf(x)		__tg_simple(x, erf)
#define	erfc(x)		__tg_simple(x, erfc)
#define	exp2(x)		__tg_simple(x, exp2)
#define	expm1(x)	__tg_simple(x, expm1)
#define	fdim(x, y)	__tg_simple2(x, y, fdim)
#define	floor(x)	__tg_simple(x, floor)
#define	fma(x, y, z)	__tg_simple3(x, y, z, fma)
#define	fmax(x, y)	__tg_simple2(x, y, fmax)
#define	fmin(x, y)	__tg_simple2(x, y, fmin)
#define	fmod(x, y)	__tg_simple2(x, y, fmod)
#define	frexp(x, y)	__tg_simplev(x, frexp, x, y)
#define	hypot(x, y)	__tg_simple2(x, y, hypot)
#define	ilogb(x)	__tg_simple(x, ilogb)
#define	ldexp(x, y)	__tg_simplev(x, ldexp, x, y)
#define	lgamma(x)	__tg_simple(x, lgamma)
#define	llrint(x)	__tg_simple(x, llrint)
#define	llround(x)	__tg_simple(x, llround)
#define	log10(x)	__tg_simple(x, log10)
#define	log1p(x)	__tg_simple(x, log1p)
#define	log2(x)		__tg_simple(x, log2)
#define	logb(x)		__tg_simple(x, logb)
#define	lrint(x)	__tg_simple(x, lrint)
#define	lround(x)	__tg_simple(x, lround)
#define	nearbyint(x)	__tg_simple(x, nearbyint)
#define	nextafter(x, y)	__tg_simple2(x, y, nextafter)
#define	nexttoward(x, y) __tg_simplev(x, nexttoward, x, y)
#define	remainder(x, y)	__tg_simple2(x, y, remainder)
#define	remquo(x, y, z)	__tg_impl_simple(x, x, y, remquol, remquo,	\
			    remquof, x, y, z)
#define	rint(x)		__tg_simple(x, rint)
#define	round(x)	__tg_simple(x, round)
#define	scalbn(x, y)	__tg_simplev(x, scalbn, x, y)
#define	scalbln(x, y)	__tg_simplev(x, scalbln, x, y)
#define	tgamma(x)	__tg_simple(x, tgamma)
#define	trunc(x)	__tg_simple(x, trunc)

/* 7.22#6 -- These macros always expand to complex functions. */
#define	carg(x)		__tg_simple(x, carg)
#define	cimag(x)	__tg_simple(x, cimag)
#define	conj(x)		__tg_simple(x, conj)
#define	cproj(x)	__tg_simple(x, cproj)
#define	creal(x)	__tg_simple(x, creal)

#endif /* !_TGMATH_H_ */
