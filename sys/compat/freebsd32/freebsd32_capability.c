/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 The FreeBSD Foundation
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

#include "opt_capsicum.h"

#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/filedesc.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/syscallsubr.h>
#include <sys/sysproto.h>

#include <security/audit/audit.h>

#include <compat/freebsd32/freebsd32_proto.h>

#ifdef CAPABILITIES

MALLOC_DECLARE(M_FILECAPS);

int
freebsd32_cap_ioctls_limit(struct thread *td,
    struct freebsd32_cap_ioctls_limit_args *uap)
{
	u_long *cmds;
	uint32_t *cmds32;
	size_t ncmds;
	u_int i;
	int error;

	ncmds = uap->ncmds;

	if (ncmds > 256)	/* XXX: Is 256 sane? */
		return (EINVAL);

	if (ncmds == 0) {
		cmds = NULL;
	} else {
		cmds32 = malloc(sizeof(cmds32[0]) * ncmds, M_FILECAPS, M_WAITOK);
		error = copyin(uap->cmds, cmds32, sizeof(cmds32[0]) * ncmds);
		if (error != 0) {
			free(cmds32, M_FILECAPS);
			return (error);
		}
		cmds = malloc(sizeof(cmds[0]) * ncmds, M_FILECAPS, M_WAITOK);
		for (i = 0; i < ncmds; i++)
			cmds[i] = cmds32[i];
		free(cmds32, M_FILECAPS);
	}

	return (kern_cap_ioctls_limit(td, uap->fd, cmds, ncmds));
}

int
freebsd32_cap_ioctls_get(struct thread *td,
    struct freebsd32_cap_ioctls_get_args *uap)
{
	struct filedesc *fdp;
	struct filedescent *fdep;
	uint32_t *cmds32;
	u_long *cmds;
	size_t maxcmds;
	int error, fd;
	u_int i;

	fd = uap->fd;
	cmds32 = uap->cmds;
	maxcmds = uap->maxcmds;

	AUDIT_ARG_FD(fd);

	fdp = td->td_proc->p_fd;
	FILEDESC_SLOCK(fdp);

	if (fget_locked(fdp, fd) == NULL) {
		error = EBADF;
		goto out;
	}

	/*
	 * If all ioctls are allowed (fde_nioctls == -1 && fde_ioctls == NULL)
	 * the only sane thing we can do is to not populate the given array and
	 * return CAP_IOCTLS_ALL (actually, INT_MAX).
	 */

	fdep = &fdp->fd_ofiles[fd];
	cmds = fdep->fde_ioctls;
	if (cmds32 != NULL && cmds != NULL) {
		for (i = 0; i < MIN(fdep->fde_nioctls, maxcmds); i++) {
			error = suword32(&cmds32[i], cmds[i]);
			if (error != 0)
				goto out;
		}
	}
	if (fdep->fde_nioctls == -1)
		td->td_retval[0] = INT_MAX;
	else
		td->td_retval[0] = fdep->fde_nioctls;

	error = 0;
out:
	FILEDESC_SUNLOCK(fdp);
	return (error);
}

#else /* !CAPABILITIES */

int
freebsd32_cap_ioctls_limit(struct thread *td,
    struct freebsd32_cap_ioctls_limit_args *uap)
{

	return (ENOSYS);
}

int
freebsd32_cap_ioctls_get(struct thread *td,
    struct freebsd32_cap_ioctls_get_args *uap)
{

	return (ENOSYS);
}

#endif /* CAPABILITIES */
