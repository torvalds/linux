/*	$OpenBSD: vfs_syscalls.c,v 1.378 2025/09/20 13:53:36 mpi Exp $	*/
/*	$NetBSD: vfs_syscalls.c,v 1.71 1996/04/23 10:29:02 mycroft Exp $	*/

/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)vfs_syscalls.c	8.28 (Berkeley) 12/10/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/namei.h>
#include <sys/filedesc.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/lock.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/pledge.h>
#include <sys/uio.h>
#include <sys/malloc.h>
#include <sys/pool.h>
#include <sys/ktrace.h>
#include <sys/unistd.h>
#include <sys/specdev.h>
#include <sys/resourcevar.h>
#include <sys/signalvar.h>

#include <sys/syscallargs.h>

static int change_dir(struct nameidata *, struct proc *);

void checkdirs(struct vnode *);

int copyout_statfs(struct statfs *, void *, struct proc *);

int doopenat(struct proc *, int, const char *, int, mode_t, register_t *);
int domknodat(struct proc *, int, const char *, mode_t, dev_t);
int dolinkat(struct proc *, int, const char *, int, const char *, int);
int dosymlinkat(struct proc *, const char *, int, const char *);
int dounlinkat(struct proc *, int, const char *, int);
int dofaccessat(struct proc *, int, const char *, int, int);
int dofstatat(struct proc *, int, const char *, struct stat *, int);
int dopathconfat(struct proc *, int, const char *, int, int, register_t *);
int doreadlinkat(struct proc *, int, const char *, char *, size_t,
    register_t *);
int dochflagsat(struct proc *, int, const char *, u_int, int);
int dovchflags(struct proc *, struct vnode *, u_int);
int dofchmodat(struct proc *, int, const char *, mode_t, int);
int dofchownat(struct proc *, int, const char *, uid_t, gid_t, int);
int dorenameat(struct proc *, int, const char *, int, const char *);
int domkdirat(struct proc *, int, const char *, mode_t);
int doutimensat(struct proc *, int, const char *, struct timespec [2], int);
int dovutimens(struct proc *, struct vnode *, struct timespec [2]);
int dofutimens(struct proc *, int, struct timespec [2]);
int dounmount_leaf(struct mount *, int, struct proc *);

/*
 * Virtual File System System Calls
 */

/*
 * Mount a file system.
 */
int
sys_mount(struct proc *p, void *v, register_t *retval)
{
	struct sys_mount_args /* {
		syscallarg(const char *) type;
		syscallarg(const char *) path;
		syscallarg(int) flags;
		syscallarg(void *) data;
	} */ *uap = v;
	struct vnode *vp;
	struct mount *mp;
	int error, mntflag = 0;
	char fstypename[MFSNAMELEN];
	char fspath[MNAMELEN];
	struct nameidata nd;
	struct vfsconf *vfsp;
	int flags = SCARG(uap, flags);
	void *args = NULL;

	if ((error = suser(p)))
		return (error);

	/*
	 * Mount points must fit in MNAMELEN, not MAXPATHLEN.
	 */
	error = copyinstr(SCARG(uap, path), fspath, MNAMELEN, NULL);
	if (error)
		return(error);

	/*
	 * Get vnode to be covered
	 */
	NDINIT(&nd, LOOKUP, FOLLOW | LOCKLEAF, UIO_SYSSPACE, fspath, p);
	if ((error = namei(&nd)) != 0)
		goto fail;
	vp = nd.ni_vp;
	if (flags & MNT_UPDATE) {
		if ((vp->v_flag & VROOT) == 0) {
			vput(vp);
			error = EINVAL;
			goto fail;
		}
		mp = vp->v_mount;
		vfsp = mp->mnt_vfc;

		args = malloc(vfsp->vfc_datasize, M_TEMP, M_WAITOK | M_ZERO);
		error = copyin(SCARG(uap, data), args, vfsp->vfc_datasize);
		if (error) {
			vput(vp);
			goto fail;
		}

		mntflag = mp->mnt_flag;
		/*
		 * We only allow the filesystem to be reloaded if it
		 * is currently mounted read-only.
		 */
		if ((flags & MNT_RELOAD) &&
		    ((mp->mnt_flag & MNT_RDONLY) == 0)) {
			vput(vp);
			error = EOPNOTSUPP;	/* Needs translation */
			goto fail;
		}

		if ((error = vfs_busy(mp, VB_READ|VB_NOWAIT)) != 0) {
			vput(vp);
			goto fail;
		}
		mp->mnt_flag |= flags & (MNT_RELOAD | MNT_UPDATE);
		goto update;
	}
	/*
	 * Do not allow disabling of permission checks unless exec and access to
	 * device files is disabled too.
	 */
	if ((flags & MNT_NOPERM) &&
	    (flags & (MNT_NODEV | MNT_NOEXEC)) != (MNT_NODEV | MNT_NOEXEC)) {
		vput(vp);
		error = EPERM;
		goto fail;
	}
	if ((error = vinvalbuf(vp, V_SAVE, p->p_ucred, p, 0, INFSLP)) != 0) {
		vput(vp);
		goto fail;
	}
	if (vp->v_type != VDIR) {
		vput(vp);
		goto fail;
	}
	error = copyinstr(SCARG(uap, type), fstypename, MFSNAMELEN, NULL);
	if (error) {
		vput(vp);
		goto fail;
	}
	vfsp = vfs_byname(fstypename);
	if (vfsp == NULL) {
		vput(vp);
		error = EOPNOTSUPP;
		goto fail;
	}

	args = malloc(vfsp->vfc_datasize, M_TEMP, M_WAITOK | M_ZERO);
	error = copyin(SCARG(uap, data), args, vfsp->vfc_datasize);
	if (error) {
		vput(vp);
		goto fail;
	}

	if (vp->v_mountedhere != NULL) {
		vput(vp);
		error = EBUSY;
		goto fail;
	}

	/*
	 * Allocate and initialize the file system.
	 */
	mp = vfs_mount_alloc(vp, vfsp);
	mp->mnt_stat.f_owner = p->p_ucred->cr_uid;

update:
	/* Ensure that the parent mountpoint does not get unmounted. */
	error = vfs_busy(vp->v_mount, VB_READ|VB_NOWAIT|VB_DUPOK);
	if (error) {
		if (mp->mnt_flag & MNT_UPDATE) {
			mp->mnt_flag = mntflag;
			vfs_unbusy(mp);
		} else {
			vfs_unbusy(mp);
			vfs_mount_free(mp);
		}
		vput(vp);
		goto fail;
	}

	/*
	 * Set the mount level flags.
	 */
	if (flags & MNT_RDONLY)
		mp->mnt_flag |= MNT_RDONLY;
	else if (mp->mnt_flag & MNT_RDONLY)
		mp->mnt_flag |= MNT_WANTRDWR;
	mp->mnt_flag &=~ (MNT_NOSUID | MNT_NOEXEC | MNT_WXALLOWED | MNT_NODEV |
	    MNT_SYNCHRONOUS | MNT_ASYNC | MNT_NOATIME | MNT_NOPERM | MNT_FORCE);
	mp->mnt_flag |= flags & (MNT_NOSUID | MNT_NOEXEC | MNT_WXALLOWED |
	    MNT_NODEV | MNT_SYNCHRONOUS | MNT_ASYNC | MNT_NOATIME | MNT_NOPERM |
	    MNT_FORCE);
	/*
	 * Mount the filesystem.
	 */
	error = VFS_MOUNT(mp, fspath, args, &nd, p);
	if (!error) {
		mp->mnt_stat.f_ctime = gettime();
	}
	if (mp->mnt_flag & MNT_UPDATE) {
		vfs_unbusy(vp->v_mount);
		vput(vp);
		if (mp->mnt_flag & MNT_WANTRDWR)
			mp->mnt_flag &= ~MNT_RDONLY;
		mp->mnt_flag &= ~MNT_OP_FLAGS;
		if (error)
			mp->mnt_flag = mntflag;

		if ((mp->mnt_flag & MNT_RDONLY) == 0) {
			if (mp->mnt_syncer == NULL)
				error = vfs_allocate_syncvnode(mp);
		} else {
			if (mp->mnt_syncer != NULL)
				vgone(mp->mnt_syncer);
			mp->mnt_syncer = NULL;
		}

		vfs_unbusy(mp);
		goto fail;
	}

	mp->mnt_flag &= ~MNT_OP_FLAGS;
	vp->v_mountedhere = mp;

	/*
	 * Put the new filesystem on the mount list after root.
	 */
	cache_purge(vp);
	if (!error) {
		TAILQ_INSERT_TAIL(&mountlist, mp, mnt_list);
		checkdirs(vp);
		vfs_unbusy(vp->v_mount);
		VOP_UNLOCK(vp);
		if ((mp->mnt_flag & MNT_RDONLY) == 0)
			error = vfs_allocate_syncvnode(mp);
		vfs_unbusy(mp);
		(void) VFS_STATFS(mp, &mp->mnt_stat, p);
		if ((error = VFS_START(mp, 0, p)) != 0)
			vrele(vp);
	} else {
		mp->mnt_vnodecovered->v_mountedhere = NULL;
		vfs_unbusy(mp);
		vfs_mount_free(mp);
		vfs_unbusy(vp->v_mount);
		vput(vp);
	}
fail:
	if (args)
		free(args, M_TEMP, vfsp->vfc_datasize);
	return (error);
}

/*
 * Scan all active processes to see if any of them have a current
 * or root directory onto which the new filesystem has just been
 * mounted. If so, replace them with the new mount point, keeping
 * track of how many were replaced.  That's the number of references
 * the old vnode had that we've replaced, so finish by vrele()'ing
 * it that many times.  This puts off any possible sleeping until
 * we've finished walking the allprocess list.
 */
void
checkdirs(struct vnode *olddp)
{
	struct filedesc *fdp;
	struct vnode *newdp;
	struct process *pr;
	u_int  free_count = 0;

	if (olddp->v_usecount == 1)
		return;
	if (VFS_ROOT(olddp->v_mountedhere, &newdp))
		panic("mount: lost mount");
	LIST_FOREACH(pr, &allprocess, ps_list) {
		fdp = pr->ps_fd;
		if (fdp->fd_cdir == olddp) {
			free_count++;
			vref(newdp);
			fdp->fd_cdir = newdp;
		}
		if (fdp->fd_rdir == olddp) {
			free_count++;
			vref(newdp);
			fdp->fd_rdir = newdp;
		}
	}
	if (rootvnode == olddp) {
		free_count++;
		vref(newdp);
		rootvnode = newdp;
	}
	while (free_count-- > 0)
		vrele(olddp);
	vput(newdp);
}

/*
 * Unmount a file system.
 *
 * Note: unmount takes a path to the vnode mounted on as argument,
 * not special file (as before).
 */
int
sys_unmount(struct proc *p, void *v, register_t *retval)
{
	struct sys_unmount_args /* {
		syscallarg(const char *) path;
		syscallarg(int) flags;
	} */ *uap = v;
	struct vnode *vp;
	struct mount *mp;
	int error;
	struct nameidata nd;

	if ((error = suser(p)) != 0)
		return (error);

	NDINIT(&nd, LOOKUP, FOLLOW | LOCKLEAF, UIO_USERSPACE,
	    SCARG(uap, path), p);
	if ((error = namei(&nd)) != 0)
		return (error);
	vp = nd.ni_vp;
	mp = vp->v_mount;

	/*
	 * Don't allow unmounting the root file system.
	 */
	if (mp->mnt_flag & MNT_ROOTFS) {
		vput(vp);
		return (EINVAL);
	}

	/*
	 * Must be the root of the filesystem
	 */
	if ((vp->v_flag & VROOT) == 0) {
		vput(vp);
		return (EINVAL);
	}
	vput(vp);

	if (vfs_busy(mp, VB_WRITE|VB_WAIT))
		return (EBUSY);

	return (dounmount(mp, SCARG(uap, flags) & MNT_FORCE, p));
}

/*
 * Do the actual file system unmount.
 */
int
dounmount(struct mount *mp, int flags, struct proc *p)
{
	SLIST_HEAD(, mount) mplist;
	struct mount *nmp;
	int error;

	SLIST_INIT(&mplist);
	SLIST_INSERT_HEAD(&mplist, mp, mnt_dounmount);

	/*
	 * Collect nested mount points. This takes advantage of the mount list
	 * being ordered - nested mount points come after their parent.
	 */
	while ((mp = TAILQ_NEXT(mp, mnt_list)) != NULL) {
		SLIST_FOREACH(nmp, &mplist, mnt_dounmount) {
			if (mp->mnt_vnodecovered == NULL ||
			    mp->mnt_vnodecovered->v_mount != nmp)
				continue;

			if ((flags & MNT_FORCE) == 0) {
				error = EBUSY;
				goto err;
			}
			error = vfs_busy(mp, VB_WRITE|VB_WAIT|VB_DUPOK);
			if (error) {
				if ((flags & MNT_DOOMED)) {
					/*
					 * If the mount point was busy due to
					 * being unmounted, it has been removed
					 * from the mount list already.
					 * Restart the iteration from the last
					 * collected busy entry.
					 */
					mp = SLIST_FIRST(&mplist);
					break;
				}
				goto err;
			}
			SLIST_INSERT_HEAD(&mplist, mp, mnt_dounmount);
			break;
		}
	}

	/*
	 * Nested mount points cannot appear during this loop as mounting
	 * requires a read lock for the parent mount point.
	 */
	while ((mp = SLIST_FIRST(&mplist)) != NULL) {
		SLIST_REMOVE(&mplist, mp, mount, mnt_dounmount);
		error = dounmount_leaf(mp, flags, p);
		if (error)
			goto err;
	}
	return (0);

err:
	while ((mp = SLIST_FIRST(&mplist)) != NULL) {
		SLIST_REMOVE(&mplist, mp, mount, mnt_dounmount);
		vfs_unbusy(mp);
	}
	return (error);
}

int
dounmount_leaf(struct mount *mp, int flags, struct proc *p)
{
	struct vnode *coveredvp;
	struct vnode *vp, *nvp;
	int error;
	int hadsyncer = 0;

	mp->mnt_flag &=~ MNT_ASYNC;
	cache_purgevfs(mp);	/* remove cache entries for this file sys */
	if (mp->mnt_syncer != NULL) {
		hadsyncer = 1;
		vgone(mp->mnt_syncer);
		mp->mnt_syncer = NULL;
	}

	/*
	 * Before calling file system unmount, make sure
	 * all unveils to vnodes in here are dropped.
	 */
	TAILQ_FOREACH_SAFE(vp , &mp->mnt_vnodelist, v_mntvnodes, nvp) {
		unveil_removevnode(vp);
	}

	if (((mp->mnt_flag & MNT_RDONLY) ||
	    (error = VFS_SYNC(mp, MNT_WAIT, 0, p->p_ucred, p)) == 0) ||
	    (flags & MNT_FORCE))
		error = VFS_UNMOUNT(mp, flags, p);

	if (error && !(flags & MNT_DOOMED)) {
		if ((mp->mnt_flag & MNT_RDONLY) == 0 && hadsyncer)
			(void) vfs_allocate_syncvnode(mp);
		vfs_unbusy(mp);
		return (error);
	}

	TAILQ_REMOVE(&mountlist, mp, mnt_list);
	if ((coveredvp = mp->mnt_vnodecovered) != NULL) {
		coveredvp->v_mountedhere = NULL;
		vrele(coveredvp);
	}

	if (!TAILQ_EMPTY(&mp->mnt_vnodelist))
		panic("unmount: dangling vnode");

	vfs_unbusy(mp);
	vfs_mount_free(mp);

	return (0);
}

/*
 * Sync each mounted filesystem.
 */
int
sys_sync(struct proc *p, void *v, register_t *retval)
{
	struct mount *mp;
	int asyncflag;

	TAILQ_FOREACH_REVERSE(mp, &mountlist, mntlist, mnt_list) {
		if (vfs_busy(mp, VB_READ|VB_NOWAIT))
			continue;
		if ((mp->mnt_flag & MNT_RDONLY) == 0) {
			asyncflag = mp->mnt_flag & MNT_ASYNC;
			mp->mnt_flag &= ~MNT_ASYNC;
			uvm_vnp_sync(mp);
			VFS_SYNC(mp, MNT_NOWAIT, 0, p->p_ucred, p);
			if (asyncflag)
				mp->mnt_flag |= MNT_ASYNC;
		}
		vfs_unbusy(mp);
	}

	return (0);
}

/*
 * Change filesystem quotas.
 */
int
sys_quotactl(struct proc *p, void *v, register_t *retval)
{
	struct sys_quotactl_args /* {
		syscallarg(const char *) path;
		syscallarg(int) cmd;
		syscallarg(int) uid;
		syscallarg(char *) arg;
	} */ *uap = v;
	struct mount *mp;
	int error;
	struct nameidata nd;

	NDINIT(&nd, LOOKUP, FOLLOW, UIO_USERSPACE, SCARG(uap, path), p);
	if ((error = namei(&nd)) != 0)
		return (error);
	mp = nd.ni_vp->v_mount;
	vrele(nd.ni_vp);
	return (VFS_QUOTACTL(mp, SCARG(uap, cmd), SCARG(uap, uid),
	    SCARG(uap, arg), p));
}

int
copyout_statfs(struct statfs *sp, void *uaddr, struct proc *p)
{
	size_t co_sz1 = offsetof(struct statfs, f_fsid);
	size_t co_off2 = co_sz1 + sizeof(fsid_t);
	size_t co_sz2 = sizeof(struct statfs) - co_off2;
	char *s, *d;
	int error;

	/* Don't let non-root see filesystem id (for NFS security) */
	if (suser(p)) {
		fsid_t fsid;

		s = (char *)sp;
		d = (char *)uaddr;

		memset(&fsid, 0, sizeof(fsid));

		if ((error = copyout(s, d, co_sz1)) != 0)
			return (error);
		if ((error = copyout(&fsid, d + co_sz1, sizeof(fsid))) != 0)
			return (error);
		return (copyout(s + co_off2, d + co_off2, co_sz2));
	}

	return (copyout(sp, uaddr, sizeof(*sp)));
}

/*
 * Get filesystem statistics.
 */
int
sys_statfs(struct proc *p, void *v, register_t *retval)
{
	struct sys_statfs_args /* {
		syscallarg(const char *) path;
		syscallarg(struct statfs *) buf;
	} */ *uap = v;
	struct mount *mp;
	struct statfs *sp;
	int error;
	struct nameidata nd;

	NDINIT(&nd, LOOKUP, FOLLOW | BYPASSUNVEIL, UIO_USERSPACE,
	    SCARG(uap, path), p);
	nd.ni_pledge = PLEDGE_RPATH;
	nd.ni_unveil = UNVEIL_READ;
	if ((error = namei(&nd)) != 0)
		return (error);
	mp = nd.ni_vp->v_mount;
	sp = &mp->mnt_stat;
	vrele(nd.ni_vp);
	if ((error = VFS_STATFS(mp, sp, p)) != 0)
		return (error);
	sp->f_flags = mp->mnt_flag & MNT_VISFLAGMASK;

	return (copyout_statfs(sp, SCARG(uap, buf), p));
}

/*
 * Get filesystem statistics.
 */
int
sys_fstatfs(struct proc *p, void *v, register_t *retval)
{
	struct sys_fstatfs_args /* {
		syscallarg(int) fd;
		syscallarg(struct statfs *) buf;
	} */ *uap = v;
	struct file *fp;
	struct mount *mp;
	struct statfs *sp;
	int error;

	if ((error = getvnode(p, SCARG(uap, fd), &fp)) != 0)
		return (error);
	mp = ((struct vnode *)fp->f_data)->v_mount;
	if (!mp) {
		FRELE(fp, p);
		return (ENOENT);
	}
	sp = &mp->mnt_stat;
	error = VFS_STATFS(mp, sp, p);
	FRELE(fp, p);
	if (error)
		return (error);
	sp->f_flags = mp->mnt_flag & MNT_VISFLAGMASK;

	return (copyout_statfs(sp, SCARG(uap, buf), p));
}

/*
 * Get statistics on all filesystems.
 */
int
sys_getfsstat(struct proc *p, void *v, register_t *retval)
{
	struct sys_getfsstat_args /* {
		syscallarg(struct statfs *) buf;
		syscallarg(size_t) bufsize;
		syscallarg(int) flags;
	} */ *uap = v;
	struct mount *mp;
	struct statfs *sp;
	struct statfs *sfsp;
	size_t count, maxcount;
	int error, flags = SCARG(uap, flags);

	maxcount = SCARG(uap, bufsize) / sizeof(struct statfs);
	sfsp = SCARG(uap, buf);
	count = 0;

	TAILQ_FOREACH(mp, &mountlist, mnt_list) {
		if (vfs_busy(mp, VB_READ|VB_NOWAIT))
			continue;
		if (sfsp && count < maxcount) {
			sp = &mp->mnt_stat;

			/* Refresh stats unless MNT_NOWAIT is specified */
			if (flags != MNT_NOWAIT &&
			    flags != MNT_LAZY &&
			    (flags == MNT_WAIT ||
			    flags == 0) &&
			    (error = VFS_STATFS(mp, sp, p))) {
				vfs_unbusy(mp);
				continue;
			}

			sp->f_flags = mp->mnt_flag & MNT_VISFLAGMASK;
			error = (copyout_statfs(sp, sfsp, p));
			if (error) {
				vfs_unbusy(mp);
				return (error);
			}
			sfsp++;
		}
		count++;
		vfs_unbusy(mp);
	}

	if (sfsp && count > maxcount)
		*retval = maxcount;
	else
		*retval = count;

	return (0);
}

/*
 * Change current working directory to a given file descriptor.
 */
int
sys_fchdir(struct proc *p, void *v, register_t *retval)
{
	struct sys_fchdir_args /* {
		syscallarg(int) fd;
	} */ *uap = v;
	struct filedesc *fdp = p->p_fd;
	struct vnode *vp, *tdp, *old_cdir;
	struct mount *mp;
	struct file *fp;
	int error;

	if ((fp = fd_getfile(fdp, SCARG(uap, fd))) == NULL)
		return (EBADF);
	vp = fp->f_data;
	if (fp->f_type != DTYPE_VNODE || vp->v_type != VDIR) {
		FRELE(fp, p);
		return (ENOTDIR);
	}
	vref(vp);
	FRELE(fp, p);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	error = VOP_ACCESS(vp, VEXEC, p->p_ucred, p);

	while (!error && (mp = vp->v_mountedhere) != NULL) {
		if (vfs_busy(mp, VB_READ|VB_WAIT))
			continue;
		error = VFS_ROOT(mp, &tdp);
		vfs_unbusy(mp);
		if (error)
			break;
		vput(vp);
		vp = tdp;
	}
	if (error) {
		vput(vp);
		return (error);
	}
	VOP_UNLOCK(vp);
	old_cdir = fdp->fd_cdir;
	fdp->fd_cdir = vp;
	vrele(old_cdir);
	return (0);
}

/*
 * Change current working directory (``.'').
 */
int
sys_chdir(struct proc *p, void *v, register_t *retval)
{
	struct sys_chdir_args /* {
		syscallarg(const char *) path;
	} */ *uap = v;
	struct filedesc *fdp = p->p_fd;
	struct vnode *old_cdir;
	int error;
	struct nameidata nd;

	NDINIT(&nd, LOOKUP, FOLLOW | LOCKLEAF, UIO_USERSPACE,
	    SCARG(uap, path), p);
	nd.ni_pledge = PLEDGE_RPATH;
	nd.ni_unveil = UNVEIL_READ;
	if ((error = change_dir(&nd, p)) != 0)
		return (error);
	old_cdir = fdp->fd_cdir;
	fdp->fd_cdir = nd.ni_vp;
	vrele(old_cdir);
	return (0);
}

/*
 * Change notion of root (``/'') directory.
 */
int
sys_chroot(struct proc *p, void *v, register_t *retval)
{
	struct sys_chroot_args /* {
		syscallarg(const char *) path;
	} */ *uap = v;
	struct filedesc *fdp = p->p_fd;
	struct vnode *old_cdir, *old_rdir;
	int error;
	struct nameidata nd;

	if ((error = suser(p)) != 0)
		return (error);
	NDINIT(&nd, LOOKUP, FOLLOW | LOCKLEAF, UIO_USERSPACE,
	    SCARG(uap, path), p);
	if ((error = change_dir(&nd, p)) != 0)
		return (error);
	if (fdp->fd_rdir != NULL) {
		/*
		 * A chroot() done inside a changed root environment does
		 * an automatic chdir to avoid the out-of-tree experience.
		 */
		vref(nd.ni_vp);
		old_rdir = fdp->fd_rdir;
		old_cdir = fdp->fd_cdir;
		fdp->fd_rdir = fdp->fd_cdir = nd.ni_vp;
		vrele(old_rdir);
		vrele(old_cdir);
	} else
		fdp->fd_rdir = nd.ni_vp;
	atomic_setbits_int(&p->p_p->ps_flags, PS_CHROOT);
	return (0);
}

/*
 * Common routine for chroot and chdir.
 */
static int
change_dir(struct nameidata *ndp, struct proc *p)
{
	struct vnode *vp;
	int error;

	if ((error = namei(ndp)) != 0)
		return (error);
	vp = ndp->ni_vp;
	if (vp->v_type != VDIR)
		error = ENOTDIR;
	else
		error = VOP_ACCESS(vp, VEXEC, p->p_ucred, p);
	if (error)
		vput(vp);
	else
		VOP_UNLOCK(vp);
	return (error);
}

int
sys___realpath(struct proc *p, void *v, register_t *retval)
{
	struct sys___realpath_args /* {
		syscallarg(const char *) pathname;
		syscallarg(char *) resolved;
	} */ *uap = v;
	char *pathname;
	char *rpbuf;
	struct nameidata nd;
	size_t pathlen;
	int error = 0;

	if (SCARG(uap, pathname) == NULL)
		return (EINVAL);

	pathname = pool_get(&namei_pool, PR_WAITOK);
	rpbuf = pool_get(&namei_pool, PR_WAITOK);

	if ((error = copyinstr(SCARG(uap, pathname), pathname, MAXPATHLEN,
	    &pathlen)))
		goto end;

	if (pathlen == 1) { /* empty string "" */
		error = ENOENT;
		goto end;
	}
	if (pathlen < 2) {
		error = EINVAL;
		goto end;
	}

	/* Get cwd for relative path if needed, prepend to rpbuf */
	rpbuf[0] = '\0';
	if (pathname[0] != '/') {
		int cwdlen = MAXPATHLEN * 4; /* for vfs_getcwd_common */
		char *cwdbuf, *bp;

		cwdbuf = malloc(cwdlen, M_TEMP, M_WAITOK);

		/* vfs_getcwd_common fills this in backwards */
		bp = &cwdbuf[cwdlen - 1];
		*bp = '\0';

		KERNEL_LOCK();
		error = vfs_getcwd_common(p->p_fd->fd_cdir, NULL, &bp, cwdbuf,
		    cwdlen/2, GETCWD_CHECK_ACCESS, p);
		KERNEL_UNLOCK();

		if (error) {
			free(cwdbuf, M_TEMP, cwdlen);
			goto end;
		}

		if (strlcpy(rpbuf, bp, MAXPATHLEN) >= MAXPATHLEN) {
			free(cwdbuf, M_TEMP, cwdlen);
			error = ENAMETOOLONG;
			goto end;
		}

		free(cwdbuf, M_TEMP, cwdlen);
	}

	NDINIT(&nd, LOOKUP, FOLLOW | SAVENAME | REALPATH, UIO_SYSSPACE,
	    pathname, p);

	nd.ni_cnd.cn_rpbuf = rpbuf;
	nd.ni_cnd.cn_rpi = strlen(rpbuf);

	nd.ni_pledge = PLEDGE_RPATH;
	nd.ni_unveil = UNVEIL_READ;
	KERNEL_LOCK();
	if ((error = namei(&nd)) != 0) {
		KERNEL_UNLOCK();
		goto end;
	}

	/* release reference from namei */
	if (nd.ni_vp)
		vrele(nd.ni_vp);
	KERNEL_UNLOCK();

	error = copyoutstr(nd.ni_cnd.cn_rpbuf, SCARG(uap, resolved),
	    MAXPATHLEN, NULL);

#ifdef KTRACE
	if (KTRPOINT(p, KTR_NAMEI))
		ktrnamei(p, nd.ni_cnd.cn_rpbuf);
#endif
	pool_put(&namei_pool, nd.ni_cnd.cn_pnbuf);
end:
	pool_put(&namei_pool, rpbuf);
	pool_put(&namei_pool, pathname);
	return (error);
}

int
sys_unveil(struct proc *p, void *v, register_t *retval)
{
	struct sys_unveil_args /* {
		syscallarg(const char *) path;
		syscallarg(const char *) permissions;
	} */ *uap = v;
	struct process *pr = p->p_p;
	char *pathname, *c;
	struct nameidata nd;
	size_t pathlen;
	char permissions[5];
	int error, allow;

	if (SCARG(uap, path) == NULL && SCARG(uap, permissions) == NULL) {
		pr->ps_uvdone = 1;
		return (0);
	}

	if (pr->ps_uvdone != 0)
		return EPERM;

	error = copyinstr(SCARG(uap, permissions), permissions,
	    sizeof(permissions), NULL);
	if (error)
		return (error);

	/*
	 * System calls in other threads may sleep between unveil
	 * datastructure inspections -- this is the simplest way to
	 * provide consistency 
	 */
	single_thread_set(p, SINGLE_UNWIND);

	pathname = pool_get(&namei_pool, PR_WAITOK);
	error = copyinstr(SCARG(uap, path), pathname, MAXPATHLEN, &pathlen);
	if (error)
		goto end;

#ifdef KTRACE
	if (KTRPOINT(p, KTR_STRUCT))
		ktrstruct(p, "unveil", permissions, strlen(permissions));
#endif
	if (pathlen < 2) {
		error = EINVAL;
		goto end;
	}

	/* find root "/" or "//" */
	for (c = pathname; *c != '\0'; c++) {
		if (*c != '/')
			break;
	}
	if (*c == '\0')
		/* root directory */
		NDINIT(&nd, LOOKUP, FOLLOW | LOCKLEAF | SAVENAME,
		    UIO_SYSSPACE, pathname, p);
	else
		NDINIT(&nd, CREATE, FOLLOW | LOCKLEAF | LOCKPARENT | SAVENAME,
		    UIO_SYSSPACE, pathname, p);

	nd.ni_pledge = PLEDGE_UNVEIL;
	if ((error = namei(&nd)) != 0)
		goto end;

	/*
	 * XXX Any access to the file or directory will allow us to
	 * pledge path it
	 */
	allow = ((nd.ni_vp &&
	    (VOP_ACCESS(nd.ni_vp, VREAD, p->p_ucred, p) == 0 ||
	    VOP_ACCESS(nd.ni_vp, VWRITE, p->p_ucred, p) == 0 ||
	    VOP_ACCESS(nd.ni_vp, VEXEC, p->p_ucred, p) == 0)) ||
	    (nd.ni_dvp &&
	    (VOP_ACCESS(nd.ni_dvp, VREAD, p->p_ucred, p) == 0 ||
	    VOP_ACCESS(nd.ni_dvp, VWRITE, p->p_ucred, p) == 0 ||
	    VOP_ACCESS(nd.ni_dvp, VEXEC, p->p_ucred, p) == 0)));

	/* release lock from namei, but keep ref */
	if (nd.ni_vp)
		VOP_UNLOCK(nd.ni_vp);
	if (nd.ni_dvp && nd.ni_dvp != nd.ni_vp)
		VOP_UNLOCK(nd.ni_dvp);

	if (allow)
		error = unveil_add(p, &nd, permissions);
	else
		error = EPERM;

	/* release vref from namei, but not vref from unveil_add */
	if (nd.ni_vp)
		vrele(nd.ni_vp);
	if (nd.ni_dvp)
		vrele(nd.ni_dvp);

	pool_put(&namei_pool, nd.ni_cnd.cn_pnbuf);
end:
	pool_put(&namei_pool, pathname);

	single_thread_clear(p);
	return (error);
}

/*
 * Check permissions, allocate an open file structure,
 * and call the device open routine if any.
 */
int
sys_open(struct proc *p, void *v, register_t *retval)
{
	struct sys_open_args /* {
		syscallarg(const char *) path;
		syscallarg(int) flags;
		syscallarg(mode_t) mode;
	} */ *uap = v;

	return (doopenat(p, AT_FDCWD, SCARG(uap, path), SCARG(uap, flags),
	    SCARG(uap, mode), retval));
}

int
sys_openat(struct proc *p, void *v, register_t *retval)
{
	struct sys_openat_args /* {
		syscallarg(int) fd;
		syscallarg(const char *) path;
		syscallarg(int) flags;
		syscallarg(mode_t) mode;
	} */ *uap = v;

	return (doopenat(p, SCARG(uap, fd), SCARG(uap, path),
	    SCARG(uap, flags), SCARG(uap, mode), retval));
}

int
doopenat(struct proc *p, int fd, const char *path, int oflags, mode_t mode,
    register_t *retval)
{
	struct filedesc *fdp = p->p_fd;
	struct file *fp;
	struct vnode *vp;
	struct vattr vattr;
	int flags, fdflags, cmode;
	int type, indx, error, localtrunc = 0;
	struct flock lf;
	struct nameidata nd;
	uint64_t ni_pledge = 0;
	u_char ni_unveil = 0;

	if (oflags & (O_EXLOCK | O_SHLOCK)) {
		error = pledge_flock(p);
		if (error != 0)
			return (error);
	}

	fdflags = ((oflags & O_CLOEXEC) ? UF_EXCLOSE : 0)
	    | ((oflags & O_CLOFORK) ? UF_FORKCLOSE : 0);

	fdplock(fdp);
	if ((error = falloc(p, &fp, &indx)) != 0) {
		fdpunlock(fdp);
		return (error);
	}
	fdpunlock(fdp);

	flags = FFLAGS(oflags);
	if (flags & FREAD) {
		ni_pledge |= PLEDGE_RPATH;
		ni_unveil |= UNVEIL_READ;
	}
	if (flags & FWRITE) {
		ni_pledge |= PLEDGE_WPATH;
		ni_unveil |= UNVEIL_WRITE;
	}
	if (oflags & O_CREAT) {
		ni_pledge |= PLEDGE_CPATH;
		ni_unveil |= UNVEIL_CREATE;
	}

	cmode = ((mode &~ fdp->fd_cmask) & ALLPERMS) &~ S_ISTXT;
	if ((p->p_p->ps_flags & PS_PLEDGE))
		cmode &= ACCESSPERMS;
	NDINITAT(&nd, 0, 0, UIO_USERSPACE, fd, path, p);
	nd.ni_pledge = ni_pledge;
	nd.ni_unveil = ni_unveil;
	p->p_dupfd = -1;			/* XXX check for fdopen */
	if ((flags & O_TRUNC) && (flags & (O_EXLOCK | O_SHLOCK))) {
		localtrunc = 1;
		flags &= ~O_TRUNC;	/* Must do truncate ourselves */
	}
	KERNEL_LOCK();
	if ((error = vn_open(&nd, flags, cmode)) != 0) {
		fdplock(fdp);
		if (error == ENODEV &&
		    p->p_dupfd >= 0 &&			/* XXX from fdopen */
		    (error =
			dupfdopen(p, indx, flags)) == 0) {
			*retval = indx;
			goto error;
		}
		if (error == ERESTART)
			error = EINTR;
		fdremove(fdp, indx);
		goto error;
	}
	p->p_dupfd = 0;
	vp = nd.ni_vp;
	fp->f_flag = flags & FMASK;
	fp->f_type = DTYPE_VNODE;
	fp->f_ops = &vnops;
	fp->f_data = vp;
	if (flags & (O_EXLOCK | O_SHLOCK)) {
		lf.l_whence = SEEK_SET;
		lf.l_start = 0;
		lf.l_len = 0;
		if (flags & O_EXLOCK)
			lf.l_type = F_WRLCK;
		else
			lf.l_type = F_RDLCK;
		type = F_FLOCK;
		if ((flags & FNONBLOCK) == 0)
			type |= F_WAIT;
		VOP_UNLOCK(vp);
		error = VOP_ADVLOCK(vp, (caddr_t)fp, F_SETLK, &lf, type);
		if (error) {
			fdplock(fdp);
			/* closef will vn_close the file for us. */
			fdremove(fdp, indx);
			goto error;
		}
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
		atomic_setbits_int(&fp->f_iflags, FIF_HASLOCK);
	}
	if (localtrunc) {
		if ((fp->f_flag & FWRITE) == 0)
			error = EACCES;
		else if (vp->v_mount && (vp->v_mount->mnt_flag & MNT_RDONLY))
			error = EROFS;
		else if (vp->v_type == VDIR)
			error = EISDIR;
		else if ((error = vn_writechk(vp)) == 0) {
			vattr_null(&vattr);
			vattr.va_size = 0;
			error = VOP_SETATTR(vp, &vattr, fp->f_cred, p);
		}
		if (error) {
			VOP_UNLOCK(vp);
			fdplock(fdp);
			/* closef will close the file for us. */
			fdremove(fdp, indx);
			goto error;
		}
	}
	VOP_UNLOCK(vp);
	KERNEL_UNLOCK();
	*retval = indx;
	fdplock(fdp);
	fdinsert(fdp, indx, fdflags, fp);
	fdpunlock(fdp);
	FRELE(fp, p);
	return (error);
error:
	KERNEL_UNLOCK();
	fdpunlock(fdp);
	closef(fp, p);
	return (error);
}

/*
 * Open a new created file (in /tmp) suitable for mmaping.
 */
int
sys___tmpfd(struct proc *p, void *v, register_t *retval)
{
	struct sys___tmpfd_args /* {
		syscallarg(int) flags;
	} */ *uap = v;
	struct filedesc *fdp = p->p_fd;
	struct file *fp;
	struct vnode *vp;
	int oflags = SCARG(uap, flags);
	int flags, fdflags, cmode;
	int indx, error;
	unsigned int i;
	struct nameidata nd;
	char path[64];
	static const char *letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-";

	/* most flags are hardwired */
	oflags = O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW |
	    (oflags & (O_CLOEXEC | O_CLOFORK));

	fdflags = ((oflags & O_CLOEXEC) ? UF_EXCLOSE : 0)
	    | ((oflags & O_CLOFORK) ? UF_FORKCLOSE : 0);

	fdplock(fdp);
	if ((error = falloc(p, &fp, &indx)) != 0) {
		fdpunlock(fdp);
		return (error);
	}
	fdpunlock(fdp);

	flags = FFLAGS(oflags);

	arc4random_buf(path, sizeof(path));
	memcpy(path, "/tmp/", 5);
	for (i = 5; i < sizeof(path) - 1; i++)
		path[i] = letters[(unsigned char)path[i] & 63];
	path[sizeof(path)-1] = 0;

	cmode = 0600;
	NDINITAT(&nd, 0, KERNELPATH, UIO_SYSSPACE, AT_FDCWD, path, p);
	if ((error = vn_open(&nd, flags, cmode)) != 0) {
		if (error == ERESTART)
			error = EINTR;
		fdplock(fdp);
		fdremove(fdp, indx);
		fdpunlock(fdp);
		closef(fp, p);
		return (error);
	}
	vp = nd.ni_vp;
	fp->f_flag = flags & FMASK;
	fp->f_type = DTYPE_VNODE;
	fp->f_ops = &vnops;
	fp->f_data = vp;
	VOP_UNLOCK(vp);
	*retval = indx;
	fdplock(fdp);
	fdinsert(fdp, indx, fdflags, fp);
	fdpunlock(fdp);
	FRELE(fp, p);

	/* unlink it */
	/* XXX
	 * there is a wee race here, although it is mostly inconsequential.
	 * perhaps someday we can create a file like object without a name...
	 */
	NDINITAT(&nd, DELETE, KERNELPATH | LOCKPARENT | LOCKLEAF, UIO_SYSSPACE,
	    AT_FDCWD, path, p);
	if ((error = namei(&nd)) != 0) {
		printf("can't unlink temp file! %d\n", error);
		error = 0;
	} else {
		vp = nd.ni_vp;
		uvm_vnp_uncache(vp);
		error = VOP_REMOVE(nd.ni_dvp, nd.ni_vp, &nd.ni_cnd);
		if (error) {
			printf("error removing vop: %d\n", error);
			error = 0;
		}
	}

	return (error);
}

/*
 * Get file handle system call
 */
int
sys_getfh(struct proc *p, void *v, register_t *retval)
{
	struct sys_getfh_args /* {
		syscallarg(const char *) fname;
		syscallarg(fhandle_t *) fhp;
	} */ *uap = v;
	struct vnode *vp;
	fhandle_t fh;
	int error;
	struct nameidata nd;

	/*
	 * Must be super user
	 */
	error = suser(p);
	if (error)
		return (error);
	NDINIT(&nd, LOOKUP, FOLLOW | LOCKLEAF, UIO_USERSPACE,
	    SCARG(uap, fname), p);
	error = namei(&nd);
	if (error)
		return (error);
	vp = nd.ni_vp;
	memset(&fh, 0, sizeof(fh));
	fh.fh_fsid = vp->v_mount->mnt_stat.f_fsid;
	error = VFS_VPTOFH(vp, &fh.fh_fid);
	vput(vp);
	if (error)
		return (error);
	error = copyout(&fh, SCARG(uap, fhp), sizeof(fh));
	return (error);
}

/*
 * Open a file given a file handle.
 *
 * Check permissions, allocate an open file structure,
 * and call the device open routine if any.
 */
int
sys_fhopen(struct proc *p, void *v, register_t *retval)
{
	struct sys_fhopen_args /* {
		syscallarg(const fhandle_t *) fhp;
		syscallarg(int) flags;
	} */ *uap = v;
	struct filedesc *fdp = p->p_fd;
	struct file *fp;
	struct vnode *vp = NULL;
	struct mount *mp;
	struct ucred *cred = p->p_ucred;
	int flags, fdflags;
	int type, indx, error=0;
	struct flock lf;
	struct vattr va;
	fhandle_t fh;

	/*
	 * Must be super user
	 */
	if ((error = suser(p)))
		return (error);

	flags = FFLAGS(SCARG(uap, flags));
	if ((flags & (FREAD | FWRITE)) == 0)
		return (EINVAL);
	if ((flags & O_CREAT))
		return (EINVAL);

	fdflags = ((flags & O_CLOEXEC) ? UF_EXCLOSE : 0)
	    | ((flags & O_CLOFORK) ? UF_FORKCLOSE : 0);

	fdplock(fdp);
	if ((error = falloc(p, &fp, &indx)) != 0) {
		fdpunlock(fdp);
		fp = NULL;
		goto bad;
	}
	fdpunlock(fdp);

	if ((error = copyin(SCARG(uap, fhp), &fh, sizeof(fhandle_t))) != 0)
		goto bad;

	if ((mp = vfs_getvfs(&fh.fh_fsid)) == NULL) {
		error = ESTALE;
		goto bad;
	}

	if ((error = VFS_FHTOVP(mp, &fh.fh_fid, &vp)) != 0) {
		vp = NULL;	/* most likely unnecessary sanity for bad: */
		goto bad;
	}

	/* Now do an effective vn_open */

	if (vp->v_type == VSOCK) {
		error = EOPNOTSUPP;
		goto bad;
	}
	if ((flags & O_DIRECTORY) && vp->v_type != VDIR) {
		error = ENOTDIR;
		goto bad;
	}
	if (flags & FREAD) {
		if ((error = VOP_ACCESS(vp, VREAD, cred, p)) != 0)
			goto bad;
	}
	if (flags & (FWRITE | O_TRUNC)) {
		if (vp->v_type == VDIR) {
			error = EISDIR;
			goto bad;
		}
		if ((error = VOP_ACCESS(vp, VWRITE, cred, p)) != 0 ||
		    (error = vn_writechk(vp)) != 0)
			goto bad;
	}
	if (flags & O_TRUNC) {
		vattr_null(&va);
		va.va_size = 0;
		if ((error = VOP_SETATTR(vp, &va, cred, p)) != 0)
			goto bad;
	}
	if ((error = VOP_OPEN(vp, flags, cred, p)) != 0)
		goto bad;
	if (flags & FWRITE)
		vp->v_writecount++;

	/* done with modified vn_open, now finish what sys_open does. */

	fp->f_flag = flags & FMASK;
	fp->f_type = DTYPE_VNODE;
	fp->f_ops = &vnops;
	fp->f_data = vp;
	if (flags & (O_EXLOCK | O_SHLOCK)) {
		lf.l_whence = SEEK_SET;
		lf.l_start = 0;
		lf.l_len = 0;
		if (flags & O_EXLOCK)
			lf.l_type = F_WRLCK;
		else
			lf.l_type = F_RDLCK;
		type = F_FLOCK;
		if ((flags & FNONBLOCK) == 0)
			type |= F_WAIT;
		VOP_UNLOCK(vp);
		error = VOP_ADVLOCK(vp, (caddr_t)fp, F_SETLK, &lf, type);
		if (error) {
			vp = NULL;	/* closef will vn_close the file */
			goto bad;
		}
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
		atomic_setbits_int(&fp->f_iflags, FIF_HASLOCK);
	}
	VOP_UNLOCK(vp);
	*retval = indx;
	fdplock(fdp);
	fdinsert(fdp, indx, fdflags, fp);
	fdpunlock(fdp);
	FRELE(fp, p);
	return (0);

bad:
	if (fp) {
		fdplock(fdp);
		fdremove(fdp, indx);
		fdpunlock(fdp);
		closef(fp, p);
		if (vp != NULL)
			vput(vp);
	}
	return (error);
}

int
sys_fhstat(struct proc *p, void *v, register_t *retval)
{
	struct sys_fhstat_args /* {
		syscallarg(const fhandle_t *) fhp;
		syscallarg(struct stat *) sb;
	} */ *uap = v;
	struct stat sb;
	int error;
	fhandle_t fh;
	struct mount *mp;
	struct vnode *vp;

	/*
	 * Must be super user
	 */
	if ((error = suser(p)))
		return (error);

	if ((error = copyin(SCARG(uap, fhp), &fh, sizeof(fhandle_t))) != 0)
		return (error);

	if ((mp = vfs_getvfs(&fh.fh_fsid)) == NULL)
		return (ESTALE);
	if ((error = VFS_FHTOVP(mp, &fh.fh_fid, &vp)))
		return (error);
	error = vn_stat(vp, &sb, p);
	vput(vp);
	if (error)
		return (error);
	error = copyout(&sb, SCARG(uap, sb), sizeof(sb));
	return (error);
}

int
sys_fhstatfs(struct proc *p, void *v, register_t *retval)
{
	struct sys_fhstatfs_args /* {
		syscallarg(const fhandle_t *) fhp;
		syscallarg(struct statfs *) buf;
	} */ *uap = v;
	struct statfs *sp;
	fhandle_t fh;
	struct mount *mp;
	struct vnode *vp;
	int error;

	/*
	 * Must be super user
	 */
	if ((error = suser(p)))
		return (error);

	if ((error = copyin(SCARG(uap, fhp), &fh, sizeof(fhandle_t))) != 0)
		return (error);

	if ((mp = vfs_getvfs(&fh.fh_fsid)) == NULL)
		return (ESTALE);
	if ((error = VFS_FHTOVP(mp, &fh.fh_fid, &vp)))
		return (error);
	mp = vp->v_mount;
	sp = &mp->mnt_stat;
	vput(vp);
	if ((error = VFS_STATFS(mp, sp, p)) != 0)
		return (error);
	sp->f_flags = mp->mnt_flag & MNT_VISFLAGMASK;
	return (copyout(sp, SCARG(uap, buf), sizeof(*sp)));
}

/*
 * Create a special file or named pipe.
 */
int
sys_mknod(struct proc *p, void *v, register_t *retval)
{
	struct sys_mknod_args /* {
		syscallarg(const char *) path;
		syscallarg(mode_t) mode;
		syscallarg(int) dev;
	} */ *uap = v;

	return (domknodat(p, AT_FDCWD, SCARG(uap, path), SCARG(uap, mode),
	    SCARG(uap, dev)));
}

int
sys_mknodat(struct proc *p, void *v, register_t *retval)
{
	struct sys_mknodat_args /* {
		syscallarg(int) fd;
		syscallarg(const char *) path;
		syscallarg(mode_t) mode;
		syscallarg(dev_t) dev;
	} */ *uap = v;

	return (domknodat(p, SCARG(uap, fd), SCARG(uap, path),
	    SCARG(uap, mode), SCARG(uap, dev)));
}

int
domknodat(struct proc *p, int fd, const char *path, mode_t mode, dev_t dev)
{
	struct vnode *vp;
	struct vattr vattr;
	int error;
	struct nameidata nd;

	if (dev == VNOVAL)
		return (EINVAL);
	NDINITAT(&nd, CREATE, LOCKPARENT, UIO_USERSPACE, fd, path, p);
	nd.ni_pledge = PLEDGE_DPATH;
	nd.ni_unveil = UNVEIL_CREATE;
	if ((error = namei(&nd)) != 0)
		return (error);
	vp = nd.ni_vp;
	if (!S_ISFIFO(mode) || dev != 0) {
		if (!vnoperm(nd.ni_dvp) && (error = suser(p)) != 0)
			goto out;
		if (p->p_fd->fd_rdir) {
			error = EINVAL;
			goto out;
		}
	}
	if (vp != NULL)
		error = EEXIST;
	else {
		vattr_null(&vattr);
		vattr.va_mode = (mode & ALLPERMS) &~ p->p_fd->fd_cmask;
		if ((p->p_p->ps_flags & PS_PLEDGE))
			vattr.va_mode &= ACCESSPERMS;
		vattr.va_rdev = dev;

		switch (mode & S_IFMT) {
		case S_IFMT:	/* used by badsect to flag bad sectors */
			vattr.va_type = VBAD;
			break;
		case S_IFCHR:
			vattr.va_type = VCHR;
			break;
		case S_IFBLK:
			vattr.va_type = VBLK;
			break;
		case S_IFIFO:
#ifndef FIFO
			error = EOPNOTSUPP;
			break;
#else
			if (dev == 0) {
				vattr.va_type = VFIFO;
				break;
			}
			/* FALLTHROUGH */
#endif /* FIFO */
		default:
			error = EINVAL;
			break;
		}
	}
out:
	if (!error) {
		error = VOP_MKNOD(nd.ni_dvp, &nd.ni_vp, &nd.ni_cnd, &vattr);
		vput(nd.ni_dvp);
	} else {
		VOP_ABORTOP(nd.ni_dvp, &nd.ni_cnd);
		if (nd.ni_dvp == vp)
			vrele(nd.ni_dvp);
		else
			vput(nd.ni_dvp);
		if (vp)
			vrele(vp);
	}
	return (error);
}

/*
 * Create a named pipe.
 */
int
sys_mkfifo(struct proc *p, void *v, register_t *retval)
{
	struct sys_mkfifo_args /* {
		syscallarg(const char *) path;
		syscallarg(mode_t) mode;
	} */ *uap = v;

	return (domknodat(p, AT_FDCWD, SCARG(uap, path),
	    (SCARG(uap, mode) & ALLPERMS) | S_IFIFO, 0));
}

int
sys_mkfifoat(struct proc *p, void *v, register_t *retval)
{
	struct sys_mkfifoat_args /* {
		syscallarg(int) fd;
		syscallarg(const char *) path;
		syscallarg(mode_t) mode;
	} */ *uap = v;

	return (domknodat(p, SCARG(uap, fd), SCARG(uap, path),
	    (SCARG(uap, mode) & ALLPERMS) | S_IFIFO, 0));
}

/*
 * Make a hard file link.
 */
int
sys_link(struct proc *p, void *v, register_t *retval)
{
	struct sys_link_args /* {
		syscallarg(const char *) path;
		syscallarg(const char *) link;
	} */ *uap = v;

	return (dolinkat(p, AT_FDCWD, SCARG(uap, path), AT_FDCWD,
	    SCARG(uap, link), AT_SYMLINK_FOLLOW));
}

int
sys_linkat(struct proc *p, void *v, register_t *retval)
{
	struct sys_linkat_args /* {
		syscallarg(int) fd1;
		syscallarg(const char *) path1;
		syscallarg(int) fd2;
		syscallarg(const char *) path2;
		syscallarg(int) flag;
	} */ *uap = v;

	return (dolinkat(p, SCARG(uap, fd1), SCARG(uap, path1),
	    SCARG(uap, fd2), SCARG(uap, path2), SCARG(uap, flag)));
}

int
dolinkat(struct proc *p, int fd1, const char *path1, int fd2,
    const char *path2, int flag)
{
	struct vnode *vp;
	struct nameidata nd;
	int error, follow;

	if (flag & ~AT_SYMLINK_FOLLOW)
		return (EINVAL);

	follow = (flag & AT_SYMLINK_FOLLOW) ? FOLLOW : NOFOLLOW;
	NDINITAT(&nd, LOOKUP, follow, UIO_USERSPACE, fd1, path1, p);
	nd.ni_pledge = PLEDGE_RPATH;
	nd.ni_unveil = UNVEIL_READ;
	if ((error = namei(&nd)) != 0)
		return (error);
	vp = nd.ni_vp;

	if (vp->v_type == VDIR) {
		error = EPERM;
		goto out;
	}

	NDINITAT(&nd, CREATE, LOCKPARENT, UIO_USERSPACE, fd2, path2, p);
	nd.ni_pledge = PLEDGE_CPATH;
	nd.ni_unveil = UNVEIL_CREATE;
	if ((error = namei(&nd)) != 0)
		goto out;
	if (nd.ni_vp) {
		VOP_ABORTOP(nd.ni_dvp, &nd.ni_cnd);
		if (nd.ni_dvp == nd.ni_vp)
			vrele(nd.ni_dvp);
		else
			vput(nd.ni_dvp);
		vrele(nd.ni_vp);
		error = EEXIST;
		goto out;
	}

	/* No cross-mount links! */
	if (nd.ni_dvp->v_mount != vp->v_mount) {
		VOP_ABORTOP(nd.ni_dvp, &nd.ni_cnd);
		vput(nd.ni_dvp);
		error = EXDEV;
		goto out;
	}

	error = VOP_LINK(nd.ni_dvp, vp, &nd.ni_cnd);
out:
	vrele(vp);
	return (error);
}

/*
 * Make a symbolic link.
 */
int
sys_symlink(struct proc *p, void *v, register_t *retval)
{
	struct sys_symlink_args /* {
		syscallarg(const char *) path;
		syscallarg(const char *) link;
	} */ *uap = v;

	return (dosymlinkat(p, SCARG(uap, path), AT_FDCWD, SCARG(uap, link)));
}

int
sys_symlinkat(struct proc *p, void *v, register_t *retval)
{
	struct sys_symlinkat_args /* {
		syscallarg(const char *) path;
		syscallarg(int) fd;
		syscallarg(const char *) link;
	} */ *uap = v;

	return (dosymlinkat(p, SCARG(uap, path), SCARG(uap, fd),
	    SCARG(uap, link)));
}

int
dosymlinkat(struct proc *p, const char *upath, int fd, const char *link)
{
	struct vattr vattr;
	char *path;
	int error;
	struct nameidata nd;

	path = pool_get(&namei_pool, PR_WAITOK);
	error = copyinstr(upath, path, MAXPATHLEN, NULL);
	if (error)
		goto out;
	NDINITAT(&nd, CREATE, LOCKPARENT, UIO_USERSPACE, fd, link, p);
	nd.ni_pledge = PLEDGE_CPATH;
	nd.ni_unveil = UNVEIL_CREATE;
	if ((error = namei(&nd)) != 0)
		goto out;
	if (nd.ni_vp) {
		VOP_ABORTOP(nd.ni_dvp, &nd.ni_cnd);
		if (nd.ni_dvp == nd.ni_vp)
			vrele(nd.ni_dvp);
		else
			vput(nd.ni_dvp);
		vrele(nd.ni_vp);
		error = EEXIST;
		goto out;
	}
	vattr_null(&vattr);
	vattr.va_mode = ACCESSPERMS &~ p->p_fd->fd_cmask;
	error = VOP_SYMLINK(nd.ni_dvp, &nd.ni_vp, &nd.ni_cnd, &vattr, path);
out:
	pool_put(&namei_pool, path);
	return (error);
}

/*
 * Delete a name from the filesystem.
 */
int
sys_unlink(struct proc *p, void *v, register_t *retval)
{
	struct sys_unlink_args /* {
		syscallarg(const char *) path;
	} */ *uap = v;

	return (dounlinkat(p, AT_FDCWD, SCARG(uap, path), 0));
}

int
sys_unlinkat(struct proc *p, void *v, register_t *retval)
{
	struct sys_unlinkat_args /* {
		syscallarg(int) fd;
		syscallarg(const char *) path;
		syscallarg(int) flag;
	} */ *uap = v;

	return (dounlinkat(p, SCARG(uap, fd), SCARG(uap, path),
	    SCARG(uap, flag)));
}

int
dounlinkat(struct proc *p, int fd, const char *path, int flag)
{
	struct vnode *vp;
	int error;
	struct nameidata nd;

	if (flag & ~AT_REMOVEDIR)
		return (EINVAL);

	NDINITAT(&nd, DELETE, LOCKPARENT | LOCKLEAF, UIO_USERSPACE,
	    fd, path, p);
	nd.ni_pledge = PLEDGE_CPATH;
	nd.ni_unveil = UNVEIL_CREATE;
	if ((error = namei(&nd)) != 0)
		return (error);
	vp = nd.ni_vp;

	if (flag & AT_REMOVEDIR) {
		if (vp->v_type != VDIR) {
			error = ENOTDIR;
			goto out;
		}
		/*
		 * No rmdir "." please.
		 */
		if (nd.ni_dvp == vp) {
			error = EINVAL;
			goto out;
		}
		/*
		 * A mounted on directory cannot be deleted.
		 */
		if (vp->v_mountedhere != NULL) {
			error = EBUSY;
			goto out;
		}
	}

	/*
	 * The root of a mounted filesystem cannot be deleted.
	 */
	if (vp->v_flag & VROOT)
		error = EBUSY;
out:
	if (!error) {
		if (flag & AT_REMOVEDIR) {
			error = VOP_RMDIR(nd.ni_dvp, nd.ni_vp, &nd.ni_cnd);
		} else {
			(void)uvm_vnp_uncache(vp);
			error = VOP_REMOVE(nd.ni_dvp, nd.ni_vp, &nd.ni_cnd);
		}
	} else {
		VOP_ABORTOP(nd.ni_dvp, &nd.ni_cnd);
		if (nd.ni_dvp == vp)
			vrele(nd.ni_dvp);
		else
			vput(nd.ni_dvp);
		vput(vp);
	}
	return (error);
}

/*
 * Reposition read/write file offset.
 */
int
sys_lseek(struct proc *p, void *v, register_t *retval)
{
	struct sys_lseek_args /* {
		syscallarg(int) fd;
		syscallarg(off_t) offset;
		syscallarg(int) whence;
	} */ *uap = v;
	struct filedesc *fdp = p->p_fd;
	struct file *fp;
	off_t offset;
	int error;

	if ((fp = fd_getfile(fdp, SCARG(uap, fd))) == NULL)
		return (EBADF);
	if (fp->f_ops->fo_seek == NULL) {
		error = ESPIPE;
		goto bad;
	}
	offset = SCARG(uap, offset);

	error = (*fp->f_ops->fo_seek)(fp, &offset, SCARG(uap, whence), p);
	if (error)
		goto bad;

	*(off_t *)retval = offset;
	mtx_enter(&fp->f_mtx);
	fp->f_seek++;
	mtx_leave(&fp->f_mtx);
	error = 0;
 bad:
	FRELE(fp, p);
	return (error);
}

/*
 * Check access permissions.
 */
int
sys_access(struct proc *p, void *v, register_t *retval)
{
	struct sys_access_args /* {
		syscallarg(const char *) path;
		syscallarg(int) amode;
	} */ *uap = v;

	return (dofaccessat(p, AT_FDCWD, SCARG(uap, path),
	    SCARG(uap, amode), 0));
}

int
sys_faccessat(struct proc *p, void *v, register_t *retval)
{
	struct sys_faccessat_args /* {
		syscallarg(int) fd;
		syscallarg(const char *) path;
		syscallarg(int) amode;
		syscallarg(int) flag;
	} */ *uap = v;

	return (dofaccessat(p, SCARG(uap, fd), SCARG(uap, path),
	    SCARG(uap, amode), SCARG(uap, flag)));
}

int
dofaccessat(struct proc *p, int fd, const char *path, int amode, int flag)
{
	struct vnode *vp;
	struct ucred *newcred, *oldcred;
	struct nameidata nd;
	int vflags = 0, error;

	if (amode & ~(R_OK | W_OK | X_OK))
		return (EINVAL);
	if (flag & ~AT_EACCESS)
		return (EINVAL);

	newcred = NULL;
	oldcred = p->p_ucred;

	/*
	 * If access as real ids was requested and they really differ,
	 * give the thread new creds with them reset
	 */
	if ((flag & AT_EACCESS) == 0 &&
	    (oldcred->cr_uid != oldcred->cr_ruid ||
	    (oldcred->cr_gid != oldcred->cr_rgid))) {
		p->p_ucred = newcred = crdup(oldcred);
		newcred->cr_uid = newcred->cr_ruid;
		newcred->cr_gid = newcred->cr_rgid;
	}

	NDINITAT(&nd, LOOKUP, FOLLOW | LOCKLEAF, UIO_USERSPACE, fd, path, p);
	nd.ni_pledge = PLEDGE_RPATH;
	nd.ni_unveil = UNVEIL_READ;
	if (amode & R_OK)
		vflags |= VREAD;
	if (amode & W_OK) {
		vflags |= VWRITE;
		nd.ni_unveil |= UNVEIL_WRITE;
	}
	if (amode & X_OK)
		vflags |= VEXEC;
	if ((error = namei(&nd)) != 0)
		goto out;
	vp = nd.ni_vp;

	/* Flags == 0 means only check for existence. */
	if (amode) {
		error = VOP_ACCESS(vp, vflags, p->p_ucred, p);
		if (!error && (vflags & VWRITE))
			error = vn_writechk(vp);
	}
	vput(vp);
out:
	if (newcred != NULL) {
		p->p_ucred = oldcred;
		crfree(newcred);
	}
	return (error);
}

/*
 * Get file status; this version follows links.
 */
int
sys_stat(struct proc *p, void *v, register_t *retval)
{
	struct sys_stat_args /* {
		syscallarg(const char *) path;
		syscallarg(struct stat *) ub;
	} */ *uap = v;

	return (dofstatat(p, AT_FDCWD, SCARG(uap, path), SCARG(uap, ub), 0));
}

int
sys_fstatat(struct proc *p, void *v, register_t *retval)
{
	struct sys_fstatat_args /* {
		syscallarg(int) fd;
		syscallarg(const char *) path;
		syscallarg(struct stat *) buf;
		syscallarg(int) flag;
	} */ *uap = v;

	return (dofstatat(p, SCARG(uap, fd), SCARG(uap, path),
	    SCARG(uap, buf), SCARG(uap, flag)));
}

int
dofstatat(struct proc *p, int fd, const char *path, struct stat *buf, int flag)
{
	struct stat sb;
	int error, follow;
	struct nameidata nd;

	if (flag & ~AT_SYMLINK_NOFOLLOW)
		return (EINVAL);


	follow = (flag & AT_SYMLINK_NOFOLLOW) ? NOFOLLOW : FOLLOW;
	NDINITAT(&nd, LOOKUP, follow | LOCKLEAF, UIO_USERSPACE, fd, path, p);
	nd.ni_pledge = PLEDGE_RPATH;
	nd.ni_unveil = UNVEIL_READ;
	KERNEL_LOCK();
	if ((error = namei(&nd)) != 0) {
		KERNEL_UNLOCK();
		return (error);
	}
	error = vn_stat(nd.ni_vp, &sb, p);
	vput(nd.ni_vp);
	KERNEL_UNLOCK();
	if (error)
		return (error);
	/* Don't let non-root see generation numbers (for NFS security) */
	if (suser(p))
		sb.st_gen = 0;
	error = copyout(&sb, buf, sizeof(sb));
#ifdef KTRACE
	if (error == 0 && KTRPOINT(p, KTR_STRUCT))
		ktrstat(p, &sb);
#endif
	return (error);
}

/*
 * Get file status; this version does not follow links.
 */
int
sys_lstat(struct proc *p, void *v, register_t *retval)
{
	struct sys_lstat_args /* {
		syscallarg(const char *) path;
		syscallarg(struct stat *) ub;
	} */ *uap = v;

	return (dofstatat(p, AT_FDCWD, SCARG(uap, path), SCARG(uap, ub),
	    AT_SYMLINK_NOFOLLOW));
}

/*
 * Get configurable pathname variables.
 */
int
sys_pathconf(struct proc *p, void *v, register_t *retval)
{
	struct sys_pathconf_args /* {
		syscallarg(const char *) path;
		syscallarg(int) name;
	} */ *uap = v;

	return dopathconfat(p, AT_FDCWD, SCARG(uap, path), SCARG(uap, name),
	    0, retval);
}

int
sys_pathconfat(struct proc *p, void *v, register_t *retval)
{
	struct sys_pathconfat_args /* {
		syscallarg(int) fd;
		syscallarg(const char *) path;
		syscallarg(int) name;
		syscallarg(int) flag;
	} */ *uap = v;

	return dopathconfat(p, SCARG(uap, fd), SCARG(uap, path),
	    SCARG(uap, name), SCARG(uap, flag), retval);
}

int
dopathconfat(struct proc *p, int fd, const char *path, int name, int flag,
    register_t *retval)
{
	int follow, error;
	struct nameidata nd;

	if (flag & ~AT_SYMLINK_NOFOLLOW)
		return EINVAL;

	follow = (flag & AT_SYMLINK_NOFOLLOW) ? NOFOLLOW : FOLLOW;
	NDINITAT(&nd, LOOKUP, follow | LOCKLEAF, UIO_USERSPACE, fd, path, p);
	nd.ni_pledge = PLEDGE_RPATH;
	nd.ni_unveil = UNVEIL_READ;
	if ((error = namei(&nd)) != 0)
		return (error);
	error = VOP_PATHCONF(nd.ni_vp, name, retval);
	vput(nd.ni_vp);
	return (error);
}

/*
 * Return target name of a symbolic link.
 */
int
sys_readlink(struct proc *p, void *v, register_t *retval)
{
	struct sys_readlink_args /* {
		syscallarg(const char *) path;
		syscallarg(char *) buf;
		syscallarg(size_t) count;
	} */ *uap = v;

	return (doreadlinkat(p, AT_FDCWD, SCARG(uap, path), SCARG(uap, buf),
	    SCARG(uap, count), retval));
}

int
sys_readlinkat(struct proc *p, void *v, register_t *retval)
{
	struct sys_readlinkat_args /* {
		syscallarg(int) fd;
		syscallarg(const char *) path;
		syscallarg(char *) buf;
		syscallarg(size_t) count;
	} */ *uap = v;

	return (doreadlinkat(p, SCARG(uap, fd), SCARG(uap, path),
	    SCARG(uap, buf), SCARG(uap, count), retval));
}

int
doreadlinkat(struct proc *p, int fd, const char *path, char *buf,
    size_t count, register_t *retval)
{
	struct vnode *vp;
	struct iovec aiov;
	struct uio auio;
	int error;
	struct nameidata nd;

	NDINITAT(&nd, LOOKUP, NOFOLLOW | LOCKLEAF, UIO_USERSPACE, fd, path, p);
	nd.ni_pledge = PLEDGE_RPATH;
	nd.ni_unveil = UNVEIL_READ;
	if ((error = namei(&nd)) != 0)
		return (error);
	vp = nd.ni_vp;
	if (vp->v_type != VLNK)
		error = EINVAL;
	else {
		aiov.iov_base = buf;
		aiov.iov_len = count;
		auio.uio_iov = &aiov;
		auio.uio_iovcnt = 1;
		auio.uio_offset = 0;
		auio.uio_rw = UIO_READ;
		auio.uio_segflg = UIO_USERSPACE;
		auio.uio_procp = p;
		auio.uio_resid = count;
		error = VOP_READLINK(vp, &auio, p->p_ucred);
		*retval = count - auio.uio_resid;
	}
	vput(vp);
	return (error);
}

/*
 * Change flags of a file given a path name.
 */
int
sys_chflags(struct proc *p, void *v, register_t *retval)
{
	struct sys_chflags_args /* {
		syscallarg(const char *) path;
		syscallarg(u_int) flags;
	} */ *uap = v;

	return (dochflagsat(p, AT_FDCWD, SCARG(uap, path),
	    SCARG(uap, flags), 0));
}

int
sys_chflagsat(struct proc *p, void *v, register_t *retval)
{
	struct sys_chflagsat_args /* {
		syscallarg(int) fd;
		syscallarg(const char *) path;
		syscallarg(u_int) flags;
		syscallarg(int) atflags;
	} */ *uap = v;

	return (dochflagsat(p, SCARG(uap, fd), SCARG(uap, path),
	    SCARG(uap, flags), SCARG(uap, atflags)));
}

int
dochflagsat(struct proc *p, int fd, const char *path, u_int flags, int atflags)
{
	struct nameidata nd;
	int error, follow;

	if (atflags & ~AT_SYMLINK_NOFOLLOW)
		return (EINVAL);

	follow = (atflags & AT_SYMLINK_NOFOLLOW) ? NOFOLLOW : FOLLOW;
	NDINITAT(&nd, LOOKUP, follow, UIO_USERSPACE, fd, path, p);
	nd.ni_pledge = PLEDGE_FATTR | PLEDGE_RPATH;
	nd.ni_unveil = UNVEIL_WRITE;
	if ((error = namei(&nd)) != 0)
		return (error);
	return (dovchflags(p, nd.ni_vp, flags));
}

/*
 * Change flags of a file given a file descriptor.
 */
int
sys_fchflags(struct proc *p, void *v, register_t *retval)
{
	struct sys_fchflags_args /* {
		syscallarg(int) fd;
		syscallarg(u_int) flags;
	} */ *uap = v;
	struct file *fp;
	struct vnode *vp;
	int error;

	if ((error = getvnode(p, SCARG(uap, fd), &fp)) != 0)
		return (error);
	vp = fp->f_data;
	vref(vp);
	FRELE(fp, p);
	return (dovchflags(p, vp, SCARG(uap, flags)));
}

int
dovchflags(struct proc *p, struct vnode *vp, u_int flags)
{
	struct vattr vattr;
	int error;

	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	if (vp->v_mount && vp->v_mount->mnt_flag & MNT_RDONLY)
		error = EROFS;
	else if (flags == VNOVAL)
		error = EINVAL;
	else {
		if (suser(p)) {
			if ((error = VOP_GETATTR(vp, &vattr, p->p_ucred, p))
			    != 0)
				goto out;
			if (vattr.va_type == VCHR || vattr.va_type == VBLK) {
				error = EINVAL;
				goto out;
			}
		}
		vattr_null(&vattr);
		vattr.va_flags = flags;
		error = VOP_SETATTR(vp, &vattr, p->p_ucred, p);
	}
out:
	vput(vp);
	return (error);
}

/*
 * Change mode of a file given path name.
 */
int
sys_chmod(struct proc *p, void *v, register_t *retval)
{
	struct sys_chmod_args /* {
		syscallarg(const char *) path;
		syscallarg(mode_t) mode;
	} */ *uap = v;

	return (dofchmodat(p, AT_FDCWD, SCARG(uap, path), SCARG(uap, mode), 0));
}

int
sys_fchmodat(struct proc *p, void *v, register_t *retval)
{
	struct sys_fchmodat_args /* {
		syscallarg(int) fd;
		syscallarg(const char *) path;
		syscallarg(mode_t) mode;
		syscallarg(int) flag;
	} */ *uap = v;

	return (dofchmodat(p, SCARG(uap, fd), SCARG(uap, path),
	    SCARG(uap, mode), SCARG(uap, flag)));
}

int
dofchmodat(struct proc *p, int fd, const char *path, mode_t mode, int flag)
{
	struct vnode *vp;
	struct vattr vattr;
	int error, follow;
	struct nameidata nd;

	if (mode & ~(S_IFMT | ALLPERMS))
		return (EINVAL);
	if ((p->p_p->ps_flags & PS_PLEDGE))
		mode &= ACCESSPERMS;
	if (flag & ~AT_SYMLINK_NOFOLLOW)
		return (EINVAL);

	follow = (flag & AT_SYMLINK_NOFOLLOW) ? NOFOLLOW : FOLLOW;
	NDINITAT(&nd, LOOKUP, follow, UIO_USERSPACE, fd, path, p);
	nd.ni_pledge = PLEDGE_FATTR | PLEDGE_RPATH;
	nd.ni_unveil = UNVEIL_WRITE;
	if ((error = namei(&nd)) != 0)
		return (error);
	vp = nd.ni_vp;
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	if (vp->v_mount->mnt_flag & MNT_RDONLY)
		error = EROFS;
	else {
		vattr_null(&vattr);
		vattr.va_mode = mode & ALLPERMS;
		error = VOP_SETATTR(vp, &vattr, p->p_ucred, p);
	}
	vput(vp);
	return (error);
}

/*
 * Change mode of a file given a file descriptor.
 */
int
sys_fchmod(struct proc *p, void *v, register_t *retval)
{
	struct sys_fchmod_args /* {
		syscallarg(int) fd;
		syscallarg(mode_t) mode;
	} */ *uap = v;
	struct vattr vattr;
	struct vnode *vp;
	struct file *fp;
	mode_t mode = SCARG(uap, mode);
	int error;

	if (mode & ~(S_IFMT | ALLPERMS))
		return (EINVAL);
	if ((p->p_p->ps_flags & PS_PLEDGE))
		mode &= ACCESSPERMS;

	if ((error = getvnode(p, SCARG(uap, fd), &fp)) != 0)
		return (error);
	vp = fp->f_data;
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	if (vp->v_mount && vp->v_mount->mnt_flag & MNT_RDONLY)
		error = EROFS;
	else {
		vattr_null(&vattr);
		vattr.va_mode = mode & ALLPERMS;
		error = VOP_SETATTR(vp, &vattr, p->p_ucred, p);
	}
	VOP_UNLOCK(vp);
	FRELE(fp, p);
	return (error);
}

/*
 * Set ownership given a path name.
 */
int
sys_chown(struct proc *p, void *v, register_t *retval)
{
	struct sys_chown_args /* {
		syscallarg(const char *) path;
		syscallarg(uid_t) uid;
		syscallarg(gid_t) gid;
	} */ *uap = v;

	return (dofchownat(p, AT_FDCWD, SCARG(uap, path), SCARG(uap, uid),
	    SCARG(uap, gid), 0));
}

int
sys_fchownat(struct proc *p, void *v, register_t *retval)
{
	struct sys_fchownat_args /* {
		syscallarg(int) fd;
		syscallarg(const char *) path;
		syscallarg(uid_t) uid;
		syscallarg(gid_t) gid;
		syscallarg(int) flag;
	} */ *uap = v;

	return (dofchownat(p, SCARG(uap, fd), SCARG(uap, path),
	    SCARG(uap, uid), SCARG(uap, gid), SCARG(uap, flag)));
}

int
dofchownat(struct proc *p, int fd, const char *path, uid_t uid, gid_t gid,
    int flag)
{
	struct vnode *vp;
	struct vattr vattr;
	int error, follow;
	struct nameidata nd;
	mode_t mode;

	if (flag & ~AT_SYMLINK_NOFOLLOW)
		return (EINVAL);

	follow = (flag & AT_SYMLINK_NOFOLLOW) ? NOFOLLOW : FOLLOW;
	NDINITAT(&nd, LOOKUP, follow, UIO_USERSPACE, fd, path, p);
	nd.ni_pledge = PLEDGE_CHOWN | PLEDGE_RPATH;
	nd.ni_unveil = UNVEIL_WRITE;
	if ((error = namei(&nd)) != 0)
		return (error);
	vp = nd.ni_vp;
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	if (vp->v_mount->mnt_flag & MNT_RDONLY)
		error = EROFS;
	else {
		if ((error = pledge_chown(p, uid, gid)))
			goto out;
		if ((uid != -1 || gid != -1) && !vnoperm(vp)) {
			error = VOP_GETATTR(vp, &vattr, p->p_ucred, p);
			if (error)
				goto out;
			mode = vattr.va_mode & ~(VSUID | VSGID);
			if (mode == vattr.va_mode)
				mode = VNOVAL;
		} else
			mode = VNOVAL;
		vattr_null(&vattr);
		vattr.va_uid = uid;
		vattr.va_gid = gid;
		vattr.va_mode = mode;
		error = VOP_SETATTR(vp, &vattr, p->p_ucred, p);
	}
out:
	vput(vp);
	return (error);
}

/*
 * Set ownership given a path name, without following links.
 */
int
sys_lchown(struct proc *p, void *v, register_t *retval)
{
	struct sys_lchown_args /* {
		syscallarg(const char *) path;
		syscallarg(uid_t) uid;
		syscallarg(gid_t) gid;
	} */ *uap = v;
	struct vnode *vp;
	struct vattr vattr;
	int error;
	struct nameidata nd;
	mode_t mode;
	uid_t uid = SCARG(uap, uid);
	gid_t gid = SCARG(uap, gid);

	NDINIT(&nd, LOOKUP, NOFOLLOW, UIO_USERSPACE, SCARG(uap, path), p);
	nd.ni_pledge = PLEDGE_CHOWN | PLEDGE_RPATH;
	nd.ni_unveil = UNVEIL_WRITE;
	if ((error = namei(&nd)) != 0)
		return (error);
	vp = nd.ni_vp;
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	if (vp->v_mount->mnt_flag & MNT_RDONLY)
		error = EROFS;
	else {
		if ((error = pledge_chown(p, uid, gid)))
			goto out;
		if ((uid != -1 || gid != -1) && !vnoperm(vp)) {
			error = VOP_GETATTR(vp, &vattr, p->p_ucred, p);
			if (error)
				goto out;
			mode = vattr.va_mode & ~(VSUID | VSGID);
			if (mode == vattr.va_mode)
				mode = VNOVAL;
		} else
			mode = VNOVAL;
		vattr_null(&vattr);
		vattr.va_uid = uid;
		vattr.va_gid = gid;
		vattr.va_mode = mode;
		error = VOP_SETATTR(vp, &vattr, p->p_ucred, p);
	}
out:
	vput(vp);
	return (error);
}

/*
 * Set ownership given a file descriptor.
 */
int
sys_fchown(struct proc *p, void *v, register_t *retval)
{
	struct sys_fchown_args /* {
		syscallarg(int) fd;
		syscallarg(uid_t) uid;
		syscallarg(gid_t) gid;
	} */ *uap = v;
	struct vnode *vp;
	struct vattr vattr;
	int error;
	struct file *fp;
	mode_t mode;
	uid_t uid = SCARG(uap, uid);
	gid_t gid = SCARG(uap, gid);

	if ((error = getvnode(p, SCARG(uap, fd), &fp)) != 0)
		return (error);
	vp = fp->f_data;
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	if (vp->v_mount && (vp->v_mount->mnt_flag & MNT_RDONLY))
		error = EROFS;
	else {
		if ((error = pledge_chown(p, uid, gid)))
			goto out;
		if ((uid != -1 || gid != -1) && !vnoperm(vp)) {
			error = VOP_GETATTR(vp, &vattr, p->p_ucred, p);
			if (error)
				goto out;
			mode = vattr.va_mode & ~(VSUID | VSGID);
			if (mode == vattr.va_mode)
				mode = VNOVAL;
		} else
			mode = VNOVAL;
		vattr_null(&vattr);
		vattr.va_uid = uid;
		vattr.va_gid = gid;
		vattr.va_mode = mode;
		error = VOP_SETATTR(vp, &vattr, p->p_ucred, p);
	}
out:
	VOP_UNLOCK(vp);
	FRELE(fp, p);
	return (error);
}

/*
 * Set the access and modification times given a path name.
 */
int
sys_utimes(struct proc *p, void *v, register_t *retval)
{
	struct sys_utimes_args /* {
		syscallarg(const char *) path;
		syscallarg(const struct timeval *) tptr;
	} */ *uap = v;

	struct timespec ts[2];
	struct timeval tv[2];
	const struct timeval *tvp;
	int error;

	tvp = SCARG(uap, tptr);
	if (tvp != NULL) {
		error = copyin(tvp, tv, sizeof(tv));
		if (error)
			return (error);
#ifdef KTRACE
		if (KTRPOINT(p, KTR_STRUCT))
			ktrabstimeval(p, &tv);
#endif
		if (!timerisvalid(&tv[0]) || !timerisvalid(&tv[1]))
			return (EINVAL);
		TIMEVAL_TO_TIMESPEC(&tv[0], &ts[0]);
		TIMEVAL_TO_TIMESPEC(&tv[1], &ts[1]);
	} else
		ts[0].tv_nsec = ts[1].tv_nsec = UTIME_NOW;

	return (doutimensat(p, AT_FDCWD, SCARG(uap, path), ts, 0));
}

int
sys_utimensat(struct proc *p, void *v, register_t *retval)
{
	struct sys_utimensat_args /* {
		syscallarg(int) fd;
		syscallarg(const char *) path;
		syscallarg(const struct timespec *) times;
		syscallarg(int) flag;
	} */ *uap = v;

	struct timespec ts[2];
	const struct timespec *tsp;
	int error, i;

	tsp = SCARG(uap, times);
	if (tsp != NULL) {
		error = copyin(tsp, ts, sizeof(ts));
		if (error)
			return (error);
		for (i = 0; i < nitems(ts); i++) {
			if (ts[i].tv_nsec == UTIME_NOW)
				continue;
			if (ts[i].tv_nsec == UTIME_OMIT)
				continue;
#ifdef KTRACE
			if (KTRPOINT(p, KTR_STRUCT))
				ktrabstimespec(p, &ts[i]);
#endif
			if (!timespecisvalid(&ts[i]))
				return (EINVAL);
		}
	} else
		ts[0].tv_nsec = ts[1].tv_nsec = UTIME_NOW;

	return (doutimensat(p, SCARG(uap, fd), SCARG(uap, path), ts,
	    SCARG(uap, flag)));
}

int
doutimensat(struct proc *p, int fd, const char *path,
    struct timespec ts[2], int flag)
{
	struct vnode *vp;
	int error, follow;
	struct nameidata nd;

	if (flag & ~AT_SYMLINK_NOFOLLOW)
		return (EINVAL);

	follow = (flag & AT_SYMLINK_NOFOLLOW) ? NOFOLLOW : FOLLOW;
	NDINITAT(&nd, LOOKUP, follow, UIO_USERSPACE, fd, path, p);
	nd.ni_pledge = PLEDGE_FATTR | PLEDGE_RPATH;
	nd.ni_unveil = UNVEIL_WRITE;
	if ((error = namei(&nd)) != 0)
		return (error);
	vp = nd.ni_vp;

	return (dovutimens(p, vp, ts));
}

int
dovutimens(struct proc *p, struct vnode *vp, struct timespec ts[2])
{
	struct vattr vattr;
	struct timespec now;
	int error;

#ifdef KTRACE
	/* if they're both UTIME_NOW, then don't report either */
	if ((ts[0].tv_nsec != UTIME_NOW || ts[1].tv_nsec != UTIME_NOW) &&
	    KTRPOINT(p, KTR_STRUCT)) {
		ktrabstimespec(p, &ts[0]);
		ktrabstimespec(p, &ts[1]);
	}
#endif

	vattr_null(&vattr);

	/*  make sure ctime is updated even if neither mtime nor atime is */
	vattr.va_vaflags = VA_UTIMES_CHANGE;

	if (ts[0].tv_nsec == UTIME_NOW || ts[1].tv_nsec == UTIME_NOW) {
		if (ts[0].tv_nsec == UTIME_NOW && ts[1].tv_nsec == UTIME_NOW)
			vattr.va_vaflags |= VA_UTIMES_NULL;

		getnanotime(&now);
		if (ts[0].tv_nsec == UTIME_NOW)
			ts[0] = now;
		if (ts[1].tv_nsec == UTIME_NOW)
			ts[1] = now;
	}

	if (ts[0].tv_nsec != UTIME_OMIT)
		vattr.va_atime = ts[0];
	if (ts[1].tv_nsec != UTIME_OMIT)
		vattr.va_mtime = ts[1];

	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	if (vp->v_mount->mnt_flag & MNT_RDONLY)
		error = EROFS;
	else
		error = VOP_SETATTR(vp, &vattr, p->p_ucred, p);
	vput(vp);
	return (error);
}

/*
 * Set the access and modification times given a file descriptor.
 */
int
sys_futimes(struct proc *p, void *v, register_t *retval)
{
	struct sys_futimes_args /* {
		syscallarg(int) fd;
		syscallarg(const struct timeval *) tptr;
	} */ *uap = v;
	struct timeval tv[2];
	struct timespec ts[2];
	const struct timeval *tvp;
	int error;

	tvp = SCARG(uap, tptr);
	if (tvp != NULL) {
		error = copyin(tvp, tv, sizeof(tv));
		if (error)
			return (error);
#ifdef KTRACE
		if (KTRPOINT(p, KTR_STRUCT)) {
			ktrabstimeval(p, &tv[0]);
			ktrabstimeval(p, &tv[1]);
		}
#endif
		if (!timerisvalid(&tv[0]) || !timerisvalid(&tv[1]))
			return (EINVAL);
		TIMEVAL_TO_TIMESPEC(&tv[0], &ts[0]);
		TIMEVAL_TO_TIMESPEC(&tv[1], &ts[1]);
	} else
		ts[0].tv_nsec = ts[1].tv_nsec = UTIME_NOW;

	return (dofutimens(p, SCARG(uap, fd), ts));
}

int
sys_futimens(struct proc *p, void *v, register_t *retval)
{
	struct sys_futimens_args /* {
		syscallarg(int) fd;
		syscallarg(const struct timespec *) times;
	} */ *uap = v;
	struct timespec ts[2];
	const struct timespec *tsp;
	int error, i;

	tsp = SCARG(uap, times);
	if (tsp != NULL) {
		error = copyin(tsp, ts, sizeof(ts));
		if (error)
			return (error);
		for (i = 0; i < nitems(ts); i++) {
			if (ts[i].tv_nsec == UTIME_NOW)
				continue;
			if (ts[i].tv_nsec == UTIME_OMIT)
				continue;
#ifdef KTRACE
			if (KTRPOINT(p, KTR_STRUCT))
				ktrabstimespec(p, &ts[i]);
#endif
			if (!timespecisvalid(&ts[i]))
				return (EINVAL);
		}
	} else
		ts[0].tv_nsec = ts[1].tv_nsec = UTIME_NOW;

	return (dofutimens(p, SCARG(uap, fd), ts));
}

int
dofutimens(struct proc *p, int fd, struct timespec ts[2])
{
	struct file *fp;
	struct vnode *vp;
	int error;

	if ((error = getvnode(p, fd, &fp)) != 0)
		return (error);
	vp = fp->f_data;
	vref(vp);
	FRELE(fp, p);

	return (dovutimens(p, vp, ts));
}

/*
 * Truncate a file given a vnode.
 */
int
dotruncate(struct proc *p, struct vnode *vp, off_t len)
{
	struct vattr vattr;
	int error;

	if (len < 0)
		return EINVAL;
	if (vp->v_type == VDIR)
		return EISDIR;
	if ((error = vn_writechk(vp)) != 0)
		return error;
	if (vp->v_type == VREG && len > lim_cur_proc(p, RLIMIT_FSIZE)) {
		if ((error = VOP_GETATTR(vp, &vattr, p->p_ucred, p)) != 0)
			return error;
		if (len > vattr.va_size) {
			/* if extending over the limit, send signal and fail */
			psignal(p, SIGXFSZ);
			return EFBIG;
		}
	}
	vattr_null(&vattr);
	vattr.va_size = len;
	return VOP_SETATTR(vp, &vattr, p->p_ucred, p);
}

/*
 * Truncate a file given its path name.
 */
int
sys_truncate(struct proc *p, void *v, register_t *retval)
{
	struct sys_truncate_args /* {
		syscallarg(const char *) path;
		syscallarg(off_t) length;
	} */ *uap = v;
	struct vnode *vp;
	int error;
	struct nameidata nd;

	NDINIT(&nd, LOOKUP, FOLLOW, UIO_USERSPACE, SCARG(uap, path), p);
	nd.ni_pledge = PLEDGE_FATTR | PLEDGE_RPATH;
	nd.ni_unveil = UNVEIL_WRITE;
	if ((error = namei(&nd)) != 0)
		return (error);
	vp = nd.ni_vp;
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	if ((error = VOP_ACCESS(vp, VWRITE, p->p_ucred, p)) == 0)
		error = dotruncate(p, vp, SCARG(uap, length));
	vput(vp);
	return (error);
}

/*
 * Truncate a file given a file descriptor.
 */
int
sys_ftruncate(struct proc *p, void *v, register_t *retval)
{
	struct sys_ftruncate_args /* {
		syscallarg(int) fd;
		syscallarg(off_t) length;
	} */ *uap = v;
	struct vnode *vp;
	struct file *fp;
	int error;

	if ((error = getvnode(p, SCARG(uap, fd), &fp)) != 0)
		return (error);
	if ((fp->f_flag & FWRITE) == 0) {
		error = EINVAL;
		goto bad;
	}
	vp = fp->f_data;
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	error = dotruncate(p, vp, SCARG(uap, length));
	VOP_UNLOCK(vp);
bad:
	FRELE(fp, p);
	return (error);
}

/*
 * Sync an open file.
 */
int
sys_fsync(struct proc *p, void *v, register_t *retval)
{
	struct sys_fsync_args /* {
		syscallarg(int) fd;
	} */ *uap = v;
	struct vnode *vp;
	struct file *fp;
	int error;

	if ((error = getvnode(p, SCARG(uap, fd), &fp)) != 0)
		return (error);
	vp = fp->f_data;
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	error = VOP_FSYNC(vp, fp->f_cred, MNT_WAIT, p);

	VOP_UNLOCK(vp);
	FRELE(fp, p);
	return (error);
}

/*
 * Rename files.  Source and destination must either both be directories,
 * or both not be directories.  If target is a directory, it must be empty.
 */
int
sys_rename(struct proc *p, void *v, register_t *retval)
{
	struct sys_rename_args /* {
		syscallarg(const char *) from;
		syscallarg(const char *) to;
	} */ *uap = v;

	return (dorenameat(p, AT_FDCWD, SCARG(uap, from), AT_FDCWD,
	    SCARG(uap, to)));
}

int
sys_renameat(struct proc *p, void *v, register_t *retval)
{
	struct sys_renameat_args /* {
		syscallarg(int) fromfd;
		syscallarg(const char *) from;
		syscallarg(int) tofd;
		syscallarg(const char *) to;
	} */ *uap = v;

	return (dorenameat(p, SCARG(uap, fromfd), SCARG(uap, from),
	    SCARG(uap, tofd), SCARG(uap, to)));
}

int
dorenameat(struct proc *p, int fromfd, const char *from, int tofd,
    const char *to)
{
	struct vnode *tvp, *fvp, *tdvp;
	struct nameidata fromnd, tond;
	int error;
	int flags;

	NDINITAT(&fromnd, DELETE, WANTPARENT | SAVESTART, UIO_USERSPACE,
	    fromfd, from, p);
	fromnd.ni_pledge = PLEDGE_RPATH | PLEDGE_CPATH;
	fromnd.ni_unveil = UNVEIL_READ | UNVEIL_CREATE;
	if ((error = namei(&fromnd)) != 0)
		return (error);
	fvp = fromnd.ni_vp;

	flags = LOCKPARENT | LOCKLEAF | NOCACHE | SAVESTART;
	/*
	 * rename("foo/", "bar/");  is  OK
	 */
	if (fvp->v_type == VDIR)
		flags |= STRIPSLASHES;

	NDINITAT(&tond, RENAME, flags, UIO_USERSPACE, tofd, to, p);
	tond.ni_pledge = PLEDGE_CPATH;
	tond.ni_unveil = UNVEIL_CREATE;
	if ((error = namei(&tond)) != 0) {
		VOP_ABORTOP(fromnd.ni_dvp, &fromnd.ni_cnd);
		vrele(fromnd.ni_dvp);
		vrele(fvp);
		goto out1;
	}
	tdvp = tond.ni_dvp;
	tvp = tond.ni_vp;
	if (tvp != NULL) {
		if (fvp->v_type == VDIR && tvp->v_type != VDIR) {
			error = ENOTDIR;
			goto out;
		} else if (fvp->v_type != VDIR && tvp->v_type == VDIR) {
			error = EISDIR;
			goto out;
		}
	}
	if (fvp == tdvp)
		error = EINVAL;
	/*
	 * If source is the same as the destination (that is the
	 * same inode number)
	 */
	if (fvp == tvp)
		error = -1;
out:
	if (!error) {
		if (tvp) {
			(void)uvm_vnp_uncache(tvp);
		}
		error = VOP_RENAME(fromnd.ni_dvp, fromnd.ni_vp, &fromnd.ni_cnd,
				   tond.ni_dvp, tond.ni_vp, &tond.ni_cnd);
	} else {
		VOP_ABORTOP(tond.ni_dvp, &tond.ni_cnd);
		if (tdvp == tvp)
			vrele(tdvp);
		else
			vput(tdvp);
		if (tvp)
			vput(tvp);
		VOP_ABORTOP(fromnd.ni_dvp, &fromnd.ni_cnd);
		vrele(fromnd.ni_dvp);
		vrele(fvp);
	}
	vrele(tond.ni_startdir);
	pool_put(&namei_pool, tond.ni_cnd.cn_pnbuf);
out1:
	if (fromnd.ni_startdir)
		vrele(fromnd.ni_startdir);
	pool_put(&namei_pool, fromnd.ni_cnd.cn_pnbuf);
	if (error == -1)
		return (0);
	return (error);
}

/*
 * Make a directory file.
 */
int
sys_mkdir(struct proc *p, void *v, register_t *retval)
{
	struct sys_mkdir_args /* {
		syscallarg(const char *) path;
		syscallarg(mode_t) mode;
	} */ *uap = v;

	return (domkdirat(p, AT_FDCWD, SCARG(uap, path), SCARG(uap, mode)));
}

int
sys_mkdirat(struct proc *p, void *v, register_t *retval)
{
	struct sys_mkdirat_args /* {
		syscallarg(int) fd;
		syscallarg(const char *) path;
		syscallarg(mode_t) mode;
	} */ *uap = v;

	return (domkdirat(p, SCARG(uap, fd), SCARG(uap, path),
	    SCARG(uap, mode)));
}

int
domkdirat(struct proc *p, int fd, const char *path, mode_t mode)
{
	struct vnode *vp;
	struct vattr vattr;
	int error;
	struct nameidata nd;

	NDINITAT(&nd, CREATE, LOCKPARENT | STRIPSLASHES, UIO_USERSPACE,
	    fd, path, p);
	nd.ni_pledge = PLEDGE_CPATH;
	nd.ni_unveil = UNVEIL_CREATE;
	if ((error = namei(&nd)) != 0)
		return (error);
	vp = nd.ni_vp;
	if (vp != NULL) {
		VOP_ABORTOP(nd.ni_dvp, &nd.ni_cnd);
		if (nd.ni_dvp == vp)
			vrele(nd.ni_dvp);
		else
			vput(nd.ni_dvp);
		vrele(vp);
		return (EEXIST);
	}
	vattr_null(&vattr);
	vattr.va_type = VDIR;
	vattr.va_mode = (mode & ACCESSPERMS) &~ p->p_fd->fd_cmask;
	error = VOP_MKDIR(nd.ni_dvp, &nd.ni_vp, &nd.ni_cnd, &vattr);
	if (!error)
		vput(nd.ni_vp);
	return (error);
}

/*
 * Remove a directory file.
 */
int
sys_rmdir(struct proc *p, void *v, register_t *retval)
{
	struct sys_rmdir_args /* {
		syscallarg(const char *) path;
	} */ *uap = v;

	return (dounlinkat(p, AT_FDCWD, SCARG(uap, path), AT_REMOVEDIR));
}

/*
 * Read a block of directory entries in a file system independent format.
 */
int
sys_getdents(struct proc *p, void *v, register_t *retval)
{
	struct sys_getdents_args /* {
		syscallarg(int) fd;
		syscallarg(void *) buf;
		syscallarg(size_t) buflen;
	} */ *uap = v;
	struct vnode *vp;
	struct file *fp;
	struct uio auio;
	struct iovec aiov;
	size_t buflen;
	int error, eofflag;

	buflen = SCARG(uap, buflen);

	if (buflen > INT_MAX)
		return (EINVAL);
	if ((error = getvnode(p, SCARG(uap, fd), &fp)) != 0)
		return (error);
	if ((fp->f_flag & FREAD) == 0) {
		error = EBADF;
		goto bad;
	}
	vp = fp->f_data;
	if (vp->v_type != VDIR) {
		error = EINVAL;
		goto bad;
	}

	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);

	if (fp->f_offset < 0) {
		VOP_UNLOCK(vp);
		error = EINVAL;
		goto bad;
	}

	aiov.iov_base = SCARG(uap, buf);
	aiov.iov_len = buflen;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_rw = UIO_READ;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_procp = p;
	auio.uio_resid = buflen;
	auio.uio_offset = fp->f_offset;
	error = VOP_READDIR(vp, &auio, fp->f_cred, &eofflag);
	mtx_enter(&fp->f_mtx);
	fp->f_offset = auio.uio_offset;
	mtx_leave(&fp->f_mtx);
	VOP_UNLOCK(vp);
	if (error)
		goto bad;
	*retval = buflen - auio.uio_resid;
bad:
	FRELE(fp, p);
	return (error);
}

/*
 * Set the mode mask for creation of filesystem nodes.
 */
int
sys_umask(struct proc *p, void *v, register_t *retval)
{
	struct sys_umask_args /* {
		syscallarg(mode_t) newmask;
	} */ *uap = v;
	struct filedesc *fdp = p->p_fd;

	fdplock(fdp);
	*retval = fdp->fd_cmask;
	fdp->fd_cmask = SCARG(uap, newmask) & ACCESSPERMS;
	fdpunlock(fdp);
	return (0);
}

/*
 * Void all references to file by ripping underlying filesystem
 * away from vnode.
 */
int
sys_revoke(struct proc *p, void *v, register_t *retval)
{
	struct sys_revoke_args /* {
		syscallarg(const char *) path;
	} */ *uap = v;
	struct vnode *vp;
	struct vattr vattr;
	int error;
	struct nameidata nd;

	NDINIT(&nd, LOOKUP, FOLLOW, UIO_USERSPACE, SCARG(uap, path), p);
	nd.ni_pledge = PLEDGE_RPATH | PLEDGE_TTY;
	nd.ni_unveil = UNVEIL_READ;
	if ((error = namei(&nd)) != 0)
		return (error);
	vp = nd.ni_vp;
	if (vp->v_type != VCHR || (u_int)major(vp->v_rdev) >= nchrdev ||
	    cdevsw[major(vp->v_rdev)].d_type != D_TTY) {
		error = ENOTTY;
		goto out;
	}
	if ((error = VOP_GETATTR(vp, &vattr, p->p_ucred, p)) != 0)
		goto out;
	if (p->p_ucred->cr_uid != vattr.va_uid &&
	    (error = suser(p)))
		goto out;
	if (vp->v_usecount > 1 || (vp->v_flag & (VALIASED)))
		VOP_REVOKE(vp, REVOKEALL);
out:
	vrele(vp);
	return (error);
}

/*
 * Convert a user file descriptor to a kernel file entry.
 *
 * On return *fpp is FREF:ed.
 */
int
getvnode(struct proc *p, int fd, struct file **fpp)
{
	struct file *fp;
	struct vnode *vp;

	if ((fp = fd_getfile(p->p_fd, fd)) == NULL)
		return (EBADF);

	if (fp->f_type != DTYPE_VNODE) {
		FRELE(fp, p);
		return (EINVAL);
	}

	vp = fp->f_data;
	if (vp->v_type == VBAD) {
		FRELE(fp, p);
		return (EBADF);
	}

	*fpp = fp;

	return (0);
}

/*
 * Positional read system call.
 */
int
sys_pread(struct proc *p, void *v, register_t *retval)
{
	struct sys_pread_args /* {
		syscallarg(int) fd;
		syscallarg(void *) buf;
		syscallarg(size_t) nbyte;
		syscallarg(off_t) offset;
	} */ *uap = v;
	struct iovec iov;
	struct uio auio;

	iov.iov_base = SCARG(uap, buf);
	iov.iov_len = SCARG(uap, nbyte);
	if (iov.iov_len > SSIZE_MAX)
		return (EINVAL);

	auio.uio_iov = &iov;
	auio.uio_iovcnt = 1;
	auio.uio_resid = iov.iov_len;
	auio.uio_offset = SCARG(uap, offset);

	return (dofilereadv(p, SCARG(uap, fd), &auio, FO_POSITION, retval));
}

/*
 * Positional scatter read system call.
 */
int
sys_preadv(struct proc *p, void *v, register_t *retval)
{
	struct sys_preadv_args /* {
		syscallarg(int) fd;
		syscallarg(const struct iovec *) iovp;
		syscallarg(int) iovcnt;
		syscallarg(off_t) offset;
	} */ *uap = v;
	struct iovec aiov[UIO_SMALLIOV], *iov = NULL;
	int error, iovcnt = SCARG(uap, iovcnt);
	struct uio auio;
	size_t resid;

	error = iovec_copyin(SCARG(uap, iovp), &iov, aiov, iovcnt, &resid);
	if (error)
		goto done;

	auio.uio_iov = iov;
	auio.uio_iovcnt = iovcnt;
	auio.uio_resid = resid;
	auio.uio_offset = SCARG(uap, offset);

	error = dofilereadv(p, SCARG(uap, fd), &auio, FO_POSITION, retval);
 done:
	iovec_free(iov, iovcnt);
	return (error);
}

/*
 * Positional write system call.
 */
int
sys_pwrite(struct proc *p, void *v, register_t *retval)
{
	struct sys_pwrite_args /* {
		syscallarg(int) fd;
		syscallarg(const void *) buf;
		syscallarg(size_t) nbyte;
		syscallarg(off_t) offset;
	} */ *uap = v;
	struct iovec iov;
	struct uio auio;

	iov.iov_base = (void *)SCARG(uap, buf);
	iov.iov_len = SCARG(uap, nbyte);
	if (iov.iov_len > SSIZE_MAX)
		return (EINVAL);

	auio.uio_iov = &iov;
	auio.uio_iovcnt = 1;
	auio.uio_resid = iov.iov_len;
	auio.uio_offset = SCARG(uap, offset);

	return (dofilewritev(p, SCARG(uap, fd), &auio, FO_POSITION, retval));
}

/*
 * Positional gather write system call.
 */
int
sys_pwritev(struct proc *p, void *v, register_t *retval)
{
	struct sys_pwritev_args /* {
		syscallarg(int) fd;
		syscallarg(const struct iovec *) iovp;
		syscallarg(int) iovcnt;
		syscallarg(off_t) offset;
	} */ *uap = v;
	struct iovec aiov[UIO_SMALLIOV], *iov = NULL;
	int error, iovcnt = SCARG(uap, iovcnt);
	struct uio auio;
	size_t resid;

	error = iovec_copyin(SCARG(uap, iovp), &iov, aiov, iovcnt, &resid);
	if (error)
		goto done;

	auio.uio_iov = iov;
	auio.uio_iovcnt = iovcnt;
	auio.uio_resid = resid;
	auio.uio_offset = SCARG(uap, offset);

	error = dofilewritev(p, SCARG(uap, fd), &auio, FO_POSITION, retval);
 done:
	iovec_free(iov, iovcnt);
	return (error);
}
