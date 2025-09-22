/*	$OpenBSD: msdosfs_vfsops.c,v 1.99 2025/09/20 13:53:36 mpi Exp $	*/
/*	$NetBSD: msdosfs_vfsops.c,v 1.48 1997/10/18 02:54:57 briggs Exp $	*/

/*-
 * Copyright (C) 1994, 1995, 1997 Wolfgang Solfrank.
 * Copyright (C) 1994, 1995, 1997 TooLs GmbH.
 * All rights reserved.
 * Original code by Paul Popelka (paulp@uts.amdahl.com) (see below).
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
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Written by Paul Popelka (paulp@uts.amdahl.com)
 *
 * You can do anything you want with this software, just don't say you wrote
 * it, and don't remove this notice.
 *
 * This software is provided "as is".
 *
 * The author supplies this software to be publicly redistributed on the
 * understanding that the author is not responsible for the correct
 * functioning of this software in any circumstances and is not liable for
 * any damages caused by this software.
 *
 * October 1992
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/namei.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#include <sys/vnode.h>
#include <sys/lock.h>
#include <sys/specdev.h> /* XXX */	/* defines v_rdev */
#include <sys/mount.h>
#include <sys/buf.h>
#include <sys/fcntl.h>
#include <sys/disklabel.h>
#include <sys/ioctl.h>
#include <sys/malloc.h>
#include <sys/dirent.h>
#include <sys/disk.h>
#include <sys/dkio.h>
#include <sys/stdint.h>

#include <msdosfs/bpb.h>
#include <msdosfs/bootsect.h>
#include <msdosfs/direntry.h>
#include <msdosfs/denode.h>
#include <msdosfs/msdosfsmount.h>
#include <msdosfs/fat.h>

int msdosfs_mount(struct mount *, const char *, void *, struct nameidata *,
		       struct proc *);
int msdosfs_start(struct mount *, int, struct proc *);
int msdosfs_unmount(struct mount *, int, struct proc *);
int msdosfs_root(struct mount *, struct vnode **);
int msdosfs_statfs(struct mount *, struct statfs *, struct proc *);
int msdosfs_sync(struct mount *, int, int, struct ucred *, struct proc *);
int msdosfs_fhtovp(struct mount *, struct fid *, struct vnode **);
int msdosfs_vptofh(struct vnode *, struct fid *);
int msdosfs_check_export(struct mount *mp, struct mbuf *nam,
			      int *extflagsp, struct ucred **credanonp);

int msdosfs_mountfs(struct vnode *, struct mount *, struct proc *,
			 struct msdosfs_args *);

int msdosfs_sync_vnode(struct vnode *, void *);

/*
 * mp - path - addr in user space of mount point (ie /usr or whatever)
 * data - addr in user space of mount params including the name of the block
 * special file to treat as a filesystem.
 */
int
msdosfs_mount(struct mount *mp, const char *path, void *data,
    struct nameidata *ndp, struct proc *p)
{
	struct vnode *devvp;	  /* vnode for blk device to mount */
	struct msdosfs_args *args = data; /* will hold data from mount request */
	/* msdosfs specific mount control block */
	struct msdosfsmount *pmp = NULL;
	char fname[MNAMELEN];
	char fspec[MNAMELEN];
	int error, flags;

	/*
	 * If updating, check whether changing from read-only to
	 * read/write; if there is no device name, that's all we do.
	 */
	if (mp->mnt_flag & MNT_UPDATE) {
		pmp = VFSTOMSDOSFS(mp);
		error = 0;
		if (!(pmp->pm_flags & MSDOSFSMNT_RONLY) &&
		    (mp->mnt_flag & MNT_RDONLY)) {
			mp->mnt_flag &= ~MNT_RDONLY;
			VFS_SYNC(mp, MNT_WAIT, 0, p->p_ucred, p);
			mp->mnt_flag |= MNT_RDONLY;

			flags = WRITECLOSE;
			if (mp->mnt_flag & MNT_FORCE)
				flags |= FORCECLOSE;
			error = vflush(mp, NULL, flags);
			if (!error) {
				int force = 0;

				pmp->pm_flags |= MSDOSFSMNT_RONLY;
				/* may be not supported, ignore error */
				VOP_IOCTL(pmp->pm_devvp, DIOCCACHESYNC,
				    &force, FWRITE, FSCRED, p);
			}
		}
		if (!error && (mp->mnt_flag & MNT_RELOAD))
			/* not yet implemented */
			error = EOPNOTSUPP;
		if (error)
			return (error);
		if ((pmp->pm_flags & MSDOSFSMNT_RONLY) &&
		    (mp->mnt_flag & MNT_WANTRDWR))
			pmp->pm_flags &= ~MSDOSFSMNT_RONLY;

		if (args && args->fspec == NULL) {
			/*
			 * Process export requests.
			 */
			return (vfs_export(mp, &pmp->pm_export,
			    &args->export_info));
		}
		if (args == NULL)
			return (0);
	}

	/*
	 * Not an update, or updating the name: look up the name
	 * and verify that it refers to a sensible block device.
	 */
	error = copyinstr(args->fspec, fspec, sizeof(fspec), NULL);
	if (error)
		goto error;

	if (disk_map(fspec, fname, sizeof(fname), DM_OPENBLCK) == -1)
		bcopy(fspec, fname, sizeof(fname));

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
		error = msdosfs_mountfs(devvp, mp, p, args);
	else {
		if (devvp != pmp->pm_devvp)
			error = EINVAL;	/* XXX needs translation */
		else
			vrele(devvp);
	}
	if (error)
		goto error_devvp;

	pmp = VFSTOMSDOSFS(mp);
	pmp->pm_gid = args->gid;
	pmp->pm_uid = args->uid;
	pmp->pm_mask = args->mask;
	pmp->pm_flags |= args->flags & MSDOSFSMNT_MNTOPT;

	if (pmp->pm_flags & MSDOSFSMNT_NOWIN95)
		pmp->pm_flags |= MSDOSFSMNT_SHORTNAME;
	else if (!(pmp->pm_flags &
	    (MSDOSFSMNT_SHORTNAME | MSDOSFSMNT_LONGNAME)))
	        pmp->pm_flags |= MSDOSFSMNT_LONGNAME;

	if (pmp->pm_flags & MSDOSFSMNT_LONGNAME)
		mp->mnt_stat.f_namemax = WIN_MAXLEN;
	else
		mp->mnt_stat.f_namemax = 12;

	bzero(mp->mnt_stat.f_mntonname, MNAMELEN);
	strlcpy(mp->mnt_stat.f_mntonname, path, MNAMELEN);
	bzero(mp->mnt_stat.f_mntfromname, MNAMELEN);
	strlcpy(mp->mnt_stat.f_mntfromname, fname, MNAMELEN);
	bzero(mp->mnt_stat.f_mntfromspec, MNAMELEN);
	strlcpy(mp->mnt_stat.f_mntfromspec, fspec, MNAMELEN);
	bcopy(args, &mp->mnt_stat.mount_info.msdosfs_args, sizeof(*args));

#ifdef MSDOSFS_DEBUG
	printf("msdosfs_mount(): mp %p, pmp %p, inusemap %p\n", mp,
	    pmp, pmp->pm_inusemap);
#endif

	return (0);

error_devvp:
	vrele(devvp);

error:
	return (error);
}

int
msdosfs_mountfs(struct vnode *devvp, struct mount *mp, struct proc *p,
    struct msdosfs_args *argp)
{
	struct msdosfsmount *pmp;
	struct buf *bp;
	dev_t dev = devvp->v_rdev;
	union bootsector *bsp;
	struct byte_bpb33 *b33;
	struct byte_bpb50 *b50;
	struct byte_bpb710 *b710;
	extern struct vnode *rootvp;
	u_int8_t SecPerClust;
	int	ronly, error, bmapsiz;
	uint32_t fat_max_clusters;

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
	error = vinvalbuf(devvp, V_SAVE, p->p_ucred, p, 0, INFSLP);
	VOP_UNLOCK(devvp);
	if (error)
		return (error);

	ronly = (mp->mnt_flag & MNT_RDONLY) != 0;
	error = VOP_OPEN(devvp, ronly ? FREAD : FREAD|FWRITE, FSCRED, p);
	if (error)
		return (error);

	bp  = NULL; /* both used in error_exit */
	pmp = NULL;

	/*
	 * Read the boot sector of the filesystem, and then check the
	 * boot signature.  If not a dos boot sector then error out.
	 */
	if ((error = bread(devvp, 0, 4096, &bp)) != 0)
		goto error_exit;
	bsp = (union bootsector *)bp->b_data;
	b33 = (struct byte_bpb33 *)bsp->bs33.bsBPB;
	b50 = (struct byte_bpb50 *)bsp->bs50.bsBPB;
	b710 = (struct byte_bpb710 *)bsp->bs710.bsBPB;

	pmp = malloc(sizeof *pmp, M_MSDOSFSMNT, M_WAITOK | M_ZERO);
	pmp->pm_mountp = mp;

	/*
	 * Compute several useful quantities from the bpb in the
	 * bootsector.  Copy in the dos 5 variant of the bpb then fix up
	 * the fields that are different between dos 5 and dos 3.3.
	 */
	SecPerClust = b50->bpbSecPerClust;
	pmp->pm_BytesPerSec = getushort(b50->bpbBytesPerSec);
	pmp->pm_ResSectors = getushort(b50->bpbResSectors);
	pmp->pm_FATs = b50->bpbFATs;
	pmp->pm_RootDirEnts = getushort(b50->bpbRootDirEnts);
	pmp->pm_Sectors = getushort(b50->bpbSectors);
	pmp->pm_FATsecs = getushort(b50->bpbFATsecs);
	pmp->pm_SecPerTrack = getushort(b50->bpbSecPerTrack);
	pmp->pm_Heads = getushort(b50->bpbHeads);
	pmp->pm_Media = b50->bpbMedia;

	/* Determine the number of DEV_BSIZE blocks in a MSDOSFS sector */
	pmp->pm_BlkPerSec = pmp->pm_BytesPerSec / DEV_BSIZE;

	if (!pmp->pm_BytesPerSec || !SecPerClust) {
		error = EINVAL;
		goto error_exit;
	}

	if (pmp->pm_Sectors == 0) {
		pmp->pm_HiddenSects = getulong(b50->bpbHiddenSecs);
		pmp->pm_HugeSectors = getulong(b50->bpbHugeSectors);
	} else {
		pmp->pm_HiddenSects = getushort(b33->bpbHiddenSecs);
		pmp->pm_HugeSectors = pmp->pm_Sectors;
	}

	if (pmp->pm_RootDirEnts == 0) {
		if (pmp->pm_Sectors || pmp->pm_FATsecs ||
		    getushort(b710->bpbFSVers)) {
		        error = EINVAL;
			goto error_exit;
		}
		pmp->pm_fatmask = FAT32_MASK;
		pmp->pm_fatmult = 4;
		pmp->pm_fatdiv = 1;
		pmp->pm_FATsecs = getulong(b710->bpbBigFATsecs);
		if (getushort(b710->bpbExtFlags) & FATMIRROR)
		        pmp->pm_curfat = getushort(b710->bpbExtFlags) & FATNUM;
		else
		        pmp->pm_flags |= MSDOSFS_FATMIRROR;
	} else
	        pmp->pm_flags |= MSDOSFS_FATMIRROR;

	/*
	 * More sanity checks:
	 *	MSDOSFS sectors per cluster: >0 && power of 2
	 *	MSDOSFS sector size: >= DEV_BSIZE && power of 2
	 *	HUGE sector count: >0
	 *	FAT sectors: >0
	 */
	if ((SecPerClust == 0) || (SecPerClust & (SecPerClust - 1)) ||
	    (pmp->pm_BytesPerSec < DEV_BSIZE) ||
	    (pmp->pm_BytesPerSec & (pmp->pm_BytesPerSec - 1)) ||
	    (pmp->pm_HugeSectors == 0) || (pmp->pm_FATsecs == 0) ||
	    (SecPerClust * pmp->pm_BlkPerSec > MAXBSIZE / DEV_BSIZE)) {
		error = EINVAL;
		goto error_exit;
	}

	pmp->pm_HugeSectors *= pmp->pm_BlkPerSec;
	pmp->pm_HiddenSects *= pmp->pm_BlkPerSec;
	pmp->pm_FATsecs *= pmp->pm_BlkPerSec;
	pmp->pm_fatblk = pmp->pm_ResSectors * pmp->pm_BlkPerSec;
	SecPerClust *= pmp->pm_BlkPerSec;

	if (FAT32(pmp)) {
	        pmp->pm_rootdirblk = getulong(b710->bpbRootClust);
		pmp->pm_firstcluster = pmp->pm_fatblk
		        + (pmp->pm_FATs * pmp->pm_FATsecs);
		pmp->pm_fsinfo = getushort(b710->bpbFSInfo) * pmp->pm_BlkPerSec;
	} else {
	        pmp->pm_rootdirblk = pmp->pm_fatblk +
		        (pmp->pm_FATs * pmp->pm_FATsecs);
		pmp->pm_rootdirsize = (pmp->pm_RootDirEnts * sizeof(struct direntry)
				       + DEV_BSIZE - 1) / DEV_BSIZE;
		pmp->pm_firstcluster = pmp->pm_rootdirblk + pmp->pm_rootdirsize;
	}

	pmp->pm_nmbrofclusters = (pmp->pm_HugeSectors - pmp->pm_firstcluster) /
	    SecPerClust;
	pmp->pm_maxcluster = pmp->pm_nmbrofclusters + 1;
	pmp->pm_fatsize = pmp->pm_FATsecs * DEV_BSIZE;

	if (pmp->pm_fatmask == 0) {
		if (pmp->pm_maxcluster
		    <= ((CLUST_RSRVD - CLUST_FIRST) & FAT12_MASK)) {
			/*
			 * This will usually be a floppy disk. This size makes
			 * sure that one fat entry will not be split across
			 * multiple blocks.
			 */
			pmp->pm_fatmask = FAT12_MASK;
			pmp->pm_fatmult = 3;
			pmp->pm_fatdiv = 2;
		} else {
			pmp->pm_fatmask = FAT16_MASK;
			pmp->pm_fatmult = 2;
			pmp->pm_fatdiv = 1;
		}
	}
	if (FAT12(pmp))
		pmp->pm_fatblocksize = 3 * pmp->pm_BytesPerSec;
	else
		pmp->pm_fatblocksize = MAXBSIZE;

	/*
	 * We now have the number of sectors in each FAT, so can work
	 * out how many clusters can be represented in a FAT.  Let's
	 * make sure the file system doesn't claim to have more clusters
	 * than this.
	 *
	 * We perform the calculation like we do to avoid integer overflow.
	 *
	 * This will give us a count of clusters.  They are numbered
	 * from 0, so the max cluster value is one less than the value
	 * we end up with.
	 */
	fat_max_clusters = pmp->pm_fatsize / pmp->pm_fatmult;
	fat_max_clusters *= pmp->pm_fatdiv;
	if (pmp->pm_maxcluster >= fat_max_clusters) {
#ifndef SMALL_KERNEL
		printf("msdosfs: reducing max cluster to %d from %d "
		    "due to FAT size\n", fat_max_clusters - 1,
		    pmp->pm_maxcluster);
#endif
		pmp->pm_maxcluster = fat_max_clusters - 1;
	}

	pmp->pm_fatblocksec = pmp->pm_fatblocksize / DEV_BSIZE;
	pmp->pm_bnshift = ffs(DEV_BSIZE) - 1;

	/*
	 * Compute mask and shift value for isolating cluster relative byte
	 * offsets and cluster numbers from a file offset.
	 */
	pmp->pm_bpcluster = SecPerClust * DEV_BSIZE;
	pmp->pm_crbomask = pmp->pm_bpcluster - 1;
	pmp->pm_cnshift = ffs(pmp->pm_bpcluster) - 1;

	/*
	 * Check for valid cluster size
	 * must be a power of 2
	 */
	if (pmp->pm_bpcluster ^ (1 << pmp->pm_cnshift)) {
		error = EINVAL;
		goto error_exit;
	}

	/*
	 * Release the bootsector buffer.
	 */
	brelse(bp);
	bp = NULL;

	/*
	 * Check FSInfo
	 */
	if (pmp->pm_fsinfo) {
	        struct fsinfo *fp;

		if ((error = bread(devvp, pmp->pm_fsinfo, fsi_size(pmp),
		    &bp)) != 0)
		        goto error_exit;
		fp = (struct fsinfo *)bp->b_data;
		if (!bcmp(fp->fsisig1, "RRaA", 4)
		    && !bcmp(fp->fsisig2, "rrAa", 4)
		    && !bcmp(fp->fsisig3, "\0\0\125\252", 4)
		    && !bcmp(fp->fsisig4, "\0\0\125\252", 4))
		        /* Valid FSInfo. */
			;
		else
		        pmp->pm_fsinfo = 0;
		/* XXX make sure this tiny buf doesn't come back in fillinusemap! */
		SET(bp->b_flags, B_INVAL);
		brelse(bp);
		bp = NULL;
	}

	/*
	 * Check and validate (or perhaps invalidate?) the fsinfo structure? XXX
	 */

	/*
	 * Allocate memory for the bitmap of allocated clusters, and then
	 * fill it in.
	 */
	bmapsiz = howmany(pmp->pm_maxcluster + 1, N_INUSEBITS);
	if (bmapsiz == 0 || SIZE_MAX / bmapsiz < sizeof(*pmp->pm_inusemap)) {
		/* detect multiplicative integer overflow */
		error = EINVAL;
		goto error_exit;
	}
	pmp->pm_inusemap = mallocarray(bmapsiz, sizeof(*pmp->pm_inusemap),
	    M_MSDOSFSFAT, M_WAITOK | M_CANFAIL);
	if (pmp->pm_inusemap == NULL) {
		error = EINVAL;
		goto error_exit;
	}

	/*
	 * fillinusemap() needs pm_devvp.
	 */
	pmp->pm_dev = dev;
	pmp->pm_devvp = devvp;

	/*
	 * Have the inuse map filled in.
	 */
	if ((error = fillinusemap(pmp)) != 0)
		goto error_exit;

	/*
	 * If they want fat updates to be synchronous then let them suffer
	 * the performance degradation in exchange for the on disk copy of
	 * the fat being correct just about all the time.  I suppose this
	 * would be a good thing to turn on if the kernel is still flakey.
	 */
	if (mp->mnt_flag & MNT_SYNCHRONOUS)
		pmp->pm_flags |= MSDOSFSMNT_WAITONFAT;

	/*
	 * Finish up.
	 */
	if (ronly)
		pmp->pm_flags |= MSDOSFSMNT_RONLY;
	else
		pmp->pm_fmod = 1;
	mp->mnt_data = pmp;
        mp->mnt_stat.f_fsid.val[0] = (long)dev;
        mp->mnt_stat.f_fsid.val[1] = mp->mnt_vfc->vfc_typenum;
#ifdef QUOTA
	/*
	 * If we ever do quotas for DOS filesystems this would be a place
	 * to fill in the info in the msdosfsmount structure. You dolt,
	 * quotas on dos filesystems make no sense because files have no
	 * owners on dos filesystems. of course there is some empty space
	 * in the directory entry where we could put uid's and gid's.
	 */
#endif
	devvp->v_specmountpoint = mp;

	return (0);

error_exit:
	if (devvp->v_specinfo)
		devvp->v_specmountpoint = NULL;
	if (bp)
		brelse(bp);

	vn_lock(devvp, LK_EXCLUSIVE|LK_RETRY);
	(void) VOP_CLOSE(devvp, ronly ? FREAD : FREAD|FWRITE, NOCRED, p);
	VOP_UNLOCK(devvp);

	if (pmp) {
		if (pmp->pm_inusemap)
			free(pmp->pm_inusemap, M_MSDOSFSFAT, 0);
		free(pmp, M_MSDOSFSMNT, 0);
		mp->mnt_data = NULL;
	}
	return (error);
}

int
msdosfs_start(struct mount *mp, int flags, struct proc *p)
{

	return (0);
}

/*
 * Unmount the filesystem described by mp.
 */
int
msdosfs_unmount(struct mount *mp, int mntflags,struct proc *p)
{
	struct msdosfsmount *pmp;
	int error, flags;
	struct vnode *vp;

	flags = 0;
	if (mntflags & MNT_FORCE)
		flags |= FORCECLOSE;
	if ((error = vflush(mp, NULL, flags)) != 0)
		return (error);
	pmp = VFSTOMSDOSFS(mp);
	pmp->pm_devvp->v_specmountpoint = NULL;
	vp = pmp->pm_devvp;
#if defined(MSDOSFS_DEBUG) && (defined(DEBUG) || defined(DIAGNOSTIC))
	vprint("msdosfs_umount(): just before calling VOP_CLOSE()\n", vp);
#endif
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	(void)VOP_CLOSE(vp,
	    pmp->pm_flags & MSDOSFSMNT_RONLY ? FREAD : FREAD|FWRITE, NOCRED, p);
	vput(vp);
	free(pmp->pm_inusemap, M_MSDOSFSFAT, 0);
	free(pmp, M_MSDOSFSMNT, 0);
	mp->mnt_data = NULL;
	mp->mnt_flag &= ~MNT_LOCAL;
	return (0);
}

int
msdosfs_root(struct mount *mp, struct vnode **vpp)
{
	struct msdosfsmount *pmp = VFSTOMSDOSFS(mp);
	struct denode *ndep;
	int error;

	if ((error = deget(pmp, MSDOSFSROOT, MSDOSFSROOT_OFS, &ndep)) != 0)
		return (error);

#ifdef MSDOSFS_DEBUG
	printf("msdosfs_root(); mp %p, pmp %p, ndep %p, vp %p\n",
	    mp, pmp, ndep, DETOV(ndep));
#endif

	*vpp = DETOV(ndep);
	return (0);
}

int
msdosfs_statfs(struct mount *mp, struct statfs *sbp, struct proc *p)
{
	struct msdosfsmount *pmp;

	pmp = VFSTOMSDOSFS(mp);
	sbp->f_bsize = pmp->pm_bpcluster;
	sbp->f_iosize = pmp->pm_bpcluster;
	sbp->f_blocks = pmp->pm_nmbrofclusters;
	sbp->f_bfree = pmp->pm_freeclustercount;
	sbp->f_bavail = pmp->pm_freeclustercount;
	sbp->f_files = pmp->pm_RootDirEnts;			/* XXX */
	sbp->f_ffree = sbp->f_favail = 0;	/* what to put in here? */
	copy_statfs_info(sbp, mp);

	return (0);
}


struct msdosfs_sync_arg {
	struct proc *p;
	struct ucred *cred;
	int allerror;
	int waitfor;
};

int
msdosfs_sync_vnode(struct vnode *vp, void *arg)
{
	struct msdosfs_sync_arg *msa = arg;
	struct denode *dep;
	int error;
	int s, skip = 0;

	dep = VTODE(vp);
	s = splbio();
	if (vp->v_type == VNON ||
	    ((dep->de_flag & (DE_ACCESS | DE_CREATE | DE_UPDATE | DE_MODIFIED)) == 0
	      && LIST_EMPTY(&vp->v_dirtyblkhd)) ||
	    msa->waitfor == MNT_LAZY) {
		skip = 1;
	}
	splx(s);

	if (skip)
		return (0);

	if (vget(vp, LK_EXCLUSIVE | LK_NOWAIT))
		return (0);

	if ((error = VOP_FSYNC(vp, msa->cred, msa->waitfor, msa->p)) != 0)
		msa->allerror = error;
	VOP_UNLOCK(vp);
	vrele(vp);

	return (0);
}


int
msdosfs_sync(struct mount *mp, int waitfor, int stall, struct ucred *cred,
    struct proc *p)
{
	struct msdosfsmount *pmp = VFSTOMSDOSFS(mp);
	struct msdosfs_sync_arg msa;
	int error;

	msa.allerror = 0;
	msa.p = p;
	msa.cred = cred;
	msa.waitfor = waitfor;

	/*
	 * If we ever switch to not updating all of the fats all the time,
	 * this would be the place to update them from the first one.
	 */
	if (pmp->pm_fmod != 0) {
		if (pmp->pm_flags & MSDOSFSMNT_RONLY)
			panic("msdosfs_sync: rofs mod");
		else {
			/* update fats here */
		}
	}
	/*
	 * Write back each (modified) denode.
	 */
	vfs_mount_foreach_vnode(mp, msdosfs_sync_vnode, &msa);

	/*
	 * Force stale file system control information to be flushed.
	 */
	if (waitfor != MNT_LAZY) {
		vn_lock(pmp->pm_devvp, LK_EXCLUSIVE | LK_RETRY);
		if ((error = VOP_FSYNC(pmp->pm_devvp, cred, waitfor, p)) != 0)
			msa.allerror = error;
		VOP_UNLOCK(pmp->pm_devvp);
	}

	return (msa.allerror);
}

int
msdosfs_fhtovp(struct mount *mp, struct fid *fhp, struct vnode **vpp)
{
	struct msdosfsmount *pmp = VFSTOMSDOSFS(mp);
	struct defid *defhp = (struct defid *) fhp;
	struct denode *dep;
	int error;

	error = deget(pmp, defhp->defid_dirclust, defhp->defid_dirofs, &dep);
	if (error) {
		*vpp = NULL;
		return (error);
	}
	*vpp = DETOV(dep);
	return (0);
}

int
msdosfs_vptofh(struct vnode *vp, struct fid *fhp)
{
	struct denode *dep;
	struct defid *defhp;

	dep = VTODE(vp);
	defhp = (struct defid *)fhp;
	defhp->defid_len = sizeof(struct defid);
	defhp->defid_dirclust = dep->de_dirclust;
	defhp->defid_dirofs = dep->de_diroffset;
	/* defhp->defid_gen = dep->de_gen; */
	return (0);
}

int
msdosfs_check_export(struct mount *mp, struct mbuf *nam, int *exflagsp,
    struct ucred **credanonp)
{
	struct netcred *np;
	struct msdosfsmount *pmp = VFSTOMSDOSFS(mp);

	/*
	 * Get the export permission structure for this <mp, client> tuple.
	 */
	np = vfs_export_lookup(mp, &pmp->pm_export, nam);
	if (np == NULL)
		return (EACCES);

	*exflagsp = np->netc_exflags;
	*credanonp = &np->netc_anon;
	return (0);
}

const struct vfsops msdosfs_vfsops = {
	.vfs_mount	= msdosfs_mount,
	.vfs_start	= msdosfs_start,
	.vfs_unmount	= msdosfs_unmount,
	.vfs_root	= msdosfs_root,
	.vfs_quotactl	= (void *)eopnotsupp,
	.vfs_statfs	= msdosfs_statfs,
	.vfs_sync	= msdosfs_sync,
	.vfs_vget	= (void *)eopnotsupp,
	.vfs_fhtovp	= msdosfs_fhtovp,
	.vfs_vptofh	= msdosfs_vptofh,
	.vfs_init	= msdosfs_init,
	.vfs_sysctl	= (void *)eopnotsupp,
	.vfs_checkexp	= msdosfs_check_export,
};
