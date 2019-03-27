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
 *	@(#)cd9660_vnops.c	8.19 (Berkeley) 5/27/95
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/namei.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/stat.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/malloc.h>
#include <sys/dirent.h>
#include <sys/unistd.h>
#include <sys/filio.h>
#include <sys/sysctl.h>

#include <vm/vm.h>
#include <vm/vnode_pager.h>
#include <vm/uma.h>

#include <fs/cd9660/iso.h>
#include <fs/cd9660/cd9660_node.h>
#include <fs/cd9660/iso_rrip.h>

static vop_setattr_t	cd9660_setattr;
static vop_open_t	cd9660_open;
static vop_access_t	cd9660_access;
static vop_getattr_t	cd9660_getattr;
static vop_ioctl_t	cd9660_ioctl;
static vop_pathconf_t	cd9660_pathconf;
static vop_read_t	cd9660_read;
struct isoreaddir;
static int iso_uiodir(struct isoreaddir *idp, struct dirent *dp, off_t off);
static int iso_shipdir(struct isoreaddir *idp);
static vop_readdir_t	cd9660_readdir;
static vop_readlink_t	cd9660_readlink;
static vop_strategy_t	cd9660_strategy;
static vop_vptofh_t	cd9660_vptofh;
static vop_getpages_t	cd9660_getpages;

/*
 * Setattr call. Only allowed for block and character special devices.
 */
static int
cd9660_setattr(ap)
	struct vop_setattr_args /* {
		struct vnodeop_desc *a_desc;
		struct vnode *a_vp;
		struct vattr *a_vap;
		struct ucred *a_cred;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	struct vattr *vap = ap->a_vap;

	if (vap->va_flags != (u_long)VNOVAL || vap->va_uid != (uid_t)VNOVAL ||
	    vap->va_gid != (gid_t)VNOVAL || vap->va_atime.tv_sec != VNOVAL ||
	    vap->va_mtime.tv_sec != VNOVAL || vap->va_mode != (mode_t)VNOVAL)
		return (EROFS);
	if (vap->va_size != (u_quad_t)VNOVAL) {
		switch (vp->v_type) {
		case VDIR:
			return (EISDIR);
		case VLNK:
		case VREG:
			return (EROFS);
		case VCHR:
		case VBLK:
		case VSOCK:
		case VFIFO:
		case VNON:
		case VBAD:
		case VMARKER:
			return (0);
		}
	}
	return (0);
}

/*
 * Check mode permission on inode pointer. Mode is READ, WRITE or EXEC.
 * The mode is shifted to select the owner/group/other fields. The
 * super user is granted all permissions.
 */
/* ARGSUSED */
static int
cd9660_access(ap)
	struct vop_access_args /* {
		struct vnode *a_vp;
		accmode_t a_accmode;
		struct ucred *a_cred;
		struct thread *a_td;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	struct iso_node *ip = VTOI(vp);
	accmode_t accmode = ap->a_accmode;

	if (vp->v_type == VCHR || vp->v_type == VBLK)
		return (EOPNOTSUPP);

	/*
	 * Disallow write attempts unless the file is a socket,
	 * fifo, or a block or character device resident on the
	 * filesystem.
	 */
	if (accmode & VWRITE) {
		switch (vp->v_type) {
		case VDIR:
		case VLNK:
		case VREG:
			return (EROFS);
			/* NOT REACHED */
		default:
			break;
		}
	}

	return (vaccess(vp->v_type, ip->inode.iso_mode, ip->inode.iso_uid,
	    ip->inode.iso_gid, ap->a_accmode, ap->a_cred, NULL));
}

static int
cd9660_open(ap)
	struct vop_open_args /* {
		struct vnode *a_vp;
		int a_mode;
		struct ucred *a_cred;
		struct thread *a_td;
		struct file *a_fp;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	struct iso_node *ip = VTOI(vp);

	if (vp->v_type == VCHR || vp->v_type == VBLK)
		return (EOPNOTSUPP);

	vnode_create_vobject(vp, ip->i_size, ap->a_td);
	return (0);
}


static int
cd9660_getattr(ap)
	struct vop_getattr_args /* {
		struct vnode *a_vp;
		struct vattr *a_vap;
		struct ucred *a_cred;
	} */ *ap;

{
	struct vnode *vp = ap->a_vp;
	struct vattr *vap = ap->a_vap;
	struct iso_node *ip = VTOI(vp);

	vap->va_fsid    = dev2udev(ip->i_mnt->im_dev);
	vap->va_fileid	= ip->i_number;

	vap->va_mode	= ip->inode.iso_mode;
	vap->va_nlink	= ip->inode.iso_links;
	vap->va_uid	= ip->inode.iso_uid;
	vap->va_gid	= ip->inode.iso_gid;
	vap->va_atime	= ip->inode.iso_atime;
	vap->va_mtime	= ip->inode.iso_mtime;
	vap->va_ctime	= ip->inode.iso_ctime;
	vap->va_rdev	= ip->inode.iso_rdev;

	vap->va_size	= (u_quad_t) ip->i_size;
	if (ip->i_size == 0 && (vap->va_mode & S_IFMT) == S_IFLNK) {
		struct vop_readlink_args rdlnk;
		struct iovec aiov;
		struct uio auio;
		char *cp;

		cp = malloc(MAXPATHLEN, M_TEMP, M_WAITOK);
		aiov.iov_base = cp;
		aiov.iov_len = MAXPATHLEN;
		auio.uio_iov = &aiov;
		auio.uio_iovcnt = 1;
		auio.uio_offset = 0;
		auio.uio_rw = UIO_READ;
		auio.uio_segflg = UIO_SYSSPACE;
		auio.uio_td = curthread;
		auio.uio_resid = MAXPATHLEN;
		rdlnk.a_uio = &auio;
		rdlnk.a_vp = ap->a_vp;
		rdlnk.a_cred = ap->a_cred;
		if (cd9660_readlink(&rdlnk) == 0)
			vap->va_size = MAXPATHLEN - auio.uio_resid;
		free(cp, M_TEMP);
	}
	vap->va_flags	= 0;
	vap->va_gen = 1;
	vap->va_blocksize = ip->i_mnt->logical_block_size;
	vap->va_bytes	= (u_quad_t) ip->i_size;
	vap->va_type	= vp->v_type;
	vap->va_filerev	= 0;
	return (0);
}

/*
 * Vnode op for ioctl.
 */
static int
cd9660_ioctl(ap)
	struct vop_ioctl_args /* {
		struct vnode *a_vp;
		u_long  a_command;
		caddr_t  a_data;
		int  a_fflag;
		struct ucred *a_cred;
		struct thread *a_td;
	} */ *ap;
{
	struct vnode *vp;
	struct iso_node *ip;
	int error;

	vp = ap->a_vp;
	vn_lock(vp, LK_SHARED | LK_RETRY);
	if (vp->v_iflag & VI_DOOMED) {
		VOP_UNLOCK(vp, 0);
		return (EBADF);
	}
	if (vp->v_type == VCHR || vp->v_type == VBLK) {
		VOP_UNLOCK(vp, 0);
		return (EOPNOTSUPP);
	}

	ip = VTOI(vp);
	error = 0;

	switch (ap->a_command) {
	case FIOGETLBA:
		*(int *)(ap->a_data) = ip->iso_start;
		break;
	default:
		error = ENOTTY;
		break;
	}

	VOP_UNLOCK(vp, 0);
	return (error);
}

/*
 * Vnode op for reading.
 */
static int
cd9660_read(ap)
	struct vop_read_args /* {
		struct vnode *a_vp;
		struct uio *a_uio;
		int a_ioflag;
		struct ucred *a_cred;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	struct iso_node *ip = VTOI(vp);
	struct iso_mnt *imp;
	struct buf *bp;
	daddr_t lbn, rablock;
	off_t diff;
	int rasize, error = 0;
	int seqcount;
	long size, n, on;

	if (vp->v_type == VCHR || vp->v_type == VBLK)
		return (EOPNOTSUPP);

	seqcount = ap->a_ioflag >> IO_SEQSHIFT;

	if (uio->uio_resid == 0)
		return (0);
	if (uio->uio_offset < 0)
		return (EINVAL);
	imp = ip->i_mnt;
	do {
		lbn = lblkno(imp, uio->uio_offset);
		on = blkoff(imp, uio->uio_offset);
		n = MIN(imp->logical_block_size - on, uio->uio_resid);
		diff = (off_t)ip->i_size - uio->uio_offset;
		if (diff <= 0)
			return (0);
		if (diff < n)
			n = diff;
		size = blksize(imp, ip, lbn);
		rablock = lbn + 1;
		if ((vp->v_mount->mnt_flag & MNT_NOCLUSTERR) == 0) {
			if (lblktosize(imp, rablock) < ip->i_size)
				error = cluster_read(vp, (off_t)ip->i_size,
					 lbn, size, NOCRED, uio->uio_resid,
					 (ap->a_ioflag >> 16), 0, &bp);
			else
				error = bread(vp, lbn, size, NOCRED, &bp);
		} else {
			if (seqcount > 1 &&
			    lblktosize(imp, rablock) < ip->i_size) {
				rasize = blksize(imp, ip, rablock);
				error = breadn(vp, lbn, size, &rablock,
					       &rasize, 1, NOCRED, &bp);
			} else
				error = bread(vp, lbn, size, NOCRED, &bp);
		}
		if (error != 0)
			return (error);
		n = MIN(n, size - bp->b_resid);

		error = uiomove(bp->b_data + on, (int)n, uio);
		brelse(bp);
	} while (error == 0 && uio->uio_resid > 0 && n != 0);
	return (error);
}

/*
 * Structure for reading directories
 */
struct isoreaddir {
	struct dirent saveent;
	struct dirent assocent;
	struct dirent current;
	off_t saveoff;
	off_t assocoff;
	off_t curroff;
	struct uio *uio;
	off_t uio_off;
	int eofflag;
	u_long *cookies;
	int ncookies;
};

static int
iso_uiodir(idp,dp,off)
	struct isoreaddir *idp;
	struct dirent *dp;
	off_t off;
{
	int error;

	dp->d_reclen = GENERIC_DIRSIZ(dp);
	dirent_terminate(dp);

	if (idp->uio->uio_resid < dp->d_reclen) {
		idp->eofflag = 0;
		return (-1);
	}

	if (idp->cookies) {
		if (idp->ncookies <= 0) {
			idp->eofflag = 0;
			return (-1);
		}

		*idp->cookies++ = off;
		--idp->ncookies;
	}

	if ((error = uiomove(dp, dp->d_reclen, idp->uio)) != 0)
		return (error);
	idp->uio_off = off;
	return (0);
}

static int
iso_shipdir(idp)
	struct isoreaddir *idp;
{
	struct dirent *dp;
	int cl, sl, assoc;
	int error;
	char *cname, *sname;

	cl = idp->current.d_namlen;
	cname = idp->current.d_name;
	assoc = (cl > 1) && (*cname == ASSOCCHAR);
	if (assoc) {
		cl--;
		cname++;
	}

	dp = &idp->saveent;
	sname = dp->d_name;
	if (!(sl = dp->d_namlen)) {
		dp = &idp->assocent;
		sname = dp->d_name + 1;
		sl = dp->d_namlen - 1;
	}
	if (sl > 0) {
		if (sl != cl
		    || bcmp(sname,cname,sl)) {
			if (idp->assocent.d_namlen) {
				if ((error = iso_uiodir(idp,&idp->assocent,idp->assocoff)) != 0)
					return (error);
				idp->assocent.d_namlen = 0;
			}
			if (idp->saveent.d_namlen) {
				if ((error = iso_uiodir(idp,&idp->saveent,idp->saveoff)) != 0)
					return (error);
				idp->saveent.d_namlen = 0;
			}
		}
	}
	idp->current.d_reclen = GENERIC_DIRSIZ(&idp->current);
	if (assoc) {
		idp->assocoff = idp->curroff;
		memcpy(&idp->assocent, &idp->current, idp->current.d_reclen);
	} else {
		idp->saveoff = idp->curroff;
		memcpy(&idp->saveent, &idp->current, idp->current.d_reclen);
	}
	return (0);
}

/*
 * Vnode op for readdir
 */
static int
cd9660_readdir(ap)
	struct vop_readdir_args /* {
		struct vnode *a_vp;
		struct uio *a_uio;
		struct ucred *a_cred;
		int *a_eofflag;
		int *a_ncookies;
		u_long **a_cookies;
	} */ *ap;
{
	struct uio *uio = ap->a_uio;
	struct isoreaddir *idp;
	struct vnode *vdp = ap->a_vp;
	struct iso_node *dp;
	struct iso_mnt *imp;
	struct buf *bp = NULL;
	struct iso_directory_record *ep;
	int entryoffsetinblock;
	doff_t endsearch;
	u_long bmask;
	int error = 0;
	int reclen;
	u_short namelen;
	u_int ncookies = 0;
	u_long *cookies = NULL;
	cd_ino_t ino;

	dp = VTOI(vdp);
	imp = dp->i_mnt;
	bmask = imp->im_bmask;

	idp = malloc(sizeof(*idp), M_TEMP, M_WAITOK);
	idp->saveent.d_namlen = idp->assocent.d_namlen = 0;
	/*
	 * XXX
	 * Is it worth trying to figure out the type?
	 */
	idp->saveent.d_type = idp->assocent.d_type = idp->current.d_type =
	    DT_UNKNOWN;
	idp->uio = uio;
	if (ap->a_ncookies == NULL) {
		idp->cookies = NULL;
	} else {
		/*
		 * Guess the number of cookies needed.
		 */
		ncookies = uio->uio_resid / 16;
		cookies = malloc(ncookies * sizeof(u_long),
		    M_TEMP, M_WAITOK);
		idp->cookies = cookies;
		idp->ncookies = ncookies;
	}
	idp->eofflag = 1;
	idp->curroff = uio->uio_offset;
	idp->uio_off = uio->uio_offset;

	if ((entryoffsetinblock = idp->curroff & bmask) &&
	    (error = cd9660_blkatoff(vdp, (off_t)idp->curroff, NULL, &bp))) {
		free(idp, M_TEMP);
		return (error);
	}
	endsearch = dp->i_size;

	while (idp->curroff < endsearch) {
		/*
		 * If offset is on a block boundary,
		 * read the next directory block.
		 * Release previous if it exists.
		 */
		if ((idp->curroff & bmask) == 0) {
			if (bp != NULL)
				brelse(bp);
			if ((error =
			    cd9660_blkatoff(vdp, (off_t)idp->curroff, NULL, &bp)) != 0)
				break;
			entryoffsetinblock = 0;
		}
		/*
		 * Get pointer to next entry.
		 */
		ep = (struct iso_directory_record *)
			((char *)bp->b_data + entryoffsetinblock);

		reclen = isonum_711(ep->length);
		if (reclen == 0) {
			/* skip to next block, if any */
			idp->curroff =
			    (idp->curroff & ~bmask) + imp->logical_block_size;
			continue;
		}

		if (reclen < ISO_DIRECTORY_RECORD_SIZE) {
			error = EINVAL;
			/* illegal entry, stop */
			break;
		}

		if (entryoffsetinblock + reclen > imp->logical_block_size) {
			error = EINVAL;
			/* illegal directory, so stop looking */
			break;
		}

		idp->current.d_namlen = isonum_711(ep->name_len);

		if (reclen < ISO_DIRECTORY_RECORD_SIZE + idp->current.d_namlen) {
			error = EINVAL;
			/* illegal entry, stop */
			break;
		}

		if (isonum_711(ep->flags)&2)
			idp->current.d_fileno = isodirino(ep, imp);
		else
			idp->current.d_fileno = dbtob(bp->b_blkno) +
				entryoffsetinblock;

		idp->curroff += reclen;
		/* NOTE: d_off is the offset of *next* entry. */
		idp->current.d_off = idp->curroff;

		switch (imp->iso_ftype) {
		case ISO_FTYPE_RRIP:
			ino = idp->current.d_fileno;
			cd9660_rrip_getname(ep, idp->current.d_name, &namelen,
			    &ino, imp);
			idp->current.d_fileno = ino;
			idp->current.d_namlen = (u_char)namelen;
			if (idp->current.d_namlen)
				error = iso_uiodir(idp,&idp->current,idp->curroff);
			break;
		default: /* ISO_FTYPE_DEFAULT || ISO_FTYPE_9660 || ISO_FTYPE_HIGH_SIERRA*/
			strcpy(idp->current.d_name,"..");
			if (idp->current.d_namlen == 1 && ep->name[0] == 0) {
				idp->current.d_namlen = 1;
				error = iso_uiodir(idp,&idp->current,idp->curroff);
			} else if (idp->current.d_namlen == 1 && ep->name[0] == 1) {
				idp->current.d_namlen = 2;
				error = iso_uiodir(idp,&idp->current,idp->curroff);
			} else {
				isofntrans(ep->name,idp->current.d_namlen,
					   idp->current.d_name, &namelen,
					   imp->iso_ftype == ISO_FTYPE_9660,
					   isonum_711(ep->flags)&4,
					   imp->joliet_level,
					   imp->im_flags,
					   imp->im_d2l);
				idp->current.d_namlen = (u_char)namelen;
				if (imp->iso_ftype == ISO_FTYPE_DEFAULT)
					error = iso_shipdir(idp);
				else
					error = iso_uiodir(idp,&idp->current,idp->curroff);
			}
		}
		if (error)
			break;

		entryoffsetinblock += reclen;
	}

	if (!error && imp->iso_ftype == ISO_FTYPE_DEFAULT) {
		idp->current.d_namlen = 0;
		error = iso_shipdir(idp);
	}
	if (error < 0)
		error = 0;

	if (ap->a_ncookies != NULL) {
		if (error)
			free(cookies, M_TEMP);
		else {
			/*
			 * Work out the number of cookies actually used.
			 */
			*ap->a_ncookies = ncookies - idp->ncookies;
			*ap->a_cookies = cookies;
		}
	}

	if (bp)
		brelse (bp);

	uio->uio_offset = idp->uio_off;
	*ap->a_eofflag = idp->eofflag;

	free(idp, M_TEMP);

	return (error);
}

/*
 * Return target name of a symbolic link
 * Shouldn't we get the parent vnode and read the data from there?
 * This could eventually result in deadlocks in cd9660_lookup.
 * But otherwise the block read here is in the block buffer two times.
 */
typedef struct iso_directory_record ISODIR;
typedef struct iso_node		    ISONODE;
typedef struct iso_mnt		    ISOMNT;
static int
cd9660_readlink(ap)
	struct vop_readlink_args /* {
		struct vnode *a_vp;
		struct uio *a_uio;
		struct ucred *a_cred;
	} */ *ap;
{
	ISONODE	*ip;
	ISODIR	*dirp;
	ISOMNT	*imp;
	struct	buf *bp;
	struct	uio *uio;
	u_short	symlen;
	int	error;
	char	*symname;

	ip  = VTOI(ap->a_vp);
	imp = ip->i_mnt;
	uio = ap->a_uio;

	if (imp->iso_ftype != ISO_FTYPE_RRIP)
		return (EINVAL);

	/*
	 * Get parents directory record block that this inode included.
	 */
	error = bread(imp->im_devvp,
		      (ip->i_number >> imp->im_bshift) <<
		      (imp->im_bshift - DEV_BSHIFT),
		      imp->logical_block_size, NOCRED, &bp);
	if (error) {
		brelse(bp);
		return (EINVAL);
	}

	/*
	 * Setup the directory pointer for this inode
	 */
	dirp = (ISODIR *)(bp->b_data + (ip->i_number & imp->im_bmask));

	/*
	 * Just make sure, we have a right one....
	 *   1: Check not cross boundary on block
	 */
	if ((ip->i_number & imp->im_bmask) + isonum_711(dirp->length)
	    > (unsigned)imp->logical_block_size) {
		brelse(bp);
		return (EINVAL);
	}

	/*
	 * Now get a buffer
	 * Abuse a namei buffer for now.
	 */
	if (uio->uio_segflg == UIO_SYSSPACE)
		symname = uio->uio_iov->iov_base;
	else
		symname = uma_zalloc(namei_zone, M_WAITOK);

	/*
	 * Ok, we just gathering a symbolic name in SL record.
	 */
	if (cd9660_rrip_getsymname(dirp, symname, &symlen, imp) == 0) {
		if (uio->uio_segflg != UIO_SYSSPACE)
			uma_zfree(namei_zone, symname);
		brelse(bp);
		return (EINVAL);
	}
	/*
	 * Don't forget before you leave from home ;-)
	 */
	brelse(bp);

	/*
	 * return with the symbolic name to caller's.
	 */
	if (uio->uio_segflg != UIO_SYSSPACE) {
		error = uiomove(symname, symlen, uio);
		uma_zfree(namei_zone, symname);
		return (error);
	}
	uio->uio_resid -= symlen;
	uio->uio_iov->iov_base = (char *)uio->uio_iov->iov_base + symlen;
	uio->uio_iov->iov_len -= symlen;
	return (0);
}

/*
 * Calculate the logical to physical mapping if not done already,
 * then call the device strategy routine.
 */
static int
cd9660_strategy(ap)
	struct vop_strategy_args /* {
		struct buf *a_vp;
		struct buf *a_bp;
	} */ *ap;
{
	struct buf *bp = ap->a_bp;
	struct vnode *vp = ap->a_vp;
	struct iso_node *ip;
	struct bufobj *bo;

	ip = VTOI(vp);
	if (vp->v_type == VBLK || vp->v_type == VCHR)
		panic("cd9660_strategy: spec");
	if (bp->b_blkno == bp->b_lblkno) {
		bp->b_blkno = (ip->iso_start + bp->b_lblkno) <<
		    (ip->i_mnt->im_bshift - DEV_BSHIFT);
	}
	bp->b_iooffset = dbtob(bp->b_blkno);
	bo = ip->i_mnt->im_bo;
	BO_STRATEGY(bo, bp);
	return (0);
}

/*
 * Return POSIX pathconf information applicable to cd9660 filesystems.
 */
static int
cd9660_pathconf(ap)
	struct vop_pathconf_args /* {
		struct vnode *a_vp;
		int a_name;
		register_t *a_retval;
	} */ *ap;
{

	switch (ap->a_name) {
	case _PC_FILESIZEBITS:
		*ap->a_retval = 32;
		return (0);
	case _PC_LINK_MAX:
		*ap->a_retval = 1;
		return (0);
	case _PC_NAME_MAX:
		if (VTOI(ap->a_vp)->i_mnt->iso_ftype == ISO_FTYPE_RRIP)
			*ap->a_retval = NAME_MAX;
		else
			*ap->a_retval = 37;
		return (0);
	case _PC_SYMLINK_MAX:
		if (VTOI(ap->a_vp)->i_mnt->iso_ftype == ISO_FTYPE_RRIP) {
			*ap->a_retval = MAXPATHLEN;
			return (0);
		}
		return (EINVAL);
	case _PC_NO_TRUNC:
		*ap->a_retval = 1;
		return (0);
	default:
		return (vop_stdpathconf(ap));
	}
	/* NOTREACHED */
}

/*
 * Vnode pointer to File handle
 */
static int
cd9660_vptofh(ap)
	struct vop_vptofh_args /* {
		struct vnode *a_vp;
		struct fid *a_fhp;
	} */ *ap;
{
	struct ifid ifh;
	struct iso_node *ip = VTOI(ap->a_vp);

	ifh.ifid_len = sizeof(struct ifid);

	ifh.ifid_ino = ip->i_number;
	ifh.ifid_start = ip->iso_start;
	/*
	 * This intentionally uses sizeof(ifh) in order to not copy stack
	 * garbage on ILP32.
	 */
	memcpy(ap->a_fhp, &ifh, sizeof(ifh));

#ifdef	ISOFS_DBG
	printf("vptofh: ino %jd, start %ld\n",
	    (uintmax_t)ifh.ifid_ino, ifh.ifid_start);
#endif

	return (0);
}

SYSCTL_NODE(_vfs, OID_AUTO, cd9660, CTLFLAG_RW, 0, "cd9660 filesystem");
static int use_buf_pager = 1;
SYSCTL_INT(_vfs_cd9660, OID_AUTO, use_buf_pager, CTLFLAG_RWTUN,
    &use_buf_pager, 0,
    "Use buffer pager instead of bmap");

static daddr_t
cd9660_gbp_getblkno(struct vnode *vp, vm_ooffset_t off)
{

	return (lblkno(VTOI(vp)->i_mnt, off));
}

static int
cd9660_gbp_getblksz(struct vnode *vp, daddr_t lbn)
{
	struct iso_node *ip;

	ip = VTOI(vp);
	return (blksize(ip->i_mnt, ip, lbn));
}

static int
cd9660_getpages(struct vop_getpages_args *ap)
{
	struct vnode *vp;

	vp = ap->a_vp;
	if (vp->v_type == VCHR || vp->v_type == VBLK)
		return (EOPNOTSUPP);

	if (use_buf_pager)
		return (vfs_bio_getpages(vp, ap->a_m, ap->a_count,
		    ap->a_rbehind, ap->a_rahead, cd9660_gbp_getblkno,
		    cd9660_gbp_getblksz));
	return (vnode_pager_generic_getpages(vp, ap->a_m, ap->a_count,
	    ap->a_rbehind, ap->a_rahead, NULL, NULL));
}

/*
 * Global vfs data structures for cd9660
 */
struct vop_vector cd9660_vnodeops = {
	.vop_default =		&default_vnodeops,
	.vop_open =		cd9660_open,
	.vop_access =		cd9660_access,
	.vop_bmap =		cd9660_bmap,
	.vop_cachedlookup =	cd9660_lookup,
	.vop_getattr =		cd9660_getattr,
	.vop_inactive =		cd9660_inactive,
	.vop_ioctl =		cd9660_ioctl,
	.vop_lookup =		vfs_cache_lookup,
	.vop_pathconf =		cd9660_pathconf,
	.vop_read =		cd9660_read,
	.vop_readdir =		cd9660_readdir,
	.vop_readlink =		cd9660_readlink,
	.vop_reclaim =		cd9660_reclaim,
	.vop_setattr =		cd9660_setattr,
	.vop_strategy =		cd9660_strategy,
	.vop_vptofh =		cd9660_vptofh,
	.vop_getpages =		cd9660_getpages,
};

/*
 * Special device vnode ops
 */

struct vop_vector cd9660_fifoops = {
	.vop_default =		&fifo_specops,
	.vop_access =		cd9660_access,
	.vop_getattr =		cd9660_getattr,
	.vop_inactive =		cd9660_inactive,
	.vop_reclaim =		cd9660_reclaim,
	.vop_setattr =		cd9660_setattr,
	.vop_vptofh =		cd9660_vptofh,
};
