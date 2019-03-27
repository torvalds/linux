/*-
 * Copyright (c) 2012 Jilles Tjoelker
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
 * Limited test program for nftw() as specified by IEEE Std. 1003.1-2008.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/wait.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <spawn.h>
#include <unistd.h>

#include <atf-c.h>

extern char **environ;

static char template[] = "testftw.XXXXXXXXXX";
static char dir[PATH_MAX];
static int ftwflags;

static int
cb(const char *path, const struct stat *st, int type, struct FTW *f)
{

	switch (type) {
	case FTW_D:
		if ((ftwflags & FTW_DEPTH) == 0)
			return (0);
		break;
	case FTW_DP:
		if ((ftwflags & FTW_DEPTH) != 0)
			return (0);
		break;
	case FTW_SL:
		if ((ftwflags & FTW_PHYS) != 0)
			return (0);
		break;
	}
	ATF_CHECK_MSG(false,
	    "unexpected path=%s type=%d f.level=%d\n",
	    path, type, f->level);
	return (0);
}

ATF_TC_WITHOUT_HEAD(ftw_test);
ATF_TC_BODY(ftw_test, tc)
{
	int fd;

	ATF_REQUIRE_MSG(mkdtemp(template) != NULL, "mkdtemp failed");

	/* XXX: the path needs to be absolute for the 0/FTW_DEPTH testcases */
	ATF_REQUIRE_MSG(realpath(template, dir) != NULL,
	    "realpath failed; errno=%d", errno);

	fd = open(dir, O_DIRECTORY|O_RDONLY);
	ATF_REQUIRE_MSG(fd != -1, "open failed; errno=%d", errno);

	ATF_REQUIRE_MSG(mkdirat(fd, "d1", 0777) == 0,
	    "mkdirat failed; errno=%d", errno);

	ATF_REQUIRE_MSG(symlinkat(dir, fd, "d1/looper") == 0,
	    "symlinkat failed; errno=%d", errno);

	printf("ftwflags=FTW_PHYS\n");
	ftwflags = FTW_PHYS;
	ATF_REQUIRE_MSG(nftw(dir, cb, 10, ftwflags) != -1,
	    "nftw FTW_PHYS failed; errno=%d", errno);

	printf("ftwflags=FTW_PHYS|FTW_DEPTH\n");
	ftwflags = FTW_PHYS|FTW_DEPTH;
	ATF_REQUIRE_MSG(nftw(dir, cb, 10, ftwflags) != -1,
	    "nftw FTW_PHYS|FTW_DEPTH failed; errno=%d", errno);

	printf("ftwflags=0\n");
	ftwflags = 0;
	ATF_REQUIRE_MSG(nftw(dir, cb, 10, ftwflags) != -1,
	    "nftw 0 failed; errno=%d", errno);

	printf("ftwflags=FTW_DEPTH\n");
	ftwflags = FTW_DEPTH;
	ATF_REQUIRE_MSG(nftw(dir, cb, 10, ftwflags) != -1,
	    "nftw FTW_DEPTH failed; errno=%d", errno);

	close(fd);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, ftw_test);

	return (atf_no_error());
}
