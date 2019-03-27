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
 * Obtained from: $NetBSD: shmtest.c,v 1.3 2002/07/20 08:36:26 grant Exp $
 * $FreeBSD$
 */

/*
 * Test the SVID-compatible Shared Memory facility.
 */

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void print_shmid_ds(struct shmid_ds *, mode_t);
static void sigsys_handler(int);
static void sigchld_handler(int);
static void cleanup(void);
static void receiver(void);
static void usage(void);

static const char *m_str = "The quick brown fox jumped over the lazy dog.";

static int sender_shmid = -1;
static pid_t child_pid;
static key_t shmkey;
static size_t pgsize;

int
main(int argc, char *argv[])
{
	struct sigaction sa;
	struct shmid_ds s_ds;
	sigset_t sigmask;
	char *shm_buf;

	if (argc != 2)
		usage();

	/*
	 * Install a SIGSYS handler so that we can exit gracefully if
	 * System V Shared Memory support isn't in the kernel.
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

	pgsize = sysconf(_SC_PAGESIZE);

	shmkey = ftok(argv[1], 4160);

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

	if ((sender_shmid = shmget(shmkey, pgsize, IPC_CREAT | 0640)) == -1)
		err(1, "shmget");

	if (shmctl(sender_shmid, IPC_STAT, &s_ds) == -1)
		err(1, "shmctl IPC_STAT");

	print_shmid_ds(&s_ds, 0640);

	s_ds.shm_perm.mode = (s_ds.shm_perm.mode & ~0777) | 0600;

	if (shmctl(sender_shmid, IPC_SET, &s_ds) == -1)
		err(1, "shmctl IPC_SET");

	memset(&s_ds, 0, sizeof(s_ds));

	if (shmctl(sender_shmid, IPC_STAT, &s_ds) == -1)
		err(1, "shmctl IPC_STAT");

	if ((s_ds.shm_perm.mode & 0777) != 0600)
		err(1, "IPC_SET of mode didn't hold");

	print_shmid_ds(&s_ds, 0600);

	if ((shm_buf = shmat(sender_shmid, NULL, 0)) == (void *) -1)
		err(1, "sender: shmat");

	/*
	 * Write the test pattern into the shared memory buffer.
	 */
	strcpy(shm_buf, m_str);

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
	 * Suspend forever; when we get SIGCHLD, the handler will exit.
	 */
	sigemptyset(&sigmask);
	(void) sigsuspend(&sigmask);

	/*
	 * ...and any other signal is an unexpected error.
	 */
	errx(1, "sender: received unexpected signal");
}

static void
sigsys_handler(int signo __unused)
{

	errx(1, "System V Shared Memory support is not present in the kernel");
}

static void
sigchld_handler(int signo __unused)
{
	struct shmid_ds s_ds;
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

	if (shmctl(sender_shmid, IPC_STAT, &s_ds) == -1)
		err(1, "shmctl IPC_STAT");

	print_shmid_ds(&s_ds, 0600);

	exit(0);
}

static void
cleanup(void)
{

	/*
	 * If we're the sender, and it exists, remove the shared memory area.
	 */
	if (child_pid != 0 && sender_shmid != -1) {
		if (shmctl(sender_shmid, IPC_RMID, NULL) == -1)
			warn("shmctl IPC_RMID");
	}
}

static void
print_shmid_ds(struct shmid_ds *sp, mode_t mode)
{
	uid_t uid = geteuid();
	gid_t gid = getegid();

	printf("PERM: uid %d, gid %d, cuid %d, cgid %d, mode 0%o\n",
	    sp->shm_perm.uid, sp->shm_perm.gid,
	    sp->shm_perm.cuid, sp->shm_perm.cgid,
	    sp->shm_perm.mode & 0777);

	printf("segsz %lu, lpid %d, cpid %d, nattch %u\n",
	    (u_long)sp->shm_segsz, sp->shm_lpid, sp->shm_cpid,
	    sp->shm_nattch);

	printf("atime: %s", ctime(&sp->shm_atime));
	printf("dtime: %s", ctime(&sp->shm_dtime));
	printf("ctime: %s", ctime(&sp->shm_ctime));

	/*
	 * Sanity check a few things.
	 */

	if (sp->shm_perm.uid != uid || sp->shm_perm.cuid != uid)
		errx(1, "uid mismatch");

	if (sp->shm_perm.gid != gid || sp->shm_perm.cgid != gid)
		errx(1, "gid mismatch");

	if ((sp->shm_perm.mode & 0777) != mode)
		errx(1, "mode mismatch");
}

static void
usage(void)
{

	fprintf(stderr, "usage: %s keypath\n", getprogname());
	exit(1);
}

static void
receiver(void)
{
	int shmid;
	void *shm_buf;

	if ((shmid = shmget(shmkey, pgsize, 0)) == -1)
		err(1, "receiver: shmget");

	if ((shm_buf = shmat(shmid, NULL, 0)) == (void *) -1)
		err(1, "receiver: shmat");

	printf("%s\n", (const char *)shm_buf);
	if (strcmp((const char *)shm_buf, m_str) != 0)
		err(1, "receiver: data isn't correct");

	exit(0);
}
