/*-
 * Copyright (c) 2008-2013 David Schultz <das@FreeBSD.org>
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
 * Tests for casin[h](), cacos[h](), and catan[h]().
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <assert.h>
#include <complex.h>
#include <fenv.h>
#include <float.h>
#include <math.h>
#include <stdio.h>

#include "test-utils.h"

#pragma	STDC FENV_ACCESS	ON
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
	assert(feclearexcept(FE_ALL_EXCEPT) == 0);			\
	assert(cfpequal_cs((func)(_d), (result), (checksign)));		\
	assert(((void)(func), fetestexcept(exceptmask) == (excepts)));	\
} while (0)

/*
 * Test within a given tolerance.  The tolerance indicates relative error
 * in ulps.
 */
#define	test_p_tol(func, z, result, tol)			do {	\
	volatile long double complex _d = z;				\
	debug("  testing %s(%Lg + %Lg I) ~= %Lg + %Lg I\n", #func,	\
	    creall(_d), cimagl(_d), creall(result), cimagl(result));	\
	assert(cfpequal_tol((func)(_d), (result), (tol), CS_BOTH));	\
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

/* Test the given function in all precisions. */
#define	testall(func, x, result, exceptmask, excepts, checksign) do {	\
	test(func, x, result, exceptmask, excepts, checksign);		\
	test(func##f, x, result, exceptmask, excepts, checksign);	\
} while (0)
#define	testall_odd(func, x, result, exceptmask, excepts, checksign) do { \
	testall(func, x, result, exceptmask, excepts, checksign);	\
	testall(func, -(x), -result, exceptmask, excepts, checksign);	\
} while (0)
#define	testall_even(func, x, result, exceptmask, excepts, checksign) do { \
	testall(func, x, result, exceptmask, excepts, checksign);	\
	testall(func, -(x), result, exceptmask, excepts, checksign);	\
} while (0)

/*
 * Test the given function in all precisions, within a given tolerance.
 * The tolerance is specified in ulps.
 */
#define	testall_tol(func, x, result, tol)	       		   do { \
	test_tol(func, x, result, (tol) * DBL_ULP());			\
	test_tol(func##f, x, result, (tol) * FLT_ULP());		\
} while (0)
#define	testall_odd_tol(func, x, result, tol)	       		   do { \
	testall_tol(func, x, result, tol);				\
	testall_tol(func, -(x), -result, tol);				\
} while (0)
#define	testall_even_tol(func, x, result, tol)	       		   do { \
	testall_tol(func, x, result, tol);				\
	testall_tol(func, -(x), result, tol);				\
} while (0)

static const long double
pi = 3.14159265358979323846264338327950280L,
c3pi = 9.42477796076937971538793014983850839L;


/* Tests for 0 */
static void
test_zero(void)
{
	long double complex zero = CMPLXL(0.0, 0.0);

	testall_tol(cacosh, zero, CMPLXL(0.0, pi / 2), 1);
	testall_tol(cacosh, -zero, CMPLXL(0.0, -pi / 2), 1);
	testall_tol(cacos, zero, CMPLXL(pi / 2, -0.0), 1);
	testall_tol(cacos, -zero, CMPLXL(pi / 2, 0.0), 1);

	testall_odd(casinh, zero, zero, ALL_STD_EXCEPT, 0, CS_BOTH);
	testall_odd(casin, zero, zero, ALL_STD_EXCEPT, 0, CS_BOTH);

	testall_odd(catanh, zero, zero, ALL_STD_EXCEPT, 0, CS_BOTH);
	testall_odd(catan, zero, zero, ALL_STD_EXCEPT, 0, CS_BOTH);
}

/*
 * Tests for NaN inputs.
 */
static void
test_nan(void)
{
	long double complex nan_nan = CMPLXL(NAN, NAN);
	long double complex z;

	/*
	 * IN		CACOSH	    CACOS	CASINH	    CATANH
	 * NaN,NaN	NaN,NaN	    NaN,NaN	NaN,NaN	    NaN,NaN
	 * finite,NaN	NaN,NaN*    NaN,NaN*	NaN,NaN*    NaN,NaN*
	 * NaN,finite   NaN,NaN*    NaN,NaN*	NaN,NaN*    NaN,NaN*
	 * NaN,Inf	Inf,NaN     NaN,-Inf	?Inf,NaN    ?0,pi/2
	 * +-Inf,NaN	Inf,NaN     NaN,?Inf	+-Inf,NaN   +-0,NaN
	 * +-0,NaN	NaN,NaN*    pi/2,NaN	NaN,NaN*    +-0,NaN
	 * NaN,0	NaN,NaN*    NaN,NaN*	NaN,0	    NaN,NaN*
	 *
	 *  * = raise invalid
	 */
	z = nan_nan;
	testall(cacosh, z, nan_nan, ALL_STD_EXCEPT, 0, 0);
	testall(cacos, z, nan_nan, ALL_STD_EXCEPT, 0, 0);
	testall(casinh, z, nan_nan, ALL_STD_EXCEPT, 0, 0);
	testall(casin, z, nan_nan, ALL_STD_EXCEPT, 0, 0);
	testall(catanh, z, nan_nan, ALL_STD_EXCEPT, 0, 0);
	testall(catan, z, nan_nan, ALL_STD_EXCEPT, 0, 0);

	z = CMPLXL(0.5, NAN);
	testall(cacosh, z, nan_nan, OPT_INVALID, 0, 0);
	testall(cacos, z, nan_nan, OPT_INVALID, 0, 0);
	testall(casinh, z, nan_nan, OPT_INVALID, 0, 0);
	testall(casin, z, nan_nan, OPT_INVALID, 0, 0);
	testall(catanh, z, nan_nan, OPT_INVALID, 0, 0);
	testall(catan, z, nan_nan, OPT_INVALID, 0, 0);

	z = CMPLXL(NAN, 0.5);
	testall(cacosh, z, nan_nan, OPT_INVALID, 0, 0);
	testall(cacos, z, nan_nan, OPT_INVALID, 0, 0);
	testall(casinh, z, nan_nan, OPT_INVALID, 0, 0);
	testall(casin, z, nan_nan, OPT_INVALID, 0, 0);
	testall(catanh, z, nan_nan, OPT_INVALID, 0, 0);
	testall(catan, z, nan_nan, OPT_INVALID, 0, 0);

	z = CMPLXL(NAN, INFINITY);
	testall(cacosh, z, CMPLXL(INFINITY, NAN), ALL_STD_EXCEPT, 0, CS_REAL);
	testall(cacosh, -z, CMPLXL(INFINITY, NAN), ALL_STD_EXCEPT, 0, CS_REAL);
	testall(cacos, z, CMPLXL(NAN, -INFINITY), ALL_STD_EXCEPT, 0, CS_IMAG);
	testall(casinh, z, CMPLXL(INFINITY, NAN), ALL_STD_EXCEPT, 0, 0);
	testall(casin, z, CMPLXL(NAN, INFINITY), ALL_STD_EXCEPT, 0, CS_IMAG);
	testall_tol(catanh, z, CMPLXL(0.0, pi / 2), 1);
	testall(catan, z, CMPLXL(NAN, 0.0), ALL_STD_EXCEPT, 0, CS_IMAG);

	z = CMPLXL(INFINITY, NAN);
	testall_even(cacosh, z, CMPLXL(INFINITY, NAN), ALL_STD_EXCEPT, 0,
		     CS_REAL);
	testall_even(cacos, z, CMPLXL(NAN, INFINITY), ALL_STD_EXCEPT, 0, 0);
	testall_odd(casinh, z, CMPLXL(INFINITY, NAN), ALL_STD_EXCEPT, 0,
		    CS_REAL);
	testall_odd(casin, z, CMPLXL(NAN, INFINITY), ALL_STD_EXCEPT, 0, 0);
	testall_odd(catanh, z, CMPLXL(0.0, NAN), ALL_STD_EXCEPT, 0, CS_REAL);
	testall_odd_tol(catan, z, CMPLXL(pi / 2, 0.0), 1);

	z = CMPLXL(0.0, NAN);
        /* XXX We allow a spurious inexact exception here. */
	testall_even(cacosh, z, nan_nan, OPT_INVALID & ~FE_INEXACT, 0, 0);
	testall_even_tol(cacos, z, CMPLXL(pi / 2, NAN), 1);
	testall_odd(casinh, z, nan_nan, OPT_INVALID, 0, 0);
	testall_odd(casin, z, CMPLXL(0.0, NAN), ALL_STD_EXCEPT, 0, CS_REAL);
	testall_odd(catanh, z, CMPLXL(0.0, NAN), OPT_INVALID, 0, CS_REAL);
	testall_odd(catan, z, nan_nan, OPT_INVALID, 0, 0);

	z = CMPLXL(NAN, 0.0);
	testall(cacosh, z, nan_nan, OPT_INVALID, 0, 0);
	testall(cacos, z, nan_nan, OPT_INVALID, 0, 0);
	testall(casinh, z, CMPLXL(NAN, 0), ALL_STD_EXCEPT, 0, CS_IMAG);
	testall(casin, z, nan_nan, OPT_INVALID, 0, 0);
	testall(catanh, z, nan_nan, OPT_INVALID, 0, CS_IMAG);
	testall(catan, z, CMPLXL(NAN, 0.0), ALL_STD_EXCEPT, 0, 0);
}

static void
test_inf(void)
{
	long double complex z;

	/*
	 * IN		CACOSH	    CACOS	CASINH	    CATANH
	 * Inf,Inf	Inf,pi/4    pi/4,-Inf	Inf,pi/4    0,pi/2
	 * -Inf,Inf	Inf,3pi/4   3pi/4,-Inf	---	    ---
	 * Inf,finite	Inf,0	    0,-Inf	Inf,0	    0,pi/2
	 * -Inf,finite	Inf,pi      pi,-Inf	---	    ---
	 * finite,Inf	Inf,pi/2    pi/2,-Inf	Inf,pi/2    0,pi/2
	 */
	z = CMPLXL(INFINITY, INFINITY);
	testall_tol(cacosh, z, CMPLXL(INFINITY, pi / 4), 1);
	testall_tol(cacosh, -z, CMPLXL(INFINITY, -c3pi / 4), 1);
	testall_tol(cacos, z, CMPLXL(pi / 4, -INFINITY), 1);
	testall_tol(cacos, -z, CMPLXL(c3pi / 4, INFINITY), 1);
	testall_odd_tol(casinh, z, CMPLXL(INFINITY, pi / 4), 1);
	testall_odd_tol(casin, z, CMPLXL(pi / 4, INFINITY), 1);
	testall_odd_tol(catanh, z, CMPLXL(0, pi / 2), 1);
	testall_odd_tol(catan, z, CMPLXL(pi / 2, 0), 1);

	z = CMPLXL(INFINITY, 0.5);
	/* XXX We allow a spurious inexact exception here. */
	testall(cacosh, z, CMPLXL(INFINITY, 0), OPT_INEXACT, 0, CS_BOTH);
	testall_tol(cacosh, -z, CMPLXL(INFINITY, -pi), 1);
	testall(cacos, z, CMPLXL(0, -INFINITY), OPT_INEXACT, 0, CS_BOTH);
	testall_tol(cacos, -z, CMPLXL(pi, INFINITY), 1);
	testall_odd(casinh, z, CMPLXL(INFINITY, 0), OPT_INEXACT, 0, CS_BOTH);
	testall_odd_tol(casin, z, CMPLXL(pi / 2, INFINITY), 1);
	testall_odd_tol(catanh, z, CMPLXL(0, pi / 2), 1);
	testall_odd_tol(catan, z, CMPLXL(pi / 2, 0), 1);

	z = CMPLXL(0.5, INFINITY);
	testall_tol(cacosh, z, CMPLXL(INFINITY, pi / 2), 1);
	testall_tol(cacosh, -z, CMPLXL(INFINITY, -pi / 2), 1);
	testall_tol(cacos, z, CMPLXL(pi / 2, -INFINITY), 1);
	testall_tol(cacos, -z, CMPLXL(pi / 2, INFINITY), 1);
	testall_odd_tol(casinh, z, CMPLXL(INFINITY, pi / 2), 1);
	/* XXX We allow a spurious inexact exception here. */
	testall_odd(casin, z, CMPLXL(0.0, INFINITY), OPT_INEXACT, 0, CS_BOTH);
	testall_odd_tol(catanh, z, CMPLXL(0, pi / 2), 1);
	testall_odd_tol(catan, z, CMPLXL(pi / 2, 0), 1);
}

/* Tests along the real and imaginary axes. */
static void
test_axes(void)
{
	static const long double nums[] = {
		-2, -1, -0.5, 0.5, 1, 2
	};
	long double complex z;
	unsigned i;

	for (i = 0; i < nitems(nums); i++) {
		/* Real axis */
		z = CMPLXL(nums[i], 0.0);
		if (fabsl(nums[i]) <= 1) {
			testall_tol(cacosh, z, CMPLXL(0.0, acos(nums[i])), 1);
			testall_tol(cacos, z, CMPLXL(acosl(nums[i]), -0.0), 1);
			testall_tol(casin, z, CMPLXL(asinl(nums[i]), 0.0), 1);
			testall_tol(catanh, z, CMPLXL(atanh(nums[i]), 0.0), 1);
		} else {
			testall_tol(cacosh, z,
				    CMPLXL(acosh(fabsl(nums[i])),
					   (nums[i] < 0) ? pi : 0), 1);
			testall_tol(cacos, z,
				    CMPLXL((nums[i] < 0) ? pi : 0,
					   -acosh(fabsl(nums[i]))), 1);
			testall_tol(casin, z,
				    CMPLXL(copysign(pi / 2, nums[i]),
					   acosh(fabsl(nums[i]))), 1);
			testall_tol(catanh, z,
				    CMPLXL(atanh(1 / nums[i]), pi / 2), 1);
		}
		testall_tol(casinh, z, CMPLXL(asinh(nums[i]), 0.0), 1);
		testall_tol(catan, z, CMPLXL(atan(nums[i]), 0), 1);

		/* TODO: Test the imaginary axis. */
	}
}

static void
test_small(void)
{
	/*
	 * z =  0.75 + i 0.25
	 *     acos(z) = Pi/4 - i ln(2)/2
	 *     asin(z) = Pi/4 + i ln(2)/2
	 *     atan(z) = atan(4)/2 + i ln(17/9)/4
	 */
	complex long double z;
	complex long double acos_z;
	complex long double asin_z;
	complex long double atan_z;

	z = CMPLXL(0.75L, 0.25L);
	acos_z = CMPLXL(pi / 4, -0.34657359027997265470861606072908828L);
	asin_z = CMPLXL(pi / 4, 0.34657359027997265470861606072908828L);
	atan_z = CMPLXL(0.66290883183401623252961960521423782L,
			 0.15899719167999917436476103600701878L);

	testall_tol(cacos, z, acos_z, 2);
	testall_odd_tol(casin, z, asin_z, 2);
	testall_odd_tol(catan, z, atan_z, 2);
}

/* Test inputs that might cause overflow in a sloppy implementation. */
static void
test_large(void)
{

	/* TODO: Write these tests */
}

int
main(void)
{

	printf("1..6\n");

	test_zero();
	printf("ok 1 - invctrig zero\n");

	test_nan();
	printf("ok 2 - invctrig nan\n");

	test_inf();
	printf("ok 3 - invctrig inf\n");

	test_axes();
	printf("ok 4 - invctrig axes\n");

	test_small();
	printf("ok 5 - invctrig small\n");

	test_large();
	printf("ok 6 - invctrig large\n");

	return (0);
}
