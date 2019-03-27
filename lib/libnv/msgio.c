/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 The FreeBSD Foundation
 * Copyright (c) 2013 Mariusz Zaborski <oshogbo@FreeBSD.org>
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

#include <sys/param.h>
#include <sys/socket.h>

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef HAVE_PJDLOG
#include <pjdlog.h>
#endif

#include "common_impl.h"
#include "msgio.h"

#ifndef	HAVE_PJDLOG
#include <assert.h>
#define	PJDLOG_ASSERT(...)		assert(__VA_ARGS__)
#define	PJDLOG_RASSERT(expr, ...)	assert(expr)
#define	PJDLOG_ABORT(...)		abort()
#endif

#define	PKG_MAX_SIZE	(MCLBYTES / CMSG_SPACE(sizeof(int)) - 1)

static int
msghdr_add_fd(struct cmsghdr *cmsg, int fd)
{

	PJDLOG_ASSERT(fd >= 0);

	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	cmsg->cmsg_len = CMSG_LEN(sizeof(fd));
	bcopy(&fd, CMSG_DATA(cmsg), sizeof(fd));

	return (0);
}

static int
msghdr_get_fd(struct cmsghdr *cmsg)
{
	int fd;

	if (cmsg == NULL || cmsg->cmsg_level != SOL_SOCKET ||
	    cmsg->cmsg_type != SCM_RIGHTS ||
	    cmsg->cmsg_len != CMSG_LEN(sizeof(fd))) {
		errno = EINVAL;
		return (-1);
	}

	bcopy(CMSG_DATA(cmsg), &fd, sizeof(fd));
#ifndef MSG_CMSG_CLOEXEC
	/*
	 * If the MSG_CMSG_CLOEXEC flag is not available we cannot set the
	 * close-on-exec flag atomically, but we still want to set it for
	 * consistency.
	 */
	(void) fcntl(fd, F_SETFD, FD_CLOEXEC);
#endif

	return (fd);
}

static void
fd_wait(int fd, bool doread)
{
	fd_set fds;

	PJDLOG_ASSERT(fd >= 0);

	FD_ZERO(&fds);
	FD_SET(fd, &fds);
	(void)select(fd + 1, doread ? &fds : NULL, doread ? NULL : &fds,
	    NULL, NULL);
}

static int
msg_recv(int sock, struct msghdr *msg)
{
	int flags;

	PJDLOG_ASSERT(sock >= 0);

#ifdef MSG_CMSG_CLOEXEC
	flags = MSG_CMSG_CLOEXEC;
#else
	flags = 0;
#endif

	for (;;) {
		fd_wait(sock, true);
		if (recvmsg(sock, msg, flags) == -1) {
			if (errno == EINTR)
				continue;
			return (-1);
		}
		break;
	}

	return (0);
}

static int
msg_send(int sock, const struct msghdr *msg)
{

	PJDLOG_ASSERT(sock >= 0);

	for (;;) {
		fd_wait(sock, false);
		if (sendmsg(sock, msg, 0) == -1) {
			if (errno == EINTR)
				continue;
			return (-1);
		}
		break;
	}

	return (0);
}

/*
 * MacOS/Linux do not define struct cmsgcred but we need to bootstrap libnv
 * when building on non-FreeBSD systems. Since they are not used during
 * bootstrap we can just omit these two functions there.
 */
#ifndef __FreeBSD__
#warning "cred_send() not supported on non-FreeBSD systems"
#else
int
cred_send(int sock)
{
	unsigned char credbuf[CMSG_SPACE(sizeof(struct cmsgcred))];
	struct msghdr msg;
	struct cmsghdr *cmsg;
	struct iovec iov;
	uint8_t dummy;

	bzero(credbuf, sizeof(credbuf));
	bzero(&msg, sizeof(msg));
	bzero(&iov, sizeof(iov));

	/*
	 * XXX: We send one byte along with the control message, because
	 *      setting msg_iov to NULL only works if this is the first
	 *      packet send over the socket. Once we send some data we
	 *      won't be able to send credentials anymore. This is most
	 *      likely a kernel bug.
	 */
	dummy = 0;
	iov.iov_base = &dummy;
	iov.iov_len = sizeof(dummy);

	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = credbuf;
	msg.msg_controllen = sizeof(credbuf);

	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_len = CMSG_LEN(sizeof(struct cmsgcred));
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_CREDS;

	if (msg_send(sock, &msg) == -1)
		return (-1);

	return (0);
}

int
cred_recv(int sock, struct cmsgcred *cred)
{
	unsigned char credbuf[CMSG_SPACE(sizeof(struct cmsgcred))];
	struct msghdr msg;
	struct cmsghdr *cmsg;
	struct iovec iov;
	uint8_t dummy;

	bzero(credbuf, sizeof(credbuf));
	bzero(&msg, sizeof(msg));
	bzero(&iov, sizeof(iov));

	iov.iov_base = &dummy;
	iov.iov_len = sizeof(dummy);

	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = credbuf;
	msg.msg_controllen = sizeof(credbuf);

	if (msg_recv(sock, &msg) == -1)
		return (-1);

	cmsg = CMSG_FIRSTHDR(&msg);
	if (cmsg == NULL ||
	    cmsg->cmsg_len != CMSG_LEN(sizeof(struct cmsgcred)) ||
	    cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_CREDS) {
		errno = EINVAL;
		return (-1);
	}
	bcopy(CMSG_DATA(cmsg), cred, sizeof(*cred));

	return (0);
}
#endif

static int
fd_package_send(int sock, const int *fds, size_t nfds)
{
	struct msghdr msg;
	struct cmsghdr *cmsg;
	struct iovec iov;
	unsigned int i;
	int serrno, ret;
	uint8_t dummy;

	PJDLOG_ASSERT(sock >= 0);
	PJDLOG_ASSERT(fds != NULL);
	PJDLOG_ASSERT(nfds > 0);

	bzero(&msg, sizeof(msg));

	/*
	 * XXX: Look into cred_send function for more details.
	 */
	dummy = 0;
	iov.iov_base = &dummy;
	iov.iov_len = sizeof(dummy);

	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_controllen = nfds * CMSG_SPACE(sizeof(int));
	msg.msg_control = calloc(1, msg.msg_controllen);
	if (msg.msg_control == NULL)
		return (-1);

	ret = -1;

	for (i = 0, cmsg = CMSG_FIRSTHDR(&msg); i < nfds && cmsg != NULL;
	    i++, cmsg = CMSG_NXTHDR(&msg, cmsg)) {
		if (msghdr_add_fd(cmsg, fds[i]) == -1)
			goto end;
	}

	if (msg_send(sock, &msg) == -1)
		goto end;

	ret = 0;
end:
	serrno = errno;
	free(msg.msg_control);
	errno = serrno;
	return (ret);
}

static int
fd_package_recv(int sock, int *fds, size_t nfds)
{
	struct msghdr msg;
	struct cmsghdr *cmsg;
	unsigned int i;
	int serrno, ret;
	struct iovec iov;
	uint8_t dummy;

	PJDLOG_ASSERT(sock >= 0);
	PJDLOG_ASSERT(nfds > 0);
	PJDLOG_ASSERT(fds != NULL);

	bzero(&msg, sizeof(msg));
	bzero(&iov, sizeof(iov));

	/*
	 * XXX: Look into cred_send function for more details.
	 */
	iov.iov_base = &dummy;
	iov.iov_len = sizeof(dummy);

	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_controllen = nfds * CMSG_SPACE(sizeof(int));
	msg.msg_control = calloc(1, msg.msg_controllen);
	if (msg.msg_control == NULL)
		return (-1);

	ret = -1;

	if (msg_recv(sock, &msg) == -1)
		goto end;

	for (i = 0, cmsg = CMSG_FIRSTHDR(&msg); i < nfds && cmsg != NULL;
	    i++, cmsg = CMSG_NXTHDR(&msg, cmsg)) {
		fds[i] = msghdr_get_fd(cmsg);
		if (fds[i] < 0)
			break;
	}

	if (cmsg != NULL || i < nfds) {
		int fd;

		/*
		 * We need to close all received descriptors, even if we have
		 * different control message (eg. SCM_CREDS) in between.
		 */
		for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL;
		    cmsg = CMSG_NXTHDR(&msg, cmsg)) {
			fd = msghdr_get_fd(cmsg);
			if (fd >= 0)
				close(fd);
		}
		errno = EINVAL;
		goto end;
	}

	ret = 0;
end:
	serrno = errno;
	free(msg.msg_control);
	errno = serrno;
	return (ret);
}

int
fd_recv(int sock, int *fds, size_t nfds)
{
	unsigned int i, step, j;
	int ret, serrno;

	if (nfds == 0 || fds == NULL) {
		errno = EINVAL;
		return (-1);
	}

	ret = i = step = 0;
	while (i < nfds) {
		if (PKG_MAX_SIZE < nfds - i)
			step = PKG_MAX_SIZE;
		else
			step = nfds - i;
		ret = fd_package_recv(sock, fds + i, step);
		if (ret != 0) {
			/* Close all received descriptors. */
			serrno = errno;
			for (j = 0; j < i; j++)
				close(fds[j]);
			errno = serrno;
			break;
		}
		i += step;
	}

	return (ret);
}

int
fd_send(int sock, const int *fds, size_t nfds)
{
	unsigned int i, step;
	int ret;

	if (nfds == 0 || fds == NULL) {
		errno = EINVAL;
		return (-1);
	}

	ret = i = step = 0;
	while (i < nfds) {
		if (PKG_MAX_SIZE < nfds - i)
			step = PKG_MAX_SIZE;
		else
			step = nfds - i;
		ret = fd_package_send(sock, fds + i, step);
		if (ret != 0)
			break;
		i += step;
	}

	return (ret);
}

int
buf_send(int sock, void *buf, size_t size)
{
	ssize_t done;
	unsigned char *ptr;

	PJDLOG_ASSERT(sock >= 0);
	PJDLOG_ASSERT(size > 0);
	PJDLOG_ASSERT(buf != NULL);

	ptr = buf;
	do {
		fd_wait(sock, false);
		done = send(sock, ptr, size, 0);
		if (done == -1) {
			if (errno == EINTR)
				continue;
			return (-1);
		} else if (done == 0) {
			errno = ENOTCONN;
			return (-1);
		}
		size -= done;
		ptr += done;
	} while (size > 0);

	return (0);
}

int
buf_recv(int sock, void *buf, size_t size)
{
	ssize_t done;
	unsigned char *ptr;

	PJDLOG_ASSERT(sock >= 0);
	PJDLOG_ASSERT(buf != NULL);

	ptr = buf;
	while (size > 0) {
		fd_wait(sock, true);
		done = recv(sock, ptr, size, 0);
		if (done == -1) {
			if (errno == EINTR)
				continue;
			return (-1);
		} else if (done == 0) {
			errno = ENOTCONN;
			return (-1);
		}
		size -= done;
		ptr += done;
	}

	return (0);
}
