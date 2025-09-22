/*	$OpenBSD: t_mknod.c,v 1.3 2021/12/13 16:56:48 deraadt Exp $	*/
/* $NetBSD: t_mknod.c,v 1.2 2012/03/18 07:00:52 jruoho Exp $ */

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jukka Ruohonen.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "macros.h"

#include <sys/stat.h>

#include "atf-c.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <paths.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static char	 path[] = "node";

ATF_TC_WITH_CLEANUP(mknod_err);
ATF_TC_HEAD(mknod_err, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test error conditions of mknod(2) (PR kern/45111)");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(mknod_err, tc)
{
	char buf[PATH_MAX + 1];

	(void)memset(buf, 'x', sizeof(buf));

	errno = 0;
	ATF_REQUIRE_ERRNO(EINVAL, mknod(path, S_IFCHR, -1) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(ENAMETOOLONG, mknod(buf, S_IFCHR, 0) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(EFAULT, mknod((char *)-1, S_IFCHR, 0) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(ENOENT, mknod("/a/b/c/d/e/f/g", S_IFCHR, 0) == -1);
}

ATF_TC_CLEANUP(mknod_err, tc)
{
	(void)unlink(path);
}

ATF_TC_WITH_CLEANUP(mknod_exist);
ATF_TC_HEAD(mknod_exist, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test EEXIST from mknod(2)");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(mknod_exist, tc)
{
	int fd;

	fd = open("/etc/passwd", O_RDONLY);

	if (fd >= 0) {

		(void)close(fd);

		errno = 0;
		ATF_REQUIRE_ERRNO(EEXIST,
		    mknod("/etc/passwd", S_IFCHR, 0) == -1);
	}

	ATF_REQUIRE(mknod(path, S_IFCHR, 0) == 0);

	errno = 0;
	ATF_REQUIRE_ERRNO(EEXIST, mknod(path, S_IFCHR, 0) == -1);

	ATF_REQUIRE(unlink(path) == 0);
}

ATF_TC_CLEANUP(mknod_exist, tc)
{
	(void)unlink(path);
}

ATF_TC_WITH_CLEANUP(mknod_perm);
ATF_TC_HEAD(mknod_perm, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test permissions of mknod(2)");
	atf_tc_set_md_var(tc, "require.user", "unprivileged");
}

ATF_TC_BODY(mknod_perm, tc)
{

	errno = 0;
	ATF_REQUIRE_ERRNO(EPERM, mknod(path, S_IFCHR, 0) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(EPERM, mknod(path, S_IFBLK, 0) == -1);
}

ATF_TC_CLEANUP(mknod_perm, tc)
{
	(void)unlink(path);
}

ATF_TC_WITH_CLEANUP(mknod_stat);
ATF_TC_HEAD(mknod_stat, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of mknod(2)");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(mknod_stat, tc)
{
	struct stat st;

	(void)memset(&st, 0, sizeof(struct stat));

	ATF_REQUIRE(mknod(path, S_IFCHR, 0) == 0);
	ATF_REQUIRE(stat(path, &st) == 0);

	if (S_ISCHR(st.st_mode) == 0)
		atf_tc_fail_nonfatal("invalid mode from mknod(2) (S_IFCHR)");

	ATF_REQUIRE(unlink(path) == 0);

	(void)memset(&st, 0, sizeof(struct stat));

	ATF_REQUIRE(mknod(path, S_IFBLK, 0) == 0);
	ATF_REQUIRE(stat(path, &st) == 0);

	if (S_ISBLK(st.st_mode) == 0)
		atf_tc_fail_nonfatal("invalid mode from mknod(2) (S_IFBLK)");

	ATF_REQUIRE(unlink(path) == 0);

	(void)memset(&st, 0, sizeof(struct stat));

#ifndef __OpenBSD__
	/* OpenBSD only supports FIFO and device special files */
	ATF_REQUIRE(mknod(path, S_IFREG, 0) == 0);
	ATF_REQUIRE(stat(path, &st) == 0);

	if (S_ISREG(st.st_mode) == 0)
		atf_tc_fail_nonfatal("invalid mode from mknod(2) (S_IFREG)");

	ATF_REQUIRE(unlink(path) == 0);
#endif
}

ATF_TC_CLEANUP(mknod_stat, tc)
{
	(void)unlink(path);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, mknod_err);
	ATF_TP_ADD_TC(tp, mknod_exist);
	ATF_TP_ADD_TC(tp, mknod_perm);
	ATF_TP_ADD_TC(tp, mknod_stat);

	return atf_no_error();
}
