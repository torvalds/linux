/*      $OpenBSD: sprintf_test.c,v 1.3 2004/09/16 20:22:26 otto Exp $ */

/*
 * Copyright (c) 2003 Theo de Raadt
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    - Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    - Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <atf-c.h>

const char correct[] =
	"|xx 01 02 03 04\n"
	"|xx 05 06 07 08\n"
	"|xx 09 10 11 12\n"
	"|xx 13 14 15 16\n"
	"|xx 17 18 19 20\n"
	"|xx 21 22 23 24\n"
	"|xx 25 26 27 28\n"
	"|xx 29 30 31 32\n"
	"|xx 33 34 35 36\n"
	"|xx 37 38 39 40\n"
	"|xx 41 42 43 44\n"
	"|xx 45 -1 1 -1 1\n";

const char correct2[] =
	"b bs BSD";
static char buf[1024];
static wchar_t wbuf1[1024], wbuf2[1024];
static const char *temp;

ATF_TC_WITHOUT_HEAD(positional_normal);
ATF_TC_BODY(positional_normal, tc)
{

	/* Test positional arguments */
	snprintf(buf, sizeof buf,
	    "|xx %1$s %2$s %3$s %4$s\n"
	    "|xx %5$s %6$s %7$s %8$s\n"
	    "|xx %9$s %10$s %11$s %12$s\n"
	    "|xx %13$s %14$s %15$s %16$s\n"
	    "|xx %17$s %18$s %19$s %20$s\n"
	    "|xx %21$s %22$s %23$s %24$s\n"
	    "|xx %25$s %26$s %27$s %28$s\n"
	    "|xx %29$s %30$s %31$s %32$s\n"
	    "|xx %33$s %34$s %35$s %36$s\n"
	    "|xx %37$s %38$s %39$s %40$s\n"
	    "|xx %41$s %42$s %43$s %44$s\n"
	    "|xx %45$d %46$ld %47$lld %48$d %49$lld\n",
	    "01", "02", "03", "04", "05", "06",
	    "07", "08", "09", "10", "11", "12",
	    "13", "14", "15", "16", "17", "18",
	    "19", "20", "21", "22", "23", "24",
	    "25", "26", "27", "28", "29", "30",
	    "31", "32", "33", "34", "35", "36",
	    "37", "38", "39", "40", "41", "42",
	    "43", "44", 45, -1L, 1LL, -1, 1LL
	    );
	ATF_REQUIRE_MSG(wcscmp(wbuf1, wbuf2) == 0,
	    "buffers didn't match");
}

ATF_TC_WITHOUT_HEAD(positional_wide);
ATF_TC_BODY(positional_wide, tc)
{

	swprintf(wbuf1, nitems(wbuf1),
	    L"|xx %1$s %2$s %3$s %4$s\n"
	    "|xx %5$s %6$s %7$s %8$s\n"
	    "|xx %9$s %10$s %11$s %12$s\n"
	    "|xx %13$s %14$s %15$s %16$s\n"
	    "|xx %17$s %18$s %19$s %20$s\n"
	    "|xx %21$s %22$s %23$s %24$s\n"
	    "|xx %25$s %26$s %27$s %28$s\n"
	    "|xx %29$s %30$s %31$s %32$s\n"
	    "|xx %33$s %34$s %35$s %36$s\n"
	    "|xx %37$s %38$s %39$s %40$s\n"
	    "|xx %41$s %42$s %43$s %44$s\n"
	    "|xx %45$d %46$ld %47$lld %48$d %49$lld\n",
	    "01", "02", "03", "04", "05", "06",
	    "07", "08", "09", "10", "11", "12",
	    "13", "14", "15", "16", "17", "18",
	    "19", "20", "21", "22", "23", "24",
	    "25", "26", "27", "28", "29", "30",
	    "31", "32", "33", "34", "35", "36",
	    "37", "38", "39", "40", "41", "42",
	    "43", "44", 45, -1L, 1LL, -1, 1LL
	    );
	temp = correct;
	mbsrtowcs(wbuf2, &temp, nitems(wbuf2), NULL);
	ATF_REQUIRE_MSG(wcscmp(wbuf1, wbuf2) == 0,
	    "buffers didn't match");
}

ATF_TC_WITHOUT_HEAD(positional_precision);
ATF_TC_BODY(positional_precision, tc)
{

	snprintf(buf, sizeof buf, "%2$.*4$s %2$.*3$s %1$s",
		 "BSD", "bsd", 2, 1);
	ATF_REQUIRE_MSG(strcmp(buf, correct2) == 0,
	    "buffers didn't match");
}

ATF_TC_WITHOUT_HEAD(positional_precision_wide);
ATF_TC_BODY(positional_precision_wide, tc)
{

	swprintf(wbuf1, sizeof buf, L"%2$.*4$s %2$.*3$s %1$s",
		 "BSD", "bsd", 2, 1);
	temp = correct2;
	mbsrtowcs(wbuf2, &temp, nitems(wbuf2), NULL);
	ATF_REQUIRE_MSG(wcscmp(wbuf1, wbuf2) == 0,
	    "buffers didn't match");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, positional_normal);
	ATF_TP_ADD_TC(tp, positional_wide);
	ATF_TP_ADD_TC(tp, positional_precision);
	ATF_TP_ADD_TC(tp, positional_precision_wide);

	return (atf_no_error());
}
