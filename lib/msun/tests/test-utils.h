/*-
 * Copyright (c) 2005-2013 David Schultz <das@FreeBSD.org>
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

#ifndef	_TEST_UTILS_H_
#define	_TEST_UTILS_H_

#include <complex.h>
#include <fenv.h>

/*
 * Implementations are permitted to define additional exception flags
 * not specified in the standard, so it is not necessarily true that
 * FE_ALL_EXCEPT == ALL_STD_EXCEPT.
 */
#define	ALL_STD_EXCEPT	(FE_DIVBYZERO | FE_INEXACT | FE_INVALID | \
			 FE_OVERFLOW | FE_UNDERFLOW)
#define	OPT_INVALID	(ALL_STD_EXCEPT & ~FE_INVALID)
#define	OPT_INEXACT	(ALL_STD_EXCEPT & ~FE_INEXACT)
#define	FLT_ULP()	ldexpl(1.0, 1 - FLT_MANT_DIG)
#define	DBL_ULP()	ldexpl(1.0, 1 - DBL_MANT_DIG)
#define	LDBL_ULP()	ldexpl(1.0, 1 - LDBL_MANT_DIG)

/*
 * Flags that control the behavior of various fpequal* functions.
 * XXX This is messy due to merging various notions of "close enough"
 * that are best suited for different functions.
 *
 * CS_REAL
 * CS_IMAG
 * CS_BOTH
 *   (cfpequal_cs, fpequal_tol, cfpequal_tol) Whether to check the sign of
 *   the real part of the result, the imaginary part, or both.
 *
 * FPE_ABS_ZERO
 *   (fpequal_tol, cfpequal_tol) If set, treats the tolerance as an absolute
 *   tolerance when the expected value is 0.  This is useful when there is
 *   round-off error in the input, e.g., cos(Pi/2) ~= 0.
 */
#define	CS_REAL		0x01
#define	CS_IMAG		0x02
#define	CS_BOTH		(CS_REAL | CS_IMAG)
#define	FPE_ABS_ZERO	0x04

#ifdef	DEBUG
#define	debug(...)	printf(__VA_ARGS__)
#else
#define	debug(...)	(void)0
#endif

/*
 * XXX The ancient version of gcc in the base system doesn't support CMPLXL,
 * but we can fake it most of the time.
 */
#ifndef CMPLXL
static inline long double complex
CMPLXL(long double x, long double y)
{
	long double complex z;

	__real__ z = x;
	__imag__ z = y;
	return (z);
}
#endif

static int	fpequal(long double, long double) __used;
static int	cfpequal(long double complex, long double complex) __used;
static int	cfpequal_cs(long double complex, long double complex,
		    int) __used;
static int	cfpequal_tol(long double complex, long double complex,
		    long double, unsigned int) __used;

/*
 * Compare d1 and d2 using special rules: NaN == NaN and +0 != -0.
 * Fail an assertion if they differ.
 */
static int
fpequal(long double d1, long double d2)
{

	if (d1 != d2)
		return (isnan(d1) && isnan(d2));
	return (copysignl(1.0, d1) == copysignl(1.0, d2));
}

/*
 * Determine whether x and y are equal, with two special rules:
 *	+0.0 != -0.0
 *	 NaN == NaN
 * If checksign is 0, we compare the absolute values instead.
 */
static int
fpequal_cs(long double x, long double y, int checksign)
{
	if (isnan(x) && isnan(y))
		return (1);
	if (checksign)
		return (x == y && !signbit(x) == !signbit(y));
	else
		return (fabsl(x) == fabsl(y));
}

static int
fpequal_tol(long double x, long double y, long double tol,
    unsigned int flags)
{
	fenv_t env;
	int ret;

	if (isnan(x) && isnan(y))
		return (1);
	if (!signbit(x) != !signbit(y) && (flags & CS_BOTH))
		return (0);
	if (x == y)
		return (1);
	if (tol == 0)
		return (0);

	/* Hard case: need to check the tolerance. */
	feholdexcept(&env);
	/*
	 * For our purposes here, if y=0, we interpret tol as an absolute
	 * tolerance. This is to account for roundoff in the input, e.g.,
	 * cos(Pi/2) ~= 0.
	 */
	if ((flags & FPE_ABS_ZERO) && y == 0.0)
		ret = fabsl(x - y) <= fabsl(tol);
	else
		ret = fabsl(x - y) <= fabsl(y * tol);
	fesetenv(&env);
	return (ret);
}

static int
cfpequal(long double complex d1, long double complex d2)
{

	return (fpequal(creall(d1), creall(d2)) &&
	    fpequal(cimagl(d1), cimagl(d2)));
}

static int
cfpequal_cs(long double complex x, long double complex y, int checksign)
{
	return (fpequal_cs(creal(x), creal(y), checksign)
		&& fpequal_cs(cimag(x), cimag(y), checksign));
}

static int
cfpequal_tol(long double complex x, long double complex y, long double tol,
    unsigned int flags)
{
	return (fpequal_tol(creal(x), creal(y), tol, flags)
		&& fpequal_tol(cimag(x), cimag(y), tol, flags));
}

#endif /* _TEST_UTILS_H_ */
