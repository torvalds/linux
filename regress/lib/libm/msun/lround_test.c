/*	$OpenBSD: lround_test.c,v 1.2 2021/12/13 18:04:28 deraadt Exp $	*/
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
 * Test for lround(), lroundf(), llround(), and llroundf().
 */

#include <fenv.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>

#include "test-utils.h"

#define	IGNORE	0x12345

#define	test(func, x, result, excepts)	do {					\
	ATF_REQUIRE_EQ(0, feclearexcept(FE_ALL_EXCEPT));			\
	long long _r = (func)(x);						\
	CHECK_FP_EXCEPTIONS_MSG(excepts, FE_ALL_EXCEPT, "for %s(%s)",		\
	    #func, #x);								\
	if ((excepts & FE_INVALID) != 0) {					\
		ATF_REQUIRE_EQ(result, IGNORE);					\
		ATF_CHECK_EQ_MSG(FE_INVALID, fetestexcept(FE_INVALID),		\
		    "FE_INVALID not set correctly for %s(%s)", #func, #x);	\
	} else {								\
		ATF_REQUIRE_MSG(result != IGNORE, "Expected can't be IGNORE!");	\
		ATF_REQUIRE_EQ(result, (__STRING(func(_d)), _r));		\
	}									\
} while (0)

#define	testall(x, result, excepts)	do {				\
	test(lround, x, result, excepts);				\
	test(lroundf, x, result, excepts);				\
	test(llround, x, result, excepts);				\
	test(llroundf, x, result, excepts);				\
} while (0)

#pragma STDC FENV_ACCESS ON

ATF_TC_WITHOUT_HEAD(main);
ATF_TC_BODY(main, tc)
{
	testall(0.0, 0, 0);
	testall(0.25, 0, FE_INEXACT);
	testall(0.5, 1, FE_INEXACT);
	testall(-0.5, -1, FE_INEXACT);
	testall(1.0, 1, 0);
	testall(0x12345000p0, 0x12345000, 0);
	testall(0x1234.fp0, 0x1235, FE_INEXACT);
	testall(INFINITY, IGNORE, FE_INVALID);
	testall(NAN, IGNORE, FE_INVALID);

#if (LONG_MAX == 0x7fffffffl)
	test(lround, 0x7fffffff.8p0, IGNORE, FE_INVALID);
	test(lround, -0x80000000.8p0, IGNORE, FE_INVALID);
	test(lround, 0x80000000.0p0, IGNORE, FE_INVALID);
	test(lround, 0x7fffffff.4p0, 0x7fffffffl, FE_INEXACT);
	test(lround, -0x80000000.4p0, -0x80000000l, FE_INEXACT);
	test(lroundf, 0x80000000.0p0f, IGNORE, FE_INVALID);
	test(lroundf, 0x7fffff80.0p0f, 0x7fffff80l, 0);
#elif (LONG_MAX == 0x7fffffffffffffffll)
	test(lround, 0x8000000000000000.0p0, IGNORE, FE_INVALID);
	test(lroundf, 0x8000000000000000.0p0f, IGNORE, FE_INVALID);
	test(lround, 0x7ffffffffffffc00.0p0, 0x7ffffffffffffc00l, 0);
	test(lroundf, 0x7fffff8000000000.0p0f, 0x7fffff8000000000l, 0);
	test(lround, -0x8000000000000800.0p0, IGNORE, FE_INVALID);
	test(lroundf, -0x8000010000000000.0p0f, IGNORE, FE_INVALID);
	test(lround, -0x8000000000000000.0p0, (long)-0x8000000000000000l, 0);
	test(lroundf, -0x8000000000000000.0p0f, (long)-0x8000000000000000l, 0);
#else
#error "Unsupported long size"
#endif

#if (LLONG_MAX == 0x7fffffffffffffffLL)
	test(llround, 0x8000000000000000.0p0, IGNORE, FE_INVALID);
	test(llroundf, 0x8000000000000000.0p0f, IGNORE, FE_INVALID);
	test(llround, 0x7ffffffffffffc00.0p0, 0x7ffffffffffffc00ll, 0);
	test(llroundf, 0x7fffff8000000000.0p0f, 0x7fffff8000000000ll, 0);
	test(llround, -0x8000000000000800.0p0, IGNORE, FE_INVALID);
	test(llroundf, -0x8000010000000000.0p0f, IGNORE, FE_INVALID);
	test(llround, -0x8000000000000000.0p0, (long long)-0x8000000000000000ll, 0);
	test(llroundf, -0x8000000000000000.0p0f, (long long)-0x8000000000000000ll, 0);
#else
#error "Unsupported long long size"
#endif
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, main);

	return (atf_no_error());
}
