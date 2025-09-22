/*	$OpenBSD: sendrecvfd.c,v 1.1 2017/02/22 11:30:00 tb Exp $ */
/*
 * Copyright (c) 2017 Sebastien Marie <semarie@online.fr>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum testtype {
	nopledge,
	sendfd,
	recvfd,
	nosendfd,
	norecvfd,
};

static void do_receiver(enum testtype type, int sock);
static void do_sender(enum testtype type, int sock, int fd);
__dead static void usage();


static void
do_receiver(enum testtype type, int sock)
{
	struct msghdr msg;
	struct cmsghdr *cmsg;
	union {
		struct cmsghdr hdr;
		unsigned char buf[CMSG_SPACE(sizeof(int))];
	} cmsgbuf;

	/* pledge */
	switch(type) {
	case recvfd:
		if (pledge("stdio recvfd", NULL) == -1)
			err(EXIT_FAILURE, "receiver: pledge");
		break;

	case norecvfd:
		if (pledge("stdio", NULL) == -1)
			err(EXIT_FAILURE, "receiver: pledge");
		break;

	default:
		/* no pledge */
		break;
	}

	memset(&msg, 0, sizeof(msg));
	msg.msg_control = &cmsgbuf.buf;
	msg.msg_controllen = sizeof(cmsgbuf.buf);

	if (recvmsg(sock, &msg, 0) == -1)
		err(EXIT_FAILURE, "receiver: recvmsg");

	if ((msg.msg_flags & MSG_TRUNC) || (msg.msg_flags & MSG_CTRUNC))
		errx(EXIT_FAILURE, "receiver: control message truncated");

	for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL;
	    cmsg = CMSG_NXTHDR(&msg, cmsg)) {
		if (cmsg->cmsg_len == CMSG_LEN(sizeof(int)) &&
		    cmsg->cmsg_level == SOL_SOCKET &&
		    cmsg->cmsg_type == SCM_RIGHTS) {

			int fd = *(int *)CMSG_DATA(cmsg);
			struct stat sb;

			/* test received fd */
			if (fstat(fd, &sb) == -1)
				err(EXIT_FAILURE, "receiver: fstat");
		}
	}
}

static void
do_sender(enum testtype type, int sock, int fd)
{
	struct msghdr msg;
	struct cmsghdr *cmsg;
	union {
		struct cmsghdr hdr;
		unsigned char buf[CMSG_SPACE(sizeof(int))];
	} cmsgbuf;

	/* pledge */
	switch (type) {
	case sendfd:
		if (pledge("stdio sendfd", NULL) == -1)
			err(EXIT_FAILURE, "sender: pledge");
		break;

	case nosendfd:
		if (pledge("stdio", NULL) == -1)
			err(EXIT_FAILURE, "sender: pledge");
		break;

	default:
		/* no pledge */
		break;
	}

	memset(&msg, 0, sizeof(msg));
	msg.msg_control = &cmsgbuf.buf;
	msg.msg_controllen = sizeof(cmsgbuf.buf);

	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_len = CMSG_LEN(sizeof(int));
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	*(int *)CMSG_DATA(cmsg) = fd;

	if (sendmsg(sock, &msg, 0) == -1)
		err(EXIT_FAILURE, "sender: sendmsg");
}

__dead static void
usage()
{
	printf("usage: %s testtype vnodetype\n", getprogname());
	printf("  testtype  = nopledge sendfd recvfd nosendfd norecvfd\n");
	printf("  vnodetype = VREG VDIR VBLK VCHAR VLNK VSOCK VFIFO\n");
	exit(EXIT_FAILURE);
}

int
main(int argc, char *argv[])
{
	enum testtype type;
	int fd;
	int sv[2], status;
	pid_t child;

	/*
	 * parse arguments
	 */

	if (argc != 3)
		usage();

	if (strcmp(argv[1], "nopledge") == 0 ) {
		/* test sendfd/recvfd without pledge */
		type = nopledge;

	} else if (strcmp(argv[1], "sendfd") == 0) {
		/* test sendfd process with "stdio sendfd" */
		type = sendfd;

	} else if (strcmp(argv[1], "recvfd") == 0) {
		/* test recvfd process with "stdio recvfd" */
		type = recvfd;

	} else if (strcmp(argv[1], "nosendfd") == 0) {
		/* test sendfd process with "stdio" (without "sendfd") */
		type = nosendfd;

	} else if (strcmp(argv[1], "norecvfd") == 0) {
		/* test recvfd process with "stdio" (without "recvfd") */
		type = norecvfd;

	} else
		usage();

	/* open a file descriptor according to vnodetype requested */
	if (strcmp(argv[2], "VREG") == 0) {
		if ((fd = open("/etc/passwd", O_RDONLY)) == -1)
			err(EXIT_FAILURE, "open: VREG: /etc/passwd");

	} else if (strcmp(argv[2], "VDIR") == 0) {
		if ((fd = open("/dev", O_RDONLY)) == -1)
			err(EXIT_FAILURE, "open: VDIR: /dev");

	} else if (strcmp(argv[2], "VBLK") == 0) {
		if ((fd = open("/dev/vnd0c", O_RDONLY)) == -1)
		    err(EXIT_FAILURE, "open: VBLK: /dev/vnd0c");

	} else if (strcmp(argv[2], "VCHR") == 0) {
		if ((fd = open("/dev/null", O_RDONLY)) == -1)
			err(EXIT_FAILURE, "open: VCHR: /dev/null");

	} else if (strcmp(argv[2], "VLNK") == 0) {
		if ((fd = open("/etc/termcap", O_RDONLY)) == -1)
			err(EXIT_FAILURE, "open: VCHR: /etc/termcap");

	} else if (strcmp(argv[2], "VSOCK") == 0) {
		/* create socket */
		if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
			err(EXIT_FAILURE, "socket: VSOCK");

	} else if (strcmp(argv[2], "VFIFO") == 0) {
		/* unlink possibly existing file (from previous run) */
		unlink("fifo");

		/* create a new named fifo */
		if (mkfifo("fifo", 0600) == -1)
			err(EXIT_FAILURE, "mkfifo: VFIFO");

		/* open it */
		if ((fd = open("fifo", O_RDONLY|O_NONBLOCK)) == -1)
			err(EXIT_FAILURE, "open: VFIFO: fifo");

		/* unlink the file now */
		unlink("fifo");
	} else
		usage();


	/*
	 * do test
	 */

	/* communication socket */
	if (socketpair(AF_LOCAL, SOCK_STREAM, PF_UNSPEC, sv) == -1)
		err(EXIT_FAILURE, "socketpair");

	/* create two procs and pass fd from one to another */
	switch (child = fork()) {
	case -1:	/* error */
		err(EXIT_FAILURE, "fork");

	case 0:		/* child: receiver */
		close(fd);
		close(sv[0]);

		do_receiver(type, sv[1]);
		_exit(EXIT_SUCCESS);

	default:	/* parent: sender */
		close(sv[1]);

		do_sender(type, sv[0], fd);

		/* wait for child */
		while (waitpid(child, &status, 0) < 0) {
			if (errno == EAGAIN)
				continue;

			err(EXIT_FAILURE, "waitpid");
		}

		if (! WIFEXITED(status)) {
			if (WIFSIGNALED(status))
				errx(EXIT_FAILURE,
				    "child (receiver): WTERMSIG(): %d",
				    WTERMSIG(status));

			errx(EXIT_FAILURE, "child (receiver): !WIFEXITED");
		}

		if (WEXITSTATUS(status) != 0)
			errx(EXIT_FAILURE, "child(receiver): WEXITSTATUS(): %d",
			    WEXITSTATUS(status));

		exit(EXIT_SUCCESS);
	}
}
