/*	$OpenBSD: nan_test.c,v 1.3 2021/12/13 18:04:28 deraadt Exp $	*/
/*-
 * Copyright (C) 2007 David Schultz <das@FreeBSD.org>
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
 * Test for nan(), nanf(), and nanl(). We also test that strtod("nan(...)")
 * and sscanf("nan(...)", ...) work identically.
 */

#include <sys/types.h>
#include <fenv.h>
#include <float.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test-utils.h"

static void
testnan(const char *nan_format)
{
	char nan_str[128];
	char *end;
	long double ald[4];
	double ad[4];
	float af[4];
	unsigned i;

	snprintf(nan_str, sizeof(nan_str), "nan(%s)", nan_format);
	for (i = 0; i < nitems(ad); i++) {
		/*
		 * x86 has an 80-bit long double stored in 96 bits,
		 * so we need to initialize the memory for the memcmp()
		 * checks below to work.
		 */
		bzero(&af[i], sizeof(float));
		bzero(&ad[i], sizeof(double));
		bzero(&ald[i], sizeof(long double));
	}

	af[0] = nanf(nan_format);
	ATF_REQUIRE(isnan(af[0]));
	af[1] = strtof(nan_str, &end);
	ATF_REQUIRE(end == nan_str + strlen(nan_str));
	ATF_REQUIRE(sscanf(nan_str, "%e", &af[2]) == 1);
	ATF_REQUIRE(memcmp(&af[0], &af[1], sizeof(float)) == 0);
	ATF_REQUIRE(memcmp(&af[1], &af[2], sizeof(float)) == 0);
	if (*nan_format == '\0') {
		/* nanf("") == strtof("nan") */
		af[3] = strtof("nan", NULL);
		ATF_REQUIRE(memcmp(&af[2], &af[3], sizeof(float)) == 0);
	}

	ad[0] = nan(nan_format);
	ATF_REQUIRE(isnan(ad[0]));
	ad[1] = strtod(nan_str, &end);
	ATF_REQUIRE(end == nan_str + strlen(nan_str));
	ATF_REQUIRE(sscanf(nan_str, "%le", &ad[2]) == 1);
	ATF_REQUIRE(memcmp(&ad[0], &ad[1], sizeof(double)) == 0);
	ATF_REQUIRE(memcmp(&ad[1], &ad[2], sizeof(double)) == 0);
	if (*nan_format == '\0') {
		/* nan("") == strtod("nan") */
		ad[3] = strtod("nan", NULL);
		ATF_REQUIRE(memcmp(&ad[2], &ad[3], sizeof(double)) == 0);
	}

	ald[0] = nanl(nan_format);
	ATF_REQUIRE(isnan(ald[0]));
	ald[1] = strtold(nan_str, &end);
	ATF_REQUIRE(end == nan_str + strlen(nan_str));
	ATF_REQUIRE(sscanf(nan_str, "%Le", &ald[2]) == 1);
	ATF_REQUIRE(memcmp(&ald[0], &ald[1], sizeof(long double)) == 0);
	ATF_REQUIRE(memcmp(&ald[1], &ald[2], sizeof(long double)) == 0);
	if (*nan_format == '\0') {
		/* nanl("") == strtold("nan") */
		ald[3] = strtold("nan", NULL);
		ATF_REQUIRE(memcmp(&ald[2], &ald[3], sizeof(long double)) == 0);
	}
}

ATF_TC_WITHOUT_HEAD(nan);
ATF_TC_BODY(nan, tc)
{
	/* Die if a signalling NaN is returned */
	feenableexcept(FE_INVALID);

	testnan("0x1234");
	testnan("");
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, nan);

	return (atf_no_error());
}
