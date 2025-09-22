/*	$OpenBSD: t_msgctl.c,v 1.2 2021/12/13 16:56:48 deraadt Exp $	*/
/* $NetBSD: t_msgctl.c,v 1.7 2017/10/07 17:15:44 kre Exp $ */

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

#define MSG_KEY		12345689
#define MSG_MTYPE_1	0x41

struct msg {
	long		 mtype;
	char		 buf[3];
};

static void		clean(void);

static void
clean(void)
{
	int id;

	if ((id = msgget(MSG_KEY, 0)) != -1)
		(void)msgctl(id, IPC_RMID, 0);
}

ATF_TC_WITH_CLEANUP(msgctl_err);
ATF_TC_HEAD(msgctl_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test errors from msgctl(2)");
}

ATF_TC_BODY(msgctl_err, tc)
{
	const int cmd[] = { IPC_STAT, IPC_SET, IPC_RMID };
	struct msqid_ds msgds;
	size_t i;
	int id;

	(void)memset(&msgds, 0, sizeof(struct msqid_ds));

	id = msgget(MSG_KEY, IPC_CREAT | 0600);
	ATF_REQUIRE(id != -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(EINVAL, msgctl(id, INT_MAX, &msgds) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(EFAULT, msgctl(id, IPC_STAT, (void *)-1) == -1);

	for (i = 0; i < __arraycount(cmd); i++) {
		errno = 0;
		ATF_REQUIRE_ERRNO(EINVAL, msgctl(-1, cmd[i], &msgds) == -1);
	}

	ATF_REQUIRE(msgctl(id, IPC_RMID, 0) == 0);
}

ATF_TC_CLEANUP(msgctl_err, tc)
{
	clean();
}

ATF_TC_WITH_CLEANUP(msgctl_perm);
ATF_TC_HEAD(msgctl_perm, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test permissions with msgctl(2)");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(msgctl_perm, tc)
{
	struct msqid_ds msgds;
	struct passwd *pw;
	pid_t pid;
	int sta;
	int id;

	(void)memset(&msgds, 0, sizeof(struct msqid_ds));

	pw = getpwnam("nobody");
	id = msgget(MSG_KEY, IPC_CREAT | 0600);

	ATF_REQUIRE(id != -1);
	ATF_REQUIRE(pw != NULL);
	ATF_REQUIRE(msgctl(id, IPC_STAT, &msgds) == 0);

	pid = fork();
	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {

		if (setuid(pw->pw_uid) != 0)
			_exit(EX_OSERR);

		msgds.msg_perm.uid = getuid();
		msgds.msg_perm.gid = getgid();

		errno = 0;

		if (msgctl(id, IPC_SET, &msgds) == 0)
			_exit(EXIT_FAILURE);

		if (errno != EPERM)
			_exit(EXIT_FAILURE);

		(void)memset(&msgds, 0, sizeof(struct msqid_ds));

		if (msgctl(id, IPC_STAT, &msgds) != 0)
			_exit(EX_OSERR);

		msgds.msg_qbytes = 1;

		if (msgctl(id, IPC_SET, &msgds) == 0)
			_exit(EXIT_FAILURE);

		if (errno != EPERM)
			_exit(EXIT_FAILURE);

		_exit(EXIT_SUCCESS);
	}

	(void)wait(&sta);

	if (WIFEXITED(sta) == 0) {

		if (WEXITSTATUS(sta) == EX_OSERR)
			atf_tc_fail("system call failed");

		if (WEXITSTATUS(sta) == EXIT_FAILURE)
			atf_tc_fail("UID %u manipulated root's "
			    "message queue", pw->pw_uid);
	}

	ATF_REQUIRE(msgctl(id, IPC_RMID, 0) == 0);
}

ATF_TC_CLEANUP(msgctl_perm, tc)
{
	clean();
}

ATF_TC_WITH_CLEANUP(msgctl_pid);
ATF_TC_HEAD(msgctl_pid, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test that PIDs are updated");
}

ATF_TC_BODY(msgctl_pid, tc)
{
	struct msg msg = { MSG_MTYPE_1, { 'a', 'b', 'c' } };
	struct msqid_ds msgds;
	int id, sta;
	pid_t pid;

	id = msgget(MSG_KEY, IPC_CREAT | 0600);
	ATF_REQUIRE(id != -1);

	pid = fork();
	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {

		(void)msgsnd(id, &msg, sizeof(struct msg), IPC_NOWAIT);

		_exit(EXIT_SUCCESS);
	}

	(void)sleep(1);
	(void)wait(&sta);
	(void)memset(&msgds, 0, sizeof(struct msqid_ds));

	ATF_REQUIRE(msgctl(id, IPC_STAT, &msgds) == 0);

	if (pid != msgds.msg_lspid)
		atf_tc_fail("the PID of last msgsnd(2) was not updated");

	pid = fork();
	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {

		(void)msgrcv(id, &msg,
		    sizeof(struct msg), MSG_MTYPE_1, IPC_NOWAIT);

		_exit(EXIT_SUCCESS);
	}

	(void)sleep(1);
	(void)wait(&sta);
	(void)memset(&msgds, 0, sizeof(struct msqid_ds));

	ATF_REQUIRE(msgctl(id, IPC_STAT, &msgds) == 0);

	if (pid != msgds.msg_lrpid)
		atf_tc_fail("the PID of last msgrcv(2) was not updated");

	ATF_REQUIRE(msgctl(id, IPC_RMID, 0) == 0);
}

ATF_TC_CLEANUP(msgctl_pid, tc)
{
	clean();
}

ATF_TC_WITH_CLEANUP(msgctl_set);
ATF_TC_HEAD(msgctl_set, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test msgctl(2) with IPC_SET");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(msgctl_set, tc)
{
	struct msqid_ds msgds;
	struct passwd *pw;
	int id;

	(void)memset(&msgds, 0, sizeof(struct msqid_ds));

	pw = getpwnam("nobody");
	id = msgget(MSG_KEY, IPC_CREAT | 0600);

	ATF_REQUIRE(id != -1);
	ATF_REQUIRE(pw != NULL);
	ATF_REQUIRE(msgctl(id, IPC_STAT, &msgds) == 0);

	msgds.msg_perm.uid = pw->pw_uid;

	if (msgctl(id, IPC_SET, &msgds) != 0)
		atf_tc_fail("root failed to change the UID of message queue");

	msgds.msg_perm.uid = getuid();
	msgds.msg_perm.gid = pw->pw_gid;

	if (msgctl(id, IPC_SET, &msgds) != 0)
		atf_tc_fail("root failed to change the GID of message queue");

	/*
	 * Note: setting the qbytes to zero fails even as root.
	 */
	msgds.msg_qbytes = 1;
	msgds.msg_perm.gid = getgid();

	if (msgctl(id, IPC_SET, &msgds) != 0)
		atf_tc_fail("root failed to change qbytes of message queue");

	ATF_REQUIRE(msgctl(id, IPC_RMID, 0) == 0);
}

ATF_TC_CLEANUP(msgctl_set, tc)
{
	clean();
}

ATF_TC_WITH_CLEANUP(msgctl_time);
ATF_TC_HEAD(msgctl_time, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test that access times are updated");
}

ATF_TC_BODY(msgctl_time, tc)
{
	struct msg msg = { MSG_MTYPE_1, { 'a', 'b', 'c' } };
	struct msqid_ds msgds;
	time_t t;
	int id;

	id = msgget(MSG_KEY, IPC_CREAT | 0600);
	ATF_REQUIRE(id != -1);

	t = time(NULL);

	(void)memset(&msgds, 0, sizeof(struct msqid_ds));
	(void)msgsnd(id, &msg, sizeof(struct msg), IPC_NOWAIT);
	(void)msgctl(id, IPC_STAT, &msgds);

	if (llabs(t - msgds.msg_stime) > 1)
		atf_tc_fail("time of last msgsnd(2) was not updated");

	if (msgds.msg_rtime != 0)
		atf_tc_fail("time of last msgrcv(2) was updated incorrectly");

	t = time(NULL);

	(void)memset(&msgds, 0, sizeof(struct msqid_ds));
	(void)msgrcv(id, &msg, sizeof(struct msg), MSG_MTYPE_1, IPC_NOWAIT);
	(void)msgctl(id, IPC_STAT, &msgds);

	if (llabs(t - msgds.msg_rtime) > 1)
		atf_tc_fail("time of last msgrcv(2) was not updated");

	/*
	 * Note: this is non-zero even after the memset(3).
	 */
	if (msgds.msg_stime == 0)
		atf_tc_fail("time of last msgsnd(2) was updated incorrectly");

	ATF_REQUIRE(msgctl(id, IPC_RMID, 0) == 0);
}

ATF_TC_CLEANUP(msgctl_time, tc)
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

ATF_TC(msgctl_query);
ATF_TC_HEAD(msgctl_query, tc)
{
	atf_tc_set_md_var(tc, "descr", "Skip msgctl_* tests - no SYSVMSG");
}
ATF_TC_BODY(msgctl_query, tc)
{
	atf_tc_skip("No SYSVMSG in kernel");
}

ATF_TP_ADD_TCS(tp)
{

	if (no_kernel_sysvmsg()) {
		ATF_TP_ADD_TC(tp, msgctl_query);
	} else {
		ATF_TP_ADD_TC(tp, msgctl_err);
		ATF_TP_ADD_TC(tp, msgctl_perm);
		ATF_TP_ADD_TC(tp, msgctl_pid);
		ATF_TP_ADD_TC(tp, msgctl_set);
		ATF_TP_ADD_TC(tp, msgctl_time);
	}

	return atf_no_error();
}
