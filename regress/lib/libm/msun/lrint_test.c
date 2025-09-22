/*	$OpenBSD: lrint_test.c,v 1.5 2021/12/13 18:04:28 deraadt Exp $	*/
/*-
 * Copyright (c) 2005-2008 David Schultz <das@FreeBSD.org>
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
 * Test for lrint(), lrintf(), llrint(), and llrintf().
 */

#include <fenv.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>

#include "test-utils.h"

#define	test(func, x, result, excepts)	do {					\
	ATF_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);				\
	long long _r = (func)(x);						\
	ATF_CHECK_MSG(_r == (result) || fetestexcept(FE_INVALID),		\
	    #func "(%Lg) returned %lld, expected %lld", (long double)x, _r,	\
	    (long long)(result));						\
	CHECK_FP_EXCEPTIONS_MSG(excepts, FE_ALL_EXCEPT & ALL_STD_EXCEPT,	\
	    "for %s(%s)", #func, #x);						\
} while (0)

#define	testall(x, result, excepts)	do {				\
	test(lrint, x, result, excepts);				\
	test(lrintf, x, result, excepts);				\
	test(lrintl, x, result, excepts);				\
	test(llrint, x, result, excepts);				\
	test(llrintf, x, result, excepts);				\
	test(llrintl, x, result, excepts);				\
} while (0)

#define	IGNORE	0

#pragma STDC FENV_ACCESS ON

static void
run_tests(void)
{
	ATF_REQUIRE_EQ(0, fesetround(FE_DOWNWARD));
	testall(0.75, 0, FE_INEXACT);
	testall(-0.5, -1, FE_INEXACT);

	ATF_REQUIRE_EQ(0, fesetround(FE_TONEAREST));
	testall(0.0, 0, 0);
	testall(0.25, 0, FE_INEXACT);
	testall(0.5, 0, FE_INEXACT);
	testall(-2.5, -2, FE_INEXACT);
	testall(1.0, 1, 0);
	testall(0x12345000p0, 0x12345000, 0);
	testall(0x1234.fp0, 0x1235, FE_INEXACT);
	testall(INFINITY, IGNORE, FE_INVALID);
	testall(NAN, IGNORE, FE_INVALID);

#if (LONG_MAX == 0x7fffffffl)
	ATF_REQUIRE_EQ(0, fesetround(FE_UPWARD));
	test(lrint, 0x7fffffff.8p0, IGNORE, FE_INVALID);
	test(lrint, -0x80000000.4p0, (long)-0x80000000l, FE_INEXACT);

	ATF_REQUIRE_EQ(0, fesetround(FE_DOWNWARD));
	test(lrint, -0x80000000.8p0, IGNORE, FE_INVALID);
	test(lrint, 0x80000000.0p0, IGNORE, FE_INVALID);
	test(lrint, 0x7fffffff.4p0, 0x7fffffffl, FE_INEXACT);
	test(lrintf, 0x80000000.0p0f, IGNORE, FE_INVALID);
	test(lrintf, 0x7fffff80.0p0f, 0x7fffff80l, 0);

	ATF_REQUIRE_EQ(0, fesetround(FE_TOWARDZERO));
	test(lrint, 0x7fffffff.8p0,  0x7fffffffl, FE_INEXACT);
	test(lrint, -0x80000000.8p0, -0x80000000l, FE_INEXACT);
	test(lrint, 0x80000000.0p0, IGNORE, FE_INVALID);
	test(lrintf, 0x80000000.0p0f, IGNORE, FE_INVALID);
	test(lrintf, 0x7fffff80.0p0f, 0x7fffff80l, 0);
#elif (LONG_MAX == 0x7fffffffffffffffll)
	ATF_REQUIRE_EQ(0, fesetround(FE_TONEAREST));
	test(lrint, 0x8000000000000000.0p0, IGNORE, FE_INVALID);
	test(lrintf, 0x8000000000000000.0p0f, IGNORE, FE_INVALID);
	test(lrint, 0x7ffffffffffffc00.0p0, 0x7ffffffffffffc00l, 0);
	test(lrintf, 0x7fffff8000000000.0p0f, 0x7fffff8000000000l, 0);
	test(lrint, -0x8000000000000800.0p0, IGNORE, FE_INVALID);
	test(lrintf, -0x8000010000000000.0p0f, IGNORE, FE_INVALID);
	test(lrint, -0x8000000000000000.0p0, (long long)-0x8000000000000000ul, 0);
	test(lrintf, -0x8000000000000000.0p0f, (long long)-0x8000000000000000ul, 0);
#else
#error "Unsupported long size"
#endif

#if (LLONG_MAX == 0x7fffffffffffffffLL)
	ATF_REQUIRE_EQ(0, fesetround(FE_TONEAREST));
	test(llrint, 0x8000000000000000.0p0, IGNORE, FE_INVALID);
	test(llrintf, 0x8000000000000000.0p0f, IGNORE, FE_INVALID);
	test(llrint, 0x7ffffffffffffc00.0p0, 0x7ffffffffffffc00ll, 0);
	test(llrintf, 0x7fffff8000000000.0p0f, 0x7fffff8000000000ll, 0);
	test(llrint, -0x8000000000000800.0p0, IGNORE, FE_INVALID);
	test(llrintf, -0x8000010000000000.0p0f, IGNORE, FE_INVALID);
	test(llrint, -0x8000000000000000.0p0, (long long)-0x8000000000000000ull, 0);
	test(llrintf, -0x8000000000000000.0p0f, (long long)-0x8000000000000000ull, 0);
#else
#error "Unsupported long long size"
#endif
}

ATF_TC_WITHOUT_HEAD(lrint);
ATF_TC_BODY(lrint, tc)
{
	run_tests();
#ifndef __OpenBSD__
#ifdef	__i386__
	fpsetprec(FP_PE);
	run_tests();
#endif
#endif
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, lrint);
	return (atf_no_error());
}
