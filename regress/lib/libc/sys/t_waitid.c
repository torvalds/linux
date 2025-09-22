/*	$OpenBSD: t_waitid.c,v 1.1 2022/10/26 23:18:02 kettenis Exp $	*/
/* $NetBSD: t_wait.c,v 1.10 2021/07/17 14:03:35 martin Exp $ */

/*-
 * Copyright (c) 2016 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
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
#include <sys/resource.h>

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "atf-c.h"

ATF_TC(waitid_invalid);
ATF_TC_HEAD(waitid_invalid, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test that waitid(2) returns EINVAL with 0 options");
}

ATF_TC_BODY(waitid_invalid, tc)
{
	siginfo_t si;
	ATF_REQUIRE(waitid(P_ALL, 0, &si, 0) == -1
	    && errno == EINVAL);
}

ATF_TC(waitid_exited);
ATF_TC_HEAD(waitid_exited, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test that waitid(2) handled exiting process and code");
}

ATF_TC_BODY(waitid_exited, tc)
{
	siginfo_t si;
	pid_t pid;

	switch (pid = fork()) {
	case 0:
		exit(0x5a5a5a5a);
		/*NOTREACHED*/
	case -1:
		ATF_REQUIRE(pid > 0);
		__unreachable();
	default:
		ATF_REQUIRE(waitid(P_PID, pid, &si, WEXITED) == 0);
		ATF_REQUIRE(si.si_status == 0x5a5a5a5a);
		ATF_REQUIRE(si.si_pid == pid);
		ATF_REQUIRE(si.si_uid == getuid());
		ATF_REQUIRE(si.si_code == CLD_EXITED);
		printf("user: %ju system: %ju\n", (uintmax_t)si.si_utime,
		    (uintmax_t)si.si_utime);
		break;
	}
}

ATF_TC(waitid_terminated);
ATF_TC_HEAD(waitid_terminated, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test that waitid(2) handled terminated process and code");
}

ATF_TC_BODY(waitid_terminated, tc)
{
	siginfo_t si;
	pid_t pid;

	switch (pid = fork()) {
	case 0:
		sleep(100);
		/*FALLTHROUGH*/
	case -1:
		ATF_REQUIRE(pid > 0);
		__unreachable();
	default:
		ATF_REQUIRE(kill(pid, SIGTERM) == 0);
		ATF_REQUIRE(waitid(P_PID, pid, &si, WEXITED) == 0);
		ATF_REQUIRE(si.si_status == SIGTERM);
		ATF_REQUIRE(si.si_pid == pid);
		ATF_REQUIRE(si.si_uid == getuid());
		ATF_REQUIRE(si.si_code == CLD_KILLED);
		printf("user: %ju system: %ju\n", (uintmax_t)si.si_utime,
		    (uintmax_t)si.si_utime);
		break;
	}
}

ATF_TC(waitid_coredumped);
ATF_TC_HEAD(waitid_coredumped, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test that waitid(2) handled coredumped process and code");
}

ATF_TC_BODY(waitid_coredumped, tc)
{
	siginfo_t si;
	pid_t pid;
	static const struct rlimit rl = { RLIM_INFINITY, RLIM_INFINITY };

	switch (pid = fork()) {
	case 0:
		ATF_REQUIRE(setrlimit(RLIMIT_CORE, &rl) == 0);
		*(char *)8 = 0;
		/*FALLTHROUGH*/
	case -1:
		ATF_REQUIRE(pid > 0);
		__unreachable();
	default:
		ATF_REQUIRE(waitid(P_PID, pid, &si, WEXITED) == 0);
		ATF_REQUIRE(si.si_status == SIGSEGV);
		ATF_REQUIRE(si.si_pid == pid);
		ATF_REQUIRE(si.si_uid == getuid());
		ATF_REQUIRE(si.si_code == CLD_DUMPED);
		printf("user: %ju system: %ju\n", (uintmax_t)si.si_utime,
		    (uintmax_t)si.si_utime);
		break;
	}
}

ATF_TC(waitid_stop_and_go);
ATF_TC_HEAD(waitid_stop_and_go, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test that waitid(2) handled stopped/continued process and code");
}

ATF_TC_BODY(waitid_stop_and_go, tc)
{
	siginfo_t si;
	pid_t pid;
	static const struct rlimit rl = { 0, 0 };

	ATF_REQUIRE(setrlimit(RLIMIT_CORE, &rl) == 0);
	switch (pid = fork()) {
	case 0:
		sleep(100);
		/*FALLTHROUGH*/
	case -1:
		ATF_REQUIRE(pid > 0);
		__unreachable();
	default:
		ATF_REQUIRE(kill(pid, SIGSTOP) == 0);
		ATF_REQUIRE(waitid(P_PID, pid, &si, WSTOPPED) == 0);
		ATF_REQUIRE(si.si_status == SIGSTOP);
		ATF_REQUIRE(si.si_pid == pid);
		ATF_REQUIRE(si.si_uid == getuid());
		ATF_REQUIRE(si.si_code == CLD_STOPPED);
		printf("user: %ju system: %ju\n", (uintmax_t)si.si_utime,
		    (uintmax_t)si.si_utime);

		ATF_REQUIRE(kill(pid, SIGCONT) == 0);
		ATF_REQUIRE(waitid(P_PID, pid, &si, WCONTINUED) == 0);
		ATF_REQUIRE(si.si_status == SIGCONT);
		ATF_REQUIRE(si.si_pid == pid);
		ATF_REQUIRE(si.si_uid == getuid());
		ATF_REQUIRE(si.si_code == CLD_CONTINUED);
		printf("user: %ju system: %ju\n", (uintmax_t)si.si_utime,
		    (uintmax_t)si.si_utime);

		ATF_REQUIRE(kill(pid, SIGQUIT) == 0);
		ATF_REQUIRE(waitid(P_PID, pid, &si, WEXITED) == 0);
		ATF_REQUIRE(si.si_status == SIGQUIT);
		ATF_REQUIRE(si.si_pid == pid);
		ATF_REQUIRE(si.si_uid == getuid());
		ATF_REQUIRE(si.si_code == CLD_KILLED);
		printf("user: %ju system: %ju\n", (uintmax_t)si.si_utime,
		    (uintmax_t)si.si_utime);
		break;
	}
}

ATF_TC(waitid_stopgo_loop);
ATF_TC_HEAD(waitid_stopgo_loop, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test that waitid(2) handled stopped/continued process loop");
}

ATF_TC_BODY(waitid_stopgo_loop, tc)
{
	siginfo_t si;
	pid_t pid;
	static const struct rlimit rl = { 0, 0 };
	size_t N = 100;

	ATF_REQUIRE(setrlimit(RLIMIT_CORE, &rl) == 0);
	switch (pid = fork()) {
	case 0:
		sleep(100);
		/*FALLTHROUGH*/
	case -1:
		ATF_REQUIRE(pid > 0);
		__unreachable();
	}

	printf("Before loop of SIGSTOP/SIGCONT sequence %zu times\n", N);
	while (N --> 0) {
		ATF_REQUIRE(kill(pid, SIGSTOP) == 0);
		ATF_REQUIRE(waitid(P_PID, pid, &si, WSTOPPED) == 0);
		ATF_REQUIRE(si.si_status == SIGSTOP);
		ATF_REQUIRE(si.si_pid == pid);
		ATF_REQUIRE(si.si_uid == getuid());
		ATF_REQUIRE(si.si_code == CLD_STOPPED);

		ATF_REQUIRE(kill(pid, SIGCONT) == 0);
		ATF_REQUIRE(waitid(P_PID, pid, &si, WCONTINUED) == 0);
		ATF_REQUIRE(si.si_status == SIGCONT);
		ATF_REQUIRE(si.si_pid == pid);
		ATF_REQUIRE(si.si_uid == getuid());
		ATF_REQUIRE(si.si_code == CLD_CONTINUED);
	}
	ATF_REQUIRE(kill(pid, SIGQUIT) == 0);
	ATF_REQUIRE(waitid(P_PID, pid, &si, WEXITED) == 0);
	ATF_REQUIRE(si.si_status == SIGQUIT);
	ATF_REQUIRE(si.si_pid == pid);
	ATF_REQUIRE(si.si_uid == getuid());
	ATF_REQUIRE(si.si_code == CLD_KILLED);
	printf("user: %ju system: %ju\n", (uintmax_t)si.si_utime,
	    (uintmax_t)si.si_utime);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, waitid_invalid);
	ATF_TP_ADD_TC(tp, waitid_exited);
	ATF_TP_ADD_TC(tp, waitid_terminated);
	ATF_TP_ADD_TC(tp, waitid_coredumped);
	ATF_TP_ADD_TC(tp, waitid_stop_and_go);
	ATF_TP_ADD_TC(tp, waitid_stopgo_loop);

	return atf_no_error();
}
