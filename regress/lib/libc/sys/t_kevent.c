/*	$OpenBSD: t_kevent.c,v 1.3 2023/04/19 12:58:15 jsg Exp $	*/
/*	$NetBSD: t_kevent.c,v 1.9 2020/10/31 01:08:32 christos Exp $ */

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundatiom
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

#include <sys/types.h>
#include <sys/event.h>

#include "atf-c.h"
#include <errno.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>
#ifndef __OpenBSD__
#include <sys/drvctlio.h>
#endif
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/wait.h>

ATF_TC(kevent_zerotimer);
ATF_TC_HEAD(kevent_zerotimer, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks that kevent with a 0 timer "
	    "does not crash the system (PR lib/45618)");
}

ATF_TC_BODY(kevent_zerotimer, tc)
{
	struct kevent ev;
	int kq;

	ATF_REQUIRE((kq = kqueue()) != -1);
	EV_SET(&ev, 1, EVFILT_TIMER, EV_ADD|EV_ENABLE, 0, 1, 0);
	ATF_REQUIRE(kevent(kq, &ev, 1, NULL, 0, NULL) != -1);
	ATF_REQUIRE(kevent(kq, NULL, 0, &ev, 1, NULL) == 1);
}

ATF_TC(kqueue_desc_passing);
ATF_TC_HEAD(kqueue_desc_passing, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks that passing a kqueue to "
		"another process does not crash the kernel (PR 46463)");
}

ATF_TC_BODY(kqueue_desc_passing, tc)
{
	pid_t child;
	int s[2], storage, status, kq;
	struct cmsghdr *msg;
	struct iovec iov;
	struct msghdr m;
	struct kevent ev;

	ATF_REQUIRE((kq = kqueue()) != -1);

	// atf_tc_skip("crashes kernel (PR kern/46463)");

	ATF_REQUIRE(socketpair(AF_LOCAL, SOCK_STREAM, 0, s) != -1);
	msg = malloc(CMSG_SPACE(sizeof(int)));
	m.msg_iov = &iov;
	m.msg_iovlen = 1;
	m.msg_name = NULL;
	m.msg_namelen = 0;
	m.msg_control = msg;
	m.msg_controllen = CMSG_SPACE(sizeof(int));

	child = fork();
	if (child == 0) {
#ifdef __OpenBSD__
		sleep(1);
		exit(0);
#endif
		close(s[0]);

		iov.iov_base = &storage;
		iov.iov_len = sizeof(int);
		m.msg_iov = &iov;
		m.msg_iovlen = 1;

		if (recvmsg(s[1], &m, 0) == -1)
			err(1, "child: could not recvmsg");

		kq = *(int *)CMSG_DATA(msg);
		printf("child (pid %d): received kq fd %d\n", getpid(), kq);
		exit(0);
	}

	close(s[1]);

	iov.iov_base = &storage;
	iov.iov_len = sizeof(int);

	msg->cmsg_level = SOL_SOCKET;
	msg->cmsg_type = SCM_RIGHTS;
	msg->cmsg_len = CMSG_LEN(sizeof(int));

	*(int *)CMSG_DATA(msg) = kq;

	EV_SET(&ev, 1, EVFILT_TIMER, EV_ADD|EV_ENABLE, 0, 1, 0);
	ATF_CHECK(kevent(kq, &ev, 1, NULL, 0, NULL) != -1);

	printf("parent (pid %d): sending kq fd %d\n", getpid(), kq);
	if (sendmsg(s[0], &m, 0) == -1) {
#ifdef __OpenBSD__
		ATF_REQUIRE_EQ_MSG(errno, EINVAL, "errno is %d", errno);
#else
		ATF_REQUIRE_EQ_MSG(errno, EBADF, "errno is %d", errno);
		atf_tc_skip("PR kern/46523");
#endif
	}

	close(kq);

	waitpid(child, &status, 0);
	ATF_CHECK(WIFEXITED(status) && WEXITSTATUS(status)==0);
}

#ifndef __OpenBSD__
ATF_TC(kqueue_unsupported_fd);
ATF_TC_HEAD(kqueue_unsupported_fd, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks that watching an fd whose"
	    " type is not supported does not crash the kernel");
}

ATF_TC_BODY(kqueue_unsupported_fd, tc)
{
	/* mqueue and semaphore use fnullop_kqueue also */
	int fd, kq;
	struct kevent ev;

	fd = open(DRVCTLDEV, O_RDONLY);
	if (fd == -1) {
		switch (errno) {
		case ENOENT:
		case ENXIO:
			atf_tc_skip("no " DRVCTLDEV " available for testing");
			break;
		}
	}
	ATF_REQUIRE(fd != -1);
	ATF_REQUIRE((kq = kqueue()) != -1);

	EV_SET(&ev, fd, EVFILT_VNODE, EV_ADD | EV_ENABLE | EV_CLEAR,
	   NOTE_DELETE|NOTE_WRITE|NOTE_EXTEND|NOTE_ATTRIB|NOTE_LINK|
	   NOTE_RENAME|NOTE_REVOKE, 0, 0);

	ATF_REQUIRE(kevent(kq, &ev, 1, NULL, 0, NULL) == -1);
	ATF_REQUIRE_ERRNO(EOPNOTSUPP, true);

	(void)close(fd);
	(void)close(kq);
}

ATF_TC(kqueue_EVFILT_USER);
ATF_TC_HEAD(kqueue_EVFILT_USER, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks usability of EVFILT_USER");
}

ATF_TC_BODY(kqueue_EVFILT_USER, tc)
{
	/* mqueue and semaphore use fnullop_kqueue also */
	int kq;
	struct kevent ev, rev;

	ATF_REQUIRE((kq = kqueue()) != -1);

	EV_SET(&ev, 666, EVFILT_USER, EV_ADD | EV_ENABLE, 0, 0, 0);
	ATF_REQUIRE(kevent(kq, &ev, 1, NULL, 0, NULL) == 0);
	EV_SET(&ev, 666, EVFILT_USER, 0, NOTE_FFCOPY | NOTE_TRIGGER | 8, 0, 0);
	ATF_REQUIRE(kevent(kq, &ev, 1, NULL, 0, NULL) == 0);
	const struct timespec timeout = {
		.tv_sec = 1,
		.tv_nsec = 0,
	};

	ATF_REQUIRE(kevent(kq, NULL, 0, &rev, 1, &timeout) == 1);
	ATF_REQUIRE(rev.ident == 666);
	ATF_REQUIRE(rev.filter == EVFILT_USER);
	ATF_REQUIRE(rev.fflags == 8);
	(void)close(kq);
}
#endif



ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, kevent_zerotimer);
	ATF_TP_ADD_TC(tp, kqueue_desc_passing);
#ifndef __OpenBSD__
	ATF_TP_ADD_TC(tp, kqueue_unsupported_fd);
	ATF_TP_ADD_TC(tp, kqueue_EVFILT_USER);
#endif

	return atf_no_error();
}
