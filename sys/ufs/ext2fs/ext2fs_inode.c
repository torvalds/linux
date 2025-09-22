/*	$OpenBSD: ext2fs_inode.c,v 1.68 2024/07/13 14:37:56 beck Exp $	*/
/*	$NetBSD: ext2fs_inode.c,v 1.24 2001/06/19 12:59:18 wiz Exp $	*/

/*
 * Copyright (c) 1997 Manuel Bouyer.
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
 *	@(#)ffs_inode.c	8.8 (Berkeley) 10/19/94
 * Modified for ext2fs by Manuel Bouyer.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/resourcevar.h>

#include <ufs/ufs/quota.h>
#include <ufs/ufs/inode.h>
#include <ufs/ufs/ufsmount.h>
#include <ufs/ufs/ufs_extern.h>

#include <ufs/ext2fs/ext2fs.h>
#include <ufs/ext2fs/ext2fs_extern.h>

static int ext2fs_indirtrunc(struct inode *, int32_t, int32_t,
				int32_t, int, long *);

/*
 * Get the size of an inode.
 */
u_int64_t
ext2fs_size(struct inode *ip)
{
	u_int64_t size = ip->i_e2fs_size;

	if ((ip->i_e2fs_mode & IFMT) == IFREG)
		size |= (u_int64_t)ip->i_e2fs_size_hi << 32;

	return (size);
}

int
ext2fs_setsize(struct inode *ip, u_int64_t size)
{
	struct m_ext2fs *fs = ip->i_e2fs;

	if (size <= fs->e2fs_maxfilesize) {
		/* If HUGE_FILEs are off, e2fs_maxfilesize will protect us. */
		if ((ip->i_e2fs_mode & IFMT) == IFREG || ip->i_e2fs_mode == 0)
			ip->i_e2fs_size_hi = size >> 32;

		ip->i_e2fs_size = size;
		return (0);
	}

	/* Linux automagically upgrades to REV1 here! */
	if (fs->e2fs.e2fs_rev <= E2FS_REV0)
		return (EFBIG);

	if (!(fs->e2fs.e2fs_features_rocompat & EXT2F_ROCOMPAT_LARGE_FILE)) {
		fs->e2fs.e2fs_features_rocompat |= EXT2F_ROCOMPAT_LARGE_FILE;
		fs->e2fs_fmod = 1;
	}
	return (EFBIG);
}


/*
 * Last reference to an inode.  If necessary, write or delete it.
 */
int
ext2fs_inactive(void *v)
{
	struct vop_inactive_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct inode *ip = VTOI(vp);
	struct timespec ts;
	int error = 0;
#ifdef DIAGNOSTIC
	extern int prtactive;

	if (prtactive && vp->v_usecount != 0)
		vprint("ext2fs_inactive: pushing active", vp);
#endif

	/* Get rid of inodes related to stale file handles. */
	if (ip->i_e2din == NULL || ip->i_e2fs_mode == 0 || ip->i_e2fs_dtime)
		goto out;

	error = 0;
	if (ip->i_e2fs_nlink == 0 && (vp->v_mount->mnt_flag & MNT_RDONLY) == 0) {
		if (ext2fs_size(ip) != 0) {
			error = ext2fs_truncate(ip, (off_t)0, 0, NOCRED);
		}
		getnanotime(&ts);
		ip->i_e2fs_dtime = ts.tv_sec;
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
		ext2fs_inode_free(ip, ip->i_number, ip->i_e2fs_mode);
	}
	if (ip->i_flag & (IN_ACCESS | IN_CHANGE | IN_MODIFIED | IN_UPDATE)) {
		ext2fs_update(ip, 0);
	}
out:
	VOP_UNLOCK(vp);
	/*
	 * If we are done with the inode, reclaim it
	 * so that it can be reused immediately.
	 */
	if (ip->i_e2din == NULL || ip->i_e2fs_dtime != 0)
		vrecycle(vp, ap->a_p);
	return (error);
}


/*
 * Update the access, modified, and inode change times as specified by the
 * IACCESS, IUPDATE, and ICHANGE flags respectively. The IMODIFIED flag is
 * used to specify that the inode needs to be updated but that the times have
 * already been set. The access and modified times are taken from the second
 * and third parameters; the inode change time is always taken from the current
 * time. If waitfor is set, then wait for the disk write of the inode to
 * complete.
 */
int
ext2fs_update(struct inode *ip, int waitfor)
{
	struct m_ext2fs *fs;
	struct buf *bp;
	int error;
	caddr_t cp;

	if (ITOV(ip)->v_mount->mnt_flag & MNT_RDONLY)
		return (0);
	EXT2FS_ITIMES(ip);
	if ((ip->i_flag & IN_MODIFIED) == 0)
		return (0);
	ip->i_flag &= ~IN_MODIFIED;
	fs = ip->i_e2fs;
	error = bread(ip->i_devvp,
			  fsbtodb(fs, ino_to_fsba(fs, ip->i_number)),
			  (int)fs->e2fs_bsize, &bp);
	if (error) {
		brelse(bp);
		return (error);
	}
	ip->i_flag &= ~(IN_MODIFIED);
	cp = (caddr_t)bp->b_data +
	    (ino_to_fsbo(fs, ip->i_number) * EXT2_DINODE_SIZE(fs));

	/*
	 * See note about 16-bit UID/GID limitation in ext2fs_vget(). Now
	 * that we are about to write the inode, construct the split UID and
	 * GID fields out of the two 32-bit fields we kept in memory.
	 */
	ip->i_e2fs_uid_low = (u_int16_t)ip->i_e2fs_uid;
	ip->i_e2fs_gid_low = (u_int16_t)ip->i_e2fs_gid;
	ip->i_e2fs_uid_high = ip->i_e2fs_uid >> 16;
	ip->i_e2fs_gid_high = ip->i_e2fs_gid >> 16;

	e2fs_isave(fs, ip->i_e2din, (struct ext2fs_dinode *)cp);
	if (waitfor)
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
 * Truncate the inode oip to at most length size, freeing the
 * disk blocks.
 */
int
ext2fs_truncate(struct inode *oip, off_t length, int flags, struct ucred *cred)
{
	struct vnode *ovp = ITOV(oip);
	int32_t lastblock;
	int32_t bn, lbn, lastiblock[NIADDR], indir_lbn[NIADDR];
	int32_t oldblks[NDADDR + NIADDR], newblks[NDADDR + NIADDR];
	struct m_ext2fs *fs;
	struct buf *bp;
	int offset, size, level;
	long count, nblocks, vflags, blocksreleased = 0;
	int i;
	int aflags, error, allerror;
	off_t osize;

	if (length < 0)
		return (EINVAL);

	if (ovp->v_type != VREG &&
	    ovp->v_type != VDIR &&
	    ovp->v_type != VLNK)
		return (0);

	if (ovp->v_type == VLNK && ext2fs_size(oip) < EXT2_MAXSYMLINKLEN) {
#ifdef DIAGNOSTIC
		if (length != 0)
			panic("ext2fs_truncate: partial truncate of symlink");
#endif
		memset(&oip->i_e2din->e2di_shortlink, 0, ext2fs_size(oip));
		(void)ext2fs_setsize(oip, 0);
		oip->i_flag |= IN_CHANGE | IN_UPDATE;
		return (ext2fs_update(oip, 1));
	}

	if (ext2fs_size(oip) == length) {
		oip->i_flag |= IN_CHANGE | IN_UPDATE;
		return (ext2fs_update(oip, 0));
	}
	fs = oip->i_e2fs;
	osize = ext2fs_size(oip);
	/*
	 * Lengthen the size of the file. We must ensure that the
	 * last byte of the file is allocated. Since the smallest
	 * value of osize is 0, length will be at least 1.
	 */
	if (osize < length) {
#if 0 /* XXX */
		if (length > fs->fs_maxfilesize)
			return (EFBIG);
#endif
		offset = blkoff(fs, length - 1);
		lbn = lblkno(fs, length - 1);
		aflags = B_CLRBUF;
		if (flags & IO_SYNC)
			aflags |= B_SYNC;
		error = ext2fs_buf_alloc(oip, lbn, offset + 1, cred, &bp,
		    aflags);
		if (error)
			return (error);
		(void)ext2fs_setsize(oip, length);
		uvm_vnp_setsize(ovp, length);
		uvm_vnp_uncache(ovp);
		if (aflags & B_SYNC)
			bwrite(bp);
		else
			bawrite(bp);
		oip->i_flag |= IN_CHANGE | IN_UPDATE;
		return (ext2fs_update(oip, 1));
	}
	/*
	 * Shorten the size of the file. If the file is not being
	 * truncated to a block boundary, the contents of the
	 * partial block following the end of the file must be
	 * zero'ed in case it ever become accessible again because
	 * of subsequent file growth.
	 */
	offset = blkoff(fs, length);
	if (offset == 0) {
		(void)ext2fs_setsize(oip, length);
	} else {
		lbn = lblkno(fs, length);
		aflags = B_CLRBUF;
		if (flags & IO_SYNC)
			aflags |= B_SYNC;
		error = ext2fs_buf_alloc(oip, lbn, offset, cred, &bp,
		    aflags);
		if (error)
			return (error);
		(void)ext2fs_setsize(oip, length);
		size = fs->e2fs_bsize;
		uvm_vnp_setsize(ovp, length);
		uvm_vnp_uncache(ovp);
		memset(bp->b_data + offset, 0, size - offset);
		bp->b_bcount = size;
		if (aflags & B_SYNC)
			bwrite(bp);
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
	lastiblock[SINGLE] = lastblock - NDADDR;
	lastiblock[DOUBLE] = lastiblock[SINGLE] - NINDIR(fs);
	lastiblock[TRIPLE] = lastiblock[DOUBLE] - NINDIR(fs) * NINDIR(fs);
	nblocks = btodb(fs->e2fs_bsize);
	/*
	 * Update file and block pointers on disk before we start freeing
	 * blocks.  If we crash before free'ing blocks below, the blocks
	 * will be returned to the free list.  lastiblock values are also
	 * normalized to -1 for calls to ext2fs_indirtrunc below.
	 */
	memcpy(oldblks, &oip->i_e2fs_blocks[0], sizeof(oldblks));
	for (level = TRIPLE; level >= SINGLE; level--)
		if (lastiblock[level] < 0) {
			oip->i_e2fs_blocks[NDADDR + level] = 0;
			lastiblock[level] = -1;
		}
	for (i = NDADDR - 1; i > lastblock; i--)
		oip->i_e2fs_blocks[i] = 0;
	oip->i_flag |= IN_CHANGE | IN_UPDATE;
	if ((error = ext2fs_update(oip, 1)) != 0)
		allerror = error;
	/*
	 * Having written the new inode to disk, save its new configuration
	 * and put back the old block pointers long enough to process them.
	 * Note that we save the new block configuration so we can check it
	 * when we are done.
	 */
	memcpy(newblks, &oip->i_e2fs_blocks[0], sizeof(newblks));
	memcpy(&oip->i_e2fs_blocks[0], oldblks, sizeof(oldblks));
	(void)ext2fs_setsize(oip, osize);
	vflags = ((length > 0) ? V_SAVE : 0) | V_SAVEMETA;
	allerror = vinvalbuf(ovp, vflags, cred, curproc, 0, INFSLP);

	/*
	 * Indirect blocks first.
	 */
	indir_lbn[SINGLE] = -NDADDR;
	indir_lbn[DOUBLE] = indir_lbn[SINGLE] - NINDIR(fs) -1;
	indir_lbn[TRIPLE] = indir_lbn[DOUBLE] - NINDIR(fs) * NINDIR(fs) - 1;
	for (level = TRIPLE; level >= SINGLE; level--) {
		bn = letoh32(oip->i_e2fs_blocks[NDADDR + level]);
		if (bn != 0) {
			error = ext2fs_indirtrunc(oip, indir_lbn[level],
			    fsbtodb(fs, bn), lastiblock[level], level, &count);
			if (error)
				allerror = error;
			blocksreleased += count;
			if (lastiblock[level] < 0) {
				oip->i_e2fs_blocks[NDADDR + level] = 0;
				ext2fs_blkfree(oip, bn);
				blocksreleased += nblocks;
			}
		}
		if (lastiblock[level] >= 0)
			goto done;
	}

	/*
	 * All whole direct blocks or frags.
	 */
	for (i = NDADDR - 1; i > lastblock; i--) {
		bn = letoh32(oip->i_e2fs_blocks[i]);
		if (bn == 0)
			continue;
		oip->i_e2fs_blocks[i] = 0;
		ext2fs_blkfree(oip, bn);
		blocksreleased += btodb(fs->e2fs_bsize);
	}

done:
#ifdef DIAGNOSTIC
	for (level = SINGLE; level <= TRIPLE; level++)
		if (newblks[NDADDR + level] !=
		    oip->i_e2fs_blocks[NDADDR + level])
			panic("ext2fs_truncate1");
	for (i = 0; i < NDADDR; i++)
		if (newblks[i] != oip->i_e2fs_blocks[i])
			panic("ext2fs_truncate2");
	if (length == 0) {
		int s;

		s = splbio();
		if (!LIST_EMPTY(&ovp->v_cleanblkhd) ||
		    !LIST_EMPTY(&ovp->v_dirtyblkhd))
			panic("ext2fs_truncate3");
		splx(s);
	}
#endif /* DIAGNOSTIC */
	/*
	 * Put back the real size.
	 */
	(void)ext2fs_setsize(oip, length);
	if (blocksreleased >= oip->i_e2fs_nblock)
		oip->i_e2fs_nblock = 0;
	else
		oip->i_e2fs_nblock -= blocksreleased;
	oip->i_flag |= IN_CHANGE;
	return (allerror);
}

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
ext2fs_indirtrunc(struct inode *ip, int32_t lbn, int32_t dbn, int32_t lastbn, int level, long *countp)
{
	int i;
	struct buf *bp;
	struct m_ext2fs *fs = ip->i_e2fs;
	int32_t *bap;
	struct vnode *vp;
	int32_t *copy = NULL, nb, nlbn, last;
	long blkcount, factor;
	int nblocks, blocksreleased = 0;
	int error = 0, allerror = 0;

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
	bp = getblk(vp, lbn, (int)fs->e2fs_bsize, 0, INFSLP);
	if (!(bp->b_flags & (B_DONE | B_DELWRI))) {
		curproc->p_ru.ru_inblock++;		/* pay for read */
		bcstats.pendingreads++;
		bcstats.numreads++;
		bp->b_flags |= B_READ;
		if (bp->b_bcount > bp->b_bufsize)
			panic("ext2fs_indirtrunc: bad buffer size");
		bp->b_blkno = dbn;
		VOP_STRATEGY(bp->b_vp, bp);
		error = biowait(bp);
	}
	if (error) {
		brelse(bp);
		*countp = 0;
		return (error);
	}

	bap = (int32_t *)bp->b_data;
	if (lastbn >= 0) {
		copy = malloc(fs->e2fs_bsize, M_TEMP, M_WAITOK);
		memcpy(copy, bap, fs->e2fs_bsize);
		memset(&bap[last + 1], 0,
		    (NINDIR(fs) - (last + 1)) * sizeof(u_int32_t));
		error = bwrite(bp);
		if (error)
			allerror = error;
		bap = copy;
	}

	/*
	 * Recursively free totally unused blocks.
	 */
	for (i = NINDIR(fs) - 1,
		nlbn = lbn + 1 - i * factor; i > last;
		i--, nlbn += factor) {
		nb = letoh32(bap[i]);
		if (nb == 0)
			continue;
		if (level > SINGLE) {
			error = ext2fs_indirtrunc(ip, nlbn, fsbtodb(fs, nb),
						   (int32_t)-1, level - 1,
						   &blkcount);
			if (error)
				allerror = error;
			blocksreleased += blkcount;
		}
		ext2fs_blkfree(ip, nb);
		blocksreleased += nblocks;
	}

	/*
	 * Recursively free last partial block.
	 */
	if (level > SINGLE && lastbn >= 0) {
		last = lastbn % factor;
		nb = letoh32(bap[i]);
		if (nb != 0) {
			error = ext2fs_indirtrunc(ip, nlbn, fsbtodb(fs, nb),
						   last, level - 1, &blkcount);
			if (error)
				allerror = error;
			blocksreleased += blkcount;
		}
	}

	if (copy != NULL) {
		free(copy, M_TEMP, fs->e2fs_bsize);
	} else {
		bp->b_flags |= B_INVAL;
		brelse(bp);
	}

	*countp = blocksreleased;
	return (allerror);
}
