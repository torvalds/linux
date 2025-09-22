/*	$OpenBSD: t_msgrcv.c,v 1.2 2021/12/13 16:56:48 deraadt Exp $	*/
/* $NetBSD: t_msgrcv.c,v 1.5 2017/10/08 08:31:05 kre Exp $ */

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

#include <sys/msg.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/wait.h>

#include "atf-c.h"
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <time.h>
#include <unistd.h>

#define MSG_KEY		1234
#define MSG_MTYPE_1	0x41
#define	MSG_MTYPE_2	0x42
#define MSG_MTYPE_3	0x43
#define MSG_LEN		3

struct msg {
	long		 mtype;
	char		 buf[MSG_LEN];
};

static void		clean(void);

static void
clean(void)
{
	int id;

	if ((id = msgget(MSG_KEY, 0)) != -1)
		(void)msgctl(id, IPC_RMID, 0);
}

ATF_TC_WITH_CLEANUP(msgrcv_basic);
ATF_TC_HEAD(msgrcv_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of msgrcv(2)");
}

ATF_TC_BODY(msgrcv_basic, tc)
{
	struct msg msg1 = { MSG_MTYPE_1, { 'a', 'b', 'c' } };
	struct msg msg2 = { MSG_MTYPE_1, { 'x', 'y', 'z' } };
	int id;

	id = msgget(MSG_KEY, IPC_CREAT | 0600);
	ATF_REQUIRE(id != -1);

	(void)msgsnd(id, &msg1, MSG_LEN, IPC_NOWAIT);
	(void)msgrcv(id, &msg2, MSG_LEN, MSG_MTYPE_1, IPC_NOWAIT);

	ATF_CHECK(msg1.buf[0] == msg2.buf[0]);
	ATF_CHECK(msg1.buf[1] == msg2.buf[1]);
	ATF_CHECK(msg1.buf[2] == msg2.buf[2]);

	ATF_REQUIRE(msgctl(id, IPC_RMID, 0) == 0);
}

ATF_TC_CLEANUP(msgrcv_basic, tc)
{
	clean();
}

ATF_TC_WITH_CLEANUP(msgrcv_block);
ATF_TC_HEAD(msgrcv_block, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test that msgrcv(2) blocks");
}

ATF_TC_BODY(msgrcv_block, tc)
{
	struct msg msg = { MSG_MTYPE_1, { 'a', 'b', 'c' } };
	int id, sta;
	pid_t pid;

	id = msgget(MSG_KEY, IPC_CREAT | 0600);
	ATF_REQUIRE(id != -1);

	pid = fork();
	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {

		if (msgrcv(id, &msg, MSG_LEN, MSG_MTYPE_1, 0) < 0)
			_exit(EXIT_FAILURE);

		_exit(EXIT_SUCCESS);
	}

	/*
	 * Below msgsnd(2) should unblock the child,
	 * and hence kill(2) should fail with ESRCH.
	 */
	(void)sleep(1);
	(void)msgsnd(id, &msg, MSG_LEN, IPC_NOWAIT);
	(void)sleep(1);
	(void)kill(pid, SIGKILL);
	(void)wait(&sta);

	if (WIFEXITED(sta) == 0 || WIFSIGNALED(sta) != 0)
		atf_tc_fail("msgrcv(2) did not block");

	ATF_REQUIRE(msgctl(id, IPC_RMID, 0) == 0);
}

ATF_TC_CLEANUP(msgrcv_block, tc)
{
	clean();
}

ATF_TC_WITH_CLEANUP(msgrcv_err);
ATF_TC_HEAD(msgrcv_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test errors from msgrcv(2)");
}

ATF_TC_BODY(msgrcv_err, tc)
{
	struct msg msg = { MSG_MTYPE_1, { 'a', 'b', 'c' } };
	int id, r = 0;

	id = msgget(MSG_KEY, IPC_CREAT | 0600);
	ATF_REQUIRE(id != -1);

	errno = 0;

	ATF_REQUIRE_ERRNO(ENOMSG, msgrcv(id, &msg,
		MSG_LEN, MSG_MTYPE_1, IPC_NOWAIT) == -1);

	ATF_REQUIRE(msgsnd(id, &msg, MSG_LEN, IPC_NOWAIT) == 0);

	errno = 0;

	ATF_REQUIRE_ERRNO(EFAULT, msgrcv(id, (void *)-1,
		MSG_LEN, MSG_MTYPE_1, IPC_NOWAIT) == -1);

	errno = 0;

	ATF_REQUIRE_ERRNO(EINVAL, msgrcv(-1, &msg,
		MSG_LEN, MSG_MTYPE_1, IPC_NOWAIT) == -1);

	errno = 0;

	ATF_REQUIRE_ERRNO(EINVAL, msgrcv(-1, &msg,
		SSIZE_MAX, MSG_MTYPE_1, IPC_NOWAIT) == -1);

	ATF_REQUIRE(msgsnd(id, &msg, MSG_LEN, IPC_NOWAIT) == 0);

	errno = 0;

	ATF_REQUIRE_ERRNO(E2BIG, msgrcv(id, &r,
		MSG_LEN - 1, MSG_MTYPE_1, IPC_NOWAIT) == -1);

	ATF_REQUIRE(msgctl(id, IPC_RMID, 0) == 0);
}

ATF_TC_CLEANUP(msgrcv_err, tc)
{
	clean();
}


ATF_TC_WITH_CLEANUP(msgrcv_mtype);
ATF_TC_HEAD(msgrcv_mtype, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test message types with msgrcv(2)");
}

ATF_TC_BODY(msgrcv_mtype, tc)
{
	struct msg msg1 = { MSG_MTYPE_1, { 'a', 'b', 'c' } };
	struct msg msg2 = { MSG_MTYPE_3, { 'x', 'y', 'z' } };
	int id;

	id = msgget(MSG_KEY, IPC_CREAT | 0600);
	ATF_REQUIRE(id != -1);

	(void)msgsnd(id, &msg1, MSG_LEN, IPC_NOWAIT);
	(void)msgrcv(id, &msg2, MSG_LEN, MSG_MTYPE_2, IPC_NOWAIT);

	ATF_CHECK(msg1.buf[0] != msg2.buf[0]);	/* Different mtype. */
	ATF_CHECK(msg1.buf[1] != msg2.buf[1]);
	ATF_CHECK(msg1.buf[2] != msg2.buf[2]);

	(void)msgrcv(id, &msg2, MSG_LEN, MSG_MTYPE_1, IPC_NOWAIT);

	ATF_CHECK(msg1.buf[0] == msg2.buf[0]);	/* Same mtype. */
	ATF_CHECK(msg1.buf[1] == msg2.buf[1]);
	ATF_CHECK(msg1.buf[2] == msg2.buf[2]);

	ATF_REQUIRE(msgctl(id, IPC_RMID, 0) == 0);
}

ATF_TC_CLEANUP(msgrcv_mtype, tc)
{
	clean();
}

ATF_TC_WITH_CLEANUP(msgrcv_nonblock);
ATF_TC_HEAD(msgrcv_nonblock, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test msgrcv(2) with IPC_NOWAIT");
	atf_tc_set_md_var(tc, "timeout", "10");
}

ATF_TC_BODY(msgrcv_nonblock, tc)
{
	struct msg msg = { MSG_MTYPE_1, { 'a', 'b', 'c' } };
	const ssize_t n = 10;
	int id, sta;
	ssize_t i;
	pid_t pid;

	id = msgget(MSG_KEY, IPC_CREAT | 0600);
	ATF_REQUIRE(id != -1);

	for (i = 0; i < n; i++) {

		ATF_REQUIRE(msgsnd(id, &msg, MSG_LEN, IPC_NOWAIT) == 0);
	}

	pid = fork();
	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {

		while (i != 0) {

			if (msgrcv(id, &msg, MSG_LEN, MSG_MTYPE_1,
			    IPC_NOWAIT) == -1)
				_exit(EXIT_FAILURE);

			i--;
		}

		_exit(EXIT_SUCCESS);
	}

	(void)sleep(2);
	(void)kill(pid, SIGKILL);
	(void)wait(&sta);

	if (WIFSIGNALED(sta) != 0 || WTERMSIG(sta) == SIGKILL)
		atf_tc_fail("msgrcv(2) blocked with IPC_NOWAIT");

	if (WIFEXITED(sta) == 0 && WEXITSTATUS(sta) != EXIT_SUCCESS)
		atf_tc_fail("msgrcv(2) failed");

	ATF_REQUIRE(msgctl(id, IPC_RMID, 0) == 0);
}

ATF_TC_CLEANUP(msgrcv_nonblock, tc)
{
	clean();
}

ATF_TC_WITH_CLEANUP(msgrcv_truncate);
ATF_TC_HEAD(msgrcv_truncate, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test msgrcv(2) with MSG_NOERROR");
}

ATF_TC_BODY(msgrcv_truncate, tc)
{
#define	MSG_SMALLLEN	2
	struct msgsmall {
		long		 mtype;
		char		 buf[MSG_SMALLLEN];
	};

	struct msg msg1 = { MSG_MTYPE_1, { 'a', 'b', 'c' } };
	struct msgsmall msg2 = { MSG_MTYPE_1, { 'x', 'y' } };
	int id;

	id = msgget(MSG_KEY, IPC_CREAT | 0600);
	ATF_REQUIRE(id != -1);

	(void)msgsnd(id, &msg1, MSG_LEN, IPC_NOWAIT);
	(void)msgrcv(id, &msg2, MSG_SMALLLEN,
	    MSG_MTYPE_1, IPC_NOWAIT | MSG_NOERROR);

	ATF_CHECK(msg1.buf[0] == msg2.buf[0]);
	ATF_CHECK(msg1.buf[1] == msg2.buf[1]);

	ATF_REQUIRE(msgctl(id, IPC_RMID, 0) == 0);
}

ATF_TC_CLEANUP(msgrcv_truncate, tc)
{
	clean();
}

static volatile int sig_caught;

static void
sigsys_handler(int signum)
{

	sig_caught = signum;
}

static int
no_kernel_sysvmsg(void)
{
	int id;
	void (*osig)(int);

	sig_caught = 0;
	osig = signal(SIGSYS, sigsys_handler);
	id = msgget(MSG_KEY, IPC_CREAT | 0600);
	if (sig_caught || id == -1)
		return 1;

	(void)msgctl(id, IPC_RMID, 0);
	(void)signal(SIGSYS, osig);

	return 0;
}

ATF_TC(msgrcv_query);
ATF_TC_HEAD(msgrcv_query, tc)
{
	atf_tc_set_md_var(tc, "descr", "Skip msgrcv_* tests - no SYSVMSG");
}
ATF_TC_BODY(msgrcv_query, tc)
{
	atf_tc_skip("No SYSVMSG in kernel");
}

ATF_TP_ADD_TCS(tp)
{

	if (no_kernel_sysvmsg()) {
		ATF_TP_ADD_TC(tp, msgrcv_query);
	} else {
		ATF_TP_ADD_TC(tp, msgrcv_basic);
		ATF_TP_ADD_TC(tp, msgrcv_block);
		ATF_TP_ADD_TC(tp, msgrcv_err);
		ATF_TP_ADD_TC(tp, msgrcv_mtype);
		ATF_TP_ADD_TC(tp, msgrcv_nonblock);
		ATF_TP_ADD_TC(tp, msgrcv_truncate);
	}

	return atf_no_error();
}
