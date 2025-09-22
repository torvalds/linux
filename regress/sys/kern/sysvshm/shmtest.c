/*	$NetBSD: shmtest.c,v 1.2 2001/02/19 22:44:41 cgd Exp $	*/

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
 */

/*
 * Test the SVID-compatible Shared Memory facility.
 */

#include <sys/ipc.h>
#include <sys/mman.h>
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

int	main(int, char *[]);
void	print_shmid_ds(struct shmid_ds *, mode_t);
void	sigsys_handler(int);
void	sigchld_handler(int);
void	cleanup(void);
void	receiver(void);

const char *m_str = "The quick brown fox jumped over the lazy dog.";

int	sender_shmid = -1;
pid_t	child_pid;

key_t	shmkey;

char keyname[] = "/tmp/msgtestXXXXXXXX";

int verbose;

size_t	pgsize;

int
main(argc, argv)
	int argc;
	char *argv[];
{
	struct sigaction sa;
	struct shmid_ds s_ds;
	sigset_t sigmask;
	char *shm_buf;
	int fd, ch;

	if ((fd = mkstemp(keyname)) < 0)
		err(1, "mkstemp");

	close(fd);

	while ((ch = getopt(argc, argv, "v")) != -1) {
		switch (ch) {
		case 'v':
			verbose = 1;
			break;
		default:
			fprintf(stderr, "Usage: shmtest [-v]\n");
			exit(1);
		}
	}

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

	if (verbose)
		print_shmid_ds(&s_ds, 0640);

	s_ds.shm_perm.mode = (s_ds.shm_perm.mode & ~0777) | 0600;

	if (shmctl(sender_shmid, IPC_SET, &s_ds) == -1)
		err(1, "shmctl IPC_SET");

	memset(&s_ds, 0, sizeof(s_ds));

	if (shmctl(sender_shmid, IPC_STAT, &s_ds) == -1)
		err(1, "shmctl IPC_STAT");

	if ((s_ds.shm_perm.mode & 0777) != 0600)
		err(1, "IPC_SET of mode didn't hold");

	if (verbose)
		print_shmid_ds(&s_ds, 0600);

	if ((shm_buf = shmat(sender_shmid, NULL, 0)) == (void *) -1)
		err(1, "sender: shmat");

	/*
	 * Write the test pattern into the shared memory buffer.
	 */
	strlcpy(shm_buf, m_str, pgsize);

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

void
sigsys_handler(signo)
	int signo;
{

	errx(1, "System V Shared Memory support is not present in the kernel");
}

void
sigchld_handler(signo)
	int signo;
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

	if (verbose)
		print_shmid_ds(&s_ds, 0600);

	exit(0);
}

void
cleanup()
{

	/*
	 * If we're the sender, and it exists, remove the shared memory area.
	 */
	if (child_pid != 0 && sender_shmid != -1) {
		if (shmctl(sender_shmid, IPC_RMID, NULL) == -1)
			warn("shmctl IPC_RMID");
	}

	remove(keyname);
}

void
print_shmid_ds(sp, mode)
	struct shmid_ds *sp;
	mode_t mode;
{
	uid_t uid = geteuid();
	gid_t gid = getegid();

	printf("PERM: uid %u, gid %u, cuid %u, cgid %u, mode 0%o\n",
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

void
receiver()
{
	int shmid;
	void *shm_buf;
	void *block;

	if ((shmid = shmget(shmkey, pgsize, 0)) == -1)
		err(1, "receiver: shmget");

	if ((shm_buf = shmat(shmid, NULL, 0)) == (void *) -1)
		err(1, "receiver: shmat");

	if (verbose)
		printf("%.*s\n", (int)pgsize, (const char *)shm_buf);
	if (strcmp((const char *)shm_buf, m_str) != 0)
		errx(1, "receiver: data isn't correct");

	/* mmap() a page to get a distinct, freeable address */
	block = mmap(NULL, pgsize, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON,
	    -1, 0);
	if (block == MAP_FAILED)
		err(1, "receiver: mmap");

	/* detach, then try to attach, conflicting with the mmap() */
	if (shmdt(shm_buf) == -1)
		err(1, "receiver: shmdt");
	if ((shm_buf = shmat(shmid, block, 0)) != (void *) -1)
		errx(1, "receiver: shmat(conflict) succeeded!");
	if (errno != ENOMEM)
		err(1, "receiver: shmat(conflict) wrong error");

	/* free up that address and try again */
	if (munmap(block, pgsize) == -1)
		err(1, "receiver: munmap");
	if ((shm_buf = shmat(shmid, block, 0)) == (void *) -1)
		err(1, "receiver: shmat(fixed)");

	if (shm_buf != block)
		errx(1, "receiver: shmat not at expected address: %p != %p",
		    shm_buf, block);

	if (verbose)
		printf("%.*s\n", (int)pgsize, (const char *)shm_buf);
	if (strcmp((const char *)shm_buf, m_str) != 0)
		errx(1, "receiver: data isn't correct second time");

	exit(0);
}
