/*-
 * Copyright (c) 2008-2010 David Schultz <das@FreeBSD.org>
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
 * Tests for corner cases in log*().
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
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

#define	test(func, x, result, exceptmask, excepts)	do {		\
	volatile long double _d = x;					\
	assert(feclearexcept(FE_ALL_EXCEPT) == 0);			\
	assert(fpequal((func)(_d), (result)));				 \
	assert(((void)(func), fetestexcept(exceptmask) == (excepts)));	\
} while (0)

#define	test_tol(func, z, result, tol)			do {		\
	volatile long double _d = z;					\
	debug("  testing %6s(%15La) ~= % .36Le\n", #func, _d, result);	\
	assert(fpequal_tol((func)(_d), (result), (tol), CS_BOTH));	\
} while (0)

/* Test all the functions that compute log(x). */
#define	testall0(x, result, exceptmask, excepts)	do {		\
	test(log, x, result, exceptmask, excepts);			\
	test(logf, x, result, exceptmask, excepts);			\
	test(logl, x, result, exceptmask, excepts);			\
	test(log2, x, result, exceptmask, excepts);			\
	test(log2f, x, result, exceptmask, excepts);			\
	test(log2l, x, result, exceptmask, excepts);			\
	test(log10, x, result, exceptmask, excepts);			\
	test(log10f, x, result, exceptmask, excepts);			\
	test(log10l, x, result, exceptmask, excepts);			\
} while (0)

/* Test all the functions that compute log(1+x). */
#define	testall1(x, result, exceptmask, excepts)	do {		\
	test(log1p, x, result, exceptmask, excepts);			\
	test(log1pf, x, result, exceptmask, excepts);			\
	test(log1pl, x, result, exceptmask, excepts);			\
} while (0)

static void
run_generic_tests(void)
{

	/* log(1) == 0, no exceptions raised */
	testall0(1.0, 0.0, ALL_STD_EXCEPT, 0);
	testall1(0.0, 0.0, ALL_STD_EXCEPT, 0);
	testall1(-0.0, -0.0, ALL_STD_EXCEPT, 0);

	/* log(NaN) == NaN, no exceptions raised */
	testall0(NAN, NAN, ALL_STD_EXCEPT, 0);
	testall1(NAN, NAN, ALL_STD_EXCEPT, 0);

	/* log(Inf) == Inf, no exceptions raised */
	testall0(INFINITY, INFINITY, ALL_STD_EXCEPT, 0);
	testall1(INFINITY, INFINITY, ALL_STD_EXCEPT, 0);

	/* log(x) == NaN for x < 0, invalid exception raised */
	testall0(-INFINITY, NAN, ALL_STD_EXCEPT, FE_INVALID);
	testall1(-INFINITY, NAN, ALL_STD_EXCEPT, FE_INVALID);
	testall0(-1.0, NAN, ALL_STD_EXCEPT, FE_INVALID);
	testall1(-1.5, NAN, ALL_STD_EXCEPT, FE_INVALID);

	/* log(0) == -Inf, divide-by-zero exception */
	testall0(0.0, -INFINITY, ALL_STD_EXCEPT & ~FE_INEXACT, FE_DIVBYZERO);
	testall0(-0.0, -INFINITY, ALL_STD_EXCEPT & ~FE_INEXACT, FE_DIVBYZERO);
	testall1(-1.0, -INFINITY, ALL_STD_EXCEPT & ~FE_INEXACT, FE_DIVBYZERO);
}

static void
run_log2_tests(void)
{
	unsigned i;

	/*
	 * We should insist that log2() return exactly the correct
	 * result and not raise an inexact exception for powers of 2.
	 */
	assert(feclearexcept(FE_ALL_EXCEPT) == 0);
	for (i = FLT_MIN_EXP - FLT_MANT_DIG; i < FLT_MAX_EXP; i++) {
		assert(log2f(ldexpf(1.0, i)) == i);
		assert(fetestexcept(ALL_STD_EXCEPT) == 0);
	}
	for (i = DBL_MIN_EXP - DBL_MANT_DIG; i < DBL_MAX_EXP; i++) {
		assert(log2(ldexp(1.0, i)) == i);
		assert(fetestexcept(ALL_STD_EXCEPT) == 0);
	}
	for (i = LDBL_MIN_EXP - LDBL_MANT_DIG; i < LDBL_MAX_EXP; i++) {
		assert(log2l(ldexpl(1.0, i)) == i);
#if 0
		/* XXX This test does not pass yet. */
		assert(fetestexcept(ALL_STD_EXCEPT) == 0);
#endif
	}
}

static void
run_roundingmode_tests(void)
{

	/*
	 * Corner cases in other rounding modes.
	 */
	fesetround(FE_DOWNWARD);
	/* These are still positive per IEEE 754R */
#if 0
	testall0(1.0, 0.0, ALL_STD_EXCEPT, 0);
#else
	/* logl, log2l, and log10l don't pass yet. */
	test(log, 1.0, 0.0, ALL_STD_EXCEPT, 0);
	test(logf, 1.0, 0.0, ALL_STD_EXCEPT, 0);
	test(log2, 1.0, 0.0, ALL_STD_EXCEPT, 0);
	test(log2f, 1.0, 0.0, ALL_STD_EXCEPT, 0);
	test(log10, 1.0, 0.0, ALL_STD_EXCEPT, 0);
	test(log10f, 1.0, 0.0, ALL_STD_EXCEPT, 0);
#endif
	testall1(0.0, 0.0, ALL_STD_EXCEPT, 0);
	fesetround(FE_TOWARDZERO);
	testall0(1.0, 0.0, ALL_STD_EXCEPT, 0);
	testall1(0.0, 0.0, ALL_STD_EXCEPT, 0);

	fesetround(FE_UPWARD);
	testall0(1.0, 0.0, ALL_STD_EXCEPT, 0);
	testall1(0.0, 0.0, ALL_STD_EXCEPT, 0);
	/* log1p(-0.0) == -0.0 even when rounding upwards */
	testall1(-0.0, -0.0, ALL_STD_EXCEPT, 0);

	fesetround(FE_TONEAREST);
}

static void
run_accuracy_tests(void)
{
	static const struct {
		float x;
		long double log2x;
		long double logex;
		long double log10x;
        } tests[] = {
		{  0x1p-120 + 0x1p-140,
		  -1.19999998624139449158861798943319717e2L,
		  -8.31776607135195754708796206665656732e1L,
		  -3.61235990655024477716980559136055915e1L,
		},
		{  1.0 - 0x1p-20,
		  -1.37586186296463416424364914705656460e-6L,
		  -9.53674771153890007250243736279163253e-7L,
		  -4.14175690642480911859354110516159131e-7L, },
		{  1.0 + 0x1p-20,
		   1.37586055084113820105668028340371476e-6L,
		   9.53673861659188233908415514963336144e-7L,
		   4.14175295653950611453333571759200697e-7L },
		{  19.75,
		   4.30378074817710292442728634194115348e0L,
		   2.98315349134713087533848129856505779e0L,
		   1.29556709996247903756734359702926363e0L },
		{  19.75 * 0x1p100,
		   1.043037807481771029244272863419411534e2L,
		   7.229787154734166181706169344438271459e1L,
		   3.139856666636059855894123306947856631e1L },
	};
        unsigned i;

	for (i = 0; i < nitems(tests); i++) {
		test_tol(log2, tests[i].x, tests[i].log2x, DBL_ULP());
		test_tol(log2f, tests[i].x, tests[i].log2x, FLT_ULP());
		test_tol(log2l, tests[i].x, tests[i].log2x, LDBL_ULP());
		test_tol(log, tests[i].x, tests[i].logex, DBL_ULP());
		test_tol(logf, tests[i].x, tests[i].logex, FLT_ULP());
		test_tol(logl, tests[i].x, tests[i].logex, LDBL_ULP());
		test_tol(log10, tests[i].x, tests[i].log10x, DBL_ULP());
		test_tol(log10f, tests[i].x, tests[i].log10x, FLT_ULP());
		test_tol(log10l, tests[i].x, tests[i].log10x, LDBL_ULP());
		if (tests[i].x >= 0.5) {
			test_tol(log1p, tests[i].x - 1, tests[i].logex,
				 DBL_ULP());
			test_tol(log1pf, tests[i].x - 1, tests[i].logex,
				 FLT_ULP());
			test_tol(log1pl, tests[i].x - 1, tests[i].logex,
				 LDBL_ULP());
		}
	}
}

static void
run_log1p_accuracy_tests(void)
{

	test_tol(log1pf, 0x0.333333p0F,
		 1.82321546859847114303367992804596800640e-1L, FLT_ULP());
	test_tol(log1p, 0x0.3333333333333p0,
		 1.82321556793954589204283870982629267635e-1L, DBL_ULP());
	test_tol(log1pl, 0x0.33333333333333332p0L,
		 1.82321556793954626202683007050468762914e-1L, LDBL_ULP());

	test_tol(log1pf, -0x0.333333p0F,
		 -2.23143536413048672940940199918017467652e-1L, FLT_ULP());
	test_tol(log1p, -0x0.3333333333333p0,
		 -2.23143551314209700255143859052009022937e-1L, DBL_ULP());
	test_tol(log1pl, -0x0.33333333333333332p0L,
		 -2.23143551314209755752742563153765697950e-1L, LDBL_ULP());
}

int
main(void)
{

	printf("1..5\n");

	run_generic_tests();
	printf("ok 1 - logarithm\n");

	run_log2_tests();
	printf("ok 2 - logarithm\n");

	run_roundingmode_tests();
	printf("ok 3 - logarithm\n");

	run_accuracy_tests();
	printf("ok 4 - logarithm\n");

	run_log1p_accuracy_tests();
	printf("ok 5 - logarithm\n");

	return (0);
}
