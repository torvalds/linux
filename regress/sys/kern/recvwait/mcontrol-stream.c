/*	$OpenBSD: mcontrol-stream.c,v 1.1 2018/12/19 21:21:59 bluhm Exp $	*/
/*
 * Copyright (c) 2018 Alexander Bluhm <bluhm@openbsd.org>
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

#include <sys/socket.h>
#include <sys/wait.h>

#include <err.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

int
main(int argc, char *argv[])
{
	pid_t pid;
	int fd, s[2], status;
	char str[] = "foo";
	struct msghdr msg;
	struct cmsghdr *cmsg;
	union {
		struct cmsghdr hdr;
		unsigned char buf[CMSG_SPACE(sizeof(int))];
	} cmsgbuf;
	struct iovec io_vector[1];

	if ((fd = open("/dev/null", O_RDONLY)) == -1)
		err(1, "open");
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, s) == -1)
		err(1, "socketpair");

	if ((pid = fork()) == -1)
		err(1, "fork");
	if (pid == 0) {
		char buf[16];
		ssize_t n;

		/*
		 * The first recv(2) will block until the second control
		 * message causes a short read.
		 */
		if ((n = recv(s[1], buf, sizeof(buf), MSG_WAITALL)) == -1)
			err(1, "recv 1");
		if ((size_t)n != strlen(str))
			errx(1, "recv 1: len %zd", n);
		if ((n = recv(s[1], buf, sizeof(buf), 0)) == -1)
			err(1, "recv 2");
		if ((size_t)n != strlen(str))
			errx(1, "recv 2: len %zd", n);
		_exit(0);
	}

	io_vector[0].iov_base = str;
	io_vector[0].iov_len = strlen(str);

	memset(&msg, 0, sizeof(msg));
	msg.msg_control = &cmsgbuf.buf;
	msg.msg_controllen = sizeof(cmsgbuf.buf);
	msg.msg_iov = io_vector;
	msg.msg_iovlen = 1;

	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_len = CMSG_LEN(sizeof(int));
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	*(int *)CMSG_DATA(cmsg) = fd;

	if (sendmsg(s[0], &msg, 0) == -1)
		err(1, "sendmsg 1");

	/* Wait until child is blocking in recv(2) MSG_WAITALL syscall. */
	sleep(2);

	/* This will insert a control mbuf while soreceive() is sleeping. */
	if (sendmsg(s[0], &msg, 0) == -1)
		err(1, "sendmsg 2");

	if (close(s[0]) == -1)
		err(1, "close");

	if (waitpid(pid, &status, 0) == -1)
		err(1, "waitpid");
	if (status != 0)
		errx(1, "child: %d", status);

	return 0;
}
