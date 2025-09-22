/*	$OpenBSD: unfdpass.c,v 1.25 2024/11/06 17:43:53 claudio Exp $	*/
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
 * Test passing of file descriptors over Unix domain sockets and socketpairs.
 */

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/un.h>
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
void	child(int, int, int);
void	catch_sigchld(int);

int
main(int argc, char *argv[])
{
	struct msghdr msg;
	int sock, pfd[2], fd, i;
	int listensock = -1;
	char fname[16], buf[64];
	struct cmsghdr *cmp;
	int *files = NULL;
	struct sockaddr_un sun, csun;
	int csunlen;
	pid_t pid;
	union {
		struct cmsghdr hdr;
		char buf[CMSG_SPACE(sizeof(int) * 3)];
	} cmsgbuf;
	int pflag, oflag, rflag;
	int type = SOCK_STREAM;
	extern char *__progname;

	pflag = 0;
	oflag = 0;
	rflag = 0;
	while ((i = getopt(argc, argv, "opqr")) != -1) {
		switch (i) {
		case 'o':
			oflag = 1;
			break;
		case 'p':
			pflag = 1;
			break;
		case 'q':
			type = SOCK_SEQPACKET;
			break;
		case 'r':
			rflag = 1;
			break;
		default:
			fprintf(stderr, "usage: %s [-opqr]\n", __progname);
			exit(1);
		}
	}

	/*
	 * Create the test files.
	 */
	for (i = 0; i < 5; i++) {
		(void) snprintf(fname, sizeof fname, "file%d", i + 1);
		if ((fd = open(fname, O_WRONLY|O_CREAT|O_TRUNC, 0666)) == -1)
			err(1, "open %s", fname);
		(void) snprintf(buf, sizeof buf, "This is file %d.\n", i + 1);
		if (write(fd, buf, strlen(buf)) != (ssize_t) strlen(buf))
			err(1, "write %s", fname);
		(void) close(fd);
	}

	if (pflag) {
		/*
		 * Create the socketpair
		 */
		if (socketpair(PF_LOCAL, type, 0, pfd) == -1)
			err(1, "socketpair");
	} else {
		/*
		 * Create the listen socket.
		 */
		if ((listensock = socket(PF_LOCAL, type, 0)) == -1)
			err(1, "socket");

		(void) unlink(SOCK_NAME);
		(void) memset(&sun, 0, sizeof(sun));
		sun.sun_family = AF_LOCAL;
		(void) strlcpy(sun.sun_path, SOCK_NAME, sizeof sun.sun_path);

		if (bind(listensock, (struct sockaddr *)&sun, sizeof(sun)) == -1)
			err(1, "bind");

		if (listen(listensock, 1) == -1)
			err(1, "listen");
		pfd[0] = pfd[1] = -1;
	}

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
		child(pfd[1], type, oflag);
		/* NOTREACHED */
	}

	if (pfd[0] != -1) {
		close(pfd[1]);
		sock = pfd[0];
	} else {
		/*
		 * Wait for the sender to connect.
		 */
		if ((sock = accept(listensock, (struct sockaddr *)&csun,
		    &csunlen)) == -1)
		err(1, "accept");
	}

	/*
	 * Give sender a chance to run.  We will get going again
	 * once the SIGCHLD arrives.
	 */
	(void) sleep(10);

	if (rflag) {
		if (read(sock, buf, sizeof(buf)) < 0)
			err(1, "read");
		printf("read successfully returned\n");
		exit(0);
	}

	/*
	 * Grab the descriptors passed to us.
	 */
	memset(&msg, 0, sizeof(msg));
	msg.msg_control = &cmsgbuf.buf;
	msg.msg_controllen = sizeof(cmsgbuf.buf);

	if (recvmsg(sock, &msg, 0) < 0) {
		if (errno == EMSGSIZE) {
			printf("recvmsg returned EMSGSIZE\n");
			exit(0);
		} else
			err(1, "recvmsg");
	}

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
			if (cmp->cmsg_len != CMSG_LEN(sizeof(int) * 3))
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
		warnx("didn't get fd control message");
	else {
		for (i = 0; i < 3; i++) {
			(void) memset(buf, 0, sizeof(buf));
			if (read(files[i], buf, sizeof(buf)) <= 0)
				err(1, "read file %d (%d)", i + 1, files[i]);
			printf("%s", buf);
		}
	}

	/*
	 * All done!
	 */
	exit(0);
}

void
catch_sigchld(int sig)
{
	int save_errno = errno;
	int status;

	(void) wait(&status);
	errno = save_errno;
}

void
child(int sock, int type, int oflag)
{
	struct msghdr msg;
	char fname[16];
	struct cmsghdr *cmp;
	int i, fd, nfds = 3;
	struct sockaddr_un sun;
	size_t len;
	char *cmsgbuf;
	int *files;

	/*
	 * Create socket if needed and connect to the receiver.
	 */
	if (sock == -1) {
		if ((sock = socket(PF_LOCAL, type, 0)) == -1)
			err(1, "child socket");

		(void) memset(&sun, 0, sizeof(sun));
		sun.sun_family = AF_LOCAL;
		(void) strlcpy(sun.sun_path, SOCK_NAME, sizeof sun.sun_path);

		if (connect(sock, (struct sockaddr *)&sun, sizeof(sun)) == -1)
			err(1, "child connect");
	}

	if (oflag)
		nfds = 5;
	len = CMSG_SPACE(sizeof(int) * nfds);
	if ((cmsgbuf = malloc(len)) == NULL)
		err(1, "child");

	(void) memset(&msg, 0, sizeof(msg));
	msg.msg_control = cmsgbuf;
	msg.msg_controllen = len;

	cmp = CMSG_FIRSTHDR(&msg);
	cmp->cmsg_len = CMSG_LEN((sizeof(int) * nfds));
	cmp->cmsg_level = SOL_SOCKET;
	cmp->cmsg_type = SCM_RIGHTS;

	/*
	 * Open the files again, and pass them to the parent over the socket.
	 */
	files = (int *)CMSG_DATA(cmp);
	for (i = 0; i < nfds; i++) {
		(void) snprintf(fname, sizeof fname, "file%d", i + 1);
		if ((fd = open(fname, O_RDONLY)) == -1)
			err(1, "child open %s", fname);
		files[i] = fd;
	}

	if (sendmsg(sock, &msg, 0))
		err(1, "child sendmsg");

	/*
	 * All done!
	 */
	exit(0);
}
