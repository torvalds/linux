/*	$OpenBSD: t_chroot.c,v 1.3 2021/12/13 16:56:48 deraadt Exp $	*/
/* $NetBSD: t_chroot.c,v 1.2 2017/01/10 22:36:29 christos Exp $ */

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

#include <sys/wait.h>
#include <sys/stat.h>

#include "atf-c.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

ATF_TC(chroot_basic);
ATF_TC_HEAD(chroot_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of chroot(2)");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(chroot_basic, tc)
{
	char buf[PATH_MAX];
	int fd, sta;
	pid_t pid;

	(void)memset(buf, '\0', sizeof(buf));
	(void)getcwd(buf, sizeof(buf));
	(void)strlcat(buf, "/dir", sizeof(buf));

	ATF_REQUIRE(mkdir(buf, 0500) == 0);
	ATF_REQUIRE(chdir(buf) == 0);

	pid = fork();
	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {

		if (chroot(buf) != 0)
			_exit(EXIT_FAILURE);

		errno = 0;

		if (chroot("/root") != -1)
			_exit(EXIT_FAILURE);

		if (errno != ENOENT)
			_exit(EXIT_FAILURE);

		fd = open("file", O_RDONLY | O_CREAT, 0600);

		if (fd < 0)
			_exit(EXIT_FAILURE);

		if (close(fd) != 0)
			_exit(EXIT_FAILURE);

		_exit(EXIT_SUCCESS);
	}

	(void)wait(&sta);

	if (WIFEXITED(sta) == 0 || WEXITSTATUS(sta) != EXIT_SUCCESS)
		atf_tc_fail("chroot(2) failed");

	(void)chdir("/");
	(void)strlcat(buf, "/file", sizeof(buf));

	fd = open(buf, O_RDONLY);

	if (fd < 0)
		atf_tc_fail("chroot(2) did not change the root directory");

	ATF_REQUIRE(close(fd) == 0);
	ATF_REQUIRE(unlink(buf) == 0);
}

ATF_TC(chroot_err);
ATF_TC_HEAD(chroot_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test error conditions of chroot(2)");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(chroot_err, tc)
{
	char buf[PATH_MAX + 1];

	(void)memset(buf, 'x', sizeof(buf));

	errno = 0;
	ATF_REQUIRE_ERRNO(ENAMETOOLONG, chroot(buf) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(EFAULT, chroot((void *)-1) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(ENOENT, chroot("/a/b/c/d/e/f/g/h/i/j") == -1);
}

ATF_TC(chroot_perm);
ATF_TC_HEAD(chroot_perm, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test permissions with chroot(2)");
	atf_tc_set_md_var(tc, "require.user", "unprivileged");
}

ATF_TC_BODY(chroot_perm, tc)
{
	static char buf[LINE_MAX];
	pid_t pid;
	int sta;

	(void)memset(buf, '\0', sizeof(buf));
	ATF_REQUIRE(getcwd(buf, sizeof(buf)) != NULL);

	pid = fork();
	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {

		errno = 0;

		if (chroot(buf) != -1)
			_exit(EXIT_FAILURE);

		if (errno != EPERM)
			_exit(EXIT_FAILURE);

		_exit(EXIT_SUCCESS);
	}

	(void)wait(&sta);

	if (WIFEXITED(sta) == 0 || WEXITSTATUS(sta) != EXIT_SUCCESS)
		atf_tc_fail("chroot(2) succeeded as unprivileged user");
}

ATF_TC(fchroot_basic);
ATF_TC_HEAD(fchroot_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of fchroot(2)");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(fchroot_basic, tc)
{
	char buf[PATH_MAX];
	int fd, sta;
	pid_t pid;

	(void)memset(buf, '\0', sizeof(buf));
	(void)getcwd(buf, sizeof(buf));
	(void)strlcat(buf, "/dir", sizeof(buf));

	ATF_REQUIRE(mkdir(buf, 0500) == 0);
	ATF_REQUIRE(chdir(buf) == 0);

	fd = open(buf, O_RDONLY);
	ATF_REQUIRE(fd >= 0);

	pid = fork();
	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {

		if (fchroot(fd) != 0)
			_exit(EXIT_FAILURE);

		if (close(fd) != 0)
			_exit(EXIT_FAILURE);

		fd = open("file", O_RDONLY | O_CREAT, 0600);

		if (fd < 0)
			_exit(EXIT_FAILURE);

		if (close(fd) != 0)
			_exit(EXIT_FAILURE);

		_exit(EXIT_SUCCESS);
	}

	(void)wait(&sta);

	if (WIFEXITED(sta) == 0 || WEXITSTATUS(sta) != EXIT_SUCCESS)
		atf_tc_fail("fchroot(2) failed");

	(void)chdir("/");
	(void)strlcat(buf, "/file", sizeof(buf));

	fd = open(buf, O_RDONLY);

	if (fd < 0)
		atf_tc_fail("fchroot(2) did not change the root directory");

	ATF_REQUIRE(close(fd) == 0);
	ATF_REQUIRE(unlink(buf) == 0);
}

ATF_TC(fchroot_err);
ATF_TC_HEAD(fchroot_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test error conditions of fchroot(2)");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(fchroot_err, tc)
{
	int fd;

	fd = open("/etc/passwd", O_RDONLY);
	ATF_REQUIRE(fd >= 0);

	errno = 0;
	ATF_REQUIRE_ERRNO(EBADF, fchroot(-1) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(ENOTDIR, fchroot(fd) == -1);

	ATF_REQUIRE(close(fd) == 0);
}

ATF_TC(fchroot_perm);
ATF_TC_HEAD(fchroot_perm, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test permissions with fchroot(2)");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(fchroot_perm, tc)
{
	static char buf[LINE_MAX];
	struct passwd *pw;
	int fd, sta;
	pid_t pid;

	(void)memset(buf, '\0', sizeof(buf));
	ATF_REQUIRE(getcwd(buf, sizeof(buf)) != NULL);

	pw = getpwnam("nobody");
	fd = open(buf, O_RDONLY);

	ATF_REQUIRE(fd >= 0);
	ATF_REQUIRE(pw != NULL);

	pid = fork();
	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {

		(void)setuid(pw->pw_uid);

		errno = 0;

		if (fchroot(fd) != -1)
			_exit(EXIT_FAILURE);

		if (errno != EPERM)
			_exit(EXIT_FAILURE);

		_exit(EXIT_SUCCESS);
	}

	(void)wait(&sta);

	if (WIFEXITED(sta) == 0 || WEXITSTATUS(sta) != EXIT_SUCCESS)
		atf_tc_fail("fchroot(2) succeeded as unprivileged user");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, chroot_basic);
	ATF_TP_ADD_TC(tp, chroot_err);
	ATF_TP_ADD_TC(tp, chroot_perm);
#ifndef __OpenBSD__
	/* fchroot(2) not available */
	ATF_TP_ADD_TC(tp, fchroot_basic);
	ATF_TP_ADD_TC(tp, fchroot_err);
	ATF_TP_ADD_TC(tp, fchroot_perm);
#endif

	return atf_no_error();
}
