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

/*
 * Tests for csin[h](), ccos[h](), and ctan[h]().
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <complex.h>
#include <fenv.h>
#include <float.h>
#include <math.h>
#include <stdio.h>

#include <atf-c.h>

#include "test-utils.h"

#pragma STDC FENV_ACCESS	ON
#pragma	STDC CX_LIMITED_RANGE	OFF

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
#define	test_p(func, z, result, exceptmask, excepts, checksign)	do {	\
	volatile long double complex _d = z;				\
	debug("  testing %s(%Lg + %Lg I) == %Lg + %Lg I\n", #func,	\
	    creall(_d), cimagl(_d), creall(result), cimagl(result));	\
	ATF_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);			\
	ATF_CHECK(cfpequal_cs((func)(_d), (result), (checksign)));		\
	ATF_CHECK(((void)(func), fetestexcept(exceptmask) == (excepts)));	\
} while (0)

/*
 * Test within a given tolerance.  The tolerance indicates relative error
 * in ulps.  If result is 0, however, it measures absolute error in units
 * of <format>_EPSILON.
 */
#define	test_p_tol(func, z, result, tol)			do {	\
	volatile long double complex _d = z;				\
	debug("  testing %s(%Lg + %Lg I) ~= %Lg + %Lg I\n", #func,	\
	    creall(_d), cimagl(_d), creall(result), cimagl(result));	\
	ATF_CHECK(cfpequal_tol((func)(_d), (result), (tol), FPE_ABS_ZERO)); \
} while (0)

/* These wrappers apply the identities f(conj(z)) = conj(f(z)). */
#define	test(func, z, result, exceptmask, excepts, checksign)	do {	\
	test_p(func, z, result, exceptmask, excepts, checksign);	\
	test_p(func, conjl(z), conjl(result), exceptmask, excepts, checksign); \
} while (0)
#define	test_tol(func, z, result, tol)				do {	\
	test_p_tol(func, z, result, tol);				\
	test_p_tol(func, conjl(z), conjl(result), tol);			\
} while (0)
#define	test_odd_tol(func, z, result, tol)			do {	\
	test_tol(func, z, result, tol);					\
	test_tol(func, -(z), -(result), tol);				\
} while (0)
#define	test_even_tol(func, z, result, tol)			do {	\
	test_tol(func, z, result, tol);					\
	test_tol(func, -(z), result, tol);				\
} while (0)

/* Test the given function in all precisions. */
#define	testall(func, x, result, exceptmask, excepts, checksign) do {	\
	test(func, x, result, exceptmask, excepts, checksign);		\
	test(func##f, x, result, exceptmask, excepts, checksign);	\
} while (0)
#define	testall_odd(func, x, result, exceptmask, excepts, checksign) do { \
	testall(func, x, result, exceptmask, excepts, checksign);	\
	testall(func, -x, -result, exceptmask, excepts, checksign);	\
} while (0)
#define	testall_even(func, x, result, exceptmask, excepts, checksign) do { \
	testall(func, x, result, exceptmask, excepts, checksign);	\
	testall(func, -x, result, exceptmask, excepts, checksign);	\
} while (0)

/*
 * Test the given function in all precisions, within a given tolerance.
 * The tolerance is specified in ulps.
 */
#define	testall_tol(func, x, result, tol)	       		   do { \
	test_tol(func, x, result, tol * DBL_ULP());			\
	test_tol(func##f, x, result, tol * FLT_ULP());			\
} while (0)
#define	testall_odd_tol(func, x, result, tol)	       		   do { \
	test_odd_tol(func, x, result, tol * DBL_ULP());			\
	test_odd_tol(func##f, x, result, tol * FLT_ULP());		\
} while (0)
#define	testall_even_tol(func, x, result, tol)	       		   do { \
	test_even_tol(func, x, result, tol * DBL_ULP());		\
	test_even_tol(func##f, x, result, tol * FLT_ULP());		\
} while (0)


ATF_TC(test_zero_input);
ATF_TC_HEAD(test_zero_input, tc)
{
	atf_tc_set_md_var(tc, "descr", "test 0 input");
}
ATF_TC_BODY(test_zero_input, tc)
{
	long double complex zero = CMPLXL(0.0, 0.0);

#if defined(__amd64__)
#if defined(__clang__) && \
	((__clang_major__ >= 4))
	atf_tc_expect_fail("test fails with clang 4.x+ - bug 217528");
#endif
#endif

	/* csinh(0) = ctanh(0) = 0; ccosh(0) = 1 (no exceptions raised) */
	testall_odd(csinh, zero, zero, ALL_STD_EXCEPT, 0, CS_BOTH);
	testall_odd(csin, zero, zero, ALL_STD_EXCEPT, 0, CS_BOTH);
	testall_even(ccosh, zero, 1.0, ALL_STD_EXCEPT, 0, CS_BOTH);
	testall_even(ccos, zero, CMPLXL(1.0, -0.0), ALL_STD_EXCEPT, 0, CS_BOTH);
	testall_odd(ctanh, zero, zero, ALL_STD_EXCEPT, 0, CS_BOTH);
	testall_odd(ctan, zero, zero, ALL_STD_EXCEPT, 0, CS_BOTH);
}

ATF_TC(test_nan_inputs);
ATF_TC_HEAD(test_nan_inputs, tc)
{
	atf_tc_set_md_var(tc, "descr", "test NaN inputs");
}
ATF_TC_BODY(test_nan_inputs, tc)
{
	long double complex nan_nan = CMPLXL(NAN, NAN);
	long double complex z;

	/*
	 * IN		CSINH		CCOSH		CTANH
	 * NaN,NaN	NaN,NaN		NaN,NaN		NaN,NaN
	 * finite,NaN	NaN,NaN [inval]	NaN,NaN [inval]	NaN,NaN [inval]
	 * NaN,finite	NaN,NaN [inval]	NaN,NaN [inval]	NaN,NaN [inval]
	 * NaN,Inf	NaN,NaN [inval]	NaN,NaN	[inval]	NaN,NaN [inval]
	 * Inf,NaN	+-Inf,NaN	Inf,NaN		1,+-0
	 * 0,NaN	+-0,NaN		NaN,+-0		NaN,NaN	[inval]
	 * NaN,0	NaN,0		NaN,+-0		NaN,0
	 */
	z = nan_nan;
	testall_odd(csinh, z, nan_nan, ALL_STD_EXCEPT, 0, 0);
	testall_even(ccosh, z, nan_nan, ALL_STD_EXCEPT, 0, 0);
	testall_odd(ctanh, z, nan_nan, ALL_STD_EXCEPT, 0, 0);
	testall_odd(csin, z, nan_nan, ALL_STD_EXCEPT, 0, 0);
	testall_even(ccos, z, nan_nan, ALL_STD_EXCEPT, 0, 0);
	testall_odd(ctan, z, nan_nan, ALL_STD_EXCEPT, 0, 0);

	z = CMPLXL(42, NAN);
	testall_odd(csinh, z, nan_nan, OPT_INVALID, 0, 0);
	testall_even(ccosh, z, nan_nan, OPT_INVALID, 0, 0);
	/* XXX We allow a spurious inexact exception here. */
	testall_odd(ctanh, z, nan_nan, OPT_INVALID & ~FE_INEXACT, 0, 0);
	testall_odd(csin, z, nan_nan, OPT_INVALID, 0, 0);
	testall_even(ccos, z, nan_nan, OPT_INVALID, 0, 0);
	testall_odd(ctan, z, nan_nan, OPT_INVALID, 0, 0);

	z = CMPLXL(NAN, 42);
	testall_odd(csinh, z, nan_nan, OPT_INVALID, 0, 0);
	testall_even(ccosh, z, nan_nan, OPT_INVALID, 0, 0);
	testall_odd(ctanh, z, nan_nan, OPT_INVALID, 0, 0);
	testall_odd(csin, z, nan_nan, OPT_INVALID, 0, 0);
	testall_even(ccos, z, nan_nan, OPT_INVALID, 0, 0);
	/* XXX We allow a spurious inexact exception here. */
	testall_odd(ctan, z, nan_nan, OPT_INVALID & ~FE_INEXACT, 0, 0);

	z = CMPLXL(NAN, INFINITY);
	testall_odd(csinh, z, nan_nan, OPT_INVALID, 0, 0);
	testall_even(ccosh, z, nan_nan, OPT_INVALID, 0, 0);
	testall_odd(ctanh, z, nan_nan, OPT_INVALID, 0, 0);
	testall_odd(csin, z, CMPLXL(NAN, INFINITY), ALL_STD_EXCEPT, 0, 0);
	testall_even(ccos, z, CMPLXL(INFINITY, NAN), ALL_STD_EXCEPT, 0,
	    CS_IMAG);
	testall_odd(ctan, z, CMPLXL(0, 1), ALL_STD_EXCEPT, 0, CS_IMAG);

	z = CMPLXL(INFINITY, NAN);
	testall_odd(csinh, z, CMPLXL(INFINITY, NAN), ALL_STD_EXCEPT, 0, 0);
	testall_even(ccosh, z, CMPLXL(INFINITY, NAN), ALL_STD_EXCEPT, 0,
		     CS_REAL);
	testall_odd(ctanh, z, CMPLXL(1, 0), ALL_STD_EXCEPT, 0, CS_REAL);
	testall_odd(csin, z, nan_nan, OPT_INVALID, 0, 0);
	testall_even(ccos, z, nan_nan, OPT_INVALID, 0, 0);
	testall_odd(ctan, z, nan_nan, OPT_INVALID, 0, 0);

	z = CMPLXL(0, NAN);
	testall_odd(csinh, z, CMPLXL(0, NAN), ALL_STD_EXCEPT, 0, 0);
	testall_even(ccosh, z, CMPLXL(NAN, 0), ALL_STD_EXCEPT, 0, 0);
	testall_odd(ctanh, z, nan_nan, OPT_INVALID, 0, 0);
	testall_odd(csin, z, CMPLXL(0, NAN), ALL_STD_EXCEPT, 0, CS_REAL);
	testall_even(ccos, z, CMPLXL(NAN, 0), ALL_STD_EXCEPT, 0, 0);
	testall_odd(ctan, z, CMPLXL(0, NAN), ALL_STD_EXCEPT, 0, CS_REAL);

	z = CMPLXL(NAN, 0);
	testall_odd(csinh, z, CMPLXL(NAN, 0), ALL_STD_EXCEPT, 0, CS_IMAG);
	testall_even(ccosh, z, CMPLXL(NAN, 0), ALL_STD_EXCEPT, 0, 0);
	testall_odd(ctanh, z, CMPLXL(NAN, 0), ALL_STD_EXCEPT, 0, CS_IMAG);
	testall_odd(csin, z, CMPLXL(NAN, 0), ALL_STD_EXCEPT, 0, 0);
	testall_even(ccos, z, CMPLXL(NAN, 0), ALL_STD_EXCEPT, 0, 0);
	testall_odd(ctan, z, nan_nan, OPT_INVALID, 0, 0);
}

ATF_TC(test_inf_inputs);
ATF_TC_HEAD(test_inf_inputs, tc)
{
	atf_tc_set_md_var(tc, "descr", "test infinity inputs");
}
ATF_TC_BODY(test_inf_inputs, tc)
{
	static const long double finites[] = {
	    0, M_PI / 4, 3 * M_PI / 4, 5 * M_PI / 4,
	};
	long double complex z, c, s;
	unsigned i;

	/*
	 * IN		CSINH		CCOSH		CTANH
	 * Inf,Inf	+-Inf,NaN inval	+-Inf,NaN inval	1,+-0
	 * Inf,finite	Inf cis(finite)	Inf cis(finite)	1,0 sin(2 finite)
	 * 0,Inf	+-0,NaN	inval	NaN,+-0 inval	NaN,NaN	inval
	 * finite,Inf	NaN,NaN inval	NaN,NaN inval	NaN,NaN inval
	 */
	z = CMPLXL(INFINITY, INFINITY);
	testall_odd(csinh, z, CMPLXL(INFINITY, NAN),
		    ALL_STD_EXCEPT, FE_INVALID, 0);
	testall_even(ccosh, z, CMPLXL(INFINITY, NAN),
		     ALL_STD_EXCEPT, FE_INVALID, 0);
	testall_odd(ctanh, z, CMPLXL(1, 0), ALL_STD_EXCEPT, 0, CS_REAL);
	testall_odd(csin, z, CMPLXL(NAN, INFINITY),
		    ALL_STD_EXCEPT, FE_INVALID, 0);
	testall_even(ccos, z, CMPLXL(INFINITY, NAN),
		     ALL_STD_EXCEPT, FE_INVALID, 0);
	testall_odd(ctan, z, CMPLXL(0, 1), ALL_STD_EXCEPT, 0, CS_REAL);

	/* XXX We allow spurious inexact exceptions here (hard to avoid). */
	for (i = 0; i < nitems(finites); i++) {
		z = CMPLXL(INFINITY, finites[i]);
		c = INFINITY * cosl(finites[i]);
		s = finites[i] == 0 ? finites[i] : INFINITY * sinl(finites[i]);
		testall_odd(csinh, z, CMPLXL(c, s), OPT_INEXACT, 0, CS_BOTH);
		testall_even(ccosh, z, CMPLXL(c, s), OPT_INEXACT, 0, CS_BOTH);
		testall_odd(ctanh, z, CMPLXL(1, 0 * sin(finites[i] * 2)),
			    OPT_INEXACT, 0, CS_BOTH);
		z = CMPLXL(finites[i], INFINITY);
		testall_odd(csin, z, CMPLXL(s, c), OPT_INEXACT, 0, CS_BOTH);
		testall_even(ccos, z, CMPLXL(c, -s), OPT_INEXACT, 0, CS_BOTH);
		testall_odd(ctan, z, CMPLXL(0 * sin(finites[i] * 2), 1),
			    OPT_INEXACT, 0, CS_BOTH);
	}

	z = CMPLXL(0, INFINITY);
	testall_odd(csinh, z, CMPLXL(0, NAN), ALL_STD_EXCEPT, FE_INVALID, 0);
	testall_even(ccosh, z, CMPLXL(NAN, 0), ALL_STD_EXCEPT, FE_INVALID, 0);
	testall_odd(ctanh, z, CMPLXL(NAN, NAN), ALL_STD_EXCEPT, FE_INVALID, 0);
	z = CMPLXL(INFINITY, 0);
	testall_odd(csin, z, CMPLXL(NAN, 0), ALL_STD_EXCEPT, FE_INVALID, 0);
	testall_even(ccos, z, CMPLXL(NAN, 0), ALL_STD_EXCEPT, FE_INVALID, 0);
	testall_odd(ctan, z, CMPLXL(NAN, NAN), ALL_STD_EXCEPT, FE_INVALID, 0);

	z = CMPLXL(42, INFINITY);
	testall_odd(csinh, z, CMPLXL(NAN, NAN), ALL_STD_EXCEPT, FE_INVALID, 0);
	testall_even(ccosh, z, CMPLXL(NAN, NAN), ALL_STD_EXCEPT, FE_INVALID, 0);
	/* XXX We allow a spurious inexact exception here. */
	testall_odd(ctanh, z, CMPLXL(NAN, NAN), OPT_INEXACT, FE_INVALID, 0);
	z = CMPLXL(INFINITY, 42);
	testall_odd(csin, z, CMPLXL(NAN, NAN), ALL_STD_EXCEPT, FE_INVALID, 0);
	testall_even(ccos, z, CMPLXL(NAN, NAN), ALL_STD_EXCEPT, FE_INVALID, 0);
	/* XXX We allow a spurious inexact exception here. */
	testall_odd(ctan, z, CMPLXL(NAN, NAN), OPT_INEXACT, FE_INVALID, 0);
}

ATF_TC(test_axes);
ATF_TC_HEAD(test_axes, tc)
{
	atf_tc_set_md_var(tc, "descr", "test along the real/imaginary axes");
}
ATF_TC_BODY(test_axes, tc)
{
	static const long double nums[] = {
	    M_PI / 4, M_PI / 2, 3 * M_PI / 4,
	    5 * M_PI / 4, 3 * M_PI / 2, 7 * M_PI / 4,
	};
	long double complex z;
	unsigned i;

	for (i = 0; i < nitems(nums); i++) {
		/* Real axis */
		z = CMPLXL(nums[i], 0.0);
		test_odd_tol(csinh, z, CMPLXL(sinh(nums[i]), 0), DBL_ULP());
		test_even_tol(ccosh, z, CMPLXL(cosh(nums[i]), 0), DBL_ULP());
		test_odd_tol(ctanh, z, CMPLXL(tanh(nums[i]), 0), DBL_ULP());
		test_odd_tol(csin, z, CMPLXL(sin(nums[i]),
		    copysign(0, cos(nums[i]))), DBL_ULP());
		test_even_tol(ccos, z, CMPLXL(cos(nums[i]),
		    -copysign(0, sin(nums[i]))), DBL_ULP());
		test_odd_tol(ctan, z, CMPLXL(tan(nums[i]), 0), DBL_ULP());

		test_odd_tol(csinhf, z, CMPLXL(sinhf(nums[i]), 0), FLT_ULP());
		test_even_tol(ccoshf, z, CMPLXL(coshf(nums[i]), 0), FLT_ULP());
		printf("%a %a\n", creal(z), cimag(z));
		printf("%a %a\n", creal(ctanhf(z)), cimag(ctanhf(z)));
		printf("%a\n", nextafterf(tanhf(nums[i]), INFINITY));
		test_odd_tol(ctanhf, z, CMPLXL(tanhf(nums[i]), 0),
			     1.3 * FLT_ULP());
		test_odd_tol(csinf, z, CMPLXL(sinf(nums[i]),
		    copysign(0, cosf(nums[i]))), FLT_ULP());
		test_even_tol(ccosf, z, CMPLXL(cosf(nums[i]),
		    -copysign(0, sinf(nums[i]))), 2 * FLT_ULP());
		test_odd_tol(ctanf, z, CMPLXL(tanf(nums[i]), 0), FLT_ULP());

		/* Imaginary axis */
		z = CMPLXL(0.0, nums[i]);
		test_odd_tol(csinh, z, CMPLXL(copysign(0, cos(nums[i])),
						 sin(nums[i])), DBL_ULP());
		test_even_tol(ccosh, z, CMPLXL(cos(nums[i]),
		    copysign(0, sin(nums[i]))), DBL_ULP());
		test_odd_tol(ctanh, z, CMPLXL(0, tan(nums[i])), DBL_ULP());
		test_odd_tol(csin, z, CMPLXL(0, sinh(nums[i])), DBL_ULP());
		test_even_tol(ccos, z, CMPLXL(cosh(nums[i]), -0.0), DBL_ULP());
		test_odd_tol(ctan, z, CMPLXL(0, tanh(nums[i])), DBL_ULP());

		test_odd_tol(csinhf, z, CMPLXL(copysign(0, cosf(nums[i])),
						 sinf(nums[i])), FLT_ULP());
		test_even_tol(ccoshf, z, CMPLXL(cosf(nums[i]),
		    copysign(0, sinf(nums[i]))), FLT_ULP());
		test_odd_tol(ctanhf, z, CMPLXL(0, tanf(nums[i])), FLT_ULP());
		test_odd_tol(csinf, z, CMPLXL(0, sinhf(nums[i])), FLT_ULP());
		test_even_tol(ccosf, z, CMPLXL(coshf(nums[i]), -0.0),
			      FLT_ULP());
		test_odd_tol(ctanf, z, CMPLXL(0, tanhf(nums[i])),
			     1.3 * FLT_ULP());
	}
}

ATF_TC(test_small_inputs);
ATF_TC_HEAD(test_small_inputs, tc)
{
	atf_tc_set_md_var(tc, "descr", "test underflow inputs");
}
ATF_TC_BODY(test_small_inputs, tc)
{
	/*
	 * z =  0.5 + i Pi/4
	 *     sinh(z) = (sinh(0.5) + i cosh(0.5)) * sqrt(2)/2
	 *     cosh(z) = (cosh(0.5) + i sinh(0.5)) * sqrt(2)/2
	 *     tanh(z) = (2cosh(0.5)sinh(0.5) + i) / (2 cosh(0.5)**2 - 1)
	 * z = -0.5 + i Pi/2
	 *     sinh(z) = cosh(0.5)
	 *     cosh(z) = -i sinh(0.5)
	 *     tanh(z) = -coth(0.5)
	 * z =  1.0 + i 3Pi/4
	 *     sinh(z) = (-sinh(1) + i cosh(1)) * sqrt(2)/2
	 *     cosh(z) = (-cosh(1) + i sinh(1)) * sqrt(2)/2
	 *     tanh(z) = (2cosh(1)sinh(1) - i) / (2cosh(1)**2 - 1)
	 */
	static const struct {
		long double a, b;
		long double sinh_a, sinh_b;
		long double cosh_a, cosh_b;
		long double tanh_a, tanh_b;
	} tests[] = {
		{  0.5L,
		   0.78539816339744830961566084581987572L,
		   0.36847002415910435172083660522240710L,
		   0.79735196663945774996093142586179334L,
		   0.79735196663945774996093142586179334L,
		   0.36847002415910435172083660522240710L,
		   0.76159415595576488811945828260479359L,
		   0.64805427366388539957497735322615032L },
		{ -0.5L,
		   1.57079632679489661923132169163975144L,
		   0.0L,
		   1.12762596520638078522622516140267201L,
		   0.0L,
		  -0.52109530549374736162242562641149156L,
		  -2.16395341373865284877000401021802312L,
		   0.0L },
		{  1.0L,
		   2.35619449019234492884698253745962716L,
		  -0.83099273328405698212637979852748608L,
		   1.09112278079550143030545602018565236L,
		  -1.09112278079550143030545602018565236L,
		   0.83099273328405698212637979852748609L,
		   0.96402758007581688394641372410092315L,
		  -0.26580222883407969212086273981988897L }
	};
	long double complex z;
	unsigned i;

	for (i = 0; i < nitems(tests); i++) {
		z = CMPLXL(tests[i].a, tests[i].b);
		testall_odd_tol(csinh, z,
		    CMPLXL(tests[i].sinh_a, tests[i].sinh_b), 1.1);
		testall_even_tol(ccosh, z,
		    CMPLXL(tests[i].cosh_a, tests[i].cosh_b), 1.1);
		testall_odd_tol(ctanh, z,
		    CMPLXL(tests[i].tanh_a, tests[i].tanh_b), 1.4);
        }
}

ATF_TC(test_large_inputs);
ATF_TC_HEAD(test_large_inputs, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test inputs that might cause overflow in a sloppy implementation");
}
ATF_TC_BODY(test_large_inputs, tc)
{
	long double complex z;

	/* tanh() uses a threshold around x=22, so check both sides. */
	z = CMPLXL(21, 0.78539816339744830961566084581987572L);
	testall_odd_tol(ctanh, z,
	    CMPLXL(1.0, 1.14990445285871196133287617611468468e-18L), 1.2);
	z++;
	testall_odd_tol(ctanh, z,
	    CMPLXL(1.0, 1.55622644822675930314266334585597964e-19L), 1);

	z = CMPLXL(355, 0.78539816339744830961566084581987572L);
	test_odd_tol(ctanh, z,
		     CMPLXL(1.0, 8.95257245135025991216632140458264468e-309L),
		     DBL_ULP());
	z = CMPLXL(30, 0x1p1023L);
	test_odd_tol(ctanh, z,
		     CMPLXL(1.0, -1.62994325413993477997492170229268382e-26L),
		     DBL_ULP());
	z = CMPLXL(1, 0x1p1023L);
	test_odd_tol(ctanh, z,
		     CMPLXL(0.878606311888306869546254022621986509L,
			    -0.225462792499754505792678258169527424L),
		     DBL_ULP());

	z = CMPLXL(710.6, 0.78539816339744830961566084581987572L);
	test_odd_tol(csinh, z,
	    CMPLXL(1.43917579766621073533185387499658944e308L,
		   1.43917579766621073533185387499658944e308L), DBL_ULP());
	test_even_tol(ccosh, z,
	    CMPLXL(1.43917579766621073533185387499658944e308L,
		   1.43917579766621073533185387499658944e308L), DBL_ULP());

	z = CMPLXL(1500, 0.78539816339744830961566084581987572L);
	testall_odd(csinh, z, CMPLXL(INFINITY, INFINITY), OPT_INEXACT,
	    FE_OVERFLOW, CS_BOTH);
	testall_even(ccosh, z, CMPLXL(INFINITY, INFINITY), OPT_INEXACT,
	    FE_OVERFLOW, CS_BOTH);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, test_zero_input);
	ATF_TP_ADD_TC(tp, test_nan_inputs);
	ATF_TP_ADD_TC(tp, test_inf_inputs);
	ATF_TP_ADD_TC(tp, test_axes);
	ATF_TP_ADD_TC(tp, test_small_inputs);
	ATF_TP_ADD_TC(tp, test_large_inputs);

	return (atf_no_error());
}
