/*	$OpenBSD: t_revoke.c,v 1.3 2021/12/13 16:56:48 deraadt Exp $	*/
/* $NetBSD: t_revoke.c,v 1.2 2017/01/13 21:15:57 christos Exp $ */

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

#include <sys/resource.h>
#include <sys/wait.h>

#include "atf-c.h"
#include <fcntl.h>
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char path[] = "revoke";

ATF_TC_WITH_CLEANUP(revoke_basic);
ATF_TC_HEAD(revoke_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of revoke(2)");
}

ATF_TC_BODY(revoke_basic, tc)
{
	struct rlimit res;
	char tmp[10];
	size_t i, n;
	int *buf;

	(void)memset(&res, 0, sizeof(struct rlimit));
	(void)getrlimit(RLIMIT_NOFILE, &res);

	if ((n = res.rlim_cur / 10) == 0)
		n = 10;

	buf = calloc(n, sizeof(int));
	ATF_REQUIRE(buf != NULL);

	buf[0] = open(path, O_RDWR | O_CREAT, 0600);
	ATF_REQUIRE(buf[0] >= 0);

	for (i = 1; i < n; i++) {
		buf[i] = open(path, O_RDWR);
		ATF_REQUIRE(buf[i] >= 0);
	}

	ATF_REQUIRE(revoke(path) == 0);

	for (i = 0; i < n; i++) {

		ATF_REQUIRE(read(buf[i], tmp, sizeof(tmp)) == -1);

		(void)close(buf[i]);
	}

	free(buf);

	(void)unlink(path);
}

ATF_TC_CLEANUP(revoke_basic, tc)
{
	(void)unlink(path);
}

ATF_TC(revoke_err);
ATF_TC_HEAD(revoke_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test errors from revoke(2)");
	atf_tc_set_md_var(tc, "require.user", "unprivileged");
}

ATF_TC_BODY(revoke_err, tc)
{
	char buf[1024 + 1];	/* XXX: From the manual page... */

	(void)memset(buf, 'x', sizeof(buf));

	errno = 0;
	ATF_REQUIRE_ERRNO(EFAULT, revoke((char *)-1) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(ENAMETOOLONG, revoke(buf) == -1);

	errno = 0;
#ifdef __OpenBSD__
	ATF_REQUIRE_ERRNO(ENOTTY, revoke("/etc/passwd") == -1);
#else
	ATF_REQUIRE_ERRNO(EPERM, revoke("/etc/passwd") == -1);
#endif

	errno = 0;
	ATF_REQUIRE_ERRNO(ENOENT, revoke("/etc/xxx/yyy") == -1);
}

ATF_TC_WITH_CLEANUP(revoke_perm);
ATF_TC_HEAD(revoke_perm, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test permissions revoke(2)");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(revoke_perm, tc)
{
	struct passwd *pw;
	int fd, sta;
	pid_t pid;

	pw = getpwnam("nobody");
	fd = open(path, O_RDWR | O_CREAT, 0600);

	ATF_REQUIRE(fd >= 0);
	ATF_REQUIRE(pw != NULL);
	ATF_REQUIRE(revoke(path) == 0);

	pid = fork();
	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {

		if (setuid(pw->pw_uid) != 0)
			_exit(EXIT_FAILURE);

		errno = 0;

		if (revoke(path) == 0)
			_exit(EXIT_FAILURE);

		if (errno != EACCES)
			_exit(EXIT_FAILURE);

		if (close(fd) != 0)
			_exit(EXIT_FAILURE);

		_exit(EXIT_SUCCESS);
	}

	(void)wait(&sta);

	if (WIFEXITED(sta) == 0 || WEXITSTATUS(sta) != EXIT_SUCCESS)
		atf_tc_fail("revoke(2) did not obey permissions");

	(void)close(fd);
	ATF_REQUIRE(unlink(path) == 0);
}

ATF_TC_CLEANUP(revoke_perm, tc)
{
	(void)unlink(path);
}

ATF_TP_ADD_TCS(tp)
{

#ifndef __OpenBSD__
	/* OpenBSD supports revoke only on ttys */
	ATF_TP_ADD_TC(tp, revoke_basic);
#endif
	ATF_TP_ADD_TC(tp, revoke_err);
#ifndef __OpenBSD__
	/* OpenBSD supports revoke only on ttys */
	ATF_TP_ADD_TC(tp, revoke_perm);
#endif

	return atf_no_error();
}
