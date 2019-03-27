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
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>

#include "uc_common.h"
#include "t_generic.h"
#include "t_cmsgcred.h"
#include "t_cmsgcred_sockcred.h"

static int
t_cmsgcred_sockcred_server(int fd1)
{
	struct msghdr msghdr;
	struct iovec iov[1];
	struct cmsghdr *cmsghdr;
	void *cmsg_data, *cmsg1_data, *cmsg2_data;
	size_t cmsg_size, cmsg1_size, cmsg2_size;
	u_int i;
	int fd2, rv, val;

	fd2 = -1;
	rv = -2;

	cmsg1_size = CMSG_SPACE(SOCKCREDSIZE(uc_cfg.proc_cred.gid_num));
	cmsg2_size = CMSG_SPACE(sizeof(struct cmsgcred));
	cmsg1_data = malloc(cmsg1_size);
	cmsg2_data = malloc(cmsg2_size);
	if (cmsg1_data == NULL || cmsg2_data == NULL) {
		uc_logmsg("malloc");
		goto done;
	}

	uc_dbgmsg("setting LOCAL_CREDS");
	val = 1;
	if (setsockopt(fd1, 0, LOCAL_CREDS, &val, sizeof(val)) < 0) {
		uc_logmsg("setsockopt(LOCAL_CREDS)");
		goto done;
	}

	if (uc_sync_send() < 0)
		goto done;

	if (uc_cfg.sock_type == SOCK_STREAM) {
		fd2 = uc_socket_accept(fd1);
		if (fd2 < 0)
			goto done;
	} else
		fd2 = fd1;

	cmsg_data = cmsg1_data;
	cmsg_size = cmsg1_size;
	rv = -1;
	for (i = 1; i <= uc_cfg.ipc_msg.msg_num; ++i) {
		uc_dbgmsg("message #%u", i);

		uc_msghdr_init_server(&msghdr, iov, cmsg_data, cmsg_size);
		if (uc_message_recv(fd2, &msghdr) < 0) {
			rv = -2;
			break;
		}

		if (uc_check_msghdr(&msghdr, sizeof(*cmsghdr)) < 0)
			break;

		cmsghdr = CMSG_FIRSTHDR(&msghdr);
		if (i == 1 || uc_cfg.sock_type == SOCK_DGRAM) {
			if (uc_check_scm_creds_sockcred(cmsghdr) < 0)
				break;
		} else {
			if (uc_check_scm_creds_cmsgcred(cmsghdr) < 0)
				break;
		}

		cmsg_data = cmsg2_data;
		cmsg_size = cmsg2_size;
	}
	if (i > uc_cfg.ipc_msg.msg_num)
		rv = 0;
done:
	free(cmsg1_data);
	free(cmsg2_data);
	if (uc_cfg.sock_type == SOCK_STREAM && fd2 >= 0)
		if (uc_socket_close(fd2) < 0)
			rv = -2;
	return (rv);
}

int
t_cmsgcred_sockcred(void)
{
	return (t_generic(t_cmsgcred_client, t_cmsgcred_sockcred_server));
}
