/*	$OpenBSD: privsep_fdpass.c,v 1.8 2012/04/04 17:24:50 deraadt Exp $	*/

/*
 * Copyright (c) 2002 Matthieu Herrb
 * Copyright (c) 2001 Niels Provos <provos@citi.umich.edu>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "privsep.h"

void
send_fd(int sock, int fd)
{
	struct msghdr msg;
	union {
		struct cmsghdr hdr;
		char buf[CMSG_SPACE(sizeof(int))];
	} cmsgbuf;
	struct cmsghdr *cmsg;
	struct iovec vec;
	int result = 0;
	ssize_t n;

	memset(&msg, 0, sizeof(msg));

	if (fd >= 0) {
		msg.msg_control = (caddr_t)&cmsgbuf.buf;
		msg.msg_controllen = sizeof(cmsgbuf.buf);
		cmsg = CMSG_FIRSTHDR(&msg);
		cmsg->cmsg_len = CMSG_LEN(sizeof(int));
		cmsg->cmsg_level = SOL_SOCKET;
		cmsg->cmsg_type = SCM_RIGHTS;
		*(int *)CMSG_DATA(cmsg) = fd;
	} else
		result = errno;

	vec.iov_base = &result;
	vec.iov_len = sizeof(int);
	msg.msg_iov = &vec;
	msg.msg_iovlen = 1;

	if ((n = sendmsg(sock, &msg, 0)) == -1)
		warn("%s: sendmsg(%d)", __func__, sock);
	if (n != sizeof(int))
		warnx("%s: sendmsg: expected sent 1 got %ld",
		    __func__, (long)n);
}

int
receive_fd(int sock)
{
	struct msghdr msg;
	union {
		struct cmsghdr hdr;
		char buf[CMSG_SPACE(sizeof(int))];
	} cmsgbuf;
	struct cmsghdr *cmsg;
	struct iovec vec;
	ssize_t n;
	int result;
	int fd;

	memset(&msg, 0, sizeof(msg));
	vec.iov_base = &result;
	vec.iov_len = sizeof(int);
	msg.msg_iov = &vec;
	msg.msg_iovlen = 1;
	msg.msg_control = &cmsgbuf.buf;
	msg.msg_controllen = sizeof(cmsgbuf.buf);

	if ((n = recvmsg(sock, &msg, 0)) == -1) {
		warn("%s: recvmsg", __func__);
		return -1;
	}
	if (n != sizeof(int))
		warnx("%s: recvmsg: expected received 1 got %ld",
		    __func__, (long)n);
	if (result == 0) {
		cmsg = CMSG_FIRSTHDR(&msg);
		if (cmsg == NULL) {
			warnx("%s: no message header", __func__);
			return (-1);
		}
		if (cmsg->cmsg_type != SCM_RIGHTS)
			warnx("%s: expected type %d got %d", __func__,
			    SCM_RIGHTS, cmsg->cmsg_type);
		fd = (*(int *)CMSG_DATA(cmsg));
		return (fd);
	} else {
		errno = result;
		return (-1);
	}
}
