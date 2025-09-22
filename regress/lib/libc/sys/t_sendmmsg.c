/*	$OpenBSD: t_sendmmsg.c,v 1.3 2023/10/27 07:33:06 anton Exp $	*/
/*	$NetBSD: t_sendmmsg.c,v 1.3 2019/03/16 21:46:43 christos Exp $	*/

/*-
 * Copyright (c) 2018 The NetBSD Foundation, Inc.
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
#include <sys/cdefs.h>

#include "atf-c.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#include <string.h>
#include <time.h>
#include <stdint.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sched.h>

#define BUFSIZE	65536

#define min(a, b) ((a) < (b) ? (a) : (b))
static int debug;
static volatile sig_atomic_t rdied;

static void
handle_sigchld(__unused int pid)
{

	rdied = 1;
}

ATF_TC(sendmmsg_basic);
ATF_TC_HEAD(sendmmsg_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of sendmmsg(2)");
}

static void
setsock(int fd, int type)
{
	int buflen = BUFSIZE;
	socklen_t socklen = sizeof(buflen);

	ATF_REQUIRE_MSG(setsockopt(fd, SOL_SOCKET, type,
	    &buflen, socklen) != -1, "%s (%s)",
	    type == SO_RCVBUF ? "rcv" : "snd", strerror(errno));
}

ATF_TC_BODY(sendmmsg_basic, tc)
{
	int fd[2], error, cnt;
	uint8_t *buf;
	struct mmsghdr *mmsghdr;
	struct iovec *iov;
	unsigned int mmsgcnt, n;
	int status;
	off_t off;
	uint8_t DGRAM[1316] = { 0, 2, 3, 4, 5, 6, 7, 8, 9, };
	uint8_t rgram[sizeof(DGRAM)];
	struct sigaction sa;
	ssize_t overf = 0;

	error = socketpair(AF_UNIX, SOCK_DGRAM, 0, fd);
	ATF_REQUIRE_MSG(error != -1, "socketpair failed (%s)", strerror(errno));

	buf = malloc(BUFSIZE);
	ATF_REQUIRE_MSG(buf != NULL, "malloc failed (%s)", strerror(errno));

	setsock(fd[1], SO_SNDBUF);
//	setsock(fd[0], SO_RCVBUF);

	mmsgcnt = BUFSIZE / sizeof(DGRAM);
	mmsghdr = calloc(mmsgcnt, sizeof(*mmsghdr));
	ATF_REQUIRE_MSG(mmsghdr != NULL, "malloc failed (%s)", strerror(errno));
	iov = malloc(sizeof(*iov) * mmsgcnt);
	ATF_REQUIRE_MSG(iov != NULL, "malloc failed (%s)", strerror(errno));

	for (off = 0, n = 0; n < mmsgcnt; n++) {
		iov[n].iov_base = buf + off;
		memcpy(iov[n].iov_base, DGRAM, sizeof(DGRAM));
		*(buf + off) = n;
		iov[n].iov_len = sizeof(DGRAM);
		off += iov[n].iov_len;
		mmsghdr[n].msg_hdr.msg_iov = &iov[n];
		mmsghdr[n].msg_hdr.msg_iovlen = 1;
		mmsghdr[n].msg_hdr.msg_name = NULL;
		mmsghdr[n].msg_hdr.msg_namelen = 0;
	}

	memset(&sa, 0, sizeof(sa));
	sa.sa_flags = SA_RESTART;
	sa.sa_handler = &handle_sigchld;
	sigemptyset(&sa.sa_mask);
	error = sigaction(SIGCHLD, &sa, 0);
	ATF_REQUIRE_MSG(error != -1, "sigaction failed (%s)",
	    strerror(errno));

	switch (fork()) {
	case -1:
		ATF_REQUIRE_MSG(0, "fork failed (%s)", strerror(errno));
		break;
	case 0:
		sched_yield();
		if (debug)
		    printf("sending %u messages (max %u per syscall)\n", n,
			mmsgcnt);
		for (n = 0; n < mmsgcnt;) {
			if (debug)
				printf("sending packet %u/%u...\n", n,
				    mmsgcnt);
#ifdef __OpenBSD__
			int npkt = min(1024, mmsgcnt - n);
#else
			// XXX: ENOBUFS bug, on the receive side!!!
			// in npkt = min(mmsgsize, mmsgcnt - n);
			int npkt = min(3, mmsgcnt - n), a;
			do {
				a = 0;
				ATF_REQUIRE(ioctl(fd[1], FIONSPACE, &a) != -1);
				printf("1 %d\n", a);
				ATF_REQUIRE(ioctl(fd[0], FIONSPACE, &a) != -1);
				printf("0 %d\n", a);
			} while ((size_t)a < sizeof(DGRAM));
#endif
			cnt = sendmmsg(fd[1], mmsghdr + n, npkt, 0);
			if (cnt == -1 && errno == ENOBUFS) {
				overf++;
				if (debug)
					printf("send buffer overflowed"
					    " (%zu)\n",overf);
				if (overf > 100)
					exit(1);
				sched_yield();
				sched_yield();
				sched_yield();
				continue;
			}
			ATF_REQUIRE_MSG(cnt != -1, "sendmmsg %u failed (%s)",
			    n, strerror(errno));
			if (debug)
				printf("sendmmsg: sent %u messages\n", cnt);
			n += cnt;
			sched_yield();
			sched_yield();
			sched_yield();
		}
		if (debug)
			printf("done!\n");
		exit(0);
		/*NOTREACHED*/
	default:
		for (n = 0; n < mmsgcnt; n++) {
			if (debug)
				printf("receiving packet %u/%u...\n", n,
				    mmsgcnt);
			do {
				if (rdied)
					break;
				cnt = recv(fd[0], rgram, sizeof(rgram), 0);
				ATF_REQUIRE_MSG(cnt != -1 || errno != ENOBUFS,
				    "recv failed (%s)", strerror(errno));
				ATF_CHECK_EQ_MSG(cnt, sizeof(rgram),
				    "packet length");
				ATF_CHECK_EQ_MSG(rgram[0], n,
				    "number %u != %u", rgram[0], n);
				ATF_REQUIRE_MSG(memcmp(rgram + 1, DGRAM + 1,
				    sizeof(rgram) - 1) == 0, "bad data");
			} while (cnt == -1 && errno == ENOBUFS);
		}
		error = wait(&status);
		ATF_REQUIRE_MSG(error != -1, "wait failed (%s)",
		    strerror(errno));
		ATF_REQUIRE_MSG(WIFEXITED(status) && WEXITSTATUS(status) == 0,
		    "receiver died, status %d", status);
		break;
	}
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, sendmmsg_basic);

	return atf_no_error();
}
