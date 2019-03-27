/*
 * Copyright (c) 2016 Boris Astardzhiev, Smartcom-Bulgaria AD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice(s), this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified other than the possible
 *    addition of one or more copyright notices.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice(s), this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER(S) ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <poll.h>
#include <stddef.h>
#include "libc_private.h"

ssize_t
recvmmsg(int s, struct mmsghdr *__restrict msgvec, size_t vlen, int flags,
    const struct timespec *__restrict timeout)
{
	struct pollfd pfd[1];
	size_t i, rcvd;
	ssize_t ret;
	int res;
	short ev;

	if (timeout != NULL) {
		pfd[0].fd = s;
		pfd[0].revents = 0;
		pfd[0].events = ev = POLLIN | POLLRDNORM | POLLRDBAND |
		    POLLPRI;
		res = ppoll(&pfd[0], 1, timeout, NULL);
		if (res == -1 || res == 0)
			return (res);
		if (pfd[0].revents & POLLNVAL) {
			errno = EBADF;
			return (-1);
		}
		if ((pfd[0].revents & ev) == 0) {
			errno = ETIMEDOUT;
			return (-1);
		}
	}

	ret = __sys_recvmsg(s, &msgvec[0].msg_hdr, flags);
	if (ret == -1)
		return (ret);

	msgvec[0].msg_len = ret;

	/* 
	 * Do non-blocking receive for second and later messages if
	 * WAITFORONE is set.
	 */
	if (flags & MSG_WAITFORONE)
		flags |= MSG_DONTWAIT;

	rcvd = 1;
	for (i = rcvd; i < vlen; i++, rcvd++) {
		ret = __sys_recvmsg(s, &msgvec[i].msg_hdr, flags);
		if (ret == -1) {
			/* We have received messages. Let caller know
			 * about the data received, socket error is
			 * returned on next invocation.
			 */
			return (rcvd);
		}

		/* Save received bytes. */
		msgvec[i].msg_len = ret;
	}

	return (rcvd);
}
