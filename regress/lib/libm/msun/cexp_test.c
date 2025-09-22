/*	$OpenBSD: cexp_test.c,v 1.3 2021/12/13 18:04:28 deraadt Exp $	*/
/*-
 * Copyright (c) 2008-2011 David Schultz <das@FreeBSD.org>
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

#include "macros.h"

/*
 * Tests for corner cases in cexp*().
 */

#include <sys/types.h>

#include <complex.h>
#include <fenv.h>
#include <float.h>
#include <math.h>
#include <stdio.h>

#include "test-utils.h"

#pragma STDC FENV_ACCESS	ON
#pragma	STDC CX_LIMITED_RANGE	OFF

/*
 * Test that a function returns the correct value and sets the
 * exception flags correctly. The exceptmask specifies which
 * exceptions we should check. We need to be lenient for several
 * reasons, but mainly because on some architectures it's impossible
 * to raise FE_OVERFLOW without raising FE_INEXACT. In some cases,
 * whether cexp() raises an invalid exception is unspecified.
 *
 * These are macros instead of functions so that assert provides more
 * meaningful error messages.
 *
 * XXX The volatile here is to avoid gcc's bogus constant folding and work
 *     around the lack of support for the FENV_ACCESS pragma.
 */
#define	test_t(type, func, z, result, exceptmask, excepts, checksign)	\
do {									\
	volatile long double complex _d = z;				\
	volatile type complex _r = result;				\
	ATF_REQUIRE_EQ(0, feclearexcept(FE_ALL_EXCEPT));		\
	CHECK_CFPEQUAL_CS((func)(_d), (_r), (checksign));		\
	CHECK_FP_EXCEPTIONS_MSG(excepts, exceptmask, "for %s(%s)",	\
	    #func, #z);							\
} while (0)

#define	test(func, z, result, exceptmask, excepts, checksign)		\
	test_t(double, func, z, result, exceptmask, excepts, checksign)

#define	test_f(func, z, result, exceptmask, excepts, checksign)		\
	test_t(float, func, z, result, exceptmask, excepts, checksign)

#define	test_l(func, z, result, exceptmask, excepts, checksign)		\
	test_t(long double, func, z, result, exceptmask, excepts,	\
	    checksign)
/* Test within a given tolerance. */
#define	test_tol(func, z, result, tol)	do {			\
	CHECK_CFPEQUAL_TOL((func)(z), (result), (tol),		\
	    FPE_ABS_ZERO | CS_BOTH);				\
} while (0)

/* Test all the functions that compute cexp(x). */
#define	testall(x, result, exceptmask, excepts, checksign)	do {	\
	test(cexp, x, result, exceptmask, excepts, checksign);		\
	test_f(cexpf, x, result, exceptmask, excepts, checksign);	\
	test_l(cexpl, x, result, exceptmask, excepts, checksign);	\
} while (0)

/*
 * Test all the functions that compute cexp(x), within a given tolerance.
 * The tolerance is specified in ulps.
 */
#define	testall_tol(x, result, tol)				do {	\
	test_tol(cexp, x, result, tol * DBL_ULP());			\
	test_tol(cexpf, x, result, tol * FLT_ULP());			\
} while (0)

/* Various finite non-zero numbers to test. */
static const float finites[] =
{ -42.0e20, -1.0, -1.0e-10, -0.0, 0.0, 1.0e-10, 1.0, 42.0e20 };


/* Tests for 0 */
ATF_TC_WITHOUT_HEAD(zero);
ATF_TC_BODY(zero, tc)
{

	/* cexp(0) = 1, no exceptions raised */
	testall(0.0, 1.0, ALL_STD_EXCEPT, 0, 1);
	testall(-0.0, 1.0, ALL_STD_EXCEPT, 0, 1);
	testall(CMPLXL(0.0, -0.0), CMPLXL(1.0, -0.0), ALL_STD_EXCEPT, 0, 1);
	testall(CMPLXL(-0.0, -0.0), CMPLXL(1.0, -0.0), ALL_STD_EXCEPT, 0, 1);
}

/*
 * Tests for NaN.  The signs of the results are indeterminate unless the
 * imaginary part is 0.
 */
ATF_TC_WITHOUT_HEAD(nan);
ATF_TC_BODY(nan, tc)
{
	unsigned i;

	/* cexp(x + NaNi) = NaN + NaNi and optionally raises invalid */
	/* cexp(NaN + yi) = NaN + NaNi and optionally raises invalid (|y|>0) */
	for (i = 0; i < nitems(finites); i++) {
		testall(CMPLXL(finites[i], NAN), CMPLXL(NAN, NAN),
			ALL_STD_EXCEPT & ~FE_INVALID, 0, 0);
		if (finites[i] == 0.0)
			continue;
#ifndef __OpenBSD__
		/* XXX FE_INEXACT shouldn't be raised here */
		testall(CMPLXL(NAN, finites[i]), CMPLXL(NAN, NAN),
			ALL_STD_EXCEPT & ~(FE_INVALID | FE_INEXACT), 0, 0);
#else
		testall(CMPLXL(NAN, finites[i]), CMPLXL(NAN, NAN),
			ALL_STD_EXCEPT & ~(FE_INVALID), 0, 0);
#endif
	}

	/* cexp(NaN +- 0i) = NaN +- 0i */
	testall(CMPLXL(NAN, 0.0), CMPLXL(NAN, 0.0), ALL_STD_EXCEPT, 0, 1);
	testall(CMPLXL(NAN, -0.0), CMPLXL(NAN, -0.0), ALL_STD_EXCEPT, 0, 1);

	/* cexp(inf + NaN i) = inf + nan i */
	testall(CMPLXL(INFINITY, NAN), CMPLXL(INFINITY, NAN),
		ALL_STD_EXCEPT, 0, 0);
	/* cexp(-inf + NaN i) = 0 */
	testall(CMPLXL(-INFINITY, NAN), CMPLXL(0.0, 0.0),
		ALL_STD_EXCEPT, 0, 0);
	/* cexp(NaN + NaN i) = NaN + NaN i */
	testall(CMPLXL(NAN, NAN), CMPLXL(NAN, NAN),
		ALL_STD_EXCEPT, 0, 0);
}

ATF_TC_WITHOUT_HEAD(inf);
ATF_TC_BODY(inf, tc)
{
	unsigned i;

	/* cexp(x + inf i) = NaN + NaNi and raises invalid */
	for (i = 0; i < nitems(finites); i++) {
		testall(CMPLXL(finites[i], INFINITY), CMPLXL(NAN, NAN),
			ALL_STD_EXCEPT, FE_INVALID, 1);
	}
	/* cexp(-inf + yi) = 0 * (cos(y) + sin(y)i) */
	/* XXX shouldn't raise an inexact exception */
	testall(CMPLXL(-INFINITY, M_PI_4), CMPLXL(0.0, 0.0),
		ALL_STD_EXCEPT & ~FE_INEXACT, 0, 1);
	testall(CMPLXL(-INFINITY, 3 * M_PI_4), CMPLXL(-0.0, 0.0),
		ALL_STD_EXCEPT & ~FE_INEXACT, 0, 1);
	testall(CMPLXL(-INFINITY, 5 * M_PI_4), CMPLXL(-0.0, -0.0),
		ALL_STD_EXCEPT & ~FE_INEXACT, 0, 1);
	testall(CMPLXL(-INFINITY, 7 * M_PI_4), CMPLXL(0.0, -0.0),
		ALL_STD_EXCEPT & ~FE_INEXACT, 0, 1);
	testall(CMPLXL(-INFINITY, 0.0), CMPLXL(0.0, 0.0),
		ALL_STD_EXCEPT, 0, 1);
	testall(CMPLXL(-INFINITY, -0.0), CMPLXL(0.0, -0.0),
		ALL_STD_EXCEPT, 0, 1);
	/* cexp(inf + yi) = inf * (cos(y) + sin(y)i) (except y=0) */
	/* XXX shouldn't raise an inexact exception */
	testall(CMPLXL(INFINITY, M_PI_4), CMPLXL(INFINITY, INFINITY),
		ALL_STD_EXCEPT & ~FE_INEXACT, 0, 1);
	testall(CMPLXL(INFINITY, 3 * M_PI_4), CMPLXL(-INFINITY, INFINITY),
		ALL_STD_EXCEPT & ~FE_INEXACT, 0, 1);
	testall(CMPLXL(INFINITY, 5 * M_PI_4), CMPLXL(-INFINITY, -INFINITY),
		ALL_STD_EXCEPT & ~FE_INEXACT, 0, 1);
	testall(CMPLXL(INFINITY, 7 * M_PI_4), CMPLXL(INFINITY, -INFINITY),
		ALL_STD_EXCEPT & ~FE_INEXACT, 0, 1);
	/* cexp(inf + 0i) = inf + 0i */
	testall(CMPLXL(INFINITY, 0.0), CMPLXL(INFINITY, 0.0),
		ALL_STD_EXCEPT, 0, 1);
	testall(CMPLXL(INFINITY, -0.0), CMPLXL(INFINITY, -0.0),
		ALL_STD_EXCEPT, 0, 1);
}

ATF_TC_WITHOUT_HEAD(reals);
ATF_TC_BODY(reals, tc)
{
	unsigned i;

	for (i = 0; i < nitems(finites); i++) {
		/* XXX could check exceptions more meticulously */
		test(cexp, CMPLXL(finites[i], 0.0),
		     CMPLXL(exp(finites[i]), 0.0),
		     FE_INVALID | FE_DIVBYZERO, 0, 1);
		test(cexp, CMPLXL(finites[i], -0.0),
		     CMPLXL(exp(finites[i]), -0.0),
		     FE_INVALID | FE_DIVBYZERO, 0, 1);
		test_f(cexpf, CMPLXL(finites[i], 0.0),
		     CMPLXL(expf(finites[i]), 0.0),
		     FE_INVALID | FE_DIVBYZERO, 0, 1);
		test_f(cexpf, CMPLXL(finites[i], -0.0),
		     CMPLXL(expf(finites[i]), -0.0),
		     FE_INVALID | FE_DIVBYZERO, 0, 1);
	}
}

ATF_TC_WITHOUT_HEAD(imaginaries);
ATF_TC_BODY(imaginaries, tc)
{
	unsigned i;

	for (i = 0; i < nitems(finites); i++) {
		test(cexp, CMPLXL(0.0, finites[i]),
		     CMPLXL(cos(finites[i]), sin(finites[i])),
		     ALL_STD_EXCEPT & ~FE_INEXACT, 0, 1);
		test(cexp, CMPLXL(-0.0, finites[i]),
		     CMPLXL(cos(finites[i]), sin(finites[i])),
		     ALL_STD_EXCEPT & ~FE_INEXACT, 0, 1);
		test_f(cexpf, CMPLXL(0.0, finites[i]),
		     CMPLXL(cosf(finites[i]), sinf(finites[i])),
		     ALL_STD_EXCEPT & ~FE_INEXACT, 0, 1);
		test_f(cexpf, CMPLXL(-0.0, finites[i]),
		     CMPLXL(cosf(finites[i]), sinf(finites[i])),
		     ALL_STD_EXCEPT & ~FE_INEXACT, 0, 1);
	}
}

ATF_TC_WITHOUT_HEAD(small);
ATF_TC_BODY(small, tc)
{
	static const double tests[] = {
	     /* csqrt(a + bI) = x + yI */
	     /* a	b	x			y */
		 1.0,	M_PI_4,	M_SQRT2 * 0.5 * M_E,	M_SQRT2 * 0.5 * M_E,
		-1.0,	M_PI_4,	M_SQRT2 * 0.5 / M_E,	M_SQRT2 * 0.5 / M_E,
		 2.0,	M_PI_2,	0.0,			M_E * M_E,
		 M_LN2,	M_PI,	-2.0,			0.0,
	};
	double a, b;
	double x, y;
	unsigned i;

	for (i = 0; i < nitems(tests); i += 4) {
		a = tests[i];
		b = tests[i + 1];
		x = tests[i + 2];
		y = tests[i + 3];
		test_tol(cexp, CMPLXL(a, b), CMPLXL(x, y), 3 * DBL_ULP());

		/* float doesn't have enough precision to pass these tests */
		if (x == 0 || y == 0)
			continue;
		test_tol(cexpf, CMPLXL(a, b), CMPLXL(x, y), 1 * FLT_ULP());
        }
}

/* Test inputs with a real part r that would overflow exp(r). */
ATF_TC_WITHOUT_HEAD(large);
ATF_TC_BODY(large, tc)
{

	test_tol(cexp, CMPLXL(709.79, 0x1p-1074),
		 CMPLXL(INFINITY, 8.94674309915433533273e-16), DBL_ULP());
	test_tol(cexp, CMPLXL(1000, 0x1p-1074),
		 CMPLXL(INFINITY, 9.73344457300016401328e+110), DBL_ULP());
	test_tol(cexp, CMPLXL(1400, 0x1p-1074),
		 CMPLXL(INFINITY, 5.08228858149196559681e+284), DBL_ULP());
	test_tol(cexp, CMPLXL(900, 0x1.23456789abcdep-1020),
		 CMPLXL(INFINITY, 7.42156649354218408074e+83), DBL_ULP());
	test_tol(cexp, CMPLXL(1300, 0x1.23456789abcdep-1020),
		 CMPLXL(INFINITY, 3.87514844965996756704e+257), DBL_ULP());

	test_tol(cexpf, CMPLXL(88.73, 0x1p-149),
		 CMPLXL(INFINITY, 4.80265603e-07), 2 * FLT_ULP());
	test_tol(cexpf, CMPLXL(90, 0x1p-149),
		 CMPLXL(INFINITY, 1.7101492622e-06f), 2 * FLT_ULP());
	test_tol(cexpf, CMPLXL(192, 0x1p-149),
		 CMPLXL(INFINITY, 3.396809344e+38f), 2 * FLT_ULP());
	test_tol(cexpf, CMPLXL(120, 0x1.234568p-120),
		 CMPLXL(INFINITY, 1.1163382522e+16f), 2 * FLT_ULP());
	test_tol(cexpf, CMPLXL(170, 0x1.234568p-120),
		 CMPLXL(INFINITY, 5.7878851079e+37f), 2 * FLT_ULP());
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, zero);
	ATF_TP_ADD_TC(tp, nan);
	ATF_TP_ADD_TC(tp, inf);
	ATF_TP_ADD_TC(tp, reals);
	ATF_TP_ADD_TC(tp, imaginaries);
	ATF_TP_ADD_TC(tp, small);
	ATF_TP_ADD_TC(tp, large);

	return (atf_no_error());
}
