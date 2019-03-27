/*-
 * Copyright (c) 2016 Jilles Tjoelker
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

#include <sys/wait.h>

#include <atf-c.h>
#include <signal.h>
#include <unistd.h>

ATF_TC_WITHOUT_HEAD(waitpid_nohang);
ATF_TC_BODY(waitpid_nohang, tc)
{
	pid_t child, pid;
	int status, r;

	child = fork();
	ATF_REQUIRE(child != -1);
	if (child == 0) {
		sleep(10);
		_exit(1);
	}

	status = 42;
	pid = waitpid(child, &status, WNOHANG);
	ATF_REQUIRE(pid == 0);
	ATF_CHECK(status == 42);

	r = kill(child, SIGTERM);
	ATF_REQUIRE(r == 0);
	r = waitid(P_PID, child, NULL, WEXITED | WNOWAIT);
	ATF_REQUIRE(r == 0);

	status = -1;
	pid = waitpid(child, &status, WNOHANG);
	ATF_REQUIRE(pid == child);
	ATF_CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, waitpid_nohang);
	return (atf_no_error());
}
