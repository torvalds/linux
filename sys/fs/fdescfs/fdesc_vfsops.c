/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1992, 1993, 1995
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software donated to Berkeley by
 * Jan-Simon Pendry.
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
 *	@(#)fdesc_vfsops.c	8.4 (Berkeley) 1/21/94
 *
 * $FreeBSD$
 */

/*
 * /dev/fd Filesystem
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/filedesc.h>
#include <sys/kernel.h>
#include <sys/jail.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/racct.h>
#include <sys/resourcevar.h>
#include <sys/vnode.h>

#include <fs/fdescfs/fdesc.h>

static MALLOC_DEFINE(M_FDESCMNT, "fdesc_mount", "FDESC mount structure");

static vfs_cmount_t	fdesc_cmount;
static vfs_mount_t	fdesc_mount;
static vfs_unmount_t	fdesc_unmount;
static vfs_statfs_t	fdesc_statfs;
static vfs_root_t	fdesc_root;

/*
 * Compatibility shim for old mount(2) system call.
 */
int
fdesc_cmount(struct mntarg *ma, void *data, uint64_t flags)
{

	return kernel_mount(ma, flags);
}

/*
 * Mount the per-process file descriptors (/dev/fd)
 */
static int
fdesc_mount(struct mount *mp)
{
	struct fdescmount *fmp;
	struct vnode *rvp;
	int error;

	/*
	 * Update is a no-op
	 */
	if (mp->mnt_flag & (MNT_UPDATE | MNT_ROOTFS))
		return (EOPNOTSUPP);

	fmp = malloc(sizeof(struct fdescmount),
				M_FDESCMNT, M_WAITOK);	/* XXX */

	/*
	 * We need to initialize a few bits of our local mount point struct to
	 * avoid confusion in allocvp.
	 */
	mp->mnt_data = fmp;
	fmp->flags = 0;
	if (vfs_getopt(mp->mnt_optnew, "linrdlnk", NULL, NULL) == 0)
		fmp->flags |= FMNT_LINRDLNKF;
	error = fdesc_allocvp(Froot, -1, FD_ROOT, mp, &rvp);
	if (error) {
		free(fmp, M_FDESCMNT);
		mp->mnt_data = NULL;
		return (error);
	}
	rvp->v_type = VDIR;
	rvp->v_vflag |= VV_ROOT;
	fmp->f_root = rvp;
	VOP_UNLOCK(rvp, 0);
	/* XXX -- don't mark as local to work around fts() problems */
	/*mp->mnt_flag |= MNT_LOCAL;*/
	vfs_getnewfsid(mp);

	vfs_mountedfrom(mp, "fdescfs");
	return (0);
}

static int
fdesc_unmount(struct mount *mp, int mntflags)
{
	struct fdescmount *fmp;
	int error, flags;

	flags = 0;
	fmp = mp->mnt_data;
	if (mntflags & MNT_FORCE) {
		/* The hash mutex protects the private mount flags. */
		mtx_lock(&fdesc_hashmtx);
		fmp->flags |= FMNT_UNMOUNTF;
		mtx_unlock(&fdesc_hashmtx);
		flags |= FORCECLOSE;
	}

	/*
	 * Clear out buffer cache.  I don't think we
	 * ever get anything cached at this level at the
	 * moment, but who knows...
	 *
	 * There is 1 extra root vnode reference corresponding
	 * to f_root.
	 */
	if ((error = vflush(mp, 1, flags, curthread)) != 0)
		return (error);

	/*
	 * Finally, throw away the fdescmount structure.
	 */
	mp->mnt_data = NULL;
	free(fmp, M_FDESCMNT);
	return (0);
}

static int
fdesc_root(struct mount *mp, int flags, struct vnode **vpp)
{
	struct vnode *vp;

	/*
	 * Return locked reference to root.
	 */
	vp = VFSTOFDESC(mp)->f_root;
	vget(vp, LK_EXCLUSIVE | LK_RETRY, curthread);
	*vpp = vp;
	return (0);
}

static int
fdesc_statfs(struct mount *mp, struct statfs *sbp)
{
	struct thread *td;
	struct filedesc *fdp;
	int lim;
	int i;
	int last;
	int freefd;
	uint64_t limit;

	td = curthread;

	/*
	 * Compute number of free file descriptors.
	 * [ Strange results will ensue if the open file
	 * limit is ever reduced below the current number
	 * of open files... ]
	 */
	lim = lim_cur(td, RLIMIT_NOFILE);
	fdp = td->td_proc->p_fd;
	FILEDESC_SLOCK(fdp);
	limit = racct_get_limit(td->td_proc, RACCT_NOFILE);
	if (lim > limit)
		lim = limit;
	last = min(fdp->fd_nfiles, lim);
	freefd = 0;
	for (i = fdp->fd_freefile; i < last; i++)
		if (fdp->fd_ofiles[i].fde_file == NULL)
			freefd++;

	/*
	 * Adjust for the fact that the fdesc array may not
	 * have been fully allocated yet.
	 */
	if (fdp->fd_nfiles < lim)
		freefd += (lim - fdp->fd_nfiles);
	FILEDESC_SUNLOCK(fdp);

	sbp->f_flags = 0;
	sbp->f_bsize = DEV_BSIZE;
	sbp->f_iosize = DEV_BSIZE;
	sbp->f_blocks = 2;		/* 1K to keep df happy */
	sbp->f_bfree = 0;
	sbp->f_bavail = 0;
	sbp->f_files = lim + 1;		/* Allow for "." */
	sbp->f_ffree = freefd;		/* See comments above */
	return (0);
}

static struct vfsops fdesc_vfsops = {
	.vfs_cmount =		fdesc_cmount,
	.vfs_init =		fdesc_init,
	.vfs_mount =		fdesc_mount,
	.vfs_root =		fdesc_root,
	.vfs_statfs =		fdesc_statfs,
	.vfs_uninit =		fdesc_uninit,
	.vfs_unmount =		fdesc_unmount,
};

VFS_SET(fdesc_vfsops, fdescfs, VFCF_SYNTHETIC | VFCF_JAIL);
