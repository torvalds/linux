/*-
 * Copyright (c) 2014 Jilles Tjoelker
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <errno.h>
#include <paths.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <atf-c.h>

static void
runtest(const char *fname1, const char *mode1, const char *fname2,
    const char *mode2, bool success)
{
	FILE *fp1, *fp2;
	const char *fname2_print;

	fname2_print = fname2 != NULL ? fname2 : "<NULL>";
	fp1 = fopen(fname1, mode1);
	ATF_REQUIRE_MSG(fp1 != NULL,
	    "fopen(\"%s\", \"%s\") failed; errno=%d", fname1, mode1, errno);
	fp2 = freopen(fname2, mode2, fp1);
	if (fp2 == NULL) {
		ATF_REQUIRE_MSG(success == false,
		    "freopen(\"%s\", \"%s\", fopen(\"%s\", \"%s\")) succeeded "
		    "unexpectedly", fname2_print, mode2, fname1, mode1);
		return;
	}
	ATF_REQUIRE_MSG(success == true,
	    "freopen(\"%s\", \"%s\", fopen(\"%s\", \"%s\")) failed: %d",
	    fname2_print, mode2, fname1, mode1, errno);
	fclose(fp2);
}

ATF_TC_WITHOUT_HEAD(null__r__r__test);
ATF_TC_BODY(null__r__r__test, tc)
{

	runtest(_PATH_DEVNULL, "r", NULL, "r", true);
}

ATF_TC_WITHOUT_HEAD(null__w__r__test);
ATF_TC_BODY(null__w__r__test, tc)
{

	runtest(_PATH_DEVNULL, "w", NULL, "r", false);
}

ATF_TC_WITHOUT_HEAD(null__r_append__r__test);
ATF_TC_BODY(null__r_append__r__test, tc)
{

	runtest(_PATH_DEVNULL, "r+", NULL, "r", true);
}

ATF_TC_WITHOUT_HEAD(null__r__w__test);
ATF_TC_BODY(null__r__w__test, tc)
{

	runtest(_PATH_DEVNULL, "r", NULL, "w", false);
}

ATF_TC_WITHOUT_HEAD(null__w__w__test);
ATF_TC_BODY(null__w__w__test, tc)
{

	runtest(_PATH_DEVNULL, "w", NULL, "w", true);
}

ATF_TC_WITHOUT_HEAD(null__r_append__w__test);
ATF_TC_BODY(null__r_append__w__test, tc)
{

	runtest(_PATH_DEVNULL, "r+", NULL, "w", true);
}

ATF_TC_WITHOUT_HEAD(null__r__a__test);
ATF_TC_BODY(null__r__a__test, tc)
{

	runtest(_PATH_DEVNULL, "r", NULL, "a", false);
}

ATF_TC_WITHOUT_HEAD(null__w__a__test);
ATF_TC_BODY(null__w__a__test, tc)
{

	runtest(_PATH_DEVNULL, "w", NULL, "a", true);
}

ATF_TC_WITHOUT_HEAD(null__r_append__a__test);
ATF_TC_BODY(null__r_append__a__test, tc)
{

	runtest(_PATH_DEVNULL, "r+", NULL, "a", true);
}

ATF_TC_WITHOUT_HEAD(null__r__r_append__test);
ATF_TC_BODY(null__r__r_append__test, tc)
{

	runtest(_PATH_DEVNULL, "r", NULL, "r+", false);
}

ATF_TC_WITHOUT_HEAD(null__w__r_append__test);
ATF_TC_BODY(null__w__r_append__test, tc)
{

	runtest(_PATH_DEVNULL, "w", NULL, "r+", false);
}

ATF_TC_WITHOUT_HEAD(null__r_append__r_append__test);
ATF_TC_BODY(null__r_append__r_append__test, tc)
{

	runtest(_PATH_DEVNULL, "r+", NULL, "r+", true);
}

ATF_TC_WITHOUT_HEAD(null__r__w_append__test);
ATF_TC_BODY(null__r__w_append__test, tc)
{

	runtest(_PATH_DEVNULL, "r", NULL, "w+", false);
}

ATF_TC_WITHOUT_HEAD(null__w__w_append__test);
ATF_TC_BODY(null__w__w_append__test, tc)
{

	runtest(_PATH_DEVNULL, "w", NULL, "w+", false);
}

ATF_TC_WITHOUT_HEAD(null__r_append__w_append__test);
ATF_TC_BODY(null__r_append__w_append__test, tc)
{

	runtest(_PATH_DEVNULL, "r+", NULL, "w+", true);
}

ATF_TC_WITHOUT_HEAD(sh__r__r__test);
ATF_TC_BODY(sh__r__r__test, tc)
{

	runtest("/bin/sh", "r", NULL, "r", true);
}

ATF_TC_WITHOUT_HEAD(sh__sh__r__r__test);
ATF_TC_BODY(sh__sh__r__r__test, tc)
{

	runtest("/bin/sh", "r", "/bin/sh", "r", true);
}

ATF_TC_WITHOUT_HEAD(sh__null__r__r__test);
ATF_TC_BODY(sh__null__r__r__test, tc)
{

	runtest("/bin/sh", "r", _PATH_DEVNULL, "r", true);
}

ATF_TC_WITHOUT_HEAD(sh__null__r__w__test);
ATF_TC_BODY(sh__null__r__w__test, tc)
{

	runtest("/bin/sh", "r", _PATH_DEVNULL, "w", true);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, null__r__r__test);
	ATF_TP_ADD_TC(tp, null__w__r__test);
	ATF_TP_ADD_TC(tp, null__r_append__r__test);
	ATF_TP_ADD_TC(tp, null__r__w__test);
	ATF_TP_ADD_TC(tp, null__w__w__test);
	ATF_TP_ADD_TC(tp, null__r_append__w__test);
	ATF_TP_ADD_TC(tp, null__r__a__test);
	ATF_TP_ADD_TC(tp, null__w__a__test);
	ATF_TP_ADD_TC(tp, null__r_append__a__test);
	ATF_TP_ADD_TC(tp, null__r__r_append__test);
	ATF_TP_ADD_TC(tp, null__w__r_append__test);
	ATF_TP_ADD_TC(tp, null__r_append__r_append__test);
	ATF_TP_ADD_TC(tp, null__r__w_append__test);
	ATF_TP_ADD_TC(tp, null__w__w_append__test);
	ATF_TP_ADD_TC(tp, null__r_append__w_append__test);
	ATF_TP_ADD_TC(tp, sh__r__r__test);
	ATF_TP_ADD_TC(tp, sh__sh__r__r__test);
	ATF_TP_ADD_TC(tp, sh__null__r__r__test);
	ATF_TP_ADD_TC(tp, sh__null__r__w__test);

	return (atf_no_error());
}

/*
 vim:ts=8:cin:sw=8
 */
