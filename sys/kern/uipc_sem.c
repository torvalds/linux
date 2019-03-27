/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Alfred Perlstein <alfred@FreeBSD.org>
 * Copyright (c) 2003-2005 SPARTA, Inc.
 * Copyright (c) 2005, 2016-2017 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project in part by Network
 * Associates Laboratories, the Security Research Division of Network
 * Associates, Inc. under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"),
 * as part of the DARPA CHATS research program.
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

#include "opt_posix.h"

#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/condvar.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/fnv_hash.h>
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/ksem.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/posix4.h>
#include <sys/_semaphore.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/syscallsubr.h>
#include <sys/sysctl.h>
#include <sys/sysent.h>
#include <sys/sysproto.h>
#include <sys/systm.h>
#include <sys/sx.h>
#include <sys/user.h>
#include <sys/vnode.h>

#include <security/audit/audit.h>
#include <security/mac/mac_framework.h>

FEATURE(p1003_1b_semaphores, "POSIX P1003.1B semaphores support");
/*
 * TODO
 *
 * - Resource limits?
 * - Replace global sem_lock with mtx_pool locks?
 * - Add a MAC check_create() hook for creating new named semaphores.
 */

#ifndef SEM_MAX
#define	SEM_MAX	30
#endif

#ifdef SEM_DEBUG
#define	DP(x)	printf x
#else
#define	DP(x)
#endif

struct ksem_mapping {
	char		*km_path;
	Fnv32_t		km_fnv;
	struct ksem	*km_ksem;
	LIST_ENTRY(ksem_mapping) km_link;
};

static MALLOC_DEFINE(M_KSEM, "ksem", "semaphore file descriptor");
static LIST_HEAD(, ksem_mapping) *ksem_dictionary;
static struct sx ksem_dict_lock;
static struct mtx ksem_count_lock;
static struct mtx sem_lock;
static u_long ksem_hash;
static int ksem_dead;

#define	KSEM_HASH(fnv)	(&ksem_dictionary[(fnv) & ksem_hash])

static int nsems = 0;
SYSCTL_DECL(_p1003_1b);
SYSCTL_INT(_p1003_1b, OID_AUTO, nsems, CTLFLAG_RD, &nsems, 0,
    "Number of active kernel POSIX semaphores");

static int	kern_sem_wait(struct thread *td, semid_t id, int tryflag,
		    struct timespec *abstime);
static int	ksem_access(struct ksem *ks, struct ucred *ucred);
static struct ksem *ksem_alloc(struct ucred *ucred, mode_t mode,
		    unsigned int value);
static int	ksem_create(struct thread *td, const char *path,
		    semid_t *semidp, mode_t mode, unsigned int value,
		    int flags, int compat32);
static void	ksem_drop(struct ksem *ks);
static int	ksem_get(struct thread *td, semid_t id, cap_rights_t *rightsp,
    struct file **fpp);
static struct ksem *ksem_hold(struct ksem *ks);
static void	ksem_insert(char *path, Fnv32_t fnv, struct ksem *ks);
static struct ksem *ksem_lookup(char *path, Fnv32_t fnv);
static void	ksem_module_destroy(void);
static int	ksem_module_init(void);
static int	ksem_remove(char *path, Fnv32_t fnv, struct ucred *ucred);
static int	sem_modload(struct module *module, int cmd, void *arg);

static fo_stat_t	ksem_stat;
static fo_close_t	ksem_closef;
static fo_chmod_t	ksem_chmod;
static fo_chown_t	ksem_chown;
static fo_fill_kinfo_t	ksem_fill_kinfo;

/* File descriptor operations. */
static struct fileops ksem_ops = {
	.fo_read = invfo_rdwr,
	.fo_write = invfo_rdwr,
	.fo_truncate = invfo_truncate,
	.fo_ioctl = invfo_ioctl,
	.fo_poll = invfo_poll,
	.fo_kqfilter = invfo_kqfilter,
	.fo_stat = ksem_stat,
	.fo_close = ksem_closef,
	.fo_chmod = ksem_chmod,
	.fo_chown = ksem_chown,
	.fo_sendfile = invfo_sendfile,
	.fo_fill_kinfo = ksem_fill_kinfo,
	.fo_flags = DFLAG_PASSABLE
};

FEATURE(posix_sem, "POSIX semaphores");

static int
ksem_stat(struct file *fp, struct stat *sb, struct ucred *active_cred,
    struct thread *td)
{
	struct ksem *ks;
#ifdef MAC
	int error;
#endif

	ks = fp->f_data;

#ifdef MAC
	error = mac_posixsem_check_stat(active_cred, fp->f_cred, ks);
	if (error)
		return (error);
#endif
	
	/*
	 * Attempt to return sanish values for fstat() on a semaphore
	 * file descriptor.
	 */
	bzero(sb, sizeof(*sb));

	mtx_lock(&sem_lock);
	sb->st_atim = ks->ks_atime;
	sb->st_ctim = ks->ks_ctime;
	sb->st_mtim = ks->ks_mtime;
	sb->st_birthtim = ks->ks_birthtime;
	sb->st_uid = ks->ks_uid;
	sb->st_gid = ks->ks_gid;
	sb->st_mode = S_IFREG | ks->ks_mode;		/* XXX */
	mtx_unlock(&sem_lock);

	return (0);
}

static int
ksem_chmod(struct file *fp, mode_t mode, struct ucred *active_cred,
    struct thread *td)
{
	struct ksem *ks;
	int error;

	error = 0;
	ks = fp->f_data;
	mtx_lock(&sem_lock);
#ifdef MAC
	error = mac_posixsem_check_setmode(active_cred, ks, mode);
	if (error != 0)
		goto out;
#endif
	error = vaccess(VREG, ks->ks_mode, ks->ks_uid, ks->ks_gid, VADMIN,
	    active_cred, NULL);
	if (error != 0)
		goto out;
	ks->ks_mode = mode & ACCESSPERMS;
out:
	mtx_unlock(&sem_lock);
	return (error);
}

static int
ksem_chown(struct file *fp, uid_t uid, gid_t gid, struct ucred *active_cred,
    struct thread *td)
{
	struct ksem *ks;
	int error;

	error = 0;
	ks = fp->f_data;
	mtx_lock(&sem_lock);
#ifdef MAC
	error = mac_posixsem_check_setowner(active_cred, ks, uid, gid);
	if (error != 0)
		goto out;
#endif
	if (uid == (uid_t)-1)
		uid = ks->ks_uid;
	if (gid == (gid_t)-1)
                 gid = ks->ks_gid;
	if (((uid != ks->ks_uid && uid != active_cred->cr_uid) ||
	    (gid != ks->ks_gid && !groupmember(gid, active_cred))) &&
	    (error = priv_check_cred(active_cred, PRIV_VFS_CHOWN)))
		goto out;
	ks->ks_uid = uid;
	ks->ks_gid = gid;
out:
	mtx_unlock(&sem_lock);
	return (error);
}

static int
ksem_closef(struct file *fp, struct thread *td)
{
	struct ksem *ks;

	ks = fp->f_data;
	fp->f_data = NULL;
	ksem_drop(ks);

	return (0);
}

static int
ksem_fill_kinfo(struct file *fp, struct kinfo_file *kif, struct filedesc *fdp)
{
	const char *path, *pr_path;
	struct ksem *ks;
	size_t pr_pathlen;

	kif->kf_type = KF_TYPE_SEM;
	ks = fp->f_data;
	mtx_lock(&sem_lock);
	kif->kf_un.kf_sem.kf_sem_value = ks->ks_value;
	kif->kf_un.kf_sem.kf_sem_mode = S_IFREG | ks->ks_mode;	/* XXX */
	mtx_unlock(&sem_lock);
	if (ks->ks_path != NULL) {
		sx_slock(&ksem_dict_lock);
		if (ks->ks_path != NULL) {
			path = ks->ks_path;
			pr_path = curthread->td_ucred->cr_prison->pr_path;
			if (strcmp(pr_path, "/") != 0) {
				/* Return the jail-rooted pathname. */
				pr_pathlen = strlen(pr_path);
				if (strncmp(path, pr_path, pr_pathlen) == 0 &&
				    path[pr_pathlen] == '/')
					path += pr_pathlen;
			}
			strlcpy(kif->kf_path, path, sizeof(kif->kf_path));
		}
		sx_sunlock(&ksem_dict_lock);
	}
	return (0);
}

/*
 * ksem object management including creation and reference counting
 * routines.
 */
static struct ksem *
ksem_alloc(struct ucred *ucred, mode_t mode, unsigned int value)
{
	struct ksem *ks;

	mtx_lock(&ksem_count_lock);
	if (nsems == p31b_getcfg(CTL_P1003_1B_SEM_NSEMS_MAX) || ksem_dead) {
		mtx_unlock(&ksem_count_lock);
		return (NULL);
	}
	nsems++;
	mtx_unlock(&ksem_count_lock);
	ks = malloc(sizeof(*ks), M_KSEM, M_WAITOK | M_ZERO);
	ks->ks_uid = ucred->cr_uid;
	ks->ks_gid = ucred->cr_gid;
	ks->ks_mode = mode;
	ks->ks_value = value;
	cv_init(&ks->ks_cv, "ksem");
	vfs_timestamp(&ks->ks_birthtime);
	ks->ks_atime = ks->ks_mtime = ks->ks_ctime = ks->ks_birthtime;
	refcount_init(&ks->ks_ref, 1);
#ifdef MAC
	mac_posixsem_init(ks);
	mac_posixsem_create(ucred, ks);
#endif

	return (ks);
}

static struct ksem *
ksem_hold(struct ksem *ks)
{

	refcount_acquire(&ks->ks_ref);
	return (ks);
}

static void
ksem_drop(struct ksem *ks)
{

	if (refcount_release(&ks->ks_ref)) {
#ifdef MAC
		mac_posixsem_destroy(ks);
#endif
		cv_destroy(&ks->ks_cv);
		free(ks, M_KSEM);
		mtx_lock(&ksem_count_lock);
		nsems--;
		mtx_unlock(&ksem_count_lock);
	}
}

/*
 * Determine if the credentials have sufficient permissions for read
 * and write access.
 */
static int
ksem_access(struct ksem *ks, struct ucred *ucred)
{
	int error;

	error = vaccess(VREG, ks->ks_mode, ks->ks_uid, ks->ks_gid,
	    VREAD | VWRITE, ucred, NULL);
	if (error)
		error = priv_check_cred(ucred, PRIV_SEM_WRITE);
	return (error);
}

/*
 * Dictionary management.  We maintain an in-kernel dictionary to map
 * paths to semaphore objects.  We use the FNV hash on the path to
 * store the mappings in a hash table.
 */
static struct ksem *
ksem_lookup(char *path, Fnv32_t fnv)
{
	struct ksem_mapping *map;

	LIST_FOREACH(map, KSEM_HASH(fnv), km_link) {
		if (map->km_fnv != fnv)
			continue;
		if (strcmp(map->km_path, path) == 0)
			return (map->km_ksem);
	}

	return (NULL);
}

static void
ksem_insert(char *path, Fnv32_t fnv, struct ksem *ks)
{
	struct ksem_mapping *map;

	map = malloc(sizeof(struct ksem_mapping), M_KSEM, M_WAITOK);
	map->km_path = path;
	map->km_fnv = fnv;
	map->km_ksem = ksem_hold(ks);
	ks->ks_path = path;
	LIST_INSERT_HEAD(KSEM_HASH(fnv), map, km_link);
}

static int
ksem_remove(char *path, Fnv32_t fnv, struct ucred *ucred)
{
	struct ksem_mapping *map;
	int error;

	LIST_FOREACH(map, KSEM_HASH(fnv), km_link) {
		if (map->km_fnv != fnv)
			continue;
		if (strcmp(map->km_path, path) == 0) {
#ifdef MAC
			error = mac_posixsem_check_unlink(ucred, map->km_ksem);
			if (error)
				return (error);
#endif
			error = ksem_access(map->km_ksem, ucred);
			if (error)
				return (error);
			map->km_ksem->ks_path = NULL;
			LIST_REMOVE(map, km_link);
			ksem_drop(map->km_ksem);
			free(map->km_path, M_KSEM);
			free(map, M_KSEM);
			return (0);
		}
	}

	return (ENOENT);
}

static int
ksem_create_copyout_semid(struct thread *td, semid_t *semidp, int fd,
    int compat32)
{
	semid_t semid;
#ifdef COMPAT_FREEBSD32
	int32_t semid32;
#endif
	void *ptr;
	size_t ptrs;

#ifdef COMPAT_FREEBSD32
	if (compat32) {
		semid32 = fd;
		ptr = &semid32;
		ptrs = sizeof(semid32);
	} else {
#endif
		semid = fd;
		ptr = &semid;
		ptrs = sizeof(semid);
		compat32 = 0; /* silence gcc */
#ifdef COMPAT_FREEBSD32
	}
#endif

	return (copyout(ptr, semidp, ptrs));
}

/* Other helper routines. */
static int
ksem_create(struct thread *td, const char *name, semid_t *semidp, mode_t mode,
    unsigned int value, int flags, int compat32)
{
	struct filedesc *fdp;
	struct ksem *ks;
	struct file *fp;
	char *path;
	const char *pr_path;
	size_t pr_pathlen;
	Fnv32_t fnv;
	int error, fd;

	AUDIT_ARG_FFLAGS(flags);
	AUDIT_ARG_MODE(mode);
	AUDIT_ARG_VALUE(value);

	if (value > SEM_VALUE_MAX)
		return (EINVAL);

	fdp = td->td_proc->p_fd;
	mode = (mode & ~fdp->fd_cmask) & ACCESSPERMS;
	error = falloc(td, &fp, &fd, O_CLOEXEC);
	if (error) {
		if (name == NULL)
			error = ENOSPC;
		return (error);
	}

	/*
	 * Go ahead and copyout the file descriptor now.  This is a bit
	 * premature, but it is a lot easier to handle errors as opposed
	 * to later when we've possibly created a new semaphore, etc.
	 */
	error = ksem_create_copyout_semid(td, semidp, fd, compat32);
	if (error) {
		fdclose(td, fp, fd);
		fdrop(fp, td);
		return (error);
	}

	if (name == NULL) {
		/* Create an anonymous semaphore. */
		ks = ksem_alloc(td->td_ucred, mode, value);
		if (ks == NULL)
			error = ENOSPC;
		else
			ks->ks_flags |= KS_ANONYMOUS;
	} else {
		path = malloc(MAXPATHLEN, M_KSEM, M_WAITOK);
		pr_path = td->td_ucred->cr_prison->pr_path;

		/* Construct a full pathname for jailed callers. */
		pr_pathlen = strcmp(pr_path, "/") == 0 ? 0
		    : strlcpy(path, pr_path, MAXPATHLEN);
		error = copyinstr(name, path + pr_pathlen,
		    MAXPATHLEN - pr_pathlen, NULL);

		/* Require paths to start with a '/' character. */
		if (error == 0 && path[pr_pathlen] != '/')
			error = EINVAL;
		if (error) {
			fdclose(td, fp, fd);
			fdrop(fp, td);
			free(path, M_KSEM);
			return (error);
		}

		AUDIT_ARG_UPATH1_CANON(path);
		fnv = fnv_32_str(path, FNV1_32_INIT);
		sx_xlock(&ksem_dict_lock);
		ks = ksem_lookup(path, fnv);
		if (ks == NULL) {
			/* Object does not exist, create it if requested. */
			if (flags & O_CREAT) {
				ks = ksem_alloc(td->td_ucred, mode, value);
				if (ks == NULL)
					error = ENFILE;
				else {
					ksem_insert(path, fnv, ks);
					path = NULL;
				}
			} else
				error = ENOENT;
		} else {
			/*
			 * Object already exists, obtain a new
			 * reference if requested and permitted.
			 */
			if ((flags & (O_CREAT | O_EXCL)) ==
			    (O_CREAT | O_EXCL))
				error = EEXIST;
			else {
#ifdef MAC
				error = mac_posixsem_check_open(td->td_ucred,
				    ks);
				if (error == 0)
#endif
				error = ksem_access(ks, td->td_ucred);
			}
			if (error == 0)
				ksem_hold(ks);
#ifdef INVARIANTS
			else
				ks = NULL;
#endif
		}
		sx_xunlock(&ksem_dict_lock);
		if (path)
			free(path, M_KSEM);
	}

	if (error) {
		KASSERT(ks == NULL, ("ksem_create error with a ksem"));
		fdclose(td, fp, fd);
		fdrop(fp, td);
		return (error);
	}
	KASSERT(ks != NULL, ("ksem_create w/o a ksem"));

	finit(fp, FREAD | FWRITE, DTYPE_SEM, ks, &ksem_ops);

	fdrop(fp, td);

	return (0);
}

static int
ksem_get(struct thread *td, semid_t id, cap_rights_t *rightsp,
    struct file **fpp)
{
	struct ksem *ks;
	struct file *fp;
	int error;

	error = fget(td, id, rightsp, &fp);
	if (error)
		return (EINVAL);
	if (fp->f_type != DTYPE_SEM) {
		fdrop(fp, td);
		return (EINVAL);
	}
	ks = fp->f_data;
	if (ks->ks_flags & KS_DEAD) {
		fdrop(fp, td);
		return (EINVAL);
	}
	*fpp = fp;
	return (0);
}

/* System calls. */
#ifndef _SYS_SYSPROTO_H_
struct ksem_init_args {
	unsigned int	value;
	semid_t		*idp;
};
#endif
int
sys_ksem_init(struct thread *td, struct ksem_init_args *uap)
{

	return (ksem_create(td, NULL, uap->idp, S_IRWXU | S_IRWXG, uap->value,
	    0, 0));
}

#ifndef _SYS_SYSPROTO_H_
struct ksem_open_args {
	char		*name;
	int		oflag;
	mode_t		mode;
	unsigned int	value;
	semid_t		*idp;	
};
#endif
int
sys_ksem_open(struct thread *td, struct ksem_open_args *uap)
{

	DP((">>> ksem_open start, pid=%d\n", (int)td->td_proc->p_pid));

	if ((uap->oflag & ~(O_CREAT | O_EXCL)) != 0)
		return (EINVAL);
	return (ksem_create(td, uap->name, uap->idp, uap->mode, uap->value,
	    uap->oflag, 0));
}

#ifndef _SYS_SYSPROTO_H_
struct ksem_unlink_args {
	char		*name;
};
#endif
int
sys_ksem_unlink(struct thread *td, struct ksem_unlink_args *uap)
{
	char *path;
	const char *pr_path;
	size_t pr_pathlen;
	Fnv32_t fnv;
	int error;

	path = malloc(MAXPATHLEN, M_TEMP, M_WAITOK);
	pr_path = td->td_ucred->cr_prison->pr_path;
	pr_pathlen = strcmp(pr_path, "/") == 0 ? 0
	    : strlcpy(path, pr_path, MAXPATHLEN);
	error = copyinstr(uap->name, path + pr_pathlen, MAXPATHLEN - pr_pathlen,
	    NULL);
	if (error) {
		free(path, M_TEMP);
		return (error);
	}

	AUDIT_ARG_UPATH1_CANON(path);
	fnv = fnv_32_str(path, FNV1_32_INIT);
	sx_xlock(&ksem_dict_lock);
	error = ksem_remove(path, fnv, td->td_ucred);
	sx_xunlock(&ksem_dict_lock);
	free(path, M_TEMP);

	return (error);
}

#ifndef _SYS_SYSPROTO_H_
struct ksem_close_args {
	semid_t		id;
};
#endif
int
sys_ksem_close(struct thread *td, struct ksem_close_args *uap)
{
	struct ksem *ks;
	struct file *fp;
	int error;

	/* No capability rights required to close a semaphore. */
	AUDIT_ARG_FD(uap->id);
	error = ksem_get(td, uap->id, &cap_no_rights, &fp);
	if (error)
		return (error);
	ks = fp->f_data;
	if (ks->ks_flags & KS_ANONYMOUS) {
		fdrop(fp, td);
		return (EINVAL);
	}
	error = kern_close(td, uap->id);
	fdrop(fp, td);
	return (error);
}

#ifndef _SYS_SYSPROTO_H_
struct ksem_post_args {
	semid_t	id;
};
#endif
int
sys_ksem_post(struct thread *td, struct ksem_post_args *uap)
{
	cap_rights_t rights;
	struct file *fp;
	struct ksem *ks;
	int error;

	AUDIT_ARG_FD(uap->id);
	error = ksem_get(td, uap->id,
	    cap_rights_init(&rights, CAP_SEM_POST), &fp);
	if (error)
		return (error);
	ks = fp->f_data;

	mtx_lock(&sem_lock);
#ifdef MAC
	error = mac_posixsem_check_post(td->td_ucred, fp->f_cred, ks);
	if (error)
		goto err;
#endif
	if (ks->ks_value == SEM_VALUE_MAX) {
		error = EOVERFLOW;
		goto err;
	}
	++ks->ks_value;
	if (ks->ks_waiters > 0)
		cv_signal(&ks->ks_cv);
	error = 0;
	vfs_timestamp(&ks->ks_ctime);
err:
	mtx_unlock(&sem_lock);
	fdrop(fp, td);
	return (error);
}

#ifndef _SYS_SYSPROTO_H_
struct ksem_wait_args {
	semid_t		id;
};
#endif
int
sys_ksem_wait(struct thread *td, struct ksem_wait_args *uap)
{

	return (kern_sem_wait(td, uap->id, 0, NULL));
}

#ifndef _SYS_SYSPROTO_H_
struct ksem_timedwait_args {
	semid_t		id;
	const struct timespec *abstime;
};
#endif
int
sys_ksem_timedwait(struct thread *td, struct ksem_timedwait_args *uap)
{
	struct timespec abstime;
	struct timespec *ts;
	int error;

	/*
	 * We allow a null timespec (wait forever).
	 */
	if (uap->abstime == NULL)
		ts = NULL;
	else {
		error = copyin(uap->abstime, &abstime, sizeof(abstime));
		if (error != 0)
			return (error);
		if (abstime.tv_nsec >= 1000000000 || abstime.tv_nsec < 0)
			return (EINVAL);
		ts = &abstime;
	}
	return (kern_sem_wait(td, uap->id, 0, ts));
}

#ifndef _SYS_SYSPROTO_H_
struct ksem_trywait_args {
	semid_t		id;
};
#endif
int
sys_ksem_trywait(struct thread *td, struct ksem_trywait_args *uap)
{

	return (kern_sem_wait(td, uap->id, 1, NULL));
}

static int
kern_sem_wait(struct thread *td, semid_t id, int tryflag,
    struct timespec *abstime)
{
	struct timespec ts1, ts2;
	struct timeval tv;
	cap_rights_t rights;
	struct file *fp;
	struct ksem *ks;
	int error;

	DP((">>> kern_sem_wait entered! pid=%d\n", (int)td->td_proc->p_pid));
	AUDIT_ARG_FD(id);
	error = ksem_get(td, id, cap_rights_init(&rights, CAP_SEM_WAIT), &fp);
	if (error)
		return (error);
	ks = fp->f_data;
	mtx_lock(&sem_lock);
	DP((">>> kern_sem_wait critical section entered! pid=%d\n",
	    (int)td->td_proc->p_pid));
#ifdef MAC
	error = mac_posixsem_check_wait(td->td_ucred, fp->f_cred, ks);
	if (error) {
		DP(("kern_sem_wait mac failed\n"));
		goto err;
	}
#endif
	DP(("kern_sem_wait value = %d, tryflag %d\n", ks->ks_value, tryflag));
	vfs_timestamp(&ks->ks_atime);
	while (ks->ks_value == 0) {
		ks->ks_waiters++;
		if (tryflag != 0)
			error = EAGAIN;
		else if (abstime == NULL)
			error = cv_wait_sig(&ks->ks_cv, &sem_lock);
		else {
			for (;;) {
				ts1 = *abstime;
				getnanotime(&ts2);
				timespecsub(&ts1, &ts2, &ts1);
				TIMESPEC_TO_TIMEVAL(&tv, &ts1);
				if (tv.tv_sec < 0) {
					error = ETIMEDOUT;
					break;
				}
				error = cv_timedwait_sig(&ks->ks_cv,
				    &sem_lock, tvtohz(&tv));
				if (error != EWOULDBLOCK)
					break;
			}
		}
		ks->ks_waiters--;
		if (error)
			goto err;
	}
	ks->ks_value--;
	DP(("kern_sem_wait value post-decrement = %d\n", ks->ks_value));
	error = 0;
err:
	mtx_unlock(&sem_lock);
	fdrop(fp, td);
	DP(("<<< kern_sem_wait leaving, pid=%d, error = %d\n",
	    (int)td->td_proc->p_pid, error));
	return (error);
}

#ifndef _SYS_SYSPROTO_H_
struct ksem_getvalue_args {
	semid_t		id;
	int		*val;
};
#endif
int
sys_ksem_getvalue(struct thread *td, struct ksem_getvalue_args *uap)
{
	cap_rights_t rights;
	struct file *fp;
	struct ksem *ks;
	int error, val;

	AUDIT_ARG_FD(uap->id);
	error = ksem_get(td, uap->id,
	    cap_rights_init(&rights, CAP_SEM_GETVALUE), &fp);
	if (error)
		return (error);
	ks = fp->f_data;

	mtx_lock(&sem_lock);
#ifdef MAC
	error = mac_posixsem_check_getvalue(td->td_ucred, fp->f_cred, ks);
	if (error) {
		mtx_unlock(&sem_lock);
		fdrop(fp, td);
		return (error);
	}
#endif
	val = ks->ks_value;
	vfs_timestamp(&ks->ks_atime);
	mtx_unlock(&sem_lock);
	fdrop(fp, td);
	error = copyout(&val, uap->val, sizeof(val));
	return (error);
}

#ifndef _SYS_SYSPROTO_H_
struct ksem_destroy_args {
	semid_t		id;
};
#endif
int
sys_ksem_destroy(struct thread *td, struct ksem_destroy_args *uap)
{
	struct file *fp;
	struct ksem *ks;
	int error;

	/* No capability rights required to close a semaphore. */
	AUDIT_ARG_FD(uap->id);
	error = ksem_get(td, uap->id, &cap_no_rights, &fp);
	if (error)
		return (error);
	ks = fp->f_data;
	if (!(ks->ks_flags & KS_ANONYMOUS)) {
		fdrop(fp, td);
		return (EINVAL);
	}
	mtx_lock(&sem_lock);
	if (ks->ks_waiters != 0) {
		mtx_unlock(&sem_lock);
		error = EBUSY;
		goto err;
	}
	ks->ks_flags |= KS_DEAD;
	mtx_unlock(&sem_lock);

	error = kern_close(td, uap->id);
err:
	fdrop(fp, td);
	return (error);
}

static struct syscall_helper_data ksem_syscalls[] = {
	SYSCALL_INIT_HELPER(ksem_init),
	SYSCALL_INIT_HELPER(ksem_open),
	SYSCALL_INIT_HELPER(ksem_unlink),
	SYSCALL_INIT_HELPER(ksem_close),
	SYSCALL_INIT_HELPER(ksem_post),
	SYSCALL_INIT_HELPER(ksem_wait),
	SYSCALL_INIT_HELPER(ksem_timedwait),
	SYSCALL_INIT_HELPER(ksem_trywait),
	SYSCALL_INIT_HELPER(ksem_getvalue),
	SYSCALL_INIT_HELPER(ksem_destroy),
	SYSCALL_INIT_LAST
};

#ifdef COMPAT_FREEBSD32
#include <compat/freebsd32/freebsd32.h>
#include <compat/freebsd32/freebsd32_proto.h>
#include <compat/freebsd32/freebsd32_signal.h>
#include <compat/freebsd32/freebsd32_syscall.h>
#include <compat/freebsd32/freebsd32_util.h>

int
freebsd32_ksem_init(struct thread *td, struct freebsd32_ksem_init_args *uap)
{

	return (ksem_create(td, NULL, uap->idp, S_IRWXU | S_IRWXG, uap->value,
	    0, 1));
}

int
freebsd32_ksem_open(struct thread *td, struct freebsd32_ksem_open_args *uap)
{

	if ((uap->oflag & ~(O_CREAT | O_EXCL)) != 0)
		return (EINVAL);
	return (ksem_create(td, uap->name, uap->idp, uap->mode, uap->value,
	    uap->oflag, 1));
}

int
freebsd32_ksem_timedwait(struct thread *td,
    struct freebsd32_ksem_timedwait_args *uap)
{
	struct timespec32 abstime32;
	struct timespec *ts, abstime;
	int error;

	/*
	 * We allow a null timespec (wait forever).
	 */
	if (uap->abstime == NULL)
		ts = NULL;
	else {
		error = copyin(uap->abstime, &abstime32, sizeof(abstime32));
		if (error != 0)
			return (error);
		CP(abstime32, abstime, tv_sec);
		CP(abstime32, abstime, tv_nsec);
		if (abstime.tv_nsec >= 1000000000 || abstime.tv_nsec < 0)
			return (EINVAL);
		ts = &abstime;
	}
	return (kern_sem_wait(td, uap->id, 0, ts));
}

static struct syscall_helper_data ksem32_syscalls[] = {
	SYSCALL32_INIT_HELPER(freebsd32_ksem_init),
	SYSCALL32_INIT_HELPER(freebsd32_ksem_open),
	SYSCALL32_INIT_HELPER_COMPAT(ksem_unlink),
	SYSCALL32_INIT_HELPER_COMPAT(ksem_close),
	SYSCALL32_INIT_HELPER_COMPAT(ksem_post),
	SYSCALL32_INIT_HELPER_COMPAT(ksem_wait),
	SYSCALL32_INIT_HELPER(freebsd32_ksem_timedwait),
	SYSCALL32_INIT_HELPER_COMPAT(ksem_trywait),
	SYSCALL32_INIT_HELPER_COMPAT(ksem_getvalue),
	SYSCALL32_INIT_HELPER_COMPAT(ksem_destroy),
	SYSCALL_INIT_LAST
};
#endif

static int
ksem_module_init(void)
{
	int error;

	mtx_init(&sem_lock, "sem", NULL, MTX_DEF);
	mtx_init(&ksem_count_lock, "ksem count", NULL, MTX_DEF);
	sx_init(&ksem_dict_lock, "ksem dictionary");
	ksem_dictionary = hashinit(1024, M_KSEM, &ksem_hash);
	p31b_setcfg(CTL_P1003_1B_SEMAPHORES, 200112L);
	p31b_setcfg(CTL_P1003_1B_SEM_NSEMS_MAX, SEM_MAX);
	p31b_setcfg(CTL_P1003_1B_SEM_VALUE_MAX, SEM_VALUE_MAX);

	error = syscall_helper_register(ksem_syscalls, SY_THR_STATIC_KLD);
	if (error)
		return (error);
#ifdef COMPAT_FREEBSD32
	error = syscall32_helper_register(ksem32_syscalls, SY_THR_STATIC_KLD);
	if (error)
		return (error);
#endif
	return (0);
}

static void
ksem_module_destroy(void)
{

#ifdef COMPAT_FREEBSD32
	syscall32_helper_unregister(ksem32_syscalls);
#endif
	syscall_helper_unregister(ksem_syscalls);

	p31b_setcfg(CTL_P1003_1B_SEMAPHORES, 0);
	hashdestroy(ksem_dictionary, M_KSEM, ksem_hash);
	sx_destroy(&ksem_dict_lock);
	mtx_destroy(&ksem_count_lock);
	mtx_destroy(&sem_lock);
	p31b_unsetcfg(CTL_P1003_1B_SEM_VALUE_MAX);
	p31b_unsetcfg(CTL_P1003_1B_SEM_NSEMS_MAX);
}

static int
sem_modload(struct module *module, int cmd, void *arg)
{
        int error = 0;

        switch (cmd) {
        case MOD_LOAD:
		error = ksem_module_init();
		if (error)
			ksem_module_destroy();
                break;

        case MOD_UNLOAD:
		mtx_lock(&ksem_count_lock);
		if (nsems != 0) {
			error = EOPNOTSUPP;
			mtx_unlock(&ksem_count_lock);
			break;
		}
		ksem_dead = 1;
		mtx_unlock(&ksem_count_lock);
		ksem_module_destroy();
                break;

        case MOD_SHUTDOWN:
                break;
        default:
                error = EINVAL;
                break;
        }
        return (error);
}

static moduledata_t sem_mod = {
        "sem",
        &sem_modload,
        NULL
};

DECLARE_MODULE(sem, sem_mod, SI_SUB_SYSV_SEM, SI_ORDER_FIRST);
MODULE_VERSION(sem, 1);
