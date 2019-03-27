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
#include <sys/ucred.h>
#include <sys/un.h>
#include <stdarg.h>
#include <stdbool.h>

#include "uc_common.h"
#include "t_generic.h"
#include "t_peercred.h"

static int
check_xucred(const struct xucred *xucred, socklen_t len)
{
	int rc;

	if (len != sizeof(*xucred)) {
		uc_logmsgx("option value size %zu != %zu",
		    (size_t)len, sizeof(*xucred));
		return (-1);
	}

	uc_dbgmsg("xucred.cr_version %u", xucred->cr_version);
	uc_dbgmsg("xucred.cr_uid %lu", (u_long)xucred->cr_uid);
	uc_dbgmsg("xucred.cr_ngroups %d", xucred->cr_ngroups);

	rc = 0;

	if (xucred->cr_version != XUCRED_VERSION) {
		uc_logmsgx("xucred.cr_version %u != %d",
		    xucred->cr_version, XUCRED_VERSION);
		rc = -1;
	}
	if (xucred->cr_uid != uc_cfg.proc_cred.euid) {
		uc_logmsgx("xucred.cr_uid %lu != %lu (EUID)",
		    (u_long)xucred->cr_uid, (u_long)uc_cfg.proc_cred.euid);
		rc = -1;
	}
	if (xucred->cr_ngroups == 0) {
		uc_logmsgx("xucred.cr_ngroups == 0");
		rc = -1;
	}
	if (xucred->cr_ngroups < 0) {
		uc_logmsgx("xucred.cr_ngroups < 0");
		rc = -1;
	}
	if (xucred->cr_ngroups > XU_NGROUPS) {
		uc_logmsgx("xucred.cr_ngroups %hu > %u (max)",
		    xucred->cr_ngroups, XU_NGROUPS);
		rc = -1;
	}
	if (xucred->cr_groups[0] != uc_cfg.proc_cred.egid) {
		uc_logmsgx("xucred.cr_groups[0] %lu != %lu (EGID)",
		    (u_long)xucred->cr_groups[0], (u_long)uc_cfg.proc_cred.egid);
		rc = -1;
	}
	if (uc_check_groups("xucred.cr_groups", xucred->cr_groups,
	    "xucred.cr_ngroups", xucred->cr_ngroups, false) < 0)
		rc = -1;
	return (rc);
}

static int
t_peercred_client(int fd)
{
	struct xucred xucred;
	socklen_t len;

	if (uc_sync_recv() < 0)
		return (-1);

	if (uc_socket_connect(fd) < 0)
		return (-1);

	len = sizeof(xucred);
	if (getsockopt(fd, 0, LOCAL_PEERCRED, &xucred, &len) < 0) {
		uc_logmsg("getsockopt(LOCAL_PEERCRED)");
		return (-1);
	}

	if (check_xucred(&xucred, len) < 0)
		return (-1);

	return (0);
}

static int
t_peercred_server(int fd1)
{
	struct xucred xucred;
	socklen_t len;
	int fd2, rv;

	if (uc_sync_send() < 0)
		return (-2);

	fd2 = uc_socket_accept(fd1);
	if (fd2 < 0)
		return (-2);

	len = sizeof(xucred);
	if (getsockopt(fd2, 0, LOCAL_PEERCRED, &xucred, &len) < 0) {
		uc_logmsg("getsockopt(LOCAL_PEERCRED)");
		rv = -2;
		goto done;
	}

	if (check_xucred(&xucred, len) < 0) {
		rv = -1;
		goto done;
	}

	rv = 0;
done:
	if (uc_socket_close(fd2) < 0)
		rv = -2;
	return (rv);
}

int
t_peercred(void)
{
	return (t_generic(t_peercred_client, t_peercred_server));
}
