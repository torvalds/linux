/*-
 * SPDX-License-Identifier: BSD-4-Clause AND BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1994 Adam Glass and Charles Hannum.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Adam Glass and Charles
 *	Hannum.
 * 4. The names of the authors may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $NetBSD: sysv_shm.c,v 1.39 1997/10/07 10:02:03 drochner Exp $
 */
/*-
 * Copyright (c) 2003-2005 McAfee, Inc.
 * Copyright (c) 2016-2017 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project in part by McAfee
 * Research, the Security Research Division of McAfee, Inc under DARPA/SPAWAR
 * contract N66001-01-C-8035 ("CBOSS"), as part of the DARPA CHATS research
 * program.
 *
 * Portions of this software were developed by BAE Systems, the University of
 * Cambridge Computer Laboratory, and Memorial University under DARPA/AFRL
 * contract FA8650-15-C-7558 ("CADETS"), as part of the DARPA Transparent
 * Computing (TC) research program.
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

#include "opt_sysvipc.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/sysctl.h>
#include <sys/shm.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/mman.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/racct.h>
#include <sys/resourcevar.h>
#include <sys/rwlock.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/syscallsubr.h>
#include <sys/sysent.h>
#include <sys/sysproto.h>
#include <sys/jail.h>

#include <security/audit/audit.h>
#include <security/mac/mac_framework.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <vm/vm_object.h>
#include <vm/vm_map.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>

FEATURE(sysv_shm, "System V shared memory segments support");

static MALLOC_DEFINE(M_SHM, "shm", "SVID compatible shared memory segments");

static int shmget_allocate_segment(struct thread *td,
    struct shmget_args *uap, int mode);
static int shmget_existing(struct thread *td, struct shmget_args *uap,
    int mode, int segnum);

#define	SHMSEG_FREE     	0x0200
#define	SHMSEG_REMOVED  	0x0400
#define	SHMSEG_ALLOCATED	0x0800

static int shm_last_free, shm_nused, shmalloced;
vm_size_t shm_committed;
static struct shmid_kernel *shmsegs;
static unsigned shm_prison_slot;

struct shmmap_state {
	vm_offset_t va;
	int shmid;
};

static void shm_deallocate_segment(struct shmid_kernel *);
static int shm_find_segment_by_key(struct prison *, key_t);
static struct shmid_kernel *shm_find_segment(struct prison *, int, bool);
static int shm_delete_mapping(struct vmspace *vm, struct shmmap_state *);
static void shmrealloc(void);
static int shminit(void);
static int sysvshm_modload(struct module *, int, void *);
static int shmunload(void);
static void shmexit_myhook(struct vmspace *vm);
static void shmfork_myhook(struct proc *p1, struct proc *p2);
static int sysctl_shmsegs(SYSCTL_HANDLER_ARGS);
static void shm_remove(struct shmid_kernel *, int);
static struct prison *shm_find_prison(struct ucred *);
static int shm_prison_cansee(struct prison *, struct shmid_kernel *);
static int shm_prison_check(void *, void *);
static int shm_prison_set(void *, void *);
static int shm_prison_get(void *, void *);
static int shm_prison_remove(void *, void *);
static void shm_prison_cleanup(struct prison *);

/*
 * Tuneable values.
 */
#ifndef SHMMAXPGS
#define	SHMMAXPGS	131072	/* Note: sysv shared memory is swap backed. */
#endif
#ifndef SHMMAX
#define	SHMMAX	(SHMMAXPGS*PAGE_SIZE)
#endif
#ifndef SHMMIN
#define	SHMMIN	1
#endif
#ifndef SHMMNI
#define	SHMMNI	192
#endif
#ifndef SHMSEG
#define	SHMSEG	128
#endif
#ifndef SHMALL
#define	SHMALL	(SHMMAXPGS)
#endif

struct	shminfo shminfo = {
	.shmmax = SHMMAX,
	.shmmin = SHMMIN,
	.shmmni = SHMMNI,
	.shmseg = SHMSEG,
	.shmall = SHMALL
};

static int shm_use_phys;
static int shm_allow_removed = 1;

SYSCTL_ULONG(_kern_ipc, OID_AUTO, shmmax, CTLFLAG_RWTUN, &shminfo.shmmax, 0,
    "Maximum shared memory segment size");
SYSCTL_ULONG(_kern_ipc, OID_AUTO, shmmin, CTLFLAG_RWTUN, &shminfo.shmmin, 0,
    "Minimum shared memory segment size");
SYSCTL_ULONG(_kern_ipc, OID_AUTO, shmmni, CTLFLAG_RDTUN, &shminfo.shmmni, 0,
    "Number of shared memory identifiers");
SYSCTL_ULONG(_kern_ipc, OID_AUTO, shmseg, CTLFLAG_RDTUN, &shminfo.shmseg, 0,
    "Number of segments per process");
SYSCTL_ULONG(_kern_ipc, OID_AUTO, shmall, CTLFLAG_RWTUN, &shminfo.shmall, 0,
    "Maximum number of pages available for shared memory");
SYSCTL_INT(_kern_ipc, OID_AUTO, shm_use_phys, CTLFLAG_RWTUN,
    &shm_use_phys, 0, "Enable/Disable locking of shared memory pages in core");
SYSCTL_INT(_kern_ipc, OID_AUTO, shm_allow_removed, CTLFLAG_RWTUN,
    &shm_allow_removed, 0,
    "Enable/Disable attachment to attached segments marked for removal");
SYSCTL_PROC(_kern_ipc, OID_AUTO, shmsegs, CTLTYPE_OPAQUE | CTLFLAG_RD |
    CTLFLAG_MPSAFE, NULL, 0, sysctl_shmsegs, "",
    "Array of struct shmid_kernel for each potential shared memory segment");

static struct sx sysvshmsx;
#define	SYSVSHM_LOCK()		sx_xlock(&sysvshmsx)
#define	SYSVSHM_UNLOCK()	sx_xunlock(&sysvshmsx)
#define	SYSVSHM_ASSERT_LOCKED()	sx_assert(&sysvshmsx, SA_XLOCKED)

static int
shm_find_segment_by_key(struct prison *pr, key_t key)
{
	int i;

	for (i = 0; i < shmalloced; i++)
		if ((shmsegs[i].u.shm_perm.mode & SHMSEG_ALLOCATED) &&
		    shmsegs[i].cred != NULL &&
		    shmsegs[i].cred->cr_prison == pr &&
		    shmsegs[i].u.shm_perm.key == key)
			return (i);
	return (-1);
}

/*
 * Finds segment either by shmid if is_shmid is true, or by segnum if
 * is_shmid is false.
 */
static struct shmid_kernel *
shm_find_segment(struct prison *rpr, int arg, bool is_shmid)
{
	struct shmid_kernel *shmseg;
	int segnum;

	segnum = is_shmid ? IPCID_TO_IX(arg) : arg;
	if (segnum < 0 || segnum >= shmalloced)
		return (NULL);
	shmseg = &shmsegs[segnum];
	if ((shmseg->u.shm_perm.mode & SHMSEG_ALLOCATED) == 0 ||
	    (!shm_allow_removed &&
	    (shmseg->u.shm_perm.mode & SHMSEG_REMOVED) != 0) ||
	    (is_shmid && shmseg->u.shm_perm.seq != IPCID_TO_SEQ(arg)) ||
	    shm_prison_cansee(rpr, shmseg) != 0)
		return (NULL);
	return (shmseg);
}

static void
shm_deallocate_segment(struct shmid_kernel *shmseg)
{
	vm_size_t size;

	SYSVSHM_ASSERT_LOCKED();

	vm_object_deallocate(shmseg->object);
	shmseg->object = NULL;
	size = round_page(shmseg->u.shm_segsz);
	shm_committed -= btoc(size);
	shm_nused--;
	shmseg->u.shm_perm.mode = SHMSEG_FREE;
#ifdef MAC
	mac_sysvshm_cleanup(shmseg);
#endif
	racct_sub_cred(shmseg->cred, RACCT_NSHM, 1);
	racct_sub_cred(shmseg->cred, RACCT_SHMSIZE, size);
	crfree(shmseg->cred);
	shmseg->cred = NULL;
}

static int
shm_delete_mapping(struct vmspace *vm, struct shmmap_state *shmmap_s)
{
	struct shmid_kernel *shmseg;
	int segnum, result;
	vm_size_t size;

	SYSVSHM_ASSERT_LOCKED();
	segnum = IPCID_TO_IX(shmmap_s->shmid);
	KASSERT(segnum >= 0 && segnum < shmalloced,
	    ("segnum %d shmalloced %d", segnum, shmalloced));

	shmseg = &shmsegs[segnum];
	size = round_page(shmseg->u.shm_segsz);
	result = vm_map_remove(&vm->vm_map, shmmap_s->va, shmmap_s->va + size);
	if (result != KERN_SUCCESS)
		return (EINVAL);
	shmmap_s->shmid = -1;
	shmseg->u.shm_dtime = time_second;
	if (--shmseg->u.shm_nattch == 0 &&
	    (shmseg->u.shm_perm.mode & SHMSEG_REMOVED)) {
		shm_deallocate_segment(shmseg);
		shm_last_free = segnum;
	}
	return (0);
}

static void
shm_remove(struct shmid_kernel *shmseg, int segnum)
{

	shmseg->u.shm_perm.key = IPC_PRIVATE;
	shmseg->u.shm_perm.mode |= SHMSEG_REMOVED;
	if (shmseg->u.shm_nattch == 0) {
		shm_deallocate_segment(shmseg);
		shm_last_free = segnum;
	}
}

static struct prison *
shm_find_prison(struct ucred *cred)
{
	struct prison *pr, *rpr;

	pr = cred->cr_prison;
	prison_lock(pr);
	rpr = osd_jail_get(pr, shm_prison_slot);
	prison_unlock(pr);
	return rpr;
}

static int
shm_prison_cansee(struct prison *rpr, struct shmid_kernel *shmseg)
{

	if (shmseg->cred == NULL ||
	    !(rpr == shmseg->cred->cr_prison ||
	      prison_ischild(rpr, shmseg->cred->cr_prison)))
		return (EINVAL);
	return (0);
}

static int
kern_shmdt_locked(struct thread *td, const void *shmaddr)
{
	struct proc *p = td->td_proc;
	struct shmmap_state *shmmap_s;
#ifdef MAC
	int error;
#endif
	int i;

	SYSVSHM_ASSERT_LOCKED();
	if (shm_find_prison(td->td_ucred) == NULL)
		return (ENOSYS);
	shmmap_s = p->p_vmspace->vm_shm;
 	if (shmmap_s == NULL)
		return (EINVAL);
	AUDIT_ARG_SVIPC_ID(shmmap_s->shmid);
	for (i = 0; i < shminfo.shmseg; i++, shmmap_s++) {
		if (shmmap_s->shmid != -1 &&
		    shmmap_s->va == (vm_offset_t)shmaddr) {
			break;
		}
	}
	if (i == shminfo.shmseg)
		return (EINVAL);
#ifdef MAC
	error = mac_sysvshm_check_shmdt(td->td_ucred,
	    &shmsegs[IPCID_TO_IX(shmmap_s->shmid)]);
	if (error != 0)
		return (error);
#endif
	return (shm_delete_mapping(p->p_vmspace, shmmap_s));
}

#ifndef _SYS_SYSPROTO_H_
struct shmdt_args {
	const void *shmaddr;
};
#endif
int
sys_shmdt(struct thread *td, struct shmdt_args *uap)
{
	int error;

	SYSVSHM_LOCK();
	error = kern_shmdt_locked(td, uap->shmaddr);
	SYSVSHM_UNLOCK();
	return (error);
}

static int
kern_shmat_locked(struct thread *td, int shmid, const void *shmaddr,
    int shmflg)
{
	struct prison *rpr;
	struct proc *p = td->td_proc;
	struct shmid_kernel *shmseg;
	struct shmmap_state *shmmap_s;
	vm_offset_t attach_va;
	vm_prot_t prot;
	vm_size_t size;
	int cow, error, find_space, i, rv;

	AUDIT_ARG_SVIPC_ID(shmid);
	AUDIT_ARG_VALUE(shmflg);

	SYSVSHM_ASSERT_LOCKED();
	rpr = shm_find_prison(td->td_ucred);
	if (rpr == NULL)
		return (ENOSYS);
	shmmap_s = p->p_vmspace->vm_shm;
	if (shmmap_s == NULL) {
		shmmap_s = malloc(shminfo.shmseg * sizeof(struct shmmap_state),
		    M_SHM, M_WAITOK);
		for (i = 0; i < shminfo.shmseg; i++)
			shmmap_s[i].shmid = -1;
		KASSERT(p->p_vmspace->vm_shm == NULL, ("raced"));
		p->p_vmspace->vm_shm = shmmap_s;
	}
	shmseg = shm_find_segment(rpr, shmid, true);
	if (shmseg == NULL)
		return (EINVAL);
	error = ipcperm(td, &shmseg->u.shm_perm,
	    (shmflg & SHM_RDONLY) ? IPC_R : IPC_R|IPC_W);
	if (error != 0)
		return (error);
#ifdef MAC
	error = mac_sysvshm_check_shmat(td->td_ucred, shmseg, shmflg);
	if (error != 0)
		return (error);
#endif
	for (i = 0; i < shminfo.shmseg; i++) {
		if (shmmap_s->shmid == -1)
			break;
		shmmap_s++;
	}
	if (i >= shminfo.shmseg)
		return (EMFILE);
	size = round_page(shmseg->u.shm_segsz);
	prot = VM_PROT_READ;
	cow = MAP_INHERIT_SHARE | MAP_PREFAULT_PARTIAL;
	if ((shmflg & SHM_RDONLY) == 0)
		prot |= VM_PROT_WRITE;
	if (shmaddr != NULL) {
		if ((shmflg & SHM_RND) != 0)
			attach_va = rounddown2((vm_offset_t)shmaddr, SHMLBA);
		else if (((vm_offset_t)shmaddr & (SHMLBA-1)) == 0)
			attach_va = (vm_offset_t)shmaddr;
		else
			return (EINVAL);
		if ((shmflg & SHM_REMAP) != 0)
			cow |= MAP_REMAP;
		find_space = VMFS_NO_SPACE;
	} else {
		/*
		 * This is just a hint to vm_map_find() about where to
		 * put it.
		 */
		attach_va = round_page((vm_offset_t)p->p_vmspace->vm_daddr +
		    lim_max(td, RLIMIT_DATA));
		find_space = VMFS_OPTIMAL_SPACE;
	}

	vm_object_reference(shmseg->object);
	rv = vm_map_find(&p->p_vmspace->vm_map, shmseg->object, 0, &attach_va,
	    size, 0, find_space, prot, prot, cow);
	if (rv != KERN_SUCCESS) {
		vm_object_deallocate(shmseg->object);
		return (ENOMEM);
	}

	shmmap_s->va = attach_va;
	shmmap_s->shmid = shmid;
	shmseg->u.shm_lpid = p->p_pid;
	shmseg->u.shm_atime = time_second;
	shmseg->u.shm_nattch++;
	td->td_retval[0] = attach_va;
	return (error);
}

int
kern_shmat(struct thread *td, int shmid, const void *shmaddr, int shmflg)
{
	int error;

	SYSVSHM_LOCK();
	error = kern_shmat_locked(td, shmid, shmaddr, shmflg);
	SYSVSHM_UNLOCK();
	return (error);
}

#ifndef _SYS_SYSPROTO_H_
struct shmat_args {
	int shmid;
	const void *shmaddr;
	int shmflg;
};
#endif
int
sys_shmat(struct thread *td, struct shmat_args *uap)
{

	return (kern_shmat(td, uap->shmid, uap->shmaddr, uap->shmflg));
}

static int
kern_shmctl_locked(struct thread *td, int shmid, int cmd, void *buf,
    size_t *bufsz)
{
	struct prison *rpr;
	struct shmid_kernel *shmseg;
	struct shmid_ds *shmidp;
	struct shm_info shm_info;
	int error;

	SYSVSHM_ASSERT_LOCKED();

	rpr = shm_find_prison(td->td_ucred);
	if (rpr == NULL)
		return (ENOSYS);

	AUDIT_ARG_SVIPC_ID(shmid);
	AUDIT_ARG_SVIPC_CMD(cmd);

	switch (cmd) {
	/*
	 * It is possible that kern_shmctl is being called from the Linux ABI
	 * layer, in which case, we will need to implement IPC_INFO.  It should
	 * be noted that other shmctl calls will be funneled through here for
	 * Linix binaries as well.
	 *
	 * NB: The Linux ABI layer will convert this data to structure(s) more
	 * consistent with the Linux ABI.
	 */
	case IPC_INFO:
		memcpy(buf, &shminfo, sizeof(shminfo));
		if (bufsz)
			*bufsz = sizeof(shminfo);
		td->td_retval[0] = shmalloced;
		return (0);
	case SHM_INFO: {
		shm_info.used_ids = shm_nused;
		shm_info.shm_rss = 0;	/*XXX where to get from ? */
		shm_info.shm_tot = 0;	/*XXX where to get from ? */
		shm_info.shm_swp = 0;	/*XXX where to get from ? */
		shm_info.swap_attempts = 0;	/*XXX where to get from ? */
		shm_info.swap_successes = 0;	/*XXX where to get from ? */
		memcpy(buf, &shm_info, sizeof(shm_info));
		if (bufsz != NULL)
			*bufsz = sizeof(shm_info);
		td->td_retval[0] = shmalloced;
		return (0);
	}
	}
	shmseg = shm_find_segment(rpr, shmid, cmd != SHM_STAT);
	if (shmseg == NULL)
		return (EINVAL);
#ifdef MAC
	error = mac_sysvshm_check_shmctl(td->td_ucred, shmseg, cmd);
	if (error != 0)
		return (error);
#endif
	switch (cmd) {
	case SHM_STAT:
	case IPC_STAT:
		shmidp = (struct shmid_ds *)buf;
		error = ipcperm(td, &shmseg->u.shm_perm, IPC_R);
		if (error != 0)
			return (error);
		memcpy(shmidp, &shmseg->u, sizeof(struct shmid_ds));
		if (td->td_ucred->cr_prison != shmseg->cred->cr_prison)
			shmidp->shm_perm.key = IPC_PRIVATE;
		if (bufsz != NULL)
			*bufsz = sizeof(struct shmid_ds);
		if (cmd == SHM_STAT) {
			td->td_retval[0] = IXSEQ_TO_IPCID(shmid,
			    shmseg->u.shm_perm);
		}
		break;
	case IPC_SET:
		shmidp = (struct shmid_ds *)buf;
		AUDIT_ARG_SVIPC_PERM(&shmidp->shm_perm);
		error = ipcperm(td, &shmseg->u.shm_perm, IPC_M);
		if (error != 0)
			return (error);
		shmseg->u.shm_perm.uid = shmidp->shm_perm.uid;
		shmseg->u.shm_perm.gid = shmidp->shm_perm.gid;
		shmseg->u.shm_perm.mode =
		    (shmseg->u.shm_perm.mode & ~ACCESSPERMS) |
		    (shmidp->shm_perm.mode & ACCESSPERMS);
		shmseg->u.shm_ctime = time_second;
		break;
	case IPC_RMID:
		error = ipcperm(td, &shmseg->u.shm_perm, IPC_M);
		if (error != 0)
			return (error);
		shm_remove(shmseg, IPCID_TO_IX(shmid));
		break;
#if 0
	case SHM_LOCK:
	case SHM_UNLOCK:
#endif
	default:
		error = EINVAL;
		break;
	}
	return (error);
}

int
kern_shmctl(struct thread *td, int shmid, int cmd, void *buf, size_t *bufsz)
{
	int error;

	SYSVSHM_LOCK();
	error = kern_shmctl_locked(td, shmid, cmd, buf, bufsz);
	SYSVSHM_UNLOCK();
	return (error);
}


#ifndef _SYS_SYSPROTO_H_
struct shmctl_args {
	int shmid;
	int cmd;
	struct shmid_ds *buf;
};
#endif
int
sys_shmctl(struct thread *td, struct shmctl_args *uap)
{
	int error;
	struct shmid_ds buf;
	size_t bufsz;

	/*
	 * The only reason IPC_INFO, SHM_INFO, SHM_STAT exists is to support
	 * Linux binaries.  If we see the call come through the FreeBSD ABI,
	 * return an error back to the user since we do not to support this.
	 */
	if (uap->cmd == IPC_INFO || uap->cmd == SHM_INFO ||
	    uap->cmd == SHM_STAT)
		return (EINVAL);

	/* IPC_SET needs to copyin the buffer before calling kern_shmctl */
	if (uap->cmd == IPC_SET) {
		if ((error = copyin(uap->buf, &buf, sizeof(struct shmid_ds))))
			goto done;
	}

	error = kern_shmctl(td, uap->shmid, uap->cmd, (void *)&buf, &bufsz);
	if (error)
		goto done;

	/* Cases in which we need to copyout */
	switch (uap->cmd) {
	case IPC_STAT:
		error = copyout(&buf, uap->buf, bufsz);
		break;
	}

done:
	if (error) {
		/* Invalidate the return value */
		td->td_retval[0] = -1;
	}
	return (error);
}


static int
shmget_existing(struct thread *td, struct shmget_args *uap, int mode,
    int segnum)
{
	struct shmid_kernel *shmseg;
#ifdef MAC
	int error;
#endif

	SYSVSHM_ASSERT_LOCKED();
	KASSERT(segnum >= 0 && segnum < shmalloced,
	    ("segnum %d shmalloced %d", segnum, shmalloced));
	shmseg = &shmsegs[segnum];
	if ((uap->shmflg & (IPC_CREAT | IPC_EXCL)) == (IPC_CREAT | IPC_EXCL))
		return (EEXIST);
#ifdef MAC
	error = mac_sysvshm_check_shmget(td->td_ucred, shmseg, uap->shmflg);
	if (error != 0)
		return (error);
#endif
	if (uap->size != 0 && uap->size > shmseg->u.shm_segsz)
		return (EINVAL);
	td->td_retval[0] = IXSEQ_TO_IPCID(segnum, shmseg->u.shm_perm);
	return (0);
}

static int
shmget_allocate_segment(struct thread *td, struct shmget_args *uap, int mode)
{
	struct ucred *cred = td->td_ucred;
	struct shmid_kernel *shmseg;
	vm_object_t shm_object;
	int i, segnum;
	size_t size;

	SYSVSHM_ASSERT_LOCKED();

	if (uap->size < shminfo.shmmin || uap->size > shminfo.shmmax)
		return (EINVAL);
	if (shm_nused >= shminfo.shmmni) /* Any shmids left? */
		return (ENOSPC);
	size = round_page(uap->size);
	if (shm_committed + btoc(size) > shminfo.shmall)
		return (ENOMEM);
	if (shm_last_free < 0) {
		shmrealloc();	/* Maybe expand the shmsegs[] array. */
		for (i = 0; i < shmalloced; i++)
			if (shmsegs[i].u.shm_perm.mode & SHMSEG_FREE)
				break;
		if (i == shmalloced)
			return (ENOSPC);
		segnum = i;
	} else  {
		segnum = shm_last_free;
		shm_last_free = -1;
	}
	KASSERT(segnum >= 0 && segnum < shmalloced,
	    ("segnum %d shmalloced %d", segnum, shmalloced));
	shmseg = &shmsegs[segnum];
#ifdef RACCT
	if (racct_enable) {
		PROC_LOCK(td->td_proc);
		if (racct_add(td->td_proc, RACCT_NSHM, 1)) {
			PROC_UNLOCK(td->td_proc);
			return (ENOSPC);
		}
		if (racct_add(td->td_proc, RACCT_SHMSIZE, size)) {
			racct_sub(td->td_proc, RACCT_NSHM, 1);
			PROC_UNLOCK(td->td_proc);
			return (ENOMEM);
		}
		PROC_UNLOCK(td->td_proc);
	}
#endif

	/*
	 * We make sure that we have allocated a pager before we need
	 * to.
	 */
	shm_object = vm_pager_allocate(shm_use_phys ? OBJT_PHYS : OBJT_SWAP,
	    0, size, VM_PROT_DEFAULT, 0, cred);
	if (shm_object == NULL) {
#ifdef RACCT
		if (racct_enable) {
			PROC_LOCK(td->td_proc);
			racct_sub(td->td_proc, RACCT_NSHM, 1);
			racct_sub(td->td_proc, RACCT_SHMSIZE, size);
			PROC_UNLOCK(td->td_proc);
		}
#endif
		return (ENOMEM);
	}
	shm_object->pg_color = 0;
	VM_OBJECT_WLOCK(shm_object);
	vm_object_clear_flag(shm_object, OBJ_ONEMAPPING);
	vm_object_set_flag(shm_object, OBJ_COLORED | OBJ_NOSPLIT);
	VM_OBJECT_WUNLOCK(shm_object);

	shmseg->object = shm_object;
	shmseg->u.shm_perm.cuid = shmseg->u.shm_perm.uid = cred->cr_uid;
	shmseg->u.shm_perm.cgid = shmseg->u.shm_perm.gid = cred->cr_gid;
	shmseg->u.shm_perm.mode = (mode & ACCESSPERMS) | SHMSEG_ALLOCATED;
	shmseg->u.shm_perm.key = uap->key;
	shmseg->u.shm_perm.seq = (shmseg->u.shm_perm.seq + 1) & 0x7fff;
	shmseg->cred = crhold(cred);
	shmseg->u.shm_segsz = uap->size;
	shmseg->u.shm_cpid = td->td_proc->p_pid;
	shmseg->u.shm_lpid = shmseg->u.shm_nattch = 0;
	shmseg->u.shm_atime = shmseg->u.shm_dtime = 0;
#ifdef MAC
	mac_sysvshm_create(cred, shmseg);
#endif
	shmseg->u.shm_ctime = time_second;
	shm_committed += btoc(size);
	shm_nused++;
	td->td_retval[0] = IXSEQ_TO_IPCID(segnum, shmseg->u.shm_perm);

	return (0);
}

#ifndef _SYS_SYSPROTO_H_
struct shmget_args {
	key_t key;
	size_t size;
	int shmflg;
};
#endif
int
sys_shmget(struct thread *td, struct shmget_args *uap)
{
	int segnum, mode;
	int error;

	if (shm_find_prison(td->td_ucred) == NULL)
		return (ENOSYS);
	mode = uap->shmflg & ACCESSPERMS;
	SYSVSHM_LOCK();
	if (uap->key == IPC_PRIVATE) {
		error = shmget_allocate_segment(td, uap, mode);
	} else {
		segnum = shm_find_segment_by_key(td->td_ucred->cr_prison,
		    uap->key);
		if (segnum >= 0)
			error = shmget_existing(td, uap, mode, segnum);
		else if ((uap->shmflg & IPC_CREAT) == 0)
			error = ENOENT;
		else
			error = shmget_allocate_segment(td, uap, mode);
	}
	SYSVSHM_UNLOCK();
	return (error);
}

static void
shmfork_myhook(struct proc *p1, struct proc *p2)
{
	struct shmmap_state *shmmap_s;
	size_t size;
	int i;

	SYSVSHM_LOCK();
	size = shminfo.shmseg * sizeof(struct shmmap_state);
	shmmap_s = malloc(size, M_SHM, M_WAITOK);
	bcopy(p1->p_vmspace->vm_shm, shmmap_s, size);
	p2->p_vmspace->vm_shm = shmmap_s;
	for (i = 0; i < shminfo.shmseg; i++, shmmap_s++) {
		if (shmmap_s->shmid != -1) {
			KASSERT(IPCID_TO_IX(shmmap_s->shmid) >= 0 &&
			    IPCID_TO_IX(shmmap_s->shmid) < shmalloced,
			    ("segnum %d shmalloced %d",
			    IPCID_TO_IX(shmmap_s->shmid), shmalloced));
			shmsegs[IPCID_TO_IX(shmmap_s->shmid)].u.shm_nattch++;
		}
	}
	SYSVSHM_UNLOCK();
}

static void
shmexit_myhook(struct vmspace *vm)
{
	struct shmmap_state *base, *shm;
	int i;

	base = vm->vm_shm;
	if (base != NULL) {
		vm->vm_shm = NULL;
		SYSVSHM_LOCK();
		for (i = 0, shm = base; i < shminfo.shmseg; i++, shm++) {
			if (shm->shmid != -1)
				shm_delete_mapping(vm, shm);
		}
		SYSVSHM_UNLOCK();
		free(base, M_SHM);
	}
}

static void
shmrealloc(void)
{
	struct shmid_kernel *newsegs;
	int i;

	SYSVSHM_ASSERT_LOCKED();

	if (shmalloced >= shminfo.shmmni)
		return;

	newsegs = malloc(shminfo.shmmni * sizeof(*newsegs), M_SHM,
	    M_WAITOK | M_ZERO);
	for (i = 0; i < shmalloced; i++)
		bcopy(&shmsegs[i], &newsegs[i], sizeof(newsegs[0]));
	for (; i < shminfo.shmmni; i++) {
		newsegs[i].u.shm_perm.mode = SHMSEG_FREE;
		newsegs[i].u.shm_perm.seq = 0;
#ifdef MAC
		mac_sysvshm_init(&newsegs[i]);
#endif
	}
	free(shmsegs, M_SHM);
	shmsegs = newsegs;
	shmalloced = shminfo.shmmni;
}

static struct syscall_helper_data shm_syscalls[] = {
	SYSCALL_INIT_HELPER(shmat),
	SYSCALL_INIT_HELPER(shmctl),
	SYSCALL_INIT_HELPER(shmdt),
	SYSCALL_INIT_HELPER(shmget),
#if defined(COMPAT_FREEBSD4) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD7)
	SYSCALL_INIT_HELPER_COMPAT(freebsd7_shmctl),
#endif
#if defined(__i386__) && (defined(COMPAT_FREEBSD4) || defined(COMPAT_43))
	SYSCALL_INIT_HELPER(shmsys),
#endif
	SYSCALL_INIT_LAST
};

#ifdef COMPAT_FREEBSD32
#include <compat/freebsd32/freebsd32.h>
#include <compat/freebsd32/freebsd32_ipc.h>
#include <compat/freebsd32/freebsd32_proto.h>
#include <compat/freebsd32/freebsd32_signal.h>
#include <compat/freebsd32/freebsd32_syscall.h>
#include <compat/freebsd32/freebsd32_util.h>

static struct syscall_helper_data shm32_syscalls[] = {
	SYSCALL32_INIT_HELPER_COMPAT(shmat),
	SYSCALL32_INIT_HELPER_COMPAT(shmdt),
	SYSCALL32_INIT_HELPER_COMPAT(shmget),
	SYSCALL32_INIT_HELPER(freebsd32_shmsys),
	SYSCALL32_INIT_HELPER(freebsd32_shmctl),
#if defined(COMPAT_FREEBSD4) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD7)
	SYSCALL32_INIT_HELPER(freebsd7_freebsd32_shmctl),
#endif
	SYSCALL_INIT_LAST
};
#endif

static int
shminit(void)
{
	struct prison *pr;
	void **rsv;
	int i, error;
	osd_method_t methods[PR_MAXMETHOD] = {
	    [PR_METHOD_CHECK] =		shm_prison_check,
	    [PR_METHOD_SET] =		shm_prison_set,
	    [PR_METHOD_GET] =		shm_prison_get,
	    [PR_METHOD_REMOVE] =	shm_prison_remove,
	};

#ifndef BURN_BRIDGES
	if (TUNABLE_ULONG_FETCH("kern.ipc.shmmaxpgs", &shminfo.shmall) != 0)
		printf("kern.ipc.shmmaxpgs is now called kern.ipc.shmall!\n");
#endif
	if (shminfo.shmmax == SHMMAX) {
		/* Initialize shmmax dealing with possible overflow. */
		for (i = PAGE_SIZE; i != 0; i--) {
			shminfo.shmmax = shminfo.shmall * i;
			if ((shminfo.shmmax / shminfo.shmall) == (u_long)i)
				break;
		}
	}
	shmalloced = shminfo.shmmni;
	shmsegs = malloc(shmalloced * sizeof(shmsegs[0]), M_SHM,
	    M_WAITOK|M_ZERO);
	for (i = 0; i < shmalloced; i++) {
		shmsegs[i].u.shm_perm.mode = SHMSEG_FREE;
		shmsegs[i].u.shm_perm.seq = 0;
#ifdef MAC
		mac_sysvshm_init(&shmsegs[i]);
#endif
	}
	shm_last_free = 0;
	shm_nused = 0;
	shm_committed = 0;
	sx_init(&sysvshmsx, "sysvshmsx");
	shmexit_hook = &shmexit_myhook;
	shmfork_hook = &shmfork_myhook;

	/* Set current prisons according to their allow.sysvipc. */
	shm_prison_slot = osd_jail_register(NULL, methods);
	rsv = osd_reserve(shm_prison_slot);
	prison_lock(&prison0);
	(void)osd_jail_set_reserved(&prison0, shm_prison_slot, rsv, &prison0);
	prison_unlock(&prison0);
	rsv = NULL;
	sx_slock(&allprison_lock);
	TAILQ_FOREACH(pr, &allprison, pr_list) {
		if (rsv == NULL)
			rsv = osd_reserve(shm_prison_slot);
		prison_lock(pr);
		if ((pr->pr_allow & PR_ALLOW_SYSVIPC) && pr->pr_ref > 0) {
			(void)osd_jail_set_reserved(pr, shm_prison_slot, rsv,
			    &prison0);
			rsv = NULL;
		}
		prison_unlock(pr);
	}
	if (rsv != NULL)
		osd_free_reserved(rsv);
	sx_sunlock(&allprison_lock);

	error = syscall_helper_register(shm_syscalls, SY_THR_STATIC_KLD);
	if (error != 0)
		return (error);
#ifdef COMPAT_FREEBSD32
	error = syscall32_helper_register(shm32_syscalls, SY_THR_STATIC_KLD);
	if (error != 0)
		return (error);
#endif
	return (0);
}

static int
shmunload(void)
{
	int i;

	if (shm_nused > 0)
		return (EBUSY);

#ifdef COMPAT_FREEBSD32
	syscall32_helper_unregister(shm32_syscalls);
#endif
	syscall_helper_unregister(shm_syscalls);
	if (shm_prison_slot != 0)
		osd_jail_deregister(shm_prison_slot);

	for (i = 0; i < shmalloced; i++) {
#ifdef MAC
		mac_sysvshm_destroy(&shmsegs[i]);
#endif
		/*
		 * Objects might be still mapped into the processes
		 * address spaces.  Actual free would happen on the
		 * last mapping destruction.
		 */
		if (shmsegs[i].u.shm_perm.mode != SHMSEG_FREE)
			vm_object_deallocate(shmsegs[i].object);
	}
	free(shmsegs, M_SHM);
	shmexit_hook = NULL;
	shmfork_hook = NULL;
	sx_destroy(&sysvshmsx);
	return (0);
}

static int
sysctl_shmsegs(SYSCTL_HANDLER_ARGS)
{
	struct shmid_kernel tshmseg;
#ifdef COMPAT_FREEBSD32
	struct shmid_kernel32 tshmseg32;
#endif
	struct prison *pr, *rpr;
	void *outaddr;
	size_t outsize;
	int error, i;

	SYSVSHM_LOCK();
	pr = req->td->td_ucred->cr_prison;
	rpr = shm_find_prison(req->td->td_ucred);
	error = 0;
	for (i = 0; i < shmalloced; i++) {
		if ((shmsegs[i].u.shm_perm.mode & SHMSEG_ALLOCATED) == 0 ||
		    rpr == NULL || shm_prison_cansee(rpr, &shmsegs[i]) != 0) {
			bzero(&tshmseg, sizeof(tshmseg));
			tshmseg.u.shm_perm.mode = SHMSEG_FREE;
		} else {
			tshmseg = shmsegs[i];
			if (tshmseg.cred->cr_prison != pr)
				tshmseg.u.shm_perm.key = IPC_PRIVATE;
		}
#ifdef COMPAT_FREEBSD32
		if (SV_CURPROC_FLAG(SV_ILP32)) {
			bzero(&tshmseg32, sizeof(tshmseg32));
			freebsd32_ipcperm_out(&tshmseg.u.shm_perm,
			    &tshmseg32.u.shm_perm);
			CP(tshmseg, tshmseg32, u.shm_segsz);
			CP(tshmseg, tshmseg32, u.shm_lpid);
			CP(tshmseg, tshmseg32, u.shm_cpid);
			CP(tshmseg, tshmseg32, u.shm_nattch);
			CP(tshmseg, tshmseg32, u.shm_atime);
			CP(tshmseg, tshmseg32, u.shm_dtime);
			CP(tshmseg, tshmseg32, u.shm_ctime);
			/* Don't copy object, label, or cred */
			outaddr = &tshmseg32;
			outsize = sizeof(tshmseg32);
		} else
#endif
		{
			tshmseg.object = NULL;
			tshmseg.label = NULL;
			tshmseg.cred = NULL;
			outaddr = &tshmseg;
			outsize = sizeof(tshmseg);
		}
		error = SYSCTL_OUT(req, outaddr, outsize);
		if (error != 0)
			break;
	}
	SYSVSHM_UNLOCK();
	return (error);
}

static int
shm_prison_check(void *obj, void *data)
{
	struct prison *pr = obj;
	struct prison *prpr;
	struct vfsoptlist *opts = data;
	int error, jsys;

	/*
	 * sysvshm is a jailsys integer.
	 * It must be "disable" if the parent jail is disabled.
	 */
	error = vfs_copyopt(opts, "sysvshm", &jsys, sizeof(jsys));
	if (error != ENOENT) {
		if (error != 0)
			return (error);
		switch (jsys) {
		case JAIL_SYS_DISABLE:
			break;
		case JAIL_SYS_NEW:
		case JAIL_SYS_INHERIT:
			prison_lock(pr->pr_parent);
			prpr = osd_jail_get(pr->pr_parent, shm_prison_slot);
			prison_unlock(pr->pr_parent);
			if (prpr == NULL)
				return (EPERM);
			break;
		default:
			return (EINVAL);
		}
	}

	return (0);
}

static int
shm_prison_set(void *obj, void *data)
{
	struct prison *pr = obj;
	struct prison *tpr, *orpr, *nrpr, *trpr;
	struct vfsoptlist *opts = data;
	void *rsv;
	int jsys, descend;

	/*
	 * sysvshm controls which jail is the root of the associated segments
	 * (this jail or same as the parent), or if the feature is available
	 * at all.
	 */
	if (vfs_copyopt(opts, "sysvshm", &jsys, sizeof(jsys)) == ENOENT)
		jsys = vfs_flagopt(opts, "allow.sysvipc", NULL, 0)
		    ? JAIL_SYS_INHERIT
		    : vfs_flagopt(opts, "allow.nosysvipc", NULL, 0)
		    ? JAIL_SYS_DISABLE
		    : -1;
	if (jsys == JAIL_SYS_DISABLE) {
		prison_lock(pr);
		orpr = osd_jail_get(pr, shm_prison_slot);
		if (orpr != NULL)
			osd_jail_del(pr, shm_prison_slot);
		prison_unlock(pr);
		if (orpr != NULL) {
			if (orpr == pr)
				shm_prison_cleanup(pr);
			/* Disable all child jails as well. */
			FOREACH_PRISON_DESCENDANT(pr, tpr, descend) {
				prison_lock(tpr);
				trpr = osd_jail_get(tpr, shm_prison_slot);
				if (trpr != NULL) {
					osd_jail_del(tpr, shm_prison_slot);
					prison_unlock(tpr);
					if (trpr == tpr)
						shm_prison_cleanup(tpr);
				} else {
					prison_unlock(tpr);
					descend = 0;
				}
			}
		}
	} else if (jsys != -1) {
		if (jsys == JAIL_SYS_NEW)
			nrpr = pr;
		else {
			prison_lock(pr->pr_parent);
			nrpr = osd_jail_get(pr->pr_parent, shm_prison_slot);
			prison_unlock(pr->pr_parent);
		}
		rsv = osd_reserve(shm_prison_slot);
		prison_lock(pr);
		orpr = osd_jail_get(pr, shm_prison_slot);
		if (orpr != nrpr)
			(void)osd_jail_set_reserved(pr, shm_prison_slot, rsv,
			    nrpr);
		else
			osd_free_reserved(rsv);
		prison_unlock(pr);
		if (orpr != nrpr) {
			if (orpr == pr)
				shm_prison_cleanup(pr);
			if (orpr != NULL) {
				/* Change child jails matching the old root, */
				FOREACH_PRISON_DESCENDANT(pr, tpr, descend) {
					prison_lock(tpr);
					trpr = osd_jail_get(tpr,
					    shm_prison_slot);
					if (trpr == orpr) {
						(void)osd_jail_set(tpr,
						    shm_prison_slot, nrpr);
						prison_unlock(tpr);
						if (trpr == tpr)
							shm_prison_cleanup(tpr);
					} else {
						prison_unlock(tpr);
						descend = 0;
					}
				}
			}
		}
	}

	return (0);
}

static int
shm_prison_get(void *obj, void *data)
{
	struct prison *pr = obj;
	struct prison *rpr;
	struct vfsoptlist *opts = data;
	int error, jsys;

	/* Set sysvshm based on the jail's root prison. */
	prison_lock(pr);
	rpr = osd_jail_get(pr, shm_prison_slot);
	prison_unlock(pr);
	jsys = rpr == NULL ? JAIL_SYS_DISABLE
	    : rpr == pr ? JAIL_SYS_NEW : JAIL_SYS_INHERIT;
	error = vfs_setopt(opts, "sysvshm", &jsys, sizeof(jsys));
	if (error == ENOENT)
		error = 0;
	return (error);
}

static int
shm_prison_remove(void *obj, void *data __unused)
{
	struct prison *pr = obj;
	struct prison *rpr;

	SYSVSHM_LOCK();
	prison_lock(pr);
	rpr = osd_jail_get(pr, shm_prison_slot);
	prison_unlock(pr);
	if (rpr == pr)
		shm_prison_cleanup(pr);
	SYSVSHM_UNLOCK();
	return (0);
}

static void
shm_prison_cleanup(struct prison *pr)
{
	struct shmid_kernel *shmseg;
	int i;

	/* Remove any segments that belong to this jail. */
	for (i = 0; i < shmalloced; i++) {
		shmseg = &shmsegs[i];
		if ((shmseg->u.shm_perm.mode & SHMSEG_ALLOCATED) &&
		    shmseg->cred != NULL && shmseg->cred->cr_prison == pr) {
			shm_remove(shmseg, i);
		}
	}
}

SYSCTL_JAIL_PARAM_SYS_NODE(sysvshm, CTLFLAG_RW, "SYSV shared memory");

#if defined(__i386__) && (defined(COMPAT_FREEBSD4) || defined(COMPAT_43))
struct oshmid_ds {
	struct	ipc_perm_old shm_perm;	/* operation perms */
	int	shm_segsz;		/* size of segment (bytes) */
	u_short	shm_cpid;		/* pid, creator */
	u_short	shm_lpid;		/* pid, last operation */
	short	shm_nattch;		/* no. of current attaches */
	time_t	shm_atime;		/* last attach time */
	time_t	shm_dtime;		/* last detach time */
	time_t	shm_ctime;		/* last change time */
	void	*shm_handle;		/* internal handle for shm segment */
};

struct oshmctl_args {
	int shmid;
	int cmd;
	struct oshmid_ds *ubuf;
};

static int
oshmctl(struct thread *td, struct oshmctl_args *uap)
{
#ifdef COMPAT_43
	int error = 0;
	struct prison *rpr;
	struct shmid_kernel *shmseg;
	struct oshmid_ds outbuf;

	rpr = shm_find_prison(td->td_ucred);
	if (rpr == NULL)
		return (ENOSYS);
	if (uap->cmd != IPC_STAT) {
		return (freebsd7_shmctl(td,
		    (struct freebsd7_shmctl_args *)uap));
	}
	SYSVSHM_LOCK();
	shmseg = shm_find_segment(rpr, uap->shmid, true);
	if (shmseg == NULL) {
		SYSVSHM_UNLOCK();
		return (EINVAL);
	}
	error = ipcperm(td, &shmseg->u.shm_perm, IPC_R);
	if (error != 0) {
		SYSVSHM_UNLOCK();
		return (error);
	}
#ifdef MAC
	error = mac_sysvshm_check_shmctl(td->td_ucred, shmseg, uap->cmd);
	if (error != 0) {
		SYSVSHM_UNLOCK();
		return (error);
	}
#endif
	ipcperm_new2old(&shmseg->u.shm_perm, &outbuf.shm_perm);
	outbuf.shm_segsz = shmseg->u.shm_segsz;
	outbuf.shm_cpid = shmseg->u.shm_cpid;
	outbuf.shm_lpid = shmseg->u.shm_lpid;
	outbuf.shm_nattch = shmseg->u.shm_nattch;
	outbuf.shm_atime = shmseg->u.shm_atime;
	outbuf.shm_dtime = shmseg->u.shm_dtime;
	outbuf.shm_ctime = shmseg->u.shm_ctime;
	outbuf.shm_handle = shmseg->object;
	SYSVSHM_UNLOCK();
	return (copyout(&outbuf, uap->ubuf, sizeof(outbuf)));
#else
	return (EINVAL);
#endif
}

/* XXX casting to (sy_call_t *) is bogus, as usual. */
static sy_call_t *shmcalls[] = {
	(sy_call_t *)sys_shmat, (sy_call_t *)oshmctl,
	(sy_call_t *)sys_shmdt, (sy_call_t *)sys_shmget,
	(sy_call_t *)freebsd7_shmctl
};

#ifndef _SYS_SYSPROTO_H_
/* XXX actually varargs. */
struct shmsys_args {
	int	which;
	int	a2;
	int	a3;
	int	a4;
};
#endif
int
sys_shmsys(struct thread *td, struct shmsys_args *uap)
{

	AUDIT_ARG_SVIPC_WHICH(uap->which);
	if (uap->which < 0 || uap->which >= nitems(shmcalls))
		return (EINVAL);
	return ((*shmcalls[uap->which])(td, &uap->a2));
}

#endif	/* i386 && (COMPAT_FREEBSD4 || COMPAT_43) */

#ifdef COMPAT_FREEBSD32

int
freebsd32_shmsys(struct thread *td, struct freebsd32_shmsys_args *uap)
{

#if defined(COMPAT_FREEBSD4) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD7)
	AUDIT_ARG_SVIPC_WHICH(uap->which);
	switch (uap->which) {
	case 0:	{	/* shmat */
		struct shmat_args ap;

		ap.shmid = uap->a2;
		ap.shmaddr = PTRIN(uap->a3);
		ap.shmflg = uap->a4;
		return (sysent[SYS_shmat].sy_call(td, &ap));
	}
	case 2: {	/* shmdt */
		struct shmdt_args ap;

		ap.shmaddr = PTRIN(uap->a2);
		return (sysent[SYS_shmdt].sy_call(td, &ap));
	}
	case 3: {	/* shmget */
		struct shmget_args ap;

		ap.key = uap->a2;
		ap.size = uap->a3;
		ap.shmflg = uap->a4;
		return (sysent[SYS_shmget].sy_call(td, &ap));
	}
	case 4: {	/* shmctl */
		struct freebsd7_freebsd32_shmctl_args ap;

		ap.shmid = uap->a2;
		ap.cmd = uap->a3;
		ap.buf = PTRIN(uap->a4);
		return (freebsd7_freebsd32_shmctl(td, &ap));
	}
	case 1:		/* oshmctl */
	default:
		return (EINVAL);
	}
#else
	return (nosys(td, NULL));
#endif
}

#if defined(COMPAT_FREEBSD4) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD7)
int
freebsd7_freebsd32_shmctl(struct thread *td,
    struct freebsd7_freebsd32_shmctl_args *uap)
{
	int error;
	union {
		struct shmid_ds shmid_ds;
		struct shm_info shm_info;
		struct shminfo shminfo;
	} u;
	union {
		struct shmid_ds32_old shmid_ds32;
		struct shm_info32 shm_info32;
		struct shminfo32 shminfo32;
	} u32;
	size_t sz;

	if (uap->cmd == IPC_SET) {
		if ((error = copyin(uap->buf, &u32.shmid_ds32,
		    sizeof(u32.shmid_ds32))))
			goto done;
		freebsd32_ipcperm_old_in(&u32.shmid_ds32.shm_perm,
		    &u.shmid_ds.shm_perm);
		CP(u32.shmid_ds32, u.shmid_ds, shm_segsz);
		CP(u32.shmid_ds32, u.shmid_ds, shm_lpid);
		CP(u32.shmid_ds32, u.shmid_ds, shm_cpid);
		CP(u32.shmid_ds32, u.shmid_ds, shm_nattch);
		CP(u32.shmid_ds32, u.shmid_ds, shm_atime);
		CP(u32.shmid_ds32, u.shmid_ds, shm_dtime);
		CP(u32.shmid_ds32, u.shmid_ds, shm_ctime);
	}

	error = kern_shmctl(td, uap->shmid, uap->cmd, (void *)&u, &sz);
	if (error)
		goto done;

	/* Cases in which we need to copyout */
	switch (uap->cmd) {
	case IPC_INFO:
		CP(u.shminfo, u32.shminfo32, shmmax);
		CP(u.shminfo, u32.shminfo32, shmmin);
		CP(u.shminfo, u32.shminfo32, shmmni);
		CP(u.shminfo, u32.shminfo32, shmseg);
		CP(u.shminfo, u32.shminfo32, shmall);
		error = copyout(&u32.shminfo32, uap->buf,
		    sizeof(u32.shminfo32));
		break;
	case SHM_INFO:
		CP(u.shm_info, u32.shm_info32, used_ids);
		CP(u.shm_info, u32.shm_info32, shm_rss);
		CP(u.shm_info, u32.shm_info32, shm_tot);
		CP(u.shm_info, u32.shm_info32, shm_swp);
		CP(u.shm_info, u32.shm_info32, swap_attempts);
		CP(u.shm_info, u32.shm_info32, swap_successes);
		error = copyout(&u32.shm_info32, uap->buf,
		    sizeof(u32.shm_info32));
		break;
	case SHM_STAT:
	case IPC_STAT:
		memset(&u32.shmid_ds32, 0, sizeof(u32.shmid_ds32));
		freebsd32_ipcperm_old_out(&u.shmid_ds.shm_perm,
		    &u32.shmid_ds32.shm_perm);
		if (u.shmid_ds.shm_segsz > INT32_MAX)
			u32.shmid_ds32.shm_segsz = INT32_MAX;
		else
			CP(u.shmid_ds, u32.shmid_ds32, shm_segsz);
		CP(u.shmid_ds, u32.shmid_ds32, shm_lpid);
		CP(u.shmid_ds, u32.shmid_ds32, shm_cpid);
		CP(u.shmid_ds, u32.shmid_ds32, shm_nattch);
		CP(u.shmid_ds, u32.shmid_ds32, shm_atime);
		CP(u.shmid_ds, u32.shmid_ds32, shm_dtime);
		CP(u.shmid_ds, u32.shmid_ds32, shm_ctime);
		u32.shmid_ds32.shm_internal = 0;
		error = copyout(&u32.shmid_ds32, uap->buf,
		    sizeof(u32.shmid_ds32));
		break;
	}

done:
	if (error) {
		/* Invalidate the return value */
		td->td_retval[0] = -1;
	}
	return (error);
}
#endif

int
freebsd32_shmctl(struct thread *td, struct freebsd32_shmctl_args *uap)
{
	int error;
	union {
		struct shmid_ds shmid_ds;
		struct shm_info shm_info;
		struct shminfo shminfo;
	} u;
	union {
		struct shmid_ds32 shmid_ds32;
		struct shm_info32 shm_info32;
		struct shminfo32 shminfo32;
	} u32;
	size_t sz;

	if (uap->cmd == IPC_SET) {
		if ((error = copyin(uap->buf, &u32.shmid_ds32,
		    sizeof(u32.shmid_ds32))))
			goto done;
		freebsd32_ipcperm_in(&u32.shmid_ds32.shm_perm,
		    &u.shmid_ds.shm_perm);
		CP(u32.shmid_ds32, u.shmid_ds, shm_segsz);
		CP(u32.shmid_ds32, u.shmid_ds, shm_lpid);
		CP(u32.shmid_ds32, u.shmid_ds, shm_cpid);
		CP(u32.shmid_ds32, u.shmid_ds, shm_nattch);
		CP(u32.shmid_ds32, u.shmid_ds, shm_atime);
		CP(u32.shmid_ds32, u.shmid_ds, shm_dtime);
		CP(u32.shmid_ds32, u.shmid_ds, shm_ctime);
	}

	error = kern_shmctl(td, uap->shmid, uap->cmd, (void *)&u, &sz);
	if (error)
		goto done;

	/* Cases in which we need to copyout */
	switch (uap->cmd) {
	case IPC_INFO:
		CP(u.shminfo, u32.shminfo32, shmmax);
		CP(u.shminfo, u32.shminfo32, shmmin);
		CP(u.shminfo, u32.shminfo32, shmmni);
		CP(u.shminfo, u32.shminfo32, shmseg);
		CP(u.shminfo, u32.shminfo32, shmall);
		error = copyout(&u32.shminfo32, uap->buf,
		    sizeof(u32.shminfo32));
		break;
	case SHM_INFO:
		CP(u.shm_info, u32.shm_info32, used_ids);
		CP(u.shm_info, u32.shm_info32, shm_rss);
		CP(u.shm_info, u32.shm_info32, shm_tot);
		CP(u.shm_info, u32.shm_info32, shm_swp);
		CP(u.shm_info, u32.shm_info32, swap_attempts);
		CP(u.shm_info, u32.shm_info32, swap_successes);
		error = copyout(&u32.shm_info32, uap->buf,
		    sizeof(u32.shm_info32));
		break;
	case SHM_STAT:
	case IPC_STAT:
		freebsd32_ipcperm_out(&u.shmid_ds.shm_perm,
		    &u32.shmid_ds32.shm_perm);
		if (u.shmid_ds.shm_segsz > INT32_MAX)
			u32.shmid_ds32.shm_segsz = INT32_MAX;
		else
			CP(u.shmid_ds, u32.shmid_ds32, shm_segsz);
		CP(u.shmid_ds, u32.shmid_ds32, shm_lpid);
		CP(u.shmid_ds, u32.shmid_ds32, shm_cpid);
		CP(u.shmid_ds, u32.shmid_ds32, shm_nattch);
		CP(u.shmid_ds, u32.shmid_ds32, shm_atime);
		CP(u.shmid_ds, u32.shmid_ds32, shm_dtime);
		CP(u.shmid_ds, u32.shmid_ds32, shm_ctime);
		error = copyout(&u32.shmid_ds32, uap->buf,
		    sizeof(u32.shmid_ds32));
		break;
	}

done:
	if (error) {
		/* Invalidate the return value */
		td->td_retval[0] = -1;
	}
	return (error);
}
#endif

#if defined(COMPAT_FREEBSD4) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD7)

#ifndef CP
#define CP(src, dst, fld)	do { (dst).fld = (src).fld; } while (0)
#endif

#ifndef _SYS_SYSPROTO_H_
struct freebsd7_shmctl_args {
	int shmid;
	int cmd;
	struct shmid_ds_old *buf;
};
#endif
int
freebsd7_shmctl(struct thread *td, struct freebsd7_shmctl_args *uap)
{
	int error;
	struct shmid_ds_old old;
	struct shmid_ds buf;
	size_t bufsz;

	/*
	 * The only reason IPC_INFO, SHM_INFO, SHM_STAT exists is to support
	 * Linux binaries.  If we see the call come through the FreeBSD ABI,
	 * return an error back to the user since we do not to support this.
	 */
	if (uap->cmd == IPC_INFO || uap->cmd == SHM_INFO ||
	    uap->cmd == SHM_STAT)
		return (EINVAL);

	/* IPC_SET needs to copyin the buffer before calling kern_shmctl */
	if (uap->cmd == IPC_SET) {
		if ((error = copyin(uap->buf, &old, sizeof(old))))
			goto done;
		ipcperm_old2new(&old.shm_perm, &buf.shm_perm);
		CP(old, buf, shm_segsz);
		CP(old, buf, shm_lpid);
		CP(old, buf, shm_cpid);
		CP(old, buf, shm_nattch);
		CP(old, buf, shm_atime);
		CP(old, buf, shm_dtime);
		CP(old, buf, shm_ctime);
	}

	error = kern_shmctl(td, uap->shmid, uap->cmd, (void *)&buf, &bufsz);
	if (error)
		goto done;

	/* Cases in which we need to copyout */
	switch (uap->cmd) {
	case IPC_STAT:
		memset(&old, 0, sizeof(old));
		ipcperm_new2old(&buf.shm_perm, &old.shm_perm);
		if (buf.shm_segsz > INT_MAX)
			old.shm_segsz = INT_MAX;
		else
			CP(buf, old, shm_segsz);
		CP(buf, old, shm_lpid);
		CP(buf, old, shm_cpid);
		if (buf.shm_nattch > SHRT_MAX)
			old.shm_nattch = SHRT_MAX;
		else
			CP(buf, old, shm_nattch);
		CP(buf, old, shm_atime);
		CP(buf, old, shm_dtime);
		CP(buf, old, shm_ctime);
		old.shm_internal = NULL;
		error = copyout(&old, uap->buf, sizeof(old));
		break;
	}

done:
	if (error) {
		/* Invalidate the return value */
		td->td_retval[0] = -1;
	}
	return (error);
}

#endif	/* COMPAT_FREEBSD4 || COMPAT_FREEBSD5 || COMPAT_FREEBSD6 ||
	   COMPAT_FREEBSD7 */

static int
sysvshm_modload(struct module *module, int cmd, void *arg)
{
	int error = 0;

	switch (cmd) {
	case MOD_LOAD:
		error = shminit();
		if (error != 0)
			shmunload();
		break;
	case MOD_UNLOAD:
		error = shmunload();
		break;
	case MOD_SHUTDOWN:
		break;
	default:
		error = EINVAL;
		break;
	}
	return (error);
}

static moduledata_t sysvshm_mod = {
	"sysvshm",
	&sysvshm_modload,
	NULL
};

DECLARE_MODULE(sysvshm, sysvshm_mod, SI_SUB_SYSV_SHM, SI_ORDER_FIRST);
MODULE_VERSION(sysvshm, 1);
