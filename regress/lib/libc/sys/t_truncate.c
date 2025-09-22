/*	$OpenBSD: t_truncate.c,v 1.4 2022/05/24 05:14:30 anton Exp $	*/
/* $NetBSD: t_truncate.c,v 1.3 2017/01/13 20:03:51 christos Exp $ */

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
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char path[] = "truncate";
static const size_t sizes[] = { 8, 16, 512, 1024, 2048, 4094, 3000, 30 };

ATF_TC_WITH_CLEANUP(ftruncate_basic);
ATF_TC_HEAD(ftruncate_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of ftruncate(2)");
}

ATF_TC_BODY(ftruncate_basic, tc)
{
	struct stat st;
	size_t i;
	int fd;

	fd = open(path, O_RDWR | O_CREAT, 0600);
	ATF_REQUIRE(fd >= 0);

	for (i = 0; i < __arraycount(sizes); i++) {

		(void)memset(&st, 0, sizeof(struct stat));

		ATF_REQUIRE(ftruncate(fd, sizes[i]) == 0);
		ATF_REQUIRE(fstat(fd, &st) == 0);

		(void)fprintf(stderr, "truncating to %zu bytes\n", sizes[i]);

		if (sizes[i] != (size_t)st.st_size)
			atf_tc_fail("ftruncate(2) did not truncate");
	}

	(void)close(fd);
	(void)unlink(path);
}

ATF_TC_CLEANUP(ftruncate_basic, tc)
{
	(void)unlink(path);
}

ATF_TC(ftruncate_err);
ATF_TC_HEAD(ftruncate_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test errors from ftruncate(2)");
	atf_tc_set_md_var(tc, "require.user", "unprivileged");
}

ATF_TC_BODY(ftruncate_err, tc)
{
	int fd;

	fd = open("/etc/passwd", O_RDONLY);
	ATF_REQUIRE(fd >= 0);

	errno = 0;
	ATF_REQUIRE_ERRNO(EBADF, ftruncate(-1, 999) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(EINVAL, ftruncate(fd, 999) == -1);

	(void)close(fd);
}

ATF_TC_WITH_CLEANUP(truncate_basic);
ATF_TC_HEAD(truncate_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of truncate(2)");
}

ATF_TC_BODY(truncate_basic, tc)
{
	struct stat st;
	size_t i;
	int fd;

	fd = open(path, O_RDWR | O_CREAT, 0600);
	ATF_REQUIRE(fd >= 0);

	for (i = 0; i < __arraycount(sizes); i++) {

		(void)memset(&st, 0, sizeof(struct stat));

		ATF_REQUIRE(truncate(path, sizes[i]) == 0);
		ATF_REQUIRE(fstat(fd, &st) == 0);

		(void)fprintf(stderr, "truncating to %zu bytes\n", sizes[i]);

		if (sizes[i] != (size_t)st.st_size)
			atf_tc_fail("truncate(2) did not truncate");
	}

	(void)close(fd);
	(void)unlink(path);
}

ATF_TC_CLEANUP(truncate_basic, tc)
{
	(void)unlink(path);
}

ATF_TC(truncate_err);
ATF_TC_HEAD(truncate_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test errors from truncate(2)");
	atf_tc_set_md_var(tc, "require.user", "unprivileged");
}

ATF_TC_BODY(truncate_err, tc)
{
	char buf[PATH_MAX];

	errno = 0;
	ATF_REQUIRE_ERRNO(EFAULT, truncate((void *)-1, 999) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(EISDIR, truncate("/tmp", 999) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(ENOENT, truncate("/a/b/c/d/e/f/g", 999) == -1);

	errno = 0;
	snprintf(buf, sizeof(buf), "%s/truncate_test.root_owned",
	    atf_tc_get_config_var(tc, "srcdir"));
	ATF_REQUIRE_ERRNO(EACCES, truncate(buf, 999) == -1);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, ftruncate_basic);
	ATF_TP_ADD_TC(tp, ftruncate_err);
	ATF_TP_ADD_TC(tp, truncate_basic);
	ATF_TP_ADD_TC(tp, truncate_err);

	return atf_no_error();
}
