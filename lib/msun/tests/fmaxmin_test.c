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
 * Tests for fmax{,f,l}() and fmin{,f,l}.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <fenv.h>
#include <float.h>
#include <math.h>
#include <stdio.h>

#include "test-utils.h"

#pragma STDC FENV_ACCESS ON

/*
 * Test whether func(x, y) has the expected result, and make sure no
 * exceptions are raised.
 */
#define	TEST(func, type, x, y, expected) do {				      \
	type __x = (x);	/* convert before we clear exceptions */	      \
	type __y = (y);							      \
	feclearexcept(ALL_STD_EXCEPT);					      \
	long double __result = func((__x), (__y));			      \
	if (fetestexcept(ALL_STD_EXCEPT)) {				      \
		fprintf(stderr, #func "(%.20Lg, %.20Lg) raised 0x%x\n",	      \
			(x), (y), fetestexcept(FE_ALL_EXCEPT));		      \
		ok = 0;							      \
	}								      \
	if (!fpequal(__result, (expected)))	{			      \
		fprintf(stderr, #func "(%.20Lg, %.20Lg) = %.20Lg, "	      \
			"expected %.20Lg\n", (x), (y), __result, (expected)); \
		ok = 0;							      \
	}								      \
} while (0)

static int
testall_r(long double big, long double small)
{
	int ok;

	long double expected_max = isnan(big) ? small : big;
	long double expected_min = isnan(small) ? big : small;
	ok = 1;

	TEST(fmaxf, float, big, small, expected_max);
	TEST(fmaxf, float, small, big, expected_max);
	TEST(fmax, double, big, small, expected_max);
	TEST(fmax, double, small, big, expected_max);
	TEST(fmaxl, long double, big, small, expected_max);
	TEST(fmaxl, long double, small, big, expected_max);
	TEST(fminf, float, big, small, expected_min);
	TEST(fminf, float, small, big, expected_min);
	TEST(fmin, double, big, small, expected_min);
	TEST(fmin, double, small, big, expected_min);
	TEST(fminl, long double, big, small, expected_min);
	TEST(fminl, long double, small, big, expected_min);

	return (ok);
}

static const char *comment = NULL;

/*
 * Test all the functions: fmaxf, fmax, fmaxl, fminf, fmin, and fminl,
 * in all rounding modes and with the arguments in different orders.
 * The input 'big' must be >= 'small'.
 */
static void
testall(int testnum, long double big, long double small)
{
	static const int rmodes[] = {
		FE_TONEAREST, FE_UPWARD, FE_DOWNWARD, FE_TOWARDZERO
	};
	int i;

	for (i = 0; i < 4; i++) {
		fesetround(rmodes[i]);
		if (!testall_r(big, small)) {
			fprintf(stderr, "FAILURE in rounding mode %d\n",
				rmodes[i]);
			break;
		}
	}
	printf("%sok %d - big = %.20Lg, small = %.20Lg%s\n",
	       (i == 4) ? "" : "not ", testnum, big, small,
	       comment == NULL ? "" : comment);
}

/* Clang 3.8.0+ fails the invariants for testcase 6, 7, 10, and 11. */
#if defined(__clang__) && \
    ((__clang_major__ >  3)) || \
    ((__clang_major__ == 3 && __clang_minor__ >= 8))
#define	affected_by_bug_208703
#endif

int
main(void)
{

	printf("1..12\n");

	testall(1, 1.0, 0.0);
	testall(2, 42.0, nextafterf(42.0, -INFINITY));
	testall(3, nextafterf(42.0, INFINITY), 42.0);
	testall(4, -5.0, -5.0);
	testall(5, -3.0, -4.0);
#ifdef affected_by_bug_208703
	comment = "# TODO: testcase 6-7 fails invariant with clang 3.8+ (bug 208703)";
#endif
	testall(6, 1.0, NAN);
	testall(7, INFINITY, NAN);
	comment = NULL;
	testall(8, INFINITY, 1.0);
	testall(9, -3.0, -INFINITY);
	testall(10, 3.0, -INFINITY);
#ifdef affected_by_bug_208703
	comment = "# TODO: testcase 11-12 fails invariant with clang 3.8+ (bug 208703)";
#endif
	testall(11, NAN, NAN);

	/* This test isn't strictly required to work by C99. */
	testall(12, 0.0, -0.0);
	comment = NULL;

	return (0);
}
