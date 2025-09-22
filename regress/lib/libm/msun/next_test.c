/*	$OpenBSD: next_test.c,v 1.2 2021/12/13 18:04:28 deraadt Exp $	*/
/*-
 * Copyright (c) 2005 David Schultz <das@FreeBSD.org>
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
 * Test the correctness of nextafter{,f,l} and nexttoward{,f,l}.
 */

#include <fenv.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef	__i386__
#include <ieeefp.h>
#endif

#include "test-utils.h"

#define	test(exp, ans, ex)	do {			\
	double __ans = (ans);				\
	feclearexcept(ALL_STD_EXCEPT);			\
	_testl(#exp, __LINE__, (exp), __ans, (ex));	\
} while (0)
#define	testf(exp, ans, ex)	do {			\
	float __ans = (ans);				\
	feclearexcept(ALL_STD_EXCEPT);			\
	_testl(#exp, __LINE__, (exp), __ans, (ex));	\
} while (0)
#define	testl(exp, ans, ex)	do {			\
	long double __ans = (ans);			\
	feclearexcept(ALL_STD_EXCEPT);			\
	_testl(#exp, __LINE__, (exp), __ans, (ex));	\
} while (0)
#define	testboth(arg1, arg2, ans, ex, prec)	do {			\
	test##prec(nextafter##prec((arg1), (arg2)), (ans), (ex));	\
	test##prec(nexttoward##prec((arg1), (arg2)), (ans), (ex));	\
} while (0)
#define	testall(arg1, arg2, ans, ex)	do {		\
	testboth((arg1), (arg2), (ans), (ex), );	\
	testboth((arg1), (arg2), (ans), (ex), f);	\
	testboth((arg1), (arg2), (ans), (ex), l);	\
} while (0)

static void _testl(const char *, int, long double, long double, int);
static double idd(double);
static float idf(float);

static const int ex_under = FE_UNDERFLOW | FE_INEXACT;	/* shorthand */
static const int ex_over = FE_OVERFLOW | FE_INEXACT;
static const long double ldbl_eps = LDBL_EPSILON;



ATF_TC_WITHOUT_HEAD(zeros);
ATF_TC_BODY(zeros, tc)
{
	long double ldbl_small;

#ifndef	__OpenBSD__
#ifdef	__i386__
	fpsetprec(FP_PE);
#endif
#endif
	/*
	 * We can't use a compile-time constant here because gcc on
	 * FreeBSD/i386 assumes long doubles are truncated to the
	 * double format.
	 */
	ldbl_small = ldexpl(1.0, LDBL_MIN_EXP - LDBL_MANT_DIG);

	/*
	 * Special cases involving zeroes.
	 */
#define	ztest(prec)							      \
	test##prec(copysign##prec(1.0, nextafter##prec(0.0, -0.0)), -1.0, 0); \
	test##prec(copysign##prec(1.0, nextafter##prec(-0.0, 0.0)), 1.0, 0);  \
	test##prec(copysign##prec(1.0, nexttoward##prec(0.0, -0.0)), -1.0, 0);\
	test##prec(copysign##prec(1.0, nexttoward##prec(-0.0, 0.0)), 1.0, 0)

	ztest();
	ztest(f);
	ztest(l);
#undef	ztest

#define	stest(next, eps, prec)					\
	test##prec(next(-0.0, 42.0), eps, ex_under);		\
	test##prec(next(0.0, -42.0), -eps, ex_under);		\
	test##prec(next(0.0, INFINITY), eps, ex_under);		\
	test##prec(next(-0.0, -INFINITY), -eps, ex_under)

	stest(nextafter, 0x1p-1074, );
	stest(nextafterf, 0x1p-149f, f);
	stest(nextafterl, ldbl_small, l);
	stest(nexttoward, 0x1p-1074, );
	stest(nexttowardf, 0x1p-149f, f);
	stest(nexttowardl, ldbl_small, l);
#undef	stest
}

ATF_TC_WITHOUT_HEAD(eq_and_nan);
ATF_TC_BODY(eq_and_nan, tc)
{
	/*
	 * `x == y' and NaN tests
	 */
	testall(42.0, 42.0, 42.0, 0);
	testall(-42.0, -42.0, -42.0, 0);
	testall(INFINITY, INFINITY, INFINITY, 0);
	testall(-INFINITY, -INFINITY, -INFINITY, 0);
	testall(NAN, 42.0, NAN, 0);
	testall(42.0, NAN, NAN, 0);
	testall(NAN, NAN, NAN, 0);
}

ATF_TC_WITHOUT_HEAD(ordinary);
ATF_TC_BODY(ordinary, tc)
{
	/*
	 * Tests where x is an ordinary normalized number
	 */
	testboth(1.0, 2.0, 1.0 + DBL_EPSILON, 0, );
	testboth(1.0, -INFINITY, 1.0 - DBL_EPSILON / 2, 0, );
	testboth(1.0, 2.0, 1.0 + FLT_EPSILON, 0, f);
	testboth(1.0, -INFINITY, 1.0 - FLT_EPSILON / 2, 0, f);
	testboth(1.0, 2.0, 1.0 + ldbl_eps, 0, l);
	testboth(1.0, -INFINITY, 1.0 - ldbl_eps / 2, 0, l);

	testboth(-1.0, 2.0, -1.0 + DBL_EPSILON / 2, 0, );
	testboth(-1.0, -INFINITY, -1.0 - DBL_EPSILON, 0, );
	testboth(-1.0, 2.0, -1.0 + FLT_EPSILON / 2, 0, f);
	testboth(-1.0, -INFINITY, -1.0 - FLT_EPSILON, 0, f);
	testboth(-1.0, 2.0, -1.0 + ldbl_eps / 2, 0, l);
	testboth(-1.0, -INFINITY, -1.0 - ldbl_eps, 0, l);

	/* Cases where nextafter(...) != nexttoward(...) */
	test(nexttoward(1.0, 1.0 + ldbl_eps), 1.0 + DBL_EPSILON, 0);
	testf(nexttowardf(1.0, 1.0 + ldbl_eps), 1.0 + FLT_EPSILON, 0);
	testl(nexttowardl(1.0, 1.0 + ldbl_eps), 1.0 + ldbl_eps, 0);
}

ATF_TC_WITHOUT_HEAD(boundaries);
ATF_TC_BODY(boundaries, tc)
{
	/*
	 * Tests at word boundaries, normalization boundaries, etc.
	 */
	testboth(0x1.87654ffffffffp+0, INFINITY, 0x1.87655p+0, 0, );
	testboth(0x1.87655p+0, -INFINITY, 0x1.87654ffffffffp+0, 0, );
	testboth(0x1.fffffffffffffp+0, INFINITY, 0x1p1, 0, );
	testboth(0x1p1, -INFINITY, 0x1.fffffffffffffp+0, 0, );
	testboth(0x0.fffffffffffffp-1022, INFINITY, 0x1p-1022, 0, );
	testboth(0x1p-1022, -INFINITY, 0x0.fffffffffffffp-1022, ex_under, );

	testboth(0x1.fffffep0f, INFINITY, 0x1p1, 0, f);
	testboth(0x1p1, -INFINITY, 0x1.fffffep0f, 0, f);
	testboth(0x0.fffffep-126f, INFINITY, 0x1p-126f, 0, f);
	testboth(0x1p-126f, -INFINITY, 0x0.fffffep-126f, ex_under, f);

#if LDBL_MANT_DIG == 53
	testboth(0x1.87654ffffffffp+0L, INFINITY, 0x1.87655p+0L, 0, l);
	testboth(0x1.87655p+0L, -INFINITY, 0x1.87654ffffffffp+0L, 0, l);
	testboth(0x1.fffffffffffffp+0L, INFINITY, 0x1p1L, 0, l);
	testboth(0x1p1L, -INFINITY, 0x1.fffffffffffffp+0L, 0, l);
	testboth(0x0.fffffffffffffp-1022L, INFINITY, 0x1p-1022L, 0, l);
	testboth(0x1p-1022L, -INFINITY, 0x0.fffffffffffffp-1022L, ex_under, l);
#elif LDBL_MANT_DIG == 64 && !defined(__i386)
	testboth(0x1.87654321fffffffep+0L, INFINITY, 0x1.87654322p+0L, 0, l);
	testboth(0x1.87654322p+0L, -INFINITY, 0x1.87654321fffffffep+0L, 0, l);
	testboth(0x1.fffffffffffffffep0L, INFINITY, 0x1p1L, 0, l);
	testboth(0x1p1L, -INFINITY, 0x1.fffffffffffffffep0L, 0, l);
	testboth(0x0.fffffffffffffffep-16382L, INFINITY, 0x1p-16382L, 0, l);
	testboth(0x1p-16382L, -INFINITY,
	    0x0.fffffffffffffffep-16382L, ex_under, l);
#elif LDBL_MANT_DIG == 113
	testboth(0x1.876543210987ffffffffffffffffp+0L, INFINITY,
	    0x1.876543210988p+0, 0, l);
	testboth(0x1.876543210988p+0L, -INFINITY,
	    0x1.876543210987ffffffffffffffffp+0L, 0, l);
	testboth(0x1.ffffffffffffffffffffffffffffp0L, INFINITY, 0x1p1L, 0, l);
	testboth(0x1p1L, -INFINITY, 0x1.ffffffffffffffffffffffffffffp0L, 0, l);
	testboth(0x0.ffffffffffffffffffffffffffffp-16382L, INFINITY,
	    0x1p-16382L, 0, l);
	testboth(0x1p-16382L, -INFINITY,
	    0x0.ffffffffffffffffffffffffffffp-16382L, ex_under, l);
#endif
}

ATF_TC_WITHOUT_HEAD(overflow);
ATF_TC_BODY(overflow, tc)
{
	long double ldbl_max;
	/*
	 * We can't use a compile-time constant here because gcc on
	 * FreeBSD/i386 assumes long doubles are truncated to the
	 * double format.
	 */
	ldbl_max = ldexpl(1.0 - ldbl_eps / 2, LDBL_MAX_EXP);

	/*
	 * Overflow tests
	 */
	test(idd(nextafter(DBL_MAX, INFINITY)), INFINITY, ex_over);
	test(idd(nextafter(INFINITY, 0.0)), DBL_MAX, 0);
	test(idd(nexttoward(DBL_MAX, DBL_MAX * 2.0L)), INFINITY, ex_over);
#if LDBL_MANT_DIG > 53
	test(idd(nexttoward(INFINITY, DBL_MAX * 2.0L)), DBL_MAX, 0);
#endif

	testf(idf(nextafterf(FLT_MAX, INFINITY)), INFINITY, ex_over);
	testf(idf(nextafterf(INFINITY, 0.0)), FLT_MAX, 0);
	testf(idf(nexttowardf(FLT_MAX, FLT_MAX * 2.0)), INFINITY, ex_over);
	testf(idf(nexttowardf(INFINITY, FLT_MAX * 2.0)), FLT_MAX, 0);

	testboth(ldbl_max, INFINITY, INFINITY, ex_over, l);
	testboth(INFINITY, 0.0, ldbl_max, 0, l);
}

static void
_testl(const char *exp, int line, long double actual, long double expected,
    int except)
{
	int actual_except;

	actual_except = fetestexcept(ALL_STD_EXCEPT);
	if (!fpequal_cs(actual, expected, true)) {
		atf_tc_fail_check(__FILE__, line,
		    "%s returned %La, expecting %La\n", exp, actual, expected);
	}
	if (actual_except != except) {
		atf_tc_fail_check(__FILE__, line,
		    "%s raised 0x%x, expecting 0x%x\n", exp, actual_except,
		    except);
	}
}

/*
 * The idd() and idf() routines ensure that doubles and floats are
 * converted to their respective types instead of stored in the FPU
 * with extra precision.
 */
static double
idd(double x)
{
	return (x);
}

static float
idf(float x)
{
	return (x);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, zeros);
	ATF_TP_ADD_TC(tp, ordinary);
	ATF_TP_ADD_TC(tp, eq_and_nan);
	ATF_TP_ADD_TC(tp, boundaries);
	ATF_TP_ADD_TC(tp, overflow);

	return (atf_no_error());
}
