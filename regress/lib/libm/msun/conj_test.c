/*	$OpenBSD: conj_test.c,v 1.4 2021/12/13 18:04:28 deraadt Exp $	*/
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
 * Tests for conj{,f,l}()
 */

#include <complex.h>
#include <fenv.h>
#include <math.h>
#include <stdio.h>

#include "test-utils.h"

#pragma	STDC CX_LIMITED_RANGE	OFF

static float complex (*libconjf)(float complex) = conjf;
static double complex (*libconj)(double complex) = conj;
static long double complex (*libconjl)(long double complex) = conjl;
static float (*libcrealf)(float complex) = crealf;
static double (*libcreal)(double complex) = creal;
static long double (*libcreall)(long double complex) = creall;
static float (*libcimagf)(float complex) = cimagf;
static double (*libcimag)(double complex) = cimag;
static long double (*libcimagl)(long double complex) = cimagl;

static const double tests[] = {
	/* a +  bI */
	0.0,	0.0,
	0.0,	1.0,
	1.0,	0.0,
	-1.0,	0.0,
	1.0,	-0.0,
	0.0,	-1.0,
	2.0,	4.0,
	0.0,	INFINITY,
	0.0,	-INFINITY,
	INFINITY, 0.0,
	NAN,	1.0,
	1.0,	NAN,
	NAN,	NAN,
	-INFINITY, INFINITY,
};

ATF_TC_WITHOUT_HEAD(main);
ATF_TC_BODY(main, tc)
{
	static const int ntests = sizeof(tests) / sizeof(tests[0]) / 2;
	complex float in;
	complex long double expected;
	int i;

	for (i = 0; i < ntests; i++) {
		__real__ expected = __real__ in = tests[2 * i];
		__imag__ in = tests[2 * i + 1];
		__imag__ expected = -cimag(in);

		ATF_REQUIRE(fpequal_cs(libcrealf(in), __real__ in, true));
		ATF_REQUIRE(fpequal_cs(libcreal(in), __real__ in, true));
		ATF_REQUIRE(fpequal_cs(libcreall(in), __real__ in, true));
		ATF_REQUIRE(fpequal_cs(libcimagf(in), __imag__ in, true));
		ATF_REQUIRE(fpequal_cs(libcimag(in), __imag__ in, true));
		ATF_REQUIRE(fpequal_cs(libcimagl(in), __imag__ in, true));
 
		ATF_REQUIRE_EQ(0, feclearexcept(FE_ALL_EXCEPT));
		ATF_REQUIRE_MSG(
		    cfpequal(libconjf(in), expected),
		    "conjf(%#.2g + %#.2gI): wrong value", creal(in), cimag(in)
		);
		ATF_REQUIRE_EQ_MSG(0, fetestexcept(FE_ALL_EXCEPT),
		    "conj(%#.2g + %#.2gI): threw an exception: %#x", creal(in),
		    cimag(in), fetestexcept(FE_ALL_EXCEPT));
 
		ATF_REQUIRE_EQ(0, feclearexcept(FE_ALL_EXCEPT));
		ATF_REQUIRE_MSG(cfpequal(libconj(in), expected),
		    "conj(%#.2g + %#.2gI): wrong value", creal(in), cimag(in));
		ATF_REQUIRE_EQ_MSG(0, fetestexcept(FE_ALL_EXCEPT),
		    "conj(%#.2g + %#.2gI): threw an exception: %#x", creal(in),
		    cimag(in), fetestexcept(FE_ALL_EXCEPT));
 
		ATF_REQUIRE_EQ(0, feclearexcept(FE_ALL_EXCEPT));
		ATF_REQUIRE_MSG(cfpequal(libconjl(in), expected),
		    "conjl(%#.2g + %#.2gI): wrong value", creal(in), cimag(in));
		ATF_REQUIRE_EQ_MSG(0, fetestexcept(FE_ALL_EXCEPT),
		    "conjl(%#.2g + %#.2gI): threw an exception: %#x", creal(in),
		    cimag(in), fetestexcept(FE_ALL_EXCEPT));
 	}
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, main);
 
	return (atf_no_error());
}
