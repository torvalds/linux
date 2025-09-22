/*	$OpenBSD: t_pipe2.c,v 1.4 2023/10/31 07:56:44 claudio Exp $	*/
/* $NetBSD: t_pipe2.c,v 1.9 2017/01/13 21:19:45 christos Exp $ */

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *        This product includes software developed by the NetBSD
 *        Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
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

#include "atf-c.h"
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/resource.h>

static void
run(int flags)
{
	int fd[2], i;

	while ((i = open("/", O_RDONLY)) < 3)
		ATF_REQUIRE(i != -1);

	ATF_REQUIRE_MSG(closefrom(3) != -1, "closefrom failed: %s",
	    strerror(errno));

	ATF_REQUIRE(pipe2(fd, flags) == 0);

	ATF_REQUIRE(fd[0] == 3);
	ATF_REQUIRE(fd[1] == 4);

	if (flags & O_CLOEXEC) {
		ATF_REQUIRE((fcntl(fd[0], F_GETFD) & FD_CLOEXEC) != 0);
		ATF_REQUIRE((fcntl(fd[1], F_GETFD) & FD_CLOEXEC) != 0);
	} else {
		ATF_REQUIRE((fcntl(fd[0], F_GETFD) & FD_CLOEXEC) == 0);
		ATF_REQUIRE((fcntl(fd[1], F_GETFD) & FD_CLOEXEC) == 0);
	}

	if (flags & O_NONBLOCK) {
		ATF_REQUIRE((fcntl(fd[0], F_GETFL) & O_NONBLOCK) != 0);
		ATF_REQUIRE((fcntl(fd[1], F_GETFL) & O_NONBLOCK) != 0);
	} else {
		ATF_REQUIRE((fcntl(fd[0], F_GETFL) & O_NONBLOCK) == 0);
		ATF_REQUIRE((fcntl(fd[1], F_GETFL) & O_NONBLOCK) == 0);
	}

#ifndef __OpenBSD__
	/* F_GETNOSIGPIPE not available */
	if (flags & O_NOSIGPIPE) {
		ATF_REQUIRE(fcntl(fd[0], F_GETNOSIGPIPE) != 0);
		ATF_REQUIRE(fcntl(fd[1], F_GETNOSIGPIPE) != 0);
	} else {
		ATF_REQUIRE(fcntl(fd[0], F_GETNOSIGPIPE) == 0);
		ATF_REQUIRE(fcntl(fd[1], F_GETNOSIGPIPE) == 0);
	}
#endif

	ATF_REQUIRE(close(fd[0]) != -1);
	ATF_REQUIRE(close(fd[1]) != -1);
}

ATF_TC(pipe2_basic);
ATF_TC_HEAD(pipe2_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of pipe2(2)");
}

ATF_TC_BODY(pipe2_basic, tc)
{
	run(0);
}

ATF_TC(pipe2_consume);
ATF_TC_HEAD(pipe2_consume, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test that consuming file descriptors "
	    "with pipe2(2) does not crash the system (PR kern/46457)");
}

ATF_TC_BODY(pipe2_consume, tc)
{
	struct rlimit rl;
	int err, filedes[2];
	int old;

	(void)closefrom(4);

	err = getrlimit(RLIMIT_NOFILE, &rl);
	ATF_REQUIRE(err == 0);
	/*
	 * The heart of this test is to run against the number of open
	 * file descriptor limit in the middle of a pipe2() call - i.e.
	 * before the call only a single descriptor may be openend.
	 */
	old = rl.rlim_cur;
	rl.rlim_cur = 4;
	err = setrlimit(RLIMIT_NOFILE, &rl);
	ATF_REQUIRE(err == 0);

	err = pipe2(filedes, O_CLOEXEC);
	ATF_REQUIRE(err == -1);
	rl.rlim_cur = old;
	err = setrlimit(RLIMIT_NOFILE, &rl);
}

ATF_TC(pipe2_nonblock);
ATF_TC_HEAD(pipe2_nonblock, tc)
{
	atf_tc_set_md_var(tc, "descr", "A non-blocking test of pipe2(2)");
}

ATF_TC_BODY(pipe2_nonblock, tc)
{
	run(O_NONBLOCK);
}

ATF_TC(pipe2_cloexec);
ATF_TC_HEAD(pipe2_cloexec, tc)
{
	atf_tc_set_md_var(tc, "descr", "A close-on-exec test of pipe2(2)");
}

ATF_TC_BODY(pipe2_cloexec, tc)
{
	run(O_CLOEXEC);
}

ATF_TC(pipe2_nosigpipe);
ATF_TC_HEAD(pipe2_nosigpipe, tc)
{
	atf_tc_set_md_var(tc, "descr", "A no sigpipe test of pipe2(2)");
}

ATF_TC_BODY(pipe2_nosigpipe, tc)
{
	run(O_NOSIGPIPE);
}

ATF_TC(pipe2_einval);
ATF_TC_HEAD(pipe2_einval, tc)
{
	atf_tc_set_md_var(tc, "descr", "A error check of pipe2(2)");
}

ATF_TC_BODY(pipe2_einval, tc)
{
	int fd[2];
	ATF_REQUIRE_ERRNO(EINVAL, pipe2(fd, O_ASYNC) == -1);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, pipe2_basic);
	ATF_TP_ADD_TC(tp, pipe2_consume);
	ATF_TP_ADD_TC(tp, pipe2_nonblock);
	ATF_TP_ADD_TC(tp, pipe2_cloexec);
#ifndef __OpenBSD__
	/* O_NOSIGPIPE not available */
	ATF_TP_ADD_TC(tp, pipe2_nosigpipe);
#endif
	ATF_TP_ADD_TC(tp, pipe2_einval);

	return atf_no_error();
}
