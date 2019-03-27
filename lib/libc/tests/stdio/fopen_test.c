/*-
 * Copyright (c) 2013 Jilles Tjoelker
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

#include <fcntl.h>
#include <paths.h>
#include <stdio.h>
#include <string.h>

#include <atf-c.h>

/*
 * O_ACCMODE is currently defined incorrectly. This is what it should be.
 * Various code depends on the incorrect value.
 */
#define CORRECT_O_ACCMODE (O_ACCMODE | O_EXEC)

static void
runtest(const char *fname, const char *mode)
{
	FILE *fp;
	int exp_fget_ret, fget_ret, fd, flags, wantedflags;

	fp = fopen(fname, mode);
	ATF_REQUIRE_MSG(fp != NULL,
	    "fopen(\"%s\", \"%s\") failed", fname, mode);
	fd = fileno(fp);
	ATF_REQUIRE_MSG(fd >= 0, "fileno() failed for fopen");
	exp_fget_ret = strchr(mode, 'e') != NULL ? FD_CLOEXEC : 0;
	ATF_REQUIRE_MSG((fget_ret = fcntl(fd, F_GETFD)) == exp_fget_ret,
	    "fcntl(.., F_GETFD) didn't FD_CLOEXEC as expected %d != %d",
	    exp_fget_ret, fget_ret);
	flags = fcntl(fd, F_GETFL);
	if (strchr(mode, '+'))
		wantedflags = O_RDWR | (*mode == 'a' ? O_APPEND : 0);
	else if (*mode == 'r')
		wantedflags = O_RDONLY;
	else if (*mode == 'w')
		wantedflags = O_WRONLY;
	else if (*mode == 'a')
		wantedflags = O_WRONLY | O_APPEND;
	else
		wantedflags = -1;
	fclose(fp);
	if (wantedflags == -1)
		atf_tc_fail("unrecognized mode: %s", mode);
	else if ((flags & (CORRECT_O_ACCMODE | O_APPEND)) != wantedflags)
		atf_tc_fail("incorrect access mode: %s", mode);
}

ATF_TC_WITHOUT_HEAD(fopen_r_test);
ATF_TC_BODY(fopen_r_test, tc)
{

	runtest(_PATH_DEVNULL, "r");
}

ATF_TC_WITHOUT_HEAD(fopen_r_append_test);
ATF_TC_BODY(fopen_r_append_test, tc)
{

	runtest(_PATH_DEVNULL, "r+");
}

ATF_TC_WITHOUT_HEAD(fopen_w_test);
ATF_TC_BODY(fopen_w_test, tc)
{

	runtest(_PATH_DEVNULL, "w");
}

ATF_TC_WITHOUT_HEAD(fopen_w_append_test);
ATF_TC_BODY(fopen_w_append_test, tc)
{

	runtest(_PATH_DEVNULL, "w+");
}

ATF_TC_WITHOUT_HEAD(fopen_a_test);
ATF_TC_BODY(fopen_a_test, tc)
{

	runtest(_PATH_DEVNULL, "a");
}

ATF_TC_WITHOUT_HEAD(fopen_a_append_test);
ATF_TC_BODY(fopen_a_append_test, tc)
{

	runtest(_PATH_DEVNULL, "a+");
}

ATF_TC_WITHOUT_HEAD(fopen_re_test);
ATF_TC_BODY(fopen_re_test, tc)
{

	runtest(_PATH_DEVNULL, "re");
}

ATF_TC_WITHOUT_HEAD(fopen_r_append_e_test);
ATF_TC_BODY(fopen_r_append_e_test, tc)
{

	runtest(_PATH_DEVNULL, "r+e");
}

ATF_TC_WITHOUT_HEAD(fopen_we_test);
ATF_TC_BODY(fopen_we_test, tc)
{

	runtest(_PATH_DEVNULL, "we");
}

ATF_TC_WITHOUT_HEAD(fopen_w_append_e_test);
ATF_TC_BODY(fopen_w_append_e_test, tc)
{

	runtest(_PATH_DEVNULL, "w+e");
}

ATF_TC_WITHOUT_HEAD(fopen_ae_test);
ATF_TC_BODY(fopen_ae_test, tc)
{

	runtest(_PATH_DEVNULL, "ae");
}

ATF_TC_WITHOUT_HEAD(fopen_a_append_e_test);
ATF_TC_BODY(fopen_a_append_e_test, tc)
{

	runtest(_PATH_DEVNULL, "a+e");
}

ATF_TC_WITHOUT_HEAD(fopen_re_append_test);
ATF_TC_BODY(fopen_re_append_test, tc)
{

	runtest(_PATH_DEVNULL, "re+");
}

ATF_TC_WITHOUT_HEAD(fopen_we_append_test);
ATF_TC_BODY(fopen_we_append_test, tc)
{

	runtest(_PATH_DEVNULL, "we+");
}

ATF_TC_WITHOUT_HEAD(fopen_ae_append_test);
ATF_TC_BODY(fopen_ae_append_test, tc)
{

	runtest(_PATH_DEVNULL, "ae+");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, fopen_r_test);
	ATF_TP_ADD_TC(tp, fopen_r_append_test);
	ATF_TP_ADD_TC(tp, fopen_w_test);
	ATF_TP_ADD_TC(tp, fopen_w_append_test);
	ATF_TP_ADD_TC(tp, fopen_a_test);
	ATF_TP_ADD_TC(tp, fopen_a_append_test);
	ATF_TP_ADD_TC(tp, fopen_re_test);
	ATF_TP_ADD_TC(tp, fopen_r_append_e_test);
	ATF_TP_ADD_TC(tp, fopen_we_test);
	ATF_TP_ADD_TC(tp, fopen_w_append_e_test);
	ATF_TP_ADD_TC(tp, fopen_ae_test);
	ATF_TP_ADD_TC(tp, fopen_a_append_e_test);
	ATF_TP_ADD_TC(tp, fopen_re_append_test);
	ATF_TP_ADD_TC(tp, fopen_we_append_test);
	ATF_TP_ADD_TC(tp, fopen_ae_append_test);

	return (atf_no_error());
}

/*
 vim:ts=8:cin:sw=8
 */
