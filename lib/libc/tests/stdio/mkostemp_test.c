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

/*
 * Test program for mkostemp().
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <atf-c.h>

static const char template[] = "mkostemp.XXXXXXXX";
static int testnum;

#define MISCFLAGS (O_APPEND | O_DIRECT | O_SHLOCK | O_EXLOCK | O_SYNC)

static void
test_one(int oflags)
{
	char tmpf[sizeof(template)];
	struct stat st1, st2;
	int fd;

	memcpy(tmpf, template, sizeof(tmpf));
	fd = mkostemp(tmpf, oflags);
	if (fd < 0) {
		printf("not ok %d - oflags=%#x "
		    "mkostemp() reported failure: %s\n",
		    testnum++, oflags, strerror(errno));
		return;
	}
	if (memcmp(tmpf, template, sizeof(tmpf) - 8 - 1) != 0) {
		printf("not ok %d - oflags=%#x "
		    "returned pathname does not match template: %s\n",
		    testnum++, oflags, tmpf);
		return;
	}
	do {
		if (fcntl(fd, F_GETFD) !=
		    (oflags & O_CLOEXEC ? FD_CLOEXEC : 0)) {
			printf("not ok %d - oflags=%#x "
			    "close-on-exec flag incorrect\n",
			    testnum++, oflags);
			break;
		}
		if ((fcntl(fd, F_GETFL) & MISCFLAGS) != (oflags & MISCFLAGS)) {
			printf("not ok %d - oflags=%#x "
			    "open flags incorrect\n",
			    testnum++, oflags);
			break;
		}
		if (stat(tmpf, &st1) == -1) {
			printf("not ok %d - oflags=%#x "
			    "cannot stat returned pathname %s: %s\n",
			    testnum++, oflags, tmpf, strerror(errno));
			break;
		}
		if (fstat(fd, &st2) == -1) {
			printf("not ok %d - oflags=%#x "
			    "cannot fstat returned fd %d: %s\n",
			    testnum++, oflags, fd, strerror(errno));
			break;
		}
		if (!S_ISREG(st1.st_mode) || (st1.st_mode & 0777) != 0600 ||
		    st1.st_nlink != 1 || st1.st_size != 0) {
			printf("not ok %d - oflags=%#x "
			    "named file attributes incorrect\n",
			    testnum++, oflags);
			break;
		}
		if (!S_ISREG(st2.st_mode) || (st2.st_mode & 0777) != 0600 ||
		    st2.st_nlink != 1 || st2.st_size != 0) {
			printf("not ok %d - oflags=%#x "
			    "opened file attributes incorrect\n",
			    testnum++, oflags);
			break;
		}
		if (st1.st_dev != st2.st_dev || st1.st_ino != st2.st_ino) {
			printf("not ok %d - oflags=%#x "
			    "named and opened file do not match\n",
			    testnum++, oflags);
			break;
		}
		(void)unlink(tmpf);
		if (fstat(fd, &st2) == -1)
			printf("not ok %d - oflags=%#x "
			    "cannot fstat returned fd %d again: %s\n",
			    testnum++, oflags, fd, strerror(errno));
		else if (st2.st_nlink != 0)
			printf("not ok %d - oflags=%#x "
			    "st_nlink is not 0 after unlink\n",
			    testnum++, oflags);
		else
			printf("ok %d - oflags=%#x\n", testnum++, oflags);
		(void)close(fd);
		return;
	} while (0);
	(void)close(fd);
	(void)unlink(tmpf);
}

ATF_TC_WITHOUT_HEAD(zero);
ATF_TC_BODY(zero, tc)
{

	test_one(0);
}

ATF_TC_WITHOUT_HEAD(O_CLOEXEC);
ATF_TC_BODY(O_CLOEXEC, tc)
{

	test_one(O_CLOEXEC);
}

ATF_TC_WITHOUT_HEAD(O_APPEND);
ATF_TC_BODY(O_APPEND, tc)
{

	test_one(O_APPEND);
}

ATF_TC_WITHOUT_HEAD(O_APPEND__O_CLOEXEC);
ATF_TC_BODY(O_APPEND__O_CLOEXEC, tc)
{

	test_one(O_APPEND|O_CLOEXEC);
}

ATF_TC_WITHOUT_HEAD(bad_flags);
ATF_TC_BODY(bad_flags, tc)
{

	char tmpf[sizeof(template)];

	memcpy(tmpf, template, sizeof(tmpf));
	ATF_REQUIRE_MSG(mkostemp(tmpf, O_CREAT) == -1,
		"mkostemp(O_CREAT) succeeded unexpectedly");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, zero);
	ATF_TP_ADD_TC(tp, O_CLOEXEC);
	ATF_TP_ADD_TC(tp, O_APPEND);
	ATF_TP_ADD_TC(tp, O_APPEND__O_CLOEXEC);
	ATF_TP_ADD_TC(tp, bad_flags);

	return (atf_no_error());
}
