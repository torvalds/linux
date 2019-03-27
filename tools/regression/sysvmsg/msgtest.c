/*-
 * Copyright (c) 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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
 * 
 * Obtained from: $NetBSD: msgtest.c,v 1.7 2002/07/20 08:36:25 grant Exp $
 * $FreeBSD$
 */

/*
 * Test the SVID-compatible Message Queue facility.
 */

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

void	print_msqid_ds (struct msqid_ds *, mode_t);
void	sigsys_handler(int);
void	sigchld_handler (int);
void	cleanup (void);
void	receiver (void);
void	usage (void);

#define	MESSAGE_TEXT_LEN	255

/*
 * Define it as test_mymsg because we already have struct mymsg and we dont
 * want to conflict with it.  Also, regression fails when the default mymsg
 * struct is used, because mtext[] array is '1', so the passed string cannot
 * be processed.
 */
struct test_mymsg {
	long	mtype;
	char	mtext[MESSAGE_TEXT_LEN];
};

const char *m1_str = "California is overrated.";
const char *m2_str = "The quick brown fox jumped over the lazy dog.";

#define	MTYPE_1		1
#define	MTYPE_1_ACK	2

#define	MTYPE_2		3
#define	MTYPE_2_ACK	4

int	sender_msqid = -1;
pid_t	child_pid;

key_t	msgkey;

int
main(int argc, char *argv[])
{
	struct sigaction sa;
	struct msqid_ds m_ds;
	struct test_mymsg m;
	sigset_t sigmask;

	if (argc != 2)
		usage();

	/*
	 * Install a SIGSYS handler so that we can exit gracefully if
	 * System V Message Queue support isn't in the kernel.
	 */
	sa.sa_handler = sigsys_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	if (sigaction(SIGSYS, &sa, NULL) == -1)
		err(1, "sigaction SIGSYS");

	/*
	 * Install and SIGCHLD handler to deal with all possible exit
	 * conditions of the receiver.
	 */
	sa.sa_handler = sigchld_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	if (sigaction(SIGCHLD, &sa, NULL) == -1)
		err(1, "sigaction SIGCHLD");

	msgkey = ftok(argv[1], 4160);

	/*
	 * Initialize child_pid to ourselves to that the cleanup function
	 * works before we create the receiver.
	 */
	child_pid = getpid();

	/*
	 * Make sure that when the sender exits, the message queue is
	 * removed.
	 */
	if (atexit(cleanup) == -1)
		err(1, "atexit");

	if ((sender_msqid = msgget(msgkey, IPC_CREAT | 0640)) == -1)
		err(1, "msgget");

	if (msgctl(sender_msqid, IPC_STAT, &m_ds) == -1)
		err(1, "msgctl IPC_STAT");

	print_msqid_ds(&m_ds, 0640);

	m_ds.msg_perm.mode = (m_ds.msg_perm.mode & ~0777) | 0600;

	if (msgctl(sender_msqid, IPC_SET, &m_ds) == -1)
		err(1, "msgctl IPC_SET");

	bzero(&m_ds, sizeof m_ds);

	if (msgctl(sender_msqid, IPC_STAT, &m_ds) == -1)
		err(1, "msgctl IPC_STAT");

	if ((m_ds.msg_perm.mode & 0777) != 0600)
		err(1, "IPC_SET of mode didn't hold");

	print_msqid_ds(&m_ds, 0600);

	switch ((child_pid = fork())) {
	case -1:
		err(1, "fork");
		/* NOTREACHED */

	case 0:
		receiver();
		break;

	default:
		break;
	}

	/*
	 * Send the first message to the receiver and wait for the ACK.
	 */
	m.mtype = MTYPE_1;
	strcpy(m.mtext, m1_str);
	if (msgsnd(sender_msqid, &m, strlen(m1_str) + 1, 0) == -1)
		err(1, "sender: msgsnd 1");

	if (msgrcv(sender_msqid, &m, sizeof(m.mtext), MTYPE_1_ACK, 0) !=
	    strlen(m1_str) + 1)
		err(1, "sender: msgrcv 1 ack");

	print_msqid_ds(&m_ds, 0600);

	/*
	 * Send the second message to the receiver and wait for the ACK.
	 */
	m.mtype = MTYPE_2;
	strcpy(m.mtext, m2_str);
	if (msgsnd(sender_msqid, &m, strlen(m2_str) + 1, 0) == -1)
		err(1, "sender: msgsnd 2");

	if (msgrcv(sender_msqid, &m, sizeof(m.mtext), MTYPE_2_ACK, 0) !=
	    strlen(m2_str) + 1)
		err(1, "sender: msgrcv 2 ack");

	/*
	 * Suspend forever; when we get SIGCHLD, the handler will exit.
	 */
	sigemptyset(&sigmask);
	(void) sigsuspend(&sigmask);

	/*
	 * ...and any other signal is an unexpected error.
	 */
	errx(1, "sender: received unexpected signal");
}

void
sigsys_handler(int signo)
{

	errx(1, "System V Message Queue support is not present in the kernel");
}

void
sigchld_handler(int signo)
{
	struct msqid_ds m_ds;
	int cstatus;

	/*
	 * Reap the child; if it exited successfully, then the test passed!
	 */
	if (waitpid(child_pid, &cstatus, 0) != child_pid)
		err(1, "waitpid");

	if (WIFEXITED(cstatus) == 0)
		errx(1, "receiver exited abnormally");

	if (WEXITSTATUS(cstatus) != 0)
		errx(1, "receiver exited with status %d",
		    WEXITSTATUS(cstatus));

	/*
	 * If we get here, the child has exited normally, and thus
	 * we should exit normally too.  First, tho, we print out
	 * the final stats for the message queue.
	 */

	if (msgctl(sender_msqid, IPC_STAT, &m_ds) == -1)
		err(1, "msgctl IPC_STAT");

	print_msqid_ds(&m_ds, 0600);

	exit(0);
}

void
cleanup()
{

	/*
	 * If we're the sender, and it exists, remove the message queue.
	 */
	if (child_pid != 0 && sender_msqid != -1) {
		if (msgctl(sender_msqid, IPC_RMID, NULL) == -1)
			warn("msgctl IPC_RMID");
	}
}

void
print_msqid_ds(struct msqid_ds *mp, mode_t mode)
{
	uid_t uid = geteuid();
	gid_t gid = getegid();

	printf("PERM: uid %d, gid %d, cuid %d, cgid %d, mode 0%o\n",
	    mp->msg_perm.uid, mp->msg_perm.gid,
	    mp->msg_perm.cuid, mp->msg_perm.cgid,
	    mp->msg_perm.mode & 0777);

	printf("qnum %lu, qbytes %lu, lspid %d, lrpid %d\n",
	    mp->msg_qnum, (u_long)mp->msg_qbytes, mp->msg_lspid,
	    mp->msg_lrpid);

	printf("stime: %s", ctime(&mp->msg_stime));
	printf("rtime: %s", ctime(&mp->msg_rtime));
	printf("ctime: %s", ctime(&mp->msg_ctime));

	/*
	 * Sanity check a few things.
	 */

	if (mp->msg_perm.uid != uid || mp->msg_perm.cuid != uid)
		errx(1, "uid mismatch");

	if (mp->msg_perm.gid != gid || mp->msg_perm.cgid != gid)
		errx(1, "gid mismatch");

	if ((mp->msg_perm.mode & 0777) != mode)
		errx(1, "mode mismatch");
}

void
usage()
{

	fprintf(stderr, "usage: %s keypath\n", getprogname());
	exit(1);
}

void
receiver()
{
	struct test_mymsg m;
	int msqid;

	if ((msqid = msgget(msgkey, 0)) == -1)
		err(1, "receiver: msgget");

	/*
	 * Receive the first message, print it, and send an ACK.
	 */

	if (msgrcv(msqid, &m, sizeof(m.mtext), MTYPE_1, 0) !=
	    strlen(m1_str) + 1)
		err(1, "receiver: msgrcv 1");

	printf("%s\n", m.mtext);
	if (strcmp(m.mtext, m1_str) != 0)
		err(1, "receiver: message 1 data isn't correct");

	m.mtype = MTYPE_1_ACK;

	if (msgsnd(msqid, &m, strlen(m1_str) + 1, 0) == -1)
		err(1, "receiver: msgsnd ack 1");

	/*
	 * Receive the second message, print it, and send an ACK.
	 */

	if (msgrcv(msqid, &m, sizeof(m.mtext), MTYPE_2, 0) !=
	    strlen(m2_str) + 1)
		err(1, "receiver: msgrcv 2");

	printf("%s\n", m.mtext);
	if (strcmp(m.mtext, m2_str) != 0)
		err(1, "receiver: message 2 data isn't correct");

	m.mtype = MTYPE_2_ACK;

	if (msgsnd(msqid, &m, strlen(m2_str) + 1, 0) == -1)
		err(1, "receiver: msgsnd ack 2");

	exit(0);
}
