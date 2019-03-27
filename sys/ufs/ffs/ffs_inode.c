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
 *	@(#)ffs_inode.c	8.13 (Berkeley) 4/21/95
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_quota.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/racct.h>
#include <sys/random.h>
#include <sys/resourcevar.h>
#include <sys/rwlock.h>
#include <sys/stat.h>
#include <sys/vmmeter.h>
#include <sys/vnode.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_object.h>

#include <ufs/ufs/extattr.h>
#include <ufs/ufs/quota.h>
#include <ufs/ufs/ufsmount.h>
#include <ufs/ufs/inode.h>
#include <ufs/ufs/ufs_extern.h>

#include <ufs/ffs/fs.h>
#include <ufs/ffs/ffs_extern.h>

static int ffs_indirtrunc(struct inode *, ufs2_daddr_t, ufs2_daddr_t,
	    ufs2_daddr_t, int, ufs2_daddr_t *);

/*
 * Update the access, modified, and inode change times as specified by the
 * IN_ACCESS, IN_UPDATE, and IN_CHANGE flags respectively.  Write the inode
 * to disk if the IN_MODIFIED flag is set (it may be set initially, or by
 * the timestamp update).  The IN_LAZYMOD flag is set to force a write
 * later if not now.  The IN_LAZYACCESS is set instead of IN_MODIFIED if the fs
 * is currently being suspended (or is suspended) and vnode has been accessed.
 * If we write now, then clear IN_MODIFIED, IN_LAZYACCESS and IN_LAZYMOD to
 * reflect the presumably successful write, and if waitfor is set, then wait
 * for the write to complete.
 */
int
ffs_update(vp, waitfor)
	struct vnode *vp;
	int waitfor;
{
	struct fs *fs;
	struct buf *bp;
	struct inode *ip;
	int flags, error;

	ASSERT_VOP_ELOCKED(vp, "ffs_update");
	ufs_itimes(vp);
	ip = VTOI(vp);
	if ((ip->i_flag & IN_MODIFIED) == 0 && waitfor == 0)
		return (0);
	ip->i_flag &= ~(IN_LAZYACCESS | IN_LAZYMOD | IN_MODIFIED);
	fs = ITOFS(ip);
	if (fs->fs_ronly && ITOUMP(ip)->um_fsckpid == 0)
		return (0);
	/*
	 * If we are updating a snapshot and another process is currently
	 * writing the buffer containing the inode for this snapshot then
	 * a deadlock can occur when it tries to check the snapshot to see
	 * if that block needs to be copied. Thus when updating a snapshot
	 * we check to see if the buffer is already locked, and if it is
	 * we drop the snapshot lock until the buffer has been written
	 * and is available to us. We have to grab a reference to the
	 * snapshot vnode to prevent it from being removed while we are
	 * waiting for the buffer.
	 */
	flags = 0;
	if (IS_SNAPSHOT(ip))
		flags = GB_LOCK_NOWAIT;
loop:
	error = bread_gb(ITODEVVP(ip),
	     fsbtodb(fs, ino_to_fsba(fs, ip->i_number)),
	     (int) fs->fs_bsize, NOCRED, flags, &bp);
	if (error != 0) {
		if (error != EBUSY)
			return (error);
		KASSERT((IS_SNAPSHOT(ip)), ("EBUSY from non-snapshot"));
		/*
		 * Wait for our inode block to become available.
		 *
		 * Hold a reference to the vnode to protect against
		 * ffs_snapgone(). Since we hold a reference, it can only
		 * get reclaimed (VI_DOOMED flag) in a forcible downgrade
		 * or unmount. For an unmount, the entire filesystem will be
		 * gone, so we cannot attempt to touch anything associated
		 * with it while the vnode is unlocked; all we can do is 
		 * pause briefly and try again. If when we relock the vnode
		 * we discover that it has been reclaimed, updating it is no
		 * longer necessary and we can just return an error.
		 */
		vref(vp);
		VOP_UNLOCK(vp, 0);
		pause("ffsupd", 1);
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
		vrele(vp);
		if ((vp->v_iflag & VI_DOOMED) != 0)
			return (ENOENT);
		goto loop;
	}
	if (DOINGSOFTDEP(vp))
		softdep_update_inodeblock(ip, bp, waitfor);
	else if (ip->i_effnlink != ip->i_nlink)
		panic("ffs_update: bad link cnt");
	if (I_IS_UFS1(ip)) {
		*((struct ufs1_dinode *)bp->b_data +
		    ino_to_fsbo(fs, ip->i_number)) = *ip->i_din1;
		/*
		 * XXX: FIX? The entropy here is desirable,
		 * but the harvesting may be expensive
		 */
		random_harvest_queue(&(ip->i_din1), sizeof(ip->i_din1), RANDOM_FS_ATIME);
	} else {
		ffs_update_dinode_ckhash(fs, ip->i_din2);
		*((struct ufs2_dinode *)bp->b_data +
		    ino_to_fsbo(fs, ip->i_number)) = *ip->i_din2;
		/*
		 * XXX: FIX? The entropy here is desirable,
		 * but the harvesting may be expensive
		 */
		random_harvest_queue(&(ip->i_din2), sizeof(ip->i_din2), RANDOM_FS_ATIME);
	}
	if (waitfor)
		error = bwrite(bp);
	else if (vm_page_count_severe() || buf_dirty_count_severe()) {
		bawrite(bp);
		error = 0;
	} else {
		if (bp->b_bufsize == fs->fs_bsize)
			bp->b_flags |= B_CLUSTEROK;
		bdwrite(bp);
		error = 0;
	}
	return (error);
}

#define	SINGLE	0	/* index of single indirect block */
#define	DOUBLE	1	/* index of double indirect block */
#define	TRIPLE	2	/* index of triple indirect block */
/*
 * Truncate the inode ip to at most length size, freeing the
 * disk blocks.
 */
int
ffs_truncate(vp, length, flags, cred)
	struct vnode *vp;
	off_t length;
	int flags;
	struct ucred *cred;
{
	struct inode *ip;
	ufs2_daddr_t bn, lbn, lastblock, lastiblock[UFS_NIADDR];
	ufs2_daddr_t indir_lbn[UFS_NIADDR], oldblks[UFS_NDADDR + UFS_NIADDR];
	ufs2_daddr_t newblks[UFS_NDADDR + UFS_NIADDR];
	ufs2_daddr_t count, blocksreleased = 0, datablocks, blkno;
	struct bufobj *bo;
	struct fs *fs;
	struct buf *bp;
	struct ufsmount *ump;
	int softdeptrunc, journaltrunc;
	int needextclean, extblocks;
	int offset, size, level, nblocks;
	int i, error, allerror, indiroff, waitforupdate;
	u_long key;
	off_t osize;

	ip = VTOI(vp);
	ump = VFSTOUFS(vp->v_mount);
	fs = ump->um_fs;
	bo = &vp->v_bufobj;

	ASSERT_VOP_LOCKED(vp, "ffs_truncate");

	if (length < 0)
		return (EINVAL);
	if (length > fs->fs_maxfilesize)
		return (EFBIG);
#ifdef QUOTA
	error = getinoquota(ip);
	if (error)
		return (error);
#endif
	/*
	 * Historically clients did not have to specify which data
	 * they were truncating. So, if not specified, we assume
	 * traditional behavior, e.g., just the normal data.
	 */
	if ((flags & (IO_EXT | IO_NORMAL)) == 0)
		flags |= IO_NORMAL;
	if (!DOINGSOFTDEP(vp) && !DOINGASYNC(vp))
		flags |= IO_SYNC;
	waitforupdate = (flags & IO_SYNC) != 0 || !DOINGASYNC(vp);
	/*
	 * If we are truncating the extended-attributes, and cannot
	 * do it with soft updates, then do it slowly here. If we are
	 * truncating both the extended attributes and the file contents
	 * (e.g., the file is being unlinked), then pick it off with
	 * soft updates below.
	 */
	allerror = 0;
	needextclean = 0;
	softdeptrunc = 0;
	journaltrunc = DOINGSUJ(vp);
	if (journaltrunc == 0 && DOINGSOFTDEP(vp) && length == 0)
		softdeptrunc = !softdep_slowdown(vp);
	extblocks = 0;
	datablocks = DIP(ip, i_blocks);
	if (fs->fs_magic == FS_UFS2_MAGIC && ip->i_din2->di_extsize > 0) {
		extblocks = btodb(fragroundup(fs, ip->i_din2->di_extsize));
		datablocks -= extblocks;
	}
	if ((flags & IO_EXT) && extblocks > 0) {
		if (length != 0)
			panic("ffs_truncate: partial trunc of extdata");
		if (softdeptrunc || journaltrunc) {
			if ((flags & IO_NORMAL) == 0)
				goto extclean;
			needextclean = 1;
		} else {
			if ((error = ffs_syncvnode(vp, MNT_WAIT, 0)) != 0)
				return (error);
#ifdef QUOTA
			(void) chkdq(ip, -extblocks, NOCRED, 0);
#endif
			vinvalbuf(vp, V_ALT, 0, 0);
			vn_pages_remove(vp,
			    OFF_TO_IDX(lblktosize(fs, -extblocks)), 0);
			osize = ip->i_din2->di_extsize;
			ip->i_din2->di_blocks -= extblocks;
			ip->i_din2->di_extsize = 0;
			for (i = 0; i < UFS_NXADDR; i++) {
				oldblks[i] = ip->i_din2->di_extb[i];
				ip->i_din2->di_extb[i] = 0;
			}
			ip->i_flag |= IN_CHANGE;
			if ((error = ffs_update(vp, waitforupdate)))
				return (error);
			for (i = 0; i < UFS_NXADDR; i++) {
				if (oldblks[i] == 0)
					continue;
				ffs_blkfree(ump, fs, ITODEVVP(ip), oldblks[i],
				    sblksize(fs, osize, i), ip->i_number,
				    vp->v_type, NULL, SINGLETON_KEY);
			}
		}
	}
	if ((flags & IO_NORMAL) == 0)
		return (0);
	if (vp->v_type == VLNK &&
	    (ip->i_size < vp->v_mount->mnt_maxsymlinklen ||
	     datablocks == 0)) {
#ifdef INVARIANTS
		if (length != 0)
			panic("ffs_truncate: partial truncate of symlink");
#endif
		bzero(SHORTLINK(ip), (u_int)ip->i_size);
		ip->i_size = 0;
		DIP_SET(ip, i_size, 0);
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
		if (needextclean)
			goto extclean;
		return (ffs_update(vp, waitforupdate));
	}
	if (ip->i_size == length) {
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
		if (needextclean)
			goto extclean;
		return (ffs_update(vp, 0));
	}
	if (fs->fs_ronly)
		panic("ffs_truncate: read-only filesystem");
	if (IS_SNAPSHOT(ip))
		ffs_snapremove(vp);
	vp->v_lasta = vp->v_clen = vp->v_cstart = vp->v_lastw = 0;
	osize = ip->i_size;
	/*
	 * Lengthen the size of the file. We must ensure that the
	 * last byte of the file is allocated. Since the smallest
	 * value of osize is 0, length will be at least 1.
	 */
	if (osize < length) {
		vnode_pager_setsize(vp, length);
		flags |= BA_CLRBUF;
		error = UFS_BALLOC(vp, length - 1, 1, cred, flags, &bp);
		if (error) {
			vnode_pager_setsize(vp, osize);
			return (error);
		}
		ip->i_size = length;
		DIP_SET(ip, i_size, length);
		if (bp->b_bufsize == fs->fs_bsize)
			bp->b_flags |= B_CLUSTEROK;
		if (flags & IO_SYNC)
			bwrite(bp);
		else if (DOINGASYNC(vp))
			bdwrite(bp);
		else
			bawrite(bp);
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
		return (ffs_update(vp, waitforupdate));
	}
	/*
	 * Lookup block number for a given offset. Zero length files
	 * have no blocks, so return a blkno of -1.
	 */
	lbn = lblkno(fs, length - 1);
	if (length == 0) {
		blkno = -1;
	} else if (lbn < UFS_NDADDR) {
		blkno = DIP(ip, i_db[lbn]);
	} else {
		error = UFS_BALLOC(vp, lblktosize(fs, (off_t)lbn), fs->fs_bsize,
		    cred, BA_METAONLY, &bp);
		if (error)
			return (error);
		indiroff = (lbn - UFS_NDADDR) % NINDIR(fs);
		if (I_IS_UFS1(ip))
			blkno = ((ufs1_daddr_t *)(bp->b_data))[indiroff];
		else
			blkno = ((ufs2_daddr_t *)(bp->b_data))[indiroff];
		/*
		 * If the block number is non-zero, then the indirect block
		 * must have been previously allocated and need not be written.
		 * If the block number is zero, then we may have allocated
		 * the indirect block and hence need to write it out.
		 */
		if (blkno != 0)
			brelse(bp);
		else if (flags & IO_SYNC)
			bwrite(bp);
		else
			bdwrite(bp);
	}
	/*
	 * If the block number at the new end of the file is zero,
	 * then we must allocate it to ensure that the last block of 
	 * the file is allocated. Soft updates does not handle this
	 * case, so here we have to clean up the soft updates data
	 * structures describing the allocation past the truncation
	 * point. Finding and deallocating those structures is a lot of
	 * work. Since partial truncation with a hole at the end occurs
	 * rarely, we solve the problem by syncing the file so that it
	 * will have no soft updates data structures left.
	 */
	if (blkno == 0 && (error = ffs_syncvnode(vp, MNT_WAIT, 0)) != 0)
		return (error);
	if (blkno != 0 && DOINGSOFTDEP(vp)) {
		if (softdeptrunc == 0 && journaltrunc == 0) {
			/*
			 * If soft updates cannot handle this truncation,
			 * clean up soft dependency data structures and
			 * fall through to the synchronous truncation.
			 */
			if ((error = ffs_syncvnode(vp, MNT_WAIT, 0)) != 0)
				return (error);
		} else {
			flags = IO_NORMAL | (needextclean ? IO_EXT: 0);
			if (journaltrunc)
				softdep_journal_freeblocks(ip, cred, length,
				    flags);
			else
				softdep_setup_freeblocks(ip, length, flags);
			ASSERT_VOP_LOCKED(vp, "ffs_truncate1");
			if (journaltrunc == 0) {
				ip->i_flag |= IN_CHANGE | IN_UPDATE;
				error = ffs_update(vp, 0);
			}
			return (error);
		}
	}
	/*
	 * Shorten the size of the file. If the last block of the
	 * shortened file is unallocated, we must allocate it.
	 * Additionally, if the file is not being truncated to a
	 * block boundary, the contents of the partial block
	 * following the end of the file must be zero'ed in
	 * case it ever becomes accessible again because of
	 * subsequent file growth. Directories however are not
	 * zero'ed as they should grow back initialized to empty.
	 */
	offset = blkoff(fs, length);
	if (blkno != 0 && offset == 0) {
		ip->i_size = length;
		DIP_SET(ip, i_size, length);
	} else {
		lbn = lblkno(fs, length);
		flags |= BA_CLRBUF;
		error = UFS_BALLOC(vp, length - 1, 1, cred, flags, &bp);
		if (error)
			return (error);
		/*
		 * When we are doing soft updates and the UFS_BALLOC
		 * above fills in a direct block hole with a full sized
		 * block that will be truncated down to a fragment below,
		 * we must flush out the block dependency with an FSYNC
		 * so that we do not get a soft updates inconsistency
		 * when we create the fragment below.
		 */
		if (DOINGSOFTDEP(vp) && lbn < UFS_NDADDR &&
		    fragroundup(fs, blkoff(fs, length)) < fs->fs_bsize &&
		    (error = ffs_syncvnode(vp, MNT_WAIT, 0)) != 0)
			return (error);
		ip->i_size = length;
		DIP_SET(ip, i_size, length);
		size = blksize(fs, ip, lbn);
		if (vp->v_type != VDIR && offset != 0)
			bzero((char *)bp->b_data + offset,
			    (u_int)(size - offset));
		/* Kirk's code has reallocbuf(bp, size, 1) here */
		allocbuf(bp, size);
		if (bp->b_bufsize == fs->fs_bsize)
			bp->b_flags |= B_CLUSTEROK;
		if (flags & IO_SYNC)
			bwrite(bp);
		else if (DOINGASYNC(vp))
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
	lastblock = lblkno(fs, length + fs->fs_bsize - 1) - 1;
	lastiblock[SINGLE] = lastblock - UFS_NDADDR;
	lastiblock[DOUBLE] = lastiblock[SINGLE] - NINDIR(fs);
	lastiblock[TRIPLE] = lastiblock[DOUBLE] - NINDIR(fs) * NINDIR(fs);
	nblocks = btodb(fs->fs_bsize);
	/*
	 * Update file and block pointers on disk before we start freeing
	 * blocks.  If we crash before free'ing blocks below, the blocks
	 * will be returned to the free list.  lastiblock values are also
	 * normalized to -1 for calls to ffs_indirtrunc below.
	 */
	for (level = TRIPLE; level >= SINGLE; level--) {
		oldblks[UFS_NDADDR + level] = DIP(ip, i_ib[level]);
		if (lastiblock[level] < 0) {
			DIP_SET(ip, i_ib[level], 0);
			lastiblock[level] = -1;
		}
	}
	for (i = 0; i < UFS_NDADDR; i++) {
		oldblks[i] = DIP(ip, i_db[i]);
		if (i > lastblock)
			DIP_SET(ip, i_db[i], 0);
	}
	ip->i_flag |= IN_CHANGE | IN_UPDATE;
	allerror = ffs_update(vp, waitforupdate);
	
	/*
	 * Having written the new inode to disk, save its new configuration
	 * and put back the old block pointers long enough to process them.
	 * Note that we save the new block configuration so we can check it
	 * when we are done.
	 */
	for (i = 0; i < UFS_NDADDR; i++) {
		newblks[i] = DIP(ip, i_db[i]);
		DIP_SET(ip, i_db[i], oldblks[i]);
	}
	for (i = 0; i < UFS_NIADDR; i++) {
		newblks[UFS_NDADDR + i] = DIP(ip, i_ib[i]);
		DIP_SET(ip, i_ib[i], oldblks[UFS_NDADDR + i]);
	}
	ip->i_size = osize;
	DIP_SET(ip, i_size, osize);

	error = vtruncbuf(vp, cred, length, fs->fs_bsize);
	if (error && (allerror == 0))
		allerror = error;

	/*
	 * Indirect blocks first.
	 */
	indir_lbn[SINGLE] = -UFS_NDADDR;
	indir_lbn[DOUBLE] = indir_lbn[SINGLE] - NINDIR(fs) - 1;
	indir_lbn[TRIPLE] = indir_lbn[DOUBLE] - NINDIR(fs) * NINDIR(fs) - 1;
	for (level = TRIPLE; level >= SINGLE; level--) {
		bn = DIP(ip, i_ib[level]);
		if (bn != 0) {
			error = ffs_indirtrunc(ip, indir_lbn[level],
			    fsbtodb(fs, bn), lastiblock[level], level, &count);
			if (error)
				allerror = error;
			blocksreleased += count;
			if (lastiblock[level] < 0) {
				DIP_SET(ip, i_ib[level], 0);
				ffs_blkfree(ump, fs, ump->um_devvp, bn,
				    fs->fs_bsize, ip->i_number,
				    vp->v_type, NULL, SINGLETON_KEY);
				blocksreleased += nblocks;
			}
		}
		if (lastiblock[level] >= 0)
			goto done;
	}

	/*
	 * All whole direct blocks or frags.
	 */
	key = ffs_blkrelease_start(ump, ump->um_devvp, ip->i_number);
	for (i = UFS_NDADDR - 1; i > lastblock; i--) {
		long bsize;

		bn = DIP(ip, i_db[i]);
		if (bn == 0)
			continue;
		DIP_SET(ip, i_db[i], 0);
		bsize = blksize(fs, ip, i);
		ffs_blkfree(ump, fs, ump->um_devvp, bn, bsize, ip->i_number,
		    vp->v_type, NULL, key);
		blocksreleased += btodb(bsize);
	}
	ffs_blkrelease_finish(ump, key);
	if (lastblock < 0)
		goto done;

	/*
	 * Finally, look for a change in size of the
	 * last direct block; release any frags.
	 */
	bn = DIP(ip, i_db[lastblock]);
	if (bn != 0) {
		long oldspace, newspace;

		/*
		 * Calculate amount of space we're giving
		 * back as old block size minus new block size.
		 */
		oldspace = blksize(fs, ip, lastblock);
		ip->i_size = length;
		DIP_SET(ip, i_size, length);
		newspace = blksize(fs, ip, lastblock);
		if (newspace == 0)
			panic("ffs_truncate: newspace");
		if (oldspace - newspace > 0) {
			/*
			 * Block number of space to be free'd is
			 * the old block # plus the number of frags
			 * required for the storage we're keeping.
			 */
			bn += numfrags(fs, newspace);
			ffs_blkfree(ump, fs, ump->um_devvp, bn,
			   oldspace - newspace, ip->i_number, vp->v_type,
			   NULL, SINGLETON_KEY);
			blocksreleased += btodb(oldspace - newspace);
		}
	}
done:
#ifdef INVARIANTS
	for (level = SINGLE; level <= TRIPLE; level++)
		if (newblks[UFS_NDADDR + level] != DIP(ip, i_ib[level]))
			panic("ffs_truncate1: level %d newblks %jd != i_ib %jd",
			    level, (intmax_t)newblks[UFS_NDADDR + level],
			    (intmax_t)DIP(ip, i_ib[level]));
	for (i = 0; i < UFS_NDADDR; i++)
		if (newblks[i] != DIP(ip, i_db[i]))
			panic("ffs_truncate2: blkno %d newblks %jd != i_db %jd",
			    i, (intmax_t)newblks[UFS_NDADDR + level],
			    (intmax_t)DIP(ip, i_ib[level]));
	BO_LOCK(bo);
	if (length == 0 &&
	    (fs->fs_magic != FS_UFS2_MAGIC || ip->i_din2->di_extsize == 0) &&
	    (bo->bo_dirty.bv_cnt > 0 || bo->bo_clean.bv_cnt > 0))
		panic("ffs_truncate3: vp = %p, buffers: dirty = %d, clean = %d",
			vp, bo->bo_dirty.bv_cnt, bo->bo_clean.bv_cnt);
	BO_UNLOCK(bo);
#endif /* INVARIANTS */
	/*
	 * Put back the real size.
	 */
	ip->i_size = length;
	DIP_SET(ip, i_size, length);
	if (DIP(ip, i_blocks) >= blocksreleased)
		DIP_SET(ip, i_blocks, DIP(ip, i_blocks) - blocksreleased);
	else	/* sanity */
		DIP_SET(ip, i_blocks, 0);
	ip->i_flag |= IN_CHANGE;
#ifdef QUOTA
	(void) chkdq(ip, -blocksreleased, NOCRED, 0);
#endif
	return (allerror);

extclean:
	if (journaltrunc)
		softdep_journal_freeblocks(ip, cred, length, IO_EXT);
	else
		softdep_setup_freeblocks(ip, length, IO_EXT);
	return (ffs_update(vp, waitforupdate));
}

/*
 * Release blocks associated with the inode ip and stored in the indirect
 * block bn.  Blocks are free'd in LIFO order up to (but not including)
 * lastbn.  If level is greater than SINGLE, the block is an indirect block
 * and recursive calls to indirtrunc must be used to cleanse other indirect
 * blocks.
 */
static int
ffs_indirtrunc(ip, lbn, dbn, lastbn, level, countp)
	struct inode *ip;
	ufs2_daddr_t lbn, lastbn;
	ufs2_daddr_t dbn;
	int level;
	ufs2_daddr_t *countp;
{
	struct buf *bp;
	struct fs *fs;
	struct ufsmount *ump;
	struct vnode *vp;
	caddr_t copy = NULL;
	u_long key;
	int i, nblocks, error = 0, allerror = 0;
	ufs2_daddr_t nb, nlbn, last;
	ufs2_daddr_t blkcount, factor, blocksreleased = 0;
	ufs1_daddr_t *bap1 = NULL;
	ufs2_daddr_t *bap2 = NULL;
#define BAP(ip, i) (I_IS_UFS1(ip) ? bap1[i] : bap2[i])

	fs = ITOFS(ip);
	ump = ITOUMP(ip);

	/*
	 * Calculate index in current block of last
	 * block to be kept.  -1 indicates the entire
	 * block so we need not calculate the index.
	 */
	factor = lbn_offset(fs, level);
	last = lastbn;
	if (lastbn > 0)
		last /= factor;
	nblocks = btodb(fs->fs_bsize);
	/*
	 * Get buffer of block pointers, zero those entries corresponding
	 * to blocks to be free'd, and update on disk copy first.  Since
	 * double(triple) indirect before single(double) indirect, calls
	 * to bmap on these blocks will fail.  However, we already have
	 * the on disk address, so we have to set the b_blkno field
	 * explicitly instead of letting bread do everything for us.
	 */
	vp = ITOV(ip);
	bp = getblk(vp, lbn, (int)fs->fs_bsize, 0, 0, 0);
	if ((bp->b_flags & B_CACHE) == 0) {
#ifdef RACCT
		if (racct_enable) {
			PROC_LOCK(curproc);
			racct_add_buf(curproc, bp, 0);
			PROC_UNLOCK(curproc);
		}
#endif /* RACCT */
		curthread->td_ru.ru_inblock++;	/* pay for read */
		bp->b_iocmd = BIO_READ;
		bp->b_flags &= ~B_INVAL;
		bp->b_ioflags &= ~BIO_ERROR;
		if (bp->b_bcount > bp->b_bufsize)
			panic("ffs_indirtrunc: bad buffer size");
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

	if (I_IS_UFS1(ip))
		bap1 = (ufs1_daddr_t *)bp->b_data;
	else
		bap2 = (ufs2_daddr_t *)bp->b_data;
	if (lastbn != -1) {
		copy = malloc(fs->fs_bsize, M_TEMP, M_WAITOK);
		bcopy((caddr_t)bp->b_data, copy, (u_int)fs->fs_bsize);
		for (i = last + 1; i < NINDIR(fs); i++)
			if (I_IS_UFS1(ip))
				bap1[i] = 0;
			else
				bap2[i] = 0;
		if (DOINGASYNC(vp)) {
			bdwrite(bp);
		} else {
			error = bwrite(bp);
			if (error)
				allerror = error;
		}
		if (I_IS_UFS1(ip))
			bap1 = (ufs1_daddr_t *)copy;
		else
			bap2 = (ufs2_daddr_t *)copy;
	}

	/*
	 * Recursively free totally unused blocks.
	 */
	key = ffs_blkrelease_start(ump, ITODEVVP(ip), ip->i_number);
	for (i = NINDIR(fs) - 1, nlbn = lbn + 1 - i * factor; i > last;
	    i--, nlbn += factor) {
		nb = BAP(ip, i);
		if (nb == 0)
			continue;
		if (level > SINGLE) {
			if ((error = ffs_indirtrunc(ip, nlbn, fsbtodb(fs, nb),
			    (ufs2_daddr_t)-1, level - 1, &blkcount)) != 0)
				allerror = error;
			blocksreleased += blkcount;
		}
		ffs_blkfree(ump, fs, ITODEVVP(ip), nb, fs->fs_bsize,
		    ip->i_number, vp->v_type, NULL, key);
		blocksreleased += nblocks;
	}
	ffs_blkrelease_finish(ump, key);

	/*
	 * Recursively free last partial block.
	 */
	if (level > SINGLE && lastbn >= 0) {
		last = lastbn % factor;
		nb = BAP(ip, i);
		if (nb != 0) {
			error = ffs_indirtrunc(ip, nlbn, fsbtodb(fs, nb),
			    last, level - 1, &blkcount);
			if (error)
				allerror = error;
			blocksreleased += blkcount;
		}
	}
	if (copy != NULL) {
		free(copy, M_TEMP);
	} else {
		bp->b_flags |= B_INVAL | B_NOCACHE;
		brelse(bp);
	}

	*countp = blocksreleased;
	return (allerror);
}

int
ffs_rdonly(struct inode *ip)
{

	return (ITOFS(ip)->fs_ronly != 0);
}

