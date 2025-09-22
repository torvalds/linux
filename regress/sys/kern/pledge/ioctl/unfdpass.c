/*	$OpenBSD: unfdpass.c,v 1.4 2023/03/08 04:43:06 guenther Exp $	*/
/*	$NetBSD: unfdpass.c,v 1.3 1998/06/24 23:51:30 thorpej Exp $	*/

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
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
 * Test passing of a /dev/pf file descriptors over socketpair,
 * and of passing a fd opened before the first pledge call that
 * is then used for ioctl()
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <net/if.h>
#include <net/pfvar.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	SOCK_NAME	"test-sock"

int	main(int, char *[]);
void	child(int, int);
void	catch_sigchld(int);

int
main(int argc, char *argv[])
{
	struct msghdr msg;
	int sock, pfd[2], i;
	struct cmsghdr *cmp;
	int *files = NULL;
	int fdpf_prepledge, fdpf_postpledge;
	pid_t pid;
	union {
		struct cmsghdr hdr;
		char buf[CMSG_SPACE(sizeof(int))];
	} cmsgbuf;
	int type = SOCK_STREAM;
	int fail = 0;
	struct pf_status status;
	extern char *__progname;

	if ((fdpf_prepledge = open("/dev/pf", O_RDWR)) == -1) {
		err(1, "%s: cannot open pf socket", __func__);
	}

	if (pledge("stdio rpath wpath sendfd recvfd proc pf", NULL)
	    == -1)
		err(1, "pledge");

	if ((fdpf_postpledge = open("/dev/pf", O_RDWR)) == -1) {
		err(1, "%s: cannot open pf socket", __func__);
	}

	while ((i = getopt(argc, argv, "f")) != -1) {
		switch (i) {
		case 'f':
			fail = 1;
			break;
		default:
			fprintf(stderr, "usage: %s [-f]\n", __progname);
			exit(1);
		}
	}

	if (socketpair(PF_LOCAL, type, 0, pfd) == -1)
		err(1, "socketpair");

	/*
	 * Create the sender.
	 */
	(void) signal(SIGCHLD, catch_sigchld);
	pid = fork();
	switch (pid) {
	case -1:
		err(1, "fork");
		/* NOTREACHED */

	case 0:
		if (pfd[0] != -1)
			close(pfd[0]);
		child(pfd[1], (fail ? fdpf_postpledge : fdpf_prepledge));
		/* NOTREACHED */
	}

	if (pfd[0] != -1) {
		close(pfd[1]);
		sock = pfd[0];
	} else {
		err(1, "should not happen");
	}

	if (pledge("stdio recvfd pf", NULL) == -1)
		err(1, "pledge");

	/*
	 * Give sender a chance to run.  We will get going again
	 * once the SIGCHLD arrives.
	 */
	(void) sleep(10);

	/*
	 * Grab the descriptors passed to us.
	 */
	(void) memset(&msg, 0, sizeof(msg));
	msg.msg_control = &cmsgbuf.buf;
	msg.msg_controllen = sizeof(cmsgbuf.buf);

	if (recvmsg(sock, &msg, 0) < 0)
		err(1, "recvmsg");

	(void) close(sock);

	if (msg.msg_controllen == 0)
		errx(1, "no control messages received");

	if (msg.msg_flags & MSG_CTRUNC)
		errx(1, "lost control message data");

	for (cmp = CMSG_FIRSTHDR(&msg); cmp != NULL;
	    cmp = CMSG_NXTHDR(&msg, cmp)) {
		if (cmp->cmsg_level != SOL_SOCKET)
			errx(1, "bad control message level %d",
			    cmp->cmsg_level);

		switch (cmp->cmsg_type) {
		case SCM_RIGHTS:
			if (cmp->cmsg_len != CMSG_LEN(sizeof(int)))
				errx(1, "bad fd control message length %d",
				    cmp->cmsg_len);

			files = (int *)CMSG_DATA(cmp);
			break;

		default:
			errx(1, "unexpected control message");
			/* NOTREACHED */
		}
	}

	/*
	 * Read the files and print their contents.
	 */
	if (files == NULL)
		errx(1, "didn't get fd control message");

	if (ioctl(files[0], DIOCGETSTATUS, &status) == -1)
		err(1, "%s: DIOCGETSTATUS", __func__);
	if (!status.running)
		warnx("%s: pf is disabled", __func__);

	/*
	 * All done!
	 */
	return 0;
}

void
catch_sigchld(sig)
	int sig;
{
	int save_errno = errno;
	int status;

	(void) wait(&status);
	errno = save_errno;
}

void
child(int sock, int fdpf)
{
	struct msghdr msg;
	struct cmsghdr *cmp;
	union {
		struct cmsghdr hdr;
		char buf[CMSG_SPACE(sizeof(int))];
	} cmsgbuf;
	int *files;

	(void) memset(&msg, 0, sizeof(msg));
	msg.msg_control = &cmsgbuf.buf;
	msg.msg_controllen = sizeof(cmsgbuf.buf);

	cmp = CMSG_FIRSTHDR(&msg);
	cmp->cmsg_len = CMSG_LEN(sizeof(int));
	cmp->cmsg_level = SOL_SOCKET;
	cmp->cmsg_type = SCM_RIGHTS;

	files = (int *)CMSG_DATA(cmp);
	files[0] = fdpf;

	if (pledge("stdio sendfd", NULL) == -1)
		errx(1, "pledge");

	if (sendmsg(sock, &msg, 0))
		err(1, "child sendmsg");

	/*
	 * All done!
	 */
	_exit(0);
}
