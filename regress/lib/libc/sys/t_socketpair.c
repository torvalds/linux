/*	$OpenBSD: t_socketpair.c,v 1.2 2021/12/13 16:56:48 deraadt Exp $	*/
/* $NetBSD: t_socketpair.c,v 1.2 2017/01/13 20:04:52 christos Exp $ */

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
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>

static void
connected(int fd)
{
	struct sockaddr_un addr;
	socklen_t len = (socklen_t)sizeof(addr);
	ATF_REQUIRE(getpeername(fd, (struct sockaddr*)(void *)&addr,
	    &len) == 0);
}

static void
run(int flags)
{
	int fd[2], i;

	while ((i = open("/", O_RDONLY)) < 3)
		ATF_REQUIRE(i != -1);

	ATF_REQUIRE(closefrom(3) != -1);

	ATF_REQUIRE(socketpair(AF_UNIX, SOCK_DGRAM | flags, 0, fd) == 0);

	ATF_REQUIRE(fd[0] == 3);
	ATF_REQUIRE(fd[1] == 4);

	connected(fd[0]);
	connected(fd[1]);

	if (flags & SOCK_CLOEXEC) {
		ATF_REQUIRE((fcntl(fd[0], F_GETFD) & FD_CLOEXEC) != 0);
		ATF_REQUIRE((fcntl(fd[1], F_GETFD) & FD_CLOEXEC) != 0);
	} else {
		ATF_REQUIRE((fcntl(fd[0], F_GETFD) & FD_CLOEXEC) == 0);
		ATF_REQUIRE((fcntl(fd[1], F_GETFD) & FD_CLOEXEC) == 0);
	}

	if (flags & SOCK_NONBLOCK) {
		ATF_REQUIRE((fcntl(fd[0], F_GETFL) & O_NONBLOCK) != 0);
		ATF_REQUIRE((fcntl(fd[1], F_GETFL) & O_NONBLOCK) != 0);
	} else {
		ATF_REQUIRE((fcntl(fd[0], F_GETFL) & O_NONBLOCK) == 0);
		ATF_REQUIRE((fcntl(fd[1], F_GETFL) & O_NONBLOCK) == 0);
	}

	ATF_REQUIRE(close(fd[0]) != -1);
	ATF_REQUIRE(close(fd[1]) != -1);
}

ATF_TC(socketpair_basic);
ATF_TC_HEAD(socketpair_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of socketpair(2)");
}

ATF_TC_BODY(socketpair_basic, tc)
{
	run(0);
}

ATF_TC(socketpair_nonblock);
ATF_TC_HEAD(socketpair_nonblock, tc)
{
	atf_tc_set_md_var(tc, "descr", "A non-blocking test of socketpair(2)");
}

ATF_TC_BODY(socketpair_nonblock, tc)
{
	run(SOCK_NONBLOCK);
}

ATF_TC(socketpair_cloexec);
ATF_TC_HEAD(socketpair_cloexec, tc)
{
	atf_tc_set_md_var(tc, "descr", "A close-on-exec of socketpair(2)");
}

ATF_TC_BODY(socketpair_cloexec, tc)
{
	run(SOCK_CLOEXEC);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, socketpair_basic);
	ATF_TP_ADD_TC(tp, socketpair_nonblock);
	ATF_TP_ADD_TC(tp, socketpair_cloexec);

	return atf_no_error();
}
