/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1991, 1993, 1994
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
 *	@(#)ffs_vfsops.c	8.31 (Berkeley) 5/20/95
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_quota.h"
#include "opt_ufs.h"
#include "opt_ffs.h"
#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/namei.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/taskqueue.h>
#include <sys/kernel.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/ioccom.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/rwlock.h>
#include <sys/vmmeter.h>

#include <security/mac/mac_framework.h>

#include <ufs/ufs/dir.h>
#include <ufs/ufs/extattr.h>
#include <ufs/ufs/gjournal.h>
#include <ufs/ufs/quota.h>
#include <ufs/ufs/ufsmount.h>
#include <ufs/ufs/inode.h>
#include <ufs/ufs/ufs_extern.h>

#include <ufs/ffs/fs.h>
#include <ufs/ffs/ffs_extern.h>

#include <vm/vm.h>
#include <vm/uma.h>
#include <vm/vm_page.h>

#include <geom/geom.h>
#include <geom/geom_vfs.h>

#include <ddb/ddb.h>

static uma_zone_t uma_inode, uma_ufs1, uma_ufs2;

static int	ffs_mountfs(struct vnode *, struct mount *, struct thread *);
static void	ffs_oldfscompat_read(struct fs *, struct ufsmount *,
		    ufs2_daddr_t);
static void	ffs_ifree(struct ufsmount *ump, struct inode *ip);
static int	ffs_sync_lazy(struct mount *mp);
static int	ffs_use_bread(void *devfd, off_t loc, void **bufp, int size);
static int	ffs_use_bwrite(void *devfd, off_t loc, void *buf, int size);

static vfs_init_t ffs_init;
static vfs_uninit_t ffs_uninit;
static vfs_extattrctl_t ffs_extattrctl;
static vfs_cmount_t ffs_cmount;
static vfs_unmount_t ffs_unmount;
static vfs_mount_t ffs_mount;
static vfs_statfs_t ffs_statfs;
static vfs_fhtovp_t ffs_fhtovp;
static vfs_sync_t ffs_sync;

static struct vfsops ufs_vfsops = {
	.vfs_extattrctl =	ffs_extattrctl,
	.vfs_fhtovp =		ffs_fhtovp,
	.vfs_init =		ffs_init,
	.vfs_mount =		ffs_mount,
	.vfs_cmount =		ffs_cmount,
	.vfs_quotactl =		ufs_quotactl,
	.vfs_root =		ufs_root,
	.vfs_statfs =		ffs_statfs,
	.vfs_sync =		ffs_sync,
	.vfs_uninit =		ffs_uninit,
	.vfs_unmount =		ffs_unmount,
	.vfs_vget =		ffs_vget,
	.vfs_susp_clean =	process_deferred_inactive,
};

VFS_SET(ufs_vfsops, ufs, 0);
MODULE_VERSION(ufs, 1);

static b_strategy_t ffs_geom_strategy;
static b_write_t ffs_bufwrite;

static struct buf_ops ffs_ops = {
	.bop_name =	"FFS",
	.bop_write =	ffs_bufwrite,
	.bop_strategy =	ffs_geom_strategy,
	.bop_sync =	bufsync,
#ifdef NO_FFS_SNAPSHOT
	.bop_bdflush =	bufbdflush,
#else
	.bop_bdflush =	ffs_bdflush,
#endif
};

/*
 * Note that userquota and groupquota options are not currently used
 * by UFS/FFS code and generally mount(8) does not pass those options
 * from userland, but they can be passed by loader(8) via
 * vfs.root.mountfrom.options.
 */
static const char *ffs_opts[] = { "acls", "async", "noatime", "noclusterr",
    "noclusterw", "noexec", "export", "force", "from", "groupquota",
    "multilabel", "nfsv4acls", "fsckpid", "snapshot", "nosuid", "suiddir",
    "nosymfollow", "sync", "union", "userquota", NULL };

static int
ffs_mount(struct mount *mp)
{
	struct vnode *devvp;
	struct thread *td;
	struct ufsmount *ump = NULL;
	struct fs *fs;
	pid_t fsckpid = 0;
	int error, error1, flags;
	uint64_t mntorflags;
	accmode_t accmode;
	struct nameidata ndp;
	char *fspec;

	td = curthread;
	if (vfs_filteropt(mp->mnt_optnew, ffs_opts))
		return (EINVAL);
	if (uma_inode == NULL) {
		uma_inode = uma_zcreate("FFS inode",
		    sizeof(struct inode), NULL, NULL, NULL, NULL,
		    UMA_ALIGN_PTR, 0);
		uma_ufs1 = uma_zcreate("FFS1 dinode",
		    sizeof(struct ufs1_dinode), NULL, NULL, NULL, NULL,
		    UMA_ALIGN_PTR, 0);
		uma_ufs2 = uma_zcreate("FFS2 dinode",
		    sizeof(struct ufs2_dinode), NULL, NULL, NULL, NULL,
		    UMA_ALIGN_PTR, 0);
	}

	vfs_deleteopt(mp->mnt_optnew, "groupquota");
	vfs_deleteopt(mp->mnt_optnew, "userquota");

	fspec = vfs_getopts(mp->mnt_optnew, "from", &error);
	if (error)
		return (error);

	mntorflags = 0;
	if (vfs_getopt(mp->mnt_optnew, "acls", NULL, NULL) == 0)
		mntorflags |= MNT_ACLS;

	if (vfs_getopt(mp->mnt_optnew, "snapshot", NULL, NULL) == 0) {
		mntorflags |= MNT_SNAPSHOT;
		/*
		 * Once we have set the MNT_SNAPSHOT flag, do not
		 * persist "snapshot" in the options list.
		 */
		vfs_deleteopt(mp->mnt_optnew, "snapshot");
		vfs_deleteopt(mp->mnt_opt, "snapshot");
	}

	if (vfs_getopt(mp->mnt_optnew, "fsckpid", NULL, NULL) == 0 &&
	    vfs_scanopt(mp->mnt_optnew, "fsckpid", "%d", &fsckpid) == 1) {
		/*
		 * Once we have set the restricted PID, do not
		 * persist "fsckpid" in the options list.
		 */
		vfs_deleteopt(mp->mnt_optnew, "fsckpid");
		vfs_deleteopt(mp->mnt_opt, "fsckpid");
		if (mp->mnt_flag & MNT_UPDATE) {
			if (VFSTOUFS(mp)->um_fs->fs_ronly == 0 &&
			     vfs_flagopt(mp->mnt_optnew, "ro", NULL, 0) == 0) {
				vfs_mount_error(mp,
				    "Checker enable: Must be read-only");
				return (EINVAL);
			}
		} else if (vfs_flagopt(mp->mnt_optnew, "ro", NULL, 0) == 0) {
			vfs_mount_error(mp,
			    "Checker enable: Must be read-only");
			return (EINVAL);
		}
		/* Set to -1 if we are done */
		if (fsckpid == 0)
			fsckpid = -1;
	}

	if (vfs_getopt(mp->mnt_optnew, "nfsv4acls", NULL, NULL) == 0) {
		if (mntorflags & MNT_ACLS) {
			vfs_mount_error(mp,
			    "\"acls\" and \"nfsv4acls\" options "
			    "are mutually exclusive");
			return (EINVAL);
		}
		mntorflags |= MNT_NFS4ACLS;
	}

	MNT_ILOCK(mp);
	mp->mnt_flag |= mntorflags;
	MNT_IUNLOCK(mp);
	/*
	 * If updating, check whether changing from read-only to
	 * read/write; if there is no device name, that's all we do.
	 */
	if (mp->mnt_flag & MNT_UPDATE) {
		ump = VFSTOUFS(mp);
		fs = ump->um_fs;
		devvp = ump->um_devvp;
		if (fsckpid == -1 && ump->um_fsckpid > 0) {
			if ((error = ffs_flushfiles(mp, WRITECLOSE, td)) != 0 ||
			    (error = ffs_sbupdate(ump, MNT_WAIT, 0)) != 0)
				return (error);
			g_topology_lock();
			/*
			 * Return to normal read-only mode.
			 */
			error = g_access(ump->um_cp, 0, -1, 0);
			g_topology_unlock();
			ump->um_fsckpid = 0;
		}
		if (fs->fs_ronly == 0 &&
		    vfs_flagopt(mp->mnt_optnew, "ro", NULL, 0)) {
			/*
			 * Flush any dirty data and suspend filesystem.
			 */
			if ((error = vn_start_write(NULL, &mp, V_WAIT)) != 0)
				return (error);
			error = vfs_write_suspend_umnt(mp);
			if (error != 0)
				return (error);
			/*
			 * Check for and optionally get rid of files open
			 * for writing.
			 */
			flags = WRITECLOSE;
			if (mp->mnt_flag & MNT_FORCE)
				flags |= FORCECLOSE;
			if (MOUNTEDSOFTDEP(mp)) {
				error = softdep_flushfiles(mp, flags, td);
			} else {
				error = ffs_flushfiles(mp, flags, td);
			}
			if (error) {
				vfs_write_resume(mp, 0);
				return (error);
			}
			if (fs->fs_pendingblocks != 0 ||
			    fs->fs_pendinginodes != 0) {
				printf("WARNING: %s Update error: blocks %jd "
				    "files %d\n", fs->fs_fsmnt, 
				    (intmax_t)fs->fs_pendingblocks,
				    fs->fs_pendinginodes);
				fs->fs_pendingblocks = 0;
				fs->fs_pendinginodes = 0;
			}
			if ((fs->fs_flags & (FS_UNCLEAN | FS_NEEDSFSCK)) == 0)
				fs->fs_clean = 1;
			if ((error = ffs_sbupdate(ump, MNT_WAIT, 0)) != 0) {
				fs->fs_ronly = 0;
				fs->fs_clean = 0;
				vfs_write_resume(mp, 0);
				return (error);
			}
			if (MOUNTEDSOFTDEP(mp))
				softdep_unmount(mp);
			g_topology_lock();
			/*
			 * Drop our write and exclusive access.
			 */
			g_access(ump->um_cp, 0, -1, -1);
			g_topology_unlock();
			fs->fs_ronly = 1;
			MNT_ILOCK(mp);
			mp->mnt_flag |= MNT_RDONLY;
			MNT_IUNLOCK(mp);
			/*
			 * Allow the writers to note that filesystem
			 * is ro now.
			 */
			vfs_write_resume(mp, 0);
		}
		if ((mp->mnt_flag & MNT_RELOAD) &&
		    (error = ffs_reload(mp, td, 0)) != 0)
			return (error);
		if (fs->fs_ronly &&
		    !vfs_flagopt(mp->mnt_optnew, "ro", NULL, 0)) {
			/*
			 * If we are running a checker, do not allow upgrade.
			 */
			if (ump->um_fsckpid > 0) {
				vfs_mount_error(mp,
				    "Active checker, cannot upgrade to write");
				return (EINVAL);
			}
			/*
			 * If upgrade to read-write by non-root, then verify
			 * that user has necessary permissions on the device.
			 */
			vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
			error = VOP_ACCESS(devvp, VREAD | VWRITE,
			    td->td_ucred, td);
			if (error)
				error = priv_check(td, PRIV_VFS_MOUNT_PERM);
			if (error) {
				VOP_UNLOCK(devvp, 0);
				return (error);
			}
			VOP_UNLOCK(devvp, 0);
			fs->fs_flags &= ~FS_UNCLEAN;
			if (fs->fs_clean == 0) {
				fs->fs_flags |= FS_UNCLEAN;
				if ((mp->mnt_flag & MNT_FORCE) ||
				    ((fs->fs_flags &
				     (FS_SUJ | FS_NEEDSFSCK)) == 0 &&
				     (fs->fs_flags & FS_DOSOFTDEP))) {
					printf("WARNING: %s was not properly "
					   "dismounted\n", fs->fs_fsmnt);
				} else {
					vfs_mount_error(mp,
					   "R/W mount of %s denied. %s.%s",
					   fs->fs_fsmnt,
					   "Filesystem is not clean - run fsck",
					   (fs->fs_flags & FS_SUJ) == 0 ? "" :
					   " Forced mount will invalidate"
					   " journal contents");
					return (EPERM);
				}
			}
			g_topology_lock();
			/*
			 * Request exclusive write access.
			 */
			error = g_access(ump->um_cp, 0, 1, 1);
			g_topology_unlock();
			if (error)
				return (error);
			if ((error = vn_start_write(NULL, &mp, V_WAIT)) != 0)
				return (error);
			fs->fs_ronly = 0;
			MNT_ILOCK(mp);
			mp->mnt_flag &= ~MNT_RDONLY;
			MNT_IUNLOCK(mp);
			fs->fs_mtime = time_second;
			/* check to see if we need to start softdep */
			if ((fs->fs_flags & FS_DOSOFTDEP) &&
			    (error = softdep_mount(devvp, mp, fs, td->td_ucred))){
				vn_finished_write(mp);
				return (error);
			}
			fs->fs_clean = 0;
			if ((error = ffs_sbupdate(ump, MNT_WAIT, 0)) != 0) {
				vn_finished_write(mp);
				return (error);
			}
			if (fs->fs_snapinum[0] != 0)
				ffs_snapshot_mount(mp);
			vn_finished_write(mp);
		}
		/*
		 * Soft updates is incompatible with "async",
		 * so if we are doing softupdates stop the user
		 * from setting the async flag in an update.
		 * Softdep_mount() clears it in an initial mount
		 * or ro->rw remount.
		 */
		if (MOUNTEDSOFTDEP(mp)) {
			/* XXX: Reset too late ? */
			MNT_ILOCK(mp);
			mp->mnt_flag &= ~MNT_ASYNC;
			MNT_IUNLOCK(mp);
		}
		/*
		 * Keep MNT_ACLS flag if it is stored in superblock.
		 */
		if ((fs->fs_flags & FS_ACLS) != 0) {
			/* XXX: Set too late ? */
			MNT_ILOCK(mp);
			mp->mnt_flag |= MNT_ACLS;
			MNT_IUNLOCK(mp);
		}

		if ((fs->fs_flags & FS_NFS4ACLS) != 0) {
			/* XXX: Set too late ? */
			MNT_ILOCK(mp);
			mp->mnt_flag |= MNT_NFS4ACLS;
			MNT_IUNLOCK(mp);
		}
		/*
		 * If this is a request from fsck to clean up the filesystem,
		 * then allow the specified pid to proceed.
		 */
		if (fsckpid > 0) {
			if (ump->um_fsckpid != 0) {
				vfs_mount_error(mp,
				    "Active checker already running on %s",
				    fs->fs_fsmnt);
				return (EINVAL);
			}
			KASSERT(MOUNTEDSOFTDEP(mp) == 0,
			    ("soft updates enabled on read-only file system"));
			g_topology_lock();
			/*
			 * Request write access.
			 */
			error = g_access(ump->um_cp, 0, 1, 0);
			g_topology_unlock();
			if (error) {
				vfs_mount_error(mp,
				    "Checker activation failed on %s",
				    fs->fs_fsmnt);
				return (error);
			}
			ump->um_fsckpid = fsckpid;
			if (fs->fs_snapinum[0] != 0)
				ffs_snapshot_mount(mp);
			fs->fs_mtime = time_second;
			fs->fs_fmod = 1;
			fs->fs_clean = 0;
			(void) ffs_sbupdate(ump, MNT_WAIT, 0);
		}

		/*
		 * If this is a snapshot request, take the snapshot.
		 */
		if (mp->mnt_flag & MNT_SNAPSHOT)
			return (ffs_snapshot(mp, fspec));

		/*
		 * Must not call namei() while owning busy ref.
		 */
		vfs_unbusy(mp);
	}

	/*
	 * Not an update, or updating the name: look up the name
	 * and verify that it refers to a sensible disk device.
	 */
	NDINIT(&ndp, LOOKUP, FOLLOW | LOCKLEAF, UIO_SYSSPACE, fspec, td);
	error = namei(&ndp);
	if ((mp->mnt_flag & MNT_UPDATE) != 0) {
		/*
		 * Unmount does not start if MNT_UPDATE is set.  Mount
		 * update busies mp before setting MNT_UPDATE.  We
		 * must be able to retain our busy ref succesfully,
		 * without sleep.
		 */
		error1 = vfs_busy(mp, MBF_NOWAIT);
		MPASS(error1 == 0);
	}
	if (error != 0)
		return (error);
	NDFREE(&ndp, NDF_ONLY_PNBUF);
	devvp = ndp.ni_vp;
	if (!vn_isdisk(devvp, &error)) {
		vput(devvp);
		return (error);
	}

	/*
	 * If mount by non-root, then verify that user has necessary
	 * permissions on the device.
	 */
	accmode = VREAD;
	if ((mp->mnt_flag & MNT_RDONLY) == 0)
		accmode |= VWRITE;
	error = VOP_ACCESS(devvp, accmode, td->td_ucred, td);
	if (error)
		error = priv_check(td, PRIV_VFS_MOUNT_PERM);
	if (error) {
		vput(devvp);
		return (error);
	}

	if (mp->mnt_flag & MNT_UPDATE) {
		/*
		 * Update only
		 *
		 * If it's not the same vnode, or at least the same device
		 * then it's not correct.
		 */

		if (devvp->v_rdev != ump->um_devvp->v_rdev)
			error = EINVAL;	/* needs translation */
		vput(devvp);
		if (error)
			return (error);
	} else {
		/*
		 * New mount
		 *
		 * We need the name for the mount point (also used for
		 * "last mounted on") copied in. If an error occurs,
		 * the mount point is discarded by the upper level code.
		 * Note that vfs_mount_alloc() populates f_mntonname for us.
		 */
		if ((error = ffs_mountfs(devvp, mp, td)) != 0) {
			vrele(devvp);
			return (error);
		}
		if (fsckpid > 0) {
			KASSERT(MOUNTEDSOFTDEP(mp) == 0,
			    ("soft updates enabled on read-only file system"));
			ump = VFSTOUFS(mp);
			fs = ump->um_fs;
			g_topology_lock();
			/*
			 * Request write access.
			 */
			error = g_access(ump->um_cp, 0, 1, 0);
			g_topology_unlock();
			if (error) {
				printf("WARNING: %s: Checker activation "
				    "failed\n", fs->fs_fsmnt);
			} else { 
				ump->um_fsckpid = fsckpid;
				if (fs->fs_snapinum[0] != 0)
					ffs_snapshot_mount(mp);
				fs->fs_mtime = time_second;
				fs->fs_clean = 0;
				(void) ffs_sbupdate(ump, MNT_WAIT, 0);
			}
		}
	}
	vfs_mountedfrom(mp, fspec);
	return (0);
}

/*
 * Compatibility with old mount system call.
 */

static int
ffs_cmount(struct mntarg *ma, void *data, uint64_t flags)
{
	struct ufs_args args;
	struct export_args exp;
	int error;

	if (data == NULL)
		return (EINVAL);
	error = copyin(data, &args, sizeof args);
	if (error)
		return (error);
	vfs_oexport_conv(&args.export, &exp);

	ma = mount_argsu(ma, "from", args.fspec, MAXPATHLEN);
	ma = mount_arg(ma, "export", &exp, sizeof(exp));
	error = kernel_mount(ma, flags);

	return (error);
}

/*
 * Reload all incore data for a filesystem (used after running fsck on
 * the root filesystem and finding things to fix). If the 'force' flag
 * is 0, the filesystem must be mounted read-only.
 *
 * Things to do to update the mount:
 *	1) invalidate all cached meta-data.
 *	2) re-read superblock from disk.
 *	3) re-read summary information from disk.
 *	4) invalidate all inactive vnodes.
 *	5) clear MNTK_SUSPEND2 and MNTK_SUSPENDED flags, allowing secondary
 *	   writers, if requested.
 *	6) invalidate all cached file data.
 *	7) re-read inode data for all active vnodes.
 */
int
ffs_reload(struct mount *mp, struct thread *td, int flags)
{
	struct vnode *vp, *mvp, *devvp;
	struct inode *ip;
	void *space;
	struct buf *bp;
	struct fs *fs, *newfs;
	struct ufsmount *ump;
	ufs2_daddr_t sblockloc;
	int i, blks, error;
	u_long size;
	int32_t *lp;

	ump = VFSTOUFS(mp);

	MNT_ILOCK(mp);
	if ((mp->mnt_flag & MNT_RDONLY) == 0 && (flags & FFSR_FORCE) == 0) {
		MNT_IUNLOCK(mp);
		return (EINVAL);
	}
	MNT_IUNLOCK(mp);
	
	/*
	 * Step 1: invalidate all cached meta-data.
	 */
	devvp = VFSTOUFS(mp)->um_devvp;
	vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
	if (vinvalbuf(devvp, 0, 0, 0) != 0)
		panic("ffs_reload: dirty1");
	VOP_UNLOCK(devvp, 0);

	/*
	 * Step 2: re-read superblock from disk.
	 */
	fs = VFSTOUFS(mp)->um_fs;
	if ((error = bread(devvp, btodb(fs->fs_sblockloc), fs->fs_sbsize,
	    NOCRED, &bp)) != 0)
		return (error);
	newfs = (struct fs *)bp->b_data;
	if ((newfs->fs_magic != FS_UFS1_MAGIC &&
	     newfs->fs_magic != FS_UFS2_MAGIC) ||
	    newfs->fs_bsize > MAXBSIZE ||
	    newfs->fs_bsize < sizeof(struct fs)) {
			brelse(bp);
			return (EIO);		/* XXX needs translation */
	}
	/*
	 * Copy pointer fields back into superblock before copying in	XXX
	 * new superblock. These should really be in the ufsmount.	XXX
	 * Note that important parameters (eg fs_ncg) are unchanged.
	 */
	newfs->fs_csp = fs->fs_csp;
	newfs->fs_maxcluster = fs->fs_maxcluster;
	newfs->fs_contigdirs = fs->fs_contigdirs;
	newfs->fs_active = fs->fs_active;
	newfs->fs_ronly = fs->fs_ronly;
	sblockloc = fs->fs_sblockloc;
	bcopy(newfs, fs, (u_int)fs->fs_sbsize);
	brelse(bp);
	mp->mnt_maxsymlinklen = fs->fs_maxsymlinklen;
	ffs_oldfscompat_read(fs, VFSTOUFS(mp), sblockloc);
	UFS_LOCK(ump);
	if (fs->fs_pendingblocks != 0 || fs->fs_pendinginodes != 0) {
		printf("WARNING: %s: reload pending error: blocks %jd "
		    "files %d\n", fs->fs_fsmnt, (intmax_t)fs->fs_pendingblocks,
		    fs->fs_pendinginodes);
		fs->fs_pendingblocks = 0;
		fs->fs_pendinginodes = 0;
	}
	UFS_UNLOCK(ump);

	/*
	 * Step 3: re-read summary information from disk.
	 */
	size = fs->fs_cssize;
	blks = howmany(size, fs->fs_fsize);
	if (fs->fs_contigsumsize > 0)
		size += fs->fs_ncg * sizeof(int32_t);
	size += fs->fs_ncg * sizeof(u_int8_t);
	free(fs->fs_csp, M_UFSMNT);
	space = malloc(size, M_UFSMNT, M_WAITOK);
	fs->fs_csp = space;
	for (i = 0; i < blks; i += fs->fs_frag) {
		size = fs->fs_bsize;
		if (i + fs->fs_frag > blks)
			size = (blks - i) * fs->fs_fsize;
		error = bread(devvp, fsbtodb(fs, fs->fs_csaddr + i), size,
		    NOCRED, &bp);
		if (error)
			return (error);
		bcopy(bp->b_data, space, (u_int)size);
		space = (char *)space + size;
		brelse(bp);
	}
	/*
	 * We no longer know anything about clusters per cylinder group.
	 */
	if (fs->fs_contigsumsize > 0) {
		fs->fs_maxcluster = lp = space;
		for (i = 0; i < fs->fs_ncg; i++)
			*lp++ = fs->fs_contigsumsize;
		space = lp;
	}
	size = fs->fs_ncg * sizeof(u_int8_t);
	fs->fs_contigdirs = (u_int8_t *)space;
	bzero(fs->fs_contigdirs, size);
	if ((flags & FFSR_UNSUSPEND) != 0) {
		MNT_ILOCK(mp);
		mp->mnt_kern_flag &= ~(MNTK_SUSPENDED | MNTK_SUSPEND2);
		wakeup(&mp->mnt_flag);
		MNT_IUNLOCK(mp);
	}

loop:
	MNT_VNODE_FOREACH_ALL(vp, mp, mvp) {
		/*
		 * Skip syncer vnode.
		 */
		if (vp->v_type == VNON) {
			VI_UNLOCK(vp);
			continue;
		}
		/*
		 * Step 4: invalidate all cached file data.
		 */
		if (vget(vp, LK_EXCLUSIVE | LK_INTERLOCK, td)) {
			MNT_VNODE_FOREACH_ALL_ABORT(mp, mvp);
			goto loop;
		}
		if (vinvalbuf(vp, 0, 0, 0))
			panic("ffs_reload: dirty2");
		/*
		 * Step 5: re-read inode data for all active vnodes.
		 */
		ip = VTOI(vp);
		error =
		    bread(devvp, fsbtodb(fs, ino_to_fsba(fs, ip->i_number)),
		    (int)fs->fs_bsize, NOCRED, &bp);
		if (error) {
			vput(vp);
			MNT_VNODE_FOREACH_ALL_ABORT(mp, mvp);
			return (error);
		}
		if ((error = ffs_load_inode(bp, ip, fs, ip->i_number)) != 0) {
			brelse(bp);
			vput(vp);
			MNT_VNODE_FOREACH_ALL_ABORT(mp, mvp);
			return (error);
		}
		ip->i_effnlink = ip->i_nlink;
		brelse(bp);
		vput(vp);
	}
	return (0);
}

/*
 * Common code for mount and mountroot
 */
static int
ffs_mountfs(devvp, mp, td)
	struct vnode *devvp;
	struct mount *mp;
	struct thread *td;
{
	struct ufsmount *ump;
	struct fs *fs;
	struct cdev *dev;
	int error, i, len, ronly;
	struct ucred *cred;
	struct g_consumer *cp;
	struct mount *nmp;
	int candelete;
	off_t loc;

	fs = NULL;
	ump = NULL;
	cred = td ? td->td_ucred : NOCRED;
	ronly = (mp->mnt_flag & MNT_RDONLY) != 0;

	KASSERT(devvp->v_type == VCHR, ("reclaimed devvp"));
	dev = devvp->v_rdev;
	if (atomic_cmpset_acq_ptr((uintptr_t *)&dev->si_mountpt, 0,
	    (uintptr_t)mp) == 0) {
		VOP_UNLOCK(devvp, 0);
		return (EBUSY);
	}
	g_topology_lock();
	error = g_vfs_open(devvp, &cp, "ffs", ronly ? 0 : 1);
	g_topology_unlock();
	if (error != 0) {
		atomic_store_rel_ptr((uintptr_t *)&dev->si_mountpt, 0);
		VOP_UNLOCK(devvp, 0);
		return (error);
	}
	dev_ref(dev);
	devvp->v_bufobj.bo_ops = &ffs_ops;
	VOP_UNLOCK(devvp, 0);
	if (dev->si_iosize_max != 0)
		mp->mnt_iosize_max = dev->si_iosize_max;
	if (mp->mnt_iosize_max > MAXPHYS)
		mp->mnt_iosize_max = MAXPHYS;
	if ((SBLOCKSIZE % cp->provider->sectorsize) != 0) {
		error = EINVAL;
		vfs_mount_error(mp,
		    "Invalid sectorsize %d for superblock size %d",
		    cp->provider->sectorsize, SBLOCKSIZE);
		goto out;
	}
	/* fetch the superblock and summary information */
	loc = STDSB;
	if ((mp->mnt_flag & MNT_ROOTFS) != 0)
		loc = STDSB_NOHASHFAIL;
	if ((error = ffs_sbget(devvp, &fs, loc, M_UFSMNT, ffs_use_bread)) != 0)
		goto out;
	/* none of these types of check-hashes are maintained by this kernel */
	fs->fs_metackhash &= ~(CK_INDIR | CK_DIR);
	/* no support for any undefined flags */
	fs->fs_flags &= FS_SUPPORTED;
	fs->fs_flags &= ~FS_UNCLEAN;
	if (fs->fs_clean == 0) {
		fs->fs_flags |= FS_UNCLEAN;
		if (ronly || (mp->mnt_flag & MNT_FORCE) ||
		    ((fs->fs_flags & (FS_SUJ | FS_NEEDSFSCK)) == 0 &&
		     (fs->fs_flags & FS_DOSOFTDEP))) {
			printf("WARNING: %s was not properly dismounted\n",
			    fs->fs_fsmnt);
		} else {
			vfs_mount_error(mp, "R/W mount of %s denied. %s%s",
			    fs->fs_fsmnt, "Filesystem is not clean - run fsck.",
			    (fs->fs_flags & FS_SUJ) == 0 ? "" :
			    " Forced mount will invalidate journal contents");
			error = EPERM;
			goto out;
		}
		if ((fs->fs_pendingblocks != 0 || fs->fs_pendinginodes != 0) &&
		    (mp->mnt_flag & MNT_FORCE)) {
			printf("WARNING: %s: lost blocks %jd files %d\n",
			    fs->fs_fsmnt, (intmax_t)fs->fs_pendingblocks,
			    fs->fs_pendinginodes);
			fs->fs_pendingblocks = 0;
			fs->fs_pendinginodes = 0;
		}
	}
	if (fs->fs_pendingblocks != 0 || fs->fs_pendinginodes != 0) {
		printf("WARNING: %s: mount pending error: blocks %jd "
		    "files %d\n", fs->fs_fsmnt, (intmax_t)fs->fs_pendingblocks,
		    fs->fs_pendinginodes);
		fs->fs_pendingblocks = 0;
		fs->fs_pendinginodes = 0;
	}
	if ((fs->fs_flags & FS_GJOURNAL) != 0) {
#ifdef UFS_GJOURNAL
		/*
		 * Get journal provider name.
		 */
		len = 1024;
		mp->mnt_gjprovider = malloc((u_long)len, M_UFSMNT, M_WAITOK);
		if (g_io_getattr("GJOURNAL::provider", cp, &len,
		    mp->mnt_gjprovider) == 0) {
			mp->mnt_gjprovider = realloc(mp->mnt_gjprovider, len,
			    M_UFSMNT, M_WAITOK);
			MNT_ILOCK(mp);
			mp->mnt_flag |= MNT_GJOURNAL;
			MNT_IUNLOCK(mp);
		} else {
			printf("WARNING: %s: GJOURNAL flag on fs "
			    "but no gjournal provider below\n",
			    mp->mnt_stat.f_mntonname);
			free(mp->mnt_gjprovider, M_UFSMNT);
			mp->mnt_gjprovider = NULL;
		}
#else
		printf("WARNING: %s: GJOURNAL flag on fs but no "
		    "UFS_GJOURNAL support\n", mp->mnt_stat.f_mntonname);
#endif
	} else {
		mp->mnt_gjprovider = NULL;
	}
	ump = malloc(sizeof *ump, M_UFSMNT, M_WAITOK | M_ZERO);
	ump->um_cp = cp;
	ump->um_bo = &devvp->v_bufobj;
	ump->um_fs = fs;
	if (fs->fs_magic == FS_UFS1_MAGIC) {
		ump->um_fstype = UFS1;
		ump->um_balloc = ffs_balloc_ufs1;
	} else {
		ump->um_fstype = UFS2;
		ump->um_balloc = ffs_balloc_ufs2;
	}
	ump->um_blkatoff = ffs_blkatoff;
	ump->um_truncate = ffs_truncate;
	ump->um_update = ffs_update;
	ump->um_valloc = ffs_valloc;
	ump->um_vfree = ffs_vfree;
	ump->um_ifree = ffs_ifree;
	ump->um_rdonly = ffs_rdonly;
	ump->um_snapgone = ffs_snapgone;
	mtx_init(UFS_MTX(ump), "FFS", "FFS Lock", MTX_DEF);
	ffs_oldfscompat_read(fs, ump, fs->fs_sblockloc);
	fs->fs_ronly = ronly;
	fs->fs_active = NULL;
	mp->mnt_data = ump;
	mp->mnt_stat.f_fsid.val[0] = fs->fs_id[0];
	mp->mnt_stat.f_fsid.val[1] = fs->fs_id[1];
	nmp = NULL;
	if (fs->fs_id[0] == 0 || fs->fs_id[1] == 0 ||
	    (nmp = vfs_getvfs(&mp->mnt_stat.f_fsid))) {
		if (nmp)
			vfs_rel(nmp);
		vfs_getnewfsid(mp);
	}
	mp->mnt_maxsymlinklen = fs->fs_maxsymlinklen;
	MNT_ILOCK(mp);
	mp->mnt_flag |= MNT_LOCAL;
	MNT_IUNLOCK(mp);
	if ((fs->fs_flags & FS_MULTILABEL) != 0) {
#ifdef MAC
		MNT_ILOCK(mp);
		mp->mnt_flag |= MNT_MULTILABEL;
		MNT_IUNLOCK(mp);
#else
		printf("WARNING: %s: multilabel flag on fs but "
		    "no MAC support\n", mp->mnt_stat.f_mntonname);
#endif
	}
	if ((fs->fs_flags & FS_ACLS) != 0) {
#ifdef UFS_ACL
		MNT_ILOCK(mp);

		if (mp->mnt_flag & MNT_NFS4ACLS)
			printf("WARNING: %s: ACLs flag on fs conflicts with "
			    "\"nfsv4acls\" mount option; option ignored\n",
			    mp->mnt_stat.f_mntonname);
		mp->mnt_flag &= ~MNT_NFS4ACLS;
		mp->mnt_flag |= MNT_ACLS;

		MNT_IUNLOCK(mp);
#else
		printf("WARNING: %s: ACLs flag on fs but no ACLs support\n",
		    mp->mnt_stat.f_mntonname);
#endif
	}
	if ((fs->fs_flags & FS_NFS4ACLS) != 0) {
#ifdef UFS_ACL
		MNT_ILOCK(mp);

		if (mp->mnt_flag & MNT_ACLS)
			printf("WARNING: %s: NFSv4 ACLs flag on fs conflicts "
			    "with \"acls\" mount option; option ignored\n",
			    mp->mnt_stat.f_mntonname);
		mp->mnt_flag &= ~MNT_ACLS;
		mp->mnt_flag |= MNT_NFS4ACLS;

		MNT_IUNLOCK(mp);
#else
		printf("WARNING: %s: NFSv4 ACLs flag on fs but no "
		    "ACLs support\n", mp->mnt_stat.f_mntonname);
#endif
	}
	if ((fs->fs_flags & FS_TRIM) != 0) {
		len = sizeof(int);
		if (g_io_getattr("GEOM::candelete", cp, &len,
		    &candelete) == 0) {
			if (candelete)
				ump->um_flags |= UM_CANDELETE;
			else
				printf("WARNING: %s: TRIM flag on fs but disk "
				    "does not support TRIM\n",
				    mp->mnt_stat.f_mntonname);
		} else {
			printf("WARNING: %s: TRIM flag on fs but disk does "
			    "not confirm that it supports TRIM\n",
			    mp->mnt_stat.f_mntonname);
		}
		if (((ump->um_flags) & UM_CANDELETE) != 0) {
			ump->um_trim_tq = taskqueue_create("trim", M_WAITOK,
			    taskqueue_thread_enqueue, &ump->um_trim_tq);
			taskqueue_start_threads(&ump->um_trim_tq, 1, PVFS,
			    "%s trim", mp->mnt_stat.f_mntonname);
			ump->um_trimhash = hashinit(MAXTRIMIO, M_TRIM,
			    &ump->um_trimlisthashsize);
		}
	}

	ump->um_mountp = mp;
	ump->um_dev = dev;
	ump->um_devvp = devvp;
	ump->um_nindir = fs->fs_nindir;
	ump->um_bptrtodb = fs->fs_fsbtodb;
	ump->um_seqinc = fs->fs_frag;
	for (i = 0; i < MAXQUOTAS; i++)
		ump->um_quotas[i] = NULLVP;
#ifdef UFS_EXTATTR
	ufs_extattr_uepm_init(&ump->um_extattr);
#endif
	/*
	 * Set FS local "last mounted on" information (NULL pad)
	 */
	bzero(fs->fs_fsmnt, MAXMNTLEN);
	strlcpy(fs->fs_fsmnt, mp->mnt_stat.f_mntonname, MAXMNTLEN);
	mp->mnt_stat.f_iosize = fs->fs_bsize;

	if (mp->mnt_flag & MNT_ROOTFS) {
		/*
		 * Root mount; update timestamp in mount structure.
		 * this will be used by the common root mount code
		 * to update the system clock.
		 */
		mp->mnt_time = fs->fs_time;
	}

	if (ronly == 0) {
		fs->fs_mtime = time_second;
		if ((fs->fs_flags & FS_DOSOFTDEP) &&
		    (error = softdep_mount(devvp, mp, fs, cred)) != 0) {
			ffs_flushfiles(mp, FORCECLOSE, td);
			goto out;
		}
		if (fs->fs_snapinum[0] != 0)
			ffs_snapshot_mount(mp);
		fs->fs_fmod = 1;
		fs->fs_clean = 0;
		(void) ffs_sbupdate(ump, MNT_WAIT, 0);
	}
	/*
	 * Initialize filesystem state information in mount struct.
	 */
	MNT_ILOCK(mp);
	mp->mnt_kern_flag |= MNTK_LOOKUP_SHARED | MNTK_EXTENDED_SHARED |
	    MNTK_NO_IOPF | MNTK_UNMAPPED_BUFS | MNTK_USES_BCACHE;
	MNT_IUNLOCK(mp);
#ifdef UFS_EXTATTR
#ifdef UFS_EXTATTR_AUTOSTART
	/*
	 *
	 * Auto-starting does the following:
	 *	- check for /.attribute in the fs, and extattr_start if so
	 *	- for each file in .attribute, enable that file with
	 * 	  an attribute of the same name.
	 * Not clear how to report errors -- probably eat them.
	 * This would all happen while the filesystem was busy/not
	 * available, so would effectively be "atomic".
	 */
	(void) ufs_extattr_autostart(mp, td);
#endif /* !UFS_EXTATTR_AUTOSTART */
#endif /* !UFS_EXTATTR */
	return (0);
out:
	if (fs != NULL) {
		free(fs->fs_csp, M_UFSMNT);
		free(fs, M_UFSMNT);
	}
	if (cp != NULL) {
		g_topology_lock();
		g_vfs_close(cp);
		g_topology_unlock();
	}
	if (ump) {
		mtx_destroy(UFS_MTX(ump));
		if (mp->mnt_gjprovider != NULL) {
			free(mp->mnt_gjprovider, M_UFSMNT);
			mp->mnt_gjprovider = NULL;
		}
		free(ump, M_UFSMNT);
		mp->mnt_data = NULL;
	}
	atomic_store_rel_ptr((uintptr_t *)&dev->si_mountpt, 0);
	dev_rel(dev);
	return (error);
}

/*
 * A read function for use by filesystem-layer routines.
 */
static int
ffs_use_bread(void *devfd, off_t loc, void **bufp, int size)
{
	struct buf *bp;
	int error;

	KASSERT(*bufp == NULL, ("ffs_use_bread: non-NULL *bufp %p\n", *bufp));
	*bufp = malloc(size, M_UFSMNT, M_WAITOK);
	if ((error = bread((struct vnode *)devfd, btodb(loc), size, NOCRED,
	    &bp)) != 0)
		return (error);
	bcopy(bp->b_data, *bufp, size);
	bp->b_flags |= B_INVAL | B_NOCACHE;
	brelse(bp);
	return (0);
}

#include <sys/sysctl.h>
static int bigcgs = 0;
SYSCTL_INT(_debug, OID_AUTO, bigcgs, CTLFLAG_RW, &bigcgs, 0, "");

/*
 * Sanity checks for loading old filesystem superblocks.
 * See ffs_oldfscompat_write below for unwound actions.
 *
 * XXX - Parts get retired eventually.
 * Unfortunately new bits get added.
 */
static void
ffs_oldfscompat_read(fs, ump, sblockloc)
	struct fs *fs;
	struct ufsmount *ump;
	ufs2_daddr_t sblockloc;
{
	off_t maxfilesize;

	/*
	 * If not yet done, update fs_flags location and value of fs_sblockloc.
	 */
	if ((fs->fs_old_flags & FS_FLAGS_UPDATED) == 0) {
		fs->fs_flags = fs->fs_old_flags;
		fs->fs_old_flags |= FS_FLAGS_UPDATED;
		fs->fs_sblockloc = sblockloc;
	}
	/*
	 * If not yet done, update UFS1 superblock with new wider fields.
	 */
	if (fs->fs_magic == FS_UFS1_MAGIC && fs->fs_maxbsize != fs->fs_bsize) {
		fs->fs_maxbsize = fs->fs_bsize;
		fs->fs_time = fs->fs_old_time;
		fs->fs_size = fs->fs_old_size;
		fs->fs_dsize = fs->fs_old_dsize;
		fs->fs_csaddr = fs->fs_old_csaddr;
		fs->fs_cstotal.cs_ndir = fs->fs_old_cstotal.cs_ndir;
		fs->fs_cstotal.cs_nbfree = fs->fs_old_cstotal.cs_nbfree;
		fs->fs_cstotal.cs_nifree = fs->fs_old_cstotal.cs_nifree;
		fs->fs_cstotal.cs_nffree = fs->fs_old_cstotal.cs_nffree;
	}
	if (fs->fs_magic == FS_UFS1_MAGIC &&
	    fs->fs_old_inodefmt < FS_44INODEFMT) {
		fs->fs_maxfilesize = ((uint64_t)1 << 31) - 1;
		fs->fs_qbmask = ~fs->fs_bmask;
		fs->fs_qfmask = ~fs->fs_fmask;
	}
	if (fs->fs_magic == FS_UFS1_MAGIC) {
		ump->um_savedmaxfilesize = fs->fs_maxfilesize;
		maxfilesize = (uint64_t)0x80000000 * fs->fs_bsize - 1;
		if (fs->fs_maxfilesize > maxfilesize)
			fs->fs_maxfilesize = maxfilesize;
	}
	/* Compatibility for old filesystems */
	if (fs->fs_avgfilesize <= 0)
		fs->fs_avgfilesize = AVFILESIZ;
	if (fs->fs_avgfpdir <= 0)
		fs->fs_avgfpdir = AFPDIR;
	if (bigcgs) {
		fs->fs_save_cgsize = fs->fs_cgsize;
		fs->fs_cgsize = fs->fs_bsize;
	}
}

/*
 * Unwinding superblock updates for old filesystems.
 * See ffs_oldfscompat_read above for details.
 *
 * XXX - Parts get retired eventually.
 * Unfortunately new bits get added.
 */
void
ffs_oldfscompat_write(fs, ump)
	struct fs *fs;
	struct ufsmount *ump;
{

	/*
	 * Copy back UFS2 updated fields that UFS1 inspects.
	 */
	if (fs->fs_magic == FS_UFS1_MAGIC) {
		fs->fs_old_time = fs->fs_time;
		fs->fs_old_cstotal.cs_ndir = fs->fs_cstotal.cs_ndir;
		fs->fs_old_cstotal.cs_nbfree = fs->fs_cstotal.cs_nbfree;
		fs->fs_old_cstotal.cs_nifree = fs->fs_cstotal.cs_nifree;
		fs->fs_old_cstotal.cs_nffree = fs->fs_cstotal.cs_nffree;
		fs->fs_maxfilesize = ump->um_savedmaxfilesize;
	}
	if (bigcgs) {
		fs->fs_cgsize = fs->fs_save_cgsize;
		fs->fs_save_cgsize = 0;
	}
}

/*
 * unmount system call
 */
static int
ffs_unmount(mp, mntflags)
	struct mount *mp;
	int mntflags;
{
	struct thread *td;
	struct ufsmount *ump = VFSTOUFS(mp);
	struct fs *fs;
	int error, flags, susp;
#ifdef UFS_EXTATTR
	int e_restart;
#endif

	flags = 0;
	td = curthread;
	fs = ump->um_fs;
	susp = 0;
	if (mntflags & MNT_FORCE) {
		flags |= FORCECLOSE;
		susp = fs->fs_ronly == 0;
	}
#ifdef UFS_EXTATTR
	if ((error = ufs_extattr_stop(mp, td))) {
		if (error != EOPNOTSUPP)
			printf("WARNING: unmount %s: ufs_extattr_stop "
			    "returned errno %d\n", mp->mnt_stat.f_mntonname,
			    error);
		e_restart = 0;
	} else {
		ufs_extattr_uepm_destroy(&ump->um_extattr);
		e_restart = 1;
	}
#endif
	if (susp) {
		error = vfs_write_suspend_umnt(mp);
		if (error != 0)
			goto fail1;
	}
	if (MOUNTEDSOFTDEP(mp))
		error = softdep_flushfiles(mp, flags, td);
	else
		error = ffs_flushfiles(mp, flags, td);
	if (error != 0 && error != ENXIO)
		goto fail;

	UFS_LOCK(ump);
	if (fs->fs_pendingblocks != 0 || fs->fs_pendinginodes != 0) {
		printf("WARNING: unmount %s: pending error: blocks %jd "
		    "files %d\n", fs->fs_fsmnt, (intmax_t)fs->fs_pendingblocks,
		    fs->fs_pendinginodes);
		fs->fs_pendingblocks = 0;
		fs->fs_pendinginodes = 0;
	}
	UFS_UNLOCK(ump);
	if (MOUNTEDSOFTDEP(mp))
		softdep_unmount(mp);
	if (fs->fs_ronly == 0 || ump->um_fsckpid > 0) {
		fs->fs_clean = fs->fs_flags & (FS_UNCLEAN|FS_NEEDSFSCK) ? 0 : 1;
		error = ffs_sbupdate(ump, MNT_WAIT, 0);
		if (error && error != ENXIO) {
			fs->fs_clean = 0;
			goto fail;
		}
	}
	if (susp)
		vfs_write_resume(mp, VR_START_WRITE);
	if (ump->um_trim_tq != NULL) {
		while (ump->um_trim_inflight != 0)
			pause("ufsutr", hz);
		taskqueue_drain_all(ump->um_trim_tq);
		taskqueue_free(ump->um_trim_tq);
		free (ump->um_trimhash, M_TRIM);
	}
	g_topology_lock();
	if (ump->um_fsckpid > 0) {
		/*
		 * Return to normal read-only mode.
		 */
		error = g_access(ump->um_cp, 0, -1, 0);
		ump->um_fsckpid = 0;
	}
	g_vfs_close(ump->um_cp);
	g_topology_unlock();
	atomic_store_rel_ptr((uintptr_t *)&ump->um_dev->si_mountpt, 0);
	vrele(ump->um_devvp);
	dev_rel(ump->um_dev);
	mtx_destroy(UFS_MTX(ump));
	if (mp->mnt_gjprovider != NULL) {
		free(mp->mnt_gjprovider, M_UFSMNT);
		mp->mnt_gjprovider = NULL;
	}
	free(fs->fs_csp, M_UFSMNT);
	free(fs, M_UFSMNT);
	free(ump, M_UFSMNT);
	mp->mnt_data = NULL;
	MNT_ILOCK(mp);
	mp->mnt_flag &= ~MNT_LOCAL;
	MNT_IUNLOCK(mp);
	if (td->td_su == mp) {
		td->td_su = NULL;
		vfs_rel(mp);
	}
	return (error);

fail:
	if (susp)
		vfs_write_resume(mp, VR_START_WRITE);
fail1:
#ifdef UFS_EXTATTR
	if (e_restart) {
		ufs_extattr_uepm_init(&ump->um_extattr);
#ifdef UFS_EXTATTR_AUTOSTART
		(void) ufs_extattr_autostart(mp, td);
#endif
	}
#endif

	return (error);
}

/*
 * Flush out all the files in a filesystem.
 */
int
ffs_flushfiles(mp, flags, td)
	struct mount *mp;
	int flags;
	struct thread *td;
{
	struct ufsmount *ump;
	int qerror, error;

	ump = VFSTOUFS(mp);
	qerror = 0;
#ifdef QUOTA
	if (mp->mnt_flag & MNT_QUOTA) {
		int i;
		error = vflush(mp, 0, SKIPSYSTEM|flags, td);
		if (error)
			return (error);
		for (i = 0; i < MAXQUOTAS; i++) {
			error = quotaoff(td, mp, i);
			if (error != 0) {
				if ((flags & EARLYFLUSH) == 0)
					return (error);
				else
					qerror = error;
			}
		}

		/*
		 * Here we fall through to vflush again to ensure that
		 * we have gotten rid of all the system vnodes, unless
		 * quotas must not be closed.
		 */
	}
#endif
	ASSERT_VOP_LOCKED(ump->um_devvp, "ffs_flushfiles");
	if (ump->um_devvp->v_vflag & VV_COPYONWRITE) {
		if ((error = vflush(mp, 0, SKIPSYSTEM | flags, td)) != 0)
			return (error);
		ffs_snapshot_unmount(mp);
		flags |= FORCECLOSE;
		/*
		 * Here we fall through to vflush again to ensure
		 * that we have gotten rid of all the system vnodes.
		 */
	}

	/*
	 * Do not close system files if quotas were not closed, to be
	 * able to sync the remaining dquots.  The freeblks softupdate
	 * workitems might hold a reference on a dquot, preventing
	 * quotaoff() from completing.  Next round of
	 * softdep_flushworklist() iteration should process the
	 * blockers, allowing the next run of quotaoff() to finally
	 * flush held dquots.
	 *
	 * Otherwise, flush all the files.
	 */
	if (qerror == 0 && (error = vflush(mp, 0, flags, td)) != 0)
		return (error);

	/*
	 * Flush filesystem metadata.
	 */
	vn_lock(ump->um_devvp, LK_EXCLUSIVE | LK_RETRY);
	error = VOP_FSYNC(ump->um_devvp, MNT_WAIT, td);
	VOP_UNLOCK(ump->um_devvp, 0);
	return (error);
}

/*
 * Get filesystem statistics.
 */
static int
ffs_statfs(mp, sbp)
	struct mount *mp;
	struct statfs *sbp;
{
	struct ufsmount *ump;
	struct fs *fs;

	ump = VFSTOUFS(mp);
	fs = ump->um_fs;
	if (fs->fs_magic != FS_UFS1_MAGIC && fs->fs_magic != FS_UFS2_MAGIC)
		panic("ffs_statfs");
	sbp->f_version = STATFS_VERSION;
	sbp->f_bsize = fs->fs_fsize;
	sbp->f_iosize = fs->fs_bsize;
	sbp->f_blocks = fs->fs_dsize;
	UFS_LOCK(ump);
	sbp->f_bfree = fs->fs_cstotal.cs_nbfree * fs->fs_frag +
	    fs->fs_cstotal.cs_nffree + dbtofsb(fs, fs->fs_pendingblocks);
	sbp->f_bavail = freespace(fs, fs->fs_minfree) +
	    dbtofsb(fs, fs->fs_pendingblocks);
	sbp->f_files =  fs->fs_ncg * fs->fs_ipg - UFS_ROOTINO;
	sbp->f_ffree = fs->fs_cstotal.cs_nifree + fs->fs_pendinginodes;
	UFS_UNLOCK(ump);
	sbp->f_namemax = UFS_MAXNAMLEN;
	return (0);
}

static bool
sync_doupdate(struct inode *ip)
{

	return ((ip->i_flag & (IN_ACCESS | IN_CHANGE | IN_MODIFIED |
	    IN_UPDATE)) != 0);
}

/*
 * For a lazy sync, we only care about access times, quotas and the
 * superblock.  Other filesystem changes are already converted to
 * cylinder group blocks or inode blocks updates and are written to
 * disk by syncer.
 */
static int
ffs_sync_lazy(mp)
     struct mount *mp;
{
	struct vnode *mvp, *vp;
	struct inode *ip;
	struct thread *td;
	int allerror, error;

	allerror = 0;
	td = curthread;
	if ((mp->mnt_flag & MNT_NOATIME) != 0)
		goto qupdate;
	MNT_VNODE_FOREACH_ACTIVE(vp, mp, mvp) {
		if (vp->v_type == VNON) {
			VI_UNLOCK(vp);
			continue;
		}
		ip = VTOI(vp);

		/*
		 * The IN_ACCESS flag is converted to IN_MODIFIED by
		 * ufs_close() and ufs_getattr() by the calls to
		 * ufs_itimes_locked(), without subsequent UFS_UPDATE().
		 * Test also all the other timestamp flags too, to pick up
		 * any other cases that could be missed.
		 */
		if (!sync_doupdate(ip) && (vp->v_iflag & VI_OWEINACT) == 0) {
			VI_UNLOCK(vp);
			continue;
		}
		if ((error = vget(vp, LK_EXCLUSIVE | LK_NOWAIT | LK_INTERLOCK,
		    td)) != 0)
			continue;
		if (sync_doupdate(ip))
			error = ffs_update(vp, 0);
		if (error != 0)
			allerror = error;
		vput(vp);
	}

qupdate:
#ifdef QUOTA
	qsync(mp);
#endif

	if (VFSTOUFS(mp)->um_fs->fs_fmod != 0 &&
	    (error = ffs_sbupdate(VFSTOUFS(mp), MNT_LAZY, 0)) != 0)
		allerror = error;
	return (allerror);
}

/*
 * Go through the disk queues to initiate sandbagged IO;
 * go through the inodes to write those that have been modified;
 * initiate the writing of the super block if it has been modified.
 *
 * Note: we are always called with the filesystem marked busy using
 * vfs_busy().
 */
static int
ffs_sync(mp, waitfor)
	struct mount *mp;
	int waitfor;
{
	struct vnode *mvp, *vp, *devvp;
	struct thread *td;
	struct inode *ip;
	struct ufsmount *ump = VFSTOUFS(mp);
	struct fs *fs;
	int error, count, lockreq, allerror = 0;
	int suspend;
	int suspended;
	int secondary_writes;
	int secondary_accwrites;
	int softdep_deps;
	int softdep_accdeps;
	struct bufobj *bo;

	suspend = 0;
	suspended = 0;
	td = curthread;
	fs = ump->um_fs;
	if (fs->fs_fmod != 0 && fs->fs_ronly != 0 && ump->um_fsckpid == 0)
		panic("%s: ffs_sync: modification on read-only filesystem",
		    fs->fs_fsmnt);
	if (waitfor == MNT_LAZY) {
		if (!rebooting)
			return (ffs_sync_lazy(mp));
		waitfor = MNT_NOWAIT;
	}

	/*
	 * Write back each (modified) inode.
	 */
	lockreq = LK_EXCLUSIVE | LK_NOWAIT;
	if (waitfor == MNT_SUSPEND) {
		suspend = 1;
		waitfor = MNT_WAIT;
	}
	if (waitfor == MNT_WAIT)
		lockreq = LK_EXCLUSIVE;
	lockreq |= LK_INTERLOCK | LK_SLEEPFAIL;
loop:
	/* Grab snapshot of secondary write counts */
	MNT_ILOCK(mp);
	secondary_writes = mp->mnt_secondary_writes;
	secondary_accwrites = mp->mnt_secondary_accwrites;
	MNT_IUNLOCK(mp);

	/* Grab snapshot of softdep dependency counts */
	softdep_get_depcounts(mp, &softdep_deps, &softdep_accdeps);

	MNT_VNODE_FOREACH_ALL(vp, mp, mvp) {
		/*
		 * Depend on the vnode interlock to keep things stable enough
		 * for a quick test.  Since there might be hundreds of
		 * thousands of vnodes, we cannot afford even a subroutine
		 * call unless there's a good chance that we have work to do.
		 */
		if (vp->v_type == VNON) {
			VI_UNLOCK(vp);
			continue;
		}
		ip = VTOI(vp);
		if ((ip->i_flag &
		    (IN_ACCESS | IN_CHANGE | IN_MODIFIED | IN_UPDATE)) == 0 &&
		    vp->v_bufobj.bo_dirty.bv_cnt == 0) {
			VI_UNLOCK(vp);
			continue;
		}
		if ((error = vget(vp, lockreq, td)) != 0) {
			if (error == ENOENT || error == ENOLCK) {
				MNT_VNODE_FOREACH_ALL_ABORT(mp, mvp);
				goto loop;
			}
			continue;
		}
		if ((error = ffs_syncvnode(vp, waitfor, 0)) != 0)
			allerror = error;
		vput(vp);
	}
	/*
	 * Force stale filesystem control information to be flushed.
	 */
	if (waitfor == MNT_WAIT || rebooting) {
		if ((error = softdep_flushworklist(ump->um_mountp, &count, td)))
			allerror = error;
		/* Flushed work items may create new vnodes to clean */
		if (allerror == 0 && count)
			goto loop;
	}
#ifdef QUOTA
	qsync(mp);
#endif

	devvp = ump->um_devvp;
	bo = &devvp->v_bufobj;
	BO_LOCK(bo);
	if (bo->bo_numoutput > 0 || bo->bo_dirty.bv_cnt > 0) {
		BO_UNLOCK(bo);
		vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
		error = VOP_FSYNC(devvp, waitfor, td);
		VOP_UNLOCK(devvp, 0);
		if (MOUNTEDSOFTDEP(mp) && (error == 0 || error == EAGAIN))
			error = ffs_sbupdate(ump, waitfor, 0);
		if (error != 0)
			allerror = error;
		if (allerror == 0 && waitfor == MNT_WAIT)
			goto loop;
	} else if (suspend != 0) {
		if (softdep_check_suspend(mp,
					  devvp,
					  softdep_deps,
					  softdep_accdeps,
					  secondary_writes,
					  secondary_accwrites) != 0) {
			MNT_IUNLOCK(mp);
			goto loop;	/* More work needed */
		}
		mtx_assert(MNT_MTX(mp), MA_OWNED);
		mp->mnt_kern_flag |= MNTK_SUSPEND2 | MNTK_SUSPENDED;
		MNT_IUNLOCK(mp);
		suspended = 1;
	} else
		BO_UNLOCK(bo);
	/*
	 * Write back modified superblock.
	 */
	if (fs->fs_fmod != 0 &&
	    (error = ffs_sbupdate(ump, waitfor, suspended)) != 0)
		allerror = error;
	return (allerror);
}

int
ffs_vget(mp, ino, flags, vpp)
	struct mount *mp;
	ino_t ino;
	int flags;
	struct vnode **vpp;
{
	return (ffs_vgetf(mp, ino, flags, vpp, 0));
}

int
ffs_vgetf(mp, ino, flags, vpp, ffs_flags)
	struct mount *mp;
	ino_t ino;
	int flags;
	struct vnode **vpp;
	int ffs_flags;
{
	struct fs *fs;
	struct inode *ip;
	struct ufsmount *ump;
	struct buf *bp;
	struct vnode *vp;
	int error;

	error = vfs_hash_get(mp, ino, flags, curthread, vpp, NULL, NULL);
	if (error || *vpp != NULL)
		return (error);

	/*
	 * We must promote to an exclusive lock for vnode creation.  This
	 * can happen if lookup is passed LOCKSHARED.
	 */
	if ((flags & LK_TYPE_MASK) == LK_SHARED) {
		flags &= ~LK_TYPE_MASK;
		flags |= LK_EXCLUSIVE;
	}

	/*
	 * We do not lock vnode creation as it is believed to be too
	 * expensive for such rare case as simultaneous creation of vnode
	 * for same ino by different processes. We just allow them to race
	 * and check later to decide who wins. Let the race begin!
	 */

	ump = VFSTOUFS(mp);
	fs = ump->um_fs;
	ip = uma_zalloc(uma_inode, M_WAITOK | M_ZERO);

	/* Allocate a new vnode/inode. */
	error = getnewvnode("ufs", mp, fs->fs_magic == FS_UFS1_MAGIC ?
	    &ffs_vnodeops1 : &ffs_vnodeops2, &vp);
	if (error) {
		*vpp = NULL;
		uma_zfree(uma_inode, ip);
		return (error);
	}
	/*
	 * FFS supports recursive locking.
	 */
	lockmgr(vp->v_vnlock, LK_EXCLUSIVE, NULL);
	VN_LOCK_AREC(vp);
	vp->v_data = ip;
	vp->v_bufobj.bo_bsize = fs->fs_bsize;
	ip->i_vnode = vp;
	ip->i_ump = ump;
	ip->i_number = ino;
	ip->i_ea_refs = 0;
	ip->i_nextclustercg = -1;
	ip->i_flag = fs->fs_magic == FS_UFS1_MAGIC ? 0 : IN_UFS2;
	ip->i_mode = 0; /* ensure error cases below throw away vnode */
#ifdef QUOTA
	{
		int i;
		for (i = 0; i < MAXQUOTAS; i++)
			ip->i_dquot[i] = NODQUOT;
	}
#endif

	if (ffs_flags & FFSV_FORCEINSMQ)
		vp->v_vflag |= VV_FORCEINSMQ;
	error = insmntque(vp, mp);
	if (error != 0) {
		uma_zfree(uma_inode, ip);
		*vpp = NULL;
		return (error);
	}
	vp->v_vflag &= ~VV_FORCEINSMQ;
	error = vfs_hash_insert(vp, ino, flags, curthread, vpp, NULL, NULL);
	if (error || *vpp != NULL)
		return (error);

	/* Read in the disk contents for the inode, copy into the inode. */
	error = bread(ump->um_devvp, fsbtodb(fs, ino_to_fsba(fs, ino)),
	    (int)fs->fs_bsize, NOCRED, &bp);
	if (error) {
		/*
		 * The inode does not contain anything useful, so it would
		 * be misleading to leave it on its hash chain. With mode
		 * still zero, it will be unlinked and returned to the free
		 * list by vput().
		 */
		brelse(bp);
		vput(vp);
		*vpp = NULL;
		return (error);
	}
	if (I_IS_UFS1(ip))
		ip->i_din1 = uma_zalloc(uma_ufs1, M_WAITOK);
	else
		ip->i_din2 = uma_zalloc(uma_ufs2, M_WAITOK);
	if ((error = ffs_load_inode(bp, ip, fs, ino)) != 0) {
		bqrelse(bp);
		vput(vp);
		*vpp = NULL;
		return (error);
	}
	if (DOINGSOFTDEP(vp))
		softdep_load_inodeblock(ip);
	else
		ip->i_effnlink = ip->i_nlink;
	bqrelse(bp);

	/*
	 * Initialize the vnode from the inode, check for aliases.
	 * Note that the underlying vnode may have changed.
	 */
	error = ufs_vinit(mp, I_IS_UFS1(ip) ? &ffs_fifoops1 : &ffs_fifoops2,
	    &vp);
	if (error) {
		vput(vp);
		*vpp = NULL;
		return (error);
	}

	/*
	 * Finish inode initialization.
	 */
	if (vp->v_type != VFIFO) {
		/* FFS supports shared locking for all files except fifos. */
		VN_LOCK_ASHARE(vp);
	}

	/*
	 * Set up a generation number for this inode if it does not
	 * already have one. This should only happen on old filesystems.
	 */
	if (ip->i_gen == 0) {
		while (ip->i_gen == 0)
			ip->i_gen = arc4random();
		if ((vp->v_mount->mnt_flag & MNT_RDONLY) == 0) {
			ip->i_flag |= IN_MODIFIED;
			DIP_SET(ip, i_gen, ip->i_gen);
		}
	}
#ifdef MAC
	if ((mp->mnt_flag & MNT_MULTILABEL) && ip->i_mode) {
		/*
		 * If this vnode is already allocated, and we're running
		 * multi-label, attempt to perform a label association
		 * from the extended attributes on the inode.
		 */
		error = mac_vnode_associate_extattr(mp, vp);
		if (error) {
			/* ufs_inactive will release ip->i_devvp ref. */
			vput(vp);
			*vpp = NULL;
			return (error);
		}
	}
#endif

	*vpp = vp;
	return (0);
}

/*
 * File handle to vnode
 *
 * Have to be really careful about stale file handles:
 * - check that the inode number is valid
 * - for UFS2 check that the inode number is initialized
 * - call ffs_vget() to get the locked inode
 * - check for an unallocated inode (i_mode == 0)
 * - check that the given client host has export rights and return
 *   those rights via. exflagsp and credanonp
 */
static int
ffs_fhtovp(mp, fhp, flags, vpp)
	struct mount *mp;
	struct fid *fhp;
	int flags;
	struct vnode **vpp;
{
	struct ufid *ufhp;
	struct ufsmount *ump;
	struct fs *fs;
	struct cg *cgp;
	struct buf *bp;
	ino_t ino;
	u_int cg;
	int error;

	ufhp = (struct ufid *)fhp;
	ino = ufhp->ufid_ino;
	ump = VFSTOUFS(mp);
	fs = ump->um_fs;
	if (ino < UFS_ROOTINO || ino >= fs->fs_ncg * fs->fs_ipg)
		return (ESTALE);
	/*
	 * Need to check if inode is initialized because UFS2 does lazy
	 * initialization and nfs_fhtovp can offer arbitrary inode numbers.
	 */
	if (fs->fs_magic != FS_UFS2_MAGIC)
		return (ufs_fhtovp(mp, ufhp, flags, vpp));
	cg = ino_to_cg(fs, ino);
	if ((error = ffs_getcg(fs, ump->um_devvp, cg, &bp, &cgp)) != 0)
		return (error);
	if (ino >= cg * fs->fs_ipg + cgp->cg_initediblk) {
		brelse(bp);
		return (ESTALE);
	}
	brelse(bp);
	return (ufs_fhtovp(mp, ufhp, flags, vpp));
}

/*
 * Initialize the filesystem.
 */
static int
ffs_init(vfsp)
	struct vfsconf *vfsp;
{

	ffs_susp_initialize();
	softdep_initialize();
	return (ufs_init(vfsp));
}

/*
 * Undo the work of ffs_init().
 */
static int
ffs_uninit(vfsp)
	struct vfsconf *vfsp;
{
	int ret;

	ret = ufs_uninit(vfsp);
	softdep_uninitialize();
	ffs_susp_uninitialize();
	return (ret);
}

/*
 * Structure used to pass information from ffs_sbupdate to its
 * helper routine ffs_use_bwrite.
 */
struct devfd {
	struct ufsmount	*ump;
	struct buf	*sbbp;
	int		 waitfor;
	int		 suspended;
	int		 error;
};

/*
 * Write a superblock and associated information back to disk.
 */
int
ffs_sbupdate(ump, waitfor, suspended)
	struct ufsmount *ump;
	int waitfor;
	int suspended;
{
	struct fs *fs;
	struct buf *sbbp;
	struct devfd devfd;

	fs = ump->um_fs;
	if (fs->fs_ronly == 1 &&
	    (ump->um_mountp->mnt_flag & (MNT_RDONLY | MNT_UPDATE)) !=
	    (MNT_RDONLY | MNT_UPDATE) && ump->um_fsckpid == 0)
		panic("ffs_sbupdate: write read-only filesystem");
	/*
	 * We use the superblock's buf to serialize calls to ffs_sbupdate().
	 */
	sbbp = getblk(ump->um_devvp, btodb(fs->fs_sblockloc),
	    (int)fs->fs_sbsize, 0, 0, 0);
	/*
	 * Initialize info needed for write function.
	 */
	devfd.ump = ump;
	devfd.sbbp = sbbp;
	devfd.waitfor = waitfor;
	devfd.suspended = suspended;
	devfd.error = 0;
	return (ffs_sbput(&devfd, fs, fs->fs_sblockloc, ffs_use_bwrite));
}

/*
 * Write function for use by filesystem-layer routines.
 */
static int
ffs_use_bwrite(void *devfd, off_t loc, void *buf, int size)
{
	struct devfd *devfdp;
	struct ufsmount *ump;
	struct buf *bp;
	struct fs *fs;
	int error;

	devfdp = devfd;
	ump = devfdp->ump;
	fs = ump->um_fs;
	/*
	 * Writing the superblock summary information.
	 */
	if (loc != fs->fs_sblockloc) {
		bp = getblk(ump->um_devvp, btodb(loc), size, 0, 0, 0);
		bcopy(buf, bp->b_data, (u_int)size);
		if (devfdp->suspended)
			bp->b_flags |= B_VALIDSUSPWRT;
		if (devfdp->waitfor != MNT_WAIT)
			bawrite(bp);
		else if ((error = bwrite(bp)) != 0)
			devfdp->error = error;
		return (0);
	}
	/*
	 * Writing the superblock itself. We need to do special checks for it.
	 */
	bp = devfdp->sbbp;
	if (devfdp->error != 0) {
		brelse(bp);
		return (devfdp->error);
	}
	if (fs->fs_magic == FS_UFS1_MAGIC && fs->fs_sblockloc != SBLOCK_UFS1 &&
	    (fs->fs_old_flags & FS_FLAGS_UPDATED) == 0) {
		printf("WARNING: %s: correcting fs_sblockloc from %jd to %d\n",
		    fs->fs_fsmnt, fs->fs_sblockloc, SBLOCK_UFS1);
		fs->fs_sblockloc = SBLOCK_UFS1;
	}
	if (fs->fs_magic == FS_UFS2_MAGIC && fs->fs_sblockloc != SBLOCK_UFS2 &&
	    (fs->fs_old_flags & FS_FLAGS_UPDATED) == 0) {
		printf("WARNING: %s: correcting fs_sblockloc from %jd to %d\n",
		    fs->fs_fsmnt, fs->fs_sblockloc, SBLOCK_UFS2);
		fs->fs_sblockloc = SBLOCK_UFS2;
	}
	if (MOUNTEDSOFTDEP(ump->um_mountp))
		softdep_setup_sbupdate(ump, (struct fs *)bp->b_data, bp);
	bcopy((caddr_t)fs, bp->b_data, (u_int)fs->fs_sbsize);
	ffs_oldfscompat_write((struct fs *)bp->b_data, ump);
	if (devfdp->suspended)
		bp->b_flags |= B_VALIDSUSPWRT;
	if (devfdp->waitfor != MNT_WAIT)
		bawrite(bp);
	else if ((error = bwrite(bp)) != 0)
		devfdp->error = error;
	return (devfdp->error);
}

static int
ffs_extattrctl(struct mount *mp, int cmd, struct vnode *filename_vp,
	int attrnamespace, const char *attrname)
{

#ifdef UFS_EXTATTR
	return (ufs_extattrctl(mp, cmd, filename_vp, attrnamespace,
	    attrname));
#else
	return (vfs_stdextattrctl(mp, cmd, filename_vp, attrnamespace,
	    attrname));
#endif
}

static void
ffs_ifree(struct ufsmount *ump, struct inode *ip)
{

	if (ump->um_fstype == UFS1 && ip->i_din1 != NULL)
		uma_zfree(uma_ufs1, ip->i_din1);
	else if (ip->i_din2 != NULL)
		uma_zfree(uma_ufs2, ip->i_din2);
	uma_zfree(uma_inode, ip);
}

static int dobkgrdwrite = 1;
SYSCTL_INT(_debug, OID_AUTO, dobkgrdwrite, CTLFLAG_RW, &dobkgrdwrite, 0,
    "Do background writes (honoring the BV_BKGRDWRITE flag)?");

/*
 * Complete a background write started from bwrite.
 */
static void
ffs_backgroundwritedone(struct buf *bp)
{
	struct bufobj *bufobj;
	struct buf *origbp;

	/*
	 * Find the original buffer that we are writing.
	 */
	bufobj = bp->b_bufobj;
	BO_LOCK(bufobj);
	if ((origbp = gbincore(bp->b_bufobj, bp->b_lblkno)) == NULL)
		panic("backgroundwritedone: lost buffer");

	/*
	 * We should mark the cylinder group buffer origbp as
	 * dirty, to not loose the failed write.
	 */
	if ((bp->b_ioflags & BIO_ERROR) != 0)
		origbp->b_vflags |= BV_BKGRDERR;
	BO_UNLOCK(bufobj);
	/*
	 * Process dependencies then return any unfinished ones.
	 */
	if (!LIST_EMPTY(&bp->b_dep) && (bp->b_ioflags & BIO_ERROR) == 0)
		buf_complete(bp);
#ifdef SOFTUPDATES
	if (!LIST_EMPTY(&bp->b_dep))
		softdep_move_dependencies(bp, origbp);
#endif
	/*
	 * This buffer is marked B_NOCACHE so when it is released
	 * by biodone it will be tossed.
	 */
	bp->b_flags |= B_NOCACHE;
	bp->b_flags &= ~B_CACHE;
	pbrelvp(bp);

	/*
	 * Prevent brelse() from trying to keep and re-dirtying bp on
	 * errors. It causes b_bufobj dereference in
	 * bdirty()/reassignbuf(), and b_bufobj was cleared in
	 * pbrelvp() above.
	 */
	if ((bp->b_ioflags & BIO_ERROR) != 0)
		bp->b_flags |= B_INVAL;
	bufdone(bp);
	BO_LOCK(bufobj);
	/*
	 * Clear the BV_BKGRDINPROG flag in the original buffer
	 * and awaken it if it is waiting for the write to complete.
	 * If BV_BKGRDINPROG is not set in the original buffer it must
	 * have been released and re-instantiated - which is not legal.
	 */
	KASSERT((origbp->b_vflags & BV_BKGRDINPROG),
	    ("backgroundwritedone: lost buffer2"));
	origbp->b_vflags &= ~BV_BKGRDINPROG;
	if (origbp->b_vflags & BV_BKGRDWAIT) {
		origbp->b_vflags &= ~BV_BKGRDWAIT;
		wakeup(&origbp->b_xflags);
	}
	BO_UNLOCK(bufobj);
}


/*
 * Write, release buffer on completion.  (Done by iodone
 * if async).  Do not bother writing anything if the buffer
 * is invalid.
 *
 * Note that we set B_CACHE here, indicating that buffer is
 * fully valid and thus cacheable.  This is true even of NFS
 * now so we set it generally.  This could be set either here
 * or in biodone() since the I/O is synchronous.  We put it
 * here.
 */
static int
ffs_bufwrite(struct buf *bp)
{
	struct buf *newbp;
	struct cg *cgp;

	CTR3(KTR_BUF, "bufwrite(%p) vp %p flags %X", bp, bp->b_vp, bp->b_flags);
	if (bp->b_flags & B_INVAL) {
		brelse(bp);
		return (0);
	}

	if (!BUF_ISLOCKED(bp))
		panic("bufwrite: buffer is not busy???");
	/*
	 * If a background write is already in progress, delay
	 * writing this block if it is asynchronous. Otherwise
	 * wait for the background write to complete.
	 */
	BO_LOCK(bp->b_bufobj);
	if (bp->b_vflags & BV_BKGRDINPROG) {
		if (bp->b_flags & B_ASYNC) {
			BO_UNLOCK(bp->b_bufobj);
			bdwrite(bp);
			return (0);
		}
		bp->b_vflags |= BV_BKGRDWAIT;
		msleep(&bp->b_xflags, BO_LOCKPTR(bp->b_bufobj), PRIBIO,
		    "bwrbg", 0);
		if (bp->b_vflags & BV_BKGRDINPROG)
			panic("bufwrite: still writing");
	}
	bp->b_vflags &= ~BV_BKGRDERR;
	BO_UNLOCK(bp->b_bufobj);

	/*
	 * If this buffer is marked for background writing and we
	 * do not have to wait for it, make a copy and write the
	 * copy so as to leave this buffer ready for further use.
	 *
	 * This optimization eats a lot of memory.  If we have a page
	 * or buffer shortfall we can't do it.
	 */
	if (dobkgrdwrite && (bp->b_xflags & BX_BKGRDWRITE) &&
	    (bp->b_flags & B_ASYNC) &&
	    !vm_page_count_severe() &&
	    !buf_dirty_count_severe()) {
		KASSERT(bp->b_iodone == NULL,
		    ("bufwrite: needs chained iodone (%p)", bp->b_iodone));

		/* get a new block */
		newbp = geteblk(bp->b_bufsize, GB_NOWAIT_BD);
		if (newbp == NULL)
			goto normal_write;

		KASSERT(buf_mapped(bp), ("Unmapped cg"));
		memcpy(newbp->b_data, bp->b_data, bp->b_bufsize);
		BO_LOCK(bp->b_bufobj);
		bp->b_vflags |= BV_BKGRDINPROG;
		BO_UNLOCK(bp->b_bufobj);
		newbp->b_xflags |=
		    (bp->b_xflags & BX_FSPRIV) | BX_BKGRDMARKER;
		newbp->b_lblkno = bp->b_lblkno;
		newbp->b_blkno = bp->b_blkno;
		newbp->b_offset = bp->b_offset;
		newbp->b_iodone = ffs_backgroundwritedone;
		newbp->b_flags |= B_ASYNC;
		newbp->b_flags &= ~B_INVAL;
		pbgetvp(bp->b_vp, newbp);

#ifdef SOFTUPDATES
		/*
		 * Move over the dependencies.  If there are rollbacks,
		 * leave the parent buffer dirtied as it will need to
		 * be written again.
		 */
		if (LIST_EMPTY(&bp->b_dep) ||
		    softdep_move_dependencies(bp, newbp) == 0)
			bundirty(bp);
#else
		bundirty(bp);
#endif

		/*
		 * Initiate write on the copy, release the original.  The
		 * BKGRDINPROG flag prevents it from going away until 
		 * the background write completes. We have to recalculate
		 * its check hash in case the buffer gets freed and then
		 * reconstituted from the buffer cache during a later read.
		 */
		if ((bp->b_xflags & BX_CYLGRP) != 0) {
			cgp = (struct cg *)bp->b_data;
			cgp->cg_ckhash = 0;
			cgp->cg_ckhash =
			    calculate_crc32c(~0L, bp->b_data, bp->b_bcount);
		}
		bqrelse(bp);
		bp = newbp;
	} else
		/* Mark the buffer clean */
		bundirty(bp);


	/* Let the normal bufwrite do the rest for us */
normal_write:
	/*
	 * If we are writing a cylinder group, update its time.
	 */
	if ((bp->b_xflags & BX_CYLGRP) != 0) {
		cgp = (struct cg *)bp->b_data;
		cgp->cg_old_time = cgp->cg_time = time_second;
	}
	return (bufwrite(bp));
}


static void
ffs_geom_strategy(struct bufobj *bo, struct buf *bp)
{
	struct vnode *vp;
	struct buf *tbp;
	int error, nocopy;

	vp = bo2vnode(bo);
	if (bp->b_iocmd == BIO_WRITE) {
		if ((bp->b_flags & B_VALIDSUSPWRT) == 0 &&
		    bp->b_vp != NULL && bp->b_vp->v_mount != NULL &&
		    (bp->b_vp->v_mount->mnt_kern_flag & MNTK_SUSPENDED) != 0)
			panic("ffs_geom_strategy: bad I/O");
		nocopy = bp->b_flags & B_NOCOPY;
		bp->b_flags &= ~(B_VALIDSUSPWRT | B_NOCOPY);
		if ((vp->v_vflag & VV_COPYONWRITE) && nocopy == 0 &&
		    vp->v_rdev->si_snapdata != NULL) {
			if ((bp->b_flags & B_CLUSTER) != 0) {
				runningbufwakeup(bp);
				TAILQ_FOREACH(tbp, &bp->b_cluster.cluster_head,
					      b_cluster.cluster_entry) {
					error = ffs_copyonwrite(vp, tbp);
					if (error != 0 &&
					    error != EOPNOTSUPP) {
						bp->b_error = error;
						bp->b_ioflags |= BIO_ERROR;
						bufdone(bp);
						return;
					}
				}
				bp->b_runningbufspace = bp->b_bufsize;
				atomic_add_long(&runningbufspace,
					       bp->b_runningbufspace);
			} else {
				error = ffs_copyonwrite(vp, bp);
				if (error != 0 && error != EOPNOTSUPP) {
					bp->b_error = error;
					bp->b_ioflags |= BIO_ERROR;
					bufdone(bp);
					return;
				}
			}
		}
#ifdef SOFTUPDATES
		if ((bp->b_flags & B_CLUSTER) != 0) {
			TAILQ_FOREACH(tbp, &bp->b_cluster.cluster_head,
				      b_cluster.cluster_entry) {
				if (!LIST_EMPTY(&tbp->b_dep))
					buf_start(tbp);
			}
		} else {
			if (!LIST_EMPTY(&bp->b_dep))
				buf_start(bp);
		}

#endif
		/*
		 * Check for metadata that needs check-hashes and update them.
		 */
		switch (bp->b_xflags & BX_FSPRIV) {
		case BX_CYLGRP:
			((struct cg *)bp->b_data)->cg_ckhash = 0;
			((struct cg *)bp->b_data)->cg_ckhash =
			    calculate_crc32c(~0L, bp->b_data, bp->b_bcount);
			break;

		case BX_SUPERBLOCK:
		case BX_INODE:
		case BX_INDIR:
		case BX_DIR:
			printf("Check-hash write is unimplemented!!!\n");
			break;

		case 0:
			break;

		default:
			printf("multiple buffer types 0x%b\n",
			    (u_int)(bp->b_xflags & BX_FSPRIV),
			    PRINT_UFS_BUF_XFLAGS);
			break;
		}
	}
	g_vfs_strategy(bo, bp);
}

int
ffs_own_mount(const struct mount *mp)
{

	if (mp->mnt_op == &ufs_vfsops)
		return (1);
	return (0);
}

#ifdef	DDB
#ifdef SOFTUPDATES

/* defined in ffs_softdep.c */
extern void db_print_ffs(struct ufsmount *ump);

DB_SHOW_COMMAND(ffs, db_show_ffs)
{
	struct mount *mp;
	struct ufsmount *ump;

	if (have_addr) {
		ump = VFSTOUFS((struct mount *)addr);
		db_print_ffs(ump);
		return;
	}

	TAILQ_FOREACH(mp, &mountlist, mnt_list) {
		if (!strcmp(mp->mnt_stat.f_fstypename, ufs_vfsconf.vfc_name))
			db_print_ffs(VFSTOUFS(mp));
	}
}

#endif	/* SOFTUPDATES */
#endif	/* DDB */
