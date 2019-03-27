/*-
 * Copyright (c) 2012 David Schultz <das@FreeBSD.org>
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
 * Test that floating-point arithmetic works as specified by the C standard.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <fenv.h>
#include <float.h>
#include <math.h>
#include <stdio.h>

#ifdef  __i386__
#include <ieeefp.h>
#endif

#define	ALL_STD_EXCEPT	(FE_DIVBYZERO | FE_INEXACT | FE_INVALID | \
			 FE_OVERFLOW | FE_UNDERFLOW)

#define	TWICE(x)		((x) + (x))
#define	test(desc, pass)	test1((desc), (pass), 0)
#define	skiptest(desc, pass)	test1((desc), (pass), 1)

#pragma STDC FENV_ACCESS ON

static const float one_f = 1.0 + FLT_EPSILON / 2;
static const double one_d = 1.0 + DBL_EPSILON / 2;
static const long double one_ld = 1.0L + LDBL_EPSILON / 2;

static int testnum, failures;

static void
test1(const char *testdesc, int pass, int skip)
{

	testnum++;
	printf("%sok %d - %s%s\n", pass || skip ? "" : "not ", testnum, 
	    skip ? "(SKIPPED) " : "", testdesc);
	if (!pass && !skip)
		failures++;
}

/*
 * Compare d1 and d2 using special rules: NaN == NaN and +0 != -0.
 */
static int
fpequal(long double d1, long double d2)
{

	if (d1 != d2)
		return (isnan(d1) && isnan(d2));
	return (copysignl(1.0, d1) == copysignl(1.0, d2));
}

void
run_zero_opt_test(double d1, double d2)
{

	test("optimizations don't break the sign of 0",
	     fpequal(d1 - d2, 0.0)
	     && fpequal(-d1 + 0.0, 0.0)
	     && fpequal(-d1 - d2, -0.0)
	     && fpequal(-(d1 - d2), -0.0)
	     && fpequal(-d1 - (-d2), 0.0));
}

void
run_inf_opt_test(double d)
{

	test("optimizations don't break infinities",
	     fpequal(d / d, NAN) && fpequal(0.0 * d, NAN));
}

static inline double
todouble(long double ld)
{

	return (ld);
}

static inline float
tofloat(double d)
{

	return (d);
}

void
run_tests(void)
{
	volatile long double vld;
	long double ld;
	volatile double vd;
	double d;
	volatile float vf;
	float f;
	int x;

	test("sign bits", fpequal(-0.0, -0.0) && !fpequal(0.0, -0.0));

	vd = NAN;
	test("NaN equality", fpequal(NAN, NAN) && NAN != NAN && vd != vd);

	feclearexcept(ALL_STD_EXCEPT);
	test("NaN comparison returns false", !(vd <= vd));
	/*
	 * XXX disabled; gcc/amd64 botches this IEEE 754 requirement by
	 * emitting ucomisd instead of comisd.
	 */
	skiptest("FENV_ACCESS: NaN comparison raises invalid exception",
	    fetestexcept(ALL_STD_EXCEPT) == FE_INVALID);

	vd = 0.0;
	run_zero_opt_test(vd, vd);

	vd = INFINITY;
	run_inf_opt_test(vd);

	feclearexcept(ALL_STD_EXCEPT);
	vd = INFINITY;
	x = (int)vd;
	/* XXX disabled (works with -O0); gcc doesn't support FENV_ACCESS */
	skiptest("FENV_ACCESS: Inf->int conversion raises invalid exception",
	    fetestexcept(ALL_STD_EXCEPT) == FE_INVALID);

	/* Raising an inexact exception here is an IEEE-854 requirement. */
	feclearexcept(ALL_STD_EXCEPT);
	vd = 0.75;
	x = (int)vd;
	test("0.75->int conversion rounds toward 0, raises inexact exception",
	     x == 0 && fetestexcept(ALL_STD_EXCEPT) == FE_INEXACT);

	feclearexcept(ALL_STD_EXCEPT);
	vd = -42.0;
	x = (int)vd;
	test("-42.0->int conversion is exact, raises no exception",
	     x == -42 && fetestexcept(ALL_STD_EXCEPT) == 0);

	feclearexcept(ALL_STD_EXCEPT);
	x = (int)INFINITY;
	/* XXX disabled; gcc doesn't support FENV_ACCESS */
	skiptest("FENV_ACCESS: const Inf->int conversion raises invalid",
	    fetestexcept(ALL_STD_EXCEPT) == FE_INVALID);

	feclearexcept(ALL_STD_EXCEPT);
	x = (int)0.5;
	/* XXX disabled; gcc doesn't support FENV_ACCESS */
	skiptest("FENV_ACCESS: const double->int conversion raises inexact",
	     x == 0 && fetestexcept(ALL_STD_EXCEPT) == FE_INEXACT);

	test("compile-time constants don't have too much precision",
	     one_f == 1.0L && one_d == 1.0L && one_ld == 1.0L);

	test("const minimum rounding precision",
	     1.0F + FLT_EPSILON != 1.0F &&
	     1.0 + DBL_EPSILON != 1.0 &&
	     1.0L + LDBL_EPSILON != 1.0L);

	/* It isn't the compiler's fault if this fails on FreeBSD/i386. */
	vf = FLT_EPSILON;
	vd = DBL_EPSILON;
	vld = LDBL_EPSILON;
	test("runtime minimum rounding precision",
	     1.0F + vf != 1.0F && 1.0 + vd != 1.0 && 1.0L + vld != 1.0L);

	test("explicit float to float conversion discards extra precision",
	     (float)(1.0F + FLT_EPSILON * 0.5F) == 1.0F &&
	     (float)(1.0F + vf * 0.5F) == 1.0F);
	test("explicit double to float conversion discards extra precision",
	     (float)(1.0 + FLT_EPSILON * 0.5) == 1.0F &&
	     (float)(1.0 + vf * 0.5) == 1.0F);
	test("explicit ldouble to float conversion discards extra precision",
	     (float)(1.0L + FLT_EPSILON * 0.5L) == 1.0F &&
	     (float)(1.0L + vf * 0.5L) == 1.0F);

	test("explicit double to double conversion discards extra precision",
	     (double)(1.0 + DBL_EPSILON * 0.5) == 1.0 &&
	     (double)(1.0 + vd * 0.5) == 1.0);
	test("explicit ldouble to double conversion discards extra precision",
	     (double)(1.0L + DBL_EPSILON * 0.5L) == 1.0 &&
	     (double)(1.0L + vd * 0.5L) == 1.0);

	/*
	 * FLT_EVAL_METHOD > 1 implies that float expressions are always
	 * evaluated in double precision or higher, but some compilers get
	 * this wrong when registers spill to memory.  The following expression
	 * forces a spill when there are at most 8 FP registers.
	 */
	test("implicit promption to double or higher precision is consistent",
#if FLT_EVAL_METHOD == 1 || FLT_EVAL_METHOD == 2 || defined(__i386__)
	       TWICE(TWICE(TWICE(TWICE(TWICE(
	           TWICE(TWICE(TWICE(TWICE(1.0F + vf * 0.5F)))))))))
	     == (1.0 + FLT_EPSILON * 0.5) * 512.0
#else
	     1
#endif
	    );

	f = 1.0 + FLT_EPSILON * 0.5;
	d = 1.0L + DBL_EPSILON * 0.5L;
	test("const assignment discards extra precision", f == 1.0F && d == 1.0);

	f = 1.0 + vf * 0.5;
	d = 1.0L + vd * 0.5L;
	test("variable assignment discards explicit extra precision",
	     f == 1.0F && d == 1.0);
	f = 1.0F + vf * 0.5F;
	d = 1.0 + vd * 0.5;
	test("variable assignment discards implicit extra precision",
	     f == 1.0F && d == 1.0);

	test("return discards extra precision",
	     tofloat(1.0 + vf * 0.5) == 1.0F &&
	     todouble(1.0L + vd * 0.5L) == 1.0);

	fesetround(FE_UPWARD);
	/* XXX disabled (works with -frounding-math) */
	skiptest("FENV_ACCESS: constant arithmetic respects rounding mode",
	    1.0F + FLT_MIN == 1.0F + FLT_EPSILON &&
	    1.0 + DBL_MIN == 1.0 + DBL_EPSILON &&
	    1.0L + LDBL_MIN == 1.0L + LDBL_EPSILON);
	fesetround(FE_TONEAREST);

	ld = vld * 0.5;
	test("associativity is respected",
	     1.0L + ld + (LDBL_EPSILON * 0.5) == 1.0L &&
	     1.0L + (LDBL_EPSILON * 0.5) + ld == 1.0L &&
	     ld + 1.0 + (LDBL_EPSILON * 0.5) == 1.0L &&
	     ld + (LDBL_EPSILON * 0.5) + 1.0 == 1.0L + LDBL_EPSILON);
}

int
main(int argc, char *argv[])
{

	printf("1..26\n");

#ifdef  __i386__
	fpsetprec(FP_PE);
#endif
	run_tests();

	return (failures);
}
