/*	$OpenBSD: t_recvmmsg.c,v 1.2 2022/09/11 20:51:44 mbuhl Exp $	*/
/*	$NetBSD: t_recvmmsg.c,v 1.4 2018/08/21 10:39:21 christos Exp $	*/

/*-
 * Copyright (c) 2012 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jared McNeill and Christos Zoulas.
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
#include <sys/cdefs.h>

#include "atf-c.h"
#include <sys/types.h>
#include <sys/socket.h>
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
#define NPKTS	50

#define min(a, b) ((a) < (b) ? (a) : (b))
static int debug;
static volatile sig_atomic_t rdied;

static void
handle_sigchld(__unused int pid)
{

	rdied = 1;
}

ATF_TC(recvmmsg_basic);
ATF_TC_HEAD(recvmmsg_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of recvmmsg(2)");
}

ATF_TC_BODY(recvmmsg_basic, tc)
{
	int fd[2], error, i, cnt;
	uint8_t *buf;
	struct mmsghdr *mmsghdr;
	struct iovec *iov;
	unsigned int mmsgcnt, n;
	int status;
	off_t off;
	uint8_t DGRAM[1316] = { 0, 2, 3, 4, 5, 6, 7, 8, 9, };
	struct sigaction sa;
	ssize_t overf = 0;

	error = socketpair(AF_UNIX, SOCK_DGRAM, 0, fd);
	ATF_REQUIRE_MSG(error != -1, "socketpair failed (%s)", strerror(errno));

	buf = malloc(BUFSIZE);
	ATF_REQUIRE_MSG(buf != NULL, "malloc failed (%s)", strerror(errno));

	mmsgcnt = BUFSIZE / sizeof(DGRAM);
	mmsghdr = malloc(sizeof(*mmsghdr) * mmsgcnt);
	ATF_REQUIRE_MSG(mmsghdr != NULL, "malloc failed (%s)", strerror(errno));
	iov = malloc(sizeof(*iov) * mmsgcnt);
	ATF_REQUIRE_MSG(iov != NULL, "malloc failed (%s)", strerror(errno));

	for (off = 0, n = 0; n < mmsgcnt; n++) {
		iov[n].iov_base = buf + off;
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
		n = NPKTS;
		if (debug)
		    printf("waiting for %u messages (max %u per syscall)\n", n,
			mmsgcnt);
		while (n > 0) {
			struct timespec ts = { 1, 0 };
			cnt = recvmmsg(fd[1], mmsghdr, min(mmsgcnt, n),
			    MSG_WAITALL, &ts);
			if (cnt == -1 && errno == ENOBUFS) {
				overf++;
				if (debug)
					printf("receive buffer overflowed"
					    " (%zu)\n",overf);
				continue;
			}
			ATF_REQUIRE_MSG(cnt != -1, "recvmmsg failed (%s)",
			    strerror(errno));
			ATF_REQUIRE_MSG(cnt != 0, "recvmmsg timeout");
			if (debug)
				printf("recvmmsg: got %u messages\n", cnt);
			for (i = 0; i < cnt; i++) {
				ATF_CHECK_EQ_MSG(mmsghdr[i].msg_len,
				    sizeof(DGRAM), "packet length");
				ATF_CHECK_EQ_MSG(
				    ((uint8_t *)iov[i].iov_base)[0],
				    NPKTS - n + i, "packet contents");
			}
			n -= cnt;
		}
		if (debug)
			printf("done!\n");
		exit(0);
		/*NOTREACHED*/
	default:
		sched_yield();

		for (n = 0; n < NPKTS; n++) {
			if (debug)
				printf("sending packet %u/%u...\n", (n+1),
				    NPKTS);
			do {
				if (rdied)
					break;
				DGRAM[0] = n;
				error = send(fd[0], DGRAM, sizeof(DGRAM), 0);
			} while (error == -1 && errno == ENOBUFS);
			ATF_REQUIRE_MSG(error != -1, "send failed (%s)",
			    strerror(errno));
		}
		error = wait(&status);
		ATF_REQUIRE_MSG(error != -1, "wait failed (%s)",
		    strerror(errno));
		ATF_REQUIRE_MSG(WIFEXITED(status) && WEXITSTATUS(status) == 0,
		    "receiver died");
		break;
	}
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, recvmmsg_basic);

	return atf_no_error();
}
