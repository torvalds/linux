/*-
 * Copyright (c) 2011 Giorgos Keramidas. All rights reserved.
 * Copyright (c) 2007 Diomidis Spinellis. All rights reserved.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>

#include <assert.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <strings.h>
#include <syslog.h>
#include <time.h>

#include <atf-c.h>

#define KASSERT(val, msg) assert(val)

typedef u_int32_t comp_t;

#define AHZ 1000000

#include "convert.c"

union cf {
	comp_t c;
	float f;
};

static void
check_result(const char *name, float expected, union cf v)
{
	double eps;

	eps = fabs(expected - v.f) / expected;
	ATF_CHECK(eps <= FLT_EPSILON);
	if (eps > FLT_EPSILON) {
		printf("Error in %s\n", name);
		printf("Got      0x%08x %12g\n", v.c, v.f);
		v.f = expected;
		printf("Expected 0x%08x %12g (%.15lg)\n", v.c, v.f, expected);
		printf("Epsilon=%lg, rather than %g\n", eps, FLT_EPSILON);
	}
}

/*
 * Test case for encoding {0 sec, 0 usec} within a reasonable epsilon.
 */

ATF_TC_WITHOUT_HEAD(encode_tv_zero);
ATF_TC_BODY(encode_tv_zero, tc)
{
	union cf v;
	struct timeval tv;

	tv.tv_sec = 0;
	tv.tv_usec = 0;
	v.c = encode_timeval(tv);
	ATF_CHECK(fabs(v.f - 0.0) < FLT_EPSILON);
}

/*
 * Test case for encoding a random long number.
 */

ATF_TC_WITHOUT_HEAD(encode_long);
ATF_TC_BODY(encode_long, tc)
{
	union cf v;
	long l;

	l = random();
	v.c = encode_long(l);
	check_result(atf_tc_get_ident(tc), l, v);
}

/*
 * Test case for encoding a small number of seconds {1 sec, 0 usec}.
 */

ATF_TC_WITHOUT_HEAD(encode_tv_only_sec);
ATF_TC_BODY(encode_tv_only_sec, tc)
{
	union cf v;
	struct timeval tv;

	tv.tv_sec = 1;
	tv.tv_usec = 0;
	v.c = encode_timeval(tv);
	check_result(atf_tc_get_ident(tc),
	    (float)tv.tv_sec * AHZ + tv.tv_usec, v);
}

/*
 * Test case for encoding a small number of usec {0 sec, 1 usec}.
 */

ATF_TC_WITHOUT_HEAD(encode_tv_only_usec);
ATF_TC_BODY(encode_tv_only_usec, tc)
{
	union cf v;
	struct timeval tv;

	tv.tv_sec = 0;
	tv.tv_usec = 1;
	v.c = encode_timeval(tv);
	check_result(atf_tc_get_ident(tc),
	    (float)tv.tv_sec * AHZ + tv.tv_usec, v);
}

/*
 * Test case for encoding a large number of usec {1 sec, 999.999 usec}.
 */

ATF_TC_WITHOUT_HEAD(encode_tv_many_usec);
ATF_TC_BODY(encode_tv_many_usec, tc)
{
	union cf v;
	struct timeval tv;

	tv.tv_sec = 1;
	tv.tv_usec = 999999L;
	v.c = encode_timeval(tv);
	check_result(atf_tc_get_ident(tc),
	    (float)tv.tv_sec * AHZ + tv.tv_usec, v);
}

/*
 * Test case for encoding a huge number of usec {1 sec, 1.000.000 usec} that
 * overflows the usec counter and should show up as an increase in timeval's
 * seconds instead.
 */

ATF_TC_WITHOUT_HEAD(encode_tv_usec_overflow);
ATF_TC_BODY(encode_tv_usec_overflow, tc)
{
	union cf v;
	struct timeval tv;

	tv.tv_sec = 1;
	tv.tv_usec = 1000000L;
	v.c = encode_timeval(tv);
	check_result(atf_tc_get_ident(tc),
	    (float)tv.tv_sec * AHZ + tv.tv_usec, v);
}

/*
 * Test case for encoding a very large number of seconds, one that is very
 * near to the limit of 32-bit signed values.  With a usec value of 999.999
 * microseconds this should result in the largest value we can represent with
 * a timeval struct.
 */

ATF_TC_WITHOUT_HEAD(encode_tv_upper_limit);
ATF_TC_BODY(encode_tv_upper_limit, tc)
{
	union cf v;
	struct timeval tv;

	tv.tv_sec = 2147483647L;
	tv.tv_usec = 999999L;
	v.c = encode_timeval(tv);
	check_result(atf_tc_get_ident(tc),
	    (float)tv.tv_sec * AHZ + tv.tv_usec, v);
}

/*
 * Test case for encoding a million random timeval objects, and checking that
 * the conversion does not diverge too much from the expected values.
 */

ATF_TC_WITHOUT_HEAD(encode_tv_random_million);
ATF_TC_BODY(encode_tv_random_million, tc)
{
	union cf v;
	struct timeval tv;
	long k;

#ifdef __LP64__
	atf_tc_expect_fail("the testcase violates FLT_EPSILON on 64-bit "
	    "platforms, e.g. amd64");
#endif

	ATF_REQUIRE_MSG(unsetenv("TZ") == 0, "unsetting TZ failed; errno=%d", errno);

	for (k = 1; k < 1000000L; k++) {
		tv.tv_sec = random();
		tv.tv_usec = (random() % 1000000L);
		v.c = encode_timeval(tv);
		check_result(atf_tc_get_ident(tc),
		    (float)tv.tv_sec * AHZ + tv.tv_usec, v);
	}
}

/* ---------------------------------------------------------------------
 * Main.
 * --------------------------------------------------------------------- */

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, encode_long);
	ATF_TP_ADD_TC(tp, encode_tv_zero);
	ATF_TP_ADD_TC(tp, encode_tv_only_sec);
	ATF_TP_ADD_TC(tp, encode_tv_only_usec);
	ATF_TP_ADD_TC(tp, encode_tv_many_usec);
	ATF_TP_ADD_TC(tp, encode_tv_usec_overflow);
	ATF_TP_ADD_TC(tp, encode_tv_upper_limit);
	ATF_TP_ADD_TC(tp, encode_tv_random_million);

	return atf_no_error();
}
