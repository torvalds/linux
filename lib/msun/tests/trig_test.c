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
 * Tests for corner cases in trigonometric functions. Some accuracy tests
 * are included as well, but these are very basic sanity checks, not
 * intended to be comprehensive.
 *
 * The program for generating representable numbers near multiples of pi is
 * available at http://www.cs.berkeley.edu/~wkahan/testpi/ .
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>

#include <assert.h>
#include <fenv.h>
#include <float.h>
#include <math.h>
#include <stdio.h>

#include <atf-c.h>

#include "test-utils.h"

#pragma STDC FENV_ACCESS ON

/*
 * Test that a function returns the correct value and sets the
 * exception flags correctly. The exceptmask specifies which
 * exceptions we should check. We need to be lenient for several
 * reasons, but mainly because on some architectures it's impossible
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
	ATF_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);			\
	ATF_CHECK(fpequal((func)(_d), (result)));				\
	ATF_CHECK(((void)(func), fetestexcept(exceptmask) == (excepts)));	\
} while (0)

#define	testall(prefix, x, result, exceptmask, excepts)	do {		\
	test(prefix, x, (double)result, exceptmask, excepts);		\
	test(prefix##f, x, (float)result, exceptmask, excepts);		\
	test(prefix##l, x, result, exceptmask, excepts);		\
} while (0)

#define	testdf(prefix, x, result, exceptmask, excepts)	do {		\
	test(prefix, x, (double)result, exceptmask, excepts);		\
	test(prefix##f, x, (float)result, exceptmask, excepts);		\
} while (0)

ATF_TC(special);
ATF_TC_HEAD(special, tc)
{

	atf_tc_set_md_var(tc, "descr",
 	    "test special cases in sin(), cos(), and tan()");
}
ATF_TC_BODY(special, tc)
{

	/* Values at 0 should be exact. */
	testall(tan, 0.0, 0.0, ALL_STD_EXCEPT, 0);
	testall(tan, -0.0, -0.0, ALL_STD_EXCEPT, 0);
	testall(cos, 0.0, 1.0, ALL_STD_EXCEPT, 0);
	testall(cos, -0.0, 1.0, ALL_STD_EXCEPT, 0);
	testall(sin, 0.0, 0.0, ALL_STD_EXCEPT, 0);
	testall(sin, -0.0, -0.0, ALL_STD_EXCEPT, 0);

	/* func(+-Inf) == NaN */
	testall(tan, INFINITY, NAN, ALL_STD_EXCEPT, FE_INVALID);
	testall(sin, INFINITY, NAN, ALL_STD_EXCEPT, FE_INVALID);
	testall(cos, INFINITY, NAN, ALL_STD_EXCEPT, FE_INVALID);
	testall(tan, -INFINITY, NAN, ALL_STD_EXCEPT, FE_INVALID);
	testall(sin, -INFINITY, NAN, ALL_STD_EXCEPT, FE_INVALID);
	testall(cos, -INFINITY, NAN, ALL_STD_EXCEPT, FE_INVALID);

	/* func(NaN) == NaN */
	testall(tan, NAN, NAN, ALL_STD_EXCEPT, 0);
	testall(sin, NAN, NAN, ALL_STD_EXCEPT, 0);
	testall(cos, NAN, NAN, ALL_STD_EXCEPT, 0);
}

#ifndef __i386__
ATF_TC(reduction);
ATF_TC_HEAD(reduction, tc)
{

	atf_tc_set_md_var(tc, "descr",
 	    "tests to ensure argument reduction for large arguments is accurate");
}
ATF_TC_BODY(reduction, tc)
{
	/* floats very close to odd multiples of pi */
	static const float f_pi_odd[] = {
		85563208.0f,
		43998769152.0f,
		9.2763667655669323e+25f,
		1.5458357838905804e+29f,
	};
	/* doubles very close to odd multiples of pi */
	static const double d_pi_odd[] = {
		3.1415926535897931,
		91.106186954104004,
		642615.9188844458,
		3397346.5699258847,
		6134899525417045.0,
		3.0213551960457761e+43,
		1.2646209897993783e+295,
		6.2083625380677099e+307,
	};
	/* long doubles very close to odd multiples of pi */
#if LDBL_MANT_DIG == 64
	static const long double ld_pi_odd[] = {
		1.1891886960373841596e+101L,
		1.07999475322710967206e+2087L,
		6.522151627890431836e+2147L,
		8.9368974898260328229e+2484L,
		9.2961044110572205863e+2555L,
		4.90208421886578286e+3189L,
		1.5275546401232615884e+3317L,
		1.7227465626338900093e+3565L,
		2.4160090594000745334e+3808L,
		9.8477555741888350649e+4314L,
		1.6061597222105160737e+4326L,
	};
#endif

	unsigned i;

#if defined(__amd64__) && defined(__clang__) && __clang_major__ >= 7 && \
    __FreeBSD_cc_version < 1300002
	atf_tc_expect_fail("test fails with clang 7+ - bug 234040");
#endif

	for (i = 0; i < nitems(f_pi_odd); i++) {
		ATF_CHECK(fabs(sinf(f_pi_odd[i])) < FLT_EPSILON);
		ATF_CHECK(cosf(f_pi_odd[i]) == -1.0);
		ATF_CHECK(fabs(tan(f_pi_odd[i])) < FLT_EPSILON);

		ATF_CHECK(fabs(sinf(-f_pi_odd[i])) < FLT_EPSILON);
		ATF_CHECK(cosf(-f_pi_odd[i]) == -1.0);
		ATF_CHECK(fabs(tanf(-f_pi_odd[i])) < FLT_EPSILON);

		ATF_CHECK(fabs(sinf(f_pi_odd[i] * 2)) < FLT_EPSILON);
		ATF_CHECK(cosf(f_pi_odd[i] * 2) == 1.0);
		ATF_CHECK(fabs(tanf(f_pi_odd[i] * 2)) < FLT_EPSILON);

		ATF_CHECK(fabs(sinf(-f_pi_odd[i] * 2)) < FLT_EPSILON);
		ATF_CHECK(cosf(-f_pi_odd[i] * 2) == 1.0);
		ATF_CHECK(fabs(tanf(-f_pi_odd[i] * 2)) < FLT_EPSILON);
	}

	for (i = 0; i < nitems(d_pi_odd); i++) {
		ATF_CHECK(fabs(sin(d_pi_odd[i])) < 2 * DBL_EPSILON);
		ATF_CHECK(cos(d_pi_odd[i]) == -1.0);
		ATF_CHECK(fabs(tan(d_pi_odd[i])) < 2 * DBL_EPSILON);

		ATF_CHECK(fabs(sin(-d_pi_odd[i])) < 2 * DBL_EPSILON);
		ATF_CHECK(cos(-d_pi_odd[i]) == -1.0);
		ATF_CHECK(fabs(tan(-d_pi_odd[i])) < 2 * DBL_EPSILON);

		ATF_CHECK(fabs(sin(d_pi_odd[i] * 2)) < 2 * DBL_EPSILON);
		ATF_CHECK(cos(d_pi_odd[i] * 2) == 1.0);
		ATF_CHECK(fabs(tan(d_pi_odd[i] * 2)) < 2 * DBL_EPSILON);

		ATF_CHECK(fabs(sin(-d_pi_odd[i] * 2)) < 2 * DBL_EPSILON);
		ATF_CHECK(cos(-d_pi_odd[i] * 2) == 1.0);
		ATF_CHECK(fabs(tan(-d_pi_odd[i] * 2)) < 2 * DBL_EPSILON);
	}

#if LDBL_MANT_DIG == 64 /* XXX: || LDBL_MANT_DIG == 113 */
	for (i = 0; i < nitems(ld_pi_odd); i++) {
		ATF_CHECK(fabsl(sinl(ld_pi_odd[i])) < LDBL_EPSILON);
		ATF_CHECK(cosl(ld_pi_odd[i]) == -1.0);
		ATF_CHECK(fabsl(tanl(ld_pi_odd[i])) < LDBL_EPSILON);

		ATF_CHECK(fabsl(sinl(-ld_pi_odd[i])) < LDBL_EPSILON);
		ATF_CHECK(cosl(-ld_pi_odd[i]) == -1.0);
		ATF_CHECK(fabsl(tanl(-ld_pi_odd[i])) < LDBL_EPSILON);

		ATF_CHECK(fabsl(sinl(ld_pi_odd[i] * 2)) < LDBL_EPSILON);
		ATF_CHECK(cosl(ld_pi_odd[i] * 2) == 1.0);
		ATF_CHECK(fabsl(tanl(ld_pi_odd[i] * 2)) < LDBL_EPSILON);

		ATF_CHECK(fabsl(sinl(-ld_pi_odd[i] * 2)) < LDBL_EPSILON);
		ATF_CHECK(cosl(-ld_pi_odd[i] * 2) == 1.0);
		ATF_CHECK(fabsl(tanl(-ld_pi_odd[i] * 2)) < LDBL_EPSILON);
	}
#endif
}

ATF_TC(accuracy);
ATF_TC_HEAD(accuracy, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "tests the accuracy of these functions over the primary range");
}
ATF_TC_BODY(accuracy, tc)
{

	/* For small args, sin(x) = tan(x) = x, and cos(x) = 1. */
	testall(sin, 0xd.50ee515fe4aea16p-114L, 0xd.50ee515fe4aea16p-114L,
	     ALL_STD_EXCEPT, FE_INEXACT);
	testall(tan, 0xd.50ee515fe4aea16p-114L, 0xd.50ee515fe4aea16p-114L,
	     ALL_STD_EXCEPT, FE_INEXACT);
	testall(cos, 0xd.50ee515fe4aea16p-114L, 1.0,
		ALL_STD_EXCEPT, FE_INEXACT);

	/*
	 * These tests should pass for f32, d64, and ld80 as long as
	 * the error is <= 0.75 ulp (round to nearest)
	 */
#if LDBL_MANT_DIG <= 64
#define	testacc	testall
#else
#define	testacc	testdf
#endif
	testacc(sin, 0.17255452780841205174L, 0.17169949801444412683L,
		ALL_STD_EXCEPT, FE_INEXACT);
	testacc(sin, -0.75431944555904520893L, -0.68479288156557286353L,
		ALL_STD_EXCEPT, FE_INEXACT);
	testacc(cos, 0.70556358769838947292L, 0.76124620693117771850L,
		ALL_STD_EXCEPT, FE_INEXACT);
	testacc(cos, -0.34061437849088045332L, 0.94254960031831729956L,
		ALL_STD_EXCEPT, FE_INEXACT);
	testacc(tan, -0.15862817413325692897L, -0.15997221861309522115L,
		ALL_STD_EXCEPT, FE_INEXACT);
	testacc(tan, 0.38374784931303813530L, 0.40376500259976759951L,
		ALL_STD_EXCEPT, FE_INEXACT);

	/*
	 * XXX missing:
	 * - tests for ld128
	 * - tests for other rounding modes (probably won't pass for now)
	 * - tests for large numbers that get reduced to hi+lo with lo!=0
	 */
}
#endif

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, special);

#ifndef __i386__
	ATF_TP_ADD_TC(tp, accuracy);
	ATF_TP_ADD_TC(tp, reduction);
#endif

	return (atf_no_error());
}
