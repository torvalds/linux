/*	$OpenBSD: ext2fs_readwrite.c,v 1.46 2023/03/08 04:43:09 guenther Exp $	*/
/*	$NetBSD: ext2fs_readwrite.c,v 1.16 2001/02/27 04:37:47 chs Exp $	*/

/*-
 * Copyright (c) 1997 Manuel Bouyer.
 * Copyright (c) 1993
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
 *	@(#)ufs_readwrite.c	8.8 (Berkeley) 8/4/94
 * Modified for ext2fs by Manuel Bouyer.
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
#include <sys/event.h>

#include <ufs/ufs/quota.h>
#include <ufs/ufs/ufsmount.h>
#include <ufs/ufs/inode.h>
#include <ufs/ext2fs/ext2fs.h>
#include <ufs/ext2fs/ext2fs_extern.h>


static int	ext2_ind_read(struct vnode *, struct inode *, struct m_ext2fs *, struct uio *);
static int	ext4_ext_read(struct vnode *, struct inode *, struct m_ext2fs *, struct uio *);

/*
 * Vnode op for reading.
 */
int
ext2fs_read(void *v)
{
	struct vop_read_args *ap = v;
	struct vnode *vp;
	struct inode *ip;
	struct uio *uio;
	struct m_ext2fs *fs;

	vp = ap->a_vp;
	ip = VTOI(vp);
	uio = ap->a_uio;
	fs = ip->i_e2fs;

	if (ip->i_e2fs_flags & EXT4_EXTENTS)
		return ext4_ext_read(vp, ip, fs, uio);
	else
		return ext2_ind_read(vp, ip, fs, uio);
}

static int
ext2_ind_read(struct vnode *vp, struct inode *ip, struct m_ext2fs *fs,
    struct uio *uio)
{
	struct buf *bp;
	daddr_t lbn, nextlbn;
	off_t bytesinfile;
	int size, xfersize, blkoffset;
	int error;

#ifdef DIAGNOSTIC
	if (uio->uio_rw != UIO_READ)
		panic("%s: mode", "ext2fs_read");

	if (vp->v_type == VLNK) {
		if (ext2fs_size(ip) < EXT2_MAXSYMLINKLEN)
			panic("%s: short symlink", "ext2fs_read");
	} else if (vp->v_type != VREG && vp->v_type != VDIR)
		panic("%s: type %d", "ext2fs_read", vp->v_type);
#endif
	if (uio->uio_offset < 0)
		return (EINVAL);
	if (uio->uio_resid == 0)
		return (0);

	for (error = 0, bp = NULL; uio->uio_resid > 0; bp = NULL) {
		if ((bytesinfile = ext2fs_size(ip) - uio->uio_offset) <= 0)
			break;
		lbn = lblkno(fs, uio->uio_offset);
		nextlbn = lbn + 1;
		size = fs->e2fs_bsize;
		blkoffset = blkoff(fs, uio->uio_offset);
		xfersize = fs->e2fs_bsize - blkoffset;
		if (uio->uio_resid < xfersize)
			xfersize = uio->uio_resid;
		if (bytesinfile < xfersize)
			xfersize = bytesinfile;

		if (lblktosize(fs, nextlbn) >= ext2fs_size(ip))
			error = bread(vp, lbn, size, &bp);
		else if (lbn - 1 == ip->i_ci.ci_lastr) {
			int nextsize = fs->e2fs_bsize;
			error = breadn(vp, lbn, size, &nextlbn, &nextsize,
			    1, &bp);
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
		error = uiomove((char *)bp->b_data + blkoffset, xfersize, uio);
		if (error)
			break;
		brelse(bp);
	}
	if (bp != NULL)
		brelse(bp);

	if (!(vp->v_mount->mnt_flag & MNT_NOATIME)) {
		ip->i_flag |= IN_ACCESS;
	}
	return (error);
}

int
ext4_ext_read(struct vnode *vp, struct inode *ip, struct m_ext2fs *fs, struct uio *uio)
{
	struct ext4_extent_path path;
	struct ext4_extent nex, *ep;
	struct buf *bp;
	daddr_t lbn, pos;
	off_t bytesinfile;
	int size, xfersize, blkoffset;
	int error, cache_type;

	memset(&path, 0, sizeof path);

	if (uio->uio_offset < 0)
		return (EINVAL);
	if (uio->uio_resid == 0)
		return (0);

	while (uio->uio_resid > 0) {
		if ((bytesinfile = ext2fs_size(ip) - uio->uio_offset) <= 0)
			break;
		lbn = lblkno(fs, uio->uio_offset);
		size = fs->e2fs_bsize;
		blkoffset = blkoff(fs, uio->uio_offset);

		xfersize = fs->e2fs_fsize - blkoffset;
		xfersize = MIN(xfersize, uio->uio_resid);
		xfersize = MIN(xfersize, bytesinfile);

		cache_type = ext4_ext_in_cache(ip, lbn, &nex);
		switch (cache_type) {
		case EXT4_EXT_CACHE_NO:
			ext4_ext_find_extent(fs, ip, lbn, &path);
			if ((ep = path.ep_ext) == NULL)
				return (EIO);
			ext4_ext_put_cache(ip, ep, EXT4_EXT_CACHE_IN);

			pos = lbn - ep->e_blk + (((daddr_t) ep->e_start_hi << 32) | ep->e_start_lo);
			if (path.ep_bp != NULL) {
				brelse(path.ep_bp);
				path.ep_bp = NULL;
			}
			break;
		case EXT4_EXT_CACHE_GAP:
			/* block has not been allocated yet */
			return (0);
		case EXT4_EXT_CACHE_IN:
			pos = lbn - nex.e_blk + (((daddr_t) nex.e_start_hi << 32) | nex.e_start_lo);
			break;
		}
		error = bread(ip->i_devvp, fsbtodb(fs, pos), size, &bp);
		if (error) {
			brelse(bp);
			return (error);
		}
		size -= bp->b_resid;
		if (size < xfersize) {
			if (size == 0) {
				brelse(bp);
				break;
			}
			xfersize = size;
		}
		error = uiomove(bp->b_data + blkoffset, xfersize, uio);
		brelse(bp);
		if (error)
			return (error);
	}
	return (0);
}

/*
 * Vnode op for writing.
 */
int
ext2fs_write(void *v)
{
	struct vop_write_args *ap = v;
	struct vnode *vp;
	struct uio *uio;
	struct inode *ip;
	struct m_ext2fs *fs;
	struct buf *bp;
	int32_t lbn;
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
		panic("%s: mode", "ext2fs_write");
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
			uio->uio_offset = ext2fs_size(ip);
		if ((ip->i_e2fs_flags & EXT2_APPEND) &&
			uio->uio_offset != ext2fs_size(ip))
			return (EPERM);
		/* FALLTHROUGH */
	case VLNK:
		break;
	case VDIR:
		if ((ioflag & IO_SYNC) == 0)
			panic("%s: nonsync dir write", "ext2fs_write");
		break;
	default:
		panic("%s: type", "ext2fs_write");
	}

	fs = ip->i_e2fs;
	if (e2fs_overflow(fs, uio->uio_resid, uio->uio_offset + uio->uio_resid))
		return (EFBIG);

	/* do the filesize rlimit check */
	if ((error = vn_fsizechk(vp, uio, ioflag, &overrun)))
		return (error);

	resid = uio->uio_resid;
	osize = ext2fs_size(ip);
	flags = ioflag & IO_SYNC ? B_SYNC : 0;

	for (error = 0; uio->uio_resid > 0;) {
		lbn = lblkno(fs, uio->uio_offset);
		blkoffset = blkoff(fs, uio->uio_offset);
		xfersize = fs->e2fs_bsize - blkoffset;
		if (uio->uio_resid < xfersize)
			xfersize = uio->uio_resid;
		if (fs->e2fs_bsize > xfersize)
			flags |= B_CLRBUF;
		else
			flags &= ~B_CLRBUF;

		error = ext2fs_buf_alloc(ip,
			lbn, blkoffset + xfersize, ap->a_cred, &bp, flags);
		if (error)
			break;
		if (uio->uio_offset + xfersize > ext2fs_size(ip)) {
			error = ext2fs_setsize(ip, uio->uio_offset + xfersize);
			if (error)
				break;
			uvm_vnp_setsize(vp, ext2fs_size(ip));
			extended = 1;
		}
		uvm_vnp_uncache(vp);

		size = fs->e2fs_bsize - bp->b_resid;
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
		else if (xfersize + blkoffset == fs->e2fs_bsize)
			bawrite(bp);
		else
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
	if (resid > uio->uio_resid && ap->a_cred && ap->a_cred->cr_uid != 0)
		ip->i_e2fs_mode &= ~(ISUID | ISGID);
	if (resid > uio->uio_resid)
		VN_KNOTE(vp, NOTE_WRITE | (extended ? NOTE_EXTEND : 0));
	if (error) {
		if (ioflag & IO_UNIT) {
			(void)ext2fs_truncate(ip, osize,
				ioflag & IO_SYNC, ap->a_cred);
			uio->uio_offset -= resid - uio->uio_resid;
			uio->uio_resid = resid;
		}
	} else if (resid > uio->uio_resid && (ioflag & IO_SYNC)) {
		error = ext2fs_update(ip, 1);
	}
	/* correct the result for writes clamped by vn_fsizechk() */
	uio->uio_resid += overrun;
	return (error);
}
