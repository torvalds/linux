/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley
 * by Pace Willisson (pace@blitz.com).  The Rock Ridge Extension
 * Support code is derived from software contributed to Berkeley
 * by Atsushi Murai (amurai@spec.co.jp).
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
 *	@(#)cd9660_vfsops.c	8.18 (Berkeley) 5/22/95
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/namei.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/cdio.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/malloc.h>
#include <sys/stat.h>
#include <sys/syslog.h>
#include <sys/iconv.h>

#include <fs/cd9660/iso.h>
#include <fs/cd9660/iso_rrip.h>
#include <fs/cd9660/cd9660_node.h>
#include <fs/cd9660/cd9660_mount.h>

#include <geom/geom.h>
#include <geom/geom_vfs.h>

MALLOC_DEFINE(M_ISOFSMNT, "isofs_mount", "ISOFS mount structure");
MALLOC_DEFINE(M_ISOFSNODE, "isofs_node", "ISOFS vnode private part");

struct iconv_functions *cd9660_iconv = NULL;

static vfs_mount_t	cd9660_mount;
static vfs_cmount_t	cd9660_cmount;
static vfs_unmount_t	cd9660_unmount;
static vfs_root_t	cd9660_root;
static vfs_statfs_t	cd9660_statfs;
static vfs_vget_t	cd9660_vget;
static vfs_fhtovp_t	cd9660_fhtovp;

static struct vfsops cd9660_vfsops = {
	.vfs_fhtovp =		cd9660_fhtovp,
	.vfs_mount =		cd9660_mount,
	.vfs_cmount =		cd9660_cmount,
	.vfs_root =		cd9660_root,
	.vfs_statfs =		cd9660_statfs,
	.vfs_unmount =		cd9660_unmount,
	.vfs_vget =		cd9660_vget,
};
VFS_SET(cd9660_vfsops, cd9660, VFCF_READONLY);
MODULE_VERSION(cd9660, 1);

static int cd9660_vfs_hash_cmp(struct vnode *vp, void *pino);
static int iso_mountfs(struct vnode *devvp, struct mount *mp);

/*
 * VFS Operations.
 */

static int
cd9660_cmount(struct mntarg *ma, void *data, uint64_t flags)
{
	struct iso_args args;
	struct export_args exp;
	int error;

	error = copyin(data, &args, sizeof args);
	if (error)
		return (error);
	vfs_oexport_conv(&args.export, &exp);

	ma = mount_argsu(ma, "from", args.fspec, MAXPATHLEN);
	ma = mount_arg(ma, "export", &exp, sizeof(exp));
	ma = mount_argsu(ma, "cs_disk", args.cs_disk, 64);
	ma = mount_argsu(ma, "cs_local", args.cs_local, 64);
	ma = mount_argf(ma, "ssector", "%u", args.ssector);
	ma = mount_argb(ma, !(args.flags & ISOFSMNT_NORRIP), "norrip");
	ma = mount_argb(ma, args.flags & ISOFSMNT_GENS, "nogens");
	ma = mount_argb(ma, args.flags & ISOFSMNT_EXTATT, "noextatt");
	ma = mount_argb(ma, !(args.flags & ISOFSMNT_NOJOLIET), "nojoliet");
	ma = mount_argb(ma,
	    args.flags & ISOFSMNT_BROKENJOLIET, "nobrokenjoliet");
	ma = mount_argb(ma, args.flags & ISOFSMNT_KICONV, "nokiconv");

	error = kernel_mount(ma, flags);

	return (error);
}

static int
cd9660_mount(struct mount *mp)
{
	struct vnode *devvp;
	struct thread *td;
	char *fspec;
	int error;
	accmode_t accmode;
	struct nameidata ndp;
	struct iso_mnt *imp = NULL;

	td = curthread;

	/*
	 * Unconditionally mount as read-only.
	 */
	MNT_ILOCK(mp);
	mp->mnt_flag |= MNT_RDONLY;
	MNT_IUNLOCK(mp);

	fspec = vfs_getopts(mp->mnt_optnew, "from", &error);
	if (error)
		return (error);

	imp = VFSTOISOFS(mp);

	if (mp->mnt_flag & MNT_UPDATE) {
		if (vfs_flagopt(mp->mnt_optnew, "export", NULL, 0))
			return (0);
	}
	/*
	 * Not an update, or updating the name: look up the name
	 * and verify that it refers to a sensible block device.
	 */
	NDINIT(&ndp, LOOKUP, FOLLOW | LOCKLEAF, UIO_SYSSPACE, fspec, td);
	if ((error = namei(&ndp)))
		return (error);
	NDFREE(&ndp, NDF_ONLY_PNBUF);
	devvp = ndp.ni_vp;

	if (!vn_isdisk(devvp, &error)) {
		vput(devvp);
		return (error);
	}

	/*
	 * Verify that user has necessary permissions on the device,
	 * or has superuser abilities
	 */
	accmode = VREAD;
	error = VOP_ACCESS(devvp, accmode, td->td_ucred, td);
	if (error)
		error = priv_check(td, PRIV_VFS_MOUNT_PERM);
	if (error) {
		vput(devvp);
		return (error);
	}

	if ((mp->mnt_flag & MNT_UPDATE) == 0) {
		error = iso_mountfs(devvp, mp);
		if (error)
			vrele(devvp);
	} else {
		if (devvp != imp->im_devvp)
			error = EINVAL;	/* needs translation */
		vput(devvp);
	}
	if (error)
		return (error);
	vfs_mountedfrom(mp, fspec);
	return (0);
}

/*
 * Common code for mount and mountroot
 */
static int
iso_mountfs(devvp, mp)
	struct vnode *devvp;
	struct mount *mp;
{
	struct iso_mnt *isomp = NULL;
	struct buf *bp = NULL;
	struct buf *pribp = NULL, *supbp = NULL;
	struct cdev *dev;
	int error = EINVAL;
	int high_sierra = 0;
	int iso_bsize;
	int iso_blknum;
	int joliet_level;
	int isverified = 0;
	struct iso_volume_descriptor *vdp = NULL;
	struct iso_primary_descriptor *pri = NULL;
	struct iso_sierra_primary_descriptor *pri_sierra = NULL;
	struct iso_supplementary_descriptor *sup = NULL;
	struct iso_directory_record *rootp;
	int logical_block_size, ssector;
	struct g_consumer *cp;
	struct bufobj *bo;
	char *cs_local, *cs_disk;

	dev = devvp->v_rdev;
	dev_ref(dev);
	g_topology_lock();
	error = g_vfs_open(devvp, &cp, "cd9660", 0);
	if (error == 0)
		g_getattr("MNT::verified", cp, &isverified);
	g_topology_unlock();
	VOP_UNLOCK(devvp, 0);
	if (error)
		goto out;
	if (devvp->v_rdev->si_iosize_max != 0)
		mp->mnt_iosize_max = devvp->v_rdev->si_iosize_max;
	if (mp->mnt_iosize_max > MAXPHYS)
		mp->mnt_iosize_max = MAXPHYS;

	bo = &devvp->v_bufobj;

	/* This is the "logical sector size".  The standard says this
	 * should be 2048 or the physical sector size on the device,
	 * whichever is greater.
	 */
	if ((ISO_DEFAULT_BLOCK_SIZE % cp->provider->sectorsize) != 0) {
		error = EINVAL;
		goto out;
	}

	iso_bsize = cp->provider->sectorsize;

	joliet_level = 0;
	if (1 != vfs_scanopt(mp->mnt_optnew, "ssector", "%d", &ssector))
		ssector = 0;
	for (iso_blknum = 16 + ssector;
	     iso_blknum < 100 + ssector;
	     iso_blknum++) {
		if ((error = bread(devvp, iso_blknum * btodb(ISO_DEFAULT_BLOCK_SIZE),
				  iso_bsize, NOCRED, &bp)) != 0)
			goto out;

		vdp = (struct iso_volume_descriptor *)bp->b_data;
		if (bcmp (vdp->id, ISO_STANDARD_ID, sizeof vdp->id) != 0) {
			if (bcmp (vdp->id_sierra, ISO_SIERRA_ID,
				  sizeof vdp->id_sierra) != 0) {
				error = EINVAL;
				goto out;
			} else
				high_sierra = 1;
		}
		switch (isonum_711 (high_sierra? vdp->type_sierra: vdp->type)){
		case ISO_VD_PRIMARY:
			if (pribp == NULL) {
				pribp = bp;
				bp = NULL;
				pri = (struct iso_primary_descriptor *)vdp;
				pri_sierra =
				  (struct iso_sierra_primary_descriptor *)vdp;
			}
			break;

		case ISO_VD_SUPPLEMENTARY:
			if (supbp == NULL) {
				supbp = bp;
				bp = NULL;
				sup = (struct iso_supplementary_descriptor *)vdp;

				if (!vfs_flagopt(mp->mnt_optnew, "nojoliet", NULL, 0)) {
					if (bcmp(sup->escape, "%/@", 3) == 0)
						joliet_level = 1;
					if (bcmp(sup->escape, "%/C", 3) == 0)
						joliet_level = 2;
					if (bcmp(sup->escape, "%/E", 3) == 0)
						joliet_level = 3;

					if ((isonum_711 (sup->flags) & 1) &&
					    !vfs_flagopt(mp->mnt_optnew, "brokenjoliet", NULL, 0))
						joliet_level = 0;
				}
			}
			break;

		case ISO_VD_END:
			goto vd_end;

		default:
			break;
		}
		if (bp != NULL) {
			brelse(bp);
			bp = NULL;
		}
	}
 vd_end:
	if (bp != NULL) {
		brelse(bp);
		bp = NULL;
	}

	if (pri == NULL) {
		error = EINVAL;
		goto out;
	}

	logical_block_size =
		isonum_723 (high_sierra?
			    pri_sierra->logical_block_size:
			    pri->logical_block_size);

	if (logical_block_size < DEV_BSIZE || logical_block_size > MAXBSIZE
	    || (logical_block_size & (logical_block_size - 1)) != 0) {
		error = EINVAL;
		goto out;
	}

	rootp = (struct iso_directory_record *)
		(high_sierra?
		 pri_sierra->root_directory_record:
		 pri->root_directory_record);

	isomp = malloc(sizeof *isomp, M_ISOFSMNT, M_WAITOK | M_ZERO);
	isomp->im_cp = cp;
	isomp->im_bo = bo;
	isomp->logical_block_size = logical_block_size;
	isomp->volume_space_size =
		isonum_733 (high_sierra?
			    pri_sierra->volume_space_size:
			    pri->volume_space_size);
	isomp->joliet_level = 0;
	/*
	 * Since an ISO9660 multi-session CD can also access previous
	 * sessions, we have to include them into the space consider-
	 * ations.  This doesn't yield a very accurate number since
	 * parts of the old sessions might be inaccessible now, but we
	 * can't do much better.  This is also important for the NFS
	 * filehandle validation.
	 */
	isomp->volume_space_size += ssector;
	memcpy(isomp->root, rootp, sizeof isomp->root);
	isomp->root_extent = isonum_733 (rootp->extent);
	isomp->root_size = isonum_733 (rootp->size);

	isomp->im_bmask = logical_block_size - 1;
	isomp->im_bshift = ffs(logical_block_size) - 1;

	pribp->b_flags |= B_AGE;
	brelse(pribp);
	pribp = NULL;
	rootp = NULL;
	pri = NULL;
	pri_sierra = NULL;

	mp->mnt_data = isomp;
	mp->mnt_stat.f_fsid.val[0] = dev2udev(dev);
	mp->mnt_stat.f_fsid.val[1] = mp->mnt_vfc->vfc_typenum;
	mp->mnt_maxsymlinklen = 0;
	MNT_ILOCK(mp);
	if (isverified)
		mp->mnt_flag |= MNT_VERIFIED;
	mp->mnt_flag |= MNT_LOCAL;
	mp->mnt_kern_flag |= MNTK_LOOKUP_SHARED | MNTK_EXTENDED_SHARED;
	MNT_IUNLOCK(mp);
	isomp->im_mountp = mp;
	isomp->im_dev = dev;
	isomp->im_devvp = devvp;

	vfs_flagopt(mp->mnt_optnew, "norrip", &isomp->im_flags, ISOFSMNT_NORRIP);
	vfs_flagopt(mp->mnt_optnew, "gens", &isomp->im_flags, ISOFSMNT_GENS);
	vfs_flagopt(mp->mnt_optnew, "extatt", &isomp->im_flags, ISOFSMNT_EXTATT);
	vfs_flagopt(mp->mnt_optnew, "nojoliet", &isomp->im_flags, ISOFSMNT_NOJOLIET);
	vfs_flagopt(mp->mnt_optnew, "kiconv", &isomp->im_flags, ISOFSMNT_KICONV);

	/* Check the Rock Ridge Extension support */
	if (!(isomp->im_flags & ISOFSMNT_NORRIP)) {
		if ((error = bread(isomp->im_devvp, (isomp->root_extent +
		    isonum_711(((struct iso_directory_record *)isomp->root)->
		    ext_attr_length)) << (isomp->im_bshift - DEV_BSHIFT),
		    isomp->logical_block_size, NOCRED, &bp)) != 0)
			goto out;

		rootp = (struct iso_directory_record *)bp->b_data;

		if ((isomp->rr_skip = cd9660_rrip_offset(rootp,isomp)) < 0) {
		    isomp->im_flags |= ISOFSMNT_NORRIP;
		} else {
		    isomp->im_flags &= ~ISOFSMNT_GENS;
		}

		/*
		 * The contents are valid,
		 * but they will get reread as part of another vnode, so...
		 */
		bp->b_flags |= B_AGE;
		brelse(bp);
		bp = NULL;
		rootp = NULL;
	}

	if (isomp->im_flags & ISOFSMNT_KICONV && cd9660_iconv) {
		cs_local = vfs_getopts(mp->mnt_optnew, "cs_local", &error);
		if (error)
			goto out;
		cs_disk = vfs_getopts(mp->mnt_optnew, "cs_disk", &error);
		if (error)
			goto out;
		cd9660_iconv->open(cs_local, cs_disk, &isomp->im_d2l);
		cd9660_iconv->open(cs_disk, cs_local, &isomp->im_l2d);
	} else {
		isomp->im_d2l = NULL;
		isomp->im_l2d = NULL;
	}

	if (high_sierra) {
		/* this effectively ignores all the mount flags */
		if (bootverbose)
			log(LOG_INFO, "cd9660: High Sierra Format\n");
		isomp->iso_ftype = ISO_FTYPE_HIGH_SIERRA;
	} else
		switch (isomp->im_flags&(ISOFSMNT_NORRIP|ISOFSMNT_GENS)) {
		  default:
			  isomp->iso_ftype = ISO_FTYPE_DEFAULT;
			  break;
		  case ISOFSMNT_GENS|ISOFSMNT_NORRIP:
			  isomp->iso_ftype = ISO_FTYPE_9660;
			  break;
		  case 0:
			  if (bootverbose)
			  	  log(LOG_INFO, "cd9660: RockRidge Extension\n");
			  isomp->iso_ftype = ISO_FTYPE_RRIP;
			  break;
		}

	/* Decide whether to use the Joliet descriptor */

	if (isomp->iso_ftype != ISO_FTYPE_RRIP && joliet_level) {
		if (bootverbose)
			log(LOG_INFO, "cd9660: Joliet Extension (Level %d)\n",
			    joliet_level);
		rootp = (struct iso_directory_record *)
			sup->root_directory_record;
		memcpy(isomp->root, rootp, sizeof isomp->root);
		isomp->root_extent = isonum_733 (rootp->extent);
		isomp->root_size = isonum_733 (rootp->size);
		isomp->joliet_level = joliet_level;
		supbp->b_flags |= B_AGE;
	}

	if (supbp) {
		brelse(supbp);
		supbp = NULL;
		sup = NULL;
	}

	return 0;
out:
	if (bp != NULL)
		brelse(bp);
	if (pribp != NULL)
		brelse(pribp);
	if (supbp != NULL)
		brelse(supbp);
	if (cp != NULL) {
		g_topology_lock();
		g_vfs_close(cp);
		g_topology_unlock();
	}
	if (isomp) {
		free(isomp, M_ISOFSMNT);
		mp->mnt_data = NULL;
	}
	dev_rel(dev);
	return error;
}

/*
 * unmount system call
 */
static int
cd9660_unmount(mp, mntflags)
	struct mount *mp;
	int mntflags;
{
	struct iso_mnt *isomp;
	int error, flags = 0;

	if (mntflags & MNT_FORCE)
		flags |= FORCECLOSE;
	if ((error = vflush(mp, 0, flags, curthread)))
		return (error);

	isomp = VFSTOISOFS(mp);

	if (isomp->im_flags & ISOFSMNT_KICONV && cd9660_iconv) {
		if (isomp->im_d2l)
			cd9660_iconv->close(isomp->im_d2l);
		if (isomp->im_l2d)
			cd9660_iconv->close(isomp->im_l2d);
	}
	g_topology_lock();
	g_vfs_close(isomp->im_cp);
	g_topology_unlock();
	vrele(isomp->im_devvp);
	dev_rel(isomp->im_dev);
	free(isomp, M_ISOFSMNT);
	mp->mnt_data = NULL;
	MNT_ILOCK(mp);
	mp->mnt_flag &= ~MNT_LOCAL;
	MNT_IUNLOCK(mp);
	return (error);
}

/*
 * Return root of a filesystem
 */
static int
cd9660_root(mp, flags, vpp)
	struct mount *mp;
	int flags;
	struct vnode **vpp;
{
	struct iso_mnt *imp = VFSTOISOFS(mp);
	struct iso_directory_record *dp =
	    (struct iso_directory_record *)imp->root;
	cd_ino_t ino = isodirino(dp, imp);

	/*
	 * With RRIP we must use the `.' entry of the root directory.
	 * Simply tell vget, that it's a relocated directory.
	 */
	return (cd9660_vget_internal(mp, ino, flags, vpp,
	    imp->iso_ftype == ISO_FTYPE_RRIP, dp));
}

/*
 * Get filesystem statistics.
 */
static int
cd9660_statfs(mp, sbp)
	struct mount *mp;
	struct statfs *sbp;
{
	struct iso_mnt *isomp;

	isomp = VFSTOISOFS(mp);

	sbp->f_bsize = isomp->logical_block_size;
	sbp->f_iosize = sbp->f_bsize;	/* XXX */
	sbp->f_blocks = isomp->volume_space_size;
	sbp->f_bfree = 0; /* total free blocks */
	sbp->f_bavail = 0; /* blocks free for non superuser */
	sbp->f_files =	0; /* total files */
	sbp->f_ffree = 0; /* free file nodes */
	return 0;
}

/*
 * File handle to vnode
 *
 * Have to be really careful about stale file handles:
 * - check that the inode number is in range
 * - call iget() to get the locked inode
 * - check for an unallocated inode (i_mode == 0)
 * - check that the generation number matches
 */

/* ARGSUSED */
static int
cd9660_fhtovp(mp, fhp, flags, vpp)
	struct mount *mp;
	struct fid *fhp;
	int flags;
	struct vnode **vpp;
{
	struct ifid ifh;
	struct iso_node *ip;
	struct vnode *nvp;
	int error;

	memcpy(&ifh, fhp, sizeof(ifh));

#ifdef	ISOFS_DBG
	printf("fhtovp: ino %d, start %ld\n",
	    ifh.ifid_ino, ifh.ifid_start);
#endif

	if ((error = VFS_VGET(mp, ifh.ifid_ino, LK_EXCLUSIVE, &nvp)) != 0) {
		*vpp = NULLVP;
		return (error);
	}
	ip = VTOI(nvp);
	if (ip->inode.iso_mode == 0) {
		vput(nvp);
		*vpp = NULLVP;
		return (ESTALE);
	}
	*vpp = nvp;
	vnode_create_vobject(*vpp, ip->i_size, curthread);
	return (0);
}

/*
 * Conform to standard VFS interface; can't vget arbitrary inodes beyond 4GB
 * into media with current inode scheme and 32-bit ino_t.  This shouldn't be
 * needed for anything other than nfsd, and who exports a mounted DVD over NFS?
 */
static int
cd9660_vget(mp, ino, flags, vpp)
	struct mount *mp;
	ino_t ino;
	int flags;
	struct vnode **vpp;
{

	/*
	 * XXXX
	 * It would be nice if we didn't always set the `relocated' flag
	 * and force the extra read, but I don't want to think about fixing
	 * that right now.
	 */
	return (cd9660_vget_internal(mp, ino, flags, vpp,
#if 0
	    VFSTOISOFS(mp)->iso_ftype == ISO_FTYPE_RRIP,
#else
	    0,
#endif
	    (struct iso_directory_record *)0));
}

/* Use special comparator for full 64-bit ino comparison. */
static int
cd9660_vfs_hash_cmp(vp, pino)
	struct vnode *vp;
	void *pino;
{
	struct iso_node *ip;
	cd_ino_t ino;

	ip = VTOI(vp);
	ino = *(cd_ino_t *)pino;
	return (ip->i_number != ino);
}

int
cd9660_vget_internal(mp, ino, flags, vpp, relocated, isodir)
	struct mount *mp;
	cd_ino_t ino;
	int flags;
	struct vnode **vpp;
	int relocated;
	struct iso_directory_record *isodir;
{
	struct iso_mnt *imp;
	struct iso_node *ip;
	struct buf *bp;
	struct vnode *vp;
	int error;
	struct thread *td;

	td = curthread;
	error = vfs_hash_get(mp, ino, flags, td, vpp, cd9660_vfs_hash_cmp,
	    &ino);
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

	imp = VFSTOISOFS(mp);

	/* Allocate a new vnode/iso_node. */
	if ((error = getnewvnode("isofs", mp, &cd9660_vnodeops, &vp)) != 0) {
		*vpp = NULLVP;
		return (error);
	}
	ip = malloc(sizeof(struct iso_node), M_ISOFSNODE,
	    M_WAITOK | M_ZERO);
	vp->v_data = ip;
	ip->i_vnode = vp;
	ip->i_number = ino;

	lockmgr(vp->v_vnlock, LK_EXCLUSIVE, NULL);
	error = insmntque(vp, mp);
	if (error != 0) {
		free(ip, M_ISOFSNODE);
		*vpp = NULLVP;
		return (error);
	}
	error = vfs_hash_insert(vp, ino, flags, td, vpp, cd9660_vfs_hash_cmp,
	    &ino);
	if (error || *vpp != NULL)
		return (error);

	if (isodir == NULL) {
		int lbn, off;

		lbn = lblkno(imp, ino);
		if (lbn >= imp->volume_space_size) {
			vput(vp);
			printf("fhtovp: lbn exceed volume space %d\n", lbn);
			return (ESTALE);
		}

		off = blkoff(imp, ino);
		if (off + ISO_DIRECTORY_RECORD_SIZE > imp->logical_block_size) {
			vput(vp);
			printf("fhtovp: crosses block boundary %d\n",
			       off + ISO_DIRECTORY_RECORD_SIZE);
			return (ESTALE);
		}

		error = bread(imp->im_devvp,
			      lbn << (imp->im_bshift - DEV_BSHIFT),
			      imp->logical_block_size, NOCRED, &bp);
		if (error) {
			vput(vp);
			brelse(bp);
			printf("fhtovp: bread error %d\n",error);
			return (error);
		}
		isodir = (struct iso_directory_record *)(bp->b_data + off);

		if (off + isonum_711(isodir->length) >
		    imp->logical_block_size) {
			vput(vp);
			brelse(bp);
			printf("fhtovp: directory crosses block boundary %d[off=%d/len=%d]\n",
			       off +isonum_711(isodir->length), off,
			       isonum_711(isodir->length));
			return (ESTALE);
		}

#if 0
		if (isonum_733(isodir->extent) +
		    isonum_711(isodir->ext_attr_length) != ifhp->ifid_start) {
			brelse(bp);
			printf("fhtovp: file start miss %d vs %d\n",
			       isonum_733(isodir->extent) + isonum_711(isodir->ext_attr_length),
			       ifhp->ifid_start);
			return (ESTALE);
		}
#endif
	} else
		bp = NULL;

	ip->i_mnt = imp;

	if (relocated) {
		/*
		 * On relocated directories we must
		 * read the `.' entry out of a dir.
		 */
		ip->iso_start = ino >> imp->im_bshift;
		if (bp != NULL)
			brelse(bp);
		if ((error = cd9660_blkatoff(vp, (off_t)0, NULL, &bp)) != 0) {
			vput(vp);
			return (error);
		}
		isodir = (struct iso_directory_record *)bp->b_data;
	}

	ip->iso_extent = isonum_733(isodir->extent);
	ip->i_size = isonum_733(isodir->size);
	ip->iso_start = isonum_711(isodir->ext_attr_length) + ip->iso_extent;

	/*
	 * Setup time stamp, attribute
	 */
	vp->v_type = VNON;
	switch (imp->iso_ftype) {
	default:	/* ISO_FTYPE_9660 */
	    {
		struct buf *bp2;
		int off;
		if ((imp->im_flags & ISOFSMNT_EXTATT)
		    && (off = isonum_711(isodir->ext_attr_length)))
			cd9660_blkatoff(vp, (off_t)-(off << imp->im_bshift), NULL,
				     &bp2);
		else
			bp2 = NULL;
		cd9660_defattr(isodir, ip, bp2, ISO_FTYPE_9660);
		cd9660_deftstamp(isodir, ip, bp2, ISO_FTYPE_9660);
		if (bp2)
			brelse(bp2);
		break;
	    }
	case ISO_FTYPE_RRIP:
		cd9660_rrip_analyze(isodir, ip, imp);
		break;
	}

	brelse(bp);

	/*
	 * Initialize the associated vnode
	 */
	switch (vp->v_type = IFTOVT(ip->inode.iso_mode)) {
	case VFIFO:
		vp->v_op = &cd9660_fifoops;
		break;
	default:
		VN_LOCK_ASHARE(vp);
		break;
	}

	if (ip->iso_extent == imp->root_extent)
		vp->v_vflag |= VV_ROOT;

	/*
	 * XXX need generation number?
	 */

	*vpp = vp;
	return (0);
}
