/*	$OpenBSD: ext2fs_vfsops.c,v 1.123 2025/09/20 13:53:36 mpi Exp $	*/
/*	$NetBSD: ext2fs_vfsops.c,v 1.1 1997/06/11 09:34:07 bouyer Exp $	*/

/*
 * Copyright (c) 1997 Manuel Bouyer.
 * Copyright (c) 1989, 1991, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer in the
 *	documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *	may be used to endorse or promote products derived from this software
 *	without specific prior written permission.
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
 *	@(#)ffs_vfsops.c	8.14 (Berkeley) 11/28/94
 * Modified for ext2fs by Manuel Bouyer.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/namei.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#include <sys/vnode.h>
#include <sys/socket.h>
#include <sys/mount.h>
#include <sys/buf.h>
#include <sys/disk.h>
#include <sys/mbuf.h>
#include <sys/fcntl.h>
#include <sys/disklabel.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#include <sys/pool.h>
#include <sys/lock.h>
#include <sys/dkio.h>
#include <sys/specdev.h>

#include <ufs/ufs/quota.h>
#include <ufs/ufs/ufsmount.h>
#include <ufs/ufs/inode.h>
#include <ufs/ufs/dir.h>
#include <ufs/ufs/ufs_extern.h>

#include <ufs/ext2fs/ext2fs.h>
#include <ufs/ext2fs/ext2fs_extern.h>

int ext2fs_sbupdate(struct ufsmount *, int);
static int	e2fs_sbcheck(struct ext2fs *, int);

const struct vfsops ext2fs_vfsops = {
	.vfs_mount	= ext2fs_mount,
	.vfs_start	= ufs_start,
	.vfs_unmount	= ext2fs_unmount,
	.vfs_root	= ufs_root,
	.vfs_quotactl	= ufs_quotactl,
	.vfs_statfs	= ext2fs_statfs,
	.vfs_sync	= ext2fs_sync,
	.vfs_vget	= ext2fs_vget,
	.vfs_fhtovp	= ext2fs_fhtovp,
	.vfs_vptofh	= ext2fs_vptofh,
	.vfs_init	= ext2fs_init,
	.vfs_checkexp	= ufs_check_export,
};

struct pool ext2fs_inode_pool;
struct pool ext2fs_dinode_pool;

extern u_long ext2gennumber;

int
ext2fs_init(struct vfsconf *vfsp)
{
	pool_init(&ext2fs_inode_pool, sizeof(struct inode), 0,
	    IPL_NONE, PR_WAITOK, "ext2inopl", NULL);
	pool_init(&ext2fs_dinode_pool, sizeof(struct ext2fs_dinode), 0,
	    IPL_NONE, PR_WAITOK, "ext2dinopl", NULL);

	return (ufs_init(vfsp));
}

/*
 * Called by main() when ext2fs is going to be mounted as root.
 */

int
ext2fs_mountroot(void)
{
	struct m_ext2fs *fs;
        struct mount *mp;
	struct proc *p = curproc;	/* XXX */
	struct ufsmount *ump;
	int error;

	/*
	 * Get vnodes for swapdev and rootdev.
	 */
	if (bdevvp(swapdev, &swapdev_vp) || bdevvp(rootdev, &rootvp))
		panic("ext2fs_mountroot: can't setup bdevvp's");

	if ((error = vfs_rootmountalloc("ext2fs", "root_device", &mp)) != 0) {
		vrele(rootvp);
		return (error);
	}

	if ((error = ext2fs_mountfs(rootvp, mp, p)) != 0) {
		vfs_unbusy(mp);
		vfs_mount_free(mp);
		vrele(rootvp);
		return (error);
	}

	TAILQ_INSERT_TAIL(&mountlist, mp, mnt_list);
	ump = VFSTOUFS(mp);
	fs = ump->um_e2fs;
	memset(fs->e2fs_fsmnt, 0, sizeof(fs->e2fs_fsmnt));
	strlcpy(fs->e2fs_fsmnt, mp->mnt_stat.f_mntonname, sizeof(fs->e2fs_fsmnt));
	if (fs->e2fs.e2fs_rev > E2FS_REV0) {
		memset(fs->e2fs.e2fs_fsmnt, 0, sizeof(fs->e2fs.e2fs_fsmnt));
		strlcpy(fs->e2fs.e2fs_fsmnt, mp->mnt_stat.f_mntonname,
		    sizeof(fs->e2fs.e2fs_fsmnt));
	}
	(void)ext2fs_statfs(mp, &mp->mnt_stat, p);
	vfs_unbusy(mp);
	inittodr(fs->e2fs.e2fs_wtime);
	return (0);
}

/*
 * VFS Operations.
 *
 * mount system call
 */
int
ext2fs_mount(struct mount *mp, const char *path, void *data,
    struct nameidata *ndp, struct proc *p)
{
	struct vnode *devvp;
	struct ufs_args *args = data;
	struct ufsmount *ump = NULL;
	struct m_ext2fs *fs;
	char fname[MNAMELEN];
	char fspec[MNAMELEN];
	int error, flags;

	/*
	 * If updating, check whether changing from read-only to
	 * read/write; if there is no device name, that's all we do.
	 */
	if (mp->mnt_flag & MNT_UPDATE) {
		ump = VFSTOUFS(mp);
		fs = ump->um_e2fs;
		if (fs->e2fs_ronly == 0 && (mp->mnt_flag & MNT_RDONLY)) {
			flags = WRITECLOSE;
			if (mp->mnt_flag & MNT_FORCE)
				flags |= FORCECLOSE;
			error = ext2fs_flushfiles(mp, flags, p);
			if (error == 0 &&
			    ext2fs_cgupdate(ump, MNT_WAIT) == 0 &&
			    (fs->e2fs.e2fs_state & E2FS_ERRORS) == 0) {
				fs->e2fs.e2fs_state = E2FS_ISCLEAN;
				(void)ext2fs_sbupdate(ump, MNT_WAIT);
			}
			if (error)
				return (error);
			fs->e2fs_ronly = 1;
		}
		if (mp->mnt_flag & MNT_RELOAD) {
			error = ext2fs_reload(mp, ndp->ni_cnd.cn_cred, p);
			if (error)
				return (error);
		}
		if (fs->e2fs_ronly && (mp->mnt_flag & MNT_WANTRDWR)) {
			fs->e2fs_ronly = 0;
			if (fs->e2fs.e2fs_state == E2FS_ISCLEAN)
				fs->e2fs.e2fs_state = 0;
			else
				fs->e2fs.e2fs_state = E2FS_ERRORS;
			fs->e2fs_fmod = 1;
		}
		if (args && args->fspec == NULL) {
			/*
			 * Process export requests.
			 */
			return (vfs_export(mp, &ump->um_export,
			    &args->export_info));
		}
		if (args == NULL)
			goto success;
	}
	/*
	 * Not an update, or updating the name: look up the name
	 * and verify that it refers to a sensible block device.
	 */
	error = copyinstr(args->fspec, fspec, sizeof(fspec), NULL);
	if (error)
		goto error;

	if (disk_map(fspec, fname, MNAMELEN, DM_OPENBLCK) == -1)
		memcpy(fname, fspec, sizeof(fname));

	NDINIT(ndp, LOOKUP, FOLLOW, UIO_SYSSPACE, fname, p);
	if ((error = namei(ndp)) != 0)
		goto error;
	devvp = ndp->ni_vp;

	if (devvp->v_type != VBLK) {
		error = ENOTBLK;
		goto error_devvp;
	}
	if (major(devvp->v_rdev) >= nblkdev) {
		error = ENXIO;
		goto error_devvp;
	}
	if ((mp->mnt_flag & MNT_UPDATE) == 0)
		error = ext2fs_mountfs(devvp, mp, p);
	else {
		if (devvp != ump->um_devvp)
			error = EINVAL;	/* XXX needs translation */
		else
			vrele(devvp);
	}
	if (error)
		goto error_devvp;
	ump = VFSTOUFS(mp);
	fs = ump->um_e2fs;

	memset(fs->e2fs_fsmnt, 0, sizeof(fs->e2fs_fsmnt));
	strlcpy(fs->e2fs_fsmnt, path, sizeof(fs->e2fs_fsmnt));
	if (fs->e2fs.e2fs_rev > E2FS_REV0) {
		memset(fs->e2fs.e2fs_fsmnt, 0, sizeof(fs->e2fs.e2fs_fsmnt));
		strlcpy(fs->e2fs.e2fs_fsmnt, mp->mnt_stat.f_mntonname,
		    sizeof(fs->e2fs.e2fs_fsmnt));
	}
	memcpy(mp->mnt_stat.f_mntonname, fs->e2fs_fsmnt, MNAMELEN);
	memset(mp->mnt_stat.f_mntfromname, 0, MNAMELEN);
	strlcpy(mp->mnt_stat.f_mntfromname, fname, MNAMELEN);
	memset(mp->mnt_stat.f_mntfromspec, 0, MNAMELEN);
	strlcpy(mp->mnt_stat.f_mntfromspec, fspec, MNAMELEN);
	memcpy(&mp->mnt_stat.mount_info.ufs_args, args, sizeof(*args));

	if (fs->e2fs_fmod != 0) {	/* XXX */
		fs->e2fs_fmod = 0;
		if (fs->e2fs.e2fs_state == 0)
			fs->e2fs.e2fs_wtime = gettime();
		else
			printf("%s: file system not clean; please fsck(8)\n",
			    mp->mnt_stat.f_mntfromname);
		ext2fs_cgupdate(ump, MNT_WAIT);
	}

	goto success;

error_devvp:
	/* Error with devvp held. */
	vrele(devvp);

error:
	/* Error with no state to backout. */

success:
	return (error);
}

int ext2fs_reload_vnode(struct vnode *, void *args);

struct ext2fs_reload_args {
	struct m_ext2fs *fs;
	struct proc *p;
	struct ucred *cred;
	struct vnode *devvp;
};

int
ext2fs_reload_vnode(struct vnode *vp, void *args)
{
	struct ext2fs_reload_args *era = args;
	struct buf *bp;
	struct inode *ip;
	int error;
	caddr_t cp;

	/*
	 * Step 4: invalidate all inactive vnodes.
	 */
	if (vp->v_usecount == 0) {
		vgonel(vp, era->p);
		return (0);
	}

	/*
	 * Step 5: invalidate all cached file data.
	 */
	if (vget(vp, LK_EXCLUSIVE))
		return (0);

	if (vinvalbuf(vp, 0, era->cred, era->p, 0, INFSLP))
		panic("ext2fs_reload: dirty2");
	/*
	 * Step 6: re-read inode data for all active vnodes.
	 */
	ip = VTOI(vp);
	error = bread(era->devvp,
	    fsbtodb(era->fs, ino_to_fsba(era->fs, ip->i_number)),
	    (int)era->fs->e2fs_bsize, &bp);
	if (error) {
		vput(vp);
		return (error);
	}
	cp = (caddr_t)bp->b_data +
	    (ino_to_fsbo(era->fs, ip->i_number) * EXT2_DINODE_SIZE(era->fs));
	e2fs_iload(era->fs, (struct ext2fs_dinode *)cp, ip->i_e2din);
	brelse(bp);
	vput(vp);
	return (0);
}

static off_t
ext2fs_maxfilesize(struct m_ext2fs *fs)
{
	bool huge = fs->e2fs.e2fs_features_rocompat & EXT2F_ROCOMPAT_HUGE_FILE;
	off_t b = fs->e2fs_bsize / 4;
	off_t physically, logically;

	physically = dbtob(huge ? ((1ULL << 48) - 1) : UINT_MAX);
	logically = (12ULL + b + b*b + b*b*b) * fs->e2fs_bsize;

	return MIN(logically, physically);
}

static int
e2fs_sbfill(struct vnode *devvp, struct m_ext2fs *fs)
{
	struct buf *bp = NULL;
	int i, error;

	/* XXX assume hardware block size == 512 */
	fs->e2fs_ncg = howmany(fs->e2fs.e2fs_bcount - fs->e2fs.e2fs_first_dblock,
	    fs->e2fs.e2fs_bpg);
	fs->e2fs_fsbtodb = fs->e2fs.e2fs_log_bsize + 1;
	fs->e2fs_bsize = 1024 << fs->e2fs.e2fs_log_bsize;
	fs->e2fs_bshift = LOG_MINBSIZE + fs->e2fs.e2fs_log_bsize;
	fs->e2fs_fsize = 1024 << fs->e2fs.e2fs_log_fsize;

	fs->e2fs_qbmask = fs->e2fs_bsize - 1;
	fs->e2fs_bmask = ~fs->e2fs_qbmask;

	fs->e2fs_ipb = fs->e2fs_bsize / EXT2_DINODE_SIZE(fs);
	fs->e2fs_itpg = fs->e2fs.e2fs_ipg / fs->e2fs_ipb;

	/* Re-read group descriptors from the disk. */
	fs->e2fs_ngdb = howmany(fs->e2fs_ncg,
	    fs->e2fs_bsize / sizeof(struct ext2_gd));
	fs->e2fs_gd = mallocarray(fs->e2fs_ngdb, fs->e2fs_bsize,
	    M_UFSMNT, M_WAITOK);

	for (i = 0; i < fs->e2fs_ngdb; ++i) {
		daddr_t dblk = ((fs->e2fs_bsize > 1024) ? 0 : 1) + i + 1;
		size_t gdesc = i * fs->e2fs_bsize / sizeof(struct ext2_gd);
		struct ext2_gd *gd;

		error = bread(devvp, fsbtodb(fs, dblk), fs->e2fs_bsize, &bp);
		if (error) {
			size_t gdescs_space = fs->e2fs_ngdb * fs->e2fs_bsize;

			free(fs->e2fs_gd, M_UFSMNT, gdescs_space);
			fs->e2fs_gd = NULL;
			brelse(bp);
			return (error);
		}

		gd = (struct ext2_gd *) bp->b_data;
		e2fs_cgload(gd, fs->e2fs_gd + gdesc, fs->e2fs_bsize);
		brelse(bp);
		bp = NULL;
	}

	if (!(fs->e2fs.e2fs_features_rocompat & EXT2F_ROCOMPAT_LARGE_FILE) ||
	    (fs->e2fs.e2fs_rev == E2FS_REV0))
		fs->e2fs_maxfilesize = INT_MAX;
	else
		fs->e2fs_maxfilesize = ext2fs_maxfilesize(fs);

	if (fs->e2fs.e2fs_features_incompat & EXT2F_INCOMPAT_EXTENTS)
		fs->e2fs_maxfilesize *= 4;

	return (0);
}

/*
 * Reload all incore data for a filesystem (used after running fsck on
 * the root filesystem and finding things to fix). The filesystem must
 * be mounted read-only.
 *
 * Things to do to update the mount:
 *	1) invalidate all cached meta-data.
 *	2) re-read superblock from disk.
 *	3) re-read summary information from disk.
 *	4) invalidate all inactive vnodes.
 *	5) invalidate all cached file data.
 *	6) re-read inode data for all active vnodes.
 */
int
ext2fs_reload(struct mount *mountp, struct ucred *cred, struct proc *p)
{
	struct vnode *devvp;
	struct buf *bp;
	struct m_ext2fs *fs;
	struct ext2fs *newfs;
	int error;
	struct ext2fs_reload_args era;

	if ((mountp->mnt_flag & MNT_RDONLY) == 0)
		return (EINVAL);
	/*
	 * Step 1: invalidate all cached meta-data.
	 */
	devvp = VFSTOUFS(mountp)->um_devvp;
	vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
	error = vinvalbuf(devvp, 0, cred, p, 0, INFSLP);
	VOP_UNLOCK(devvp);
	if (error != 0)
		panic("ext2fs_reload: dirty1");

	/*
	 * Step 2: re-read superblock from disk.
	 */
	error = bread(devvp, (daddr_t)(SBOFF / DEV_BSIZE), SBSIZE, &bp);
	if (error) {
		brelse(bp);
		return (error);
	}
	newfs = (struct ext2fs *)bp->b_data;
	error = e2fs_sbcheck(newfs, (mountp->mnt_flag & MNT_RDONLY));
	if (error) {
		brelse(bp);
		return (error);
	}

	fs = VFSTOUFS(mountp)->um_e2fs;
	/*
	 * Copy in the new superblock, compute in-memory values
	 * and load group descriptors.
	 */
	e2fs_sbload(newfs, &fs->e2fs);
	if ((error = e2fs_sbfill(devvp, fs)) != 0)
		return (error);

	era.p = p;
	era.cred = cred;
	era.fs = fs;
	era.devvp = devvp;

	error = vfs_mount_foreach_vnode(mountp, ext2fs_reload_vnode, &era);

	return (error);
}

/*
 * Common code for mount and mountroot
 */
int
ext2fs_mountfs(struct vnode *devvp, struct mount *mp, struct proc *p)
{
	struct ufsmount *ump;
	struct buf *bp;
	struct ext2fs *fs;
	dev_t dev;
	int error, ronly;
	struct ucred *cred;

	dev = devvp->v_rdev;
	cred = p ? p->p_ucred : NOCRED;
	/*
	 * Disallow multiple mounts of the same device.
	 * Disallow mounting of a device that is currently in use
	 * (except for root, which might share swap device for miniroot).
	 * Flush out any old buffers remaining from a previous use.
	 */
	if ((error = vfs_mountedon(devvp)) != 0)
		return (error);
	if (vcount(devvp) > 1 && devvp != rootvp)
		return (EBUSY);
	vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
	error = vinvalbuf(devvp, V_SAVE, cred, p, 0, INFSLP);
	VOP_UNLOCK(devvp);
	if (error != 0)
		return (error);

	ronly = (mp->mnt_flag & MNT_RDONLY) != 0;
	error = VOP_OPEN(devvp, ronly ? FREAD : FREAD|FWRITE, FSCRED, p);
	if (error)
		return (error);

	bp = NULL;
	ump = NULL;

	/*
	 * Read the superblock from disk.
	 */
	error = bread(devvp, (daddr_t)(SBOFF / DEV_BSIZE), SBSIZE, &bp);
	if (error)
		goto out;
	fs = (struct ext2fs *)bp->b_data;
	error = e2fs_sbcheck(fs, ronly);
	if (error)
		goto out;

	ump = malloc(sizeof *ump, M_UFSMNT, M_WAITOK | M_ZERO);
	ump->um_e2fs = malloc(sizeof(struct m_ext2fs), M_UFSMNT,
	    M_WAITOK | M_ZERO);

	/*
	 * Copy in the superblock, compute in-memory values
	 * and load group descriptors.
	 */
	e2fs_sbload(fs, &ump->um_e2fs->e2fs);
	if ((error = e2fs_sbfill(devvp, ump->um_e2fs)) != 0)
		goto out;
	brelse(bp);
	bp = NULL;
	fs = &ump->um_e2fs->e2fs;
	ump->um_e2fs->e2fs_ronly = ronly;
	ump->um_fstype = UM_EXT2FS;

	if (ronly == 0) {
		if (fs->e2fs_state == E2FS_ISCLEAN)
			fs->e2fs_state = 0;
		else
			fs->e2fs_state = E2FS_ERRORS;
		ump->um_e2fs->e2fs_fmod = 1;
	}

	mp->mnt_data = ump;
	mp->mnt_stat.f_fsid.val[0] = (long)dev;
	mp->mnt_stat.f_fsid.val[1] = mp->mnt_vfc->vfc_typenum;
	mp->mnt_stat.f_namemax = MAXNAMLEN;
	mp->mnt_flag |= MNT_LOCAL;
	ump->um_mountp = mp;
	ump->um_dev = dev;
	ump->um_devvp = devvp;
	ump->um_nindir = NINDIR(ump->um_e2fs);
	ump->um_bptrtodb = ump->um_e2fs->e2fs_fsbtodb;
	ump->um_seqinc = 1; /* no frags */
	ump->um_maxsymlinklen = EXT2_MAXSYMLINKLEN;
	devvp->v_specmountpoint = mp;
	return (0);
out:
	if (devvp->v_specinfo)
		devvp->v_specmountpoint = NULL;
	if (bp)
		brelse(bp);
	vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
	(void)VOP_CLOSE(devvp, ronly ? FREAD : FREAD|FWRITE, cred, p);
	VOP_UNLOCK(devvp);
	if (ump) {
		free(ump->um_e2fs, M_UFSMNT, sizeof *ump->um_e2fs);
		free(ump, M_UFSMNT, sizeof *ump);
		mp->mnt_data = NULL;
	}
	return (error);
}

/*
 * unmount system call
 */
int
ext2fs_unmount(struct mount *mp, int mntflags, struct proc *p)
{
	struct ufsmount *ump;
	struct m_ext2fs *fs;
	int error, flags;
	size_t gdescs_space;

	flags = 0;
	if (mntflags & MNT_FORCE)
		flags |= FORCECLOSE;
	if ((error = ext2fs_flushfiles(mp, flags, p)) != 0)
		return (error);
	ump = VFSTOUFS(mp);
	fs = ump->um_e2fs;
	gdescs_space = fs->e2fs_ngdb * fs->e2fs_bsize;

	if (!fs->e2fs_ronly && ext2fs_cgupdate(ump, MNT_WAIT) == 0 &&
	    (fs->e2fs.e2fs_state & E2FS_ERRORS) == 0) {
		fs->e2fs.e2fs_state = E2FS_ISCLEAN;
		(void) ext2fs_sbupdate(ump, MNT_WAIT);
	}

	if (ump->um_devvp->v_type != VBAD)
		ump->um_devvp->v_specmountpoint = NULL;
	vn_lock(ump->um_devvp, LK_EXCLUSIVE | LK_RETRY);
	(void)VOP_CLOSE(ump->um_devvp, fs->e2fs_ronly ? FREAD : FREAD|FWRITE,
	    NOCRED, p);
	vput(ump->um_devvp);
	free(fs->e2fs_gd, M_UFSMNT, gdescs_space);
	free(fs, M_UFSMNT, sizeof *fs);
	free(ump, M_UFSMNT, sizeof *ump);
	mp->mnt_data = NULL;
	mp->mnt_flag &= ~MNT_LOCAL;
	return (0);
}

/*
 * Flush out all the files in a filesystem.
 */
int
ext2fs_flushfiles(struct mount *mp, int flags, struct proc *p)
{
	struct ufsmount *ump;
	int error;

	ump = VFSTOUFS(mp);
	/*
	 * Flush all the files.
	 */
	if ((error = vflush(mp, NULL, flags)) != 0)
		return (error);
	/*
	 * Flush filesystem metadata.
	 */
	vn_lock(ump->um_devvp, LK_EXCLUSIVE | LK_RETRY);
	error = VOP_FSYNC(ump->um_devvp, p->p_ucred, MNT_WAIT, p);
	VOP_UNLOCK(ump->um_devvp);
	return (error);
}

/*
 * Get file system statistics.
 */
int
ext2fs_statfs(struct mount *mp, struct statfs *sbp, struct proc *p)
{
	struct ufsmount *ump;
	struct m_ext2fs *fs;
	u_int32_t overhead, overhead_per_group;
	int i, ngroups;

	ump = VFSTOUFS(mp);
	fs = ump->um_e2fs;
	if (fs->e2fs.e2fs_magic != E2FS_MAGIC)
		panic("ext2fs_statfs");

	/*
	 * Compute the overhead (FS structures)
	 */
	overhead_per_group = 1 /* block bitmap */ + 1 /* inode bitmap */ +
	    fs->e2fs_itpg;
	overhead = fs->e2fs.e2fs_first_dblock +
	    fs->e2fs_ncg * overhead_per_group;
	if (fs->e2fs.e2fs_rev > E2FS_REV0 &&
	    fs->e2fs.e2fs_features_rocompat & EXT2F_ROCOMPAT_SPARSE_SUPER) {
		for (i = 0, ngroups = 0; i < fs->e2fs_ncg; i++) {
			if (cg_has_sb(i))
				ngroups++;
		}
	} else {
		ngroups = fs->e2fs_ncg;
	}
	overhead += ngroups * (1 + fs->e2fs_ngdb);

	sbp->f_bsize = fs->e2fs_bsize;
	sbp->f_iosize = fs->e2fs_bsize;
	sbp->f_blocks = fs->e2fs.e2fs_bcount - overhead;
	sbp->f_bfree = fs->e2fs.e2fs_fbcount;
	sbp->f_bavail = sbp->f_bfree - fs->e2fs.e2fs_rbcount;
	sbp->f_files =  fs->e2fs.e2fs_icount;
	sbp->f_favail = sbp->f_ffree = fs->e2fs.e2fs_ficount;
	copy_statfs_info(sbp, mp);

	return (0);
}

int ext2fs_sync_vnode(struct vnode *vp, void *);

struct ext2fs_sync_args {
	int allerror;
	int waitfor;
	int nlink0;
	int inflight;
	struct proc *p;
	struct ucred *cred;
};

int
ext2fs_sync_vnode(struct vnode *vp, void *args)
{
	struct ext2fs_sync_args *esa = args;
	struct inode *ip;
	int error, nlink0 = 0;
	int s, skip = 0;

	if (vp->v_type == VNON)
		return (0);

	ip = VTOI(vp);
	
	if (ip->i_e2fs_nlink == 0)
		nlink0 = 1;

	s = splbio();
	if ((ip->i_flag & (IN_ACCESS | IN_CHANGE | IN_MODIFIED | IN_UPDATE)) == 0 &&
	    LIST_EMPTY(&vp->v_dirtyblkhd)) {
		skip = 1;
	}
	splx(s);

	if (skip)
		goto end;

	if (vget(vp, LK_EXCLUSIVE | LK_NOWAIT)) {
		esa->inflight = MIN(esa->inflight+1, 65536);
		goto end;
	}

	if ((error = VOP_FSYNC(vp, esa->cred, esa->waitfor, esa->p)) != 0)
		esa->allerror = error;
	vput(vp);
end:
	esa->nlink0 = MIN(esa->nlink0 + nlink0, 65536);
	return (0);
}

/*
 * Go through the disk queues to initiate sandbagged IO;
 * go through the inodes to write those that have been modified;
 * initiate the writing of the super block if it has been modified.
 *
 * Should always be called with the mount point locked.
 */
int
ext2fs_sync(struct mount *mp, int waitfor, int stall,
    struct ucred *cred, struct proc *p)
{
	struct ufsmount *ump = VFSTOUFS(mp);
	struct m_ext2fs *fs;
	int error, allerror = 0, state, fmod;
	struct ext2fs_sync_args esa;

	fs = ump->um_e2fs;
	if (fs->e2fs_ronly != 0) {		/* XXX */
		printf("fs = %s\n", fs->e2fs_fsmnt);
		panic("update: rofs mod");
	}

	/*
	 * Write back each (modified) inode.
	 */
	esa.p = p;
	esa.cred = cred;
	esa.allerror = 0;
	esa.waitfor = waitfor;
	esa.nlink0 = 0;
	esa.inflight = 0;

	vfs_mount_foreach_vnode(mp, ext2fs_sync_vnode, &esa);
	if (esa.allerror != 0)
		allerror = esa.allerror;

	/*
	 * Force stale file system control information to be flushed.
	 */
	if (waitfor != MNT_LAZY) {
		vn_lock(ump->um_devvp, LK_EXCLUSIVE | LK_RETRY);
		if ((error = VOP_FSYNC(ump->um_devvp, cred, waitfor, p)) != 0)
			allerror = error;
		VOP_UNLOCK(ump->um_devvp);
	}
	/*
	 * Write back modified superblock.
	 */
	state = fs->e2fs.e2fs_state;
	fmod = fs->e2fs_fmod;
	if (stall && fs->e2fs_ronly == 0) {
		fs->e2fs_fmod = 1;
		if (allerror == 0 && esa.nlink0 == 0 && esa.inflight == 0) {
			if ((fs->e2fs.e2fs_state & E2FS_ERRORS) == 0)
				fs->e2fs.e2fs_state = E2FS_ISCLEAN;
#if 0
			printf("%s force clean (dangling %d inflight %d)\n",
			    mp->mnt_stat.f_mntonname, esa.nlink0, esa.inflight);
#endif
		} else {
			fs->e2fs.e2fs_state = 0;
#if 0
			printf("%s force dirty (dangling %d inflight %d)\n",
			    mp->mnt_stat.f_mntonname, esa.nlink0, esa.inflight);
#endif
		}
	}		
	if (fs->e2fs_fmod != 0) {
		fs->e2fs_fmod = 0;
		fs->e2fs.e2fs_wtime = gettime();
		if ((error = ext2fs_cgupdate(ump, waitfor)))
			allerror = error;
	}
	fs->e2fs.e2fs_state = state;
	fs->e2fs_fmod = fmod;
	return (allerror);
}

/*
 * Look up a EXT2FS dinode number to find its incore vnode, otherwise read it
 * in from disk.  If it is in core, wait for the lock bit to clear, then
 * return the inode locked.  Detection and handling of mount points must be
 * done by the calling routine.
 */
int
ext2fs_vget(struct mount *mp, ino_t ino, struct vnode **vpp)
{
	struct m_ext2fs *fs;
	struct inode *ip;
	struct ext2fs_dinode *dp;
	struct ufsmount *ump;
	struct buf *bp;
	struct vnode *vp;
	dev_t dev;
	int error;

	if (ino > (ufsino_t)-1)
		panic("ext2fs_vget: alien ino_t %llu",
		    (unsigned long long)ino);

	ump = VFSTOUFS(mp);
	dev = ump->um_dev;

 retry:
	if ((*vpp = ufs_ihashget(dev, ino)) != NULL)
		return (0);

	/* Allocate a new vnode/inode. */
	if ((error = getnewvnode(VT_EXT2FS, mp, &ext2fs_vops, &vp)) != 0) {
		*vpp = NULL;
		return (error);
	}

	ip = pool_get(&ext2fs_inode_pool, PR_WAITOK|PR_ZERO);
	rrw_init_flags(&ip->i_lock, "inode", RWL_DUPOK | RWL_IS_VNODE);
	vp->v_data = ip;
	ip->i_vnode = vp;
	ip->i_ump = ump;
	ip->i_e2fs = fs = ump->um_e2fs;
	ip->i_dev = dev;
	ip->i_number = ino;
	ip->i_e2fs_last_lblk = 0;
	ip->i_e2fs_last_blk = 0;

	/*
	 * Put it onto its hash chain and lock it so that other requests for
	 * this inode will block if they arrive while we are sleeping waiting
	 * for old data structures to be purged or for the contents of the
	 * disk portion of this inode to be read.
	 */
	error = ufs_ihashins(ip);

	if (error) {
		vrele(vp);

		if (error == EEXIST)
			goto retry;

		return (error);
	}

	/* Read in the disk contents for the inode, copy into the inode. */
	error = bread(ump->um_devvp, fsbtodb(fs, ino_to_fsba(fs, ino)),
	    (int)fs->e2fs_bsize, &bp);
	if (error) {
		/*
		 * The inode does not contain anything useful, so it would
	 	 * be misleading to leave it on its hash chain. With mode
		 * still zero, it will be unlinked and returned to the free
		 * list by vput().
		 */
		vput(vp);
		brelse(bp);
		*vpp = NULL;
		return (error);
	}

	dp = (struct ext2fs_dinode *) ((char *)bp->b_data
	    + EXT2_DINODE_SIZE(fs) * ino_to_fsbo(fs, ino));

	ip->i_e2din = pool_get(&ext2fs_dinode_pool, PR_WAITOK);
	e2fs_iload(fs, dp, ip->i_e2din);
	brelse(bp);

	ip->i_effnlink = ip->i_e2fs_nlink;

	/*
	 * The fields for storing the UID and GID of an ext2fs inode are
	 * limited to 16 bits. To overcome this limitation, Linux decided to
	 * scatter the highest bits of these values into a previously reserved
	 * area on the disk inode. We deal with this situation by having two
	 * 32-bit fields *out* of the disk inode to hold the complete values.
	 * Now that we are reading in the inode, compute these fields.
	 */
	ip->i_e2fs_uid = ip->i_e2fs_uid_low | (ip->i_e2fs_uid_high << 16);
	ip->i_e2fs_gid = ip->i_e2fs_gid_low | (ip->i_e2fs_gid_high << 16);

	/* If the inode was deleted, reset all fields */
	if (ip->i_e2fs_dtime != 0) {
		ip->i_e2fs_mode = ip->i_e2fs_nblock = 0;
		(void)ext2fs_setsize(ip, 0);
	}

	/*
	 * Initialize the vnode from the inode, check for aliases.
	 * Note that the underlying vnode may have changed.
	 */
	error = ext2fs_vinit(mp, &vp);
	if (error) {
		vput(vp);
		*vpp = NULL;
		return (error);
	}

	/*
	 * Finish inode initialization now that aliasing has been resolved.
	 */
	vref(ip->i_devvp);
	/*
	 * Set up a generation number for this inode if it does not
	 * already have one. This should only happen on old filesystems.
	 */
	if (ip->i_e2fs_gen == 0) {
		if (++ext2gennumber < (u_long)gettime())
			ext2gennumber = gettime();
		ip->i_e2fs_gen = ext2gennumber;
		if ((vp->v_mount->mnt_flag & MNT_RDONLY) == 0)
			ip->i_flag |= IN_MODIFIED;
	}

	*vpp = vp;
	return (0);
}

/*
 * File handle to vnode
 *
 * Have to be really careful about stale file handles:
 * - check that the inode number is valid
 * - call ext2fs_vget() to get the locked inode
 * - check for an unallocated inode (i_mode == 0)
 * - check that the given client host has export rights and return
 *   those rights via. exflagsp and credanonp
 */
int
ext2fs_fhtovp(struct mount *mp, struct fid *fhp, struct vnode **vpp)
{
	struct inode *ip;
	struct vnode *nvp;
	int error;
	struct ufid *ufhp;
	struct m_ext2fs *fs;

	ufhp = (struct ufid *)fhp;
	fs = VFSTOUFS(mp)->um_e2fs;
	if ((ufhp->ufid_ino < EXT2_FIRSTINO && ufhp->ufid_ino != EXT2_ROOTINO) ||
	    ufhp->ufid_ino > fs->e2fs_ncg * fs->e2fs.e2fs_ipg)
		return (ESTALE);

	if ((error = VFS_VGET(mp, ufhp->ufid_ino, &nvp)) != 0) {
		*vpp = NULL;
		return (error);
	}
	ip = VTOI(nvp);
	if (ip->i_e2fs_mode == 0 || ip->i_e2fs_dtime != 0 ||
	    ip->i_e2fs_gen != ufhp->ufid_gen) {
		vput(nvp);
		*vpp = NULL;
		return (ESTALE);
	}
	*vpp = nvp;
	return (0);
}

/*
 * Vnode pointer to File handle
 */
int
ext2fs_vptofh(struct vnode *vp, struct fid *fhp)
{
	struct inode *ip;
	struct ufid *ufhp;

	ip = VTOI(vp);
	ufhp = (struct ufid *)fhp;
	ufhp->ufid_len = sizeof(struct ufid);
	ufhp->ufid_ino = ip->i_number;
	ufhp->ufid_gen = ip->i_e2fs_gen;
	return (0);
}

/*
 * Write a superblock and associated information back to disk.
 */
int
ext2fs_sbupdate(struct ufsmount *mp, int waitfor)
{
	struct m_ext2fs *fs = mp->um_e2fs;
	struct buf *bp;
	int error = 0;

	bp = getblk(mp->um_devvp, SBLOCK, SBSIZE, 0, INFSLP);
	e2fs_sbsave(&fs->e2fs, (struct ext2fs *) bp->b_data);
	if (waitfor == MNT_WAIT)
		error = bwrite(bp);
	else
		bawrite(bp);
	fs->e2fs_fmod = 0;
	return (error);
}

int
ext2fs_cgupdate(struct ufsmount *mp, int waitfor)
{
	struct m_ext2fs *fs = mp->um_e2fs;
	struct buf *bp;
	int i, error = 0, allerror = 0;

	allerror = ext2fs_sbupdate(mp, waitfor);
	for (i = 0; i < fs->e2fs_ngdb; i++) {
		bp = getblk(mp->um_devvp, fsbtodb(fs, ((fs->e2fs_bsize>1024)?0:1)+i+1),
		    fs->e2fs_bsize, 0, INFSLP);
		e2fs_cgsave(&fs->e2fs_gd[i* fs->e2fs_bsize / sizeof(struct ext2_gd)], (struct ext2_gd*)bp->b_data, fs->e2fs_bsize);
		if (waitfor == MNT_WAIT)
			error = bwrite(bp);
		else
			bawrite(bp);
	}

	if (!allerror && error)
		allerror = error;
	return (allerror);
}

/* This is called before the superblock is copied.  Watch out for endianness! */
static int
e2fs_sbcheck(struct ext2fs *fs, int ronly)
{
	u_int32_t mask, tmp;
	int i;

	tmp = letoh16(fs->e2fs_magic);
	if (tmp != E2FS_MAGIC) {
		printf("ext2fs: wrong magic number 0x%x\n", tmp);
		return (EIO);		/* XXX needs translation */
	}

	tmp = letoh32(fs->e2fs_log_bsize);
	if (tmp > 2) {
		/* skewed log(block size): 1024 -> 0 | 2048 -> 1 | 4096 -> 2 */
		tmp += 10;
		printf("ext2fs: wrong log2(block size) %d\n", tmp);
		return (EIO);	   /* XXX needs translation */
	}

	if (fs->e2fs_bpg == 0) {
		printf("ext2fs: zero blocks per group\n");
		return (EIO);
	}

	tmp = letoh32(fs->e2fs_rev);
	if (tmp > E2FS_REV1) {
		printf("ext2fs: wrong revision number 0x%x\n", tmp);
		return (EIO);		/* XXX needs translation */
	}
	else if (tmp == E2FS_REV0)
		return (0);

	tmp = letoh32(fs->e2fs_first_ino);
	if (tmp != EXT2_FIRSTINO) {
		printf("ext2fs: first inode at 0x%x\n", tmp);
		return (EINVAL);      /* XXX needs translation */
	}

	tmp = letoh32(fs->e2fs_features_incompat);
	mask = tmp & ~(EXT2F_INCOMPAT_SUPP | EXT4F_RO_INCOMPAT_SUPP);
	if (mask) {
		printf("ext2fs: unsupported incompat features: ");
		for (i = 0; i < nitems(incompat); i++)
			if (mask & incompat[i].mask)
				printf("%s ", incompat[i].name);
		printf("\n");
		return (EINVAL);      /* XXX needs translation */
	}

	if (!ronly && (tmp & EXT4F_RO_INCOMPAT_SUPP)) {
		printf("ext4fs: only read-only support right now\n");
		return (EROFS);      /* XXX needs translation */
	}

	if (tmp & EXT2F_INCOMPAT_RECOVER) {
		printf("ext2fs: your file system says it needs recovery\n");
		if (!ronly)
			return (EROFS);	/* XXX needs translation */
	}

	tmp = letoh32(fs->e2fs_features_rocompat) & ~EXT2F_ROCOMPAT_SUPP;
	if (!ronly && tmp) {
		printf("ext2fs: unsupported R/O compat features: ");
		for (i = 0; i < nitems(ro_compat); i++)
			if (tmp & ro_compat[i].mask)
				printf("%s ", ro_compat[i].name);
		printf("\n");
		return (EROFS);      /* XXX needs translation */
	}

	return (0);
}
