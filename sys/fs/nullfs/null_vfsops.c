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
 *	@(#)null_vfsops.c	8.2 (Berkeley) 1/21/94
 *
 * @(#)lofs_vfsops.c	1.2 (Berkeley) 6/18/92
 * $FreeBSD$
 */

/*
 * Null Layer
 * (See null_vnops.c for a description of what this does.)
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/jail.h>

#include <fs/nullfs/null.h>

static MALLOC_DEFINE(M_NULLFSMNT, "nullfs_mount", "NULLFS mount structure");

static vfs_fhtovp_t	nullfs_fhtovp;
static vfs_mount_t	nullfs_mount;
static vfs_quotactl_t	nullfs_quotactl;
static vfs_root_t	nullfs_root;
static vfs_sync_t	nullfs_sync;
static vfs_statfs_t	nullfs_statfs;
static vfs_unmount_t	nullfs_unmount;
static vfs_vget_t	nullfs_vget;
static vfs_extattrctl_t	nullfs_extattrctl;

/*
 * Mount null layer
 */
static int
nullfs_mount(struct mount *mp)
{
	struct vnode *lowerrootvp, *vp;
	struct vnode *nullm_rootvp;
	struct null_mount *xmp;
	struct null_node *nn;
	struct nameidata nd, *ndp;
	char *target;
	int error, len;
	bool isvnunlocked;

	NULLFSDEBUG("nullfs_mount(mp = %p)\n", (void *)mp);

	if (mp->mnt_flag & MNT_ROOTFS)
		return (EOPNOTSUPP);

	/*
	 * Update is a no-op
	 */
	if (mp->mnt_flag & MNT_UPDATE) {
		/*
		 * Only support update mounts for NFS export.
		 */
		if (vfs_flagopt(mp->mnt_optnew, "export", NULL, 0))
			return (0);
		else
			return (EOPNOTSUPP);
	}

	/*
	 * Get argument
	 */
	error = vfs_getopt(mp->mnt_optnew, "target", (void **)&target, &len);
	if (error || target[len - 1] != '\0')
		return (EINVAL);

	/*
	 * Unlock lower node to avoid possible deadlock.
	 */
	if (mp->mnt_vnodecovered->v_op == &null_vnodeops &&
	    VOP_ISLOCKED(mp->mnt_vnodecovered) == LK_EXCLUSIVE) {
		VOP_UNLOCK(mp->mnt_vnodecovered, 0);
		isvnunlocked = true;
	} else {
		isvnunlocked = false;
	}

	/*
	 * Find lower node
	 */
	ndp = &nd;
	NDINIT(ndp, LOOKUP, FOLLOW|LOCKLEAF, UIO_SYSSPACE, target, curthread);
	error = namei(ndp);

	/*
	 * Re-lock vnode.
	 * XXXKIB This is deadlock-prone as well.
	 */
	if (isvnunlocked)
		vn_lock(mp->mnt_vnodecovered, LK_EXCLUSIVE | LK_RETRY);

	if (error)
		return (error);
	NDFREE(ndp, NDF_ONLY_PNBUF);

	/*
	 * Sanity check on lower vnode
	 */
	lowerrootvp = ndp->ni_vp;

	/*
	 * Check multi null mount to avoid `lock against myself' panic.
	 */
	if (mp->mnt_vnodecovered->v_op == &null_vnodeops) {
		nn = VTONULL(mp->mnt_vnodecovered);
		if (nn == NULL || lowerrootvp == nn->null_lowervp) {
			NULLFSDEBUG("nullfs_mount: multi null mount?\n");
			vput(lowerrootvp);
			return (EDEADLK);
		}
	}

	xmp = (struct null_mount *) malloc(sizeof(struct null_mount),
	    M_NULLFSMNT, M_WAITOK | M_ZERO);

	/*
	 * Save reference to underlying FS
	 */
	xmp->nullm_vfs = lowerrootvp->v_mount;

	/*
	 * Save reference.  Each mount also holds
	 * a reference on the root vnode.
	 */
	error = null_nodeget(mp, lowerrootvp, &vp);
	/*
	 * Make sure the node alias worked
	 */
	if (error) {
		free(xmp, M_NULLFSMNT);
		return (error);
	}

	/*
	 * Keep a held reference to the root vnode.
	 * It is vrele'd in nullfs_unmount.
	 */
	nullm_rootvp = vp;
	nullm_rootvp->v_vflag |= VV_ROOT;
	xmp->nullm_rootvp = nullm_rootvp;

	/*
	 * Unlock the node (either the lower or the alias)
	 */
	VOP_UNLOCK(vp, 0);

	if (NULLVPTOLOWERVP(nullm_rootvp)->v_mount->mnt_flag & MNT_LOCAL) {
		MNT_ILOCK(mp);
		mp->mnt_flag |= MNT_LOCAL;
		MNT_IUNLOCK(mp);
	}

	xmp->nullm_flags |= NULLM_CACHE;
	if (vfs_getopt(mp->mnt_optnew, "nocache", NULL, NULL) == 0 ||
	    (xmp->nullm_vfs->mnt_kern_flag & MNTK_NULL_NOCACHE) != 0)
		xmp->nullm_flags &= ~NULLM_CACHE;

	MNT_ILOCK(mp);
	if ((xmp->nullm_flags & NULLM_CACHE) != 0) {
		mp->mnt_kern_flag |= lowerrootvp->v_mount->mnt_kern_flag &
		    (MNTK_SHARED_WRITES | MNTK_LOOKUP_SHARED |
		    MNTK_EXTENDED_SHARED);
	}
	mp->mnt_kern_flag |= MNTK_LOOKUP_EXCL_DOTDOT;
	mp->mnt_kern_flag |= lowerrootvp->v_mount->mnt_kern_flag &
	    (MNTK_USES_BCACHE | MNTK_NO_IOPF | MNTK_UNMAPPED_BUFS);
	MNT_IUNLOCK(mp);
	mp->mnt_data = xmp;
	vfs_getnewfsid(mp);
	if ((xmp->nullm_flags & NULLM_CACHE) != 0) {
		MNT_ILOCK(xmp->nullm_vfs);
		TAILQ_INSERT_TAIL(&xmp->nullm_vfs->mnt_uppers, mp,
		    mnt_upper_link);
		MNT_IUNLOCK(xmp->nullm_vfs);
	}

	vfs_mountedfrom(mp, target);

	NULLFSDEBUG("nullfs_mount: lower %s, alias at %s\n",
		mp->mnt_stat.f_mntfromname, mp->mnt_stat.f_mntonname);
	return (0);
}

/*
 * Free reference to null layer
 */
static int
nullfs_unmount(mp, mntflags)
	struct mount *mp;
	int mntflags;
{
	struct null_mount *mntdata;
	struct mount *ump;
	int error, flags, rootrefs;

	NULLFSDEBUG("nullfs_unmount: mp = %p\n", (void *)mp);

	if (mntflags & MNT_FORCE)
		flags = FORCECLOSE;
	else
		flags = 0;

	for (rootrefs = 1;; rootrefs = 0) {
		/* There is 1 extra root vnode reference (nullm_rootvp). */
		error = vflush(mp, rootrefs, flags, curthread);
		if (error)
			return (error);
		MNT_ILOCK(mp);
		if (mp->mnt_nvnodelistsize == 0) {
			MNT_IUNLOCK(mp);
			break;
		}
		MNT_IUNLOCK(mp);
		if ((mntflags & MNT_FORCE) == 0)
			return (EBUSY);
	}

	/*
	 * Finally, throw away the null_mount structure
	 */
	mntdata = mp->mnt_data;
	ump = mntdata->nullm_vfs;
	if ((mntdata->nullm_flags & NULLM_CACHE) != 0) {
		MNT_ILOCK(ump);
		while ((ump->mnt_kern_flag & MNTK_VGONE_UPPER) != 0) {
			ump->mnt_kern_flag |= MNTK_VGONE_WAITER;
			msleep(&ump->mnt_uppers, &ump->mnt_mtx, 0, "vgnupw", 0);
		}
		TAILQ_REMOVE(&ump->mnt_uppers, mp, mnt_upper_link);
		MNT_IUNLOCK(ump);
	}
	mp->mnt_data = NULL;
	free(mntdata, M_NULLFSMNT);
	return (0);
}

static int
nullfs_root(mp, flags, vpp)
	struct mount *mp;
	int flags;
	struct vnode **vpp;
{
	struct vnode *vp;

	NULLFSDEBUG("nullfs_root(mp = %p, vp = %p->%p)\n", (void *)mp,
	    (void *)MOUNTTONULLMOUNT(mp)->nullm_rootvp,
	    (void *)NULLVPTOLOWERVP(MOUNTTONULLMOUNT(mp)->nullm_rootvp));

	/*
	 * Return locked reference to root.
	 */
	vp = MOUNTTONULLMOUNT(mp)->nullm_rootvp;
	VREF(vp);

	ASSERT_VOP_UNLOCKED(vp, "root vnode is locked");
	vn_lock(vp, flags | LK_RETRY);
	*vpp = vp;
	return 0;
}

static int
nullfs_quotactl(mp, cmd, uid, arg)
	struct mount *mp;
	int cmd;
	uid_t uid;
	void *arg;
{
	return VFS_QUOTACTL(MOUNTTONULLMOUNT(mp)->nullm_vfs, cmd, uid, arg);
}

static int
nullfs_statfs(mp, sbp)
	struct mount *mp;
	struct statfs *sbp;
{
	int error;
	struct statfs *mstat;

	NULLFSDEBUG("nullfs_statfs(mp = %p, vp = %p->%p)\n", (void *)mp,
	    (void *)MOUNTTONULLMOUNT(mp)->nullm_rootvp,
	    (void *)NULLVPTOLOWERVP(MOUNTTONULLMOUNT(mp)->nullm_rootvp));

	mstat = malloc(sizeof(struct statfs), M_STATFS, M_WAITOK | M_ZERO);

	error = VFS_STATFS(MOUNTTONULLMOUNT(mp)->nullm_vfs, mstat);
	if (error) {
		free(mstat, M_STATFS);
		return (error);
	}

	/* now copy across the "interesting" information and fake the rest */
	sbp->f_type = mstat->f_type;
	sbp->f_flags = (sbp->f_flags & (MNT_RDONLY | MNT_NOEXEC | MNT_NOSUID |
	    MNT_UNION | MNT_NOSYMFOLLOW | MNT_AUTOMOUNTED)) |
	    (mstat->f_flags & ~(MNT_ROOTFS | MNT_AUTOMOUNTED));
	sbp->f_bsize = mstat->f_bsize;
	sbp->f_iosize = mstat->f_iosize;
	sbp->f_blocks = mstat->f_blocks;
	sbp->f_bfree = mstat->f_bfree;
	sbp->f_bavail = mstat->f_bavail;
	sbp->f_files = mstat->f_files;
	sbp->f_ffree = mstat->f_ffree;

	free(mstat, M_STATFS);
	return (0);
}

static int
nullfs_sync(mp, waitfor)
	struct mount *mp;
	int waitfor;
{
	/*
	 * XXX - Assumes no data cached at null layer.
	 */
	return (0);
}

static int
nullfs_vget(mp, ino, flags, vpp)
	struct mount *mp;
	ino_t ino;
	int flags;
	struct vnode **vpp;
{
	int error;

	KASSERT((flags & LK_TYPE_MASK) != 0,
	    ("nullfs_vget: no lock requested"));

	error = VFS_VGET(MOUNTTONULLMOUNT(mp)->nullm_vfs, ino, flags, vpp);
	if (error != 0)
		return (error);
	return (null_nodeget(mp, *vpp, vpp));
}

static int
nullfs_fhtovp(mp, fidp, flags, vpp)
	struct mount *mp;
	struct fid *fidp;
	int flags;
	struct vnode **vpp;
{
	int error;

	error = VFS_FHTOVP(MOUNTTONULLMOUNT(mp)->nullm_vfs, fidp, flags,
	    vpp);
	if (error != 0)
		return (error);
	return (null_nodeget(mp, *vpp, vpp));
}

static int                        
nullfs_extattrctl(mp, cmd, filename_vp, namespace, attrname)
	struct mount *mp;
	int cmd;
	struct vnode *filename_vp;
	int namespace;
	const char *attrname;
{

	return (VFS_EXTATTRCTL(MOUNTTONULLMOUNT(mp)->nullm_vfs, cmd,
	    filename_vp, namespace, attrname));
}

static void
nullfs_reclaim_lowervp(struct mount *mp, struct vnode *lowervp)
{
	struct vnode *vp;

	vp = null_hashget(mp, lowervp);
	if (vp == NULL)
		return;
	VTONULL(vp)->null_flags |= NULLV_NOUNLOCK;
	vgone(vp);
	vput(vp);
}

static void
nullfs_unlink_lowervp(struct mount *mp, struct vnode *lowervp)
{
	struct vnode *vp;
	struct null_node *xp;

	vp = null_hashget(mp, lowervp);
	if (vp == NULL)
		return;
	xp = VTONULL(vp);
	xp->null_flags |= NULLV_DROP | NULLV_NOUNLOCK;
	vhold(vp);
	vunref(vp);

	if (vp->v_usecount == 0) {
		/*
		 * If vunref() dropped the last use reference on the
		 * nullfs vnode, it must be reclaimed, and its lock
		 * was split from the lower vnode lock.  Need to do
		 * extra unlock before allowing the final vdrop() to
		 * free the vnode.
		 */
		KASSERT((vp->v_iflag & VI_DOOMED) != 0,
		    ("not reclaimed nullfs vnode %p", vp));
		VOP_UNLOCK(vp, 0);
	} else {
		/*
		 * Otherwise, the nullfs vnode still shares the lock
		 * with the lower vnode, and must not be unlocked.
		 * Also clear the NULLV_NOUNLOCK, the flag is not
		 * relevant for future reclamations.
		 */
		ASSERT_VOP_ELOCKED(vp, "unlink_lowervp");
		KASSERT((vp->v_iflag & VI_DOOMED) == 0,
		    ("reclaimed nullfs vnode %p", vp));
		xp->null_flags &= ~NULLV_NOUNLOCK;
	}
	vdrop(vp);
}

static struct vfsops null_vfsops = {
	.vfs_extattrctl =	nullfs_extattrctl,
	.vfs_fhtovp =		nullfs_fhtovp,
	.vfs_init =		nullfs_init,
	.vfs_mount =		nullfs_mount,
	.vfs_quotactl =		nullfs_quotactl,
	.vfs_root =		nullfs_root,
	.vfs_statfs =		nullfs_statfs,
	.vfs_sync =		nullfs_sync,
	.vfs_uninit =		nullfs_uninit,
	.vfs_unmount =		nullfs_unmount,
	.vfs_vget =		nullfs_vget,
	.vfs_reclaim_lowervp =	nullfs_reclaim_lowervp,
	.vfs_unlink_lowervp =	nullfs_unlink_lowervp,
};

VFS_SET(null_vfsops, nullfs, VFCF_LOOPBACK | VFCF_JAIL);
