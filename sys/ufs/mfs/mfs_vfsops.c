/*	$OpenBSD: mfs_vfsops.c,v 1.63 2024/10/17 09:11:35 claudio Exp $	*/
/*	$NetBSD: mfs_vfsops.c,v 1.10 1996/02/09 22:31:28 christos Exp $	*/

/*
 * Copyright (c) 1989, 1990, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)mfs_vfsops.c	8.4 (Berkeley) 4/16/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/mount.h>
#include <sys/signalvar.h>
#include <sys/vnode.h>
#include <sys/malloc.h>

#include <ufs/ufs/quota.h>
#include <ufs/ufs/inode.h>
#include <ufs/ufs/ufsmount.h>
#include <ufs/ufs/ufs_extern.h>

#include <ufs/ffs/fs.h>
#include <ufs/ffs/ffs_extern.h>

#include <ufs/mfs/mfsnode.h>
#include <ufs/mfs/mfs_extern.h>

static	int mfs_minor;	/* used for building internal dev_t */

/*
 * mfs vfs operations.
 */
const struct vfsops mfs_vfsops = {
	.vfs_mount	= mfs_mount,
	.vfs_start	= mfs_start,
	.vfs_unmount	= ffs_unmount,
	.vfs_root	= ufs_root,
	.vfs_quotactl	= ufs_quotactl,
	.vfs_statfs	= ffs_statfs,
	.vfs_sync	= ffs_sync,
	.vfs_vget	= ffs_vget,
	.vfs_fhtovp	= ffs_fhtovp,
	.vfs_vptofh	= ffs_vptofh,
	.vfs_init	= mfs_init,
	.vfs_sysctl	= ffs_sysctl,
	.vfs_checkexp	= mfs_checkexp,
};

/*
 * VFS Operations.
 *
 * mount system call
 */
int
mfs_mount(struct mount *mp, const char *path, void *data,
    struct nameidata *ndp, struct proc *p)
{
	struct vnode *devvp;
	struct mfs_args *args = data;
	struct ufsmount *ump;
	struct fs *fs;
	struct mfsnode *mfsp;
	char fspec[MNAMELEN];
	int flags, error;

	/*
	 * If updating, check whether changing from read-only to
	 * read/write; if there is no device name, that's all we do.
	 */
	if (mp->mnt_flag & MNT_UPDATE) {
		ump = VFSTOUFS(mp);
		fs = ump->um_fs;
		if (fs->fs_ronly == 0 && (mp->mnt_flag & MNT_RDONLY)) {
			flags = WRITECLOSE;
			if (mp->mnt_flag & MNT_FORCE)
				flags |= FORCECLOSE;
			error = ffs_flushfiles(mp, flags, p);
			if (error)
				return (error);
		}
		if (fs->fs_ronly && (mp->mnt_flag & MNT_WANTRDWR))
			fs->fs_ronly = 0;
#ifdef EXPORTMFS
		if (args && args->fspec == NULL)
			return (vfs_export(mp, &ump->um_export, 
			    &args->export_info));
#endif
		return (0);
	}
	error = copyinstr(args->fspec, fspec, sizeof(fspec), NULL);
	if (error)
		return (error);
	error = getnewvnode(VT_MFS, NULL, &mfs_vops, &devvp);
	if (error)
		return (error);
	devvp->v_type = VBLK;
	if (checkalias(devvp, makedev(255, mfs_minor), NULL))
		panic("mfs_mount: dup dev");
	mfs_minor++;
	mfsp = malloc(sizeof *mfsp, M_MFSNODE, M_WAITOK | M_ZERO);
	devvp->v_data = mfsp;
	mfsp->mfs_baseoff = args->base;
	mfsp->mfs_size = args->size;
	mfsp->mfs_vnode = devvp;
	mfsp->mfs_tid = p->p_tid;
	bufq_init(&mfsp->mfs_bufq, BUFQ_FIFO);
	if ((error = ffs_mountfs(devvp, mp, p)) != 0) {
		mfsp->mfs_shutdown = 1;
		vrele(devvp);
		return (error);
	}
	ump = VFSTOUFS(mp);
	fs = ump->um_fs;

	memset(fs->fs_fsmnt, 0, sizeof(fs->fs_fsmnt));
	strlcpy(fs->fs_fsmnt, path, sizeof(fs->fs_fsmnt));
	memcpy(mp->mnt_stat.f_mntonname, fs->fs_fsmnt, MNAMELEN);
	memset(mp->mnt_stat.f_mntfromname, 0, MNAMELEN);
	strlcpy(mp->mnt_stat.f_mntfromname, fspec, MNAMELEN);
	memset(mp->mnt_stat.f_mntfromspec, 0, MNAMELEN);
	strlcpy(mp->mnt_stat.f_mntfromspec, fspec, MNAMELEN);
	memcpy(&mp->mnt_stat.mount_info.mfs_args, args, sizeof(*args));

	return (0);
}

/*
 * Used to grab the process and keep it in the kernel to service
 * memory filesystem I/O requests.
 *
 * Loop servicing I/O requests.
 * Copy the requested data into or out of the memory filesystem
 * address space.
 */
int
mfs_start(struct mount *mp, int flags, struct proc *p)
{
	struct vnode *vp = VFSTOUFS(mp)->um_devvp;
	struct mfsnode *mfsp = VTOMFS(vp);
	struct buf *bp;
	int sleepreturn = 0, sig;
	struct sigctx ctx;

	while (1) {
		while (1) {
			if (mfsp->mfs_shutdown == 1)
				break;
			bp = bufq_dequeue(&mfsp->mfs_bufq);
			if (bp == NULL)
				break;
			mfs_doio(mfsp, bp);
			wakeup(bp);
		}
		if (mfsp->mfs_shutdown == 1)
			break;

		/*
		 * If a non-ignored signal is received, try to unmount.
		 * If that fails, clear the signal (it has been "processed"),
		 * otherwise we will loop here, as tsleep will always return
		 * EINTR/ERESTART.
		 */
		if (sleepreturn != 0) {
			sig = cursig(p, &ctx, 0);
			if (vfs_busy(mp, VB_WRITE|VB_NOWAIT) ||
			    dounmount(mp, (sig == SIGKILL) ? MNT_FORCE : 0, p))
				atomic_clearbits_int(&p->p_siglist,
				    sigmask(sig));
			sleepreturn = 0;
			continue;
		}
		sleepreturn = tsleep_nsec(vp, PWAIT | PCATCH, "mfsidl", INFSLP);
	}
	return (0);
}

/*
 * check export permission, not supported
 */
int
mfs_checkexp(struct mount *mp, struct mbuf *nam, int *exflagsp,
    struct ucred **credanonp)
{
	return (EOPNOTSUPP);
}

/*
 * Memory based filesystem initialization.
 */
int
mfs_init(struct vfsconf *vfsp)
{
	return (ffs_init(vfsp));
}
