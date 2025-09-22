/*	$OpenBSD: t_kill.c,v 1.4 2021/12/13 16:56:48 deraadt Exp $	*/
/* $NetBSD: t_kill.c,v 1.1 2011/07/07 06:57:53 jruoho Exp $ */

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

#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#include "atf-c.h"

ATF_TC(kill_basic);
ATF_TC_HEAD(kill_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test that kill(2) works");
}

ATF_TC_BODY(kill_basic, tc)
{
	const int sig[] = { SIGHUP, SIGINT, SIGKILL, SIGTERM };
	pid_t pid;
	size_t i;
	int sta;

	for (i = 0; i < __arraycount(sig); i++) {

		pid = fork();
		ATF_REQUIRE(pid >= 0);

		switch (pid) {

		case 0:
			pause();
			break;

		default:
			ATF_REQUIRE(kill(pid, sig[i]) == 0);
		}

		(void)wait(&sta);

		if (WIFSIGNALED(sta) == 0 || WTERMSIG(sta) != sig[i])
			atf_tc_fail("kill(2) failed to kill child");
	}
}

ATF_TC(kill_err);
ATF_TC_HEAD(kill_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test error conditions of kill(2)");
}

ATF_TC_BODY(kill_err, tc)
{
	int rv, sta;
	pid_t pid;

	pid = fork();
	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {

		errno = 0;
		rv = kill(getpid(), -1);

		if (rv == 0 || errno != EINVAL)
			_exit(EINVAL);

		errno = 0;
		rv = kill(INT_MAX, SIGUSR1);

		if (rv == 0 || errno != ESRCH)
			_exit(ESRCH);

		_exit(EXIT_SUCCESS);
	}

	(void)wait(&sta);

	if (WIFEXITED(sta) == 0 || WEXITSTATUS(sta) != EXIT_SUCCESS) {

		if (WEXITSTATUS(sta) == EINVAL)
			atf_tc_fail("expected EINVAL, but kill(2) succeeded");

		if (WEXITSTATUS(sta) == ESRCH)
			atf_tc_fail("expected ESRCH, but kill(2) succeeded");

		atf_tc_fail("unknown error from kill(2)");
	}
}

ATF_TC(kill_perm);
ATF_TC_HEAD(kill_perm, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test kill(2) permissions");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(kill_perm, tc)
{
	struct passwd *pw;
	pid_t cpid, ppid;
	uid_t cuid = 0;
	uid_t puid = 0;
	int sta;

	/*
	 * Test that kill(2) fails when called
	 * for a PID owned by another user.
	 */
	pw = getpwnam("operator");

	if (pw != NULL)
		cuid = pw->pw_uid;

	pw = getpwnam("nobody");

	if (pw != NULL)
		puid = pw->pw_uid;

	if (cuid == 0 || puid == 0 || cuid == puid)
		atf_tc_fail("getpwnam(3) failed");

	ppid = fork();

	if (ppid < 0)
		_exit(EXIT_FAILURE);

	if (ppid == 0) {

		cpid = fork();

		if (cpid < 0)
			_exit(EXIT_FAILURE);

		if (cpid == 0) {

			if (setuid(cuid) < 0)
				_exit(EXIT_FAILURE);
			else {
				(void)sleep(1);
			}

			_exit(EXIT_SUCCESS);
		}

		/*
		 * Try to kill the child after having
		 * set the real and effective UID.
		 */
		if (setuid(puid) != 0)
			_exit(EXIT_FAILURE);

		errno = 0;

		if (kill(cpid, SIGKILL) == 0)
			_exit(EPERM);

		if (errno != EPERM)
			_exit(EPERM);

		(void)waitpid(cpid, &sta, 0);

		_exit(EXIT_SUCCESS);
	}

	(void)waitpid(ppid, &sta, 0);

	if (WIFEXITED(sta) == 0 || WEXITSTATUS(sta) == EPERM)
		atf_tc_fail("killed a process of another user");

	if (WIFEXITED(sta) == 0 || WEXITSTATUS(sta) != EXIT_SUCCESS)
		atf_tc_fail("unknown error from kill(2)");
}

ATF_TC(kill_pgrp_neg);
ATF_TC_HEAD(kill_pgrp_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test kill(2) with process group, #2");
}

ATF_TC_BODY(kill_pgrp_neg, tc)
{
	const int maxiter = 3;
	pid_t cpid, ppid;
	int i, sta;

	ppid = fork();
	ATF_REQUIRE(ppid >= 0);

	if (ppid == 0) {

		ATF_REQUIRE(setpgid(0, 0) == 0);

		for (i = 0; i < maxiter; i++) {

			cpid = fork();
			ATF_REQUIRE(cpid >= 0);

			if (cpid == 0)
				pause();
		}

		/*
		 * Test the variant of killpg(3); if the process number
		 * is negative but not -1, the signal should be sent to
		 * all processes whose process group ID is equal to the
		 * absolute value of the process number.
		 */
		ATF_REQUIRE(kill(-getpgrp(), SIGKILL) == 0);

		(void)sleep(1);

		_exit(EXIT_SUCCESS);
	}

	(void)waitpid(ppid, &sta, 0);

	if (WIFSIGNALED(sta) == 0 || WTERMSIG(sta) != SIGKILL)
		atf_tc_fail("failed to kill(2) a process group");
}

ATF_TC(kill_pgrp_zero);
ATF_TC_HEAD(kill_pgrp_zero, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test kill(2) with process group, #1");
}

ATF_TC_BODY(kill_pgrp_zero, tc)
{
	const int maxiter = 3;
	pid_t cpid, ppid;
	int i, sta;

	ppid = fork();
	ATF_REQUIRE(ppid >= 0);

	if (ppid == 0) {

		ATF_REQUIRE(setpgid(0, 0) == 0);

		for (i = 0; i < maxiter; i++) {

			cpid = fork();
			ATF_REQUIRE(cpid >= 0);

			if (cpid == 0)
				pause();
		}

		/*
		 * If the supplied process number is zero,
		 * the signal should be sent to all processes
		 * under the current process group.
		 */
		ATF_REQUIRE(kill(0, SIGKILL) == 0);

		(void)sleep(1);

		_exit(EXIT_SUCCESS);
	}

	(void)waitpid(ppid, &sta, 0);

	if (WIFSIGNALED(sta) == 0 || WTERMSIG(sta) != SIGKILL)
		atf_tc_fail("failed to kill(2) a process group");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, kill_basic);
	ATF_TP_ADD_TC(tp, kill_err);
	ATF_TP_ADD_TC(tp, kill_perm);
	ATF_TP_ADD_TC(tp, kill_pgrp_neg);
	ATF_TP_ADD_TC(tp, kill_pgrp_zero);

	return atf_no_error();
}
