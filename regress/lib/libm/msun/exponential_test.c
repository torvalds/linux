/*	$OpenBSD: exponential_test.c,v 1.3 2021/12/13 18:04:28 deraadt Exp $	*/
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

#include "macros.h"

/*
 * Tests for corner cases in exp*().
 */

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
	ATF_REQUIRE_EQ(0, feclearexcept(FE_ALL_EXCEPT));		\
	CHECK_FPEQUAL((func)(_d), (result));			\
	CHECK_FP_EXCEPTIONS_MSG(excepts, exceptmask, "for %s(%s)",	\
	    #func, #x);							\
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


/*
 * We should insist that exp2() return exactly the correct
 * result and not raise an inexact exception for integer
 * arguments.
 */
ATF_TC_WITHOUT_HEAD(exp2f);
ATF_TC_BODY(exp2f, tc)
{
	int i;
	ATF_REQUIRE_EQ(0, feclearexcept(FE_ALL_EXCEPT));
	for (i = FLT_MIN_EXP - FLT_MANT_DIG; i < FLT_MAX_EXP; i++) {
		ATF_CHECK_EQ(exp2f(i), ldexpf(1.0, i));
		CHECK_FP_EXCEPTIONS(0, ALL_STD_EXCEPT);
	}
}

ATF_TC_WITHOUT_HEAD(exp2);
ATF_TC_BODY(exp2, tc)
{
	int i;
	ATF_REQUIRE_EQ(0, feclearexcept(FE_ALL_EXCEPT));
	for (i = DBL_MIN_EXP - DBL_MANT_DIG; i < DBL_MAX_EXP; i++) {
		ATF_CHECK_EQ(exp2(i), ldexp(1.0, i));
		CHECK_FP_EXCEPTIONS(0, ALL_STD_EXCEPT);
	}
}

ATF_TC_WITHOUT_HEAD(exp2l);
ATF_TC_BODY(exp2l, tc)
{
	int i;
	ATF_REQUIRE_EQ(0, feclearexcept(FE_ALL_EXCEPT));
	for (i = LDBL_MIN_EXP - LDBL_MANT_DIG; i < LDBL_MAX_EXP; i++) {
		ATF_CHECK_EQ(exp2l(i), ldexpl(1.0, i));
		CHECK_FP_EXCEPTIONS(0, ALL_STD_EXCEPT);
	}
}

ATF_TC_WITHOUT_HEAD(generic);
ATF_TC_BODY(generic, tc)
{
	run_generic_tests();
}

#ifndef __OpenBSD__
#ifdef __i386__
ATF_TC_WITHOUT_HEAD(generic_fp_pe);
ATF_TC_BODY(generic_fp_pe, tc)
{
	fpsetprec(FP_PE);
	run_generic_tests();
}
#endif
#endif

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, generic);
#ifndef __OpenBSD__
#ifdef __i386__
	ATF_TP_ADD_TC(tp, generic_fp_pe);
#endif
#endif
	ATF_TP_ADD_TC(tp, exp2);
	ATF_TP_ADD_TC(tp, exp2f);
	ATF_TP_ADD_TC(tp, exp2l);

	return (atf_no_error());
}
