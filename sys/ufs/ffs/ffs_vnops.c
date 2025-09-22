/*	$OpenBSD: ffs_vnops.c,v 1.103 2025/03/27 23:30:54 tedu Exp $	*/
/*	$NetBSD: ffs_vnops.c,v 1.7 1996/05/11 18:27:24 mycroft Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1989, 1993
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
 *	@(#)ffs_vnops.c	8.10 (Berkeley) 8/10/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/resourcevar.h>
#include <sys/kernel.h>
#include <sys/stat.h>
#include <sys/buf.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/malloc.h>
#include <sys/signalvar.h>
#include <sys/pool.h>
#include <sys/event.h>
#include <sys/specdev.h>

#include <miscfs/fifofs/fifo.h>

#include <ufs/ufs/quota.h>
#include <ufs/ufs/inode.h>
#include <ufs/ufs/dir.h>
#include <ufs/ufs/ufs_extern.h>
#include <ufs/ufs/ufsmount.h>

#include <ufs/ffs/fs.h>
#include <ufs/ffs/ffs_extern.h>

const struct vops ffs_vops = {
	.vop_lookup	= ufs_lookup,
	.vop_create	= ufs_create,
	.vop_mknod	= ufs_mknod,
	.vop_open	= ufs_open,
	.vop_close	= ufs_close,
	.vop_access	= ufs_access,
	.vop_getattr	= ufs_getattr,
	.vop_setattr	= ufs_setattr,
	.vop_read	= ffs_read,
	.vop_write	= ffs_write,
	.vop_ioctl	= ufs_ioctl,
	.vop_kqfilter	= ufs_kqfilter,
	.vop_revoke	= vop_generic_revoke,
	.vop_fsync	= ffs_fsync,
	.vop_remove	= ufs_remove,
	.vop_link	= ufs_link,
	.vop_rename	= ufs_rename,
	.vop_mkdir	= ufs_mkdir,
	.vop_rmdir	= ufs_rmdir,
	.vop_symlink	= ufs_symlink,
	.vop_readdir	= ufs_readdir,
	.vop_readlink	= ufs_readlink,
	.vop_abortop	= vop_generic_abortop,
	.vop_inactive	= ufs_inactive,
	.vop_reclaim	= ffs_reclaim,
	.vop_lock	= ufs_lock,
	.vop_unlock	= ufs_unlock,
	.vop_bmap	= ufs_bmap,
	.vop_strategy	= ufs_strategy,
	.vop_print	= ufs_print,
	.vop_islocked	= ufs_islocked,
	.vop_pathconf	= ufs_pathconf,
	.vop_advlock	= ufs_advlock,
	.vop_bwrite	= vop_generic_bwrite
};

const struct vops ffs_specvops = {
	.vop_close	= ufsspec_close,
	.vop_access	= ufs_access,
	.vop_getattr	= ufs_getattr,
	.vop_setattr	= ufs_setattr,
	.vop_read	= ufsspec_read,
	.vop_write	= ufsspec_write,
	.vop_fsync	= ffs_fsync,
	.vop_inactive	= ufs_inactive,
	.vop_reclaim	= ffs_reclaim,
	.vop_lock	= ufs_lock,
	.vop_unlock	= ufs_unlock,
	.vop_print	= ufs_print,
	.vop_islocked	= ufs_islocked,

	/* XXX: Keep in sync with spec_vops */
	.vop_lookup	= vop_generic_lookup,
	.vop_create	= vop_generic_badop,
	.vop_mknod	= vop_generic_badop,
	.vop_open	= spec_open,
	.vop_ioctl	= spec_ioctl,
	.vop_kqfilter	= spec_kqfilter,
	.vop_revoke	= vop_generic_revoke,
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
const struct vops ffs_fifovops = {
	.vop_close	= ufsfifo_close,
	.vop_access	= ufs_access,
	.vop_getattr	= ufs_getattr,
	.vop_setattr	= ufs_setattr,
	.vop_read	= ufsfifo_read,
	.vop_write	= ufsfifo_write,
	.vop_fsync	= ffs_fsync,
	.vop_inactive	= ufs_inactive,
	.vop_reclaim	= ffsfifo_reclaim,
	.vop_lock	= ufs_lock,
	.vop_unlock	= ufs_unlock,
	.vop_print	= ufs_print,
	.vop_islocked	= ufs_islocked,
	.vop_bwrite	= vop_generic_bwrite,

	/* XXX: Keep in sync with fifo_vops */
	.vop_lookup	= vop_generic_lookup,
	.vop_create	= vop_generic_badop,
	.vop_mknod	= vop_generic_badop,
	.vop_open	= fifo_open,
	.vop_ioctl	= fifo_ioctl,
	.vop_kqfilter	= fifo_kqfilter,
	.vop_revoke	= vop_generic_revoke,
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
	.vop_advlock	= fifo_advlock
};
#endif /* FIFO */

/*
 * Vnode op for reading.
 */
int
ffs_read(void *v)
{
	struct vop_read_args *ap = v;
	struct vnode *vp;
	struct inode *ip;
	struct uio *uio;
	struct fs *fs;
	struct buf *bp;
	daddr_t lbn, nextlbn;
	off_t bytesinfile;
	int size, xfersize, blkoffset;
	mode_t mode;
	int error;

	vp = ap->a_vp;
	ip = VTOI(vp);
	mode = DIP(ip, mode);
	uio = ap->a_uio;

#ifdef DIAGNOSTIC
	if (uio->uio_rw != UIO_READ)
		panic("ffs_read: mode");

	if (vp->v_type == VLNK) {
		if (DIP(ip, size) < ip->i_ump->um_maxsymlinklen)
			panic("ffs_read: short symlink");
	} else if (vp->v_type != VREG && vp->v_type != VDIR)
		panic("ffs_read: type %d", vp->v_type);
#endif
	fs = ip->i_fs;
	if (uio->uio_offset < 0)
		return (EINVAL);
	if (uio->uio_resid == 0)
		return (0);

	for (error = 0, bp = NULL; uio->uio_resid > 0; bp = NULL) {
		if ((bytesinfile = DIP(ip, size) - uio->uio_offset) <= 0)
			break;
		lbn = lblkno(fs, uio->uio_offset);
		nextlbn = lbn + 1;
		size = fs->fs_bsize;	/* WAS blksize(fs, ip, lbn); */
		blkoffset = blkoff(fs, uio->uio_offset);
		xfersize = fs->fs_bsize - blkoffset;
		if (uio->uio_resid < xfersize)
			xfersize = uio->uio_resid;
		if (bytesinfile < xfersize)
			xfersize = bytesinfile;

		if (lblktosize(fs, nextlbn) >= DIP(ip, size))
			error = bread(vp, lbn, size, &bp);
		else if (lbn - 1 == ip->i_ci.ci_lastr ||
		    uio->uio_resid > xfersize) {
			error = bread_cluster(vp, lbn, size, &bp);
		} else
			error = bread(vp, lbn, size, &bp);

		if (error)
			break;
		ip->i_ci.ci_lastr = lbn;

		/*
		 * We should only get non-zero b_resid when an I/O error
		 * has occurred, which should cause us to break above.
		 * However, if the short read did not cause an error,
		 * then we want to ensure that we do not uiomove bad
		 * or uninitialized data.
		 */
		size -= bp->b_resid;
		if (size < xfersize) {
			if (size == 0)
				break;
			xfersize = size;
		}
		error = uiomove(bp->b_data + blkoffset, xfersize, uio);
		if (error)
			break;
		brelse(bp);
	}
	if (bp != NULL)
		brelse(bp);
	if (!(vp->v_mount->mnt_flag & MNT_NOATIME) ||
	    (ip->i_flag & (IN_CHANGE | IN_UPDATE))) {
		ip->i_flag |= IN_ACCESS;
	}
	return (error);
}

/*
 * Vnode op for writing.
 */
int
ffs_write(void *v)
{
	struct vop_write_args *ap = v;
	struct vnode *vp;
	struct uio *uio;
	struct inode *ip;
	struct fs *fs;
	struct buf *bp;
	daddr_t lbn;
	off_t osize;
	int blkoffset, error, extended, flags, ioflag, size, xfersize;
	size_t resid;
	ssize_t overrun;

	extended = 0;
	ioflag = ap->a_ioflag;
	uio = ap->a_uio;
	vp = ap->a_vp;
	ip = VTOI(vp);

#ifdef DIAGNOSTIC
	if (uio->uio_rw != UIO_WRITE)
		panic("ffs_write: mode");
#endif

	/*
	 * If writing 0 bytes, succeed and do not change
	 * update time or file offset (standards compliance)
	 */
	if (uio->uio_resid == 0)
		return (0);

	switch (vp->v_type) {
	case VREG:
		if (ioflag & IO_APPEND)
			uio->uio_offset = DIP(ip, size);
		if ((DIP(ip, flags) & APPEND) && uio->uio_offset != DIP(ip, size))
			return (EPERM);
		/* FALLTHROUGH */
	case VLNK:
		break;
	case VDIR:
		if ((ioflag & IO_SYNC) == 0)
			panic("ffs_write: nonsync dir write");
		break;
	default:
		panic("ffs_write: type %d", vp->v_type);
	}

	fs = ip->i_fs;
	if (uio->uio_offset < 0 ||
	    (u_int64_t)uio->uio_offset + uio->uio_resid > fs->fs_maxfilesize)
		return (EFBIG);

	/* do the filesize rlimit check */
	if ((error = vn_fsizechk(vp, uio, ioflag, &overrun)))
		return (error);

	resid = uio->uio_resid;
	osize = DIP(ip, size);
	flags = ioflag & IO_SYNC ? B_SYNC : 0;

	for (error = 0; uio->uio_resid > 0;) {
		lbn = lblkno(fs, uio->uio_offset);
		blkoffset = blkoff(fs, uio->uio_offset);
		xfersize = fs->fs_bsize - blkoffset;
		if (uio->uio_resid < xfersize)
			xfersize = uio->uio_resid;
		if (fs->fs_bsize > xfersize)
			flags |= B_CLRBUF;
		else
			flags &= ~B_CLRBUF;

		if ((error = UFS_BUF_ALLOC(ip, uio->uio_offset, xfersize,
			 ap->a_cred, flags, &bp)) != 0)
			break;
		if (uio->uio_offset + xfersize > DIP(ip, size)) {
			DIP_ASSIGN(ip, size, uio->uio_offset + xfersize);
			uvm_vnp_setsize(vp, DIP(ip, size));
			extended = 1;
		}
		(void)uvm_vnp_uncache(vp);

		size = blksize(fs, ip, lbn) - bp->b_resid;
		if (size < xfersize)
			xfersize = size;

		error = uiomove(bp->b_data + blkoffset, xfersize, uio);
		/*
		 * If the buffer is not already filled and we encounter an
		 * error while trying to fill it, we have to clear out any
		 * garbage data from the pages instantiated for the buffer.
		 * If we do not, a failed uiomove() during a write can leave
		 * the prior contents of the pages exposed to a userland mmap.
		 *
		 * Note that we don't need to clear buffers that were
		 * allocated with the B_CLRBUF flag set.
                 */
		if (error != 0 && !(flags & B_CLRBUF))
			memset(bp->b_data + blkoffset, 0, xfersize);

		if (ioflag & IO_NOCACHE)
			bp->b_flags |= B_NOCACHE;

		if (ioflag & IO_SYNC)
			(void)bwrite(bp);
		else if (xfersize + blkoffset == fs->fs_bsize) {
			bawrite(bp);
		} else
			bdwrite(bp);

		if (error || xfersize == 0)
			break;
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
	}
	/*
	 * If we successfully wrote any data, and we are not the superuser
	 * we clear the setuid and setgid bits as a precaution against
	 * tampering.
	 */
	if (resid > uio->uio_resid && ap->a_cred && ap->a_cred->cr_uid != 0 &&
	    !vnoperm(vp))
		DIP_ASSIGN(ip, mode, DIP(ip, mode) & ~(ISUID | ISGID));
	if (resid > uio->uio_resid)
		VN_KNOTE(vp, NOTE_WRITE | (extended ? NOTE_EXTEND : 0));
	if (error) {
		if (ioflag & IO_UNIT) {
			(void)UFS_TRUNCATE(ip, osize,
			    ioflag & IO_SYNC, ap->a_cred);
			uio->uio_offset -= resid - uio->uio_resid;
			uio->uio_resid = resid;
		}
	} else if (resid > uio->uio_resid && (ioflag & IO_SYNC)) {
		error = UFS_UPDATE(ip, 1);
	}
	/* correct the result for writes clamped by vn_fsizechk() */
	uio->uio_resid += overrun;
	return (error);
}

/*
 * Synch an open file.
 */
int
ffs_fsync(void *v)
{
	struct vop_fsync_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct buf *bp, *nbp;
	int s, error, passes, skipmeta;

	/*
	 * Flush all dirty buffers associated with a vnode.
	 */
	passes = NIADDR + 1;
	skipmeta = 0;
	if (ap->a_waitfor == MNT_WAIT)
		skipmeta = 1;
	s = splbio();
loop:
	LIST_FOREACH(bp, &vp->v_dirtyblkhd, b_vnbufs) {
		bp->b_flags &= ~B_SCANNED;
	}
	LIST_FOREACH_SAFE(bp, &vp->v_dirtyblkhd, b_vnbufs, nbp) {
		/* 
		 * Reasons to skip this buffer: it has already been considered
		 * on this pass, this pass is the first time through on a
		 * synchronous flush request and the buffer being considered
		 * is metadata, the buffer has dependencies that will cause
		 * it to be redirtied and it has not already been deferred,
		 * or it is already being written.
		 */
		if (bp->b_flags & (B_BUSY | B_SCANNED))
			continue;
		if ((bp->b_flags & B_DELWRI) == 0)
			panic("ffs_fsync: not dirty");
		if (skipmeta && bp->b_lblkno < 0)
			continue;

		bufcache_take(bp);
		buf_acquire(bp);
		bp->b_flags |= B_SCANNED;
		splx(s);
		/*
		 * On our final pass through, do all I/O synchronously
		 * so that we can find out if our flush is failing
		 * because of write errors.
		 */
		if (passes > 0 || ap->a_waitfor != MNT_WAIT)
			(void) bawrite(bp);
		else if ((error = bwrite(bp)) != 0)
			return (error);
		s = splbio();
		/*
		 * Since we may have slept during the I/O, we need
		 * to start from a known point.
		 */
		nbp = LIST_FIRST(&vp->v_dirtyblkhd);
	}
	if (skipmeta) {
		skipmeta = 0;
		goto loop;
	}
	if (ap->a_waitfor == MNT_WAIT) {
		vwaitforio(vp, 0, "ffs_fsync", INFSLP);

		/*
		 * Ensure that any filesystem metadata associated
		 * with the vnode has been written.
		 */
		splx(s);
		/* XXX softdep was here. reconsider this locking dance */
		s = splbio();
		if (!LIST_EMPTY(&vp->v_dirtyblkhd)) {
			/*
			 * Block devices associated with filesystems may
			 * have new I/O requests posted for them even if
			 * the vnode is locked, so no amount of trying will
			 * get them clean. Thus we give block devices a
			 * good effort, then just give up. For all other file
			 * types, go around and try again until it is clean.
			 */
			if (passes > 0) {
				passes -= 1;
				goto loop;
			}
#ifdef DIAGNOSTIC
			if (vp->v_type != VBLK)
				vprint("ffs_fsync: dirty", vp);
#endif
		}
	}
	splx(s);
	return (UFS_UPDATE(VTOI(vp), ap->a_waitfor == MNT_WAIT));
}

/*
 * Reclaim an inode so that it can be used for other purposes.
 */
int
ffs_reclaim(void *v)
{
	struct vop_reclaim_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct inode *ip = VTOI(vp);
	int error;

	if ((error = ufs_reclaim(vp)) != 0)
		return (error);

	if (ip->i_din1 != NULL) {
#ifdef FFS2
		if (ip->i_ump->um_fstype == UM_UFS2)
			pool_put(&ffs_dinode2_pool, ip->i_din2);
		else
#endif
			pool_put(&ffs_dinode1_pool, ip->i_din1);
	}

	pool_put(&ffs_ino_pool, ip);

	vp->v_data = NULL;

	return (0);
}

#ifdef FIFO
int
ffsfifo_reclaim(void *v)
{
	fifo_reclaim(v);
	return (ffs_reclaim(v));
}
#endif
