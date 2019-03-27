/*-
 * Copyright (c) 2012 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Pawel Jakub Dawidek under sponsorship from
 * the FreeBSD Foundation.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <strings.h>

#include "misc.h"

int
pdwait(int pfd)
{
	fd_set fdset;

	FD_ZERO(&fdset);
	FD_SET(pfd, &fdset);

	return (select(pfd + 1, NULL, &fdset, NULL, NULL) == -1 ? -1 : 0);
}

int
descriptor_send(int sock, int fd)
{
	unsigned char ctrl[CMSG_SPACE(sizeof(fd))];
	struct msghdr msg;
	struct cmsghdr *cmsg;

	assert(sock >= 0);
	assert(fd >= 0);

	bzero(&msg, sizeof(msg));
	bzero(&ctrl, sizeof(ctrl));

	msg.msg_iov = NULL;
	msg.msg_iovlen = 0;
	msg.msg_control = ctrl;
	msg.msg_controllen = sizeof(ctrl);

	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	cmsg->cmsg_len = CMSG_LEN(sizeof(fd));
	bcopy(&fd, CMSG_DATA(cmsg), sizeof(fd));

	if (sendmsg(sock, &msg, 0) == -1)
		return (errno);

	return (0);
}

int
descriptor_recv(int sock, int *fdp)
{
	unsigned char ctrl[CMSG_SPACE(sizeof(*fdp))];
	struct msghdr msg;
	struct cmsghdr *cmsg;
	struct iovec iov;
	int val;

	assert(sock >= 0);
	assert(fdp != NULL);

	bzero(&msg, sizeof(msg));
	bzero(&ctrl, sizeof(ctrl));

#if 1
	/*
	 * This doesn't really make sense, as we don't plan to receive any
	 * data, but if no buffer is provided and recv(2) returns 0 without
	 * control message. Must be kernel bug.
	 */
	iov.iov_base = &val;
	iov.iov_len = sizeof(val);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
#else
	msg.msg_iov = NULL;
	msg.msg_iovlen = 0;
#endif
	msg.msg_control = ctrl;
	msg.msg_controllen = sizeof(ctrl);

	if (recvmsg(sock, &msg, 0) == -1)
		return (errno);

	cmsg = CMSG_FIRSTHDR(&msg);
	if (cmsg == NULL || cmsg->cmsg_level != SOL_SOCKET ||
	    cmsg->cmsg_type != SCM_RIGHTS) {
		return (EINVAL);
	}
	bcopy(CMSG_DATA(cmsg), fdp, sizeof(*fdp));

	return (0);
}
