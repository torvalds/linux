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
 * Tests for fma{,f,l}().
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <assert.h>
#include <fenv.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

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
 */
#define	test(func, x, y, z, result, exceptmask, excepts) do {		\
	volatile long double _vx = (x), _vy = (y), _vz = (z);		\
	assert(feclearexcept(FE_ALL_EXCEPT) == 0);			\
	assert(fpequal((func)(_vx, _vy, _vz), (result)));		\
	assert(((void)(func), fetestexcept(exceptmask) == (excepts)));	\
} while (0)

#define	testall(x, y, z, result, exceptmask, excepts)	do {		\
	test(fma, (double)(x), (double)(y), (double)(z),		\
		(double)(result), (exceptmask), (excepts));		\
	test(fmaf, (float)(x), (float)(y), (float)(z),			\
		(float)(result), (exceptmask), (excepts));		\
	test(fmal, (x), (y), (z), (result), (exceptmask), (excepts));	\
} while (0)

/* Test in all rounding modes. */
#define	testrnd(func, x, y, z, rn, ru, rd, rz, exceptmask, excepts)	do { \
	fesetround(FE_TONEAREST);					\
	test((func), (x), (y), (z), (rn), (exceptmask), (excepts));	\
	fesetround(FE_UPWARD);						\
	test((func), (x), (y), (z), (ru), (exceptmask), (excepts));	\
	fesetround(FE_DOWNWARD);					\
	test((func), (x), (y), (z), (rd), (exceptmask), (excepts));	\
	fesetround(FE_TOWARDZERO);					\
	test((func), (x), (y), (z), (rz), (exceptmask), (excepts));	\
} while (0)

/*
 * This is needed because clang constant-folds fma in ways that are incorrect
 * in rounding modes other than FE_TONEAREST.
 */
static volatile double one = 1.0;

static void
test_zeroes(void)
{
	const int rd = (fegetround() == FE_DOWNWARD);

	testall(0.0, 0.0, 0.0, 0.0, ALL_STD_EXCEPT, 0);
	testall(1.0, 0.0, 0.0, 0.0, ALL_STD_EXCEPT, 0);
	testall(0.0, 1.0, 0.0, 0.0, ALL_STD_EXCEPT, 0);
	testall(0.0, 0.0, 1.0, 1.0, ALL_STD_EXCEPT, 0);

	testall(-0.0, 0.0, 0.0, rd ? -0.0 : 0.0, ALL_STD_EXCEPT, 0);
	testall(0.0, -0.0, 0.0, rd ? -0.0 : 0.0, ALL_STD_EXCEPT, 0);
	testall(-0.0, -0.0, 0.0, 0.0, ALL_STD_EXCEPT, 0);
	testall(0.0, 0.0, -0.0, rd ? -0.0 : 0.0, ALL_STD_EXCEPT, 0);
	testall(-0.0, -0.0, -0.0, rd ? -0.0 : 0.0, ALL_STD_EXCEPT, 0);

	testall(-0.0, 0.0, -0.0, -0.0, ALL_STD_EXCEPT, 0);
	testall(0.0, -0.0, -0.0, -0.0, ALL_STD_EXCEPT, 0);

	testall(-one, one, one, rd ? -0.0 : 0.0, ALL_STD_EXCEPT, 0);
	testall(one, -one, one, rd ? -0.0 : 0.0, ALL_STD_EXCEPT, 0);
	testall(-one, -one, -one, rd ? -0.0 : 0.0, ALL_STD_EXCEPT, 0);

	switch (fegetround()) {
	case FE_TONEAREST:
	case FE_TOWARDZERO:
		test(fmaf, -FLT_MIN, FLT_MIN, 0.0, -0.0,
		     ALL_STD_EXCEPT, FE_INEXACT | FE_UNDERFLOW);
		test(fma, -DBL_MIN, DBL_MIN, 0.0, -0.0,
		     ALL_STD_EXCEPT, FE_INEXACT | FE_UNDERFLOW);
		test(fmal, -LDBL_MIN, LDBL_MIN, 0.0, -0.0,
		     ALL_STD_EXCEPT, FE_INEXACT | FE_UNDERFLOW);
	}
}

static void
test_infinities(void)
{

	testall(INFINITY, 1.0, -1.0, INFINITY, ALL_STD_EXCEPT, 0);
	testall(-1.0, INFINITY, 0.0, -INFINITY, ALL_STD_EXCEPT, 0);
	testall(0.0, 0.0, INFINITY, INFINITY, ALL_STD_EXCEPT, 0);
	testall(1.0, 1.0, INFINITY, INFINITY, ALL_STD_EXCEPT, 0);
	testall(1.0, 1.0, -INFINITY, -INFINITY, ALL_STD_EXCEPT, 0);

	testall(INFINITY, -INFINITY, 1.0, -INFINITY, ALL_STD_EXCEPT, 0);
	testall(INFINITY, INFINITY, 1.0, INFINITY, ALL_STD_EXCEPT, 0);
	testall(-INFINITY, -INFINITY, INFINITY, INFINITY, ALL_STD_EXCEPT, 0);

	testall(0.0, INFINITY, 1.0, NAN, ALL_STD_EXCEPT, FE_INVALID);
	testall(INFINITY, 0.0, -0.0, NAN, ALL_STD_EXCEPT, FE_INVALID);

	/* The invalid exception is optional in this case. */
	testall(INFINITY, 0.0, NAN, NAN, ALL_STD_EXCEPT & ~FE_INVALID, 0);

	testall(INFINITY, INFINITY, -INFINITY, NAN,
		ALL_STD_EXCEPT, FE_INVALID);
	testall(-INFINITY, INFINITY, INFINITY, NAN,
		ALL_STD_EXCEPT, FE_INVALID);
	testall(INFINITY, -1.0, INFINITY, NAN,
		ALL_STD_EXCEPT, FE_INVALID);

	test(fmaf, FLT_MAX, FLT_MAX, -INFINITY, -INFINITY, ALL_STD_EXCEPT, 0);
	test(fma, DBL_MAX, DBL_MAX, -INFINITY, -INFINITY, ALL_STD_EXCEPT, 0);
	test(fmal, LDBL_MAX, LDBL_MAX, -INFINITY, -INFINITY,
	     ALL_STD_EXCEPT, 0);
	test(fmaf, FLT_MAX, -FLT_MAX, INFINITY, INFINITY, ALL_STD_EXCEPT, 0);
	test(fma, DBL_MAX, -DBL_MAX, INFINITY, INFINITY, ALL_STD_EXCEPT, 0);
	test(fmal, LDBL_MAX, -LDBL_MAX, INFINITY, INFINITY,
	     ALL_STD_EXCEPT, 0);
}

static void
test_nans(void)
{

	testall(NAN, 0.0, 0.0, NAN, ALL_STD_EXCEPT, 0);
	testall(1.0, NAN, 1.0, NAN, ALL_STD_EXCEPT, 0);
	testall(1.0, -1.0, NAN, NAN, ALL_STD_EXCEPT, 0);
	testall(0.0, 0.0, NAN, NAN, ALL_STD_EXCEPT, 0);
	testall(NAN, NAN, NAN, NAN, ALL_STD_EXCEPT, 0);

	/* x*y should not raise an inexact/overflow/underflow if z is NaN. */
	testall(M_PI, M_PI, NAN, NAN, ALL_STD_EXCEPT, 0);
	test(fmaf, FLT_MIN, FLT_MIN, NAN, NAN, ALL_STD_EXCEPT, 0);
	test(fma, DBL_MIN, DBL_MIN, NAN, NAN, ALL_STD_EXCEPT, 0);
	test(fmal, LDBL_MIN, LDBL_MIN, NAN, NAN, ALL_STD_EXCEPT, 0);
	test(fmaf, FLT_MAX, FLT_MAX, NAN, NAN, ALL_STD_EXCEPT, 0);
	test(fma, DBL_MAX, DBL_MAX, NAN, NAN, ALL_STD_EXCEPT, 0);
	test(fmal, LDBL_MAX, LDBL_MAX, NAN, NAN, ALL_STD_EXCEPT, 0);
}

/*
 * Tests for cases where z is very small compared to x*y.
 */
static void
test_small_z(void)
{

	/* x*y positive, z positive */
	if (fegetround() == FE_UPWARD) {
		test(fmaf, one, one, 0x1.0p-100, 1.0 + FLT_EPSILON,
		     ALL_STD_EXCEPT, FE_INEXACT);
		test(fma, one, one, 0x1.0p-200, 1.0 + DBL_EPSILON,
		     ALL_STD_EXCEPT, FE_INEXACT);
		test(fmal, one, one, 0x1.0p-200, 1.0 + LDBL_EPSILON,
		     ALL_STD_EXCEPT, FE_INEXACT);
	} else {
		testall(0x1.0p100, one, 0x1.0p-100, 0x1.0p100,
			ALL_STD_EXCEPT, FE_INEXACT);
	}

	/* x*y negative, z negative */
	if (fegetround() == FE_DOWNWARD) {
		test(fmaf, -one, one, -0x1.0p-100, -(1.0 + FLT_EPSILON),
		     ALL_STD_EXCEPT, FE_INEXACT);
		test(fma, -one, one, -0x1.0p-200, -(1.0 + DBL_EPSILON),
		     ALL_STD_EXCEPT, FE_INEXACT);
		test(fmal, -one, one, -0x1.0p-200, -(1.0 + LDBL_EPSILON),
		     ALL_STD_EXCEPT, FE_INEXACT);
	} else {
		testall(0x1.0p100, -one, -0x1.0p-100, -0x1.0p100,
			ALL_STD_EXCEPT, FE_INEXACT);
	}

	/* x*y positive, z negative */
	if (fegetround() == FE_DOWNWARD || fegetround() == FE_TOWARDZERO) {
		test(fmaf, one, one, -0x1.0p-100, 1.0 - FLT_EPSILON / 2,
		     ALL_STD_EXCEPT, FE_INEXACT);
		test(fma, one, one, -0x1.0p-200, 1.0 - DBL_EPSILON / 2,
		     ALL_STD_EXCEPT, FE_INEXACT);
		test(fmal, one, one, -0x1.0p-200, 1.0 - LDBL_EPSILON / 2,
		     ALL_STD_EXCEPT, FE_INEXACT);
	} else {
		testall(0x1.0p100, one, -0x1.0p-100, 0x1.0p100,
			ALL_STD_EXCEPT, FE_INEXACT);
	}

	/* x*y negative, z positive */
	if (fegetround() == FE_UPWARD || fegetround() == FE_TOWARDZERO) {
		test(fmaf, -one, one, 0x1.0p-100, -1.0 + FLT_EPSILON / 2,
		     ALL_STD_EXCEPT, FE_INEXACT);
		test(fma, -one, one, 0x1.0p-200, -1.0 + DBL_EPSILON / 2,
		     ALL_STD_EXCEPT, FE_INEXACT);
		test(fmal, -one, one, 0x1.0p-200, -1.0 + LDBL_EPSILON / 2,
		     ALL_STD_EXCEPT, FE_INEXACT);
	} else {
		testall(-0x1.0p100, one, 0x1.0p-100, -0x1.0p100,
			ALL_STD_EXCEPT, FE_INEXACT);
	}
}

/*
 * Tests for cases where z is very large compared to x*y.
 */
static void
test_big_z(void)
{

	/* z positive, x*y positive */
	if (fegetround() == FE_UPWARD) {
		test(fmaf, 0x1.0p-50, 0x1.0p-50, 1.0, 1.0 + FLT_EPSILON,
		     ALL_STD_EXCEPT, FE_INEXACT);
		test(fma, 0x1.0p-100, 0x1.0p-100, 1.0, 1.0 + DBL_EPSILON,
		     ALL_STD_EXCEPT, FE_INEXACT);
		test(fmal, 0x1.0p-100, 0x1.0p-100, 1.0, 1.0 + LDBL_EPSILON,
		     ALL_STD_EXCEPT, FE_INEXACT);
	} else {
		testall(-0x1.0p-50, -0x1.0p-50, 0x1.0p100, 0x1.0p100,
			ALL_STD_EXCEPT, FE_INEXACT);
	}

	/* z negative, x*y negative */
	if (fegetround() == FE_DOWNWARD) {
		test(fmaf, -0x1.0p-50, 0x1.0p-50, -1.0, -(1.0 + FLT_EPSILON),
		     ALL_STD_EXCEPT, FE_INEXACT);
		test(fma, -0x1.0p-100, 0x1.0p-100, -1.0, -(1.0 + DBL_EPSILON),
		     ALL_STD_EXCEPT, FE_INEXACT);
		test(fmal, -0x1.0p-100, 0x1.0p-100, -1.0, -(1.0 + LDBL_EPSILON),
		     ALL_STD_EXCEPT, FE_INEXACT);
	} else {
		testall(0x1.0p-50, -0x1.0p-50, -0x1.0p100, -0x1.0p100,
			ALL_STD_EXCEPT, FE_INEXACT);
	}

	/* z negative, x*y positive */
	if (fegetround() == FE_UPWARD || fegetround() == FE_TOWARDZERO) {
		test(fmaf, -0x1.0p-50, -0x1.0p-50, -1.0,
		     -1.0 + FLT_EPSILON / 2, ALL_STD_EXCEPT, FE_INEXACT);
		test(fma, -0x1.0p-100, -0x1.0p-100, -1.0,
		     -1.0 + DBL_EPSILON / 2, ALL_STD_EXCEPT, FE_INEXACT);
		test(fmal, -0x1.0p-100, -0x1.0p-100, -1.0,
		     -1.0 + LDBL_EPSILON / 2, ALL_STD_EXCEPT, FE_INEXACT);
	} else {
		testall(0x1.0p-50, 0x1.0p-50, -0x1.0p100, -0x1.0p100,
			ALL_STD_EXCEPT, FE_INEXACT);
	}

	/* z positive, x*y negative */
	if (fegetround() == FE_DOWNWARD || fegetround() == FE_TOWARDZERO) {
		test(fmaf, 0x1.0p-50, -0x1.0p-50, 1.0, 1.0 - FLT_EPSILON / 2,
		     ALL_STD_EXCEPT, FE_INEXACT);
		test(fma, 0x1.0p-100, -0x1.0p-100, 1.0, 1.0 - DBL_EPSILON / 2,
		     ALL_STD_EXCEPT, FE_INEXACT);
		test(fmal, 0x1.0p-100, -0x1.0p-100, 1.0, 1.0 - LDBL_EPSILON / 2,
		     ALL_STD_EXCEPT, FE_INEXACT);
	} else {
		testall(-0x1.0p-50, 0x1.0p-50, 0x1.0p100, 0x1.0p100,
			ALL_STD_EXCEPT, FE_INEXACT);
	}
}

static void
test_accuracy(void)
{

	/* ilogb(x*y) - ilogb(z) = 20 */
	testrnd(fmaf, -0x1.c139d8p-51, -0x1.600e7ap32, 0x1.26558cp-38,
		0x1.34e48ap-18, 0x1.34e48cp-18, 0x1.34e48ap-18, 0x1.34e48ap-18,
		ALL_STD_EXCEPT, FE_INEXACT);
	testrnd(fma, -0x1.c139d7b84f1a3p-51, -0x1.600e7a2a16484p32,
		0x1.26558cac31580p-38, 0x1.34e48a78aae97p-18,
		0x1.34e48a78aae97p-18, 0x1.34e48a78aae96p-18,
		0x1.34e48a78aae96p-18, ALL_STD_EXCEPT, FE_INEXACT);
#if LDBL_MANT_DIG == 113
	testrnd(fmal, -0x1.c139d7b84f1a3079263afcc5bae3p-51L,
		-0x1.600e7a2a164840edbe2e7d301a72p32L,
		0x1.26558cac315807eb07e448042101p-38L,
		0x1.34e48a78aae96c76ed36077dd387p-18L,
		0x1.34e48a78aae96c76ed36077dd388p-18L,
		0x1.34e48a78aae96c76ed36077dd387p-18L,
		0x1.34e48a78aae96c76ed36077dd387p-18L,
		ALL_STD_EXCEPT, FE_INEXACT);
#elif LDBL_MANT_DIG == 64
	testrnd(fmal, -0x1.c139d7b84f1a307ap-51L, -0x1.600e7a2a164840eep32L,
		0x1.26558cac315807ecp-38L, 0x1.34e48a78aae96c78p-18L,
		0x1.34e48a78aae96c78p-18L, 0x1.34e48a78aae96c76p-18L,
		0x1.34e48a78aae96c76p-18L, ALL_STD_EXCEPT, FE_INEXACT);
#elif LDBL_MANT_DIG == 53
	testrnd(fmal, -0x1.c139d7b84f1a3p-51L, -0x1.600e7a2a16484p32L,
		0x1.26558cac31580p-38L, 0x1.34e48a78aae97p-18L,
		0x1.34e48a78aae97p-18L, 0x1.34e48a78aae96p-18L,
		0x1.34e48a78aae96p-18L, ALL_STD_EXCEPT, FE_INEXACT);
#endif

	/* ilogb(x*y) - ilogb(z) = -40 */
	testrnd(fmaf, 0x1.98210ap53, 0x1.9556acp-24, 0x1.d87da4p70,
		0x1.d87da4p70, 0x1.d87da6p70, 0x1.d87da4p70, 0x1.d87da4p70,
		ALL_STD_EXCEPT, FE_INEXACT);
	testrnd(fma, 0x1.98210ac83fe2bp53, 0x1.9556ac1475f0fp-24,
		0x1.d87da3aafc60ep70, 0x1.d87da3aafda40p70,
		0x1.d87da3aafda40p70, 0x1.d87da3aafda3fp70,
		0x1.d87da3aafda3fp70, ALL_STD_EXCEPT, FE_INEXACT);
#if LDBL_MANT_DIG == 113
	testrnd(fmal, 0x1.98210ac83fe2a8f65b6278b74cebp53L,
		0x1.9556ac1475f0f28968b61d0de65ap-24L,
		0x1.d87da3aafc60d830aa4c6d73b749p70L,
		0x1.d87da3aafda3f36a69eb86488224p70L,
		0x1.d87da3aafda3f36a69eb86488225p70L,
		0x1.d87da3aafda3f36a69eb86488224p70L,
		0x1.d87da3aafda3f36a69eb86488224p70L,
		ALL_STD_EXCEPT, FE_INEXACT);
#elif LDBL_MANT_DIG == 64
	testrnd(fmal, 0x1.98210ac83fe2a8f6p53L, 0x1.9556ac1475f0f28ap-24L,
		0x1.d87da3aafc60d83p70L, 0x1.d87da3aafda3f36ap70L,
		0x1.d87da3aafda3f36ap70L, 0x1.d87da3aafda3f368p70L,
		0x1.d87da3aafda3f368p70L, ALL_STD_EXCEPT, FE_INEXACT);
#elif LDBL_MANT_DIG == 53
	testrnd(fmal, 0x1.98210ac83fe2bp53L, 0x1.9556ac1475f0fp-24L,
		0x1.d87da3aafc60ep70L, 0x1.d87da3aafda40p70L,
		0x1.d87da3aafda40p70L, 0x1.d87da3aafda3fp70L,
		0x1.d87da3aafda3fp70L, ALL_STD_EXCEPT, FE_INEXACT);
#endif

	/* ilogb(x*y) - ilogb(z) = 0 */
	testrnd(fmaf, 0x1.31ad02p+100, 0x1.2fbf7ap-42, -0x1.c3e106p+58,
		-0x1.64c27cp+56, -0x1.64c27ap+56, -0x1.64c27cp+56,
		-0x1.64c27ap+56, ALL_STD_EXCEPT, FE_INEXACT);
	testrnd(fma, 0x1.31ad012ede8aap+100, 0x1.2fbf79c839067p-42,
		-0x1.c3e106929056ep+58, -0x1.64c282b970a5fp+56,
		-0x1.64c282b970a5ep+56, -0x1.64c282b970a5fp+56,
		-0x1.64c282b970a5ep+56, ALL_STD_EXCEPT, FE_INEXACT);
#if LDBL_MANT_DIG == 113
	testrnd(fmal, 0x1.31ad012ede8aa282fa1c19376d16p+100L,
		 0x1.2fbf79c839066f0f5c68f6d2e814p-42L,
		-0x1.c3e106929056ec19de72bfe64215p+58L,
		-0x1.64c282b970a612598fc025ca8cddp+56L,
		-0x1.64c282b970a612598fc025ca8cddp+56L,
		-0x1.64c282b970a612598fc025ca8cdep+56L,
		-0x1.64c282b970a612598fc025ca8cddp+56L,
		ALL_STD_EXCEPT, FE_INEXACT);
#elif LDBL_MANT_DIG == 64
	testrnd(fmal, 0x1.31ad012ede8aa4eap+100L, 0x1.2fbf79c839066aeap-42L,
		-0x1.c3e106929056e61p+58L, -0x1.64c282b970a60298p+56L,
		-0x1.64c282b970a60298p+56L, -0x1.64c282b970a6029ap+56L,
		-0x1.64c282b970a60298p+56L, ALL_STD_EXCEPT, FE_INEXACT);
#elif LDBL_MANT_DIG == 53
	testrnd(fmal, 0x1.31ad012ede8aap+100L, 0x1.2fbf79c839067p-42L,
		-0x1.c3e106929056ep+58L, -0x1.64c282b970a5fp+56L,
		-0x1.64c282b970a5ep+56L, -0x1.64c282b970a5fp+56L,
		-0x1.64c282b970a5ep+56L, ALL_STD_EXCEPT, FE_INEXACT);
#endif

	/* x*y (rounded) ~= -z */
	/* XXX spurious inexact exceptions */
	testrnd(fmaf, 0x1.bbffeep-30, -0x1.1d164cp-74, 0x1.ee7296p-104,
		-0x1.c46ea8p-128, -0x1.c46ea8p-128, -0x1.c46ea8p-128,
		-0x1.c46ea8p-128, ALL_STD_EXCEPT & ~FE_INEXACT, 0);
	testrnd(fma, 0x1.bbffeea6fc7d6p-30, 0x1.1d164c6cbf078p-74,
		-0x1.ee72993aff948p-104, -0x1.71f72ac7d9d8p-159,
		-0x1.71f72ac7d9d8p-159, -0x1.71f72ac7d9d8p-159,
		-0x1.71f72ac7d9d8p-159, ALL_STD_EXCEPT & ~FE_INEXACT, 0);
#if LDBL_MANT_DIG == 113
	testrnd(fmal, 0x1.bbffeea6fc7d65927d147f437675p-30L,
		0x1.1d164c6cbf078b7a22607d1cd6a2p-74L,
		-0x1.ee72993aff94973876031bec0944p-104L,
		0x1.64e086175b3a2adc36e607058814p-217L,
		0x1.64e086175b3a2adc36e607058814p-217L,
		0x1.64e086175b3a2adc36e607058814p-217L,
		0x1.64e086175b3a2adc36e607058814p-217L,
		ALL_STD_EXCEPT & ~FE_INEXACT, 0);
#elif LDBL_MANT_DIG == 64
	testrnd(fmal, 0x1.bbffeea6fc7d6592p-30L, 0x1.1d164c6cbf078b7ap-74L,
		-0x1.ee72993aff949736p-104L, 0x1.af190e7a1ee6ad94p-168L,
		0x1.af190e7a1ee6ad94p-168L, 0x1.af190e7a1ee6ad94p-168L,
		0x1.af190e7a1ee6ad94p-168L, ALL_STD_EXCEPT & ~FE_INEXACT, 0);
#elif LDBL_MANT_DIG == 53
	testrnd(fmal, 0x1.bbffeea6fc7d6p-30L, 0x1.1d164c6cbf078p-74L,
		-0x1.ee72993aff948p-104L, -0x1.71f72ac7d9d8p-159L,
		-0x1.71f72ac7d9d8p-159L, -0x1.71f72ac7d9d8p-159L,
		-0x1.71f72ac7d9d8p-159L, ALL_STD_EXCEPT & ~FE_INEXACT, 0);
#endif
}

static void
test_double_rounding(void)
{

	/*
	 *     a =  0x1.8000000000001p0
	 *     b =  0x1.8000000000001p0
	 *     c = -0x0.0000000000000000000000000080...1p+1
	 * a * b =  0x1.2000000000001800000000000080p+1
	 *
	 * The correct behavior is to round DOWN to 0x1.2000000000001p+1 in
	 * round-to-nearest mode.  An implementation that computes a*b+c in
	 * double+double precision, however, will get 0x1.20000000000018p+1,
	 * and then round UP.
	 */
	fesetround(FE_TONEAREST);
	test(fma, 0x1.8000000000001p0, 0x1.8000000000001p0,
	     -0x1.0000000000001p-104, 0x1.2000000000001p+1,
	     ALL_STD_EXCEPT, FE_INEXACT);
	fesetround(FE_DOWNWARD);
	test(fma, 0x1.8000000000001p0, 0x1.8000000000001p0,
	     -0x1.0000000000001p-104, 0x1.2000000000001p+1,
	     ALL_STD_EXCEPT, FE_INEXACT);
	fesetround(FE_UPWARD);
	test(fma, 0x1.8000000000001p0, 0x1.8000000000001p0,
	     -0x1.0000000000001p-104, 0x1.2000000000002p+1,
	     ALL_STD_EXCEPT, FE_INEXACT);

	fesetround(FE_TONEAREST);
	test(fmaf, 0x1.800002p+0, 0x1.800002p+0, -0x1.000002p-46, 0x1.200002p+1,
	     ALL_STD_EXCEPT, FE_INEXACT);
	fesetround(FE_DOWNWARD);
	test(fmaf, 0x1.800002p+0, 0x1.800002p+0, -0x1.000002p-46, 0x1.200002p+1,
	     ALL_STD_EXCEPT, FE_INEXACT);
	fesetround(FE_UPWARD);
	test(fmaf, 0x1.800002p+0, 0x1.800002p+0, -0x1.000002p-46, 0x1.200004p+1,
	     ALL_STD_EXCEPT, FE_INEXACT);

	fesetround(FE_TONEAREST);
#if LDBL_MANT_DIG == 64
	test(fmal, 0x1.4p+0L, 0x1.0000000000000004p+0L, 0x1p-128L,
	     0x1.4000000000000006p+0L, ALL_STD_EXCEPT, FE_INEXACT);
#elif LDBL_MANT_DIG == 113
	test(fmal, 0x1.8000000000000000000000000001p+0L,
	     0x1.8000000000000000000000000001p+0L,
	     -0x1.0000000000000000000000000001p-224L,
	     0x1.2000000000000000000000000001p+1L, ALL_STD_EXCEPT, FE_INEXACT);
#endif

}

int
main(void)
{
	int rmodes[] = { FE_TONEAREST, FE_UPWARD, FE_DOWNWARD, FE_TOWARDZERO };
	unsigned i, j;

#if defined(__i386__)
	printf("1..0 # SKIP all testcases fail on i386\n");
	exit(0);
#endif

	j = 1;

	printf("1..19\n");

	for (i = 0; i < nitems(rmodes); i++, j++) {
		printf("rmode = %d\n", rmodes[i]);
		fesetround(rmodes[i]);
		test_zeroes();
		printf("ok %d - fma zeroes\n", j);
	}

	for (i = 0; i < nitems(rmodes); i++, j++) {
#if defined(__amd64__)
		printf("ok %d # SKIP testcase fails assertion on "
		    "amd64\n", j);
		continue;
#else
		printf("rmode = %d\n", rmodes[i]);
		fesetround(rmodes[i]);
		test_infinities();
		printf("ok %d - fma infinities\n", j);
#endif
	}

	fesetround(FE_TONEAREST);
	test_nans();
	printf("ok %d - fma NaNs\n", j);
	j++;

	for (i = 0; i < nitems(rmodes); i++, j++) {
		printf("rmode = %d\n", rmodes[i]);
		fesetround(rmodes[i]);
		test_small_z();
		printf("ok %d - fma small z\n", j);
	}

	for (i = 0; i < nitems(rmodes); i++, j++) {
		printf("rmode = %d\n", rmodes[i]);
		fesetround(rmodes[i]);
		test_big_z();
		printf("ok %d - fma big z\n", j);
	}

	fesetround(FE_TONEAREST);
	test_accuracy();
	printf("ok %d - fma accuracy\n", j);
	j++;

	test_double_rounding();
	printf("ok %d - fma double rounding\n", j);
	j++;

	/*
	 * TODO:
	 * - Tests for subnormals
	 * - Cancellation tests (e.g., z = (double)x*y, but x*y is inexact)
	 */

	return (0);
}
