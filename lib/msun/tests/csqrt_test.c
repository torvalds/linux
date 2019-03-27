/*-
 * Copyright (c) 2007 David Schultz <das@FreeBSD.org>
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
 * Tests for csqrt{,f}()
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>

#include <assert.h>
#include <complex.h>
#include <float.h>
#include <math.h>
#include <stdio.h>

#include "test-utils.h"

/*
 * This is a test hook that can point to csqrtl(), _csqrt(), or to _csqrtf().
 * The latter two convert to float or double, respectively, and test csqrtf()
 * and csqrt() with the same arguments.
 */
static long double complex (*t_csqrt)(long double complex);

static long double complex
_csqrtf(long double complex d)
{

	return (csqrtf((float complex)d));
}

static long double complex
_csqrt(long double complex d)
{

	return (csqrt((double complex)d));
}

#pragma	STDC CX_LIMITED_RANGE	OFF

/*
 * Compare d1 and d2 using special rules: NaN == NaN and +0 != -0.
 * Fail an assertion if they differ.
 */
static void
assert_equal(long double complex d1, long double complex d2)
{

	assert(cfpequal(d1, d2));
}

/*
 * Test csqrt for some finite arguments where the answer is exact.
 * (We do not test if it produces correctly rounded answers when the
 * result is inexact, nor do we check whether it throws spurious
 * exceptions.)
 */
static void
test_finite(void)
{
	static const double tests[] = {
	     /* csqrt(a + bI) = x + yI */
	     /* a	b	x	y */
		0,	8,	2,	2,
		0,	-8,	2,	-2,
		4,	0,	2,	0,
		-4,	0,	0,	2,
		3,	4,	2,	1,
		3,	-4,	2,	-1,
		-3,	4,	1,	2,
		-3,	-4,	1,	-2,
		5,	12,	3,	2,
		7,	24,	4,	3,
		9,	40,	5,	4,
		11,	60,	6,	5,
		13,	84,	7,	6,
		33,	56,	7,	4,
		39,	80,	8,	5,
		65,	72,	9,	4,
		987,	9916,	74,	67,
		5289,	6640,	83,	40,
		460766389075.0, 16762287900.0, 678910, 12345
	};
	/*
	 * We also test some multiples of the above arguments. This
	 * array defines which multiples we use. Note that these have
	 * to be small enough to not cause overflow for float precision
	 * with all of the constants in the above table.
	 */
	static const double mults[] = {
		1,
		2,
		3,
		13,
		16,
		0x1.p30,
		0x1.p-30,
	};

	double a, b;
	double x, y;
	unsigned i, j;

	for (i = 0; i < nitems(tests); i += 4) {
		for (j = 0; j < nitems(mults); j++) {
			a = tests[i] * mults[j] * mults[j];
			b = tests[i + 1] * mults[j] * mults[j];
			x = tests[i + 2] * mults[j];
			y = tests[i + 3] * mults[j];
			assert(t_csqrt(CMPLXL(a, b)) == CMPLXL(x, y));
		}
	}

}

/*
 * Test the handling of +/- 0.
 */
static void
test_zeros(void)
{

	assert_equal(t_csqrt(CMPLXL(0.0, 0.0)), CMPLXL(0.0, 0.0));
	assert_equal(t_csqrt(CMPLXL(-0.0, 0.0)), CMPLXL(0.0, 0.0));
	assert_equal(t_csqrt(CMPLXL(0.0, -0.0)), CMPLXL(0.0, -0.0));
	assert_equal(t_csqrt(CMPLXL(-0.0, -0.0)), CMPLXL(0.0, -0.0));
}

/*
 * Test the handling of infinities when the other argument is not NaN.
 */
static void
test_infinities(void)
{
	static const double vals[] = {
		0.0,
		-0.0,
		42.0,
		-42.0,
		INFINITY,
		-INFINITY,
	};

	unsigned i;

	for (i = 0; i < nitems(vals); i++) {
		if (isfinite(vals[i])) {
			assert_equal(t_csqrt(CMPLXL(-INFINITY, vals[i])),
			    CMPLXL(0.0, copysignl(INFINITY, vals[i])));
			assert_equal(t_csqrt(CMPLXL(INFINITY, vals[i])),
			    CMPLXL(INFINITY, copysignl(0.0, vals[i])));
		}
		assert_equal(t_csqrt(CMPLXL(vals[i], INFINITY)),
		    CMPLXL(INFINITY, INFINITY));
		assert_equal(t_csqrt(CMPLXL(vals[i], -INFINITY)),
		    CMPLXL(INFINITY, -INFINITY));
	}
}

/*
 * Test the handling of NaNs.
 */
static void
test_nans(void)
{

	assert(creall(t_csqrt(CMPLXL(INFINITY, NAN))) == INFINITY);
	assert(isnan(cimagl(t_csqrt(CMPLXL(INFINITY, NAN)))));

	assert(isnan(creall(t_csqrt(CMPLXL(-INFINITY, NAN)))));
	assert(isinf(cimagl(t_csqrt(CMPLXL(-INFINITY, NAN)))));

	assert_equal(t_csqrt(CMPLXL(NAN, INFINITY)),
		     CMPLXL(INFINITY, INFINITY));
	assert_equal(t_csqrt(CMPLXL(NAN, -INFINITY)),
		     CMPLXL(INFINITY, -INFINITY));

	assert_equal(t_csqrt(CMPLXL(0.0, NAN)), CMPLXL(NAN, NAN));
	assert_equal(t_csqrt(CMPLXL(-0.0, NAN)), CMPLXL(NAN, NAN));
	assert_equal(t_csqrt(CMPLXL(42.0, NAN)), CMPLXL(NAN, NAN));
	assert_equal(t_csqrt(CMPLXL(-42.0, NAN)), CMPLXL(NAN, NAN));
	assert_equal(t_csqrt(CMPLXL(NAN, 0.0)), CMPLXL(NAN, NAN));
	assert_equal(t_csqrt(CMPLXL(NAN, -0.0)), CMPLXL(NAN, NAN));
	assert_equal(t_csqrt(CMPLXL(NAN, 42.0)), CMPLXL(NAN, NAN));
	assert_equal(t_csqrt(CMPLXL(NAN, -42.0)), CMPLXL(NAN, NAN));
	assert_equal(t_csqrt(CMPLXL(NAN, NAN)), CMPLXL(NAN, NAN));
}

/*
 * Test whether csqrt(a + bi) works for inputs that are large enough to
 * cause overflow in hypot(a, b) + a.  Each of the tests is scaled up to
 * near MAX_EXP.
 */
static void
test_overflow(int maxexp)
{
	long double a, b;
	long double complex result;
	int exp, i;

	assert(maxexp > 0 && maxexp % 2 == 0);

	for (i = 0; i < 4; i++) {
		exp = maxexp - 2 * i;

		/* csqrt(115 + 252*I) == 14 + 9*I */
		a = ldexpl(115 * 0x1p-8, exp);
		b = ldexpl(252 * 0x1p-8, exp);
		result = t_csqrt(CMPLXL(a, b));
		assert(creall(result) == ldexpl(14 * 0x1p-4, exp / 2));
		assert(cimagl(result) == ldexpl(9 * 0x1p-4, exp / 2));

		/* csqrt(-11 + 60*I) = 5 + 6*I */
		a = ldexpl(-11 * 0x1p-6, exp);
		b = ldexpl(60 * 0x1p-6, exp);
		result = t_csqrt(CMPLXL(a, b));
		assert(creall(result) == ldexpl(5 * 0x1p-3, exp / 2));
		assert(cimagl(result) == ldexpl(6 * 0x1p-3, exp / 2));

		/* csqrt(225 + 0*I) == 15 + 0*I */
		a = ldexpl(225 * 0x1p-8, exp);
		b = 0;
		result = t_csqrt(CMPLXL(a, b));
		assert(creall(result) == ldexpl(15 * 0x1p-4, exp / 2));
		assert(cimagl(result) == 0);
	}
}

/*
 * Test that precision is maintained for some large squares.  Set all or
 * some bits in the lower mantdig/2 bits, square the number, and try to
 * recover the sqrt.  Note:
 * 	(x + xI)**2 = 2xxI
 */
static void
test_precision(int maxexp, int mantdig)
{
	long double b, x;
	long double complex result;
	uint64_t mantbits, sq_mantbits;
	int exp, i;

	assert(maxexp > 0 && maxexp % 2 == 0);
	assert(mantdig <= 64);
	mantdig = rounddown(mantdig, 2);

	for (exp = 0; exp <= maxexp; exp += 2) {
		mantbits = ((uint64_t)1 << (mantdig / 2 )) - 1;
		for (i = 0;
		     i < 100 && mantbits > ((uint64_t)1 << (mantdig / 2 - 1));
		     i++, mantbits--) {
			sq_mantbits = mantbits * mantbits;
			/*
			 * sq_mantibts is a mantdig-bit number.  Divide by
			 * 2**mantdig to normalize it to [0.5, 1), where,
			 * note, the binary power will be -1.  Raise it by
			 * 2**exp for the test.  exp is even.  Lower it by
			 * one to reach a final binary power which is also
			 * even.  The result should be exactly
			 * representable, given that mantdig is less than or
			 * equal to the available precision.
			 */
			b = ldexpl((long double)sq_mantbits,
			    exp - 1 - mantdig);
			x = ldexpl(mantbits, (exp - 2 - mantdig) / 2);
			assert(b == x * x * 2);
			result = t_csqrt(CMPLXL(0, b));
			assert(creall(result) == x);
			assert(cimagl(result) == x);
		}
	}
}

int
main(void)
{

	printf("1..18\n");

	/* Test csqrt() */
	t_csqrt = _csqrt;

	test_finite();
	printf("ok 1 - csqrt\n");

	test_zeros();
	printf("ok 2 - csqrt\n");

	test_infinities();
	printf("ok 3 - csqrt\n");

	test_nans();
	printf("ok 4 - csqrt\n");

	test_overflow(DBL_MAX_EXP);
	printf("ok 5 - csqrt\n");

	test_precision(DBL_MAX_EXP, DBL_MANT_DIG);
	printf("ok 6 - csqrt\n");

	/* Now test csqrtf() */
	t_csqrt = _csqrtf;

	test_finite();
	printf("ok 7 - csqrt\n");

	test_zeros();
	printf("ok 8 - csqrt\n");

	test_infinities();
	printf("ok 9 - csqrt\n");

	test_nans();
	printf("ok 10 - csqrt\n");

	test_overflow(FLT_MAX_EXP);
	printf("ok 11 - csqrt\n");

	test_precision(FLT_MAX_EXP, FLT_MANT_DIG);
	printf("ok 12 - csqrt\n");

	/* Now test csqrtl() */
	t_csqrt = csqrtl;

	test_finite();
	printf("ok 13 - csqrt\n");

	test_zeros();
	printf("ok 14 - csqrt\n");

	test_infinities();
	printf("ok 15 - csqrt\n");

	test_nans();
	printf("ok 16 - csqrt\n");

	test_overflow(LDBL_MAX_EXP);
	printf("ok 17 - csqrt\n");

	test_precision(LDBL_MAX_EXP,
#ifndef __i386__
	    LDBL_MANT_DIG
#else
	    DBL_MANT_DIG
#endif
	    );
	printf("ok 18 - csqrt\n");

	return (0);
}
