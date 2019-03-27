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
#include <fcntl.h>
#include <paths.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <atf-c.h>

static void
runtest(const char *fname, int intmode, const char *strmode, bool success)
{
	FILE *fp;
	int fd;

	fd = open(fname, intmode);
	ATF_REQUIRE_MSG(fd != -1,
	    "open(\"%s\", %#x) failed; errno=%d", fname, intmode, errno);

	fp = fdopen(fd, strmode);
	if (fp == NULL) {
		close(fd);
		ATF_REQUIRE_MSG(success == false,
		    "fdopen(open(\"%s\", %#x), \"%s\") succeeded unexpectedly",
		    fname, intmode, strmode);
		return;
	}
	ATF_REQUIRE_MSG(success == true,
	    "fdopen(open(\"%s\", %#x), \"%s\") failed; errno=%d",
	    fname, intmode, strmode, errno);
	fclose(fp);
}

ATF_TC_WITHOUT_HEAD(null__O_RDONLY__r_test);
ATF_TC_BODY(null__O_RDONLY__r_test, tc)
{

	runtest(_PATH_DEVNULL, O_RDONLY, "r", true);
}

ATF_TC_WITHOUT_HEAD(null__O_WRONLY__r_test);
ATF_TC_BODY(null__O_WRONLY__r_test, tc)
{

	runtest(_PATH_DEVNULL, O_WRONLY, "r", false);
}

ATF_TC_WITHOUT_HEAD(null__O_RDWR__r_test);
ATF_TC_BODY(null__O_RDWR__r_test, tc)
{

	runtest(_PATH_DEVNULL, O_RDWR, "r", true);
}

ATF_TC_WITHOUT_HEAD(null__O_RDONLY__w_test);
ATF_TC_BODY(null__O_RDONLY__w_test, tc)
{

	runtest(_PATH_DEVNULL, O_RDONLY, "w", false);
}

ATF_TC_WITHOUT_HEAD(null__O_WRONLY__w_test);
ATF_TC_BODY(null__O_WRONLY__w_test, tc)
{

	runtest(_PATH_DEVNULL, O_WRONLY, "w", true);
}

ATF_TC_WITHOUT_HEAD(null__O_RDWR__w_test);
ATF_TC_BODY(null__O_RDWR__w_test, tc)
{

	runtest(_PATH_DEVNULL, O_RDWR, "w", true);
}

ATF_TC_WITHOUT_HEAD(null__O_RDONLY__a_test);
ATF_TC_BODY(null__O_RDONLY__a_test, tc)
{

	runtest(_PATH_DEVNULL, O_RDONLY, "a", false);
}

ATF_TC_WITHOUT_HEAD(null__O_WRONLY__a_test);
ATF_TC_BODY(null__O_WRONLY__a_test, tc)
{

	runtest(_PATH_DEVNULL, O_WRONLY, "a", true);
}

ATF_TC_WITHOUT_HEAD(null__O_RDWR__test);
ATF_TC_BODY(null__O_RDWR__test, tc)
{

	runtest(_PATH_DEVNULL, O_RDWR, "a", true);
}

ATF_TC_WITHOUT_HEAD(null__O_RDONLY__r_append);
ATF_TC_BODY(null__O_RDONLY__r_append, tc)
{

	runtest(_PATH_DEVNULL, O_RDONLY, "r+", false);
}

ATF_TC_WITHOUT_HEAD(null__O_WRONLY__r_append);
ATF_TC_BODY(null__O_WRONLY__r_append, tc)
{

	runtest(_PATH_DEVNULL, O_WRONLY, "r+", false);
}

ATF_TC_WITHOUT_HEAD(null__O_RDWR__r_append);
ATF_TC_BODY(null__O_RDWR__r_append, tc)
{

	runtest(_PATH_DEVNULL, O_RDWR, "r+", true);
}

ATF_TC_WITHOUT_HEAD(null__O_RDONLY__w_append);
ATF_TC_BODY(null__O_RDONLY__w_append, tc)
{

	runtest(_PATH_DEVNULL, O_RDONLY, "w+", false);
}

ATF_TC_WITHOUT_HEAD(null__O_WRONLY__w_append);
ATF_TC_BODY(null__O_WRONLY__w_append, tc)
{

	runtest(_PATH_DEVNULL, O_WRONLY, "w+", false);
}

ATF_TC_WITHOUT_HEAD(null__O_RDWR__w_append);
ATF_TC_BODY(null__O_RDWR__w_append, tc)
{

	runtest(_PATH_DEVNULL, O_RDWR, "w+", true);
}

ATF_TC_WITHOUT_HEAD(sh__O_EXEC__r);
ATF_TC_BODY(sh__O_EXEC__r, tc)
{

	runtest("/bin/sh", O_EXEC, "r", false);
}

ATF_TC_WITHOUT_HEAD(sh__O_EXEC__w);
ATF_TC_BODY(sh__O_EXEC__w, tc)
{

	runtest("/bin/sh", O_EXEC, "w", false);
}

ATF_TC_WITHOUT_HEAD(sh__O_EXEC__r_append);
ATF_TC_BODY(sh__O_EXEC__r_append, tc)
{

	runtest("/bin/sh", O_EXEC, "r+", false);
}

ATF_TC_WITHOUT_HEAD(sh__O_EXEC__w_append);
ATF_TC_BODY(sh__O_EXEC__w_append, tc)
{

	runtest("/bin/sh", O_EXEC, "w+", false);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, null__O_RDONLY__r_test);
	ATF_TP_ADD_TC(tp, null__O_WRONLY__r_test);
	ATF_TP_ADD_TC(tp, null__O_RDWR__r_test);
	ATF_TP_ADD_TC(tp, null__O_RDONLY__w_test);
	ATF_TP_ADD_TC(tp, null__O_WRONLY__w_test);
	ATF_TP_ADD_TC(tp, null__O_RDWR__w_test);
	ATF_TP_ADD_TC(tp, null__O_RDONLY__a_test);
	ATF_TP_ADD_TC(tp, null__O_WRONLY__a_test);
	ATF_TP_ADD_TC(tp, null__O_RDWR__test);
	ATF_TP_ADD_TC(tp, null__O_RDONLY__r_append);
	ATF_TP_ADD_TC(tp, null__O_WRONLY__r_append);
	ATF_TP_ADD_TC(tp, null__O_RDWR__r_append);
	ATF_TP_ADD_TC(tp, null__O_RDONLY__w_append);
	ATF_TP_ADD_TC(tp, null__O_WRONLY__w_append);
	ATF_TP_ADD_TC(tp, null__O_RDWR__w_append);
	ATF_TP_ADD_TC(tp, sh__O_EXEC__r);
	ATF_TP_ADD_TC(tp, sh__O_EXEC__w);
	ATF_TP_ADD_TC(tp, sh__O_EXEC__r_append);
	ATF_TP_ADD_TC(tp, sh__O_EXEC__w_append);

	return (atf_no_error());
}

/*
 vim:ts=8:cin:sw=8
 */
