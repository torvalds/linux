/*-
 * Copyright (c) 2008 David Schultz <das@FreeBSD.org>
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
 */

/*
 * Tests for corner cases in exp*().
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <assert.h>
#include <fenv.h>
#include <float.h>
#include <math.h>
#include <stdio.h>

#ifdef __i386__
#include <ieeefp.h>
#endif

#include "test-utils.h"

#pragma STDC FENV_ACCESS ON

/*
 * Test that a function returns the correct value and sets the
 * exception flags correctly. The exceptmask specifies which
 * exceptions we should check. We need to be lenient for several
 * reasoons, but mainly because on some architectures it's impossible
 * to raise FE_OVERFLOW without raising FE_INEXACT.
 *
 * These are macros instead of functions so that assert provides more
 * meaningful error messages.
 *
 * XXX The volatile here is to avoid gcc's bogus constant folding and work
 *     around the lack of support for the FENV_ACCESS pragma.
 */
#define	test(func, x, result, exceptmask, excepts)	do {		\
	volatile long double _d = x;					\
	assert(feclearexcept(FE_ALL_EXCEPT) == 0);			\
	assert(fpequal((func)(_d), (result)));				 \
	assert(((void)(func), fetestexcept(exceptmask) == (excepts)));	\
} while (0)

/* Test all the functions that compute b^x. */
#define	_testall0(x, result, exceptmask, excepts)	do {		\
	test(exp, x, result, exceptmask, excepts);			\
	test(expf, x, result, exceptmask, excepts);			\
	test(exp2, x, result, exceptmask, excepts);			\
	test(exp2f, x, result, exceptmask, excepts);			\
} while (0)

/* Skip over exp2l on platforms that don't support it. */
#if LDBL_PREC == 53
#define	testall0	_testall0
#else
#define	testall0(x, result, exceptmask, excepts)	do {		\
	_testall0(x, result, exceptmask, excepts); 			\
	test(exp2l, x, result, exceptmask, excepts);			\
} while (0)
#endif

/* Test all the functions that compute b^x - 1. */
#define	testall1(x, result, exceptmask, excepts)	do {		\
	test(expm1, x, result, exceptmask, excepts);			\
	test(expm1f, x, result, exceptmask, excepts);			\
} while (0)

static void
run_generic_tests(void)
{

	/* exp(0) == 1, no exceptions raised */
	testall0(0.0, 1.0, ALL_STD_EXCEPT, 0);
	testall1(0.0, 0.0, ALL_STD_EXCEPT, 0);
	testall0(-0.0, 1.0, ALL_STD_EXCEPT, 0);
	testall1(-0.0, -0.0, ALL_STD_EXCEPT, 0);

	/* exp(NaN) == NaN, no exceptions raised */
	testall0(NAN, NAN, ALL_STD_EXCEPT, 0);
	testall1(NAN, NAN, ALL_STD_EXCEPT, 0);

	/* exp(Inf) == Inf, no exceptions raised */
	testall0(INFINITY, INFINITY, ALL_STD_EXCEPT, 0);
	testall1(INFINITY, INFINITY, ALL_STD_EXCEPT, 0);

	/* exp(-Inf) == 0, no exceptions raised */
	testall0(-INFINITY, 0.0, ALL_STD_EXCEPT, 0);
	testall1(-INFINITY, -1.0, ALL_STD_EXCEPT, 0);

#if !defined(__i386__)
	/* exp(big) == Inf, overflow exception */
	testall0(50000.0, INFINITY, ALL_STD_EXCEPT & ~FE_INEXACT, FE_OVERFLOW);
	testall1(50000.0, INFINITY, ALL_STD_EXCEPT & ~FE_INEXACT, FE_OVERFLOW);

	/* exp(small) == 0, underflow and inexact exceptions */
	testall0(-50000.0, 0.0, ALL_STD_EXCEPT, FE_UNDERFLOW | FE_INEXACT);
#endif
	testall1(-50000.0, -1.0, ALL_STD_EXCEPT, FE_INEXACT);
}

static void
run_exp2_tests(void)
{
	unsigned i;

	/*
	 * We should insist that exp2() return exactly the correct
	 * result and not raise an inexact exception for integer
	 * arguments.
	 */
	feclearexcept(FE_ALL_EXCEPT);
	for (i = FLT_MIN_EXP - FLT_MANT_DIG; i < FLT_MAX_EXP; i++) {
		assert(exp2f(i) == ldexpf(1.0, i));
		assert(fetestexcept(ALL_STD_EXCEPT) == 0);
	}
	for (i = DBL_MIN_EXP - DBL_MANT_DIG; i < DBL_MAX_EXP; i++) {
		assert(exp2(i) == ldexp(1.0, i));
		assert(fetestexcept(ALL_STD_EXCEPT) == 0);
	}
	for (i = LDBL_MIN_EXP - LDBL_MANT_DIG; i < LDBL_MAX_EXP; i++) {
		assert(exp2l(i) == ldexpl(1.0, i));
		assert(fetestexcept(ALL_STD_EXCEPT) == 0);
	}
}

int
main(void)
{

	printf("1..3\n");

	run_generic_tests();
	printf("ok 1 - exponential\n");

#ifdef __i386__
	fpsetprec(FP_PE);
	run_generic_tests();
#endif
	printf("ok 2 - exponential\n");

	run_exp2_tests();
	printf("ok 3 - exponential\n");

	return (0);
}
