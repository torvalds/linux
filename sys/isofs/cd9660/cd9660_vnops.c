/*	$OpenBSD: cd9660_vnops.c,v 1.97 2024/10/18 05:52:32 miod Exp $	*/
/*	$NetBSD: cd9660_vnops.c,v 1.42 1997/10/16 23:56:57 christos Exp $	*/

/*-
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
 *	@(#)cd9660_vnops.c	8.15 (Berkeley) 12/5/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/namei.h>
#include <sys/resourcevar.h>
#include <sys/kernel.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/pool.h>
#include <sys/dirent.h>
#include <sys/ioctl.h>
#include <sys/ioccom.h>
#include <sys/specdev.h>
#include <sys/unistd.h>

#include <miscfs/fifofs/fifo.h>

#include <isofs/cd9660/iso.h>
#include <isofs/cd9660/cd9660_extern.h>
#include <isofs/cd9660/cd9660_node.h>
#include <isofs/cd9660/iso_rrip.h>

int cd9660_kqfilter(void *v);


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
};

int	iso_uiodir(struct isoreaddir *, struct dirent *, off_t);
int	iso_shipdir(struct isoreaddir *);

/*
 * Setattr call. Only allowed for block and character special devices.
 */
int
cd9660_setattr(void *v)
{
	struct vop_setattr_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct vattr *vap = ap->a_vap;

	if (vap->va_flags != VNOVAL || vap->va_uid != (uid_t)VNOVAL ||
	    vap->va_gid != (gid_t)VNOVAL || vap->va_atime.tv_nsec != VNOVAL ||
	    vap->va_mtime.tv_nsec != VNOVAL || vap->va_mode != (mode_t)VNOVAL ||
	    (vap->va_vaflags & VA_UTIMES_CHANGE))
		return (EROFS);
	if (vap->va_size != VNOVAL) {
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
			return (0);
		default:
			return (EINVAL);
		}
	}

	return (EINVAL);
}

/*
 * Open called.
 *
 * Nothing to do.
 */
int
cd9660_open(void *v)
{
	return (0);
}

/*
 * Close called
 *
 * Update the times on the inode on writeable file systems.
 */
int
cd9660_close(void *v)
{
	return (0);
}

/*
 * Check mode permission on inode pointer. Mode is READ, WRITE or EXEC.
 * The mode is shifted to select the owner/group/other fields. The
 * super user is granted all permissions.
 */
int
cd9660_access(void *v)
{
	struct vop_access_args *ap = v;
	struct iso_node *ip = VTOI(ap->a_vp);

	return (vaccess(ap->a_vp->v_type, ip->inode.iso_mode & ALLPERMS,
	    ip->inode.iso_uid, ip->inode.iso_gid, ap->a_mode, ap->a_cred));
}

int
cd9660_getattr(void *v)
{
	struct vop_getattr_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct vattr *vap = ap->a_vap;
	struct iso_node *ip = VTOI(vp);

	vap->va_fsid	= ip->i_dev;
	vap->va_fileid	= ip->i_number;

	vap->va_mode	= ip->inode.iso_mode & ALLPERMS;
	vap->va_nlink	= ip->inode.iso_links;
	vap->va_uid	= ip->inode.iso_uid;
	vap->va_gid	= ip->inode.iso_gid;
	vap->va_atime	= ip->inode.iso_atime;
	vap->va_mtime	= ip->inode.iso_mtime;
	vap->va_ctime	= ip->inode.iso_ctime;
	vap->va_rdev	= ip->inode.iso_rdev;

	vap->va_size	= (u_quad_t) ip->i_size;
	if (ip->i_size == 0 && vp->v_type  == VLNK) {
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
		auio.uio_procp = ap->a_p;
		auio.uio_resid = MAXPATHLEN;
		rdlnk.a_uio = &auio;
		rdlnk.a_vp = ap->a_vp;
		rdlnk.a_cred = ap->a_cred;
		if (cd9660_readlink(&rdlnk) == 0)
			vap->va_size = MAXPATHLEN - auio.uio_resid;
		free(cp, M_TEMP, 0);
	}
	vap->va_flags	= 0;
	vap->va_gen = 1;
	vap->va_blocksize = ip->i_mnt->logical_block_size;
	vap->va_bytes	= (u_quad_t) ip->i_size;
	vap->va_type	= vp->v_type;
	return (0);
}

/*
 * Vnode op for reading.
 */
int
cd9660_read(void *v)
{
	struct vop_read_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	struct iso_node *ip = VTOI(vp);
	struct iso_mnt *imp;
	struct buf *bp;
	daddr_t lbn, rablock;
	off_t diff;
	int error = 0;
	long size, on;
	size_t n;

	if (uio->uio_resid == 0)
		return (0);
	if (uio->uio_offset < 0)
		return (EINVAL);
	ip->i_flag |= IN_ACCESS;
	imp = ip->i_mnt;
	do {
		struct cluster_info *ci = &ip->i_ci;

		lbn = lblkno(imp, uio->uio_offset);
		on = blkoff(imp, uio->uio_offset);
		n = ulmin(imp->logical_block_size - on, uio->uio_resid);
		diff = (off_t)ip->i_size - uio->uio_offset;
		if (diff <= 0)
			return (0);
		if (diff < n)
			n = diff;
		size = blksize(imp, ip, lbn);
		rablock = lbn + 1;
#define MAX_RA 32
		if (ci->ci_lastr + 1 == lbn) {
			struct ra {
				daddr_t blks[MAX_RA];
				int sizes[MAX_RA];
			} *ra;
			int i;

			ra = malloc(sizeof *ra, M_TEMP, M_WAITOK);
			for (i = 0; i < MAX_RA &&
			    lblktosize(imp, (rablock + i)) < ip->i_size;
			    i++) {
				ra->blks[i] = rablock + i;
				ra->sizes[i] = blksize(imp, ip, rablock + i);
			}
			error = breadn(vp, lbn, size, ra->blks,
			    ra->sizes, i, &bp);
			free(ra, M_TEMP, 0);
		} else
			error = bread(vp, lbn, size, &bp);
		ci->ci_lastr = lbn;
		n = ulmin(n, size - bp->b_resid);
		if (error) {
			brelse(bp);
			return (error);
		}

		error = uiomove(bp->b_data + on, n, uio);

		brelse(bp);
	} while (error == 0 && uio->uio_resid > 0 && n != 0);
	return (error);
}

int
cd9660_ioctl(void *v)
{
	return (ENOTTY);
}

/*
 * Mmap a file
 *
 * NB Currently unsupported.
 */
int
cd9660_mmap(void *v)
{

	return (EINVAL);
}

/*
 * Seek on a file
 *
 * Nothing to do, so just return.
 */
int
cd9660_seek(void *v)
{
	return (0);
}

int
iso_uiodir(struct isoreaddir *idp, struct dirent *dp, off_t off)
{
	int error;

	dp->d_name[dp->d_namlen] = 0;
	dp->d_reclen = DIRENT_SIZE(dp);

	if (memchr(dp->d_name, '/', dp->d_namlen) != NULL) {
		/* illegal file name */
		return (EINVAL);
	}

	if (idp->uio->uio_resid < dp->d_reclen) {
		idp->eofflag = 0;
		return (-1);
	}

	dp->d_off = off;
	if ((error = uiomove(dp, dp->d_reclen, idp->uio)) != 0)
		return (error);
	idp->uio_off = off;
	return (0);
}

int
iso_shipdir(struct isoreaddir *idp)
{
	struct dirent *dp;
	int cl, sl, assoc;
	int error;
	char *cname, *sname;

	cl = idp->current.d_namlen;
	cname = idp->current.d_name;

	if ((assoc = cl > 1 && *cname == ASSOCCHAR)) {
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
				error = iso_uiodir(idp, &idp->assocent,
						   idp->assocoff);
				if (error)
					return (error);
				idp->assocent.d_namlen = 0;
			}
			if (idp->saveent.d_namlen) {
				error = iso_uiodir(idp, &idp->saveent,
						   idp->saveoff);
				if (error)
					return (error);
				idp->saveent.d_namlen = 0;
			}
		}
	}
	idp->current.d_reclen = DIRENT_SIZE(&idp->current);
	if (assoc) {
		idp->assocoff = idp->curroff;
		bcopy(&idp->current,&idp->assocent,idp->current.d_reclen);
	} else {
		idp->saveoff = idp->curroff;
		bcopy(&idp->current,&idp->saveent,idp->current.d_reclen);
	}
	return (0);
}

/*
 * Vnode op for readdir
 */
int
cd9660_readdir(void *v)
{
	struct vop_readdir_args *ap = v;
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
	cdino_t ino;

	dp = VTOI(vdp);
	imp = dp->i_mnt;
	bmask = imp->im_bmask;

	idp = malloc(sizeof(*idp), M_TEMP, M_WAITOK);

	/*
	 * These are passed to copyout(), so make sure there's no garbage
	 * being leaked in padding or after short names.
	 */
	memset(&idp->saveent, 0, sizeof(idp->saveent));
	memset(&idp->assocent, 0, sizeof(idp->assocent));
	memset(&idp->current, 0, sizeof(idp->current));

	/*
	 * XXX
	 * Is it worth trying to figure out the type?
	 */
	idp->saveent.d_type = idp->assocent.d_type = idp->current.d_type =
	    DT_UNKNOWN;
	idp->uio = uio;
	idp->eofflag = 1;
	idp->curroff = uio->uio_offset;
	idp->uio_off = uio->uio_offset;

	if ((entryoffsetinblock = idp->curroff & bmask) &&
	    (error = cd9660_bufatoff(dp, (off_t)idp->curroff, NULL, &bp))) {
		free(idp, M_TEMP, 0);
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
			error = cd9660_bufatoff(dp, (off_t)idp->curroff,
					     NULL, &bp);
			if (error)
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
			ino = isodirino(ep, imp);
		else
			ino = dbtob(bp->b_blkno) + entryoffsetinblock;

		idp->curroff += reclen;

		switch (imp->iso_ftype) {
		case ISO_FTYPE_RRIP:
			cd9660_rrip_getname(ep,idp->current.d_name, &namelen,
					   &ino, imp);
			idp->current.d_fileno = ino;
			idp->current.d_namlen = (u_char)namelen;
			if (idp->current.d_namlen)
				error = iso_uiodir(idp,&idp->current,idp->curroff);
			break;
		default:	/* ISO_FTYPE_DEFAULT || ISO_FTYPE_9660 */
			idp->current.d_fileno = ino;
			strlcpy(idp->current.d_name,"..",
			    sizeof idp->current.d_name);
			if (idp->current.d_namlen == 1 && ep->name[0] == 0) {
				idp->current.d_namlen = 1;
				error = iso_uiodir(idp,&idp->current,idp->curroff);
			} else if (idp->current.d_namlen == 1 && 
			    ep->name[0] == 1) {
				idp->current.d_namlen = 2;
				error = iso_uiodir(idp,&idp->current,idp->curroff);
			} else {
				isofntrans(ep->name,idp->current.d_namlen,
					   idp->current.d_name, &namelen,
					   imp->iso_ftype == ISO_FTYPE_9660,
					   isonum_711(ep->flags) & 4,
					   imp->joliet_level);
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

	if (bp)
		brelse (bp);

	uio->uio_offset = idp->uio_off;
	*ap->a_eofflag = idp->eofflag;

	free(idp, M_TEMP, 0);

	return (error);
}

/*
 * Return target name of a symbolic link
 * Shouldn't we get the parent vnode and read the data from there?
 * This could eventually result in deadlocks in cd9660_lookup.
 * But otherwise the block read here is in the block buffer two times.
 */
typedef struct iso_directory_record ISODIR;
typedef struct iso_node             ISONODE;
typedef struct iso_mnt              ISOMNT;
int
cd9660_readlink(void *v)
{
	struct vop_readlink_args *ap = v;
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
		      imp->logical_block_size, &bp);
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
	    > imp->logical_block_size) {
		brelse(bp);
		return (EINVAL);
	}

	/*
	 * Now get a buffer
	 * Abuse a namei buffer for now.
	 */
	if (uio->uio_segflg == UIO_SYSSPACE &&
	    uio->uio_iov->iov_len >= MAXPATHLEN)
		symname = uio->uio_iov->iov_base;
	else
		symname = pool_get(&namei_pool, PR_WAITOK);
	
	/*
	 * Ok, we just gathering a symbolic name in SL record.
	 */
	if (cd9660_rrip_getsymname(dirp, symname, &symlen, imp) == 0) {
		if (uio->uio_segflg != UIO_SYSSPACE ||
		    uio->uio_iov->iov_len < MAXPATHLEN)
			pool_put(&namei_pool, symname);
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
	if (uio->uio_segflg != UIO_SYSSPACE ||
	    uio->uio_iov->iov_len < MAXPATHLEN) {
		error = uiomove(symname, symlen, uio);
		pool_put(&namei_pool, symname);
		return (error);
	}
	uio->uio_resid -= symlen;
	uio->uio_iov->iov_base = (char *)uio->uio_iov->iov_base + symlen;
	uio->uio_iov->iov_len -= symlen;
	return (0);
}

int
cd9660_link(void *v)
{
	struct vop_link_args *ap = v;

	VOP_ABORTOP(ap->a_dvp, ap->a_cnp);
	vput(ap->a_dvp);
	return (EROFS);
}

int
cd9660_symlink(void *v)
{
	struct vop_symlink_args *ap = v;

	VOP_ABORTOP(ap->a_dvp, ap->a_cnp);
	vput(ap->a_dvp);
	return (EROFS);
}

/*
 * Lock an inode.
 */
int
cd9660_lock(void *v)
{
	struct vop_lock_args *ap = v;
	struct vnode *vp = ap->a_vp;

	return rrw_enter(&VTOI(vp)->i_lock, ap->a_flags & LK_RWFLAGS);
}

/*
 * Unlock an inode.
 */
int
cd9660_unlock(void *v)
{
	struct vop_unlock_args *ap = v;
	struct vnode *vp = ap->a_vp;

	rrw_exit(&VTOI(vp)->i_lock);
	return 0;
}

/*
 * Calculate the logical to physical mapping if not done already,
 * then call the device strategy routine.
 */
int
cd9660_strategy(void *v)
{
	struct vop_strategy_args *ap = v;
	struct buf *bp = ap->a_bp;
	struct vnode *vp = bp->b_vp;
	struct iso_node *ip;
	int error;
	int s;

	ip = VTOI(vp);
	if (vp->v_type == VBLK || vp->v_type == VCHR)
		panic("cd9660_strategy: spec");
	if (bp->b_blkno == bp->b_lblkno) {
		error = VOP_BMAP(vp, bp->b_lblkno, NULL, &bp->b_blkno, NULL);
		if (error) {
			bp->b_error = error;
			bp->b_flags |= B_ERROR;
			s = splbio();
			biodone(bp);
			splx(s);
			return (error);
		}
		if ((long)bp->b_blkno == -1)
			clrbuf(bp);
	}
	if ((long)bp->b_blkno == -1) {
		s = splbio();
		biodone(bp);
		splx(s);
		return (0);
	}
	vp = ip->i_devvp;
	bp->b_dev = vp->v_rdev;
	VOP_STRATEGY(vp, bp);
	return (0);
}

/*
 * Print out the contents of an inode.
 */
int
cd9660_print(void *v)
{
#if defined(DEBUG) || defined(DIAGNOSTIC) || defined(VFSLCKDEBUG)
	printf("tag VT_ISOFS, isofs vnode\n");
#endif
	return (0);
}

/*
 * Check for a locked inode.
 */
int
cd9660_islocked(void *v)
{
	struct vop_islocked_args *ap = v;

	return rrw_status(&VTOI(ap->a_vp)->i_lock);
}

/*
 * Return POSIX pathconf information applicable to cd9660 filesystems.
 */
int
cd9660_pathconf(void *v)
{
	struct vop_pathconf_args *ap = v;
	int error = 0;

	switch (ap->a_name) {
	case _PC_LINK_MAX:
		*ap->a_retval = 1;
		break;
	case _PC_NAME_MAX:
		if (VTOI(ap->a_vp)->i_mnt->iso_ftype == ISO_FTYPE_RRIP)
			*ap->a_retval = NAME_MAX;
		else
			*ap->a_retval = 37;
		break;
	case _PC_CHOWN_RESTRICTED:
		*ap->a_retval = 1;
		break;
	case _PC_NO_TRUNC:
		*ap->a_retval = 1;
		break;
	case _PC_TIMESTAMP_RESOLUTION:
		*ap->a_retval = 1000000000;	/* one billion nanoseconds */
		break;
	default:
		error = EINVAL;
		break;
	}

	return (error);
}

/*
 * Global vfs data structures for isofs
 */

/* Global vfs data structures for cd9660. */
const struct vops cd9660_vops = {
	.vop_lookup	= cd9660_lookup,
	.vop_create	= eopnotsupp,
	.vop_mknod	= eopnotsupp,
	.vop_open	= cd9660_open,
	.vop_close	= cd9660_close,
	.vop_access	= cd9660_access,
	.vop_getattr	= cd9660_getattr,
	.vop_setattr	= cd9660_setattr,
	.vop_read	= cd9660_read,
	.vop_write	= eopnotsupp,
	.vop_ioctl	= cd9660_ioctl,
	.vop_kqfilter	= cd9660_kqfilter,
	.vop_revoke	= vop_generic_revoke,
	.vop_fsync	= nullop,
	.vop_remove	= eopnotsupp,
	.vop_link	= cd9660_link,
	.vop_rename	= eopnotsupp,
	.vop_mkdir	= eopnotsupp,
	.vop_rmdir	= eopnotsupp,
	.vop_symlink	= cd9660_symlink,
	.vop_readdir	= cd9660_readdir,
	.vop_readlink	= cd9660_readlink,
	.vop_abortop	= vop_generic_abortop,
	.vop_inactive	= cd9660_inactive,
	.vop_reclaim	= cd9660_reclaim,
	.vop_lock	= cd9660_lock,
	.vop_unlock	= cd9660_unlock,
	.vop_bmap	= cd9660_bmap,
	.vop_strategy	= cd9660_strategy,
	.vop_print	= cd9660_print,
	.vop_islocked	= cd9660_islocked,
	.vop_pathconf	= cd9660_pathconf,
	.vop_advlock	= eopnotsupp,
	.vop_bwrite	= vop_generic_bwrite,
};

/* Special device vnode ops */
const struct vops cd9660_specvops = {
	.vop_access	= cd9660_access,
	.vop_getattr	= cd9660_getattr,
	.vop_setattr	= cd9660_setattr,
	.vop_inactive	= cd9660_inactive,
	.vop_reclaim	= cd9660_reclaim,
	.vop_lock	= cd9660_lock,
	.vop_unlock	= cd9660_unlock,
	.vop_print	= cd9660_print,
	.vop_islocked	= cd9660_islocked,

	/* XXX: Keep in sync with spec_vops. */
	.vop_lookup	= vop_generic_lookup,
	.vop_create	= vop_generic_badop,
	.vop_mknod	= vop_generic_badop,
	.vop_open	= spec_open,
	.vop_close	= spec_close,
	.vop_read	= spec_read,
	.vop_write	= spec_write,
	.vop_ioctl	= spec_ioctl,
	.vop_kqfilter	= spec_kqfilter,
	.vop_revoke	= vop_generic_revoke,
	.vop_fsync	= spec_fsync,
	.vop_remove	= vop_generic_badop,
	.vop_link	= vop_generic_badop,
	.vop_rename	= vop_generic_badop,
	.vop_mkdir	= vop_generic_badop,
	.vop_rmdir	= vop_generic_badop,
	.vop_symlink	= vop_generic_badop,
	.vop_readdir	= vop_generic_badop,
	.vop_readlink	= vop_generic_badop,
	.vop_abortop	= vop_generic_badop,
	.vop_bmap	= vop_generic_bmap,
	.vop_strategy	= spec_strategy,
	.vop_pathconf	= spec_pathconf,
	.vop_advlock	= spec_advlock,
	.vop_bwrite	= vop_generic_bwrite,
};

#ifdef FIFO
const struct vops cd9660_fifovops = {
	.vop_access	= cd9660_access,
	.vop_getattr	= cd9660_getattr,
	.vop_setattr	= cd9660_setattr,
	.vop_inactive	= cd9660_inactive,
	.vop_reclaim	= cd9660_reclaim,
	.vop_lock	= cd9660_lock,
	.vop_unlock	= cd9660_unlock,
	.vop_print	= cd9660_print,
	.vop_islocked	= cd9660_islocked,
	.vop_bwrite	= vop_generic_bwrite,

	/* XXX: Keep in sync with fifo_vops. */
	.vop_lookup	= vop_generic_lookup,
	.vop_create	= vop_generic_badop,
	.vop_mknod	= vop_generic_badop,
	.vop_open	= fifo_open,
	.vop_close	= fifo_close,
	.vop_read	= fifo_read,
	.vop_write	= fifo_write,
	.vop_ioctl	= fifo_ioctl,
	.vop_kqfilter	= fifo_kqfilter,
	.vop_revoke	= vop_generic_revoke,
	.vop_fsync	= nullop,
	.vop_remove	= vop_generic_badop,
	.vop_link	= vop_generic_badop,
	.vop_rename	= vop_generic_badop,
	.vop_mkdir	= vop_generic_badop,
	.vop_rmdir	= vop_generic_badop,
	.vop_symlink	= vop_generic_badop,
	.vop_readdir	= vop_generic_badop,
	.vop_readlink	= vop_generic_badop,
	.vop_abortop	= vop_generic_badop,
	.vop_bmap	= vop_generic_bmap,
	.vop_strategy	= vop_generic_badop,
	.vop_pathconf	= fifo_pathconf,
	.vop_advlock	= fifo_advlock,
};
#endif /* FIFO */

void filt_cd9660detach(struct knote *kn);
int filt_cd9660read(struct knote *kn, long hint);
int filt_cd9660write(struct knote *kn, long hint);
int filt_cd9660vnode(struct knote *kn, long hint);

const struct filterops cd9660read_filtops = {
	.f_flags	= FILTEROP_ISFD,
	.f_attach	= NULL,
	.f_detach	= filt_cd9660detach,
	.f_event	= filt_cd9660read,
};

const struct filterops cd9660write_filtops = {
	.f_flags	= FILTEROP_ISFD,
	.f_attach	= NULL,
	.f_detach	= filt_cd9660detach,
	.f_event	= filt_cd9660write,
};

const struct filterops cd9660vnode_filtops = {
	.f_flags	= FILTEROP_ISFD,
	.f_attach	= NULL,
	.f_detach	= filt_cd9660detach,
	.f_event	= filt_cd9660vnode,
};

int
cd9660_kqfilter(void *v)
{
	struct vop_kqfilter_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct knote *kn = ap->a_kn;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &cd9660read_filtops;
		break;
	case EVFILT_WRITE:
		kn->kn_fop = &cd9660write_filtops;
		break;
	case EVFILT_VNODE:
		kn->kn_fop = &cd9660vnode_filtops;
		break;
	default:
		return (EINVAL);
	}

	kn->kn_hook = (caddr_t)vp;

	klist_insert_locked(&vp->v_klist, kn);

	return (0);
}

void
filt_cd9660detach(struct knote *kn)
{
	struct vnode *vp = (struct vnode *)kn->kn_hook;

	klist_remove_locked(&vp->v_klist, kn);
}

int
filt_cd9660read(struct knote *kn, long hint)
{
	struct vnode *vp = (struct vnode *)kn->kn_hook;
	struct iso_node *node = VTOI(vp);

	/*
	 * filesystem is gone, so set the EOF flag and schedule 
	 * the knote for deletion.
	 */
	if (hint == NOTE_REVOKE) {
		kn->kn_flags |= (EV_EOF | EV_ONESHOT);
		return (1);
	}

	kn->kn_data = node->i_size - foffset(kn->kn_fp);
	if (kn->kn_data == 0 && kn->kn_sfflags & NOTE_EOF) {
		kn->kn_fflags |= NOTE_EOF;
		return (1);
	}

	if (kn->kn_flags & (__EV_POLL | __EV_SELECT))
		return (1);

	return (kn->kn_data != 0);
}

int
filt_cd9660write(struct knote *kn, long hint)
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
filt_cd9660vnode(struct knote *kn, long hint)
{
	if (kn->kn_sfflags & hint)
		kn->kn_fflags |= hint;
	if (hint == NOTE_REVOKE) {
		kn->kn_flags |= EV_EOF;
		return (1);
	}
	return (kn->kn_fflags != 0);
}
