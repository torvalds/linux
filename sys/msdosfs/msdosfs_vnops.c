/*	$OpenBSD: msdosfs_vnops.c,v 1.143 2024/10/18 05:52:32 miod Exp $	*/
/*	$NetBSD: msdosfs_vnops.c,v 1.63 1997/10/17 11:24:19 ws Exp $	*/

/*-
 * Copyright (C) 2005 Thomas Wang.
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
#include <sys/resourcevar.h>	/* defines plimit structure in proc struct */
#include <sys/fcntl.h>		/* define FWRITE ... */
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/lock.h>
#include <sys/signalvar.h>
#include <sys/specdev.h> /* XXX */	/* defines v_rdev */
#include <sys/malloc.h>
#include <sys/pool.h>
#include <sys/dirent.h>		/* defines dirent structure */
#include <sys/lockf.h>
#include <sys/unistd.h>

#include <msdosfs/bpb.h>
#include <msdosfs/direntry.h>
#include <msdosfs/denode.h>
#include <msdosfs/msdosfsmount.h>
#include <msdosfs/fat.h>

static uint32_t fileidhash(uint64_t);

int msdosfs_bmaparray(struct vnode *, uint32_t, daddr_t *, int *);
int msdosfs_kqfilter(void *);
int filt_msdosfsread(struct knote *, long);
int filt_msdosfswrite(struct knote *, long);
int filt_msdosfsvnode(struct knote *, long);
void filt_msdosfsdetach(struct knote *);

/*
 * Some general notes:
 *
 * In the ufs filesystem the inodes, superblocks, and indirect blocks are
 * read/written using the vnode for the filesystem. Blocks that represent
 * the contents of a file are read/written using the vnode for the file
 * (including directories when they are read/written as files). This
 * presents problems for the dos filesystem because data that should be in
 * an inode (if dos had them) resides in the directory itself.  Since we
 * must update directory entries without the benefit of having the vnode
 * for the directory we must use the vnode for the filesystem.  This means
 * that when a directory is actually read/written (via read, write, or
 * readdir, or seek) we must use the vnode for the filesystem instead of
 * the vnode for the directory as would happen in ufs. This is to insure we
 * retrieve the correct block from the buffer cache since the hash value is
 * based upon the vnode address and the desired block number.
 */

/*
 * Create a regular file. On entry the directory to contain the file being
 * created is locked.  We must release before we return. We must also free
 * the pathname buffer pointed at by cnp->cn_pnbuf, always on error, or
 * only if the SAVESTART bit in cn_flags is clear on success.
 */
int
msdosfs_create(void *v)
{
	struct vop_create_args *ap = v;
	struct componentname *cnp = ap->a_cnp;
	struct denode ndirent;
	struct denode *dep;
	struct denode *pdep = VTODE(ap->a_dvp);
	int error;
	struct timespec ts;

#ifdef MSDOSFS_DEBUG
	printf("msdosfs_create(cnp %p, vap %p\n", cnp, ap->a_vap);
#endif

	/*
	 * If this is the root directory and there is no space left we
	 * can't do anything.  This is because the root directory can not
	 * change size.
	 */
	if (pdep->de_StartCluster == MSDOSFSROOT
	    && pdep->de_fndoffset >= pdep->de_FileSize) {
		error = ENOSPC;
		goto bad;
	}

	/*
	 * Create a directory entry for the file, then call createde() to
	 * have it installed. NOTE: DOS files are always executable.  We
	 * use the absence of the owner write bit to make the file
	 * readonly.
	 */
#ifdef DIAGNOSTIC
	if ((cnp->cn_flags & HASBUF) == 0)
		panic("msdosfs_create: no name");
#endif
	bzero(&ndirent, sizeof(ndirent));
	if ((error = uniqdosname(pdep, cnp, ndirent.de_Name)) != 0)
		goto bad;

	ndirent.de_Attributes = (ap->a_vap->va_mode & VWRITE) ?
				ATTR_ARCHIVE : ATTR_ARCHIVE | ATTR_READONLY;
	ndirent.de_StartCluster = 0;
	ndirent.de_FileSize = 0;
	ndirent.de_dev = pdep->de_dev;
	ndirent.de_devvp = pdep->de_devvp;
	ndirent.de_pmp = pdep->de_pmp;
	ndirent.de_flag = DE_ACCESS | DE_CREATE | DE_UPDATE;
	getnanotime(&ts);
	DETIMES(&ndirent, &ts, &ts, &ts);
	if ((error = createde(&ndirent, pdep, &dep, cnp)) != 0)
		goto bad;
	if ((cnp->cn_flags & SAVESTART) == 0)
		pool_put(&namei_pool, cnp->cn_pnbuf);
	VN_KNOTE(ap->a_dvp, NOTE_WRITE);
	*ap->a_vpp = DETOV(dep);
	return (0);

bad:
	pool_put(&namei_pool, cnp->cn_pnbuf);
	return (error);
}

int
msdosfs_mknod(void *v)
{
	struct vop_mknod_args *ap = v;

	pool_put(&namei_pool, ap->a_cnp->cn_pnbuf);
	VN_KNOTE(ap->a_dvp, NOTE_WRITE);
	return (EINVAL);
}

int
msdosfs_open(void *v)
{
	return (0);
}

int
msdosfs_close(void *v)
{
	struct vop_close_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct denode *dep = VTODE(vp);
	struct timespec ts;

	if (vp->v_usecount > 1 && !VOP_ISLOCKED(vp)) {
		getnanotime(&ts);
		DETIMES(dep, &ts, &ts, &ts);
	}
	return (0);
}

int
msdosfs_access(void *v)
{
	struct vop_access_args *ap = v;
	struct denode *dep = VTODE(ap->a_vp);
	struct msdosfsmount *pmp = dep->de_pmp;
	mode_t dosmode;

	dosmode = (S_IRUSR | S_IRGRP | S_IROTH);
	if ((dep->de_Attributes & ATTR_READONLY) == 0)
		dosmode |= (S_IWUSR | S_IWGRP | S_IWOTH);
	if (dep->de_Attributes & ATTR_DIRECTORY)
		dosmode |= (S_IXUSR | S_IXGRP | S_IXOTH);
	dosmode &= pmp->pm_mask;

	return (vaccess(ap->a_vp->v_type, dosmode, pmp->pm_uid, pmp->pm_gid,
	    ap->a_mode, ap->a_cred));
}

int
msdosfs_getattr(void *v)
{
	struct vop_getattr_args *ap = v;
	struct denode *dep = VTODE(ap->a_vp);
	struct msdosfsmount *pmp = dep->de_pmp;
	struct vattr *vap = ap->a_vap;
	struct timespec ts;
	uint32_t fileid;

	getnanotime(&ts);
	DETIMES(dep, &ts, &ts, &ts);
	vap->va_fsid = dep->de_dev;

	/*
	 * The following computation of the fileid must be the same as
	 * that used in msdosfs_readdir() to compute d_fileno. If not,
	 * pwd doesn't work.
	 *
	 * We now use the starting cluster number as the fileid/fileno.
	 * This works for both files and directories (including the root
	 * directory, on FAT32).  Even on FAT32, this will at most be a
	 * 28-bit number, as the high 4 bits of FAT32 cluster numbers
	 * are reserved.
	 *
	 * However, we do need to do something for 0-length files, which
	 * will not have a starting cluster number.
	 *
	 * These files cannot be directories, since (except for /, which
	 * is special-cased anyway) directories contain entries for . and
	 * .., so must have non-zero length.
	 *
	 * In this case, we just create a non-cryptographic hash of the
	 * original fileid calculation, and set the top bit.
	 *
	 * This algorithm has the benefit that all directories, and all
	 * non-zero-length files, will have fileids that are persistent
	 * across mounts and reboots, and that cannot collide (as long
	 * as the filesystem is not corrupt).  Zero-length files will
	 * have fileids that are persistent, but that may collide.  We
	 * will just have to live with that.
	 */
	fileid = dep->de_StartCluster;

	if (dep->de_Attributes & ATTR_DIRECTORY) {
		/* Special-case root */
		if (dep->de_StartCluster == MSDOSFSROOT)
			fileid = FAT32(pmp) ? pmp->pm_rootdirblk : 1;
	} else {
		if (dep->de_FileSize == 0) {
			uint32_t dirsperblk;
			uint64_t fileid64;

			dirsperblk = pmp->pm_BytesPerSec /
			    sizeof(struct direntry);

			fileid64 = (dep->de_dirclust == MSDOSFSROOT) ?
			    roottobn(pmp, 0) : cntobn(pmp, dep->de_dirclust);
			fileid64 *= dirsperblk;
			fileid64 += dep->de_diroffset / sizeof(struct direntry);

			fileid = fileidhash(fileid64);
		}
	}

	vap->va_fileid = fileid;
	vap->va_mode = (S_IRUSR|S_IRGRP|S_IROTH);
	if ((dep->de_Attributes & ATTR_READONLY) == 0)
		vap->va_mode |= (S_IWUSR|S_IWGRP|S_IWOTH);
	if (dep->de_Attributes & ATTR_DIRECTORY) {
		vap->va_mode |= S_IFDIR;
		vap->va_mode |= (vap->va_mode & S_IRUSR) ? S_IXUSR : 0;
		vap->va_mode |= (vap->va_mode & S_IRGRP) ? S_IXGRP : 0;
		vap->va_mode |= (vap->va_mode & S_IROTH) ? S_IXOTH : 0;
	}
	vap->va_mode &= dep->de_pmp->pm_mask;
	vap->va_nlink = 1;
	vap->va_gid = dep->de_pmp->pm_gid;
	vap->va_uid = dep->de_pmp->pm_uid;
	vap->va_rdev = 0;
	vap->va_size = dep->de_FileSize;
	dos2unixtime(dep->de_MDate, dep->de_MTime, 0, &vap->va_mtime);
	if (dep->de_pmp->pm_flags & MSDOSFSMNT_LONGNAME) {
		dos2unixtime(dep->de_ADate, 0, 0, &vap->va_atime);
		dos2unixtime(dep->de_CDate, dep->de_CTime, dep->de_CTimeHundredth, &vap->va_ctime);
	} else {
		vap->va_atime = vap->va_mtime;
		vap->va_ctime = vap->va_mtime;
	}
	vap->va_flags = 0;
	if ((dep->de_Attributes & ATTR_ARCHIVE) == 0)
		vap->va_flags |= SF_ARCHIVED;
	vap->va_gen = 0;
	vap->va_blocksize = dep->de_pmp->pm_bpcluster;
	vap->va_bytes = (dep->de_FileSize + dep->de_pmp->pm_crbomask) &
				~(dep->de_pmp->pm_crbomask);
	vap->va_type = ap->a_vp->v_type;
	return (0);
}

int
msdosfs_setattr(void *v)
{
	struct vop_setattr_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct denode *dep = VTODE(ap->a_vp);
	struct msdosfsmount *pmp = dep->de_pmp;
	struct vattr *vap = ap->a_vap;
	struct ucred *cred = ap->a_cred;
	int error = 0;

#ifdef MSDOSFS_DEBUG
	printf("msdosfs_setattr(): vp %p, vap %p, cred %p, p %p\n",
	    ap->a_vp, vap, cred, ap->a_p);
#endif
	if ((vap->va_type != VNON) || (vap->va_nlink != VNOVAL) ||
	    (vap->va_fsid != VNOVAL) || (vap->va_fileid != VNOVAL) ||
	    (vap->va_blocksize != VNOVAL) || (vap->va_rdev != VNOVAL) ||
	    (vap->va_bytes != VNOVAL) || (vap->va_gen != VNOVAL)) {
#ifdef MSDOSFS_DEBUG
		printf("msdosfs_setattr(): returning EINVAL\n");
		printf("    va_type %d, va_nlink %x, va_fsid %ld, "
		    "va_fileid %llx\n", vap->va_type, vap->va_nlink,
		    vap->va_fsid, (unsigned long long)vap->va_fileid);
		printf("    va_blocksize %lx, va_rdev %x, va_bytes %llx, "
		    "va_gen %lx\n", vap->va_blocksize,
		    (unsigned int)vap->va_rdev,
		    (unsigned long long)vap->va_bytes, vap->va_gen);
#endif
		return (EINVAL);
	}
	if (vap->va_flags != VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			return (EINVAL);
		if (cred->cr_uid != pmp->pm_uid) {
			error = suser_ucred(cred);
			if (error)
				return (error);
		}
		/*
		 * We are very inconsistent about handling unsupported
		 * attributes.  We ignored the access time and the
		 * read and execute bits.  We were strict for the other
		 * attributes.
		 *
		 * Here we are strict, stricter than ufs in not allowing
		 * users to attempt to set SF_SETTABLE bits or anyone to
		 * set unsupported bits.  However, we ignore attempts to
		 * set ATTR_ARCHIVE for directories `cp -pr' from a more
		 * sensible filesystem attempts it a lot.
		 */
		if (vap->va_flags & SF_SETTABLE) {
			error = suser_ucred(cred);
			if (error)
				return (error);
		}
		if (vap->va_flags & ~SF_ARCHIVED)
			return EOPNOTSUPP;
		if (vap->va_flags & SF_ARCHIVED)
			dep->de_Attributes &= ~ATTR_ARCHIVE;
		else if (!(dep->de_Attributes & ATTR_DIRECTORY))
			dep->de_Attributes |= ATTR_ARCHIVE;
		dep->de_flag |= DE_MODIFIED;
	}

	if (vap->va_uid != (uid_t)VNOVAL || vap->va_gid != (gid_t)VNOVAL) {
		uid_t uid;
		gid_t gid;

		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			return (EINVAL);
		uid = vap->va_uid;
		if (uid == (uid_t)VNOVAL)
			uid = pmp->pm_uid;
		gid = vap->va_gid;
		if (gid == (gid_t)VNOVAL)
			gid = pmp->pm_gid;
		if (cred->cr_uid != pmp->pm_uid || uid != pmp->pm_uid ||
		    (gid != pmp->pm_gid && !groupmember(gid, cred))) {
			error = suser_ucred(cred);
			if (error)
				return (error);
		}
		if (uid != pmp->pm_uid || gid != pmp->pm_gid)
			return EINVAL;
	}

	if (vap->va_size != VNOVAL) {
		switch (vp->v_type) {
		case VDIR:
			return (EISDIR);
		case VREG:
			/*
			 * Truncation is only supported for regular files,
			 * Disallow it if the filesystem is read-only.
			 */
			if (vp->v_mount->mnt_flag & MNT_RDONLY)
				return (EINVAL);
			break;
		default:
			/*
			 * According to POSIX, the result is unspecified
			 * for file types other than regular files,
			 * directories and shared memory objects.  We
			 * don't support any file types except regular
			 * files and directories in this file system, so
			 * this (default) case is unreachable and can do
			 * anything.  Keep falling through to detrunc()
			 * for now.
			 */
			break;
		}
		error = detrunc(dep, vap->va_size, 0, cred, ap->a_p);
		if (error)
			return error;
	}
	if ((vap->va_vaflags & VA_UTIMES_CHANGE) ||
	    vap->va_atime.tv_nsec != VNOVAL ||
	    vap->va_mtime.tv_nsec != VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			return (EINVAL);
		if (cred->cr_uid != pmp->pm_uid &&
		    (error = suser_ucred(cred)) &&
		    ((vap->va_vaflags & VA_UTIMES_NULL) == 0 ||
		    (error = VOP_ACCESS(ap->a_vp, VWRITE, cred, ap->a_p))))
			return (error);
		if (vp->v_type != VDIR) {
			if ((pmp->pm_flags & MSDOSFSMNT_NOWIN95) == 0 &&
			    vap->va_atime.tv_nsec != VNOVAL) {
				dep->de_flag &= ~DE_ACCESS;
				unix2dostime(&vap->va_atime, &dep->de_ADate,
				    NULL, NULL);
			}
			if (vap->va_mtime.tv_nsec != VNOVAL) {
				dep->de_flag &= ~DE_UPDATE;
				unix2dostime(&vap->va_mtime, &dep->de_MDate,
				    &dep->de_MTime, NULL);
			}
			dep->de_Attributes |= ATTR_ARCHIVE;
			dep->de_flag |= DE_MODIFIED;
		}
	}
	/*
	 * DOS files only have the ability to have their writability
	 * attribute set, so we use the owner write bit to set the readonly
	 * attribute.
	 */
	if (vap->va_mode != (mode_t)VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			return (EINVAL);
		if (cred->cr_uid != pmp->pm_uid) {
			error = suser_ucred(cred);
			if (error)
				return (error);
		}
		if (vp->v_type != VDIR) {
			/* We ignore the read and execute bits. */
			if (vap->va_mode & VWRITE)
				dep->de_Attributes &= ~ATTR_READONLY;
			else
				dep->de_Attributes |= ATTR_READONLY;
			dep->de_Attributes |= ATTR_ARCHIVE;
			dep->de_flag |= DE_MODIFIED;
		}
	}
	VN_KNOTE(ap->a_vp, NOTE_ATTRIB);
	return (deupdat(dep, 1));
}

int
msdosfs_read(void *v)
{
	struct vop_read_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct denode *dep = VTODE(vp);
	struct msdosfsmount *pmp = dep->de_pmp;
	struct uio *uio = ap->a_uio;
	int isadir, error = 0;
	uint32_t n, diff, size, on;
	struct buf *bp;
	uint32_t cn;
	daddr_t bn;

	/*
	 * If they didn't ask for any data, then we are done.
	 */
	if (uio->uio_resid == 0)
		return (0);
	if (uio->uio_offset < 0)
		return (EINVAL);

	isadir = dep->de_Attributes & ATTR_DIRECTORY;
	do {
		if (uio->uio_offset >= dep->de_FileSize)
			return (0);

		cn = de_cluster(pmp, uio->uio_offset);
		size = pmp->pm_bpcluster;
		on = uio->uio_offset & pmp->pm_crbomask;
		n = ulmin(pmp->pm_bpcluster - on, uio->uio_resid);

		/*
		 * de_FileSize is uint32_t, and we know that uio_offset <
		 * de_FileSize, so uio->uio_offset < 2^32.  Therefore
		 * the cast to uint32_t on the next line is safe.
		 */
		diff = dep->de_FileSize - (uint32_t)uio->uio_offset;
		if (diff < n)
			n = diff;

		/*
		 * If we are operating on a directory file then be sure to
		 * do i/o with the vnode for the filesystem instead of the
		 * vnode for the directory.
		 */
		if (isadir) {
			/* convert cluster # to block # */
			error = pcbmap(dep, cn, &bn, 0, &size);
			if (error)
				return (error);
			error = bread(pmp->pm_devvp, bn, size, &bp);
		} else {
			if (de_cn2off(pmp, cn + 1) >= dep->de_FileSize)
				error = bread(vp, cn, size, &bp);
			else
				error = bread_cluster(vp, cn, size, &bp);
		}
		n = min(n, pmp->pm_bpcluster - bp->b_resid);
		if (error) {
			brelse(bp);
			return (error);
		}
		error = uiomove(bp->b_data + on, n, uio);
		brelse(bp);
	} while (error == 0 && uio->uio_resid > 0 && n != 0);
	if (!isadir && !(vp->v_mount->mnt_flag & MNT_NOATIME))
		dep->de_flag |= DE_ACCESS;
	return (error);
}

/*
 * Write data to a file or directory.
 */
int
msdosfs_write(void *v)
{
	struct vop_write_args *ap = v;
	uint32_t n, croffset;
	size_t resid;
	ssize_t overrun;
	int extended = 0;
	uint32_t osize;
	int error = 0;
	uint32_t count, lastcn, cn;
	struct buf *bp;
	int ioflag = ap->a_ioflag;
	struct uio *uio = ap->a_uio;
	struct vnode *vp = ap->a_vp;
	struct vnode *thisvp;
	struct denode *dep = VTODE(vp);
	struct msdosfsmount *pmp = dep->de_pmp;
	struct ucred *cred = ap->a_cred;

#ifdef MSDOSFS_DEBUG
	printf("msdosfs_write(vp %p, uio %p, ioflag %08x, cred %p\n",
	    vp, uio, ioflag, cred);
	printf("msdosfs_write(): diroff %d, dirclust %d, startcluster %d\n",
	    dep->de_diroffset, dep->de_dirclust, dep->de_StartCluster);
#endif

	switch (vp->v_type) {
	case VREG:
		if (ioflag & IO_APPEND)
			uio->uio_offset = dep->de_FileSize;
		thisvp = vp;
		break;
	case VDIR:
		return EISDIR;
	default:
		panic("msdosfs_write(): bad file type");
	}

	if (uio->uio_offset < 0)
		return (EINVAL);

	if (uio->uio_resid == 0)
		return (0);

	/* Don't bother to try to write files larger than the f/s limit */
	if (uio->uio_offset > MSDOSFS_FILESIZE_MAX ||
	    uio->uio_resid > (MSDOSFS_FILESIZE_MAX - uio->uio_offset))
		return (EFBIG);

	/* do the filesize rlimit check */
	if ((error = vn_fsizechk(vp, uio, ioflag, &overrun)))
		return (error);

	/*
	 * If the offset we are starting the write at is beyond the end of
	 * the file, then they've done a seek.  Unix filesystems allow
	 * files with holes in them, DOS doesn't so we must fill the hole
	 * with zeroed blocks.
	 */
	if (uio->uio_offset > dep->de_FileSize) {
		if ((error = deextend(dep, uio->uio_offset, cred)) != 0)
			goto out;
	}

	/*
	 * Remember some values in case the write fails.
	 */
	resid = uio->uio_resid;
	osize = dep->de_FileSize;

	/*
	 * If we write beyond the end of the file, extend it to its ultimate
	 * size ahead of the time to hopefully get a contiguous area.
	 */
	if (uio->uio_offset + resid > osize) {
		extended = 1;
		count = de_clcount(pmp, uio->uio_offset + resid) -
			de_clcount(pmp, osize);
		if ((error = extendfile(dep, count, NULL, NULL, 0)) &&
		    (error != ENOSPC || (ioflag & IO_UNIT)))
			goto errexit;
		lastcn = dep->de_fc[FC_LASTFC].fc_frcn;
	} else
		lastcn = de_clcount(pmp, osize) - 1;

	do {
		croffset = uio->uio_offset & pmp->pm_crbomask;
		cn = de_cluster(pmp, uio->uio_offset);

		if (cn > lastcn) {
			error = ENOSPC;
			break;
		}

		if (croffset == 0 &&
		    (de_cluster(pmp, uio->uio_offset + uio->uio_resid) > cn ||
		     (uio->uio_offset + uio->uio_resid) >= dep->de_FileSize)) {
			/*
			 * If either the whole cluster gets written,
			 * or we write the cluster from its start beyond EOF,
			 * then no need to read data from disk.
			 */
			bp = getblk(thisvp, cn, pmp->pm_bpcluster, 0, INFSLP);
			clrbuf(bp);
			/*
			 * Do the bmap now, since pcbmap needs buffers
			 * for the fat table. (see msdosfs_strategy)
			 */
			if (bp->b_blkno == bp->b_lblkno) {
				error = pcbmap(dep, bp->b_lblkno, &bp->b_blkno, 0, 0);
				if (error)
					bp->b_blkno = -1;
			}
			if (bp->b_blkno == -1) {
				brelse(bp);
				if (!error)
					error = EIO;		/* XXX */
				break;
			}
		} else {
			/*
			 * The block we need to write into exists, so
			 * read it in.
			 */
			error = bread(thisvp, cn, pmp->pm_bpcluster, &bp);
			if (error) {
				brelse(bp);
				break;
			}
		}

		n = ulmin(uio->uio_resid, pmp->pm_bpcluster - croffset);
		if (uio->uio_offset + n > dep->de_FileSize) {
			dep->de_FileSize = uio->uio_offset + n;
			uvm_vnp_setsize(vp, dep->de_FileSize);
		}
		uvm_vnp_uncache(vp);
		/*
		 * Should these vnode_pager_* functions be done on dir
		 * files?
		 */

		/*
		 * Copy the data from user space into the buf header.
		 */
		error = uiomove(bp->b_data + croffset, n, uio);

		/*
		 * If they want this synchronous then write it and wait for
		 * it.  Otherwise, if on a cluster boundary write it
		 * asynchronously so we can move on to the next block
		 * without delay.  Otherwise do a delayed write because we
		 * may want to write some more into the block later.
		 */
#if 0
		if (ioflag & IO_NOCACHE)
			bp->b_flags |= B_NOCACHE;
#endif
		if (ioflag & IO_SYNC)
			(void) bwrite(bp);
		else if (n + croffset == pmp->pm_bpcluster)
			bawrite(bp);
		else
			bdwrite(bp);
		dep->de_flag |= DE_UPDATE;
	} while (error == 0 && uio->uio_resid > 0);

	if (resid > uio->uio_resid)
		 VN_KNOTE(ap->a_vp, NOTE_WRITE | (extended ? NOTE_EXTEND : 0));

	if (dep->de_FileSize < osize)
		VN_KNOTE(ap->a_vp, NOTE_TRUNCATE);

	/*
	 * If the write failed and they want us to, truncate the file back
	 * to the size it was before the write was attempted.
	 */
errexit:
	if (error) {
		if (ioflag & IO_UNIT) {
			detrunc(dep, osize, ioflag & IO_SYNC, NOCRED, curproc);
			uio->uio_offset -= resid - uio->uio_resid;
			uio->uio_resid = resid;
		} else {
			detrunc(dep, dep->de_FileSize, ioflag & IO_SYNC, NOCRED, curproc);
			if (uio->uio_resid != resid)
				error = 0;
		}
	} else if (ioflag & IO_SYNC)
		error = deupdat(dep, 1);

out:
	/* correct the result for writes clamped by vn_fsizechk() */
	uio->uio_resid += overrun;
	return (error);
}

int
msdosfs_ioctl(void *v)
{
	return (ENOTTY);
}

/*
 * Flush the blocks of a file to disk.
 *
 * This function is worthless for vnodes that represent directories. Maybe we
 * could just do a sync if they try an fsync on a directory file.
 */
int
msdosfs_fsync(void *v)
{
	struct vop_fsync_args *ap = v;
	struct vnode *vp = ap->a_vp;

	vflushbuf(vp, ap->a_waitfor == MNT_WAIT);
	return (deupdat(VTODE(vp), ap->a_waitfor == MNT_WAIT));
}

/*
 * Flush the blocks of a file to disk.
 *
 * This function is worthless for vnodes that represent directories. Maybe we
 * could just do a sync if they try an fsync on a directory file.
 */
int
msdosfs_remove(void *v)
{
	struct vop_remove_args *ap = v;
	struct denode *dep = VTODE(ap->a_vp);
	struct denode *ddep = VTODE(ap->a_dvp);
	int error;

	if (ap->a_vp->v_type == VDIR)
		error = EPERM;
	else
		error = removede(ddep, dep);

	VN_KNOTE(ap->a_vp, NOTE_DELETE);
	VN_KNOTE(ap->a_dvp, NOTE_WRITE);

#ifdef MSDOSFS_DEBUG
	printf("msdosfs_remove(), dep %p, v_usecount %d\n", dep,
	    ap->a_vp->v_usecount);
#endif
	return (error);
}

/*
 * DOS filesystems don't know what links are. But since we already called
 * msdosfs_lookup() with create and lockparent, the parent is locked so we
 * have to free it before we return the error.
 */
int
msdosfs_link(void *v)
{
	struct vop_link_args *ap = v;

	VOP_ABORTOP(ap->a_dvp, ap->a_cnp);
	vput(ap->a_dvp);
	return (EOPNOTSUPP);
}

/*
 * Renames on files require moving the denode to a new hash queue since the
 * denode's location is used to compute which hash queue to put the file
 * in. Unless it is a rename in place.  For example "mv a b".
 *
 * What follows is the basic algorithm:
 *
 * if (file move) {
 *	if (dest file exists) {
 *		remove dest file
 *	}
 *	if (dest and src in same directory) {
 *		rewrite name in existing directory slot
 *	} else {
 *		write new entry in dest directory
 *		update offset and dirclust in denode
 *		move denode to new hash chain
 *		clear old directory entry
 *	}
 * } else {
 *	directory move
 *	if (dest directory exists) {
 *		if (dest is not empty) {
 *			return ENOTEMPTY
 *		}
 *		remove dest directory
 *	}
 *	if (dest and src in same directory) {
 *		rewrite name in existing entry
 *	} else {
 *		be sure dest is not a child of src directory
 *		write entry in dest directory
 *		update "." and ".." in moved directory
 *		update offset and dirclust in denode
 *		move denode to new hash chain
 *		clear old directory entry for moved directory
 *	}
 * }
 *
 * On entry:
 *	source's parent directory is unlocked
 *	source file or directory is unlocked
 *	destination's parent directory is locked
 *	destination file or directory is locked if it exists
 *
 * On exit:
 *	all denodes should be released
 *
 * Notes:
 * I'm not sure how the memory containing the pathnames pointed at by the
 * componentname structures is freed, there may be some memory bleeding
 * for each rename done.
 */
int
msdosfs_rename(void *v)
{
	struct vop_rename_args *ap = v;
	struct vnode *tvp = ap->a_tvp;
	struct vnode *tdvp = ap->a_tdvp;
	struct vnode *fvp = ap->a_fvp;
	struct vnode *fdvp = ap->a_fdvp;
	struct componentname *tcnp = ap->a_tcnp;
	struct componentname *fcnp = ap->a_fcnp;
	struct denode *ip, *xp, *dp, *zp;
	u_char toname[11], oldname[11];
	uint32_t from_diroffset, to_diroffset;
	u_char to_count;
	int doingdirectory = 0, newparent = 0;
	int error;
	uint32_t cn, pcl;
	daddr_t bn;
	struct msdosfsmount *pmp;
	struct direntry *dotdotp;
	struct buf *bp;

	pmp = VFSTOMSDOSFS(fdvp->v_mount);

#ifdef DIAGNOSTIC
	if ((tcnp->cn_flags & HASBUF) == 0 ||
	    (fcnp->cn_flags & HASBUF) == 0)
		panic("msdosfs_rename: no name");
#endif
	/*
	 * Check for cross-device rename.
	 */
	if ((fvp->v_mount != tdvp->v_mount) ||
	    (tvp && (fvp->v_mount != tvp->v_mount))) {
		error = EXDEV;
abortit:
		VOP_ABORTOP(tdvp, tcnp);
		if (tdvp == tvp)
			vrele(tdvp);
		else
			vput(tdvp);
		if (tvp)
			vput(tvp);
		VOP_ABORTOP(fdvp, fcnp);
		vrele(fdvp);
		vrele(fvp);
		return (error);
	}

	/*
	 * If source and dest are the same, do nothing.
	 */
	if (tvp == fvp) {
		error = 0;
		goto abortit;
	}

	/* */
	if ((error = vn_lock(fvp, LK_EXCLUSIVE | LK_RETRY)) != 0)
		goto abortit;
	dp = VTODE(fdvp);
	ip = VTODE(fvp);

	/*
	 * Be sure we are not renaming ".", "..", or an alias of ".". This
	 * leads to a crippled directory tree.  It's pretty tough to do a
	 * "ls" or "pwd" with the "." directory entry missing, and "cd .."
	 * doesn't work if the ".." entry is missing.
	 */
	if (ip->de_Attributes & ATTR_DIRECTORY) {
		/*
		 * Avoid ".", "..", and aliases of "." for obvious reasons.
		 */
		if ((fcnp->cn_namelen == 1 && fcnp->cn_nameptr[0] == '.') ||
		    dp == ip ||
		    (fcnp->cn_flags & ISDOTDOT) ||
		    (tcnp->cn_flags & ISDOTDOT) ||
		    (ip->de_flag & DE_RENAME)) {
			VOP_UNLOCK(fvp);
			error = EINVAL;
			goto abortit;
		}
		ip->de_flag |= DE_RENAME;
		doingdirectory++;
	}
	VN_KNOTE(fdvp, NOTE_WRITE);	/* XXX right place? */

	/*
	 * When the target exists, both the directory
	 * and target vnodes are returned locked.
	 */
	dp = VTODE(tdvp);
	xp = tvp ? VTODE(tvp) : NULL;
	/*
	 * Remember direntry place to use for destination
	 */
	to_diroffset = dp->de_fndoffset;
	to_count = dp->de_fndcnt;

	/*
	 * If ".." must be changed (ie the directory gets a new
	 * parent) then the source directory must not be in the
	 * directory hierarchy above the target, as this would
	 * orphan everything below the source directory. Also
	 * the user must have write permission in the source so
	 * as to be able to change "..". We must repeat the call
	 * to namei, as the parent directory is unlocked by the
	 * call to doscheckpath().
	 */
	error = VOP_ACCESS(fvp, VWRITE, tcnp->cn_cred, tcnp->cn_proc);
	VOP_UNLOCK(fvp);
	if (VTODE(fdvp)->de_StartCluster != VTODE(tdvp)->de_StartCluster)
		newparent = 1;
	vrele(fdvp);
	if (doingdirectory && newparent) {
		if (error)	/* write access check above */
			goto bad1;
		if (xp != NULL)
			vput(tvp);
		/*
		 * doscheckpath() vput()'s dp,
		 * so we have to do a relookup afterwards
		 */
		if ((error = doscheckpath(ip, dp)) != 0)
			goto out;
		if ((tcnp->cn_flags & SAVESTART) == 0)
			panic("msdosfs_rename: lost to startdir");
		if ((error = vfs_relookup(tdvp, &tvp, tcnp)) != 0)
			goto out;
		dp = VTODE(tdvp);
		xp = tvp ? VTODE(tvp) : NULL;
	}

	VN_KNOTE(tdvp, NOTE_WRITE);

	if (xp != NULL) {
		/*
		 * Target must be empty if a directory and have no links
		 * to it. Also, ensure source and target are compatible
		 * (both directories, or both not directories).
		 */
		if (xp->de_Attributes & ATTR_DIRECTORY) {
			if (!dosdirempty(xp)) {
				error = ENOTEMPTY;
				goto bad1;
			}
			if (!doingdirectory) {
				error = ENOTDIR;
				goto bad1;
			}
			cache_purge(tdvp);
		} else if (doingdirectory) {
			error = EISDIR;
			goto bad1;
		}
		if ((error = removede(dp, xp)) != 0)
			goto bad1;
		VN_KNOTE(tvp, NOTE_DELETE);
		vput(tvp);
		xp = NULL;
	}

	/*
	 * Convert the filename in tcnp into a dos filename. We copy this
	 * into the denode and directory entry for the destination
	 * file/directory.
	 */
	if ((error = uniqdosname(VTODE(tdvp), tcnp, toname)) != 0)
		goto bad1;

	/*
	 * Since from wasn't locked at various places above,
	 * have to do a relookup here.
	 */
	fcnp->cn_flags &= ~MODMASK;
	fcnp->cn_flags |= LOCKPARENT | LOCKLEAF;
	if ((fcnp->cn_flags & SAVESTART) == 0)
		panic("msdosfs_rename: lost from startdir");
	if (!newparent)
		VOP_UNLOCK(tdvp);
	(void) vfs_relookup(fdvp, &fvp, fcnp);
	if (fvp == NULL) {
		/*
		 * From name has disappeared.
		 */
		if (doingdirectory)
			panic("rename: lost dir entry");
		vrele(ap->a_fvp);
		if (newparent)
			VOP_UNLOCK(tdvp);
		vrele(tdvp);
		return 0;
	}
	xp = VTODE(fvp);
	zp = VTODE(fdvp);
	from_diroffset = zp->de_fndoffset;

	/*
	 * Ensure that the directory entry still exists and has not
	 * changed till now. If the source is a file the entry may
	 * have been unlinked or renamed. In either case there is
	 * no further work to be done. If the source is a directory
	 * then it cannot have been rmdir'ed or renamed; this is
	 * prohibited by the DE_RENAME flag.
	 */
	if (xp != ip) {
		if (doingdirectory)
			panic("rename: lost dir entry");
		vrele(ap->a_fvp);
		if (newparent)
			VOP_UNLOCK(fdvp);
		xp = NULL;
	} else {
		vrele(fvp);
		xp = NULL;

		/*
		 * First write a new entry in the destination
		 * directory and mark the entry in the source directory
		 * as deleted.  Then move the denode to the correct hash
		 * chain for its new location in the filesystem.  And, if
		 * we moved a directory, then update its .. entry to point
		 * to the new parent directory.
		 */
		bcopy(ip->de_Name, oldname, 11);
		bcopy(toname, ip->de_Name, 11);	/* update denode */
		dp->de_fndoffset = to_diroffset;
		dp->de_fndcnt = to_count;
		error = createde(ip, dp, NULL, tcnp);
		if (error) {
			bcopy(oldname, ip->de_Name, 11);
			if (newparent)
				VOP_UNLOCK(fdvp);
			goto bad;
		}
		ip->de_refcnt++;
		zp->de_fndoffset = from_diroffset;
		if ((error = removede(zp, ip)) != 0) {
			/* XXX should really panic here, fs is corrupt */
			if (newparent)
				VOP_UNLOCK(fdvp);
			goto bad;
		}

		cache_purge(fvp);

		if (!doingdirectory) {
			error = pcbmap(dp, de_cluster(pmp, to_diroffset), 0,
				       &ip->de_dirclust, 0);
			if (error) {
				/* XXX should really panic here, fs is corrupt */
				if (newparent)
					VOP_UNLOCK(fdvp);
				goto bad;
			}
			ip->de_diroffset = to_diroffset;
			if (ip->de_dirclust != MSDOSFSROOT)
				ip->de_diroffset &= pmp->pm_crbomask;
		}
		reinsert(ip);
		if (newparent)
			VOP_UNLOCK(fdvp);
	}

	/*
	 * If we moved a directory to a new parent directory, then we must
	 * fixup the ".." entry in the moved directory.
	 */
	if (doingdirectory && newparent) {
		cn = ip->de_StartCluster;
		if (cn == MSDOSFSROOT) {
			/* this should never happen */
			panic("msdosfs_rename: updating .. in root directory?");
		} else
			bn = cntobn(pmp, cn);
		error = bread(pmp->pm_devvp, bn, pmp->pm_bpcluster, &bp);
		if (error) {
			/* XXX should really panic here, fs is corrupt */
			brelse(bp);
			goto bad;
		}
		dotdotp = (struct direntry *)bp->b_data;
		putushort(dotdotp[0].deStartCluster, cn);
		pcl = dp->de_StartCluster;
		if (FAT32(pmp) && pcl == pmp->pm_rootdirblk)
			pcl = 0;
		putushort(dotdotp[1].deStartCluster, pcl);
		if (FAT32(pmp)) {
			putushort(dotdotp[0].deHighClust, cn >> 16);
			putushort(dotdotp[1].deHighClust, pcl >> 16);
		}
		if ((error = bwrite(bp)) != 0) {
			/* XXX should really panic here, fs is corrupt */
			goto bad;
		}
	}

	VN_KNOTE(fvp, NOTE_RENAME);

bad:
	VOP_UNLOCK(fvp);
	vrele(fdvp);
bad1:
	if (xp)
		vput(tvp);
	vput(tdvp);
out:
	ip->de_flag &= ~DE_RENAME;
	vrele(fvp);
	return (error);

}

struct {
	struct direntry dot;
	struct direntry dotdot;
} dosdirtemplate = {
	{	".       ", "   ",			/* the . entry */
		ATTR_DIRECTORY,				/* file attribute */
		CASE_LOWER_BASE | CASE_LOWER_EXT,	/* lower case */
		0,					/* create time 100ths */
		{ 0, 0 }, { 0, 0 },			/* create time & date */
		{ 0, 0 },				/* access date */
		{ 0, 0 },				/* high bits of start cluster */
		{ 210, 4 }, { 210, 4 },			/* modify time & date */
		{ 0, 0 },				/* startcluster */
		{ 0, 0, 0, 0 }				/* filesize */
	},
	{	"..      ", "   ",			/* the .. entry */
		ATTR_DIRECTORY,				/* file attribute */
		CASE_LOWER_BASE | CASE_LOWER_EXT,	/* lower case */
		0,					/* create time 100ths */
		{ 0, 0 }, { 0, 0 },			/* create time & date */
		{ 0, 0 },				/* access date */
		{ 0, 0 },				/* high bits of start cluster */
		{ 210, 4 }, { 210, 4 },			/* modify time & date */
		{ 0, 0 },				/* startcluster */
		{ 0, 0, 0, 0 }				/* filesize */
	}
};

int
msdosfs_mkdir(void *v)
{
	struct vop_mkdir_args *ap = v;
	struct componentname *cnp = ap->a_cnp;
	struct denode ndirent;
	struct denode *dep;
	struct denode *pdep = VTODE(ap->a_dvp);
	int error;
	daddr_t bn;
	uint32_t newcluster, pcl;
	struct direntry *denp;
	struct msdosfsmount *pmp = pdep->de_pmp;
	struct buf *bp;
	struct timespec ts;

	/*
	 * If this is the root directory and there is no space left we
	 * can't do anything.  This is because the root directory can not
	 * change size.
	 */
	if (pdep->de_StartCluster == MSDOSFSROOT
	    && pdep->de_fndoffset >= pdep->de_FileSize) {
		error = ENOSPC;
		goto bad2;
	}

	/*
	 * Allocate a cluster to hold the about to be created directory.
	 */
	error = clusteralloc(pmp, 0, 1, &newcluster, NULL);
	if (error)
		goto bad2;

	bzero(&ndirent, sizeof(ndirent));
	ndirent.de_pmp = pmp;
	ndirent.de_flag = DE_ACCESS | DE_CREATE | DE_UPDATE;
	getnanotime(&ts);
	DETIMES(&ndirent, &ts, &ts, &ts);

	/*
	 * Now fill the cluster with the "." and ".." entries. And write
	 * the cluster to disk.  This way it is there for the parent
	 * directory to be pointing at if there were a crash.
	 */
	bn = cntobn(pmp, newcluster);
	/* always succeeds */
	bp = getblk(pmp->pm_devvp, bn, pmp->pm_bpcluster, 0, INFSLP);
	bzero(bp->b_data, pmp->pm_bpcluster);
	bcopy(&dosdirtemplate, bp->b_data, sizeof dosdirtemplate);
	denp = (struct direntry *)bp->b_data;
	putushort(denp[0].deStartCluster, newcluster);
	putushort(denp[0].deCDate, ndirent.de_CDate);
	putushort(denp[0].deCTime, ndirent.de_CTime);
	denp[0].deCTimeHundredth = ndirent.de_CTimeHundredth;
	putushort(denp[0].deADate, ndirent.de_ADate);
	putushort(denp[0].deMDate, ndirent.de_MDate);
	putushort(denp[0].deMTime, ndirent.de_MTime);
	pcl = pdep->de_StartCluster;
	if (FAT32(pmp) && pcl == pmp->pm_rootdirblk)
		pcl = 0;
	putushort(denp[1].deStartCluster, pcl);
	putushort(denp[1].deCDate, ndirent.de_CDate);
	putushort(denp[1].deCTime, ndirent.de_CTime);
	denp[1].deCTimeHundredth = ndirent.de_CTimeHundredth;
	putushort(denp[1].deADate, ndirent.de_ADate);
	putushort(denp[1].deMDate, ndirent.de_MDate);
	putushort(denp[1].deMTime, ndirent.de_MTime);
	if (FAT32(pmp)) {
		putushort(denp[0].deHighClust, newcluster >> 16);
		putushort(denp[1].deHighClust, pdep->de_StartCluster >> 16);
	}

	if ((error = bwrite(bp)) != 0)
		goto bad;

	/*
	 * Now build up a directory entry pointing to the newly allocated
	 * cluster.  This will be written to an empty slot in the parent
	 * directory.
	 */
#ifdef DIAGNOSTIC
	if ((cnp->cn_flags & HASBUF) == 0)
		panic("msdosfs_mkdir: no name");
#endif
	if ((error = uniqdosname(pdep, cnp, ndirent.de_Name)) != 0)
		goto bad;

	ndirent.de_Attributes = ATTR_DIRECTORY;
	ndirent.de_StartCluster = newcluster;
	ndirent.de_FileSize = 0;
	ndirent.de_dev = pdep->de_dev;
	ndirent.de_devvp = pdep->de_devvp;
	if ((error = createde(&ndirent, pdep, &dep, cnp)) != 0)
		goto bad;
	if ((cnp->cn_flags & SAVESTART) == 0)
		pool_put(&namei_pool, cnp->cn_pnbuf);
	VN_KNOTE(ap->a_dvp, NOTE_WRITE | NOTE_LINK);
	vput(ap->a_dvp);
	*ap->a_vpp = DETOV(dep);
	return (0);

bad:
	clusterfree(pmp, newcluster, NULL);
bad2:
	pool_put(&namei_pool, cnp->cn_pnbuf);
	vput(ap->a_dvp);
	return (error);
}

int
msdosfs_rmdir(void *v)
{
	struct vop_rmdir_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct vnode *dvp = ap->a_dvp;
	struct componentname *cnp = ap->a_cnp;
	struct denode *ip, *dp;
	int error;

	ip = VTODE(vp);
	dp = VTODE(dvp);
	/*
	 * Verify the directory is empty (and valid).
	 * (Rmdir ".." won't be valid since
	 *  ".." will contain a reference to
	 *  the current directory and thus be
	 *  non-empty.)
	 */
	error = 0;
	if (!dosdirempty(ip) || ip->de_flag & DE_RENAME) {
		error = ENOTEMPTY;
		goto out;
	}

	VN_KNOTE(dvp, NOTE_WRITE | NOTE_LINK);

	/*
	 * Delete the entry from the directory.  For dos filesystems this
	 * gets rid of the directory entry on disk, the in memory copy
	 * still exists but the de_refcnt is <= 0.  This prevents it from
	 * being found by deget().  When the vput() on dep is done we give
	 * up access and eventually msdosfs_reclaim() will be called which
	 * will remove it from the denode cache.
	 */
	if ((error = removede(dp, ip)) != 0)
		goto out;
	/*
	 * This is where we decrement the link count in the parent
	 * directory.  Since dos filesystems don't do this we just purge
	 * the name cache and let go of the parent directory denode.
	 */
	cache_purge(dvp);
	vput(dvp);
	dvp = NULL;
	/*
	 * Truncate the directory that is being deleted.
	 */
	error = detrunc(ip, (uint32_t)0, IO_SYNC, cnp->cn_cred, cnp->cn_proc);
	cache_purge(vp);
out:
	if (dvp)
		vput(dvp);
	VN_KNOTE(vp, NOTE_DELETE);
	vput(vp);
	return (error);
}

/*
 * DOS filesystems don't know what symlinks are.
 */
int
msdosfs_symlink(void *v)
{
	struct vop_symlink_args *ap = v;

	VOP_ABORTOP(ap->a_dvp, ap->a_cnp);
	vput(ap->a_dvp);
	return (EOPNOTSUPP);
}

int
msdosfs_readdir(void *v)
{
	struct vop_readdir_args *ap = v;
	int error = 0;
	int diff;
	long n;
	int blsize;
	long on;
	long lost;
	long count;
	uint32_t dirsperblk;
	uint32_t cn, lbn;
	uint32_t fileno;
	long bias = 0;
	daddr_t bn;
	struct buf *bp;
	struct denode *dep = VTODE(ap->a_vp);
	struct msdosfsmount *pmp = dep->de_pmp;
	struct direntry *dentp;
	struct dirent dirbuf;
	struct uio *uio = ap->a_uio;
	off_t offset, wlast = -1;
	int chksum = -1;

#ifdef MSDOSFS_DEBUG
	printf("msdosfs_readdir(): vp %p, uio %p, cred %p, eofflagp %p\n",
	    ap->a_vp, uio, ap->a_cred, ap->a_eofflag);
#endif

	/*
	 * msdosfs_readdir() won't operate properly on regular files since
	 * it does i/o only with the filesystem vnode, and hence can
	 * retrieve the wrong block from the buffer cache for a plain file.
	 * So, fail attempts to readdir() on a plain file.
	 */
	if ((dep->de_Attributes & ATTR_DIRECTORY) == 0)
		return (ENOTDIR);

	/*
	 * To be safe, initialize dirbuf
	 */
	bzero(&dirbuf, sizeof(dirbuf));

	/*
	 * If the user buffer is smaller than the size of one dos directory
	 * entry or the file offset is not a multiple of the size of a
	 * directory entry, then we fail the read.
	 */
	count = uio->uio_resid & ~(sizeof(struct direntry) - 1);
	offset = uio->uio_offset;
	if (count < sizeof(struct direntry) ||
	    (offset & (sizeof(struct direntry) - 1)))
		return (EINVAL);
	lost = uio->uio_resid - count;
	uio->uio_resid = count;

	dirsperblk = pmp->pm_BytesPerSec / sizeof(struct direntry);

	/*
	 * If they are reading from the root directory then, we simulate
	 * the . and .. entries since these don't exist in the root
	 * directory.  We also set the offset bias to make up for having to
	 * simulate these entries. By this I mean that at file offset 64 we
	 * read the first entry in the root directory that lives on disk.
	 */
	if (dep->de_StartCluster == MSDOSFSROOT
	    || (FAT32(pmp) && dep->de_StartCluster == pmp->pm_rootdirblk)) {
#if 0
		printf("msdosfs_readdir(): going after . or .. in root dir, offset %d\n",
		    offset);
#endif
		bias = 2 * sizeof(struct direntry);
		if (offset < bias) {
			for (n = (int)offset / sizeof(struct direntry);
			     n < 2; n++) {
			        if (FAT32(pmp))
				        dirbuf.d_fileno = pmp->pm_rootdirblk;
				else
				        dirbuf.d_fileno = 1;
				dirbuf.d_type = DT_DIR;
				switch (n) {
				case 0:
					dirbuf.d_namlen = 1;
					strlcpy(dirbuf.d_name, ".",
					    sizeof dirbuf.d_name);
					break;
				case 1:
					dirbuf.d_namlen = 2;
					strlcpy(dirbuf.d_name, "..",
					    sizeof dirbuf.d_name);
					break;
				}
				dirbuf.d_reclen = DIRENT_SIZE(&dirbuf);
				dirbuf.d_off = offset +
				    sizeof(struct direntry);
				if (uio->uio_resid < dirbuf.d_reclen)
					goto out;
				error = uiomove(&dirbuf, dirbuf.d_reclen, uio);
				if (error)
					goto out;
				offset = dirbuf.d_off;
			}
		}
	}

	while (uio->uio_resid > 0) {
		lbn = de_cluster(pmp, offset - bias);
		on = (offset - bias) & pmp->pm_crbomask;
		n = min(pmp->pm_bpcluster - on, uio->uio_resid);
		diff = dep->de_FileSize - (offset - bias);
		if (diff <= 0)
			break;
		n = min(n, diff);
		if ((error = pcbmap(dep, lbn, &bn, &cn, &blsize)) != 0)
			break;
		error = bread(pmp->pm_devvp, bn, blsize, &bp);
		if (error) {
			brelse(bp);
			return (error);
		}
		n = min(n, blsize - bp->b_resid);

		/*
		 * Convert from dos directory entries to fs-independent
		 * directory entries.
		 */
		for (dentp = (struct direntry *)(bp->b_data + on);
		     (char *)dentp < bp->b_data + on + n;
		     dentp++, offset += sizeof(struct direntry)) {
#if 0
			printf("rd: dentp %08x prev %08x crnt %08x deName %02x attr %02x\n",
			    dentp, prev, crnt, dentp->deName[0], dentp->deAttributes);
#endif
			/*
			 * If this is an unused entry, we can stop.
			 */
			if (dentp->deName[0] == SLOT_EMPTY) {
				brelse(bp);
				goto out;
			}
			/*
			 * Skip deleted entries.
			 */
			if (dentp->deName[0] == SLOT_DELETED) {
				chksum = -1;
				wlast = -1;
				continue;
			}

			/*
			 * Handle Win95 long directory entries
			 */
			if (dentp->deAttributes == ATTR_WIN95) {
				struct winentry *wep;
				if (pmp->pm_flags & MSDOSFSMNT_SHORTNAME)
					continue;
				wep = (struct winentry *)dentp;
				chksum = win2unixfn(wep, &dirbuf, chksum);
				if (wep->weCnt & WIN_LAST)
					wlast = offset;
				continue;
			}

			/*
			 * Skip volume labels
			 */
			if (dentp->deAttributes & ATTR_VOLUME) {
				chksum = -1;
				wlast = -1;
				continue;
			}

			/*
			 * This computation of d_fileno must match
			 * the computation of va_fileid in
			 * msdosfs_getattr.
			 */
			fileno = getushort(dentp->deStartCluster);
			if (FAT32(pmp))
			    fileno |= getushort(dentp->deHighClust) << 16;

			if (dentp->deAttributes & ATTR_DIRECTORY) {
				/* Special-case root */
				if (fileno == MSDOSFSROOT)  {
					fileno = FAT32(pmp) ?
					    pmp->pm_rootdirblk : 1;
				}

				dirbuf.d_fileno = fileno;
				dirbuf.d_type = DT_DIR;
			} else {
				if (getulong(dentp->deFileSize) == 0) {
					uint64_t fileno64;

					fileno64 = (cn == MSDOSFSROOT) ?
					    roottobn(pmp, 0) : cntobn(pmp, cn);

					fileno64 *= dirsperblk;
					fileno64 += dentp -
					    (struct direntry *)bp->b_data;

					fileno = fileidhash(fileno64);
				}

				dirbuf.d_fileno = fileno;
				dirbuf.d_type = DT_REG;
			}

			if (chksum != winChksum(dentp->deName))
				dirbuf.d_namlen = dos2unixfn(dentp->deName,
				    (u_char *)dirbuf.d_name,
				    pmp->pm_flags & MSDOSFSMNT_SHORTNAME);
			else
				dirbuf.d_name[dirbuf.d_namlen] = 0;
			chksum = -1;
			dirbuf.d_reclen = DIRENT_SIZE(&dirbuf);
			dirbuf.d_off = offset + sizeof(struct direntry);
			if (uio->uio_resid < dirbuf.d_reclen) {
				brelse(bp);
				/* Remember long-name offset. */
				if (wlast != -1)
					offset = wlast;
				goto out;
			}
			wlast = -1;
			error = uiomove(&dirbuf, dirbuf.d_reclen, uio);
			if (error) {
				brelse(bp);
				goto out;
			}
		}
		brelse(bp);
	}

out:
	uio->uio_offset = offset;
	uio->uio_resid += lost;
	if (dep->de_FileSize - (offset - bias) <= 0)
		*ap->a_eofflag = 1;
	else
		*ap->a_eofflag = 0;
	return (error);
}

/*
 * DOS filesystems don't know what symlinks are.
 */
int
msdosfs_readlink(void *v)
{
#if 0
	struct vop_readlink_args /* {
		struct vnode *a_vp;
		struct uio *a_uio;
		struct ucred *a_cred;
	} */ *ap;
#endif

	return (EINVAL);
}

int
msdosfs_lock(void *v)
{
	struct vop_lock_args *ap = v;
	struct vnode *vp = ap->a_vp;

	return rrw_enter(&VTODE(vp)->de_lock, ap->a_flags & LK_RWFLAGS);
}

int
msdosfs_unlock(void *v)
{
	struct vop_unlock_args *ap = v;
	struct vnode *vp = ap->a_vp;

	rrw_exit(&VTODE(vp)->de_lock);
	return 0;
}

int
msdosfs_islocked(void *v)
{
	struct vop_islocked_args *ap = v;

	return rrw_status(&VTODE(ap->a_vp)->de_lock);
}

/*
 * vp  - address of vnode file the file
 * bn  - which cluster we are interested in mapping to a filesystem block number
 * vpp - returns the vnode for the block special file holding the filesystem
 *	 containing the file of interest
 * bnp - address of where to return the filesystem relative block number
 */
int
msdosfs_bmap(void *v)
{
	struct vop_bmap_args *ap = v;
	struct denode *dep = VTODE(ap->a_vp);
	uint32_t cn;

	if (ap->a_vpp != NULL)
		*ap->a_vpp = dep->de_devvp;
	if (ap->a_bnp == NULL)
		return (0);

	cn = ap->a_bn;
	if (cn != ap->a_bn)
		return (EFBIG);

	return (msdosfs_bmaparray(ap->a_vp, cn, ap->a_bnp, ap->a_runp));
}

int
msdosfs_bmaparray(struct vnode *vp, uint32_t cn, daddr_t *bnp, int *runp)
{
	struct denode *dep = VTODE(vp);
	struct msdosfsmount *pmp = dep->de_pmp;
	struct mount *mp;
	int error, maxrun = 0, run;
	daddr_t runbn;

	mp = vp->v_mount;

	if (runp) {
		/*
		 * XXX
		 * If MAXBSIZE is the largest transfer the disks can handle,
		 * we probably want maxrun to be 1 block less so that we
		 * don't create a block larger than the device can handle.
		 */
		*runp = 0;
		maxrun = min(MAXBSIZE / mp->mnt_stat.f_iosize - 1,
		    pmp->pm_maxcluster - cn);
	}

	if ((error = pcbmap(dep, cn, bnp, 0, 0)) != 0)
		return (error);

	for (run = 1; run <= maxrun; run++) {
		error = pcbmap(dep, cn + run, &runbn, 0, 0);
		if (error != 0 || (runbn != *bnp + de_cn2bn(pmp, run)))
			break;
	}

	if (runp)
		*runp = run - 1;

	return (0);
}

int
msdosfs_strategy(void *v)
{
	struct vop_strategy_args *ap = v;
	struct buf *bp = ap->a_bp;
	struct denode *dep = VTODE(bp->b_vp);
	struct vnode *vp;
	int error = 0;
	int s;

	if (bp->b_vp->v_type == VBLK || bp->b_vp->v_type == VCHR)
		panic("msdosfs_strategy: spec");
	/*
	 * If we don't already know the filesystem relative block number
	 * then get it using pcbmap().  If pcbmap() returns the block
	 * number as -1 then we've got a hole in the file.  DOS filesystems
	 * don't allow files with holes, so we shouldn't ever see this.
	 */
	if (bp->b_blkno == bp->b_lblkno) {
		error = pcbmap(dep, bp->b_lblkno, &bp->b_blkno, 0, 0);
		if (error)
			bp->b_blkno = -1;
		if (bp->b_blkno == -1)
			clrbuf(bp);
	}
	if (bp->b_blkno == -1) {
		s = splbio();
		biodone(bp);
		splx(s);
		return (error);
	}

	/*
	 * Read/write the block from/to the disk that contains the desired
	 * file block.
	 */

	vp = dep->de_devvp;
	bp->b_dev = vp->v_rdev;
	VOP_STRATEGY(vp, bp);
	return (0);
}

int
msdosfs_print(void *v)
{
#if defined(DEBUG) || defined(DIAGNOSTIC) || defined(VFSLCKDEBUG)
	struct vop_print_args *ap = v;
	struct denode *dep = VTODE(ap->a_vp);

	printf(
	    "tag VT_MSDOSFS, startcluster %u, dircluster %u, diroffset %u ",
	    dep->de_StartCluster, dep->de_dirclust, dep->de_diroffset);
	printf(" dev %d, %d, %s\n",
	    major(dep->de_dev), minor(dep->de_dev),
	    VOP_ISLOCKED(ap->a_vp) ? "(LOCKED)" : "");
#ifdef DIAGNOSTIC
	printf("\n");
#endif
#endif

	return (0);
}

int
msdosfs_advlock(void *v)
{
	struct vop_advlock_args *ap = v;
	struct denode *dep = VTODE(ap->a_vp);

	return (lf_advlock(&dep->de_lockf, dep->de_FileSize, ap->a_id, ap->a_op,
	    ap->a_fl, ap->a_flags));
}

int
msdosfs_pathconf(void *v)
{
	struct vop_pathconf_args *ap = v;
	struct msdosfsmount *pmp = VTODE(ap->a_vp)->de_pmp;
	int error = 0;

	switch (ap->a_name) {
	case _PC_LINK_MAX:
		*ap->a_retval = 1;
		break;
	case _PC_NAME_MAX:
		*ap->a_retval = pmp->pm_flags & MSDOSFSMNT_LONGNAME ? WIN_MAXLEN : 12;
		break;
	case _PC_CHOWN_RESTRICTED:
		*ap->a_retval = 1;
		break;
	case _PC_NO_TRUNC:
		*ap->a_retval = 0;
		break;
	case _PC_TIMESTAMP_RESOLUTION:
		*ap->a_retval = 2000000000;	/* 2 billion nanoseconds */
		break;
	default:
		error = EINVAL;
		break;
	}

	return (error);
}

/*
 * Thomas Wang's hash function, severely hacked to always set the high
 * bit on the number it returns (so no longer a proper hash function).
 */
static uint32_t
fileidhash(uint64_t fileid)
{
	uint64_t c1 = 0x6e5ea73858134343LL;
	uint64_t c2 = 0xb34e8f99a2ec9ef5LL;

	/*
	 * We now have the original fileid value, as 64-bit value.
	 * We need to reduce it to 32-bits, with the top bit set.
	 */
	fileid ^= ((c1 ^ fileid) >> 32);
	fileid *= c1;
	fileid ^= ((c2 ^ fileid) >> 31);
	fileid *= c2;
	fileid ^= ((c1 ^ fileid) >> 32);

	return (uint32_t)(fileid | 0x80000000);
}

/* Global vfs data structures for msdosfs */
const struct vops msdosfs_vops = {
	.vop_lookup	= msdosfs_lookup,
	.vop_create	= msdosfs_create,
	.vop_mknod	= msdosfs_mknod,
	.vop_open	= msdosfs_open,
	.vop_close	= msdosfs_close,
	.vop_access	= msdosfs_access,
	.vop_getattr	= msdosfs_getattr,
	.vop_setattr	= msdosfs_setattr,
	.vop_read	= msdosfs_read,
	.vop_write	= msdosfs_write,
	.vop_ioctl	= msdosfs_ioctl,
	.vop_kqfilter	= msdosfs_kqfilter,
	.vop_fsync	= msdosfs_fsync,
	.vop_remove	= msdosfs_remove,
	.vop_link	= msdosfs_link,
	.vop_rename	= msdosfs_rename,
	.vop_mkdir	= msdosfs_mkdir,
	.vop_rmdir	= msdosfs_rmdir,
	.vop_symlink	= msdosfs_symlink,
	.vop_readdir	= msdosfs_readdir,
	.vop_readlink	= msdosfs_readlink,
	.vop_abortop	= vop_generic_abortop,
	.vop_inactive	= msdosfs_inactive,
	.vop_reclaim	= msdosfs_reclaim,
	.vop_lock	= msdosfs_lock,
	.vop_unlock	= msdosfs_unlock,
	.vop_bmap	= msdosfs_bmap,
	.vop_strategy	= msdosfs_strategy,
	.vop_print	= msdosfs_print,
	.vop_islocked	= msdosfs_islocked,
	.vop_pathconf	= msdosfs_pathconf,
	.vop_advlock	= msdosfs_advlock,
	.vop_bwrite	= vop_generic_bwrite,
	.vop_revoke	= vop_generic_revoke,
};

const struct filterops msdosfsread_filtops = {
	.f_flags	= FILTEROP_ISFD,
	.f_attach	= NULL,
	.f_detach	= filt_msdosfsdetach,
	.f_event	= filt_msdosfsread,
};

const struct filterops msdosfswrite_filtops = {
	.f_flags	= FILTEROP_ISFD,
	.f_attach	= NULL,
	.f_detach	= filt_msdosfsdetach,
	.f_event	= filt_msdosfswrite,
};

const struct filterops msdosfsvnode_filtops = {
	.f_flags	= FILTEROP_ISFD,
	.f_attach	= NULL,
	.f_detach	= filt_msdosfsdetach,
	.f_event	= filt_msdosfsvnode,
};

int
msdosfs_kqfilter(void *v)
{
	struct vop_kqfilter_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct knote *kn = ap->a_kn;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &msdosfsread_filtops;
		break;
	case EVFILT_WRITE:
		kn->kn_fop = &msdosfswrite_filtops;
		break;
	case EVFILT_VNODE:
		kn->kn_fop = &msdosfsvnode_filtops;
		break;
	default:
		return (EINVAL);
	}

	kn->kn_hook = (caddr_t)vp;

	klist_insert_locked(&vp->v_klist, kn);

	return (0);
}

void
filt_msdosfsdetach(struct knote *kn)
{
	struct vnode *vp = (struct vnode *)kn->kn_hook;

	klist_remove_locked(&vp->v_klist, kn);
}

int
filt_msdosfsread(struct knote *kn, long hint)
{
	struct vnode *vp = (struct vnode *)kn->kn_hook;
	struct denode *dep = VTODE(vp);

	/*
	 * filesystem is gone, so set the EOF flag and schedule
	 * the knote for deletion.
	 */
	if (hint == NOTE_REVOKE) {
		kn->kn_flags |= (EV_EOF | EV_ONESHOT);
		return (1);
	}

	kn->kn_data = dep->de_FileSize - foffset(kn->kn_fp);
	if (kn->kn_data == 0 && kn->kn_sfflags & NOTE_EOF) {
		kn->kn_fflags |= NOTE_EOF;
		return (1);
	}

	if (kn->kn_flags & (__EV_POLL | __EV_SELECT))
		return (1);

	return (kn->kn_data != 0);
}

int
filt_msdosfswrite(struct knote *kn, long hint)
{
	/*
	 * filesystem is gone, so set the EOF flag and schedule
	 * the knote for deletion.
	 */
	if (hint == NOTE_REVOKE) {
		kn->kn_flags |= (EV_EOF | EV_ONESHOT);
		return (1);
	}

	kn->kn_data = 0;
	return (1);
}

int
filt_msdosfsvnode(struct knote *kn, long hint)
{
	if (kn->kn_sfflags & hint)
		kn->kn_fflags |= hint;
	if (hint == NOTE_REVOKE) {
		kn->kn_flags |= EV_EOF;
		return (1);
	}
	return (kn->kn_fflags != 0);
}
