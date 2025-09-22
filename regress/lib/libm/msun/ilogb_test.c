/*	$OpenBSD: ilogb_test.c,v 1.2 2021/10/22 18:00:22 mbuhl Exp $	*/
/*-
 * Copyright (c) 2004 Stefan Farfeleder
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
 * $FreeBSD: head/lib/msun/tests/ilogb_test.c 292328 2015-12-16 09:11:11Z ngie $
 */

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "test-utils.h"

ATF_TC_WITHOUT_HEAD(ilogb);
ATF_TC_BODY(ilogb, tc)
{
	char buf[128], *end;
	double d;
	int e, i;

	ATF_CHECK_EQ(FP_ILOGB0, ilogb(0));
	ATF_CHECK_EQ(FP_ILOGBNAN, ilogb(NAN));
	ATF_CHECK_EQ(INT_MAX, ilogb(INFINITY));
	for (e = DBL_MIN_EXP - DBL_MANT_DIG; e < DBL_MAX_EXP; e++) {
		snprintf(buf, sizeof(buf), "0x1.p%d", e);
		d = strtod(buf, &end);
		ATF_CHECK_EQ('\0', *end);
		i = ilogb(d);
		ATF_CHECK_EQ_MSG(e, i, "ilogb(%g) returned %d not %d", d, i, e);
	}
}

ATF_TC_WITHOUT_HEAD(ilogbf);
ATF_TC_BODY(ilogbf, tc)
{
	char buf[128], *end;
	float f;
	int e, i;

	ATF_CHECK_EQ(FP_ILOGB0, ilogbf(0));
	ATF_CHECK_EQ(FP_ILOGBNAN, ilogbf(NAN));
	ATF_CHECK_EQ(INT_MAX, ilogbf(INFINITY));
	for (e = FLT_MIN_EXP - FLT_MANT_DIG; e < FLT_MAX_EXP; e++) {
		snprintf(buf, sizeof(buf), "0x1.p%d", e);
		f = strtof(buf, &end);
		ATF_CHECK_EQ('\0', *end);
		i = ilogbf(f);
		ATF_CHECK_EQ_MSG(e, i, "ilogbf(%g) returned %d not %d", f, i,
		    e);
	}
}

ATF_TC_WITHOUT_HEAD(ilogbl);
ATF_TC_BODY(ilogbl, tc)
{
	char buf[128], *end;
	long double ld;
	int e, i;

	ATF_CHECK_EQ(FP_ILOGB0, ilogbl(0));
	ATF_CHECK_EQ(FP_ILOGBNAN, ilogbl(NAN));
	ATF_CHECK_EQ(INT_MAX, ilogbl(INFINITY));
	for (e = LDBL_MIN_EXP - LDBL_MANT_DIG; e < LDBL_MAX_EXP; e++) {
		snprintf(buf, sizeof(buf), "0x1.p%d", e);
		ld = strtold(buf, &end);
		ATF_CHECK_EQ('\0', *end);
		i = ilogbl(ld);
		ATF_CHECK_EQ_MSG(e, i, "ilogbl(%Lg) returned %d not %d", ld, i,
		    e);
	}
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, ilogb);
	ATF_TP_ADD_TC(tp, ilogbf);
	ATF_TP_ADD_TC(tp, ilogbl);

	return (atf_no_error());
}
