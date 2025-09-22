/*	$OpenBSD: t_setuid.c,v 1.2 2021/12/13 16:56:48 deraadt Exp $	*/
/* $NetBSD: t_setuid.c,v 1.1 2011/07/07 06:57:54 jruoho Exp $ */

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

#include "atf-c.h"
#include <errno.h>
#include <pwd.h>
#include <stdlib.h>
#include <unistd.h>

ATF_TC(setuid_perm);
ATF_TC_HEAD(setuid_perm, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test setuid(0) as normal user");
	atf_tc_set_md_var(tc, "require.user", "unprivileged");
}

ATF_TC_BODY(setuid_perm, tc)
{
	errno = 0;

	ATF_REQUIRE(setuid(0) == -1);
	ATF_REQUIRE(errno == EPERM);
}

ATF_TC(setuid_real);
ATF_TC_HEAD(setuid_real, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test setuid(2) with real UID");
}

ATF_TC_BODY(setuid_real, tc)
{
	uid_t uid = getuid();

	ATF_REQUIRE(setuid(uid) == 0);

	ATF_REQUIRE(getuid() == uid);
	ATF_REQUIRE(geteuid() == uid);
}

ATF_TC(setuid_root);
ATF_TC_HEAD(setuid_root, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of setuid(2)");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(setuid_root, tc)
{
	struct passwd *pw;
	int rv, sta;
	pid_t pid;
	uid_t uid;

	while ((pw = getpwent()) != NULL) {

		pid = fork();
		ATF_REQUIRE(pid >= 0);

		if (pid == 0) {

			rv = setuid(pw->pw_uid);

			if (rv != 0)
				_exit(EXIT_FAILURE);

			uid = getuid();

			if (uid != pw->pw_uid)
				_exit(EXIT_FAILURE);

			_exit(EXIT_SUCCESS);
		}

		(void)wait(&sta);

		if (WIFEXITED(sta) == 0 || WEXITSTATUS(sta) != EXIT_SUCCESS)
			atf_tc_fail("failed to change UID to %u", pw->pw_uid);
	}
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, setuid_perm);
	ATF_TP_ADD_TC(tp, setuid_real);
	ATF_TP_ADD_TC(tp, setuid_root);

	return atf_no_error();
}
