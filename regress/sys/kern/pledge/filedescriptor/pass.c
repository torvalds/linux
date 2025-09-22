/*	$OpenBSD: pass.c,v 1.1.1.1 2018/04/10 23:00:53 bluhm Exp $	*/
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
#include <string.h>
#include <unistd.h>

#include "header.h"

void
fdops(int fdpre, int fdpost)
{
	struct msghdr	 msg;
	struct iovec	 iov[1];
	char		 buf[1];
	struct cmsghdr	*cmsg;
	union {
		struct cmsghdr	 hdr;
		unsigned char	 buf[CMSG_SPACE(sizeof(int))];
	} cmsgbuf;
	pid_t child;
	int pair[2], status;

	if (socketpair(AF_UNIX, SOCK_DGRAM, 0, pair) == -1)
		err(1, "socketpair");

	memset(&msg, 0, sizeof(msg));
	msg.msg_control = &cmsgbuf.buf;
	msg.msg_controllen = sizeof(cmsgbuf.buf);

	if ((child = fork()) == -1)
		err(1, "fork");

	if (child == 0) {

		/* child process */

		cmsg = CMSG_FIRSTHDR(&msg);
		cmsg->cmsg_len = CMSG_LEN(sizeof(int));
		cmsg->cmsg_level = SOL_SOCKET;
		cmsg->cmsg_type = SCM_RIGHTS;

		*(int *)CMSG_DATA(cmsg) = fdpre;
		if (sendmsg(pair[1], &msg, 0) == -1)
			err(1, "sendmsg pre");

		cmsg = CMSG_FIRSTHDR(&msg);
		cmsg->cmsg_len = CMSG_LEN(sizeof(int));
		cmsg->cmsg_level = SOL_SOCKET;
		cmsg->cmsg_type = SCM_RIGHTS;

		*(int *)CMSG_DATA(cmsg) = fdpost;
		if (sendmsg(pair[1], &msg, 0) == -1)
			err(1, "sendmsg post");

		_exit(0);
	}

	/* parent process */

	msg.msg_iov = iov;
	msg.msg_iovlen = 1;
	iov[0].iov_base = buf;
	iov[0].iov_len = sizeof(buf);

	if (recvmsg(pair[0], &msg, 0) == -1)
		err(1, "recvmsg pre");
	if ((msg.msg_flags & MSG_TRUNC) || (msg.msg_flags & MSG_CTRUNC))
		errx(1, "trunk pre");

	if (recvmsg(pair[0], &msg, 0) == -1)
		err(1, "recvmsg post");
	if ((msg.msg_flags & MSG_TRUNC) || (msg.msg_flags & MSG_CTRUNC))
		errx(1, "trunk post");

	if (waitpid(child, &status, 0) == -1)
		err(1, "waitpid");
	if (status != 0)
		errx(1, "child failed: %d", status);
}
