/*-
 *  modified for Lites 1.1
 *
 *  Aug 1995, Godmar Back (gback@cs.utah.edu)
 *  University of Utah, Department of Computer Science
 */
/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 *	@(#)ffs_inode.c	8.5 (Berkeley) 12/30/93
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mount.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/malloc.h>
#include <sys/rwlock.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>

#include <fs/ext2fs/fs.h>
#include <fs/ext2fs/inode.h>
#include <fs/ext2fs/ext2_mount.h>
#include <fs/ext2fs/ext2fs.h>
#include <fs/ext2fs/fs.h>
#include <fs/ext2fs/ext2_extern.h>
#include <fs/ext2fs/ext2_extattr.h>

/*
 * Update the access, modified, and inode change times as specified by the
 * IN_ACCESS, IN_UPDATE, and IN_CHANGE flags respectively.  Write the inode
 * to disk if the IN_MODIFIED flag is set (it may be set initially, or by
 * the timestamp update).  The IN_LAZYMOD flag is set to force a write
 * later if not now.  If we write now, then clear both IN_MODIFIED and
 * IN_LAZYMOD to reflect the presumably successful write, and if waitfor is
 * set, then wait for the write to complete.
 */
int
ext2_update(struct vnode *vp, int waitfor)
{
	struct m_ext2fs *fs;
	struct buf *bp;
	struct inode *ip;
	int error;

	ASSERT_VOP_ELOCKED(vp, "ext2_update");
	ext2_itimes(vp);
	ip = VTOI(vp);
	if ((ip->i_flag & IN_MODIFIED) == 0 && waitfor == 0)
		return (0);
	ip->i_flag &= ~(IN_LAZYACCESS | IN_LAZYMOD | IN_MODIFIED);
	fs = ip->i_e2fs;
	if (fs->e2fs_ronly)
		return (0);
	if ((error = bread(ip->i_devvp,
	    fsbtodb(fs, ino_to_fsba(fs, ip->i_number)),
	    (int)fs->e2fs_bsize, NOCRED, &bp)) != 0) {
		brelse(bp);
		return (error);
	}
	error = ext2_i2ei(ip, (struct ext2fs_dinode *)((char *)bp->b_data +
	    EXT2_INODE_SIZE(fs) * ino_to_fsbo(fs, ip->i_number)));
	if (error) {
		brelse(bp);
		return (error);
	}
	if (waitfor && !DOINGASYNC(vp))
		return (bwrite(bp));
	else {
		bdwrite(bp);
		return (0);
	}
}

#define	SINGLE	0	/* index of single indirect block */
#define	DOUBLE	1	/* index of double indirect block */
#define	TRIPLE	2	/* index of triple indirect block */

/*
 * Release blocks associated with the inode ip and stored in the indirect
 * block bn.  Blocks are free'd in LIFO order up to (but not including)
 * lastbn.  If level is greater than SINGLE, the block is an indirect block
 * and recursive calls to indirtrunc must be used to cleanse other indirect
 * blocks.
 *
 * NB: triple indirect blocks are untested.
 */
static int
ext2_indirtrunc(struct inode *ip, daddr_t lbn, daddr_t dbn,
    daddr_t lastbn, int level, e4fs_daddr_t *countp)
{
	struct buf *bp;
	struct m_ext2fs *fs = ip->i_e2fs;
	struct vnode *vp;
	e2fs_daddr_t *bap, *copy;
	int i, nblocks, error = 0, allerror = 0;
	e2fs_lbn_t nb, nlbn, last;
	e4fs_daddr_t blkcount, factor, blocksreleased = 0;

	/*
	 * Calculate index in current block of last
	 * block to be kept.  -1 indicates the entire
	 * block so we need not calculate the index.
	 */
	factor = 1;
	for (i = SINGLE; i < level; i++)
		factor *= NINDIR(fs);
	last = lastbn;
	if (lastbn > 0)
		last /= factor;
	nblocks = btodb(fs->e2fs_bsize);
	/*
	 * Get buffer of block pointers, zero those entries corresponding
	 * to blocks to be free'd, and update on disk copy first.  Since
	 * double(triple) indirect before single(double) indirect, calls
	 * to bmap on these blocks will fail.  However, we already have
	 * the on disk address, so we have to set the b_blkno field
	 * explicitly instead of letting bread do everything for us.
	 */
	vp = ITOV(ip);
	bp = getblk(vp, lbn, (int)fs->e2fs_bsize, 0, 0, 0);
	if ((bp->b_flags & (B_DONE | B_DELWRI)) == 0) {
		bp->b_iocmd = BIO_READ;
		if (bp->b_bcount > bp->b_bufsize)
			panic("ext2_indirtrunc: bad buffer size");
		bp->b_blkno = dbn;
		vfs_busy_pages(bp, 0);
		bp->b_iooffset = dbtob(bp->b_blkno);
		bstrategy(bp);
		error = bufwait(bp);
	}
	if (error) {
		brelse(bp);
		*countp = 0;
		return (error);
	}
	bap = (e2fs_daddr_t *)bp->b_data;
	copy = malloc(fs->e2fs_bsize, M_TEMP, M_WAITOK);
	bcopy((caddr_t)bap, (caddr_t)copy, (u_int)fs->e2fs_bsize);
	bzero((caddr_t)&bap[last + 1],
	    (NINDIR(fs) - (last + 1)) * sizeof(e2fs_daddr_t));
	if (last == -1)
		bp->b_flags |= B_INVAL;
	if (DOINGASYNC(vp)) {
		bdwrite(bp);
	} else {
		error = bwrite(bp);
		if (error)
			allerror = error;
	}
	bap = copy;

	/*
	 * Recursively free totally unused blocks.
	 */
	for (i = NINDIR(fs) - 1, nlbn = lbn + 1 - i * factor; i > last;
	    i--, nlbn += factor) {
		nb = bap[i];
		if (nb == 0)
			continue;
		if (level > SINGLE) {
			if ((error = ext2_indirtrunc(ip, nlbn,
			    fsbtodb(fs, nb), (int32_t)-1, level - 1, &blkcount)) != 0)
				allerror = error;
			blocksreleased += blkcount;
		}
		ext2_blkfree(ip, nb, fs->e2fs_bsize);
		blocksreleased += nblocks;
	}

	/*
	 * Recursively free last partial block.
	 */
	if (level > SINGLE && lastbn >= 0) {
		last = lastbn % factor;
		nb = bap[i];
		if (nb != 0) {
			if ((error = ext2_indirtrunc(ip, nlbn, fsbtodb(fs, nb),
			    last, level - 1, &blkcount)) != 0)
				allerror = error;
			blocksreleased += blkcount;
		}
	}
	free(copy, M_TEMP);
	*countp = blocksreleased;
	return (allerror);
}

/*
 * Truncate the inode oip to at most length size, freeing the
 * disk blocks.
 */
static int
ext2_ind_truncate(struct vnode *vp, off_t length, int flags, struct ucred *cred,
    struct thread *td)
{
	struct vnode *ovp = vp;
	e4fs_daddr_t lastblock;
	struct inode *oip;
	e4fs_daddr_t bn, lbn, lastiblock[EXT2_NIADDR], indir_lbn[EXT2_NIADDR];
	uint32_t oldblks[EXT2_NDADDR + EXT2_NIADDR];
	uint32_t newblks[EXT2_NDADDR + EXT2_NIADDR];
	struct m_ext2fs *fs;
	struct buf *bp;
	int offset, size, level;
	e4fs_daddr_t count, nblocks, blocksreleased = 0;
	int error, i, allerror;
	off_t osize;
#ifdef INVARIANTS
	struct bufobj *bo;
#endif

	oip = VTOI(ovp);
#ifdef INVARIANTS
	bo = &ovp->v_bufobj;
#endif

	fs = oip->i_e2fs;
	osize = oip->i_size;
	/*
	 * Lengthen the size of the file. We must ensure that the
	 * last byte of the file is allocated. Since the smallest
	 * value of osize is 0, length will be at least 1.
	 */
	if (osize < length) {
		if (length > oip->i_e2fs->e2fs_maxfilesize)
			return (EFBIG);
		vnode_pager_setsize(ovp, length);
		offset = blkoff(fs, length - 1);
		lbn = lblkno(fs, length - 1);
		flags |= BA_CLRBUF;
		error = ext2_balloc(oip, lbn, offset + 1, cred, &bp, flags);
		if (error) {
			vnode_pager_setsize(vp, osize);
			return (error);
		}
		oip->i_size = length;
		if (bp->b_bufsize == fs->e2fs_bsize)
			bp->b_flags |= B_CLUSTEROK;
		if (flags & IO_SYNC)
			bwrite(bp);
		else if (DOINGASYNC(ovp))
			bdwrite(bp);
		else
			bawrite(bp);
		oip->i_flag |= IN_CHANGE | IN_UPDATE;
		return (ext2_update(ovp, !DOINGASYNC(ovp)));
	}
	/*
	 * Shorten the size of the file. If the file is not being
	 * truncated to a block boundary, the contents of the
	 * partial block following the end of the file must be
	 * zero'ed in case it ever become accessible again because
	 * of subsequent file growth.
	 */
	/* I don't understand the comment above */
	offset = blkoff(fs, length);
	if (offset == 0) {
		oip->i_size = length;
	} else {
		lbn = lblkno(fs, length);
		flags |= BA_CLRBUF;
		error = ext2_balloc(oip, lbn, offset, cred, &bp, flags);
		if (error)
			return (error);
		oip->i_size = length;
		size = blksize(fs, oip, lbn);
		bzero((char *)bp->b_data + offset, (u_int)(size - offset));
		allocbuf(bp, size);
		if (bp->b_bufsize == fs->e2fs_bsize)
			bp->b_flags |= B_CLUSTEROK;
		if (flags & IO_SYNC)
			bwrite(bp);
		else if (DOINGASYNC(ovp))
			bdwrite(bp);
		else
			bawrite(bp);
	}
	/*
	 * Calculate index into inode's block list of
	 * last direct and indirect blocks (if any)
	 * which we want to keep.  Lastblock is -1 when
	 * the file is truncated to 0.
	 */
	lastblock = lblkno(fs, length + fs->e2fs_bsize - 1) - 1;
	lastiblock[SINGLE] = lastblock - EXT2_NDADDR;
	lastiblock[DOUBLE] = lastiblock[SINGLE] - NINDIR(fs);
	lastiblock[TRIPLE] = lastiblock[DOUBLE] - NINDIR(fs) * NINDIR(fs);
	nblocks = btodb(fs->e2fs_bsize);
	/*
	 * Update file and block pointers on disk before we start freeing
	 * blocks.  If we crash before free'ing blocks below, the blocks
	 * will be returned to the free list.  lastiblock values are also
	 * normalized to -1 for calls to ext2_indirtrunc below.
	 */
	for (level = TRIPLE; level >= SINGLE; level--) {
		oldblks[EXT2_NDADDR + level] = oip->i_ib[level];
		if (lastiblock[level] < 0) {
			oip->i_ib[level] = 0;
			lastiblock[level] = -1;
		}
	}
	for (i = 0; i < EXT2_NDADDR; i++) {
		oldblks[i] = oip->i_db[i];
		if (i > lastblock)
			oip->i_db[i] = 0;
	}
	oip->i_flag |= IN_CHANGE | IN_UPDATE;
	allerror = ext2_update(ovp, !DOINGASYNC(ovp));

	/*
	 * Having written the new inode to disk, save its new configuration
	 * and put back the old block pointers long enough to process them.
	 * Note that we save the new block configuration so we can check it
	 * when we are done.
	 */
	for (i = 0; i < EXT2_NDADDR; i++) {
		newblks[i] = oip->i_db[i];
		oip->i_db[i] = oldblks[i];
	}
	for (i = 0; i < EXT2_NIADDR; i++) {
		newblks[EXT2_NDADDR + i] = oip->i_ib[i];
		oip->i_ib[i] = oldblks[EXT2_NDADDR + i];
	}
	oip->i_size = osize;
	error = vtruncbuf(ovp, cred, length, (int)fs->e2fs_bsize);
	if (error && (allerror == 0))
		allerror = error;
	vnode_pager_setsize(ovp, length);

	/*
	 * Indirect blocks first.
	 */
	indir_lbn[SINGLE] = -EXT2_NDADDR;
	indir_lbn[DOUBLE] = indir_lbn[SINGLE] - NINDIR(fs) - 1;
	indir_lbn[TRIPLE] = indir_lbn[DOUBLE] - NINDIR(fs) * NINDIR(fs) - 1;
	for (level = TRIPLE; level >= SINGLE; level--) {
		bn = oip->i_ib[level];
		if (bn != 0) {
			error = ext2_indirtrunc(oip, indir_lbn[level],
			    fsbtodb(fs, bn), lastiblock[level], level, &count);
			if (error)
				allerror = error;
			blocksreleased += count;
			if (lastiblock[level] < 0) {
				oip->i_ib[level] = 0;
				ext2_blkfree(oip, bn, fs->e2fs_fsize);
				blocksreleased += nblocks;
			}
		}
		if (lastiblock[level] >= 0)
			goto done;
	}

	/*
	 * All whole direct blocks or frags.
	 */
	for (i = EXT2_NDADDR - 1; i > lastblock; i--) {
		long bsize;

		bn = oip->i_db[i];
		if (bn == 0)
			continue;
		oip->i_db[i] = 0;
		bsize = blksize(fs, oip, i);
		ext2_blkfree(oip, bn, bsize);
		blocksreleased += btodb(bsize);
	}
	if (lastblock < 0)
		goto done;

	/*
	 * Finally, look for a change in size of the
	 * last direct block; release any frags.
	 */
	bn = oip->i_db[lastblock];
	if (bn != 0) {
		long oldspace, newspace;

		/*
		 * Calculate amount of space we're giving
		 * back as old block size minus new block size.
		 */
		oldspace = blksize(fs, oip, lastblock);
		oip->i_size = length;
		newspace = blksize(fs, oip, lastblock);
		if (newspace == 0)
			panic("ext2_truncate: newspace");
		if (oldspace - newspace > 0) {
			/*
			 * Block number of space to be free'd is
			 * the old block # plus the number of frags
			 * required for the storage we're keeping.
			 */
			bn += numfrags(fs, newspace);
			ext2_blkfree(oip, bn, oldspace - newspace);
			blocksreleased += btodb(oldspace - newspace);
		}
	}
done:
#ifdef INVARIANTS
	for (level = SINGLE; level <= TRIPLE; level++)
		if (newblks[EXT2_NDADDR + level] != oip->i_ib[level])
			panic("itrunc1");
	for (i = 0; i < EXT2_NDADDR; i++)
		if (newblks[i] != oip->i_db[i])
			panic("itrunc2");
	BO_LOCK(bo);
	if (length == 0 && (bo->bo_dirty.bv_cnt != 0 ||
	    bo->bo_clean.bv_cnt != 0))
		panic("itrunc3");
	BO_UNLOCK(bo);
#endif	/* INVARIANTS */
	/*
	 * Put back the real size.
	 */
	oip->i_size = length;
	if (oip->i_blocks >= blocksreleased)
		oip->i_blocks -= blocksreleased;
	else				/* sanity */
		oip->i_blocks = 0;
	oip->i_flag |= IN_CHANGE;
	vnode_pager_setsize(ovp, length);
	return (allerror);
}

static int
ext2_ext_truncate(struct vnode *vp, off_t length, int flags,
    struct ucred *cred, struct thread *td)
{
	struct vnode *ovp = vp;
	int32_t lastblock;
	struct m_ext2fs *fs;
	struct inode *oip;
	struct buf *bp;
	uint32_t lbn, offset;
	int error, size;
	off_t osize;

	oip = VTOI(ovp);
	fs = oip->i_e2fs;
	osize = oip->i_size;

	if (osize < length) {
		if (length > oip->i_e2fs->e2fs_maxfilesize) {
			return (EFBIG);
		}
		vnode_pager_setsize(ovp, length);
		offset = blkoff(fs, length - 1);
		lbn = lblkno(fs, length - 1);
		flags |= BA_CLRBUF;
		error = ext2_balloc(oip, lbn, offset + 1, cred, &bp, flags);
		if (error) {
			vnode_pager_setsize(vp, osize);
			return (error);
		}
		oip->i_size = length;
		if (bp->b_bufsize == fs->e2fs_bsize)
			bp->b_flags |= B_CLUSTEROK;
		if (flags & IO_SYNC)
			bwrite(bp);
		else if (DOINGASYNC(ovp))
			bdwrite(bp);
		else
			bawrite(bp);
		oip->i_flag |= IN_CHANGE | IN_UPDATE;
		return (ext2_update(ovp, !DOINGASYNC(ovp)));
	}

	lastblock = (length + fs->e2fs_bsize - 1) / fs->e2fs_bsize;
	error = ext4_ext_remove_space(oip, lastblock, flags, cred, td);
	if (error)
		return (error);

	offset = blkoff(fs, length);
	if (offset == 0) {
		oip->i_size = length;
	} else {
		lbn = lblkno(fs, length);
		flags |= BA_CLRBUF;
		error = ext2_balloc(oip, lbn, offset, cred, &bp, flags);
		if (error) {
			return (error);
		}
		oip->i_size = length;
		size = blksize(fs, oip, lbn);
		bzero((char *)bp->b_data + offset, (u_int)(size - offset));
		allocbuf(bp, size);
		if (bp->b_bufsize == fs->e2fs_bsize)
			bp->b_flags |= B_CLUSTEROK;
		if (flags & IO_SYNC)
			bwrite(bp);
		else if (DOINGASYNC(ovp))
			bdwrite(bp);
		else
			bawrite(bp);
	}

	oip->i_size = osize;
	error = vtruncbuf(ovp, cred, length, (int)fs->e2fs_bsize);
	if (error)
		return (error);

	vnode_pager_setsize(ovp, length);

	oip->i_size = length;
	oip->i_flag |= IN_CHANGE | IN_UPDATE;
	error = ext2_update(ovp, !DOINGASYNC(ovp));

	return (error);
}

/*
 * Truncate the inode ip to at most length size, freeing the
 * disk blocks.
 */
int
ext2_truncate(struct vnode *vp, off_t length, int flags, struct ucred *cred,
    struct thread *td)
{
	struct inode *ip;
	int error;

	ASSERT_VOP_LOCKED(vp, "ext2_truncate");

	if (length < 0)
		return (EINVAL);

	ip = VTOI(vp);
	if (vp->v_type == VLNK &&
	    ip->i_size < vp->v_mount->mnt_maxsymlinklen) {
#ifdef INVARIANTS
		if (length != 0)
			panic("ext2_truncate: partial truncate of symlink");
#endif
		bzero((char *)&ip->i_shortlink, (u_int)ip->i_size);
		ip->i_size = 0;
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
		return (ext2_update(vp, 1));
	}
	if (ip->i_size == length) {
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
		return (ext2_update(vp, 0));
	}

	if (ip->i_flag & IN_E4EXTENTS)
		error = ext2_ext_truncate(vp, length, flags, cred, td);
	else
		error = ext2_ind_truncate(vp, length, flags, cred, td);

	return (error);
}

/*
 *	discard preallocated blocks
 */
int
ext2_inactive(struct vop_inactive_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct inode *ip = VTOI(vp);
	struct thread *td = ap->a_td;
	int mode, error = 0;

	/*
	 * Ignore inodes related to stale file handles.
	 */
	if (ip->i_mode == 0)
		goto out;
	if (ip->i_nlink <= 0) {
		ext2_extattr_free(ip);
		error = ext2_truncate(vp, (off_t)0, 0, NOCRED, td);
		if (!(ip->i_flag & IN_E4EXTENTS))
			ip->i_rdev = 0;
		mode = ip->i_mode;
		ip->i_mode = 0;
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
		ext2_vfree(vp, ip->i_number, mode);
	}
	if (ip->i_flag & (IN_ACCESS | IN_CHANGE | IN_MODIFIED | IN_UPDATE))
		ext2_update(vp, 0);
out:
	/*
	 * If we are done with the inode, reclaim it
	 * so that it can be reused immediately.
	 */
	if (ip->i_mode == 0)
		vrecycle(vp);
	return (error);
}

/*
 * Reclaim an inode so that it can be used for other purposes.
 */
int
ext2_reclaim(struct vop_reclaim_args *ap)
{
	struct inode *ip;
	struct vnode *vp = ap->a_vp;

	ip = VTOI(vp);
	if (ip->i_flag & IN_LAZYMOD) {
		ip->i_flag |= IN_MODIFIED;
		ext2_update(vp, 0);
	}
	vfs_hash_remove(vp);
	free(vp->v_data, M_EXT2NODE);
	vp->v_data = 0;
	vnode_destroy_vobject(vp);
	return (0);
}
