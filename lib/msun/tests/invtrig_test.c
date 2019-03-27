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
 * Tests for corner cases in the inverse trigonometric functions. Some
 * accuracy tests are included as well, but these are very basic
 * sanity checks, not intended to be comprehensive.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <assert.h>
#include <fenv.h>
#include <float.h>
#include <math.h>
#include <stdio.h>

#include "test-utils.h"

#pragma STDC FENV_ACCESS ON

/*
 * Test that a function returns the correct value and sets the
 * exception flags correctly. A tolerance specifying the maximum
 * relative error allowed may be specified. For the 'testall'
 * functions, the tolerance is specified in ulps.
 *
 * These are macros instead of functions so that assert provides more
 * meaningful error messages.
 */
#define	test_tol(func, x, result, tol, excepts) do {			\
	volatile long double _in = (x), _out = (result);		\
	assert(feclearexcept(FE_ALL_EXCEPT) == 0);			\
	assert(fpequal_tol(func(_in), _out, (tol), CS_BOTH));		\
	assert(((void)func, fetestexcept(ALL_STD_EXCEPT) == (excepts))); \
} while (0)
#define test(func, x, result, excepts)					\
	test_tol(func, (x), (result), 0, (excepts))

#define	_testall_tol(prefix, x, result, tol, excepts) do {		\
	test_tol(prefix, (double)(x), (double)(result),			\
		 (tol) * ldexp(1.0, 1 - DBL_MANT_DIG), (excepts));	\
	test_tol(prefix##f, (float)(x), (float)(result),		\
		 (tol) * ldexpf(1.0, 1 - FLT_MANT_DIG), (excepts));	\
} while (0)

#if LDBL_PREC == 53
#define	testall_tol	_testall_tol
#else
#define	testall_tol(prefix, x, result, tol, excepts) do {		\
	_testall_tol(prefix, x, result, tol, excepts);			\
	test_tol(prefix##l, (x), (result),				\
		 (tol) * ldexpl(1.0, 1 - LDBL_MANT_DIG), (excepts));	\
} while (0)
#endif

#define testall(prefix, x, result, excepts)				\
	testall_tol(prefix, (x), (result), 0, (excepts))

#define	test2_tol(func, y, x, result, tol, excepts) do {		\
	volatile long double _iny = (y), _inx = (x), _out = (result);	\
	assert(feclearexcept(FE_ALL_EXCEPT) == 0);			\
	assert(fpequal_tol(func(_iny, _inx), _out, (tol), CS_BOTH));	\
	assert(((void)func, fetestexcept(ALL_STD_EXCEPT) == (excepts))); \
} while (0)
#define test2(func, y, x, result, excepts)				\
	test2_tol(func, (y), (x), (result), 0, (excepts))

#define	_testall2_tol(prefix, y, x, result, tol, excepts) do {		\
	test2_tol(prefix, (double)(y), (double)(x), (double)(result),	\
		  (tol) * ldexp(1.0, 1 - DBL_MANT_DIG), (excepts));	\
	test2_tol(prefix##f, (float)(y), (float)(x), (float)(result),	\
		  (tol) * ldexpf(1.0, 1 - FLT_MANT_DIG), (excepts));	\
} while (0)

#if LDBL_PREC == 53
#define	testall2_tol	_testall2_tol
#else
#define	testall2_tol(prefix, y, x, result, tol, excepts) do {		\
	_testall2_tol(prefix, y, x, result, tol, excepts);		\
	test2_tol(prefix##l, (y), (x), (result),			\
		  (tol) * ldexpl(1.0, 1 - LDBL_MANT_DIG), (excepts));	\
} while (0)
#endif

#define testall2(prefix, y, x, result, excepts)				\
	testall2_tol(prefix, (y), (x), (result), 0, (excepts))

static long double
pi =   3.14159265358979323846264338327950280e+00L,
pio3 = 1.04719755119659774615421446109316766e+00L,
c3pi = 9.42477796076937971538793014983850839e+00L,
c7pi = 2.19911485751285526692385036829565196e+01L,
c5pio3 = 5.23598775598298873077107230546583851e+00L,
sqrt2m1 = 4.14213562373095048801688724209698081e-01L;


/*
 * Test special case inputs in asin(), acos() and atan(): signed
 * zeroes, infinities, and NaNs.
 */
static void
test_special(void)
{

	testall(asin, 0.0, 0.0, 0);
	testall(acos, 0.0, pi / 2, FE_INEXACT);
	testall(atan, 0.0, 0.0, 0);
	testall(asin, -0.0, -0.0, 0);
	testall(acos, -0.0, pi / 2, FE_INEXACT);
	testall(atan, -0.0, -0.0, 0);

	testall(asin, INFINITY, NAN, FE_INVALID);
	testall(acos, INFINITY, NAN, FE_INVALID);
	testall(atan, INFINITY, pi / 2, FE_INEXACT);
	testall(asin, -INFINITY, NAN, FE_INVALID);
	testall(acos, -INFINITY, NAN, FE_INVALID);
	testall(atan, -INFINITY, -pi / 2, FE_INEXACT);

	testall(asin, NAN, NAN, 0);
	testall(acos, NAN, NAN, 0);
	testall(atan, NAN, NAN, 0);
}

/*
 * Test special case inputs in atan2(), where the exact value of y/x is
 * zero or non-finite.
 */
static void
test_special_atan2(void)
{
	long double z;
	int e;

	testall2(atan2, 0.0, -0.0, pi, FE_INEXACT);
	testall2(atan2, -0.0, -0.0, -pi, FE_INEXACT);
	testall2(atan2, 0.0, 0.0, 0.0, 0);
	testall2(atan2, -0.0, 0.0, -0.0, 0);

	testall2(atan2, INFINITY, -INFINITY, c3pi / 4, FE_INEXACT);
	testall2(atan2, -INFINITY, -INFINITY, -c3pi / 4, FE_INEXACT);
	testall2(atan2, INFINITY, INFINITY, pi / 4, FE_INEXACT);
	testall2(atan2, -INFINITY, INFINITY, -pi / 4, FE_INEXACT);

	/* Tests with one input in the range (0, Inf]. */
	z = 1.23456789L;
	for (e = FLT_MIN_EXP - FLT_MANT_DIG; e <= FLT_MAX_EXP; e++) {
		test2(atan2f, 0.0, ldexpf(z, e), 0.0, 0);
		test2(atan2f, -0.0, ldexpf(z, e), -0.0, 0);
		test2(atan2f, 0.0, ldexpf(-z, e), (float)pi, FE_INEXACT);
		test2(atan2f, -0.0, ldexpf(-z, e), (float)-pi, FE_INEXACT);
		test2(atan2f, ldexpf(z, e), 0.0, (float)pi / 2, FE_INEXACT);
		test2(atan2f, ldexpf(z, e), -0.0, (float)pi / 2, FE_INEXACT);
		test2(atan2f, ldexpf(-z, e), 0.0, (float)-pi / 2, FE_INEXACT);
		test2(atan2f, ldexpf(-z, e), -0.0, (float)-pi / 2, FE_INEXACT);
	}
	for (e = DBL_MIN_EXP - DBL_MANT_DIG; e <= DBL_MAX_EXP; e++) {
		test2(atan2, 0.0, ldexp(z, e), 0.0, 0);
		test2(atan2, -0.0, ldexp(z, e), -0.0, 0);
		test2(atan2, 0.0, ldexp(-z, e), (double)pi, FE_INEXACT);
		test2(atan2, -0.0, ldexp(-z, e), (double)-pi, FE_INEXACT);
		test2(atan2, ldexp(z, e), 0.0, (double)pi / 2, FE_INEXACT);
		test2(atan2, ldexp(z, e), -0.0, (double)pi / 2, FE_INEXACT);
		test2(atan2, ldexp(-z, e), 0.0, (double)-pi / 2, FE_INEXACT);
		test2(atan2, ldexp(-z, e), -0.0, (double)-pi / 2, FE_INEXACT);
	}
	for (e = LDBL_MIN_EXP - LDBL_MANT_DIG; e <= LDBL_MAX_EXP; e++) {
		test2(atan2l, 0.0, ldexpl(z, e), 0.0, 0);
		test2(atan2l, -0.0, ldexpl(z, e), -0.0, 0);
		test2(atan2l, 0.0, ldexpl(-z, e), pi, FE_INEXACT);
		test2(atan2l, -0.0, ldexpl(-z, e), -pi, FE_INEXACT);
		test2(atan2l, ldexpl(z, e), 0.0, pi / 2, FE_INEXACT);
		test2(atan2l, ldexpl(z, e), -0.0, pi / 2, FE_INEXACT);
		test2(atan2l, ldexpl(-z, e), 0.0, -pi / 2, FE_INEXACT);
		test2(atan2l, ldexpl(-z, e), -0.0, -pi / 2, FE_INEXACT);
	}

	/* Tests with one input in the range (0, Inf). */
	for (e = FLT_MIN_EXP - FLT_MANT_DIG; e <= FLT_MAX_EXP - 1; e++) {
		test2(atan2f, ldexpf(z, e), INFINITY, 0.0, 0);
		test2(atan2f, ldexpf(-z,e), INFINITY, -0.0, 0);
		test2(atan2f, ldexpf(z, e), -INFINITY, (float)pi, FE_INEXACT);
		test2(atan2f, ldexpf(-z,e), -INFINITY, (float)-pi, FE_INEXACT);
		test2(atan2f, INFINITY, ldexpf(z,e), (float)pi/2, FE_INEXACT);
		test2(atan2f, INFINITY, ldexpf(-z,e), (float)pi/2, FE_INEXACT);
		test2(atan2f, -INFINITY, ldexpf(z,e), (float)-pi/2,FE_INEXACT);
		test2(atan2f, -INFINITY, ldexpf(-z,e),(float)-pi/2,FE_INEXACT);
	}
	for (e = DBL_MIN_EXP - DBL_MANT_DIG; e <= DBL_MAX_EXP - 1; e++) {
		test2(atan2, ldexp(z, e), INFINITY, 0.0, 0);
		test2(atan2, ldexp(-z,e), INFINITY, -0.0, 0);
		test2(atan2, ldexp(z, e), -INFINITY, (double)pi, FE_INEXACT);
		test2(atan2, ldexp(-z,e), -INFINITY, (double)-pi, FE_INEXACT);
		test2(atan2, INFINITY, ldexp(z,e), (double)pi/2, FE_INEXACT);
		test2(atan2, INFINITY, ldexp(-z,e), (double)pi/2, FE_INEXACT);
		test2(atan2, -INFINITY, ldexp(z,e), (double)-pi/2,FE_INEXACT);
		test2(atan2, -INFINITY, ldexp(-z,e),(double)-pi/2,FE_INEXACT);
	}
	for (e = LDBL_MIN_EXP - LDBL_MANT_DIG; e <= LDBL_MAX_EXP - 1; e++) {
		test2(atan2l, ldexpl(z, e), INFINITY, 0.0, 0);
		test2(atan2l, ldexpl(-z,e), INFINITY, -0.0, 0);
		test2(atan2l, ldexpl(z, e), -INFINITY, pi, FE_INEXACT);
		test2(atan2l, ldexpl(-z,e), -INFINITY, -pi, FE_INEXACT);
		test2(atan2l, INFINITY, ldexpl(z, e), pi / 2, FE_INEXACT);
		test2(atan2l, INFINITY, ldexpl(-z, e), pi / 2, FE_INEXACT);
		test2(atan2l, -INFINITY, ldexpl(z, e), -pi / 2, FE_INEXACT);
		test2(atan2l, -INFINITY, ldexpl(-z, e), -pi / 2, FE_INEXACT);
	}
}

/*
 * Test various inputs to asin(), acos() and atan() and verify that the
 * results are accurate to within 1 ulp.
 */
static void
test_accuracy(void)
{

	/* We expect correctly rounded results for these basic cases. */
	testall(asin, 1.0, pi / 2, FE_INEXACT);
	testall(acos, 1.0, 0, 0);
	testall(atan, 1.0, pi / 4, FE_INEXACT);
	testall(asin, -1.0, -pi / 2, FE_INEXACT);
	testall(acos, -1.0, pi, FE_INEXACT);
	testall(atan, -1.0, -pi / 4, FE_INEXACT);

	/*
	 * Here we expect answers to be within 1 ulp, although inexactness
	 * in the input, combined with double rounding, could cause larger
	 * errors.
	 */

	testall_tol(asin, sqrtl(2) / 2, pi / 4, 1, FE_INEXACT);
	testall_tol(acos, sqrtl(2) / 2, pi / 4, 1, FE_INEXACT);
	testall_tol(asin, -sqrtl(2) / 2, -pi / 4, 1, FE_INEXACT);
	testall_tol(acos, -sqrtl(2) / 2, c3pi / 4, 1, FE_INEXACT);

	testall_tol(asin, sqrtl(3) / 2, pio3, 1, FE_INEXACT);
	testall_tol(acos, sqrtl(3) / 2, pio3 / 2, 1, FE_INEXACT);
	testall_tol(atan, sqrtl(3), pio3, 1, FE_INEXACT);
	testall_tol(asin, -sqrtl(3) / 2, -pio3, 1, FE_INEXACT);
	testall_tol(acos, -sqrtl(3) / 2, c5pio3 / 2, 1, FE_INEXACT);
	testall_tol(atan, -sqrtl(3), -pio3, 1, FE_INEXACT);

	testall_tol(atan, sqrt2m1, pi / 8, 1, FE_INEXACT);
	testall_tol(atan, -sqrt2m1, -pi / 8, 1, FE_INEXACT);
}

/*
 * Test inputs to atan2() where x is a power of 2. These are easy cases
 * because y/x is exact.
 */
static void
test_p2x_atan2(void)
{

	testall2(atan2, 1.0, 1.0, pi / 4, FE_INEXACT);
	testall2(atan2, 1.0, -1.0, c3pi / 4, FE_INEXACT);
	testall2(atan2, -1.0, 1.0, -pi / 4, FE_INEXACT);
	testall2(atan2, -1.0, -1.0, -c3pi / 4, FE_INEXACT);

	testall2_tol(atan2, sqrt2m1 * 2, 2.0, pi / 8, 1, FE_INEXACT);
	testall2_tol(atan2, sqrt2m1 * 2, -2.0, c7pi / 8, 1, FE_INEXACT);
	testall2_tol(atan2, -sqrt2m1 * 2, 2.0, -pi / 8, 1, FE_INEXACT);
	testall2_tol(atan2, -sqrt2m1 * 2, -2.0, -c7pi / 8, 1, FE_INEXACT);

	testall2_tol(atan2, sqrtl(3) * 0.5, 0.5, pio3, 1, FE_INEXACT);
	testall2_tol(atan2, sqrtl(3) * 0.5, -0.5, pio3 * 2, 1, FE_INEXACT);
	testall2_tol(atan2, -sqrtl(3) * 0.5, 0.5, -pio3, 1, FE_INEXACT);
	testall2_tol(atan2, -sqrtl(3) * 0.5, -0.5, -pio3 * 2, 1, FE_INEXACT);
}

/*
 * Test inputs very close to 0.
 */
static void
test_tiny(void)
{
	float tiny = 0x1.23456p-120f;

	testall(asin, tiny, tiny, FE_INEXACT);
	testall(acos, tiny, pi / 2, FE_INEXACT);
	testall(atan, tiny, tiny, FE_INEXACT);

	testall(asin, -tiny, -tiny, FE_INEXACT);
	testall(acos, -tiny, pi / 2, FE_INEXACT);
	testall(atan, -tiny, -tiny, FE_INEXACT);

	/* Test inputs to atan2() that would cause y/x to underflow. */
	test2(atan2f, 0x1.0p-100, 0x1.0p100, 0.0, FE_INEXACT | FE_UNDERFLOW);
	test2(atan2, 0x1.0p-1000, 0x1.0p1000, 0.0, FE_INEXACT | FE_UNDERFLOW);
	test2(atan2l, ldexpl(1.0, 100 - LDBL_MAX_EXP),
	      ldexpl(1.0, LDBL_MAX_EXP - 100), 0.0, FE_INEXACT | FE_UNDERFLOW);
	test2(atan2f, -0x1.0p-100, 0x1.0p100, -0.0, FE_INEXACT | FE_UNDERFLOW);
	test2(atan2, -0x1.0p-1000, 0x1.0p1000, -0.0, FE_INEXACT | FE_UNDERFLOW);
	test2(atan2l, -ldexpl(1.0, 100 - LDBL_MAX_EXP),
	      ldexpl(1.0, LDBL_MAX_EXP - 100), -0.0, FE_INEXACT | FE_UNDERFLOW);
	test2(atan2f, 0x1.0p-100, -0x1.0p100, (float)pi, FE_INEXACT);
	test2(atan2, 0x1.0p-1000, -0x1.0p1000, (double)pi, FE_INEXACT);
	test2(atan2l, ldexpl(1.0, 100 - LDBL_MAX_EXP),
	      -ldexpl(1.0, LDBL_MAX_EXP - 100), pi, FE_INEXACT);
	test2(atan2f, -0x1.0p-100, -0x1.0p100, (float)-pi, FE_INEXACT);
	test2(atan2, -0x1.0p-1000, -0x1.0p1000, (double)-pi, FE_INEXACT);
	test2(atan2l, -ldexpl(1.0, 100 - LDBL_MAX_EXP),
	      -ldexpl(1.0, LDBL_MAX_EXP - 100), -pi, FE_INEXACT);
}

/*
 * Test very large inputs to atan().
 */
static void
test_atan_huge(void)
{
	float huge = 0x1.23456p120;

	testall(atan, huge, pi / 2, FE_INEXACT);
	testall(atan, -huge, -pi / 2, FE_INEXACT);

	/* Test inputs to atan2() that would cause y/x to overflow. */
	test2(atan2f, 0x1.0p100, 0x1.0p-100, (float)pi / 2, FE_INEXACT);
	test2(atan2, 0x1.0p1000, 0x1.0p-1000, (double)pi / 2, FE_INEXACT);
	test2(atan2l, ldexpl(1.0, LDBL_MAX_EXP - 100),
	      ldexpl(1.0, 100 - LDBL_MAX_EXP), pi / 2, FE_INEXACT);
	test2(atan2f, -0x1.0p100, 0x1.0p-100, (float)-pi / 2, FE_INEXACT);
	test2(atan2, -0x1.0p1000, 0x1.0p-1000, (double)-pi / 2, FE_INEXACT);
	test2(atan2l, -ldexpl(1.0, LDBL_MAX_EXP - 100),
	      ldexpl(1.0, 100 - LDBL_MAX_EXP), -pi / 2, FE_INEXACT);

	test2(atan2f, 0x1.0p100, -0x1.0p-100, (float)pi / 2, FE_INEXACT);
	test2(atan2, 0x1.0p1000, -0x1.0p-1000, (double)pi / 2, FE_INEXACT);
	test2(atan2l, ldexpl(1.0, LDBL_MAX_EXP - 100),
	      -ldexpl(1.0, 100 - LDBL_MAX_EXP), pi / 2, FE_INEXACT);
	test2(atan2f, -0x1.0p100, -0x1.0p-100, (float)-pi / 2, FE_INEXACT);
	test2(atan2, -0x1.0p1000, -0x1.0p-1000, (double)-pi / 2, FE_INEXACT);
	test2(atan2l, -ldexpl(1.0, LDBL_MAX_EXP - 100),
	      -ldexpl(1.0, 100 - LDBL_MAX_EXP), -pi / 2, FE_INEXACT);
}

/*
 * Test that sin(asin(x)) == x, and similarly for acos() and atan().
 * You need to have a working sinl(), cosl(), and tanl() for these
 * tests to pass.
 */
static long double
sinasinf(float x)
{

	return (sinl(asinf(x)));
}

static long double
sinasin(double x)
{

	return (sinl(asin(x)));
}

static long double
sinasinl(long double x)
{

	return (sinl(asinl(x)));
}

static long double
cosacosf(float x)
{

	return (cosl(acosf(x)));
}

static long double
cosacos(double x)
{

	return (cosl(acos(x)));
}

static long double
cosacosl(long double x)
{

	return (cosl(acosl(x)));
}

static long double
tanatanf(float x)
{

	return (tanl(atanf(x)));
}

static long double
tanatan(double x)
{

	return (tanl(atan(x)));
}

static long double
tanatanl(long double x)
{

	return (tanl(atanl(x)));
}

static void
test_inverse(void)
{
	float i;

	for (i = -1; i <= 1; i += 0x1.0p-12f) {
		testall_tol(sinasin, i, i, 2, i == 0 ? 0 : FE_INEXACT);
		/* The relative error for cosacos is very large near x=0. */
		if (fabsf(i) > 0x1.0p-4f)
			testall_tol(cosacos, i, i, 16, i == 1 ? 0 : FE_INEXACT);
		testall_tol(tanatan, i, i, 2, i == 0 ? 0 : FE_INEXACT);
	}
}

int
main(void)
{

#if defined(__i386__)
	printf("1..0 # SKIP fails all assertions on i386\n");
	return (0);
#endif

	printf("1..7\n");

	test_special();
	printf("ok 1 - special\n");

	test_special_atan2();
	printf("ok 2 - atan2 special\n");

	test_accuracy();
	printf("ok 3 - accuracy\n");

	test_p2x_atan2();
	printf("ok 4 - atan2 p2x\n");

	test_tiny();
	printf("ok 5 - tiny inputs\n");

	test_atan_huge();
	printf("ok 6 - atan huge inputs\n");

	test_inverse();
	printf("ok 7 - inverse\n");

	return (0);
}
