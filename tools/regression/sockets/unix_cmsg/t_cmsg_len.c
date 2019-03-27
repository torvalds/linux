/*-
 * Copyright (c) 2005 Andrey Simonenko
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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
#include <sys/socket.h>
#include <sys/un.h>
#include <inttypes.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "uc_common.h"
#include "t_generic.h"
#include "t_cmsg_len.h"

#ifndef __LP64__
static int
t_cmsg_len_client(int fd)
{
	struct msghdr msghdr;
	struct iovec iov[1];
	struct cmsghdr *cmsghdr;
	void *cmsg_data;
	size_t size, cmsg_size;
	socklen_t socklen;
	int rv;

	if (uc_sync_recv() < 0)
		return (-2);

	rv = -2;

	cmsg_size = CMSG_SPACE(sizeof(struct cmsgcred));
	cmsg_data = malloc(cmsg_size);
	if (cmsg_data == NULL) {
		uc_logmsg("malloc");
		goto done;
	}
	uc_msghdr_init_client(&msghdr, iov, cmsg_data, cmsg_size,
	    SCM_CREDS, sizeof(struct cmsgcred));
	cmsghdr = CMSG_FIRSTHDR(&msghdr);

	if (uc_socket_connect(fd) < 0)
		goto done;

	size = msghdr.msg_iov != NULL ? msghdr.msg_iov->iov_len : 0;
	rv = -1;
	for (socklen = 0; socklen < CMSG_LEN(0); ++socklen) {
		cmsghdr->cmsg_len = socklen;
		uc_dbgmsg("send: data size %zu", size);
		uc_dbgmsg("send: msghdr.msg_controllen %u",
		    (u_int)msghdr.msg_controllen);
		uc_dbgmsg("send: cmsghdr.cmsg_len %u",
		    (u_int)cmsghdr->cmsg_len);
		if (sendmsg(fd, &msghdr, 0) < 0) {
			uc_dbgmsg("sendmsg(2) failed: %s; retrying",
			    strerror(errno));
			continue;
		}
		uc_logmsgx("sent message with cmsghdr.cmsg_len %u < %u",
		    (u_int)cmsghdr->cmsg_len, (u_int)CMSG_LEN(0));
		break;
	}
	if (socklen == CMSG_LEN(0))
		rv = 0;

	if (uc_sync_send() < 0) {
		rv = -2;
		goto done;
	}
done:
	free(cmsg_data);
	return (rv);
}

static int
t_cmsg_len_server(int fd1)
{
	int fd2, rv;

	if (uc_sync_send() < 0)
		return (-2);

	rv = -2;

	if (uc_cfg.sock_type == SOCK_STREAM) {
		fd2 = uc_socket_accept(fd1);
		if (fd2 < 0)
			goto done;
	} else
		fd2 = fd1;

	if (uc_sync_recv() < 0)
		goto done;

	rv = 0;
done:
	if (uc_cfg.sock_type == SOCK_STREAM && fd2 >= 0)
		if (uc_socket_close(fd2) < 0)
			rv = -2;
	return (rv);
}

int
t_cmsg_len(void)
{
	return (t_generic(t_cmsg_len_client, t_cmsg_len_server));
}
#endif
