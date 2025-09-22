/*	$OpenBSD: monitor_fdpass.c,v 1.17 2016/02/29 20:22:36 jca Exp $	*/

/*
 * Copyright 2001 Niels Provos <provos@citi.umich.edu>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <errno.h>
#include <string.h>

#include "log.h"
#include "monitor.h"

int
mm_send_fd(int socket, int fd)
{
	struct msghdr   msg;
	union {
		struct cmsghdr hdr;
		char buf[CMSG_SPACE(sizeof(int))];
	} cmsgbuf;
	char		ch = '\0';
	struct cmsghdr *cmsg;
	struct iovec    vec;
	ssize_t         n;

	bzero(&msg, sizeof msg);
	msg.msg_control = (caddr_t)&cmsgbuf.buf;
	msg.msg_controllen = sizeof(cmsgbuf.buf);
	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_len = CMSG_LEN(sizeof(int));
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	*(int *)CMSG_DATA(cmsg) = fd;

	vec.iov_base = &ch;
	vec.iov_len = 1;
	msg.msg_iov = &vec;
	msg.msg_iovlen = 1;

	if ((n = sendmsg(socket, &msg, 0)) == -1) {
		log_error("mm_send_fd: sendmsg(%d)", fd);
		return -1;
	}
	if (n != 1) {
		log_error("mm_send_fd: sendmsg: expected sent 1 got %zd", n);
		return -1;
	}
	return 0;
}

int
mm_receive_fd(int socket)
{
	struct msghdr   msg;
	union {
		struct cmsghdr hdr;
		char buf[CMSG_SPACE(sizeof(int))];
	} cmsgbuf;
	char		ch;
	struct cmsghdr *cmsg;
	struct iovec    vec;
	ssize_t         n;
	int             fd;

	bzero(&msg, sizeof msg);
	vec.iov_base = &ch;
	vec.iov_len = 1;
	msg.msg_iov = &vec;
	msg.msg_iovlen = 1;
	msg.msg_control = &cmsgbuf.buf;
	msg.msg_controllen = sizeof(cmsgbuf.buf);

	if ((n = recvmsg(socket, &msg, 0)) == -1) {
		log_error("mm_receive_fd: recvmsg");
		return -1;
	}
	if (n != 1) {
		log_error("mm_receive_fd: recvmsg: expected received 1 got %zd",
		    n);
		return -1;
	}
	cmsg = CMSG_FIRSTHDR(&msg);
	if (cmsg == NULL) {
		log_error("mm_receive_fd: no message header");
		return -1;
	}
	if (cmsg->cmsg_type != SCM_RIGHTS) {
		log_error("mm_receive_fd: expected type %d got %d", SCM_RIGHTS,
		    cmsg->cmsg_type);
		return -1;
	}
	fd = (*(int *)CMSG_DATA(cmsg));
	return fd;
}
