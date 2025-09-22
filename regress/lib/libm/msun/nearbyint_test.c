/*	$OpenBSD: nearbyint_test.c,v 1.3 2021/12/13 18:04:28 deraadt Exp $	*/
/*-
 * Copyright (c) 2010 David Schultz <das@FreeBSD.org>
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
 * Tests for nearbyint{,f,l}()
 *
 * TODO:
 * - adapt tests for rint(3)
 * - tests for harder values (more mantissa bits than float)
 */

#include <sys/types.h>
#include <fenv.h>
#include <math.h>
#include <stdio.h>

#include "test-utils.h"

static const int rmodes[] = {
	FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO,
};

/* Make sure we're testing the library, not some broken compiler built-ins. */
static double (*libnearbyint)(double) = nearbyint;
static float (*libnearbyintf)(float) = nearbyintf;
static long double (*libnearbyintl)(long double) = nearbyintl;
#define nearbyintf libnearbyintf
#define nearbyint libnearbyint
#define nearbyintl libnearbyintl

static const struct {
	float in;
	float out[3];	/* one answer per rounding mode except towardzero */
} tests[] = {
/* input	output (expected) */
    { 0.0,	{ 0.0, 0.0, 0.0 }},
    { 0.5,	{ 0.0, 0.0, 1.0 }},
    { M_PI,	{ 3.0, 3.0, 4.0 }},
    { 65536.5,	{ 65536, 65536, 65537 }},
    { INFINITY,	{ INFINITY, INFINITY, INFINITY }},
    { NAN,	{ NAN, NAN, NAN }},
};

/* Get the appropriate result for the current rounding mode. */
static float
get_output(int testindex, int rmodeindex, int negative)
{
	double out;

	if (negative) {	/* swap downwards and upwards if input is negative */
		if (rmodeindex == 1)
			rmodeindex = 2;
		else if (rmodeindex == 2)
			rmodeindex = 1;
	}
	if (rmodeindex == 3) /* FE_TOWARDZERO uses the value for downwards */
		rmodeindex = 1;
	out = tests[testindex].out[rmodeindex];
	return (negative ? -out : out);
}

static void
test_nearby(int testindex)
{
	float in, out;
	unsigned i;

	for (i = 0; i < sizeof(rmodes) / sizeof(rmodes[0]); i++) {
		ATF_REQUIRE_EQ(0, fesetround(rmodes[i]));
		ATF_REQUIRE_EQ(0, feclearexcept(ALL_STD_EXCEPT));

		in = tests[testindex].in;
		out = get_output(testindex, i, 0);
		CHECK_FPEQUAL(out, libnearbyintf(in));
		CHECK_FPEQUAL(out, nearbyint(in));
		CHECK_FPEQUAL(out, nearbyintl(in));
		CHECK_FP_EXCEPTIONS(0, ALL_STD_EXCEPT);

		in = -tests[testindex].in;
		out = get_output(testindex, i, 1);
		CHECK_FPEQUAL(out, nearbyintf(in));
		CHECK_FPEQUAL(out, nearbyint(in));
		CHECK_FPEQUAL(out, nearbyintl(in));
		CHECK_FP_EXCEPTIONS(0, ALL_STD_EXCEPT);
	}
}

static void
test_modf(int testindex)
{
	float in, out;
	float ipartf, ipart_expected;
	double ipart;
	long double ipartl;
	unsigned i;

	for (i = 0; i < sizeof(rmodes) / sizeof(rmodes[0]); i++) {
		ATF_REQUIRE_EQ(0, fesetround(rmodes[i]));
		ATF_REQUIRE_EQ(0, feclearexcept(ALL_STD_EXCEPT));

		in = tests[testindex].in;
		ipart_expected = tests[testindex].out[1];
		out = copysignf(
		    isinf(ipart_expected) ? 0.0 : in - ipart_expected, in);
		ipartl = ipart = ipartf = 42.0;

		CHECK_FPEQUAL(out, modff(in, &ipartf));
		CHECK_FPEQUAL(ipart_expected, ipartf);
		CHECK_FPEQUAL(out, modf(in, &ipart));
		CHECK_FPEQUAL(ipart_expected, ipart);
		CHECK_FPEQUAL(out, modfl(in, &ipartl));
		CHECK_FPEQUAL(ipart_expected, ipartl);
		CHECK_FP_EXCEPTIONS(0, ALL_STD_EXCEPT);

		in = -in;
		ipart_expected = -ipart_expected;
		out = -out;
		ipartl = ipart = ipartf = 42.0;
		CHECK_FPEQUAL(out, modff(in, &ipartf));
		CHECK_FPEQUAL(ipart_expected, ipartf);
		CHECK_FPEQUAL(out, modf(in, &ipart));
		CHECK_FPEQUAL(ipart_expected, ipart);
		CHECK_FPEQUAL(out, modfl(in, &ipartl));
		CHECK_FPEQUAL(ipart_expected, ipartl);
		CHECK_FP_EXCEPTIONS(0, ALL_STD_EXCEPT);
	}
}

ATF_TC_WITHOUT_HEAD(nearbyint);
ATF_TC_BODY(nearbyint, tc)
{
	unsigned i;

	for (i = 0; i < nitems(tests); i++) {
		test_nearby(i);
		test_modf(i);
	}
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, nearbyint);

	return (atf_no_error());
}
