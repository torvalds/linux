/*	$OpenBSD: udf_vfsops.c,v 1.73 2025/09/20 13:53:36 mpi Exp $	*/

/*
 * Copyright (c) 2001, 2002 Scott Long <scottl@freebsd.org>
 * All rights reserved.
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
 *
 * $FreeBSD: src/sys/fs/udf/udf_vfsops.c,v 1.25 2005/01/25 15:52:03 phk Exp $
 */

/*
 * Ported to OpenBSD by Pedro Martelletto in February 2005.
 */

/*
 * Ok, here's how it goes.  The UDF specs are pretty clear on how each data
 * structure is made up, but not very clear on how they relate to each other.
 * Here is the skinny... This demonstrates a filesystem with one file in the
 * root directory.  Subdirectories are treated just as normal files, but they
 * have File Id Descriptors of their children as their file data.  As for the
 * Anchor Volume Descriptor Pointer, it can exist in two of the following three
 * places: sector 256, sector n (the max sector of the disk), or sector
 * n - 256.  It's a pretty good bet that one will exist at sector 256 though.
 * One caveat is unclosed CD media.  For that, sector 256 cannot be written,
 * so the Anchor Volume Descriptor Pointer can exist at sector 512 until the
 * media is closed.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/buf.h>
#include <sys/dirent.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/pool.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/queue.h>
#include <sys/vnode.h>
#include <sys/endian.h>
#include <sys/specdev.h>

#include <crypto/siphash.h>

#include <isofs/udf/ecma167-udf.h>
#include <isofs/udf/udf.h>
#include <isofs/udf/udf_extern.h>

struct pool udf_trans_pool;
struct pool unode_pool;
struct pool udf_ds_pool;

int udf_find_partmaps(struct umount *, struct logvol_desc *);
int udf_get_vpartmap(struct umount *, struct part_map_virt *);
int udf_get_spartmap(struct umount *, struct part_map_spare *);
int udf_get_mpartmap(struct umount *, struct part_map_meta *);
int udf_mountfs(struct vnode *, struct mount *, uint32_t, struct proc *);

const struct vfsops udf_vfsops = {
	.vfs_mount	= udf_mount,
	.vfs_start	= udf_start,
	.vfs_unmount	= udf_unmount,
	.vfs_root	= udf_root,
	.vfs_quotactl	= udf_quotactl,
	.vfs_statfs	= udf_statfs,
	.vfs_sync	= udf_sync,
	.vfs_vget	= udf_vget,
	.vfs_fhtovp	= udf_fhtovp,
	.vfs_vptofh	= udf_vptofh,
	.vfs_init	= udf_init,
	.vfs_checkexp	= udf_checkexp,
};

int
udf_init(struct vfsconf *foo)
{
	pool_init(&udf_trans_pool, MAXNAMLEN * sizeof(unicode_t), 0, IPL_NONE,
	    PR_WAITOK, "udftrpl", NULL);
	pool_init(&unode_pool, sizeof(struct unode), 0, IPL_NONE,
	    PR_WAITOK, "udfndpl", NULL);
	pool_init(&udf_ds_pool, sizeof(struct udf_dirstream), 0, IPL_NONE,
	    PR_WAITOK, "udfdspl", NULL);

	return (0);
}

int
udf_start(struct mount *mp, int flags, struct proc *p)
{
	return (0);
}

int
udf_mount(struct mount *mp, const char *path, void *data,
    struct nameidata *ndp,  struct proc *p)
{
	struct vnode *devvp;	/* vnode of the mount device */
	struct udf_args *args = data;
	char fspec[MNAMELEN];
	int error;

	if ((mp->mnt_flag & MNT_RDONLY) == 0) {
		mp->mnt_flag |= MNT_RDONLY;
#ifdef UDF_DEBUG
		printf("udf_mount: enforcing read-only mode\n");
#endif
	}

	/*
	 * No root filesystem support.  Probably not a big deal, since the
	 * bootloader doesn't understand UDF.
	 */
	if (mp->mnt_flag & MNT_ROOTFS)
		return (EOPNOTSUPP);

	/*
	 * If updating, check whether changing from read-only to
	 * read/write; if there is no device name, that's all we do.
	 */
	if (mp->mnt_flag & MNT_UPDATE) {
		return (0);
	}

	error = copyinstr(args->fspec, fspec, sizeof(fspec), NULL);
	if (error)
		return (error);

	NDINIT(ndp, LOOKUP, FOLLOW, UIO_SYSSPACE, fspec, p);
	if ((error = namei(ndp)))
		return (error);

	devvp = ndp->ni_vp;
	if (devvp->v_type != VBLK) {
		vrele(devvp);
		return (ENOTBLK);
	}

	if (major(devvp->v_rdev) >= nblkdev) {
		vrele(devvp);
		return (ENXIO);
	}

	if ((error = udf_mountfs(devvp, mp, args->lastblock, p))) {
		vrele(devvp);
		return (error);
	}

	/*
	 * Keep a copy of the mount information.
	 */
	bzero(mp->mnt_stat.f_mntonname, MNAMELEN);
	strlcpy(mp->mnt_stat.f_mntonname, path, MNAMELEN);
	bzero(mp->mnt_stat.f_mntfromname, MNAMELEN);
	strlcpy(mp->mnt_stat.f_mntfromname, fspec, MNAMELEN);
	bzero(mp->mnt_stat.f_mntfromspec, MNAMELEN);
	strlcpy(mp->mnt_stat.f_mntfromspec, fspec, MNAMELEN);

	return (0);
};

/*
 * Check the descriptor tag for both the correct id and correct checksum.
 * Return zero if all is good, EINVAL if not.
 */
int
udf_checktag(struct desc_tag *tag, uint16_t id)
{
	uint8_t *itag;
	uint8_t i, cksum = 0;

	itag = (uint8_t *)tag;

	if (letoh16(tag->id) != id)
		return (EINVAL);

	for (i = 0; i < 15; i++)
		cksum = cksum + itag[i];
	cksum = cksum - itag[4];

	if (cksum == tag->cksum)
		return (0);

	return (EINVAL);
}

int
udf_mountfs(struct vnode *devvp, struct mount *mp, uint32_t lb, struct proc *p)
{
	struct buf *bp = NULL;
	struct anchor_vdp avdp;
	struct umount *ump = NULL;
	struct part_desc *pd;
	struct logvol_desc *lvd;
	struct fileset_desc *fsd;
	struct extfile_entry *xfentry;
	struct file_entry *fentry;
	uint32_t sector, size, mvds_start, mvds_end;
	uint32_t fsd_offset = 0;
	uint16_t part_num = 0, fsd_part = 0;
	int error = EINVAL;
	int logvol_found = 0, part_found = 0, fsd_found = 0;
	int bsize;

	/*
	 * Disallow multiple mounts of the same device.
	 * Disallow mounting of a device that is currently in use
	 * (except for root, which might share swap device for miniroot).
	 * Flush out any old buffers remaining from a previous use.
	 */
	if ((error = vfs_mountedon(devvp)))
		return (error);
	if (vcount(devvp) > 1 && devvp != rootvp)
		return (EBUSY);
	vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
	error = vinvalbuf(devvp, V_SAVE, p->p_ucred, p, 0, INFSLP);
	VOP_UNLOCK(devvp);
	if (error)
		return (error);

	error = VOP_OPEN(devvp, FREAD, FSCRED, p);
	if (error)
		return (error);

	ump = malloc(sizeof(*ump), M_UDFMOUNT, M_WAITOK | M_ZERO);

	mp->mnt_data = ump;
	mp->mnt_stat.f_fsid.val[0] = devvp->v_rdev;
	mp->mnt_stat.f_fsid.val[1] = mp->mnt_vfc->vfc_typenum;
	mp->mnt_stat.f_namemax = NAME_MAX;
	mp->mnt_flag |= MNT_LOCAL;

	ump->um_mountp = mp;
	ump->um_dev = devvp->v_rdev;
	ump->um_devvp = devvp;

	bsize = 2048;	/* Should probe the media for its size. */

	/* 
	 * Get the Anchor Volume Descriptor Pointer from sector 256.
	 * Should also check sector n - 256, n, and 512.
	 */
	sector = 256;
	if ((error = bread(devvp, sector * btodb(bsize), bsize, &bp)) != 0)
		goto bail;
	if ((error = udf_checktag((struct desc_tag *)bp->b_data, TAGID_ANCHOR)))
		goto bail;

	bcopy(bp->b_data, &avdp, sizeof(struct anchor_vdp));
	brelse(bp);
	bp = NULL;

	/*
	 * Extract the Partition Descriptor and Logical Volume Descriptor
	 * from the Volume Descriptor Sequence.
	 * Should we care about the partition type right now?
	 * What about multiple partitions?
	 */
	mvds_start = letoh32(avdp.main_vds_ex.loc);
	mvds_end = mvds_start + (letoh32(avdp.main_vds_ex.len) - 1) / bsize;
	for (sector = mvds_start; sector < mvds_end; sector++) {
		if ((error = bread(devvp, sector * btodb(bsize), bsize, 
				   &bp)) != 0) {
			printf("Can't read sector %d of VDS\n", sector);
			goto bail;
		}
		lvd = (struct logvol_desc *)bp->b_data;
		if (!udf_checktag(&lvd->tag, TAGID_LOGVOL)) {
			ump->um_bsize = letoh32(lvd->lb_size);
			ump->um_bmask = ump->um_bsize - 1;
			ump->um_bshift = ffs(ump->um_bsize) - 1;
			fsd_part = letoh16(lvd->_lvd_use.fsd_loc.loc.part_num);
			fsd_offset = letoh32(lvd->_lvd_use.fsd_loc.loc.lb_num);
			if (udf_find_partmaps(ump, lvd))
				break;
			logvol_found = 1;
		}
		pd = (struct part_desc *)bp->b_data;
		if (!udf_checktag(&pd->tag, TAGID_PARTITION)) {
			part_found = 1;
			part_num = letoh16(pd->part_num);
			ump->um_len = ump->um_reallen = letoh32(pd->part_len);
			ump->um_start = ump->um_realstart = letoh32(pd->start_loc);
		}

		brelse(bp); 
		bp = NULL;
		if ((part_found) && (logvol_found))
			break;
	}

	if (!part_found || !logvol_found) {
		error = EINVAL;
		goto bail;
	}

	if (ISSET(ump->um_flags, UDF_MNT_USES_META)) {
		/* Read Metadata File 'File Entry' to find Metadata file. */
		struct long_ad *la;
		sector = ump->um_start + ump->um_meta_start; /* Set in udf_get_mpartmap() */
		if ((error = RDSECTOR(devvp, sector, ump->um_bsize, &bp)) != 0) {
			printf("Cannot read sector %d for Metadata File Entry\n", sector);
			error = EINVAL;
			goto bail;
		}
		xfentry = (struct extfile_entry *)bp->b_data;
		fentry = (struct file_entry *)bp->b_data;
		if (udf_checktag(&xfentry->tag, TAGID_EXTFENTRY) == 0)
			la = (struct long_ad *)&xfentry->data[letoh32(xfentry->l_ea)];
		else if (udf_checktag(&fentry->tag, TAGID_FENTRY) == 0)
			la = (struct long_ad *)&fentry->data[letoh32(fentry->l_ea)];
		else {
			printf("Invalid Metadata File FE @ sector %d! (tag.id %d)\n",
			    sector, fentry->tag.id);
			error = EINVAL;
			goto bail;
		}
		ump->um_meta_start = letoh32(la->loc.lb_num);
		ump->um_meta_len = letoh32(la->len);
		if (bp != NULL) {
			brelse(bp);
			bp = NULL;
		}
	} else if (fsd_part != part_num) {
		printf("FSD does not lie within the partition!\n");
		error = EINVAL;
		goto bail;
	}

	mtx_init(&ump->um_hashmtx, IPL_NONE);
	ump->um_hashtbl = hashinit(UDF_HASHTBLSIZE, M_UDFMOUNT, M_WAITOK,
	    &ump->um_hashsz);
	arc4random_buf(&ump->um_hashkey, sizeof(ump->um_hashkey));

	/* Get the VAT, if needed */
	if (ump->um_flags & UDF_MNT_FIND_VAT) {
		error = udf_vat_get(ump, lb);
		if (error)
			goto bail;
	}

	/*
	 * Grab the Fileset Descriptor
	 * Thanks to Chuck McCrobie <mccrobie@cablespeed.com> for pointing
	 * me in the right direction here.
	 */

	if (ISSET(ump->um_flags, UDF_MNT_USES_META))
		sector = ump->um_meta_start; 
	else
		sector = fsd_offset;
	udf_vat_map(ump, &sector);
	if ((error = RDSECTOR(devvp, sector, ump->um_bsize, &bp)) != 0) {
		printf("Cannot read sector %d of FSD\n", sector);
		goto bail;
	}
	fsd = (struct fileset_desc *)bp->b_data;
	if (!udf_checktag(&fsd->tag, TAGID_FSD)) {
		fsd_found = 1;
		bcopy(&fsd->rootdir_icb, &ump->um_root_icb,
		    sizeof(struct long_ad));
		if (ISSET(ump->um_flags, UDF_MNT_USES_META)) {
			ump->um_root_icb.loc.lb_num += ump->um_meta_start; 
			ump->um_root_icb.loc.part_num = part_num;
		}
	}

	brelse(bp);
	bp = NULL;

	if (!fsd_found) {
		printf("Couldn't find the fsd\n");
		error = EINVAL;
		goto bail;
	}

	/*
	 * Find the file entry for the root directory.
	 */
	sector = letoh32(ump->um_root_icb.loc.lb_num);
	size = letoh32(ump->um_root_icb.len);
	udf_vat_map(ump, &sector);
	if ((error = udf_readlblks(ump, sector, size, &bp)) != 0) {
		printf("Cannot read sector %d\n", sector);
		goto bail;
	}

	xfentry = (struct extfile_entry *)bp->b_data;
	fentry = (struct file_entry *)bp->b_data;
	error = udf_checktag(&xfentry->tag, TAGID_EXTFENTRY);
	if (error) {
	    	error = udf_checktag(&fentry->tag, TAGID_FENTRY);
		if (error) {
			printf("Invalid root file entry!\n");
			goto bail;
		}
	}

	brelse(bp);
	bp = NULL;

	devvp->v_specmountpoint = mp;

	return (0);

bail:
	if (ump != NULL) {
		hashfree(ump->um_hashtbl, UDF_HASHTBLSIZE, M_UDFMOUNT);
		free(ump, M_UDFMOUNT, 0);
		mp->mnt_data = NULL;
		mp->mnt_flag &= ~MNT_LOCAL;
	}
	if (devvp->v_specinfo)
		devvp->v_specmountpoint = NULL;
	if (bp != NULL)
		brelse(bp);

	vn_lock(devvp, LK_EXCLUSIVE|LK_RETRY);
	VOP_CLOSE(devvp, FREAD, FSCRED, p);
	VOP_UNLOCK(devvp);

	return (error);
}

int
udf_unmount(struct mount *mp, int mntflags, struct proc *p)
{
	struct umount *ump;
	struct vnode *devvp;
	int error, flags = 0;

	ump = VFSTOUDFFS(mp);
	devvp = ump->um_devvp;

	if (mntflags & MNT_FORCE)
		flags |= FORCECLOSE;

	if ((error = vflush(mp, NULL, flags)))
		return (error);

	vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
	vinvalbuf(devvp, V_SAVE, NOCRED, p, 0, INFSLP);
	(void)VOP_CLOSE(devvp, FREAD, NOCRED, p);
	VOP_UNLOCK(devvp);

	devvp->v_specmountpoint = NULL;
	vrele(devvp);

	if (ump->um_flags & UDF_MNT_USES_VAT)
		free(ump->um_vat, M_UDFMOUNT, 0);

	if (ump->um_stbl != NULL)
		free(ump->um_stbl, M_UDFMOUNT, 0);

	hashfree(ump->um_hashtbl, UDF_HASHTBLSIZE, M_UDFMOUNT);
	free(ump, M_UDFMOUNT, 0);

	mp->mnt_data = NULL;
	mp->mnt_flag &= ~MNT_LOCAL;

	return (0);
}

int
udf_root(struct mount *mp, struct vnode **vpp)
{
	struct umount *ump;
	struct vnode *vp;
	udfino_t id;
	int error;

	ump = VFSTOUDFFS(mp);

	id = udf_getid(&ump->um_root_icb);

	error = udf_vget(mp, id, vpp);
	if (error)
		return (error);

	vp = *vpp;
	vp->v_flag |= VROOT;

	return (0);
}

int
udf_quotactl(struct mount *mp, int cmds, uid_t uid, caddr_t arg,
    struct proc *p)
{
	return (EOPNOTSUPP);
}

int
udf_statfs(struct mount *mp, struct statfs *sbp, struct proc *p)
{
	struct umount *ump;

	ump = VFSTOUDFFS(mp);

	sbp->f_bsize = ump->um_bsize;
	sbp->f_iosize = ump->um_bsize;
	sbp->f_blocks = ump->um_len;
	sbp->f_bfree = 0;
	sbp->f_bavail = 0;
	sbp->f_files = 0;
	sbp->f_ffree = 0;
	sbp->f_favail = 0;
	copy_statfs_info(sbp, mp);

	return (0);
}

int
udf_sync(struct mount *mp, int waitfor, int stall, struct ucred *cred, struct proc *p)
{
	return (0);
}

int
udf_vget(struct mount *mp, ino_t ino, struct vnode **vpp)
{
	struct buf *bp;
	struct vnode *devvp;
	struct umount *ump;
	struct proc *p;
	struct vnode *vp, *nvp;
	struct unode *up;
	struct extfile_entry *xfe;
	struct file_entry *fe;
	uint32_t sector;
	int error, size;

	if (ino > (udfino_t)-1)
		panic("udf_vget: alien ino_t %llu", (unsigned long long)ino);

	p = curproc;
	bp = NULL;
	*vpp = NULL;
	ump = VFSTOUDFFS(mp);

	/* See if we already have this in the cache */
	if ((error = udf_hashlookup(ump, ino, LK_EXCLUSIVE, vpp)) != 0)
		return (error);
	if (*vpp != NULL)
		return (0);

	/*
	 * Allocate memory and check the tag id's before grabbing a new
	 * vnode, since it's hard to roll back if there is a problem.
	 */
	up = pool_get(&unode_pool, PR_WAITOK | PR_ZERO);

	/*
	 * Copy in the file entry.  Per the spec, the size can only be 1 block.
	 */
	sector = ino;
	devvp = ump->um_devvp;
	udf_vat_map(ump, &sector);
	if ((error = RDSECTOR(devvp, sector, ump->um_bsize, &bp)) != 0) {
		printf("Cannot read sector %d\n", sector);
		pool_put(&unode_pool, up);
		if (bp != NULL)
			brelse(bp);
		return (error);
	}

	xfe = (struct extfile_entry *)bp->b_data;
	fe = (struct file_entry *)bp->b_data;
	error = udf_checktag(&xfe->tag, TAGID_EXTFENTRY);
	if (error == 0) {
		size = letoh32(xfe->l_ea) + letoh32(xfe->l_ad);
	} else {
		error = udf_checktag(&fe->tag, TAGID_FENTRY);
		if (error) {
			printf("Invalid file entry!\n");
			pool_put(&unode_pool, up);
			if (bp != NULL)
				brelse(bp);
			return (ENOMEM);
		} else
			size = letoh32(fe->l_ea) + letoh32(fe->l_ad);
	}

	/* Allocate max size of FE/XFE. */
	up->u_fentry = malloc(size + UDF_EXTFENTRY_SIZE, M_UDFFENTRY, M_NOWAIT | M_ZERO);
	if (up->u_fentry == NULL) {
		pool_put(&unode_pool, up);
		if (bp != NULL)
			brelse(bp);
		return (ENOMEM); /* Cannot allocate file entry block */
	}

	if (udf_checktag(&xfe->tag, TAGID_EXTFENTRY) == 0)
		bcopy(bp->b_data, up->u_fentry, size + UDF_EXTFENTRY_SIZE);
	else
		bcopy(bp->b_data, up->u_fentry, size + UDF_FENTRY_SIZE);
	
	brelse(bp);
	bp = NULL;

	if ((error = udf_allocv(mp, &vp, p))) {
		free(up->u_fentry, M_UDFFENTRY, 0);
		pool_put(&unode_pool, up);
		return (error); /* Error from udf_allocv() */
	}

	up->u_vnode = vp;
	up->u_ino = ino;
	up->u_devvp = ump->um_devvp;
	up->u_dev = ump->um_dev;
	up->u_ump = ump;
	vp->v_data = up;
	vref(ump->um_devvp);

	rrw_init_flags(&up->u_lock, "unode", RWL_DUPOK | RWL_IS_VNODE);

	/*
	 * udf_hashins() will lock the vnode for us.
	 */
	udf_hashins(up);

	switch (up->u_fentry->icbtag.file_type) {
	default:
		printf("Unrecognized file type (%d)\n", vp->v_type);
		vp->v_type = VREG;
		break;
	case UDF_ICB_FILETYPE_DIRECTORY:
		vp->v_type = VDIR;
		break;
	case UDF_ICB_FILETYPE_BLOCKDEVICE:
		vp->v_type = VBLK;
		break;
	case UDF_ICB_FILETYPE_CHARDEVICE:
		vp->v_type = VCHR;
		break;
	case UDF_ICB_FILETYPE_FIFO:
		vp->v_type = VFIFO;
		break;
	case UDF_ICB_FILETYPE_SOCKET:
		vp->v_type = VSOCK;
		break;
	case UDF_ICB_FILETYPE_SYMLINK:
		vp->v_type = VLNK;
		break;
	case UDF_ICB_FILETYPE_RANDOMACCESS:
	case UDF_ICB_FILETYPE_REALTIME:
	case UDF_ICB_FILETYPE_UNKNOWN:
		vp->v_type = VREG;
		break;
	}

	/* check if this is a vnode alias */
	if ((nvp = checkalias(vp, up->u_dev, ump->um_mountp)) != NULL) {
		printf("found a vnode alias\n");
		/*
		 * Discard unneeded vnode, but save its udf_node.
		 * Note that the lock is carried over in the udf_node
		 */
		nvp->v_data = vp->v_data;
		vp->v_data = NULL;
		vp->v_op = &spec_vops;
		vrele(vp);
		vgone(vp);
		/*
		 * Reinitialize aliased inode.
		 */
		vp = nvp;
		ump->um_devvp = vp;
	}

	*vpp = vp;

	return (0);
}

struct ifid {
	u_short	ifid_len;
	u_short	ifid_pad;
	int	ifid_ino;
	long	ifid_start;
};

int
udf_fhtovp(struct mount *mp, struct fid *fhp, struct vnode **vpp)
{
	struct ifid *ifhp;
	struct vnode *nvp;
	int error;

	ifhp = (struct ifid *)fhp;

	if ((error = VFS_VGET(mp, ifhp->ifid_ino, &nvp)) != 0) {
		*vpp = NULL;
		return (error);
	}

	*vpp = nvp;

	return (0);
}

int
udf_vptofh(struct vnode *vp, struct fid *fhp)
{
	struct unode *up;
	struct ifid *ifhp;

	up = VTOU(vp);
	ifhp = (struct ifid *)fhp;
	ifhp->ifid_len = sizeof(struct ifid);
	ifhp->ifid_ino = up->u_ino;

	return (0);
}

int
udf_checkexp(struct mount *mp, struct mbuf *nam, int *exflagsp,
    struct ucred **credanonp)
{
	return (EACCES); /* For the time being */
}

/* Handle a virtual partition map */
int
udf_get_vpartmap(struct umount *ump, struct part_map_virt *pmv)
{
	ump->um_flags |= UDF_MNT_FIND_VAT; /* Should do more than this */
	return (0);
}

/* Handle a sparable partition map */
int
udf_get_spartmap(struct umount *ump, struct part_map_spare *pms)
{
	struct buf *bp;
	int i, error;

	ump->um_stbl = malloc(letoh32(pms->st_size), M_UDFMOUNT, M_NOWAIT);
	if (ump->um_stbl == NULL)
		return (ENOMEM);

	bzero(ump->um_stbl, letoh32(pms->st_size));

	/* Calculate the number of sectors per packet */
	ump->um_psecs = letoh16(pms->packet_len) / ump->um_bsize;

	error = udf_readlblks(ump, letoh32(pms->st_loc[0]),
	    letoh32(pms->st_size), &bp);

	if (error) {
		if (bp != NULL)
			brelse(bp);
		free(ump->um_stbl, M_UDFMOUNT, 0);
		return (error); /* Failed to read sparing table */
	}

	bcopy(bp->b_data, ump->um_stbl, letoh32(pms->st_size));
	brelse(bp);
	bp = NULL;

	if (udf_checktag(&ump->um_stbl->tag, 0)) {
		free(ump->um_stbl, M_UDFMOUNT, 0);
		return (EINVAL); /* Invalid sparing table found */
	}

	/*
	 * See how many valid entries there are here. The list is
	 * supposed to be sorted, 0xfffffff0 and higher are not valid.
	 */
	for (i = 0; i < letoh16(ump->um_stbl->rt_l); i++) {
		ump->um_stbl_len = i;
		if (letoh32(ump->um_stbl->entries[i].org) >= 0xfffffff0)
			break;
	}

	return (0);
}

/* Handle a metadata partition map */
int
udf_get_mpartmap(struct umount *ump, struct part_map_meta *pmm)
{
	ump->um_flags |= UDF_MNT_USES_META;
	ump->um_meta_start = pmm->meta_file_lbn;
	return (0);
}

/* Scan the partition maps */
int
udf_find_partmaps(struct umount *ump, struct logvol_desc *lvd)
{
	struct regid *pmap_id;
	unsigned char regid_id[UDF_REGID_ID_SIZE + 1];
	int i, ptype, psize, error;
	uint8_t *pmap = (uint8_t *) &lvd->maps[0];

	for (i = 0; i < letoh32(lvd->n_pm); i++) {
		ptype = pmap[0];
		psize = pmap[1];

		if (ptype != 1 && ptype != 2)
			return (EINVAL); /* Invalid partition map type */

		if (psize != sizeof(struct part_map_1)  &&
		    psize != sizeof(struct part_map_2))
			return (EINVAL); /* Invalid partition map size */

		if (ptype == 1) {
			pmap += sizeof(struct part_map_1);
			continue;
		}

		/* Type 2 map. Find out the details */
		pmap_id = (struct regid *) &pmap[4];
		regid_id[UDF_REGID_ID_SIZE] = '\0';
		bcopy(&pmap_id->id[0], &regid_id[0], UDF_REGID_ID_SIZE);

		if (!bcmp(&regid_id[0], "*UDF Virtual Partition",
		    UDF_REGID_ID_SIZE))
			error = udf_get_vpartmap(ump,
			    (struct part_map_virt *) pmap);
		else if (!bcmp(&regid_id[0], "*UDF Sparable Partition",
		    UDF_REGID_ID_SIZE))
			error = udf_get_spartmap(ump,
			    (struct part_map_spare *) pmap);
		else if (!bcmp(&regid_id[0], "*UDF Metadata Partition",
		    UDF_REGID_ID_SIZE))
			error = udf_get_mpartmap(ump,
			    (struct part_map_meta *) pmap);
		else
			return (EINVAL); /* Unsupported partition map */

		if (error)
			return (error); /* Error getting partition */

		pmap += sizeof(struct part_map_2);
	}

	return (0);
}
