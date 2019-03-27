/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008-2011 Robert N. M. Watson
 * Copyright (c) 2010-2011 Jonathan Anderson
 * Copyright (c) 2012 FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed at the University of Cambridge Computer
 * Laboratory with support from a grant from Google, Inc.
 *
 * Portions of this software were developed by Pawel Jakub Dawidek under
 * sponsorship from the FreeBSD Foundation.
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

/*
 * FreeBSD kernel capability facility.
 *
 * Two kernel features are implemented here: capability mode, a sandboxed mode
 * of execution for processes, and capabilities, a refinement on file
 * descriptors that allows fine-grained control over operations on the file
 * descriptor.  Collectively, these allow processes to run in the style of a
 * historic "capability system" in which they can use only resources
 * explicitly delegated to them.  This model is enforced by restricting access
 * to global namespaces in capability mode.
 *
 * Capabilities wrap other file descriptor types, binding them to a constant
 * rights mask set when the capability is created.  New capabilities may be
 * derived from existing capabilities, but only if they have the same or a
 * strict subset of the rights on the original capability.
 *
 * System calls permitted in capability mode are defined in capabilities.conf;
 * calls must be carefully audited for safety to ensure that they don't allow
 * escape from a sandbox.  Some calls permit only a subset of operations in
 * capability mode -- for example, shm_open(2) is limited to creating
 * anonymous, rather than named, POSIX shared memory objects.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_capsicum.h"
#include "opt_ktrace.h"

#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/syscallsubr.h>
#include <sys/sysproto.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/ucred.h>
#include <sys/uio.h>
#include <sys/ktrace.h>

#include <security/audit/audit.h>

#include <vm/uma.h>
#include <vm/vm.h>

bool __read_frequently trap_enotcap;
SYSCTL_BOOL(_kern, OID_AUTO, trap_enotcap, CTLFLAG_RWTUN, &trap_enotcap, 0,
    "Deliver SIGTRAP on ENOTCAPABLE");

#ifdef CAPABILITY_MODE

#define        IOCTLS_MAX_COUNT        256     /* XXX: Is 256 sane? */

FEATURE(security_capability_mode, "Capsicum Capability Mode");

/*
 * System call to enter capability mode for the process.
 */
int
sys_cap_enter(struct thread *td, struct cap_enter_args *uap)
{
	struct ucred *newcred, *oldcred;
	struct proc *p;

	if (IN_CAPABILITY_MODE(td))
		return (0);

	newcred = crget();
	p = td->td_proc;
	PROC_LOCK(p);
	oldcred = crcopysafe(p, newcred);
	newcred->cr_flags |= CRED_FLAG_CAPMODE;
	proc_set_cred(p, newcred);
	PROC_UNLOCK(p);
	crfree(oldcred);
	return (0);
}

/*
 * System call to query whether the process is in capability mode.
 */
int
sys_cap_getmode(struct thread *td, struct cap_getmode_args *uap)
{
	u_int i;

	i = IN_CAPABILITY_MODE(td) ? 1 : 0;
	return (copyout(&i, uap->modep, sizeof(i)));
}

#else /* !CAPABILITY_MODE */

int
sys_cap_enter(struct thread *td, struct cap_enter_args *uap)
{

	return (ENOSYS);
}

int
sys_cap_getmode(struct thread *td, struct cap_getmode_args *uap)
{

	return (ENOSYS);
}

#endif /* CAPABILITY_MODE */

#ifdef CAPABILITIES

FEATURE(security_capabilities, "Capsicum Capabilities");

MALLOC_DECLARE(M_FILECAPS);

static inline int
_cap_check(const cap_rights_t *havep, const cap_rights_t *needp,
    enum ktr_cap_fail_type type)
{

	if (!cap_rights_contains(havep, needp)) {
#ifdef KTRACE
		if (KTRPOINT(curthread, KTR_CAPFAIL))
			ktrcapfail(type, needp, havep);
#endif
		return (ENOTCAPABLE);
	}
	return (0);
}

/*
 * Test whether a capability grants the requested rights.
 */
int
cap_check(const cap_rights_t *havep, const cap_rights_t *needp)
{

	return (_cap_check(havep, needp, CAPFAIL_NOTCAPABLE));
}

/*
 * Convert capability rights into VM access flags.
 */
u_char
cap_rights_to_vmprot(const cap_rights_t *havep)
{
	u_char maxprot;

	maxprot = VM_PROT_NONE;
	if (cap_rights_is_set(havep, CAP_MMAP_R))
		maxprot |= VM_PROT_READ;
	if (cap_rights_is_set(havep, CAP_MMAP_W))
		maxprot |= VM_PROT_WRITE;
	if (cap_rights_is_set(havep, CAP_MMAP_X))
		maxprot |= VM_PROT_EXECUTE;

	return (maxprot);
}

/*
 * Extract rights from a capability for monitoring purposes -- not for use in
 * any other way, as we want to keep all capability permission evaluation in
 * this one file.
 */

const cap_rights_t *
cap_rights_fde(const struct filedescent *fdep)
{

	return (cap_rights_fde_inline(fdep));
}

const cap_rights_t *
cap_rights(struct filedesc *fdp, int fd)
{

	return (cap_rights_fde(&fdp->fd_ofiles[fd]));
}

int
kern_cap_rights_limit(struct thread *td, int fd, cap_rights_t *rights)
{
	struct filedesc *fdp;
	struct filedescent *fdep;
	int error;

	fdp = td->td_proc->p_fd;
	FILEDESC_XLOCK(fdp);
	fdep = fdeget_locked(fdp, fd);
	if (fdep == NULL) {
		FILEDESC_XUNLOCK(fdp);
		return (EBADF);
	}
	error = _cap_check(cap_rights(fdp, fd), rights, CAPFAIL_INCREASE);
	if (error == 0) {
		fdep->fde_rights = *rights;
		if (!cap_rights_is_set(rights, CAP_IOCTL)) {
			free(fdep->fde_ioctls, M_FILECAPS);
			fdep->fde_ioctls = NULL;
			fdep->fde_nioctls = 0;
		}
		if (!cap_rights_is_set(rights, CAP_FCNTL))
			fdep->fde_fcntls = 0;
	}
	FILEDESC_XUNLOCK(fdp);
	return (error);
}

/*
 * System call to limit rights of the given capability.
 */
int
sys_cap_rights_limit(struct thread *td, struct cap_rights_limit_args *uap)
{
	cap_rights_t rights;
	int error, version;

	cap_rights_init(&rights);

	error = copyin(uap->rightsp, &rights, sizeof(rights.cr_rights[0]));
	if (error != 0)
		return (error);
	version = CAPVER(&rights);
	if (version != CAP_RIGHTS_VERSION_00)
		return (EINVAL);

	error = copyin(uap->rightsp, &rights,
	    sizeof(rights.cr_rights[0]) * CAPARSIZE(&rights));
	if (error != 0)
		return (error);
	/* Check for race. */
	if (CAPVER(&rights) != version)
		return (EINVAL);

	if (!cap_rights_is_valid(&rights))
		return (EINVAL);

	if (version != CAP_RIGHTS_VERSION) {
		rights.cr_rights[0] &= ~(0x3ULL << 62);
		rights.cr_rights[0] |= ((uint64_t)CAP_RIGHTS_VERSION << 62);
	}
#ifdef KTRACE
	if (KTRPOINT(td, KTR_STRUCT))
		ktrcaprights(&rights);
#endif

	AUDIT_ARG_FD(uap->fd);
	AUDIT_ARG_RIGHTS(&rights);
	return (kern_cap_rights_limit(td, uap->fd, &rights));
}

/*
 * System call to query the rights mask associated with a capability.
 */
int
sys___cap_rights_get(struct thread *td, struct __cap_rights_get_args *uap)
{
	struct filedesc *fdp;
	cap_rights_t rights;
	int error, fd, i, n;

	if (uap->version != CAP_RIGHTS_VERSION_00)
		return (EINVAL);

	fd = uap->fd;

	AUDIT_ARG_FD(fd);

	fdp = td->td_proc->p_fd;
	FILEDESC_SLOCK(fdp);
	if (fget_locked(fdp, fd) == NULL) {
		FILEDESC_SUNLOCK(fdp);
		return (EBADF);
	}
	rights = *cap_rights(fdp, fd);
	FILEDESC_SUNLOCK(fdp);
	n = uap->version + 2;
	if (uap->version != CAPVER(&rights)) {
		/*
		 * For older versions we need to check if the descriptor
		 * doesn't contain rights not understood by the caller.
		 * If it does, we have to return an error.
		 */
		for (i = n; i < CAPARSIZE(&rights); i++) {
			if ((rights.cr_rights[i] & ~(0x7FULL << 57)) != 0)
				return (EINVAL);
		}
	}
	error = copyout(&rights, uap->rightsp, sizeof(rights.cr_rights[0]) * n);
#ifdef KTRACE
	if (error == 0 && KTRPOINT(td, KTR_STRUCT))
		ktrcaprights(&rights);
#endif
	return (error);
}

/*
 * Test whether a capability grants the given ioctl command.
 * If descriptor doesn't have CAP_IOCTL, then ioctls list is empty and
 * ENOTCAPABLE will be returned.
 */
int
cap_ioctl_check(struct filedesc *fdp, int fd, u_long cmd)
{
	struct filedescent *fdep;
	u_long *cmds;
	ssize_t ncmds;
	long i;

	KASSERT(fd >= 0 && fd < fdp->fd_nfiles,
		("%s: invalid fd=%d", __func__, fd));

	fdep = fdeget_locked(fdp, fd);
	KASSERT(fdep != NULL,
	    ("%s: invalid fd=%d", __func__, fd));

	ncmds = fdep->fde_nioctls;
	if (ncmds == -1)
		return (0);

	cmds = fdep->fde_ioctls;
	for (i = 0; i < ncmds; i++) {
		if (cmds[i] == cmd)
			return (0);
	}

	return (ENOTCAPABLE);
}

/*
 * Check if the current ioctls list can be replaced by the new one.
 */
static int
cap_ioctl_limit_check(struct filedescent *fdep, const u_long *cmds,
    size_t ncmds)
{
	u_long *ocmds;
	ssize_t oncmds;
	u_long i;
	long j;

	oncmds = fdep->fde_nioctls;
	if (oncmds == -1)
		return (0);
	if (oncmds < (ssize_t)ncmds)
		return (ENOTCAPABLE);

	ocmds = fdep->fde_ioctls;
	for (i = 0; i < ncmds; i++) {
		for (j = 0; j < oncmds; j++) {
			if (cmds[i] == ocmds[j])
				break;
		}
		if (j == oncmds)
			return (ENOTCAPABLE);
	}

	return (0);
}

int
kern_cap_ioctls_limit(struct thread *td, int fd, u_long *cmds, size_t ncmds)
{
	struct filedesc *fdp;
	struct filedescent *fdep;
	u_long *ocmds;
	int error;

	AUDIT_ARG_FD(fd);

	if (ncmds > IOCTLS_MAX_COUNT) {
		error = EINVAL;
		goto out_free;
	}

	fdp = td->td_proc->p_fd;
	FILEDESC_XLOCK(fdp);

	fdep = fdeget_locked(fdp, fd);
	if (fdep == NULL) {
		error = EBADF;
		goto out;
	}

	error = cap_ioctl_limit_check(fdep, cmds, ncmds);
	if (error != 0)
		goto out;

	ocmds = fdep->fde_ioctls;
	fdep->fde_ioctls = cmds;
	fdep->fde_nioctls = ncmds;

	cmds = ocmds;
	error = 0;
out:
	FILEDESC_XUNLOCK(fdp);
out_free:
	free(cmds, M_FILECAPS);
	return (error);
}

int
sys_cap_ioctls_limit(struct thread *td, struct cap_ioctls_limit_args *uap)
{
	u_long *cmds;
	size_t ncmds;
	int error;

	ncmds = uap->ncmds;

	if (ncmds > IOCTLS_MAX_COUNT)
		return (EINVAL);

	if (ncmds == 0) {
		cmds = NULL;
	} else {
		cmds = malloc(sizeof(cmds[0]) * ncmds, M_FILECAPS, M_WAITOK);
		error = copyin(uap->cmds, cmds, sizeof(cmds[0]) * ncmds);
		if (error != 0) {
			free(cmds, M_FILECAPS);
			return (error);
		}
	}

	return (kern_cap_ioctls_limit(td, uap->fd, cmds, ncmds));
}

int
sys_cap_ioctls_get(struct thread *td, struct cap_ioctls_get_args *uap)
{
	struct filedesc *fdp;
	struct filedescent *fdep;
	u_long *cmdsp, *dstcmds;
	size_t maxcmds, ncmds;
	int16_t count;
	int error, fd;

	fd = uap->fd;
	dstcmds = uap->cmds;
	maxcmds = uap->maxcmds;

	AUDIT_ARG_FD(fd);

	fdp = td->td_proc->p_fd;

	cmdsp = NULL;
	if (dstcmds != NULL) {
		cmdsp = malloc(sizeof(cmdsp[0]) * IOCTLS_MAX_COUNT, M_FILECAPS,
		    M_WAITOK | M_ZERO);
	}

	FILEDESC_SLOCK(fdp);
	fdep = fdeget_locked(fdp, fd);
	if (fdep == NULL) {
		error = EBADF;
		FILEDESC_SUNLOCK(fdp);
		goto out;
	}
	count = fdep->fde_nioctls;
	if (count != -1 && cmdsp != NULL) {
		ncmds = MIN(count, maxcmds);
		memcpy(cmdsp, fdep->fde_ioctls, sizeof(cmdsp[0]) * ncmds);
	}
	FILEDESC_SUNLOCK(fdp);

	/*
	 * If all ioctls are allowed (fde_nioctls == -1 && fde_ioctls == NULL)
	 * the only sane thing we can do is to not populate the given array and
	 * return CAP_IOCTLS_ALL.
	 */
	if (count != -1) {
		if (cmdsp != NULL) {
			error = copyout(cmdsp, dstcmds,
			    sizeof(cmdsp[0]) * ncmds);
			if (error != 0)
				goto out;
		}
		td->td_retval[0] = count;
	} else {
		td->td_retval[0] = CAP_IOCTLS_ALL;
	}

	error = 0;
out:
	free(cmdsp, M_FILECAPS);
	return (error);
}

/*
 * Test whether a capability grants the given fcntl command.
 */
int
cap_fcntl_check_fde(struct filedescent *fdep, int cmd)
{
	uint32_t fcntlcap;

	fcntlcap = (1 << cmd);
	KASSERT((CAP_FCNTL_ALL & fcntlcap) != 0,
	    ("Unsupported fcntl=%d.", cmd));

	if ((fdep->fde_fcntls & fcntlcap) != 0)
		return (0);

	return (ENOTCAPABLE);
}

int
cap_fcntl_check(struct filedesc *fdp, int fd, int cmd)
{

	KASSERT(fd >= 0 && fd < fdp->fd_nfiles,
	    ("%s: invalid fd=%d", __func__, fd));

	return (cap_fcntl_check_fde(&fdp->fd_ofiles[fd], cmd));
}

int
sys_cap_fcntls_limit(struct thread *td, struct cap_fcntls_limit_args *uap)
{
	struct filedesc *fdp;
	struct filedescent *fdep;
	uint32_t fcntlrights;
	int fd;

	fd = uap->fd;
	fcntlrights = uap->fcntlrights;

	AUDIT_ARG_FD(fd);
	AUDIT_ARG_FCNTL_RIGHTS(fcntlrights);

	if ((fcntlrights & ~CAP_FCNTL_ALL) != 0)
		return (EINVAL);

	fdp = td->td_proc->p_fd;
	FILEDESC_XLOCK(fdp);

	fdep = fdeget_locked(fdp, fd);
	if (fdep == NULL) {
		FILEDESC_XUNLOCK(fdp);
		return (EBADF);
	}

	if ((fcntlrights & ~fdep->fde_fcntls) != 0) {
		FILEDESC_XUNLOCK(fdp);
		return (ENOTCAPABLE);
	}

	fdep->fde_fcntls = fcntlrights;
	FILEDESC_XUNLOCK(fdp);

	return (0);
}

int
sys_cap_fcntls_get(struct thread *td, struct cap_fcntls_get_args *uap)
{
	struct filedesc *fdp;
	struct filedescent *fdep;
	uint32_t rights;
	int fd;

	fd = uap->fd;

	AUDIT_ARG_FD(fd);

	fdp = td->td_proc->p_fd;
	FILEDESC_SLOCK(fdp);
	fdep = fdeget_locked(fdp, fd);
	if (fdep == NULL) {
		FILEDESC_SUNLOCK(fdp);
		return (EBADF);
	}
	rights = fdep->fde_fcntls;
	FILEDESC_SUNLOCK(fdp);

	return (copyout(&rights, uap->fcntlrightsp, sizeof(rights)));
}

#else /* !CAPABILITIES */

/*
 * Stub Capability functions for when options CAPABILITIES isn't compiled
 * into the kernel.
 */

int
sys_cap_rights_limit(struct thread *td, struct cap_rights_limit_args *uap)
{

	return (ENOSYS);
}

int
sys___cap_rights_get(struct thread *td, struct __cap_rights_get_args *uap)
{

	return (ENOSYS);
}

int
sys_cap_ioctls_limit(struct thread *td, struct cap_ioctls_limit_args *uap)
{

	return (ENOSYS);
}

int
sys_cap_ioctls_get(struct thread *td, struct cap_ioctls_get_args *uap)
{

	return (ENOSYS);
}

int
sys_cap_fcntls_limit(struct thread *td, struct cap_fcntls_limit_args *uap)
{

	return (ENOSYS);
}

int
sys_cap_fcntls_get(struct thread *td, struct cap_fcntls_get_args *uap)
{

	return (ENOSYS);
}

#endif /* CAPABILITIES */
