/*-
 * SPDX-License-Identifier: (BSD-2-Clause-FreeBSD AND BSD-3-Clause)
 *
 * Copyright (c) 2002 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by Marshall
 * Kirk McKusick and Network Associates Laboratories, the Security
 * Research Division of Network Associates, Inc. under DARPA/SPAWAR
 * contract N66001-01-C-8035 ("CBOSS"), as part of the DARPA CHATS
 * research program
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
 *	@(#)ffs_balloc.c	8.8 (Berkeley) 6/16/95
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/lock.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/vmmeter.h>

#include <ufs/ufs/quota.h>
#include <ufs/ufs/inode.h>
#include <ufs/ufs/ufs_extern.h>
#include <ufs/ufs/extattr.h>
#include <ufs/ufs/ufsmount.h>

#include <ufs/ffs/fs.h>
#include <ufs/ffs/ffs_extern.h>

/*
 * Balloc defines the structure of filesystem storage
 * by allocating the physical blocks on a device given
 * the inode and the logical block number in a file.
 * This is the allocation strategy for UFS1. Below is
 * the allocation strategy for UFS2.
 */
int
ffs_balloc_ufs1(struct vnode *vp, off_t startoffset, int size,
    struct ucred *cred, int flags, struct buf **bpp)
{
	struct inode *ip;
	struct ufs1_dinode *dp;
	ufs_lbn_t lbn, lastlbn;
	struct fs *fs;
	ufs1_daddr_t nb;
	struct buf *bp, *nbp;
	struct ufsmount *ump;
	struct indir indirs[UFS_NIADDR + 2];
	int deallocated, osize, nsize, num, i, error;
	ufs2_daddr_t newb;
	ufs1_daddr_t *bap, pref;
	ufs1_daddr_t *allocib, *blkp, *allocblk, allociblk[UFS_NIADDR + 1];
	ufs2_daddr_t *lbns_remfree, lbns[UFS_NIADDR + 1];
	int unwindidx = -1;
	int saved_inbdflush;
	static struct timeval lastfail;
	static int curfail;
	int gbflags, reclaimed;

	ip = VTOI(vp);
	dp = ip->i_din1;
	fs = ITOFS(ip);
	ump = ITOUMP(ip);
	lbn = lblkno(fs, startoffset);
	size = blkoff(fs, startoffset) + size;
	reclaimed = 0;
	if (size > fs->fs_bsize)
		panic("ffs_balloc_ufs1: blk too big");
	*bpp = NULL;
	if (flags & IO_EXT)
		return (EOPNOTSUPP);
	if (lbn < 0)
		return (EFBIG);
	gbflags = (flags & BA_UNMAPPED) != 0 ? GB_UNMAPPED : 0;

	if (DOINGSOFTDEP(vp))
		softdep_prealloc(vp, MNT_WAIT);
	/*
	 * If the next write will extend the file into a new block,
	 * and the file is currently composed of a fragment
	 * this fragment has to be extended to be a full block.
	 */
	lastlbn = lblkno(fs, ip->i_size);
	if (lastlbn < UFS_NDADDR && lastlbn < lbn) {
		nb = lastlbn;
		osize = blksize(fs, ip, nb);
		if (osize < fs->fs_bsize && osize > 0) {
			UFS_LOCK(ump);
			error = ffs_realloccg(ip, nb, dp->di_db[nb],
			   ffs_blkpref_ufs1(ip, lastlbn, (int)nb,
			   &dp->di_db[0]), osize, (int)fs->fs_bsize, flags,
			   cred, &bp);
			if (error)
				return (error);
			if (DOINGSOFTDEP(vp))
				softdep_setup_allocdirect(ip, nb,
				    dbtofsb(fs, bp->b_blkno), dp->di_db[nb],
				    fs->fs_bsize, osize, bp);
			ip->i_size = smalllblktosize(fs, nb + 1);
			dp->di_size = ip->i_size;
			dp->di_db[nb] = dbtofsb(fs, bp->b_blkno);
			ip->i_flag |= IN_CHANGE | IN_UPDATE;
			if (flags & IO_SYNC)
				bwrite(bp);
			else if (DOINGASYNC(vp))
				bdwrite(bp);
			else
				bawrite(bp);
		}
	}
	/*
	 * The first UFS_NDADDR blocks are direct blocks
	 */
	if (lbn < UFS_NDADDR) {
		if (flags & BA_METAONLY)
			panic("ffs_balloc_ufs1: BA_METAONLY for direct block");
		nb = dp->di_db[lbn];
		if (nb != 0 && ip->i_size >= smalllblktosize(fs, lbn + 1)) {
			error = bread(vp, lbn, fs->fs_bsize, NOCRED, &bp);
			if (error) {
				brelse(bp);
				return (error);
			}
			bp->b_blkno = fsbtodb(fs, nb);
			*bpp = bp;
			return (0);
		}
		if (nb != 0) {
			/*
			 * Consider need to reallocate a fragment.
			 */
			osize = fragroundup(fs, blkoff(fs, ip->i_size));
			nsize = fragroundup(fs, size);
			if (nsize <= osize) {
				error = bread(vp, lbn, osize, NOCRED, &bp);
				if (error) {
					brelse(bp);
					return (error);
				}
				bp->b_blkno = fsbtodb(fs, nb);
			} else {
				UFS_LOCK(ump);
				error = ffs_realloccg(ip, lbn, dp->di_db[lbn],
				    ffs_blkpref_ufs1(ip, lbn, (int)lbn,
				    &dp->di_db[0]), osize, nsize, flags,
				    cred, &bp);
				if (error)
					return (error);
				if (DOINGSOFTDEP(vp))
					softdep_setup_allocdirect(ip, lbn,
					    dbtofsb(fs, bp->b_blkno), nb,
					    nsize, osize, bp);
			}
		} else {
			if (ip->i_size < smalllblktosize(fs, lbn + 1))
				nsize = fragroundup(fs, size);
			else
				nsize = fs->fs_bsize;
			UFS_LOCK(ump);
			error = ffs_alloc(ip, lbn,
			    ffs_blkpref_ufs1(ip, lbn, (int)lbn, &dp->di_db[0]),
			    nsize, flags, cred, &newb);
			if (error)
				return (error);
			bp = getblk(vp, lbn, nsize, 0, 0, gbflags);
			bp->b_blkno = fsbtodb(fs, newb);
			if (flags & BA_CLRBUF)
				vfs_bio_clrbuf(bp);
			if (DOINGSOFTDEP(vp))
				softdep_setup_allocdirect(ip, lbn, newb, 0,
				    nsize, 0, bp);
		}
		dp->di_db[lbn] = dbtofsb(fs, bp->b_blkno);
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
		*bpp = bp;
		return (0);
	}
	/*
	 * Determine the number of levels of indirection.
	 */
	pref = 0;
	if ((error = ufs_getlbns(vp, lbn, indirs, &num)) != 0)
		return(error);
#ifdef INVARIANTS
	if (num < 1)
		panic ("ffs_balloc_ufs1: ufs_getlbns returned indirect block");
#endif
	saved_inbdflush = curthread_pflags_set(TDP_INBDFLUSH);
	/*
	 * Fetch the first indirect block allocating if necessary.
	 */
	--num;
	nb = dp->di_ib[indirs[0].in_off];
	allocib = NULL;
	allocblk = allociblk;
	lbns_remfree = lbns;
	if (nb == 0) {
		UFS_LOCK(ump);
		pref = ffs_blkpref_ufs1(ip, lbn, -indirs[0].in_off - 1,
		    (ufs1_daddr_t *)0);
		if ((error = ffs_alloc(ip, lbn, pref, (int)fs->fs_bsize,
		    flags, cred, &newb)) != 0) {
			curthread_pflags_restore(saved_inbdflush);
			return (error);
		}
		pref = newb + fs->fs_frag;
		nb = newb;
		MPASS(allocblk < allociblk + nitems(allociblk));
		MPASS(lbns_remfree < lbns + nitems(lbns));
		*allocblk++ = nb;
		*lbns_remfree++ = indirs[1].in_lbn;
		bp = getblk(vp, indirs[1].in_lbn, fs->fs_bsize, 0, 0, gbflags);
		bp->b_blkno = fsbtodb(fs, nb);
		vfs_bio_clrbuf(bp);
		if (DOINGSOFTDEP(vp)) {
			softdep_setup_allocdirect(ip,
			    UFS_NDADDR + indirs[0].in_off, newb, 0,
			    fs->fs_bsize, 0, bp);
			bdwrite(bp);
		} else if ((flags & IO_SYNC) == 0 && DOINGASYNC(vp)) {
			if (bp->b_bufsize == fs->fs_bsize)
				bp->b_flags |= B_CLUSTEROK;
			bdwrite(bp);
		} else {
			if ((error = bwrite(bp)) != 0)
				goto fail;
		}
		allocib = &dp->di_ib[indirs[0].in_off];
		*allocib = nb;
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
	}
	/*
	 * Fetch through the indirect blocks, allocating as necessary.
	 */
retry:
	for (i = 1;;) {
		error = bread(vp,
		    indirs[i].in_lbn, (int)fs->fs_bsize, NOCRED, &bp);
		if (error) {
			brelse(bp);
			goto fail;
		}
		bap = (ufs1_daddr_t *)bp->b_data;
		nb = bap[indirs[i].in_off];
		if (i == num)
			break;
		i += 1;
		if (nb != 0) {
			bqrelse(bp);
			continue;
		}
		UFS_LOCK(ump);
		/*
		 * If parent indirect has just been allocated, try to cluster
		 * immediately following it.
		 */
		if (pref == 0)
			pref = ffs_blkpref_ufs1(ip, lbn, i - num - 1,
			    (ufs1_daddr_t *)0);
		if ((error = ffs_alloc(ip, lbn, pref, (int)fs->fs_bsize,
		    flags | IO_BUFLOCKED, cred, &newb)) != 0) {
			brelse(bp);
			if (DOINGSOFTDEP(vp) && ++reclaimed == 1) {
				UFS_LOCK(ump);
				softdep_request_cleanup(fs, vp, cred,
				    FLUSH_BLOCKS_WAIT);
				UFS_UNLOCK(ump);
				goto retry;
			}
			if (ppsratecheck(&lastfail, &curfail, 1)) {
				ffs_fserr(fs, ip->i_number, "filesystem full");
				uprintf("\n%s: write failed, filesystem "
				    "is full\n", fs->fs_fsmnt);
			}
			goto fail;
		}
		pref = newb + fs->fs_frag;
		nb = newb;
		MPASS(allocblk < allociblk + nitems(allociblk));
		MPASS(lbns_remfree < lbns + nitems(lbns));
		*allocblk++ = nb;
		*lbns_remfree++ = indirs[i].in_lbn;
		nbp = getblk(vp, indirs[i].in_lbn, fs->fs_bsize, 0, 0, 0);
		nbp->b_blkno = fsbtodb(fs, nb);
		vfs_bio_clrbuf(nbp);
		if (DOINGSOFTDEP(vp)) {
			softdep_setup_allocindir_meta(nbp, ip, bp,
			    indirs[i - 1].in_off, nb);
			bdwrite(nbp);
		} else if ((flags & IO_SYNC) == 0 && DOINGASYNC(vp)) {
			if (nbp->b_bufsize == fs->fs_bsize)
				nbp->b_flags |= B_CLUSTEROK;
			bdwrite(nbp);
		} else {
			if ((error = bwrite(nbp)) != 0) {
				brelse(bp);
				goto fail;
			}
		}
		bap[indirs[i - 1].in_off] = nb;
		if (allocib == NULL && unwindidx < 0)
			unwindidx = i - 1;
		/*
		 * If required, write synchronously, otherwise use
		 * delayed write.
		 */
		if (flags & IO_SYNC) {
			bwrite(bp);
		} else {
			if (bp->b_bufsize == fs->fs_bsize)
				bp->b_flags |= B_CLUSTEROK;
			bdwrite(bp);
		}
	}
	/*
	 * If asked only for the indirect block, then return it.
	 */
	if (flags & BA_METAONLY) {
		curthread_pflags_restore(saved_inbdflush);
		*bpp = bp;
		return (0);
	}
	/*
	 * Get the data block, allocating if necessary.
	 */
	if (nb == 0) {
		UFS_LOCK(ump);
		/*
		 * If allocating metadata at the front of the cylinder
		 * group and parent indirect block has just been allocated,
		 * then cluster next to it if it is the first indirect in
		 * the file. Otherwise it has been allocated in the metadata
		 * area, so we want to find our own place out in the data area.
		 */
		if (pref == 0 || (lbn > UFS_NDADDR && fs->fs_metaspace != 0))
			pref = ffs_blkpref_ufs1(ip, lbn, indirs[i].in_off,
			    &bap[0]);
		error = ffs_alloc(ip, lbn, pref, (int)fs->fs_bsize,
		    flags | IO_BUFLOCKED, cred, &newb);
		if (error) {
			brelse(bp);
			if (DOINGSOFTDEP(vp) && ++reclaimed == 1) {
				UFS_LOCK(ump);
				softdep_request_cleanup(fs, vp, cred,
				    FLUSH_BLOCKS_WAIT);
				UFS_UNLOCK(ump);
				goto retry;
			}
			if (ppsratecheck(&lastfail, &curfail, 1)) {
				ffs_fserr(fs, ip->i_number, "filesystem full");
				uprintf("\n%s: write failed, filesystem "
				    "is full\n", fs->fs_fsmnt);
			}
			goto fail;
		}
		nb = newb;
		MPASS(allocblk < allociblk + nitems(allociblk));
		MPASS(lbns_remfree < lbns + nitems(lbns));
		*allocblk++ = nb;
		*lbns_remfree++ = lbn;
		nbp = getblk(vp, lbn, fs->fs_bsize, 0, 0, gbflags);
		nbp->b_blkno = fsbtodb(fs, nb);
		if (flags & BA_CLRBUF)
			vfs_bio_clrbuf(nbp);
		if (DOINGSOFTDEP(vp))
			softdep_setup_allocindir_page(ip, lbn, bp,
			    indirs[i].in_off, nb, 0, nbp);
		bap[indirs[i].in_off] = nb;
		/*
		 * If required, write synchronously, otherwise use
		 * delayed write.
		 */
		if (flags & IO_SYNC) {
			bwrite(bp);
		} else {
			if (bp->b_bufsize == fs->fs_bsize)
				bp->b_flags |= B_CLUSTEROK;
			bdwrite(bp);
		}
		curthread_pflags_restore(saved_inbdflush);
		*bpp = nbp;
		return (0);
	}
	brelse(bp);
	if (flags & BA_CLRBUF) {
		int seqcount = (flags & BA_SEQMASK) >> BA_SEQSHIFT;
		if (seqcount != 0 &&
		    (vp->v_mount->mnt_flag & MNT_NOCLUSTERR) == 0 &&
		    !(vm_page_count_severe() || buf_dirty_count_severe())) {
			error = cluster_read(vp, ip->i_size, lbn,
			    (int)fs->fs_bsize, NOCRED,
			    MAXBSIZE, seqcount, gbflags, &nbp);
		} else {
			error = bread_gb(vp, lbn, (int)fs->fs_bsize, NOCRED,
			    gbflags, &nbp);
		}
		if (error) {
			brelse(nbp);
			goto fail;
		}
	} else {
		nbp = getblk(vp, lbn, fs->fs_bsize, 0, 0, gbflags);
		nbp->b_blkno = fsbtodb(fs, nb);
	}
	curthread_pflags_restore(saved_inbdflush);
	*bpp = nbp;
	return (0);
fail:
	curthread_pflags_restore(saved_inbdflush);
	/*
	 * If we have failed to allocate any blocks, simply return the error.
	 * This is the usual case and avoids the need to fsync the file.
	 */
	if (allocblk == allociblk && allocib == NULL && unwindidx == -1)
		return (error);
	/*
	 * If we have failed part way through block allocation, we
	 * have to deallocate any indirect blocks that we have allocated.
	 * We have to fsync the file before we start to get rid of all
	 * of its dependencies so that we do not leave them dangling.
	 * We have to sync it at the end so that the soft updates code
	 * does not find any untracked changes. Although this is really
	 * slow, running out of disk space is not expected to be a common
	 * occurrence. The error return from fsync is ignored as we already
	 * have an error to return to the user.
	 *
	 * XXX Still have to journal the free below
	 */
	(void) ffs_syncvnode(vp, MNT_WAIT, 0);
	for (deallocated = 0, blkp = allociblk, lbns_remfree = lbns;
	     blkp < allocblk; blkp++, lbns_remfree++) {
		/*
		 * We shall not leave the freed blocks on the vnode
		 * buffer object lists.
		 */
		bp = getblk(vp, *lbns_remfree, fs->fs_bsize, 0, 0,
		    GB_NOCREAT | GB_UNMAPPED);
		if (bp != NULL) {
			KASSERT(bp->b_blkno == fsbtodb(fs, *blkp),
			    ("mismatch1 l %jd %jd b %ju %ju",
			    (intmax_t)bp->b_lblkno, (uintmax_t)*lbns_remfree,
			    (uintmax_t)bp->b_blkno,
			    (uintmax_t)fsbtodb(fs, *blkp)));
			bp->b_flags |= B_INVAL | B_RELBUF | B_NOCACHE;
			bp->b_flags &= ~(B_ASYNC | B_CACHE);
			brelse(bp);
		}
		deallocated += fs->fs_bsize;
	}
	if (allocib != NULL) {
		*allocib = 0;
	} else if (unwindidx >= 0) {
		int r;

		r = bread(vp, indirs[unwindidx].in_lbn, 
		    (int)fs->fs_bsize, NOCRED, &bp);
		if (r) {
			panic("Could not unwind indirect block, error %d", r);
			brelse(bp);
		} else {
			bap = (ufs1_daddr_t *)bp->b_data;
			bap[indirs[unwindidx].in_off] = 0;
			if (flags & IO_SYNC) {
				bwrite(bp);
			} else {
				if (bp->b_bufsize == fs->fs_bsize)
					bp->b_flags |= B_CLUSTEROK;
				bdwrite(bp);
			}
		}
	}
	if (deallocated) {
#ifdef QUOTA
		/*
		 * Restore user's disk quota because allocation failed.
		 */
		(void) chkdq(ip, -btodb(deallocated), cred, FORCE);
#endif
		dp->di_blocks -= btodb(deallocated);
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
	}
	(void) ffs_syncvnode(vp, MNT_WAIT, 0);
	/*
	 * After the buffers are invalidated and on-disk pointers are
	 * cleared, free the blocks.
	 */
	for (blkp = allociblk; blkp < allocblk; blkp++) {
#ifdef INVARIANTS
		if (blkp == allociblk)
			lbns_remfree = lbns;
		bp = getblk(vp, *lbns_remfree, fs->fs_bsize, 0, 0,
		    GB_NOCREAT | GB_UNMAPPED);
		if (bp != NULL) {
			panic("zombie1 %jd %ju %ju",
			    (intmax_t)bp->b_lblkno, (uintmax_t)bp->b_blkno,
			    (uintmax_t)fsbtodb(fs, *blkp));
		}
		lbns_remfree++;
#endif
		ffs_blkfree(ump, fs, ump->um_devvp, *blkp, fs->fs_bsize,
		    ip->i_number, vp->v_type, NULL, SINGLETON_KEY);
	}
	return (error);
}

/*
 * Balloc defines the structure of file system storage
 * by allocating the physical blocks on a device given
 * the inode and the logical block number in a file.
 * This is the allocation strategy for UFS2. Above is
 * the allocation strategy for UFS1.
 */
int
ffs_balloc_ufs2(struct vnode *vp, off_t startoffset, int size,
    struct ucred *cred, int flags, struct buf **bpp)
{
	struct inode *ip;
	struct ufs2_dinode *dp;
	ufs_lbn_t lbn, lastlbn;
	struct fs *fs;
	struct buf *bp, *nbp;
	struct ufsmount *ump;
	struct indir indirs[UFS_NIADDR + 2];
	ufs2_daddr_t nb, newb, *bap, pref;
	ufs2_daddr_t *allocib, *blkp, *allocblk, allociblk[UFS_NIADDR + 1];
	ufs2_daddr_t *lbns_remfree, lbns[UFS_NIADDR + 1];
	int deallocated, osize, nsize, num, i, error;
	int unwindidx = -1;
	int saved_inbdflush;
	static struct timeval lastfail;
	static int curfail;
	int gbflags, reclaimed;

	ip = VTOI(vp);
	dp = ip->i_din2;
	fs = ITOFS(ip);
	ump = ITOUMP(ip);
	lbn = lblkno(fs, startoffset);
	size = blkoff(fs, startoffset) + size;
	reclaimed = 0;
	if (size > fs->fs_bsize)
		panic("ffs_balloc_ufs2: blk too big");
	*bpp = NULL;
	if (lbn < 0)
		return (EFBIG);
	gbflags = (flags & BA_UNMAPPED) != 0 ? GB_UNMAPPED : 0;

	if (DOINGSOFTDEP(vp))
		softdep_prealloc(vp, MNT_WAIT);
	
	/*
	 * Check for allocating external data.
	 */
	if (flags & IO_EXT) {
		if (lbn >= UFS_NXADDR)
			return (EFBIG);
		/*
		 * If the next write will extend the data into a new block,
		 * and the data is currently composed of a fragment
		 * this fragment has to be extended to be a full block.
		 */
		lastlbn = lblkno(fs, dp->di_extsize);
		if (lastlbn < lbn) {
			nb = lastlbn;
			osize = sblksize(fs, dp->di_extsize, nb);
			if (osize < fs->fs_bsize && osize > 0) {
				UFS_LOCK(ump);
				error = ffs_realloccg(ip, -1 - nb,
				    dp->di_extb[nb],
				    ffs_blkpref_ufs2(ip, lastlbn, (int)nb,
				    &dp->di_extb[0]), osize,
				    (int)fs->fs_bsize, flags, cred, &bp);
				if (error)
					return (error);
				if (DOINGSOFTDEP(vp))
					softdep_setup_allocext(ip, nb,
					    dbtofsb(fs, bp->b_blkno),
					    dp->di_extb[nb],
					    fs->fs_bsize, osize, bp);
				dp->di_extsize = smalllblktosize(fs, nb + 1);
				dp->di_extb[nb] = dbtofsb(fs, bp->b_blkno);
				bp->b_xflags |= BX_ALTDATA;
				ip->i_flag |= IN_CHANGE;
				if (flags & IO_SYNC)
					bwrite(bp);
				else
					bawrite(bp);
			}
		}
		/*
		 * All blocks are direct blocks
		 */
		if (flags & BA_METAONLY)
			panic("ffs_balloc_ufs2: BA_METAONLY for ext block");
		nb = dp->di_extb[lbn];
		if (nb != 0 && dp->di_extsize >= smalllblktosize(fs, lbn + 1)) {
			error = bread_gb(vp, -1 - lbn, fs->fs_bsize, NOCRED,
			    gbflags, &bp);
			if (error) {
				brelse(bp);
				return (error);
			}
			bp->b_blkno = fsbtodb(fs, nb);
			bp->b_xflags |= BX_ALTDATA;
			*bpp = bp;
			return (0);
		}
		if (nb != 0) {
			/*
			 * Consider need to reallocate a fragment.
			 */
			osize = fragroundup(fs, blkoff(fs, dp->di_extsize));
			nsize = fragroundup(fs, size);
			if (nsize <= osize) {
				error = bread_gb(vp, -1 - lbn, osize, NOCRED,
				    gbflags, &bp);
				if (error) {
					brelse(bp);
					return (error);
				}
				bp->b_blkno = fsbtodb(fs, nb);
				bp->b_xflags |= BX_ALTDATA;
			} else {
				UFS_LOCK(ump);
				error = ffs_realloccg(ip, -1 - lbn,
				    dp->di_extb[lbn],
				    ffs_blkpref_ufs2(ip, lbn, (int)lbn,
				    &dp->di_extb[0]), osize, nsize, flags,
				    cred, &bp);
				if (error)
					return (error);
				bp->b_xflags |= BX_ALTDATA;
				if (DOINGSOFTDEP(vp))
					softdep_setup_allocext(ip, lbn,
					    dbtofsb(fs, bp->b_blkno), nb,
					    nsize, osize, bp);
			}
		} else {
			if (dp->di_extsize < smalllblktosize(fs, lbn + 1))
				nsize = fragroundup(fs, size);
			else
				nsize = fs->fs_bsize;
			UFS_LOCK(ump);
			error = ffs_alloc(ip, lbn,
			   ffs_blkpref_ufs2(ip, lbn, (int)lbn, &dp->di_extb[0]),
			   nsize, flags, cred, &newb);
			if (error)
				return (error);
			bp = getblk(vp, -1 - lbn, nsize, 0, 0, gbflags);
			bp->b_blkno = fsbtodb(fs, newb);
			bp->b_xflags |= BX_ALTDATA;
			if (flags & BA_CLRBUF)
				vfs_bio_clrbuf(bp);
			if (DOINGSOFTDEP(vp))
				softdep_setup_allocext(ip, lbn, newb, 0,
				    nsize, 0, bp);
		}
		dp->di_extb[lbn] = dbtofsb(fs, bp->b_blkno);
		ip->i_flag |= IN_CHANGE;
		*bpp = bp;
		return (0);
	}
	/*
	 * If the next write will extend the file into a new block,
	 * and the file is currently composed of a fragment
	 * this fragment has to be extended to be a full block.
	 */
	lastlbn = lblkno(fs, ip->i_size);
	if (lastlbn < UFS_NDADDR && lastlbn < lbn) {
		nb = lastlbn;
		osize = blksize(fs, ip, nb);
		if (osize < fs->fs_bsize && osize > 0) {
			UFS_LOCK(ump);
			error = ffs_realloccg(ip, nb, dp->di_db[nb],
			    ffs_blkpref_ufs2(ip, lastlbn, (int)nb,
			    &dp->di_db[0]), osize, (int)fs->fs_bsize,
			    flags, cred, &bp);
			if (error)
				return (error);
			if (DOINGSOFTDEP(vp))
				softdep_setup_allocdirect(ip, nb,
				    dbtofsb(fs, bp->b_blkno),
				    dp->di_db[nb],
				    fs->fs_bsize, osize, bp);
			ip->i_size = smalllblktosize(fs, nb + 1);
			dp->di_size = ip->i_size;
			dp->di_db[nb] = dbtofsb(fs, bp->b_blkno);
			ip->i_flag |= IN_CHANGE | IN_UPDATE;
			if (flags & IO_SYNC)
				bwrite(bp);
			else
				bawrite(bp);
		}
	}
	/*
	 * The first UFS_NDADDR blocks are direct blocks
	 */
	if (lbn < UFS_NDADDR) {
		if (flags & BA_METAONLY)
			panic("ffs_balloc_ufs2: BA_METAONLY for direct block");
		nb = dp->di_db[lbn];
		if (nb != 0 && ip->i_size >= smalllblktosize(fs, lbn + 1)) {
			error = bread_gb(vp, lbn, fs->fs_bsize, NOCRED,
			    gbflags, &bp);
			if (error) {
				brelse(bp);
				return (error);
			}
			bp->b_blkno = fsbtodb(fs, nb);
			*bpp = bp;
			return (0);
		}
		if (nb != 0) {
			/*
			 * Consider need to reallocate a fragment.
			 */
			osize = fragroundup(fs, blkoff(fs, ip->i_size));
			nsize = fragroundup(fs, size);
			if (nsize <= osize) {
				error = bread_gb(vp, lbn, osize, NOCRED,
				    gbflags, &bp);
				if (error) {
					brelse(bp);
					return (error);
				}
				bp->b_blkno = fsbtodb(fs, nb);
			} else {
				UFS_LOCK(ump);
				error = ffs_realloccg(ip, lbn, dp->di_db[lbn],
				    ffs_blkpref_ufs2(ip, lbn, (int)lbn,
				    &dp->di_db[0]), osize, nsize, flags,
				    cred, &bp);
				if (error)
					return (error);
				if (DOINGSOFTDEP(vp))
					softdep_setup_allocdirect(ip, lbn,
					    dbtofsb(fs, bp->b_blkno), nb,
					    nsize, osize, bp);
			}
		} else {
			if (ip->i_size < smalllblktosize(fs, lbn + 1))
				nsize = fragroundup(fs, size);
			else
				nsize = fs->fs_bsize;
			UFS_LOCK(ump);
			error = ffs_alloc(ip, lbn,
			    ffs_blkpref_ufs2(ip, lbn, (int)lbn,
				&dp->di_db[0]), nsize, flags, cred, &newb);
			if (error)
				return (error);
			bp = getblk(vp, lbn, nsize, 0, 0, gbflags);
			bp->b_blkno = fsbtodb(fs, newb);
			if (flags & BA_CLRBUF)
				vfs_bio_clrbuf(bp);
			if (DOINGSOFTDEP(vp))
				softdep_setup_allocdirect(ip, lbn, newb, 0,
				    nsize, 0, bp);
		}
		dp->di_db[lbn] = dbtofsb(fs, bp->b_blkno);
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
		*bpp = bp;
		return (0);
	}
	/*
	 * Determine the number of levels of indirection.
	 */
	pref = 0;
	if ((error = ufs_getlbns(vp, lbn, indirs, &num)) != 0)
		return(error);
#ifdef INVARIANTS
	if (num < 1)
		panic ("ffs_balloc_ufs2: ufs_getlbns returned indirect block");
#endif
	saved_inbdflush = curthread_pflags_set(TDP_INBDFLUSH);
	/*
	 * Fetch the first indirect block allocating if necessary.
	 */
	--num;
	nb = dp->di_ib[indirs[0].in_off];
	allocib = NULL;
	allocblk = allociblk;
	lbns_remfree = lbns;
	if (nb == 0) {
		UFS_LOCK(ump);
		pref = ffs_blkpref_ufs2(ip, lbn, -indirs[0].in_off - 1,
		    (ufs2_daddr_t *)0);
		if ((error = ffs_alloc(ip, lbn, pref, (int)fs->fs_bsize,
		    flags, cred, &newb)) != 0) {
			curthread_pflags_restore(saved_inbdflush);
			return (error);
		}
		pref = newb + fs->fs_frag;
		nb = newb;
		MPASS(allocblk < allociblk + nitems(allociblk));
		MPASS(lbns_remfree < lbns + nitems(lbns));
		*allocblk++ = nb;
		*lbns_remfree++ = indirs[1].in_lbn;
		bp = getblk(vp, indirs[1].in_lbn, fs->fs_bsize, 0, 0,
		    GB_UNMAPPED);
		bp->b_blkno = fsbtodb(fs, nb);
		vfs_bio_clrbuf(bp);
		if (DOINGSOFTDEP(vp)) {
			softdep_setup_allocdirect(ip,
			    UFS_NDADDR + indirs[0].in_off, newb, 0,
			    fs->fs_bsize, 0, bp);
			bdwrite(bp);
		} else if ((flags & IO_SYNC) == 0 && DOINGASYNC(vp)) {
			if (bp->b_bufsize == fs->fs_bsize)
				bp->b_flags |= B_CLUSTEROK;
			bdwrite(bp);
		} else {
			if ((error = bwrite(bp)) != 0)
				goto fail;
		}
		allocib = &dp->di_ib[indirs[0].in_off];
		*allocib = nb;
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
	}
	/*
	 * Fetch through the indirect blocks, allocating as necessary.
	 */
retry:
	for (i = 1;;) {
		error = bread(vp,
		    indirs[i].in_lbn, (int)fs->fs_bsize, NOCRED, &bp);
		if (error) {
			brelse(bp);
			goto fail;
		}
		bap = (ufs2_daddr_t *)bp->b_data;
		nb = bap[indirs[i].in_off];
		if (i == num)
			break;
		i += 1;
		if (nb != 0) {
			bqrelse(bp);
			continue;
		}
		UFS_LOCK(ump);
		/*
		 * If parent indirect has just been allocated, try to cluster
		 * immediately following it.
		 */
		if (pref == 0)
			pref = ffs_blkpref_ufs2(ip, lbn, i - num - 1,
			    (ufs2_daddr_t *)0);
		if ((error = ffs_alloc(ip, lbn, pref, (int)fs->fs_bsize,
		    flags | IO_BUFLOCKED, cred, &newb)) != 0) {
			brelse(bp);
			if (DOINGSOFTDEP(vp) && ++reclaimed == 1) {
				UFS_LOCK(ump);
				softdep_request_cleanup(fs, vp, cred,
				    FLUSH_BLOCKS_WAIT);
				UFS_UNLOCK(ump);
				goto retry;
			}
			if (ppsratecheck(&lastfail, &curfail, 1)) {
				ffs_fserr(fs, ip->i_number, "filesystem full");
				uprintf("\n%s: write failed, filesystem "
				    "is full\n", fs->fs_fsmnt);
			}
			goto fail;
		}
		pref = newb + fs->fs_frag;
		nb = newb;
		MPASS(allocblk < allociblk + nitems(allociblk));
		MPASS(lbns_remfree < lbns + nitems(lbns));
		*allocblk++ = nb;
		*lbns_remfree++ = indirs[i].in_lbn;
		nbp = getblk(vp, indirs[i].in_lbn, fs->fs_bsize, 0, 0,
		    GB_UNMAPPED);
		nbp->b_blkno = fsbtodb(fs, nb);
		vfs_bio_clrbuf(nbp);
		if (DOINGSOFTDEP(vp)) {
			softdep_setup_allocindir_meta(nbp, ip, bp,
			    indirs[i - 1].in_off, nb);
			bdwrite(nbp);
		} else if ((flags & IO_SYNC) == 0 && DOINGASYNC(vp)) {
			if (nbp->b_bufsize == fs->fs_bsize)
				nbp->b_flags |= B_CLUSTEROK;
			bdwrite(nbp);
		} else {
			if ((error = bwrite(nbp)) != 0) {
				brelse(bp);
				goto fail;
			}
		}
		bap[indirs[i - 1].in_off] = nb;
		if (allocib == NULL && unwindidx < 0)
			unwindidx = i - 1;
		/*
		 * If required, write synchronously, otherwise use
		 * delayed write.
		 */
		if (flags & IO_SYNC) {
			bwrite(bp);
		} else {
			if (bp->b_bufsize == fs->fs_bsize)
				bp->b_flags |= B_CLUSTEROK;
			bdwrite(bp);
		}
	}
	/*
	 * If asked only for the indirect block, then return it.
	 */
	if (flags & BA_METAONLY) {
		curthread_pflags_restore(saved_inbdflush);
		*bpp = bp;
		return (0);
	}
	/*
	 * Get the data block, allocating if necessary.
	 */
	if (nb == 0) {
		UFS_LOCK(ump);
		/*
		 * If allocating metadata at the front of the cylinder
		 * group and parent indirect block has just been allocated,
		 * then cluster next to it if it is the first indirect in
		 * the file. Otherwise it has been allocated in the metadata
		 * area, so we want to find our own place out in the data area.
		 */
		if (pref == 0 || (lbn > UFS_NDADDR && fs->fs_metaspace != 0))
			pref = ffs_blkpref_ufs2(ip, lbn, indirs[i].in_off,
			    &bap[0]);
		error = ffs_alloc(ip, lbn, pref, (int)fs->fs_bsize,
		    flags | IO_BUFLOCKED, cred, &newb);
		if (error) {
			brelse(bp);
			if (DOINGSOFTDEP(vp) && ++reclaimed == 1) {
				UFS_LOCK(ump);
				softdep_request_cleanup(fs, vp, cred,
				    FLUSH_BLOCKS_WAIT);
				UFS_UNLOCK(ump);
				goto retry;
			}
			if (ppsratecheck(&lastfail, &curfail, 1)) {
				ffs_fserr(fs, ip->i_number, "filesystem full");
				uprintf("\n%s: write failed, filesystem "
				    "is full\n", fs->fs_fsmnt);
			}
			goto fail;
		}
		nb = newb;
		MPASS(allocblk < allociblk + nitems(allociblk));
		MPASS(lbns_remfree < lbns + nitems(lbns));
		*allocblk++ = nb;
		*lbns_remfree++ = lbn;
		nbp = getblk(vp, lbn, fs->fs_bsize, 0, 0, gbflags);
		nbp->b_blkno = fsbtodb(fs, nb);
		if (flags & BA_CLRBUF)
			vfs_bio_clrbuf(nbp);
		if (DOINGSOFTDEP(vp))
			softdep_setup_allocindir_page(ip, lbn, bp,
			    indirs[i].in_off, nb, 0, nbp);
		bap[indirs[i].in_off] = nb;
		/*
		 * If required, write synchronously, otherwise use
		 * delayed write.
		 */
		if (flags & IO_SYNC) {
			bwrite(bp);
		} else {
			if (bp->b_bufsize == fs->fs_bsize)
				bp->b_flags |= B_CLUSTEROK;
			bdwrite(bp);
		}
		curthread_pflags_restore(saved_inbdflush);
		*bpp = nbp;
		return (0);
	}
	brelse(bp);
	/*
	 * If requested clear invalid portions of the buffer.  If we
	 * have to do a read-before-write (typical if BA_CLRBUF is set),
	 * try to do some read-ahead in the sequential case to reduce
	 * the number of I/O transactions.
	 */
	if (flags & BA_CLRBUF) {
		int seqcount = (flags & BA_SEQMASK) >> BA_SEQSHIFT;
		if (seqcount != 0 &&
		    (vp->v_mount->mnt_flag & MNT_NOCLUSTERR) == 0 &&
		    !(vm_page_count_severe() || buf_dirty_count_severe())) {
			error = cluster_read(vp, ip->i_size, lbn,
			    (int)fs->fs_bsize, NOCRED,
			    MAXBSIZE, seqcount, gbflags, &nbp);
		} else {
			error = bread_gb(vp, lbn, (int)fs->fs_bsize,
			    NOCRED, gbflags, &nbp);
		}
		if (error) {
			brelse(nbp);
			goto fail;
		}
	} else {
		nbp = getblk(vp, lbn, fs->fs_bsize, 0, 0, gbflags);
		nbp->b_blkno = fsbtodb(fs, nb);
	}
	curthread_pflags_restore(saved_inbdflush);
	*bpp = nbp;
	return (0);
fail:
	curthread_pflags_restore(saved_inbdflush);
	/*
	 * If we have failed to allocate any blocks, simply return the error.
	 * This is the usual case and avoids the need to fsync the file.
	 */
	if (allocblk == allociblk && allocib == NULL && unwindidx == -1)
		return (error);
	/*
	 * If we have failed part way through block allocation, we
	 * have to deallocate any indirect blocks that we have allocated.
	 * We have to fsync the file before we start to get rid of all
	 * of its dependencies so that we do not leave them dangling.
	 * We have to sync it at the end so that the soft updates code
	 * does not find any untracked changes. Although this is really
	 * slow, running out of disk space is not expected to be a common
	 * occurrence. The error return from fsync is ignored as we already
	 * have an error to return to the user.
	 *
	 * XXX Still have to journal the free below
	 */
	(void) ffs_syncvnode(vp, MNT_WAIT, 0);
	for (deallocated = 0, blkp = allociblk, lbns_remfree = lbns;
	     blkp < allocblk; blkp++, lbns_remfree++) {
		/*
		 * We shall not leave the freed blocks on the vnode
		 * buffer object lists.
		 */
		bp = getblk(vp, *lbns_remfree, fs->fs_bsize, 0, 0,
		    GB_NOCREAT | GB_UNMAPPED);
		if (bp != NULL) {
			KASSERT(bp->b_blkno == fsbtodb(fs, *blkp),
			    ("mismatch2 l %jd %jd b %ju %ju",
			    (intmax_t)bp->b_lblkno, (uintmax_t)*lbns_remfree,
			    (uintmax_t)bp->b_blkno,
			    (uintmax_t)fsbtodb(fs, *blkp)));
			bp->b_flags |= B_INVAL | B_RELBUF | B_NOCACHE;
			bp->b_flags &= ~(B_ASYNC | B_CACHE);
			brelse(bp);
		}
		deallocated += fs->fs_bsize;
	}
	if (allocib != NULL) {
		*allocib = 0;
	} else if (unwindidx >= 0) {
		int r;

		r = bread(vp, indirs[unwindidx].in_lbn, 
		    (int)fs->fs_bsize, NOCRED, &bp);
		if (r) {
			panic("Could not unwind indirect block, error %d", r);
			brelse(bp);
		} else {
			bap = (ufs2_daddr_t *)bp->b_data;
			bap[indirs[unwindidx].in_off] = 0;
			if (flags & IO_SYNC) {
				bwrite(bp);
			} else {
				if (bp->b_bufsize == fs->fs_bsize)
					bp->b_flags |= B_CLUSTEROK;
				bdwrite(bp);
			}
		}
	}
	if (deallocated) {
#ifdef QUOTA
		/*
		 * Restore user's disk quota because allocation failed.
		 */
		(void) chkdq(ip, -btodb(deallocated), cred, FORCE);
#endif
		dp->di_blocks -= btodb(deallocated);
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
	}
	(void) ffs_syncvnode(vp, MNT_WAIT, 0);
	/*
	 * After the buffers are invalidated and on-disk pointers are
	 * cleared, free the blocks.
	 */
	for (blkp = allociblk; blkp < allocblk; blkp++) {
#ifdef INVARIANTS
		if (blkp == allociblk)
			lbns_remfree = lbns;
		bp = getblk(vp, *lbns_remfree, fs->fs_bsize, 0, 0,
		    GB_NOCREAT | GB_UNMAPPED);
		if (bp != NULL) {
			panic("zombie2 %jd %ju %ju",
			    (intmax_t)bp->b_lblkno, (uintmax_t)bp->b_blkno,
			    (uintmax_t)fsbtodb(fs, *blkp));
		}
		lbns_remfree++;
#endif
		ffs_blkfree(ump, fs, ump->um_devvp, *blkp, fs->fs_bsize,
		    ip->i_number, vp->v_type, NULL, SINGLETON_KEY);
	}
	return (error);
}
