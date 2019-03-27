/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1992, 1993, 1995
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2000
 *	Poul-Henning Kamp.  All rights reserved.
 *
 * This code is derived from software donated to Berkeley by
 * Jan-Simon Pendry.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Neither the name of the University nor the names of its contributors
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
 *	@(#)kernfs_vfsops.c	8.10 (Berkeley) 5/14/95
 * From: FreeBSD: src/sys/miscfs/kernfs/kernfs_vfsops.c 1.36
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/sx.h>
#include <sys/vnode.h>
#include <sys/limits.h>
#include <sys/jail.h>

#include <fs/devfs/devfs.h>

static struct unrhdr	*devfs_unr;

MALLOC_DEFINE(M_DEVFS, "DEVFS", "DEVFS data");

static vfs_mount_t	devfs_mount;
static vfs_unmount_t	devfs_unmount;
static vfs_root_t	devfs_root;
static vfs_statfs_t	devfs_statfs;

static const char *devfs_opts[] = {
	"from", "export", "ruleset", NULL
};

/*
 * Mount the filesystem
 */
static int
devfs_mount(struct mount *mp)
{
	int error;
	struct devfs_mount *fmp;
	struct vnode *rvp;
	struct thread *td = curthread;
	int injail, rsnum;

	if (devfs_unr == NULL)
		devfs_unr = new_unrhdr(0, INT_MAX, NULL);

	error = 0;

	if (mp->mnt_flag & MNT_ROOTFS)
		return (EOPNOTSUPP);

	rsnum = 0;
	injail = jailed(td->td_ucred);

	if (mp->mnt_optnew != NULL) {
		if (vfs_filteropt(mp->mnt_optnew, devfs_opts))
			return (EINVAL);

		if (vfs_flagopt(mp->mnt_optnew, "export", NULL, 0))
			return (EOPNOTSUPP);

		if (vfs_getopt(mp->mnt_optnew, "ruleset", NULL, NULL) == 0 &&
		    (vfs_scanopt(mp->mnt_optnew, "ruleset", "%d",
		    &rsnum) != 1 || rsnum < 0 || rsnum > 65535)) {
			vfs_mount_error(mp, "%s",
			    "invalid ruleset specification");
			return (EINVAL);
		}

		if (injail && rsnum != 0 &&
		    rsnum != td->td_ucred->cr_prison->pr_devfs_rsnum)
			return (EPERM);
	}

	/* jails enforce their ruleset */
	if (injail)
		rsnum = td->td_ucred->cr_prison->pr_devfs_rsnum;

	if (mp->mnt_flag & MNT_UPDATE) {
		if (rsnum != 0) {
			fmp = mp->mnt_data;
			if (fmp != NULL) {
				sx_xlock(&fmp->dm_lock);
				devfs_ruleset_set((devfs_rsnum)rsnum, fmp);
				devfs_ruleset_apply(fmp);
				sx_xunlock(&fmp->dm_lock);
			}
		}
		return (0);
	}

	fmp = malloc(sizeof *fmp, M_DEVFS, M_WAITOK | M_ZERO);
	fmp->dm_idx = alloc_unr(devfs_unr);
	sx_init(&fmp->dm_lock, "devfsmount");
	fmp->dm_holdcnt = 1;

	MNT_ILOCK(mp);
	mp->mnt_flag |= MNT_LOCAL;
	mp->mnt_kern_flag |= MNTK_LOOKUP_SHARED | MNTK_EXTENDED_SHARED;
#ifdef MAC
	mp->mnt_flag |= MNT_MULTILABEL;
#endif
	MNT_IUNLOCK(mp);
	fmp->dm_mount = mp;
	mp->mnt_data = (void *) fmp;
	vfs_getnewfsid(mp);

	fmp->dm_rootdir = devfs_vmkdir(fmp, NULL, 0, NULL, DEVFS_ROOTINO);

	error = devfs_root(mp, LK_EXCLUSIVE, &rvp);
	if (error) {
		sx_destroy(&fmp->dm_lock);
		free_unr(devfs_unr, fmp->dm_idx);
		free(fmp, M_DEVFS);
		return (error);
	}

	if (rsnum != 0) {
		sx_xlock(&fmp->dm_lock);
		devfs_ruleset_set((devfs_rsnum)rsnum, fmp);
		sx_xunlock(&fmp->dm_lock);
	}

	VOP_UNLOCK(rvp, 0);

	vfs_mountedfrom(mp, "devfs");

	return (0);
}

void
devfs_unmount_final(struct devfs_mount *fmp)
{
	sx_destroy(&fmp->dm_lock);
	free(fmp, M_DEVFS);
}

static int
devfs_unmount(struct mount *mp, int mntflags)
{
	int error;
	int flags = 0;
	struct devfs_mount *fmp;
	int hold;
	u_int idx;

	fmp = VFSTODEVFS(mp);
	KASSERT(fmp->dm_mount != NULL,
		("devfs_unmount unmounted devfs_mount"));
	if (mntflags & MNT_FORCE)
		flags |= FORCECLOSE;
	/* There is 1 extra root vnode reference from devfs_mount(). */
	error = vflush(mp, 1, flags, curthread);
	if (error)
		return (error);
	sx_xlock(&fmp->dm_lock);
	devfs_cleanup(fmp);
	devfs_rules_cleanup(fmp);
	fmp->dm_mount = NULL;
	hold = --fmp->dm_holdcnt;
	mp->mnt_data = NULL;
	idx = fmp->dm_idx;
	sx_xunlock(&fmp->dm_lock);
	free_unr(devfs_unr, idx);
	if (hold == 0)
		devfs_unmount_final(fmp);
	return 0;
}

/* Return locked reference to root.  */

static int
devfs_root(struct mount *mp, int flags, struct vnode **vpp)
{
	int error;
	struct vnode *vp;
	struct devfs_mount *dmp;

	dmp = VFSTODEVFS(mp);
	sx_xlock(&dmp->dm_lock);
	error = devfs_allocv(dmp->dm_rootdir, mp, LK_EXCLUSIVE, &vp);
	if (error)
		return (error);
	vp->v_vflag |= VV_ROOT;
	*vpp = vp;
	return (0);
}

static int
devfs_statfs(struct mount *mp, struct statfs *sbp)
{

	sbp->f_flags = 0;
	sbp->f_bsize = DEV_BSIZE;
	sbp->f_iosize = DEV_BSIZE;
	sbp->f_blocks = 2;		/* 1K to keep df happy */
	sbp->f_bfree = 0;
	sbp->f_bavail = 0;
	sbp->f_files = 0;
	sbp->f_ffree = 0;
	return (0);
}

static struct vfsops devfs_vfsops = {
	.vfs_mount =		devfs_mount,
	.vfs_root =		devfs_root,
	.vfs_statfs =		devfs_statfs,
	.vfs_unmount =		devfs_unmount,
};

VFS_SET(devfs_vfsops, devfs, VFCF_SYNTHETIC | VFCF_JAIL);
