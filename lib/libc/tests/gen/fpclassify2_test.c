/*-
 * Copyright (c) 2003 Mike Barcroft <mike@FreeBSD.org>
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
 *
 * $FreeBSD$
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <atf-c.h>

ATF_TC_WITHOUT_HEAD(test_fpclassify);
ATF_TC_BODY(test_fpclassify, tc)
{

	ATF_CHECK(fpclassify((float)0) == FP_ZERO);
	ATF_CHECK(fpclassify((float)-0.0) == FP_ZERO);
	ATF_CHECK(fpclassify((float)1) == FP_NORMAL);
	ATF_CHECK(fpclassify((float)1000) == FP_NORMAL);
	ATF_CHECK(fpclassify(HUGE_VALF) == FP_INFINITE);
	ATF_CHECK(fpclassify((float)HUGE_VAL) == FP_INFINITE);
	ATF_CHECK(fpclassify((float)HUGE_VALL) == FP_INFINITE);
	ATF_CHECK(fpclassify(NAN) == FP_NAN);

	ATF_CHECK(fpclassify((double)0) == FP_ZERO);
	ATF_CHECK(fpclassify((double)-0) == FP_ZERO);
	ATF_CHECK(fpclassify((double)1) == FP_NORMAL);
	ATF_CHECK(fpclassify((double)1000) == FP_NORMAL);
	ATF_CHECK(fpclassify(HUGE_VAL) == FP_INFINITE);
	ATF_CHECK(fpclassify((double)HUGE_VALF) == FP_INFINITE);
	ATF_CHECK(fpclassify((double)HUGE_VALL) == FP_INFINITE);
	ATF_CHECK(fpclassify((double)NAN) == FP_NAN);

	ATF_CHECK(fpclassify((long double)0) == FP_ZERO);
	ATF_CHECK(fpclassify((long double)-0.0) == FP_ZERO);
	ATF_CHECK(fpclassify((long double)1) == FP_NORMAL);
	ATF_CHECK(fpclassify((long double)1000) == FP_NORMAL);
	ATF_CHECK(fpclassify(HUGE_VALL) == FP_INFINITE);
	ATF_CHECK(fpclassify((long double)HUGE_VALF) == FP_INFINITE);
	ATF_CHECK(fpclassify((long double)HUGE_VAL) == FP_INFINITE);
	ATF_CHECK(fpclassify((long double)NAN) == FP_NAN);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, test_fpclassify);

	return (atf_no_error());
}
