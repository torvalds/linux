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
 *	@(#)ffs_alloc.c	8.18 (Berkeley) 5/26/95
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_quota.h"

#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/systm.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/kernel.h>
#include <sys/syscallsubr.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/taskqueue.h>

#include <security/audit/audit.h>

#include <geom/geom.h>
#include <geom/geom_vfs.h>

#include <ufs/ufs/dir.h>
#include <ufs/ufs/extattr.h>
#include <ufs/ufs/quota.h>
#include <ufs/ufs/inode.h>
#include <ufs/ufs/ufs_extern.h>
#include <ufs/ufs/ufsmount.h>

#include <ufs/ffs/fs.h>
#include <ufs/ffs/ffs_extern.h>
#include <ufs/ffs/softdep.h>

typedef ufs2_daddr_t allocfcn_t(struct inode *ip, u_int cg, ufs2_daddr_t bpref,
				  int size, int rsize);

static ufs2_daddr_t ffs_alloccg(struct inode *, u_int, ufs2_daddr_t, int, int);
static ufs2_daddr_t
	      ffs_alloccgblk(struct inode *, struct buf *, ufs2_daddr_t, int);
static void	ffs_blkfree_cg(struct ufsmount *, struct fs *,
		    struct vnode *, ufs2_daddr_t, long, ino_t,
		    struct workhead *);
#ifdef INVARIANTS
static int	ffs_checkblk(struct inode *, ufs2_daddr_t, long);
#endif
static ufs2_daddr_t ffs_clusteralloc(struct inode *, u_int, ufs2_daddr_t, int);
static ino_t	ffs_dirpref(struct inode *);
static ufs2_daddr_t ffs_fragextend(struct inode *, u_int, ufs2_daddr_t,
		    int, int);
static ufs2_daddr_t	ffs_hashalloc
		(struct inode *, u_int, ufs2_daddr_t, int, int, allocfcn_t *);
static ufs2_daddr_t ffs_nodealloccg(struct inode *, u_int, ufs2_daddr_t, int,
		    int);
static ufs1_daddr_t ffs_mapsearch(struct fs *, struct cg *, ufs2_daddr_t, int);
static int	ffs_reallocblks_ufs1(struct vop_reallocblks_args *);
static int	ffs_reallocblks_ufs2(struct vop_reallocblks_args *);
static void	ffs_ckhash_cg(struct buf *);

/*
 * Allocate a block in the filesystem.
 *
 * The size of the requested block is given, which must be some
 * multiple of fs_fsize and <= fs_bsize.
 * A preference may be optionally specified. If a preference is given
 * the following hierarchy is used to allocate a block:
 *   1) allocate the requested block.
 *   2) allocate a rotationally optimal block in the same cylinder.
 *   3) allocate a block in the same cylinder group.
 *   4) quadradically rehash into other cylinder groups, until an
 *      available block is located.
 * If no block preference is given the following hierarchy is used
 * to allocate a block:
 *   1) allocate a block in the cylinder group that contains the
 *      inode for the file.
 *   2) quadradically rehash into other cylinder groups, until an
 *      available block is located.
 */
int
ffs_alloc(ip, lbn, bpref, size, flags, cred, bnp)
	struct inode *ip;
	ufs2_daddr_t lbn, bpref;
	int size, flags;
	struct ucred *cred;
	ufs2_daddr_t *bnp;
{
	struct fs *fs;
	struct ufsmount *ump;
	ufs2_daddr_t bno;
	u_int cg, reclaimed;
	static struct timeval lastfail;
	static int curfail;
	int64_t delta;
#ifdef QUOTA
	int error;
#endif

	*bnp = 0;
	ump = ITOUMP(ip);
	fs = ump->um_fs;
	mtx_assert(UFS_MTX(ump), MA_OWNED);
#ifdef INVARIANTS
	if ((u_int)size > fs->fs_bsize || fragoff(fs, size) != 0) {
		printf("dev = %s, bsize = %ld, size = %d, fs = %s\n",
		    devtoname(ump->um_dev), (long)fs->fs_bsize, size,
		    fs->fs_fsmnt);
		panic("ffs_alloc: bad size");
	}
	if (cred == NOCRED)
		panic("ffs_alloc: missing credential");
#endif /* INVARIANTS */
	reclaimed = 0;
retry:
#ifdef QUOTA
	UFS_UNLOCK(ump);
	error = chkdq(ip, btodb(size), cred, 0);
	if (error)
		return (error);
	UFS_LOCK(ump);
#endif
	if (size == fs->fs_bsize && fs->fs_cstotal.cs_nbfree == 0)
		goto nospace;
	if (priv_check_cred(cred, PRIV_VFS_BLOCKRESERVE) &&
	    freespace(fs, fs->fs_minfree) - numfrags(fs, size) < 0)
		goto nospace;
	if (bpref >= fs->fs_size)
		bpref = 0;
	if (bpref == 0)
		cg = ino_to_cg(fs, ip->i_number);
	else
		cg = dtog(fs, bpref);
	bno = ffs_hashalloc(ip, cg, bpref, size, size, ffs_alloccg);
	if (bno > 0) {
		delta = btodb(size);
		DIP_SET(ip, i_blocks, DIP(ip, i_blocks) + delta);
		if (flags & IO_EXT)
			ip->i_flag |= IN_CHANGE;
		else
			ip->i_flag |= IN_CHANGE | IN_UPDATE;
		*bnp = bno;
		return (0);
	}
nospace:
#ifdef QUOTA
	UFS_UNLOCK(ump);
	/*
	 * Restore user's disk quota because allocation failed.
	 */
	(void) chkdq(ip, -btodb(size), cred, FORCE);
	UFS_LOCK(ump);
#endif
	if (reclaimed == 0 && (flags & IO_BUFLOCKED) == 0) {
		reclaimed = 1;
		softdep_request_cleanup(fs, ITOV(ip), cred, FLUSH_BLOCKS_WAIT);
		goto retry;
	}
	UFS_UNLOCK(ump);
	if (reclaimed > 0 && ppsratecheck(&lastfail, &curfail, 1)) {
		ffs_fserr(fs, ip->i_number, "filesystem full");
		uprintf("\n%s: write failed, filesystem is full\n",
		    fs->fs_fsmnt);
	}
	return (ENOSPC);
}

/*
 * Reallocate a fragment to a bigger size
 *
 * The number and size of the old block is given, and a preference
 * and new size is also specified. The allocator attempts to extend
 * the original block. Failing that, the regular block allocator is
 * invoked to get an appropriate block.
 */
int
ffs_realloccg(ip, lbprev, bprev, bpref, osize, nsize, flags, cred, bpp)
	struct inode *ip;
	ufs2_daddr_t lbprev;
	ufs2_daddr_t bprev;
	ufs2_daddr_t bpref;
	int osize, nsize, flags;
	struct ucred *cred;
	struct buf **bpp;
{
	struct vnode *vp;
	struct fs *fs;
	struct buf *bp;
	struct ufsmount *ump;
	u_int cg, request, reclaimed;
	int error, gbflags;
	ufs2_daddr_t bno;
	static struct timeval lastfail;
	static int curfail;
	int64_t delta;

	vp = ITOV(ip);
	ump = ITOUMP(ip);
	fs = ump->um_fs;
	bp = NULL;
	gbflags = (flags & BA_UNMAPPED) != 0 ? GB_UNMAPPED : 0;

	mtx_assert(UFS_MTX(ump), MA_OWNED);
#ifdef INVARIANTS
	if (vp->v_mount->mnt_kern_flag & MNTK_SUSPENDED)
		panic("ffs_realloccg: allocation on suspended filesystem");
	if ((u_int)osize > fs->fs_bsize || fragoff(fs, osize) != 0 ||
	    (u_int)nsize > fs->fs_bsize || fragoff(fs, nsize) != 0) {
		printf(
		"dev = %s, bsize = %ld, osize = %d, nsize = %d, fs = %s\n",
		    devtoname(ump->um_dev), (long)fs->fs_bsize, osize,
		    nsize, fs->fs_fsmnt);
		panic("ffs_realloccg: bad size");
	}
	if (cred == NOCRED)
		panic("ffs_realloccg: missing credential");
#endif /* INVARIANTS */
	reclaimed = 0;
retry:
	if (priv_check_cred(cred, PRIV_VFS_BLOCKRESERVE) &&
	    freespace(fs, fs->fs_minfree) -  numfrags(fs, nsize - osize) < 0) {
		goto nospace;
	}
	if (bprev == 0) {
		printf("dev = %s, bsize = %ld, bprev = %jd, fs = %s\n",
		    devtoname(ump->um_dev), (long)fs->fs_bsize, (intmax_t)bprev,
		    fs->fs_fsmnt);
		panic("ffs_realloccg: bad bprev");
	}
	UFS_UNLOCK(ump);
	/*
	 * Allocate the extra space in the buffer.
	 */
	error = bread_gb(vp, lbprev, osize, NOCRED, gbflags, &bp);
	if (error) {
		brelse(bp);
		return (error);
	}

	if (bp->b_blkno == bp->b_lblkno) {
		if (lbprev >= UFS_NDADDR)
			panic("ffs_realloccg: lbprev out of range");
		bp->b_blkno = fsbtodb(fs, bprev);
	}

#ifdef QUOTA
	error = chkdq(ip, btodb(nsize - osize), cred, 0);
	if (error) {
		brelse(bp);
		return (error);
	}
#endif
	/*
	 * Check for extension in the existing location.
	 */
	*bpp = NULL;
	cg = dtog(fs, bprev);
	UFS_LOCK(ump);
	bno = ffs_fragextend(ip, cg, bprev, osize, nsize);
	if (bno) {
		if (bp->b_blkno != fsbtodb(fs, bno))
			panic("ffs_realloccg: bad blockno");
		delta = btodb(nsize - osize);
		DIP_SET(ip, i_blocks, DIP(ip, i_blocks) + delta);
		if (flags & IO_EXT)
			ip->i_flag |= IN_CHANGE;
		else
			ip->i_flag |= IN_CHANGE | IN_UPDATE;
		allocbuf(bp, nsize);
		bp->b_flags |= B_DONE;
		vfs_bio_bzero_buf(bp, osize, nsize - osize);
		if ((bp->b_flags & (B_MALLOC | B_VMIO)) == B_VMIO)
			vfs_bio_set_valid(bp, osize, nsize - osize);
		*bpp = bp;
		return (0);
	}
	/*
	 * Allocate a new disk location.
	 */
	if (bpref >= fs->fs_size)
		bpref = 0;
	switch ((int)fs->fs_optim) {
	case FS_OPTSPACE:
		/*
		 * Allocate an exact sized fragment. Although this makes
		 * best use of space, we will waste time relocating it if
		 * the file continues to grow. If the fragmentation is
		 * less than half of the minimum free reserve, we choose
		 * to begin optimizing for time.
		 */
		request = nsize;
		if (fs->fs_minfree <= 5 ||
		    fs->fs_cstotal.cs_nffree >
		    (off_t)fs->fs_dsize * fs->fs_minfree / (2 * 100))
			break;
		log(LOG_NOTICE, "%s: optimization changed from SPACE to TIME\n",
			fs->fs_fsmnt);
		fs->fs_optim = FS_OPTTIME;
		break;
	case FS_OPTTIME:
		/*
		 * At this point we have discovered a file that is trying to
		 * grow a small fragment to a larger fragment. To save time,
		 * we allocate a full sized block, then free the unused portion.
		 * If the file continues to grow, the `ffs_fragextend' call
		 * above will be able to grow it in place without further
		 * copying. If aberrant programs cause disk fragmentation to
		 * grow within 2% of the free reserve, we choose to begin
		 * optimizing for space.
		 */
		request = fs->fs_bsize;
		if (fs->fs_cstotal.cs_nffree <
		    (off_t)fs->fs_dsize * (fs->fs_minfree - 2) / 100)
			break;
		log(LOG_NOTICE, "%s: optimization changed from TIME to SPACE\n",
			fs->fs_fsmnt);
		fs->fs_optim = FS_OPTSPACE;
		break;
	default:
		printf("dev = %s, optim = %ld, fs = %s\n",
		    devtoname(ump->um_dev), (long)fs->fs_optim, fs->fs_fsmnt);
		panic("ffs_realloccg: bad optim");
		/* NOTREACHED */
	}
	bno = ffs_hashalloc(ip, cg, bpref, request, nsize, ffs_alloccg);
	if (bno > 0) {
		bp->b_blkno = fsbtodb(fs, bno);
		if (!DOINGSOFTDEP(vp))
			/*
			 * The usual case is that a smaller fragment that
			 * was just allocated has been replaced with a bigger
			 * fragment or a full-size block. If it is marked as
			 * B_DELWRI, the current contents have not been written
			 * to disk. It is possible that the block was written
			 * earlier, but very uncommon. If the block has never
			 * been written, there is no need to send a BIO_DELETE
			 * for it when it is freed. The gain from avoiding the
			 * TRIMs for the common case of unwritten blocks far
			 * exceeds the cost of the write amplification for the
			 * uncommon case of failing to send a TRIM for a block
			 * that had been written.
			 */
			ffs_blkfree(ump, fs, ump->um_devvp, bprev, (long)osize,
			    ip->i_number, vp->v_type, NULL,
			    (bp->b_flags & B_DELWRI) != 0 ?
			    NOTRIM_KEY : SINGLETON_KEY);
		delta = btodb(nsize - osize);
		DIP_SET(ip, i_blocks, DIP(ip, i_blocks) + delta);
		if (flags & IO_EXT)
			ip->i_flag |= IN_CHANGE;
		else
			ip->i_flag |= IN_CHANGE | IN_UPDATE;
		allocbuf(bp, nsize);
		bp->b_flags |= B_DONE;
		vfs_bio_bzero_buf(bp, osize, nsize - osize);
		if ((bp->b_flags & (B_MALLOC | B_VMIO)) == B_VMIO)
			vfs_bio_set_valid(bp, osize, nsize - osize);
		*bpp = bp;
		return (0);
	}
#ifdef QUOTA
	UFS_UNLOCK(ump);
	/*
	 * Restore user's disk quota because allocation failed.
	 */
	(void) chkdq(ip, -btodb(nsize - osize), cred, FORCE);
	UFS_LOCK(ump);
#endif
nospace:
	/*
	 * no space available
	 */
	if (reclaimed == 0 && (flags & IO_BUFLOCKED) == 0) {
		reclaimed = 1;
		UFS_UNLOCK(ump);
		if (bp) {
			brelse(bp);
			bp = NULL;
		}
		UFS_LOCK(ump);
		softdep_request_cleanup(fs, vp, cred, FLUSH_BLOCKS_WAIT);
		goto retry;
	}
	UFS_UNLOCK(ump);
	if (bp)
		brelse(bp);
	if (reclaimed > 0 && ppsratecheck(&lastfail, &curfail, 1)) {
		ffs_fserr(fs, ip->i_number, "filesystem full");
		uprintf("\n%s: write failed, filesystem is full\n",
		    fs->fs_fsmnt);
	}
	return (ENOSPC);
}

/*
 * Reallocate a sequence of blocks into a contiguous sequence of blocks.
 *
 * The vnode and an array of buffer pointers for a range of sequential
 * logical blocks to be made contiguous is given. The allocator attempts
 * to find a range of sequential blocks starting as close as possible
 * from the end of the allocation for the logical block immediately
 * preceding the current range. If successful, the physical block numbers
 * in the buffer pointers and in the inode are changed to reflect the new
 * allocation. If unsuccessful, the allocation is left unchanged. The
 * success in doing the reallocation is returned. Note that the error
 * return is not reflected back to the user. Rather the previous block
 * allocation will be used.
 */

SYSCTL_NODE(_vfs, OID_AUTO, ffs, CTLFLAG_RW, 0, "FFS filesystem");

static int doasyncfree = 1;
SYSCTL_INT(_vfs_ffs, OID_AUTO, doasyncfree, CTLFLAG_RW, &doasyncfree, 0,
"do not force synchronous writes when blocks are reallocated");

static int doreallocblks = 1;
SYSCTL_INT(_vfs_ffs, OID_AUTO, doreallocblks, CTLFLAG_RW, &doreallocblks, 0,
"enable block reallocation");

static int dotrimcons = 1;
SYSCTL_INT(_vfs_ffs, OID_AUTO, dotrimcons, CTLFLAG_RWTUN, &dotrimcons, 0,
"enable BIO_DELETE / TRIM consolidation");

static int maxclustersearch = 10;
SYSCTL_INT(_vfs_ffs, OID_AUTO, maxclustersearch, CTLFLAG_RW, &maxclustersearch,
0, "max number of cylinder group to search for contigous blocks");

#ifdef DEBUG
static volatile int prtrealloc = 0;
#endif

int
ffs_reallocblks(ap)
	struct vop_reallocblks_args /* {
		struct vnode *a_vp;
		struct cluster_save *a_buflist;
	} */ *ap;
{
	struct ufsmount *ump;

	/*
	 * We used to skip reallocating the blocks of a file into a
	 * contiguous sequence if the underlying flash device requested
	 * BIO_DELETE notifications, because devices that benefit from
	 * BIO_DELETE also benefit from not moving the data. However,
	 * the destination for the data is usually moved before the data
	 * is written to the initially allocated location, so we rarely
	 * suffer the penalty of extra writes. With the addition of the
	 * consolidation of contiguous blocks into single BIO_DELETE
	 * operations, having fewer but larger contiguous blocks reduces
	 * the number of (slow and expensive) BIO_DELETE operations. So
	 * when doing BIO_DELETE consolidation, we do block reallocation.
	 *
	 * Skip if reallocblks has been disabled globally.
	 */
	ump = ap->a_vp->v_mount->mnt_data;
	if ((((ump->um_flags) & UM_CANDELETE) != 0 && dotrimcons == 0) ||
	    doreallocblks == 0)
		return (ENOSPC);

	/*
	 * We can't wait in softdep prealloc as it may fsync and recurse
	 * here.  Instead we simply fail to reallocate blocks if this
	 * rare condition arises.
	 */
	if (DOINGSOFTDEP(ap->a_vp))
		if (softdep_prealloc(ap->a_vp, MNT_NOWAIT) != 0)
			return (ENOSPC);
	if (ump->um_fstype == UFS1)
		return (ffs_reallocblks_ufs1(ap));
	return (ffs_reallocblks_ufs2(ap));
}
	
static int
ffs_reallocblks_ufs1(ap)
	struct vop_reallocblks_args /* {
		struct vnode *a_vp;
		struct cluster_save *a_buflist;
	} */ *ap;
{
	struct fs *fs;
	struct inode *ip;
	struct vnode *vp;
	struct buf *sbp, *ebp, *bp;
	ufs1_daddr_t *bap, *sbap, *ebap;
	struct cluster_save *buflist;
	struct ufsmount *ump;
	ufs_lbn_t start_lbn, end_lbn;
	ufs1_daddr_t soff, newblk, blkno;
	ufs2_daddr_t pref;
	struct indir start_ap[UFS_NIADDR + 1], end_ap[UFS_NIADDR + 1], *idp;
	int i, cg, len, start_lvl, end_lvl, ssize;

	vp = ap->a_vp;
	ip = VTOI(vp);
	ump = ITOUMP(ip);
	fs = ump->um_fs;
	/*
	 * If we are not tracking block clusters or if we have less than 4%
	 * free blocks left, then do not attempt to cluster. Running with
	 * less than 5% free block reserve is not recommended and those that
	 * choose to do so do not expect to have good file layout.
	 */
	if (fs->fs_contigsumsize <= 0 || freespace(fs, 4) < 0)
		return (ENOSPC);
	buflist = ap->a_buflist;
	len = buflist->bs_nchildren;
	start_lbn = buflist->bs_children[0]->b_lblkno;
	end_lbn = start_lbn + len - 1;
#ifdef INVARIANTS
	for (i = 0; i < len; i++)
		if (!ffs_checkblk(ip,
		   dbtofsb(fs, buflist->bs_children[i]->b_blkno), fs->fs_bsize))
			panic("ffs_reallocblks: unallocated block 1");
	for (i = 1; i < len; i++)
		if (buflist->bs_children[i]->b_lblkno != start_lbn + i)
			panic("ffs_reallocblks: non-logical cluster");
	blkno = buflist->bs_children[0]->b_blkno;
	ssize = fsbtodb(fs, fs->fs_frag);
	for (i = 1; i < len - 1; i++)
		if (buflist->bs_children[i]->b_blkno != blkno + (i * ssize))
			panic("ffs_reallocblks: non-physical cluster %d", i);
#endif
	/*
	 * If the cluster crosses the boundary for the first indirect
	 * block, leave space for the indirect block. Indirect blocks
	 * are initially laid out in a position after the last direct
	 * block. Block reallocation would usually destroy locality by
	 * moving the indirect block out of the way to make room for
	 * data blocks if we didn't compensate here. We should also do
	 * this for other indirect block boundaries, but it is only
	 * important for the first one.
	 */
	if (start_lbn < UFS_NDADDR && end_lbn >= UFS_NDADDR)
		return (ENOSPC);
	/*
	 * If the latest allocation is in a new cylinder group, assume that
	 * the filesystem has decided to move and do not force it back to
	 * the previous cylinder group.
	 */
	if (dtog(fs, dbtofsb(fs, buflist->bs_children[0]->b_blkno)) !=
	    dtog(fs, dbtofsb(fs, buflist->bs_children[len - 1]->b_blkno)))
		return (ENOSPC);
	if (ufs_getlbns(vp, start_lbn, start_ap, &start_lvl) ||
	    ufs_getlbns(vp, end_lbn, end_ap, &end_lvl))
		return (ENOSPC);
	/*
	 * Get the starting offset and block map for the first block.
	 */
	if (start_lvl == 0) {
		sbap = &ip->i_din1->di_db[0];
		soff = start_lbn;
	} else {
		idp = &start_ap[start_lvl - 1];
		if (bread(vp, idp->in_lbn, (int)fs->fs_bsize, NOCRED, &sbp)) {
			brelse(sbp);
			return (ENOSPC);
		}
		sbap = (ufs1_daddr_t *)sbp->b_data;
		soff = idp->in_off;
	}
	/*
	 * If the block range spans two block maps, get the second map.
	 */
	ebap = NULL;
	if (end_lvl == 0 || (idp = &end_ap[end_lvl - 1])->in_off + 1 >= len) {
		ssize = len;
	} else {
#ifdef INVARIANTS
		if (start_lvl > 0 &&
		    start_ap[start_lvl - 1].in_lbn == idp->in_lbn)
			panic("ffs_reallocblk: start == end");
#endif
		ssize = len - (idp->in_off + 1);
		if (bread(vp, idp->in_lbn, (int)fs->fs_bsize, NOCRED, &ebp))
			goto fail;
		ebap = (ufs1_daddr_t *)ebp->b_data;
	}
	/*
	 * Find the preferred location for the cluster. If we have not
	 * previously failed at this endeavor, then follow our standard
	 * preference calculation. If we have failed at it, then pick up
	 * where we last ended our search.
	 */
	UFS_LOCK(ump);
	if (ip->i_nextclustercg == -1)
		pref = ffs_blkpref_ufs1(ip, start_lbn, soff, sbap);
	else
		pref = cgdata(fs, ip->i_nextclustercg);
	/*
	 * Search the block map looking for an allocation of the desired size.
	 * To avoid wasting too much time, we limit the number of cylinder
	 * groups that we will search.
	 */
	cg = dtog(fs, pref);
	for (i = min(maxclustersearch, fs->fs_ncg); i > 0; i--) {
		if ((newblk = ffs_clusteralloc(ip, cg, pref, len)) != 0)
			break;
		cg += 1;
		if (cg >= fs->fs_ncg)
			cg = 0;
	}
	/*
	 * If we have failed in our search, record where we gave up for
	 * next time. Otherwise, fall back to our usual search citerion.
	 */
	if (newblk == 0) {
		ip->i_nextclustercg = cg;
		UFS_UNLOCK(ump);
		goto fail;
	}
	ip->i_nextclustercg = -1;
	/*
	 * We have found a new contiguous block.
	 *
	 * First we have to replace the old block pointers with the new
	 * block pointers in the inode and indirect blocks associated
	 * with the file.
	 */
#ifdef DEBUG
	if (prtrealloc)
		printf("realloc: ino %ju, lbns %jd-%jd\n\told:",
		    (uintmax_t)ip->i_number,
		    (intmax_t)start_lbn, (intmax_t)end_lbn);
#endif
	blkno = newblk;
	for (bap = &sbap[soff], i = 0; i < len; i++, blkno += fs->fs_frag) {
		if (i == ssize) {
			bap = ebap;
			soff = -i;
		}
#ifdef INVARIANTS
		if (!ffs_checkblk(ip,
		   dbtofsb(fs, buflist->bs_children[i]->b_blkno), fs->fs_bsize))
			panic("ffs_reallocblks: unallocated block 2");
		if (dbtofsb(fs, buflist->bs_children[i]->b_blkno) != *bap)
			panic("ffs_reallocblks: alloc mismatch");
#endif
#ifdef DEBUG
		if (prtrealloc)
			printf(" %d,", *bap);
#endif
		if (DOINGSOFTDEP(vp)) {
			if (sbap == &ip->i_din1->di_db[0] && i < ssize)
				softdep_setup_allocdirect(ip, start_lbn + i,
				    blkno, *bap, fs->fs_bsize, fs->fs_bsize,
				    buflist->bs_children[i]);
			else
				softdep_setup_allocindir_page(ip, start_lbn + i,
				    i < ssize ? sbp : ebp, soff + i, blkno,
				    *bap, buflist->bs_children[i]);
		}
		*bap++ = blkno;
	}
	/*
	 * Next we must write out the modified inode and indirect blocks.
	 * For strict correctness, the writes should be synchronous since
	 * the old block values may have been written to disk. In practise
	 * they are almost never written, but if we are concerned about
	 * strict correctness, the `doasyncfree' flag should be set to zero.
	 *
	 * The test on `doasyncfree' should be changed to test a flag
	 * that shows whether the associated buffers and inodes have
	 * been written. The flag should be set when the cluster is
	 * started and cleared whenever the buffer or inode is flushed.
	 * We can then check below to see if it is set, and do the
	 * synchronous write only when it has been cleared.
	 */
	if (sbap != &ip->i_din1->di_db[0]) {
		if (doasyncfree)
			bdwrite(sbp);
		else
			bwrite(sbp);
	} else {
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
		if (!doasyncfree)
			ffs_update(vp, 1);
	}
	if (ssize < len) {
		if (doasyncfree)
			bdwrite(ebp);
		else
			bwrite(ebp);
	}
	/*
	 * Last, free the old blocks and assign the new blocks to the buffers.
	 */
#ifdef DEBUG
	if (prtrealloc)
		printf("\n\tnew:");
#endif
	for (blkno = newblk, i = 0; i < len; i++, blkno += fs->fs_frag) {
		bp = buflist->bs_children[i];
		if (!DOINGSOFTDEP(vp))
			/*
			 * The usual case is that a set of N-contiguous blocks
			 * that was just allocated has been replaced with a
			 * set of N+1-contiguous blocks. If they are marked as
			 * B_DELWRI, the current contents have not been written
			 * to disk. It is possible that the blocks were written
			 * earlier, but very uncommon. If the blocks have never
			 * been written, there is no need to send a BIO_DELETE
			 * for them when they are freed. The gain from avoiding
			 * the TRIMs for the common case of unwritten blocks
			 * far exceeds the cost of the write amplification for
			 * the uncommon case of failing to send a TRIM for the
			 * blocks that had been written.
			 */
			ffs_blkfree(ump, fs, ump->um_devvp,
			    dbtofsb(fs, bp->b_blkno),
			    fs->fs_bsize, ip->i_number, vp->v_type, NULL,
			    (bp->b_flags & B_DELWRI) != 0 ?
			    NOTRIM_KEY : SINGLETON_KEY);
		bp->b_blkno = fsbtodb(fs, blkno);
#ifdef INVARIANTS
		if (!ffs_checkblk(ip, dbtofsb(fs, bp->b_blkno), fs->fs_bsize))
			panic("ffs_reallocblks: unallocated block 3");
#endif
#ifdef DEBUG
		if (prtrealloc)
			printf(" %d,", blkno);
#endif
	}
#ifdef DEBUG
	if (prtrealloc) {
		prtrealloc--;
		printf("\n");
	}
#endif
	return (0);

fail:
	if (ssize < len)
		brelse(ebp);
	if (sbap != &ip->i_din1->di_db[0])
		brelse(sbp);
	return (ENOSPC);
}

static int
ffs_reallocblks_ufs2(ap)
	struct vop_reallocblks_args /* {
		struct vnode *a_vp;
		struct cluster_save *a_buflist;
	} */ *ap;
{
	struct fs *fs;
	struct inode *ip;
	struct vnode *vp;
	struct buf *sbp, *ebp, *bp;
	ufs2_daddr_t *bap, *sbap, *ebap;
	struct cluster_save *buflist;
	struct ufsmount *ump;
	ufs_lbn_t start_lbn, end_lbn;
	ufs2_daddr_t soff, newblk, blkno, pref;
	struct indir start_ap[UFS_NIADDR + 1], end_ap[UFS_NIADDR + 1], *idp;
	int i, cg, len, start_lvl, end_lvl, ssize;

	vp = ap->a_vp;
	ip = VTOI(vp);
	ump = ITOUMP(ip);
	fs = ump->um_fs;
	/*
	 * If we are not tracking block clusters or if we have less than 4%
	 * free blocks left, then do not attempt to cluster. Running with
	 * less than 5% free block reserve is not recommended and those that
	 * choose to do so do not expect to have good file layout.
	 */
	if (fs->fs_contigsumsize <= 0 || freespace(fs, 4) < 0)
		return (ENOSPC);
	buflist = ap->a_buflist;
	len = buflist->bs_nchildren;
	start_lbn = buflist->bs_children[0]->b_lblkno;
	end_lbn = start_lbn + len - 1;
#ifdef INVARIANTS
	for (i = 0; i < len; i++)
		if (!ffs_checkblk(ip,
		   dbtofsb(fs, buflist->bs_children[i]->b_blkno), fs->fs_bsize))
			panic("ffs_reallocblks: unallocated block 1");
	for (i = 1; i < len; i++)
		if (buflist->bs_children[i]->b_lblkno != start_lbn + i)
			panic("ffs_reallocblks: non-logical cluster");
	blkno = buflist->bs_children[0]->b_blkno;
	ssize = fsbtodb(fs, fs->fs_frag);
	for (i = 1; i < len - 1; i++)
		if (buflist->bs_children[i]->b_blkno != blkno + (i * ssize))
			panic("ffs_reallocblks: non-physical cluster %d", i);
#endif
	/*
	 * If the cluster crosses the boundary for the first indirect
	 * block, do not move anything in it. Indirect blocks are
	 * usually initially laid out in a position between the data
	 * blocks. Block reallocation would usually destroy locality by
	 * moving the indirect block out of the way to make room for
	 * data blocks if we didn't compensate here. We should also do
	 * this for other indirect block boundaries, but it is only
	 * important for the first one.
	 */
	if (start_lbn < UFS_NDADDR && end_lbn >= UFS_NDADDR)
		return (ENOSPC);
	/*
	 * If the latest allocation is in a new cylinder group, assume that
	 * the filesystem has decided to move and do not force it back to
	 * the previous cylinder group.
	 */
	if (dtog(fs, dbtofsb(fs, buflist->bs_children[0]->b_blkno)) !=
	    dtog(fs, dbtofsb(fs, buflist->bs_children[len - 1]->b_blkno)))
		return (ENOSPC);
	if (ufs_getlbns(vp, start_lbn, start_ap, &start_lvl) ||
	    ufs_getlbns(vp, end_lbn, end_ap, &end_lvl))
		return (ENOSPC);
	/*
	 * Get the starting offset and block map for the first block.
	 */
	if (start_lvl == 0) {
		sbap = &ip->i_din2->di_db[0];
		soff = start_lbn;
	} else {
		idp = &start_ap[start_lvl - 1];
		if (bread(vp, idp->in_lbn, (int)fs->fs_bsize, NOCRED, &sbp)) {
			brelse(sbp);
			return (ENOSPC);
		}
		sbap = (ufs2_daddr_t *)sbp->b_data;
		soff = idp->in_off;
	}
	/*
	 * If the block range spans two block maps, get the second map.
	 */
	ebap = NULL;
	if (end_lvl == 0 || (idp = &end_ap[end_lvl - 1])->in_off + 1 >= len) {
		ssize = len;
	} else {
#ifdef INVARIANTS
		if (start_lvl > 0 &&
		    start_ap[start_lvl - 1].in_lbn == idp->in_lbn)
			panic("ffs_reallocblk: start == end");
#endif
		ssize = len - (idp->in_off + 1);
		if (bread(vp, idp->in_lbn, (int)fs->fs_bsize, NOCRED, &ebp))
			goto fail;
		ebap = (ufs2_daddr_t *)ebp->b_data;
	}
	/*
	 * Find the preferred location for the cluster. If we have not
	 * previously failed at this endeavor, then follow our standard
	 * preference calculation. If we have failed at it, then pick up
	 * where we last ended our search.
	 */
	UFS_LOCK(ump);
	if (ip->i_nextclustercg == -1)
		pref = ffs_blkpref_ufs2(ip, start_lbn, soff, sbap);
	else
		pref = cgdata(fs, ip->i_nextclustercg);
	/*
	 * Search the block map looking for an allocation of the desired size.
	 * To avoid wasting too much time, we limit the number of cylinder
	 * groups that we will search.
	 */
	cg = dtog(fs, pref);
	for (i = min(maxclustersearch, fs->fs_ncg); i > 0; i--) {
		if ((newblk = ffs_clusteralloc(ip, cg, pref, len)) != 0)
			break;
		cg += 1;
		if (cg >= fs->fs_ncg)
			cg = 0;
	}
	/*
	 * If we have failed in our search, record where we gave up for
	 * next time. Otherwise, fall back to our usual search citerion.
	 */
	if (newblk == 0) {
		ip->i_nextclustercg = cg;
		UFS_UNLOCK(ump);
		goto fail;
	}
	ip->i_nextclustercg = -1;
	/*
	 * We have found a new contiguous block.
	 *
	 * First we have to replace the old block pointers with the new
	 * block pointers in the inode and indirect blocks associated
	 * with the file.
	 */
#ifdef DEBUG
	if (prtrealloc)
		printf("realloc: ino %ju, lbns %jd-%jd\n\told:", (uintmax_t)ip->i_number,
		    (intmax_t)start_lbn, (intmax_t)end_lbn);
#endif
	blkno = newblk;
	for (bap = &sbap[soff], i = 0; i < len; i++, blkno += fs->fs_frag) {
		if (i == ssize) {
			bap = ebap;
			soff = -i;
		}
#ifdef INVARIANTS
		if (!ffs_checkblk(ip,
		   dbtofsb(fs, buflist->bs_children[i]->b_blkno), fs->fs_bsize))
			panic("ffs_reallocblks: unallocated block 2");
		if (dbtofsb(fs, buflist->bs_children[i]->b_blkno) != *bap)
			panic("ffs_reallocblks: alloc mismatch");
#endif
#ifdef DEBUG
		if (prtrealloc)
			printf(" %jd,", (intmax_t)*bap);
#endif
		if (DOINGSOFTDEP(vp)) {
			if (sbap == &ip->i_din2->di_db[0] && i < ssize)
				softdep_setup_allocdirect(ip, start_lbn + i,
				    blkno, *bap, fs->fs_bsize, fs->fs_bsize,
				    buflist->bs_children[i]);
			else
				softdep_setup_allocindir_page(ip, start_lbn + i,
				    i < ssize ? sbp : ebp, soff + i, blkno,
				    *bap, buflist->bs_children[i]);
		}
		*bap++ = blkno;
	}
	/*
	 * Next we must write out the modified inode and indirect blocks.
	 * For strict correctness, the writes should be synchronous since
	 * the old block values may have been written to disk. In practise
	 * they are almost never written, but if we are concerned about
	 * strict correctness, the `doasyncfree' flag should be set to zero.
	 *
	 * The test on `doasyncfree' should be changed to test a flag
	 * that shows whether the associated buffers and inodes have
	 * been written. The flag should be set when the cluster is
	 * started and cleared whenever the buffer or inode is flushed.
	 * We can then check below to see if it is set, and do the
	 * synchronous write only when it has been cleared.
	 */
	if (sbap != &ip->i_din2->di_db[0]) {
		if (doasyncfree)
			bdwrite(sbp);
		else
			bwrite(sbp);
	} else {
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
		if (!doasyncfree)
			ffs_update(vp, 1);
	}
	if (ssize < len) {
		if (doasyncfree)
			bdwrite(ebp);
		else
			bwrite(ebp);
	}
	/*
	 * Last, free the old blocks and assign the new blocks to the buffers.
	 */
#ifdef DEBUG
	if (prtrealloc)
		printf("\n\tnew:");
#endif
	for (blkno = newblk, i = 0; i < len; i++, blkno += fs->fs_frag) {
		bp = buflist->bs_children[i];
		if (!DOINGSOFTDEP(vp))
			/*
			 * The usual case is that a set of N-contiguous blocks
			 * that was just allocated has been replaced with a
			 * set of N+1-contiguous blocks. If they are marked as
			 * B_DELWRI, the current contents have not been written
			 * to disk. It is possible that the blocks were written
			 * earlier, but very uncommon. If the blocks have never
			 * been written, there is no need to send a BIO_DELETE
			 * for them when they are freed. The gain from avoiding
			 * the TRIMs for the common case of unwritten blocks
			 * far exceeds the cost of the write amplification for
			 * the uncommon case of failing to send a TRIM for the
			 * blocks that had been written.
			 */
			ffs_blkfree(ump, fs, ump->um_devvp,
			    dbtofsb(fs, bp->b_blkno),
			    fs->fs_bsize, ip->i_number, vp->v_type, NULL,
			    (bp->b_flags & B_DELWRI) != 0 ?
			    NOTRIM_KEY : SINGLETON_KEY);
		bp->b_blkno = fsbtodb(fs, blkno);
#ifdef INVARIANTS
		if (!ffs_checkblk(ip, dbtofsb(fs, bp->b_blkno), fs->fs_bsize))
			panic("ffs_reallocblks: unallocated block 3");
#endif
#ifdef DEBUG
		if (prtrealloc)
			printf(" %jd,", (intmax_t)blkno);
#endif
	}
#ifdef DEBUG
	if (prtrealloc) {
		prtrealloc--;
		printf("\n");
	}
#endif
	return (0);

fail:
	if (ssize < len)
		brelse(ebp);
	if (sbap != &ip->i_din2->di_db[0])
		brelse(sbp);
	return (ENOSPC);
}

/*
 * Allocate an inode in the filesystem.
 *
 * If allocating a directory, use ffs_dirpref to select the inode.
 * If allocating in a directory, the following hierarchy is followed:
 *   1) allocate the preferred inode.
 *   2) allocate an inode in the same cylinder group.
 *   3) quadradically rehash into other cylinder groups, until an
 *      available inode is located.
 * If no inode preference is given the following hierarchy is used
 * to allocate an inode:
 *   1) allocate an inode in cylinder group 0.
 *   2) quadradically rehash into other cylinder groups, until an
 *      available inode is located.
 */
int
ffs_valloc(pvp, mode, cred, vpp)
	struct vnode *pvp;
	int mode;
	struct ucred *cred;
	struct vnode **vpp;
{
	struct inode *pip;
	struct fs *fs;
	struct inode *ip;
	struct timespec ts;
	struct ufsmount *ump;
	ino_t ino, ipref;
	u_int cg;
	int error, error1, reclaimed;
	static struct timeval lastfail;
	static int curfail;

	*vpp = NULL;
	pip = VTOI(pvp);
	ump = ITOUMP(pip);
	fs = ump->um_fs;

	UFS_LOCK(ump);
	reclaimed = 0;
retry:
	if (fs->fs_cstotal.cs_nifree == 0)
		goto noinodes;

	if ((mode & IFMT) == IFDIR)
		ipref = ffs_dirpref(pip);
	else
		ipref = pip->i_number;
	if (ipref >= fs->fs_ncg * fs->fs_ipg)
		ipref = 0;
	cg = ino_to_cg(fs, ipref);
	/*
	 * Track number of dirs created one after another
	 * in a same cg without intervening by files.
	 */
	if ((mode & IFMT) == IFDIR) {
		if (fs->fs_contigdirs[cg] < 255)
			fs->fs_contigdirs[cg]++;
	} else {
		if (fs->fs_contigdirs[cg] > 0)
			fs->fs_contigdirs[cg]--;
	}
	ino = (ino_t)ffs_hashalloc(pip, cg, ipref, mode, 0,
					(allocfcn_t *)ffs_nodealloccg);
	if (ino == 0)
		goto noinodes;
	error = ffs_vget(pvp->v_mount, ino, LK_EXCLUSIVE, vpp);
	if (error) {
		error1 = ffs_vgetf(pvp->v_mount, ino, LK_EXCLUSIVE, vpp,
		    FFSV_FORCEINSMQ);
		ffs_vfree(pvp, ino, mode);
		if (error1 == 0) {
			ip = VTOI(*vpp);
			if (ip->i_mode)
				goto dup_alloc;
			ip->i_flag |= IN_MODIFIED;
			vput(*vpp);
		}
		return (error);
	}
	ip = VTOI(*vpp);
	if (ip->i_mode) {
dup_alloc:
		printf("mode = 0%o, inum = %ju, fs = %s\n",
		    ip->i_mode, (uintmax_t)ip->i_number, fs->fs_fsmnt);
		panic("ffs_valloc: dup alloc");
	}
	if (DIP(ip, i_blocks) && (fs->fs_flags & FS_UNCLEAN) == 0) {  /* XXX */
		printf("free inode %s/%lu had %ld blocks\n",
		    fs->fs_fsmnt, (u_long)ino, (long)DIP(ip, i_blocks));
		DIP_SET(ip, i_blocks, 0);
	}
	ip->i_flags = 0;
	DIP_SET(ip, i_flags, 0);
	/*
	 * Set up a new generation number for this inode.
	 */
	while (ip->i_gen == 0 || ++ip->i_gen == 0)
		ip->i_gen = arc4random();
	DIP_SET(ip, i_gen, ip->i_gen);
	if (fs->fs_magic == FS_UFS2_MAGIC) {
		vfs_timestamp(&ts);
		ip->i_din2->di_birthtime = ts.tv_sec;
		ip->i_din2->di_birthnsec = ts.tv_nsec;
	}
	ufs_prepare_reclaim(*vpp);
	ip->i_flag = 0;
	(*vpp)->v_vflag = 0;
	(*vpp)->v_type = VNON;
	if (fs->fs_magic == FS_UFS2_MAGIC) {
		(*vpp)->v_op = &ffs_vnodeops2;
		ip->i_flag |= IN_UFS2;
	} else {
		(*vpp)->v_op = &ffs_vnodeops1;
	}
	return (0);
noinodes:
	if (reclaimed == 0) {
		reclaimed = 1;
		softdep_request_cleanup(fs, pvp, cred, FLUSH_INODES_WAIT);
		goto retry;
	}
	UFS_UNLOCK(ump);
	if (ppsratecheck(&lastfail, &curfail, 1)) {
		ffs_fserr(fs, pip->i_number, "out of inodes");
		uprintf("\n%s: create/symlink failed, no inodes free\n",
		    fs->fs_fsmnt);
	}
	return (ENOSPC);
}

/*
 * Find a cylinder group to place a directory.
 *
 * The policy implemented by this algorithm is to allocate a
 * directory inode in the same cylinder group as its parent
 * directory, but also to reserve space for its files inodes
 * and data. Restrict the number of directories which may be
 * allocated one after another in the same cylinder group
 * without intervening allocation of files.
 *
 * If we allocate a first level directory then force allocation
 * in another cylinder group.
 */
static ino_t
ffs_dirpref(pip)
	struct inode *pip;
{
	struct fs *fs;
	int cg, prefcg, dirsize, cgsize;
	u_int avgifree, avgbfree, avgndir, curdirsize;
	u_int minifree, minbfree, maxndir;
	u_int mincg, minndir;
	u_int maxcontigdirs;

	mtx_assert(UFS_MTX(ITOUMP(pip)), MA_OWNED);
	fs = ITOFS(pip);

	avgifree = fs->fs_cstotal.cs_nifree / fs->fs_ncg;
	avgbfree = fs->fs_cstotal.cs_nbfree / fs->fs_ncg;
	avgndir = fs->fs_cstotal.cs_ndir / fs->fs_ncg;

	/*
	 * Force allocation in another cg if creating a first level dir.
	 */
	ASSERT_VOP_LOCKED(ITOV(pip), "ffs_dirpref");
	if (ITOV(pip)->v_vflag & VV_ROOT) {
		prefcg = arc4random() % fs->fs_ncg;
		mincg = prefcg;
		minndir = fs->fs_ipg;
		for (cg = prefcg; cg < fs->fs_ncg; cg++)
			if (fs->fs_cs(fs, cg).cs_ndir < minndir &&
			    fs->fs_cs(fs, cg).cs_nifree >= avgifree &&
			    fs->fs_cs(fs, cg).cs_nbfree >= avgbfree) {
				mincg = cg;
				minndir = fs->fs_cs(fs, cg).cs_ndir;
			}
		for (cg = 0; cg < prefcg; cg++)
			if (fs->fs_cs(fs, cg).cs_ndir < minndir &&
			    fs->fs_cs(fs, cg).cs_nifree >= avgifree &&
			    fs->fs_cs(fs, cg).cs_nbfree >= avgbfree) {
				mincg = cg;
				minndir = fs->fs_cs(fs, cg).cs_ndir;
			}
		return ((ino_t)(fs->fs_ipg * mincg));
	}

	/*
	 * Count various limits which used for
	 * optimal allocation of a directory inode.
	 */
	maxndir = min(avgndir + fs->fs_ipg / 16, fs->fs_ipg);
	minifree = avgifree - avgifree / 4;
	if (minifree < 1)
		minifree = 1;
	minbfree = avgbfree - avgbfree / 4;
	if (minbfree < 1)
		minbfree = 1;
	cgsize = fs->fs_fsize * fs->fs_fpg;
	dirsize = fs->fs_avgfilesize * fs->fs_avgfpdir;
	curdirsize = avgndir ? (cgsize - avgbfree * fs->fs_bsize) / avgndir : 0;
	if (dirsize < curdirsize)
		dirsize = curdirsize;
	if (dirsize <= 0)
		maxcontigdirs = 0;		/* dirsize overflowed */
	else
		maxcontigdirs = min((avgbfree * fs->fs_bsize) / dirsize, 255);
	if (fs->fs_avgfpdir > 0)
		maxcontigdirs = min(maxcontigdirs,
				    fs->fs_ipg / fs->fs_avgfpdir);
	if (maxcontigdirs == 0)
		maxcontigdirs = 1;

	/*
	 * Limit number of dirs in one cg and reserve space for 
	 * regular files, but only if we have no deficit in
	 * inodes or space.
	 *
	 * We are trying to find a suitable cylinder group nearby
	 * our preferred cylinder group to place a new directory.
	 * We scan from our preferred cylinder group forward looking
	 * for a cylinder group that meets our criterion. If we get
	 * to the final cylinder group and do not find anything,
	 * we start scanning forwards from the beginning of the
	 * filesystem. While it might seem sensible to start scanning
	 * backwards or even to alternate looking forward and backward,
	 * this approach fails badly when the filesystem is nearly full.
	 * Specifically, we first search all the areas that have no space
	 * and finally try the one preceding that. We repeat this on
	 * every request and in the case of the final block end up
	 * searching the entire filesystem. By jumping to the front
	 * of the filesystem, our future forward searches always look
	 * in new cylinder groups so finds every possible block after
	 * one pass over the filesystem.
	 */
	prefcg = ino_to_cg(fs, pip->i_number);
	for (cg = prefcg; cg < fs->fs_ncg; cg++)
		if (fs->fs_cs(fs, cg).cs_ndir < maxndir &&
		    fs->fs_cs(fs, cg).cs_nifree >= minifree &&
		    fs->fs_cs(fs, cg).cs_nbfree >= minbfree) {
			if (fs->fs_contigdirs[cg] < maxcontigdirs)
				return ((ino_t)(fs->fs_ipg * cg));
		}
	for (cg = 0; cg < prefcg; cg++)
		if (fs->fs_cs(fs, cg).cs_ndir < maxndir &&
		    fs->fs_cs(fs, cg).cs_nifree >= minifree &&
		    fs->fs_cs(fs, cg).cs_nbfree >= minbfree) {
			if (fs->fs_contigdirs[cg] < maxcontigdirs)
				return ((ino_t)(fs->fs_ipg * cg));
		}
	/*
	 * This is a backstop when we have deficit in space.
	 */
	for (cg = prefcg; cg < fs->fs_ncg; cg++)
		if (fs->fs_cs(fs, cg).cs_nifree >= avgifree)
			return ((ino_t)(fs->fs_ipg * cg));
	for (cg = 0; cg < prefcg; cg++)
		if (fs->fs_cs(fs, cg).cs_nifree >= avgifree)
			break;
	return ((ino_t)(fs->fs_ipg * cg));
}

/*
 * Select the desired position for the next block in a file.  The file is
 * logically divided into sections. The first section is composed of the
 * direct blocks and the next fs_maxbpg blocks. Each additional section
 * contains fs_maxbpg blocks.
 *
 * If no blocks have been allocated in the first section, the policy is to
 * request a block in the same cylinder group as the inode that describes
 * the file. The first indirect is allocated immediately following the last
 * direct block and the data blocks for the first indirect immediately
 * follow it.
 *
 * If no blocks have been allocated in any other section, the indirect 
 * block(s) are allocated in the same cylinder group as its inode in an
 * area reserved immediately following the inode blocks. The policy for
 * the data blocks is to place them in a cylinder group with a greater than
 * average number of free blocks. An appropriate cylinder group is found
 * by using a rotor that sweeps the cylinder groups. When a new group of
 * blocks is needed, the sweep begins in the cylinder group following the
 * cylinder group from which the previous allocation was made. The sweep
 * continues until a cylinder group with greater than the average number
 * of free blocks is found. If the allocation is for the first block in an
 * indirect block or the previous block is a hole, then the information on
 * the previous allocation is unavailable; here a best guess is made based
 * on the logical block number being allocated.
 *
 * If a section is already partially allocated, the policy is to
 * allocate blocks contiguously within the section if possible.
 */
ufs2_daddr_t
ffs_blkpref_ufs1(ip, lbn, indx, bap)
	struct inode *ip;
	ufs_lbn_t lbn;
	int indx;
	ufs1_daddr_t *bap;
{
	struct fs *fs;
	u_int cg, inocg;
	u_int avgbfree, startcg;
	ufs2_daddr_t pref;

	KASSERT(indx <= 0 || bap != NULL, ("need non-NULL bap"));
	mtx_assert(UFS_MTX(ITOUMP(ip)), MA_OWNED);
	fs = ITOFS(ip);
	/*
	 * Allocation of indirect blocks is indicated by passing negative
	 * values in indx: -1 for single indirect, -2 for double indirect,
	 * -3 for triple indirect. As noted below, we attempt to allocate
	 * the first indirect inline with the file data. For all later
	 * indirect blocks, the data is often allocated in other cylinder
	 * groups. However to speed random file access and to speed up
	 * fsck, the filesystem reserves the first fs_metaspace blocks
	 * (typically half of fs_minfree) of the data area of each cylinder
	 * group to hold these later indirect blocks.
	 */
	inocg = ino_to_cg(fs, ip->i_number);
	if (indx < 0) {
		/*
		 * Our preference for indirect blocks is the zone at the
		 * beginning of the inode's cylinder group data area that
		 * we try to reserve for indirect blocks.
		 */
		pref = cgmeta(fs, inocg);
		/*
		 * If we are allocating the first indirect block, try to
		 * place it immediately following the last direct block.
		 */
		if (indx == -1 && lbn < UFS_NDADDR + NINDIR(fs) &&
		    ip->i_din1->di_db[UFS_NDADDR - 1] != 0)
			pref = ip->i_din1->di_db[UFS_NDADDR - 1] + fs->fs_frag;
		return (pref);
	}
	/*
	 * If we are allocating the first data block in the first indirect
	 * block and the indirect has been allocated in the data block area,
	 * try to place it immediately following the indirect block.
	 */
	if (lbn == UFS_NDADDR) {
		pref = ip->i_din1->di_ib[0];
		if (pref != 0 && pref >= cgdata(fs, inocg) &&
		    pref < cgbase(fs, inocg + 1))
			return (pref + fs->fs_frag);
	}
	/*
	 * If we are at the beginning of a file, or we have already allocated
	 * the maximum number of blocks per cylinder group, or we do not
	 * have a block allocated immediately preceding us, then we need
	 * to decide where to start allocating new blocks.
	 */
	if (indx % fs->fs_maxbpg == 0 || bap[indx - 1] == 0) {
		/*
		 * If we are allocating a directory data block, we want
		 * to place it in the metadata area.
		 */
		if ((ip->i_mode & IFMT) == IFDIR)
			return (cgmeta(fs, inocg));
		/*
		 * Until we fill all the direct and all the first indirect's
		 * blocks, we try to allocate in the data area of the inode's
		 * cylinder group.
		 */
		if (lbn < UFS_NDADDR + NINDIR(fs))
			return (cgdata(fs, inocg));
		/*
		 * Find a cylinder with greater than average number of
		 * unused data blocks.
		 */
		if (indx == 0 || bap[indx - 1] == 0)
			startcg = inocg + lbn / fs->fs_maxbpg;
		else
			startcg = dtog(fs, bap[indx - 1]) + 1;
		startcg %= fs->fs_ncg;
		avgbfree = fs->fs_cstotal.cs_nbfree / fs->fs_ncg;
		for (cg = startcg; cg < fs->fs_ncg; cg++)
			if (fs->fs_cs(fs, cg).cs_nbfree >= avgbfree) {
				fs->fs_cgrotor = cg;
				return (cgdata(fs, cg));
			}
		for (cg = 0; cg <= startcg; cg++)
			if (fs->fs_cs(fs, cg).cs_nbfree >= avgbfree) {
				fs->fs_cgrotor = cg;
				return (cgdata(fs, cg));
			}
		return (0);
	}
	/*
	 * Otherwise, we just always try to lay things out contiguously.
	 */
	return (bap[indx - 1] + fs->fs_frag);
}

/*
 * Same as above, but for UFS2
 */
ufs2_daddr_t
ffs_blkpref_ufs2(ip, lbn, indx, bap)
	struct inode *ip;
	ufs_lbn_t lbn;
	int indx;
	ufs2_daddr_t *bap;
{
	struct fs *fs;
	u_int cg, inocg;
	u_int avgbfree, startcg;
	ufs2_daddr_t pref;

	KASSERT(indx <= 0 || bap != NULL, ("need non-NULL bap"));
	mtx_assert(UFS_MTX(ITOUMP(ip)), MA_OWNED);
	fs = ITOFS(ip);
	/*
	 * Allocation of indirect blocks is indicated by passing negative
	 * values in indx: -1 for single indirect, -2 for double indirect,
	 * -3 for triple indirect. As noted below, we attempt to allocate
	 * the first indirect inline with the file data. For all later
	 * indirect blocks, the data is often allocated in other cylinder
	 * groups. However to speed random file access and to speed up
	 * fsck, the filesystem reserves the first fs_metaspace blocks
	 * (typically half of fs_minfree) of the data area of each cylinder
	 * group to hold these later indirect blocks.
	 */
	inocg = ino_to_cg(fs, ip->i_number);
	if (indx < 0) {
		/*
		 * Our preference for indirect blocks is the zone at the
		 * beginning of the inode's cylinder group data area that
		 * we try to reserve for indirect blocks.
		 */
		pref = cgmeta(fs, inocg);
		/*
		 * If we are allocating the first indirect block, try to
		 * place it immediately following the last direct block.
		 */
		if (indx == -1 && lbn < UFS_NDADDR + NINDIR(fs) &&
		    ip->i_din2->di_db[UFS_NDADDR - 1] != 0)
			pref = ip->i_din2->di_db[UFS_NDADDR - 1] + fs->fs_frag;
		return (pref);
	}
	/*
	 * If we are allocating the first data block in the first indirect
	 * block and the indirect has been allocated in the data block area,
	 * try to place it immediately following the indirect block.
	 */
	if (lbn == UFS_NDADDR) {
		pref = ip->i_din2->di_ib[0];
		if (pref != 0 && pref >= cgdata(fs, inocg) &&
		    pref < cgbase(fs, inocg + 1))
			return (pref + fs->fs_frag);
	}
	/*
	 * If we are at the beginning of a file, or we have already allocated
	 * the maximum number of blocks per cylinder group, or we do not
	 * have a block allocated immediately preceding us, then we need
	 * to decide where to start allocating new blocks.
	 */
	if (indx % fs->fs_maxbpg == 0 || bap[indx - 1] == 0) {
		/*
		 * If we are allocating a directory data block, we want
		 * to place it in the metadata area.
		 */
		if ((ip->i_mode & IFMT) == IFDIR)
			return (cgmeta(fs, inocg));
		/*
		 * Until we fill all the direct and all the first indirect's
		 * blocks, we try to allocate in the data area of the inode's
		 * cylinder group.
		 */
		if (lbn < UFS_NDADDR + NINDIR(fs))
			return (cgdata(fs, inocg));
		/*
		 * Find a cylinder with greater than average number of
		 * unused data blocks.
		 */
		if (indx == 0 || bap[indx - 1] == 0)
			startcg = inocg + lbn / fs->fs_maxbpg;
		else
			startcg = dtog(fs, bap[indx - 1]) + 1;
		startcg %= fs->fs_ncg;
		avgbfree = fs->fs_cstotal.cs_nbfree / fs->fs_ncg;
		for (cg = startcg; cg < fs->fs_ncg; cg++)
			if (fs->fs_cs(fs, cg).cs_nbfree >= avgbfree) {
				fs->fs_cgrotor = cg;
				return (cgdata(fs, cg));
			}
		for (cg = 0; cg <= startcg; cg++)
			if (fs->fs_cs(fs, cg).cs_nbfree >= avgbfree) {
				fs->fs_cgrotor = cg;
				return (cgdata(fs, cg));
			}
		return (0);
	}
	/*
	 * Otherwise, we just always try to lay things out contiguously.
	 */
	return (bap[indx - 1] + fs->fs_frag);
}

/*
 * Implement the cylinder overflow algorithm.
 *
 * The policy implemented by this algorithm is:
 *   1) allocate the block in its requested cylinder group.
 *   2) quadradically rehash on the cylinder group number.
 *   3) brute force search for a free block.
 *
 * Must be called with the UFS lock held.  Will release the lock on success
 * and return with it held on failure.
 */
/*VARARGS5*/
static ufs2_daddr_t
ffs_hashalloc(ip, cg, pref, size, rsize, allocator)
	struct inode *ip;
	u_int cg;
	ufs2_daddr_t pref;
	int size;	/* Search size for data blocks, mode for inodes */
	int rsize;	/* Real allocated size. */
	allocfcn_t *allocator;
{
	struct fs *fs;
	ufs2_daddr_t result;
	u_int i, icg = cg;

	mtx_assert(UFS_MTX(ITOUMP(ip)), MA_OWNED);
#ifdef INVARIANTS
	if (ITOV(ip)->v_mount->mnt_kern_flag & MNTK_SUSPENDED)
		panic("ffs_hashalloc: allocation on suspended filesystem");
#endif
	fs = ITOFS(ip);
	/*
	 * 1: preferred cylinder group
	 */
	result = (*allocator)(ip, cg, pref, size, rsize);
	if (result)
		return (result);
	/*
	 * 2: quadratic rehash
	 */
	for (i = 1; i < fs->fs_ncg; i *= 2) {
		cg += i;
		if (cg >= fs->fs_ncg)
			cg -= fs->fs_ncg;
		result = (*allocator)(ip, cg, 0, size, rsize);
		if (result)
			return (result);
	}
	/*
	 * 3: brute force search
	 * Note that we start at i == 2, since 0 was checked initially,
	 * and 1 is always checked in the quadratic rehash.
	 */
	cg = (icg + 2) % fs->fs_ncg;
	for (i = 2; i < fs->fs_ncg; i++) {
		result = (*allocator)(ip, cg, 0, size, rsize);
		if (result)
			return (result);
		cg++;
		if (cg == fs->fs_ncg)
			cg = 0;
	}
	return (0);
}

/*
 * Determine whether a fragment can be extended.
 *
 * Check to see if the necessary fragments are available, and
 * if they are, allocate them.
 */
static ufs2_daddr_t
ffs_fragextend(ip, cg, bprev, osize, nsize)
	struct inode *ip;
	u_int cg;
	ufs2_daddr_t bprev;
	int osize, nsize;
{
	struct fs *fs;
	struct cg *cgp;
	struct buf *bp;
	struct ufsmount *ump;
	int nffree;
	long bno;
	int frags, bbase;
	int i, error;
	u_int8_t *blksfree;

	ump = ITOUMP(ip);
	fs = ump->um_fs;
	if (fs->fs_cs(fs, cg).cs_nffree < numfrags(fs, nsize - osize))
		return (0);
	frags = numfrags(fs, nsize);
	bbase = fragnum(fs, bprev);
	if (bbase > fragnum(fs, (bprev + frags - 1))) {
		/* cannot extend across a block boundary */
		return (0);
	}
	UFS_UNLOCK(ump);
	if ((error = ffs_getcg(fs, ump->um_devvp, cg, &bp, &cgp)) != 0)
		goto fail;
	bno = dtogd(fs, bprev);
	blksfree = cg_blksfree(cgp);
	for (i = numfrags(fs, osize); i < frags; i++)
		if (isclr(blksfree, bno + i))
			goto fail;
	/*
	 * the current fragment can be extended
	 * deduct the count on fragment being extended into
	 * increase the count on the remaining fragment (if any)
	 * allocate the extended piece
	 */
	for (i = frags; i < fs->fs_frag - bbase; i++)
		if (isclr(blksfree, bno + i))
			break;
	cgp->cg_frsum[i - numfrags(fs, osize)]--;
	if (i != frags)
		cgp->cg_frsum[i - frags]++;
	for (i = numfrags(fs, osize), nffree = 0; i < frags; i++) {
		clrbit(blksfree, bno + i);
		cgp->cg_cs.cs_nffree--;
		nffree++;
	}
	UFS_LOCK(ump);
	fs->fs_cstotal.cs_nffree -= nffree;
	fs->fs_cs(fs, cg).cs_nffree -= nffree;
	fs->fs_fmod = 1;
	ACTIVECLEAR(fs, cg);
	UFS_UNLOCK(ump);
	if (DOINGSOFTDEP(ITOV(ip)))
		softdep_setup_blkmapdep(bp, UFSTOVFS(ump), bprev,
		    frags, numfrags(fs, osize));
	bdwrite(bp);
	return (bprev);

fail:
	brelse(bp);
	UFS_LOCK(ump);
	return (0);

}

/*
 * Determine whether a block can be allocated.
 *
 * Check to see if a block of the appropriate size is available,
 * and if it is, allocate it.
 */
static ufs2_daddr_t
ffs_alloccg(ip, cg, bpref, size, rsize)
	struct inode *ip;
	u_int cg;
	ufs2_daddr_t bpref;
	int size;
	int rsize;
{
	struct fs *fs;
	struct cg *cgp;
	struct buf *bp;
	struct ufsmount *ump;
	ufs1_daddr_t bno;
	ufs2_daddr_t blkno;
	int i, allocsiz, error, frags;
	u_int8_t *blksfree;

	ump = ITOUMP(ip);
	fs = ump->um_fs;
	if (fs->fs_cs(fs, cg).cs_nbfree == 0 && size == fs->fs_bsize)
		return (0);
	UFS_UNLOCK(ump);
	if ((error = ffs_getcg(fs, ump->um_devvp, cg, &bp, &cgp)) != 0 ||
	   (cgp->cg_cs.cs_nbfree == 0 && size == fs->fs_bsize))
		goto fail;
	if (size == fs->fs_bsize) {
		UFS_LOCK(ump);
		blkno = ffs_alloccgblk(ip, bp, bpref, rsize);
		ACTIVECLEAR(fs, cg);
		UFS_UNLOCK(ump);
		bdwrite(bp);
		return (blkno);
	}
	/*
	 * check to see if any fragments are already available
	 * allocsiz is the size which will be allocated, hacking
	 * it down to a smaller size if necessary
	 */
	blksfree = cg_blksfree(cgp);
	frags = numfrags(fs, size);
	for (allocsiz = frags; allocsiz < fs->fs_frag; allocsiz++)
		if (cgp->cg_frsum[allocsiz] != 0)
			break;
	if (allocsiz == fs->fs_frag) {
		/*
		 * no fragments were available, so a block will be
		 * allocated, and hacked up
		 */
		if (cgp->cg_cs.cs_nbfree == 0)
			goto fail;
		UFS_LOCK(ump);
		blkno = ffs_alloccgblk(ip, bp, bpref, rsize);
		ACTIVECLEAR(fs, cg);
		UFS_UNLOCK(ump);
		bdwrite(bp);
		return (blkno);
	}
	KASSERT(size == rsize,
	    ("ffs_alloccg: size(%d) != rsize(%d)", size, rsize));
	bno = ffs_mapsearch(fs, cgp, bpref, allocsiz);
	if (bno < 0)
		goto fail;
	for (i = 0; i < frags; i++)
		clrbit(blksfree, bno + i);
	cgp->cg_cs.cs_nffree -= frags;
	cgp->cg_frsum[allocsiz]--;
	if (frags != allocsiz)
		cgp->cg_frsum[allocsiz - frags]++;
	UFS_LOCK(ump);
	fs->fs_cstotal.cs_nffree -= frags;
	fs->fs_cs(fs, cg).cs_nffree -= frags;
	fs->fs_fmod = 1;
	blkno = cgbase(fs, cg) + bno;
	ACTIVECLEAR(fs, cg);
	UFS_UNLOCK(ump);
	if (DOINGSOFTDEP(ITOV(ip)))
		softdep_setup_blkmapdep(bp, UFSTOVFS(ump), blkno, frags, 0);
	bdwrite(bp);
	return (blkno);

fail:
	brelse(bp);
	UFS_LOCK(ump);
	return (0);
}

/*
 * Allocate a block in a cylinder group.
 *
 * This algorithm implements the following policy:
 *   1) allocate the requested block.
 *   2) allocate a rotationally optimal block in the same cylinder.
 *   3) allocate the next available block on the block rotor for the
 *      specified cylinder group.
 * Note that this routine only allocates fs_bsize blocks; these
 * blocks may be fragmented by the routine that allocates them.
 */
static ufs2_daddr_t
ffs_alloccgblk(ip, bp, bpref, size)
	struct inode *ip;
	struct buf *bp;
	ufs2_daddr_t bpref;
	int size;
{
	struct fs *fs;
	struct cg *cgp;
	struct ufsmount *ump;
	ufs1_daddr_t bno;
	ufs2_daddr_t blkno;
	u_int8_t *blksfree;
	int i, cgbpref;

	ump = ITOUMP(ip);
	fs = ump->um_fs;
	mtx_assert(UFS_MTX(ump), MA_OWNED);
	cgp = (struct cg *)bp->b_data;
	blksfree = cg_blksfree(cgp);
	if (bpref == 0) {
		bpref = cgbase(fs, cgp->cg_cgx) + cgp->cg_rotor + fs->fs_frag;
	} else if ((cgbpref = dtog(fs, bpref)) != cgp->cg_cgx) {
		/* map bpref to correct zone in this cg */
		if (bpref < cgdata(fs, cgbpref))
			bpref = cgmeta(fs, cgp->cg_cgx);
		else
			bpref = cgdata(fs, cgp->cg_cgx);
	}
	/*
	 * if the requested block is available, use it
	 */
	bno = dtogd(fs, blknum(fs, bpref));
	if (ffs_isblock(fs, blksfree, fragstoblks(fs, bno)))
		goto gotit;
	/*
	 * Take the next available block in this cylinder group.
	 */
	bno = ffs_mapsearch(fs, cgp, bpref, (int)fs->fs_frag);
	if (bno < 0)
		return (0);
	/* Update cg_rotor only if allocated from the data zone */
	if (bno >= dtogd(fs, cgdata(fs, cgp->cg_cgx)))
		cgp->cg_rotor = bno;
gotit:
	blkno = fragstoblks(fs, bno);
	ffs_clrblock(fs, blksfree, (long)blkno);
	ffs_clusteracct(fs, cgp, blkno, -1);
	cgp->cg_cs.cs_nbfree--;
	fs->fs_cstotal.cs_nbfree--;
	fs->fs_cs(fs, cgp->cg_cgx).cs_nbfree--;
	fs->fs_fmod = 1;
	blkno = cgbase(fs, cgp->cg_cgx) + bno;
	/*
	 * If the caller didn't want the whole block free the frags here.
	 */
	size = numfrags(fs, size);
	if (size != fs->fs_frag) {
		bno = dtogd(fs, blkno);
		for (i = size; i < fs->fs_frag; i++)
			setbit(blksfree, bno + i);
		i = fs->fs_frag - size;
		cgp->cg_cs.cs_nffree += i;
		fs->fs_cstotal.cs_nffree += i;
		fs->fs_cs(fs, cgp->cg_cgx).cs_nffree += i;
		fs->fs_fmod = 1;
		cgp->cg_frsum[i]++;
	}
	/* XXX Fixme. */
	UFS_UNLOCK(ump);
	if (DOINGSOFTDEP(ITOV(ip)))
		softdep_setup_blkmapdep(bp, UFSTOVFS(ump), blkno, size, 0);
	UFS_LOCK(ump);
	return (blkno);
}

/*
 * Determine whether a cluster can be allocated.
 *
 * We do not currently check for optimal rotational layout if there
 * are multiple choices in the same cylinder group. Instead we just
 * take the first one that we find following bpref.
 */
static ufs2_daddr_t
ffs_clusteralloc(ip, cg, bpref, len)
	struct inode *ip;
	u_int cg;
	ufs2_daddr_t bpref;
	int len;
{
	struct fs *fs;
	struct cg *cgp;
	struct buf *bp;
	struct ufsmount *ump;
	int i, run, bit, map, got, error;
	ufs2_daddr_t bno;
	u_char *mapp;
	int32_t *lp;
	u_int8_t *blksfree;

	ump = ITOUMP(ip);
	fs = ump->um_fs;
	if (fs->fs_maxcluster[cg] < len)
		return (0);
	UFS_UNLOCK(ump);
	if ((error = ffs_getcg(fs, ump->um_devvp, cg, &bp, &cgp)) != 0) {
		UFS_LOCK(ump);
		return (0);
	}
	/*
	 * Check to see if a cluster of the needed size (or bigger) is
	 * available in this cylinder group.
	 */
	lp = &cg_clustersum(cgp)[len];
	for (i = len; i <= fs->fs_contigsumsize; i++)
		if (*lp++ > 0)
			break;
	if (i > fs->fs_contigsumsize) {
		/*
		 * This is the first time looking for a cluster in this
		 * cylinder group. Update the cluster summary information
		 * to reflect the true maximum sized cluster so that
		 * future cluster allocation requests can avoid reading
		 * the cylinder group map only to find no clusters.
		 */
		lp = &cg_clustersum(cgp)[len - 1];
		for (i = len - 1; i > 0; i--)
			if (*lp-- > 0)
				break;
		UFS_LOCK(ump);
		fs->fs_maxcluster[cg] = i;
		brelse(bp);
		return (0);
	}
	/*
	 * Search the cluster map to find a big enough cluster.
	 * We take the first one that we find, even if it is larger
	 * than we need as we prefer to get one close to the previous
	 * block allocation. We do not search before the current
	 * preference point as we do not want to allocate a block
	 * that is allocated before the previous one (as we will
	 * then have to wait for another pass of the elevator
	 * algorithm before it will be read). We prefer to fail and
	 * be recalled to try an allocation in the next cylinder group.
	 */
	if (dtog(fs, bpref) != cg)
		bpref = cgdata(fs, cg);
	else
		bpref = blknum(fs, bpref);
	bpref = fragstoblks(fs, dtogd(fs, bpref));
	mapp = &cg_clustersfree(cgp)[bpref / NBBY];
	map = *mapp++;
	bit = 1 << (bpref % NBBY);
	for (run = 0, got = bpref; got < cgp->cg_nclusterblks; got++) {
		if ((map & bit) == 0) {
			run = 0;
		} else {
			run++;
			if (run == len)
				break;
		}
		if ((got & (NBBY - 1)) != (NBBY - 1)) {
			bit <<= 1;
		} else {
			map = *mapp++;
			bit = 1;
		}
	}
	if (got >= cgp->cg_nclusterblks) {
		UFS_LOCK(ump);
		brelse(bp);
		return (0);
	}
	/*
	 * Allocate the cluster that we have found.
	 */
	blksfree = cg_blksfree(cgp);
	for (i = 1; i <= len; i++)
		if (!ffs_isblock(fs, blksfree, got - run + i))
			panic("ffs_clusteralloc: map mismatch");
	bno = cgbase(fs, cg) + blkstofrags(fs, got - run + 1);
	if (dtog(fs, bno) != cg)
		panic("ffs_clusteralloc: allocated out of group");
	len = blkstofrags(fs, len);
	UFS_LOCK(ump);
	for (i = 0; i < len; i += fs->fs_frag)
		if (ffs_alloccgblk(ip, bp, bno + i, fs->fs_bsize) != bno + i)
			panic("ffs_clusteralloc: lost block");
	ACTIVECLEAR(fs, cg);
	UFS_UNLOCK(ump);
	bdwrite(bp);
	return (bno);
}

static inline struct buf *
getinobuf(struct inode *ip, u_int cg, u_int32_t cginoblk, int gbflags)
{
	struct fs *fs;

	fs = ITOFS(ip);
	return (getblk(ITODEVVP(ip), fsbtodb(fs, ino_to_fsba(fs,
	    cg * fs->fs_ipg + cginoblk)), (int)fs->fs_bsize, 0, 0,
	    gbflags));
}

/*
 * Synchronous inode initialization is needed only when barrier writes do not
 * work as advertised, and will impose a heavy cost on file creation in a newly
 * created filesystem.
 */
static int doasyncinodeinit = 1;
SYSCTL_INT(_vfs_ffs, OID_AUTO, doasyncinodeinit, CTLFLAG_RWTUN,
    &doasyncinodeinit, 0,
    "Perform inode block initialization using asynchronous writes");

/*
 * Determine whether an inode can be allocated.
 *
 * Check to see if an inode is available, and if it is,
 * allocate it using the following policy:
 *   1) allocate the requested inode.
 *   2) allocate the next available inode after the requested
 *      inode in the specified cylinder group.
 */
static ufs2_daddr_t
ffs_nodealloccg(ip, cg, ipref, mode, unused)
	struct inode *ip;
	u_int cg;
	ufs2_daddr_t ipref;
	int mode;
	int unused;
{
	struct fs *fs;
	struct cg *cgp;
	struct buf *bp, *ibp;
	struct ufsmount *ump;
	u_int8_t *inosused, *loc;
	struct ufs2_dinode *dp2;
	int error, start, len, i;
	u_int32_t old_initediblk;

	ump = ITOUMP(ip);
	fs = ump->um_fs;
check_nifree:
	if (fs->fs_cs(fs, cg).cs_nifree == 0)
		return (0);
	UFS_UNLOCK(ump);
	if ((error = ffs_getcg(fs, ump->um_devvp, cg, &bp, &cgp)) != 0) {
		UFS_LOCK(ump);
		return (0);
	}
restart:
	if (cgp->cg_cs.cs_nifree == 0) {
		brelse(bp);
		UFS_LOCK(ump);
		return (0);
	}
	inosused = cg_inosused(cgp);
	if (ipref) {
		ipref %= fs->fs_ipg;
		if (isclr(inosused, ipref))
			goto gotit;
	}
	start = cgp->cg_irotor / NBBY;
	len = howmany(fs->fs_ipg - cgp->cg_irotor, NBBY);
	loc = memcchr(&inosused[start], 0xff, len);
	if (loc == NULL) {
		len = start + 1;
		start = 0;
		loc = memcchr(&inosused[start], 0xff, len);
		if (loc == NULL) {
			printf("cg = %d, irotor = %ld, fs = %s\n",
			    cg, (long)cgp->cg_irotor, fs->fs_fsmnt);
			panic("ffs_nodealloccg: map corrupted");
			/* NOTREACHED */
		}
	}
	ipref = (loc - inosused) * NBBY + ffs(~*loc) - 1;
gotit:
	/*
	 * Check to see if we need to initialize more inodes.
	 */
	if (fs->fs_magic == FS_UFS2_MAGIC &&
	    ipref + INOPB(fs) > cgp->cg_initediblk &&
	    cgp->cg_initediblk < cgp->cg_niblk) {
		old_initediblk = cgp->cg_initediblk;

		/*
		 * Free the cylinder group lock before writing the
		 * initialized inode block.  Entering the
		 * babarrierwrite() with the cylinder group lock
		 * causes lock order violation between the lock and
		 * snaplk.
		 *
		 * Another thread can decide to initialize the same
		 * inode block, but whichever thread first gets the
		 * cylinder group lock after writing the newly
		 * allocated inode block will update it and the other
		 * will realize that it has lost and leave the
		 * cylinder group unchanged.
		 */
		ibp = getinobuf(ip, cg, old_initediblk, GB_LOCK_NOWAIT);
		brelse(bp);
		if (ibp == NULL) {
			/*
			 * The inode block buffer is already owned by
			 * another thread, which must initialize it.
			 * Wait on the buffer to allow another thread
			 * to finish the updates, with dropped cg
			 * buffer lock, then retry.
			 */
			ibp = getinobuf(ip, cg, old_initediblk, 0);
			brelse(ibp);
			UFS_LOCK(ump);
			goto check_nifree;
		}
		bzero(ibp->b_data, (int)fs->fs_bsize);
		dp2 = (struct ufs2_dinode *)(ibp->b_data);
		for (i = 0; i < INOPB(fs); i++) {
			while (dp2->di_gen == 0)
				dp2->di_gen = arc4random();
			dp2++;
		}

		/*
		 * Rather than adding a soft updates dependency to ensure
		 * that the new inode block is written before it is claimed
		 * by the cylinder group map, we just do a barrier write
		 * here. The barrier write will ensure that the inode block
		 * gets written before the updated cylinder group map can be
		 * written. The barrier write should only slow down bulk
		 * loading of newly created filesystems.
		 */
		if (doasyncinodeinit)
			babarrierwrite(ibp);
		else
			bwrite(ibp);

		/*
		 * After the inode block is written, try to update the
		 * cg initediblk pointer.  If another thread beat us
		 * to it, then leave it unchanged as the other thread
		 * has already set it correctly.
		 */
		error = ffs_getcg(fs, ump->um_devvp, cg, &bp, &cgp);
		UFS_LOCK(ump);
		ACTIVECLEAR(fs, cg);
		UFS_UNLOCK(ump);
		if (error != 0)
			return (error);
		if (cgp->cg_initediblk == old_initediblk)
			cgp->cg_initediblk += INOPB(fs);
		goto restart;
	}
	cgp->cg_irotor = ipref;
	UFS_LOCK(ump);
	ACTIVECLEAR(fs, cg);
	setbit(inosused, ipref);
	cgp->cg_cs.cs_nifree--;
	fs->fs_cstotal.cs_nifree--;
	fs->fs_cs(fs, cg).cs_nifree--;
	fs->fs_fmod = 1;
	if ((mode & IFMT) == IFDIR) {
		cgp->cg_cs.cs_ndir++;
		fs->fs_cstotal.cs_ndir++;
		fs->fs_cs(fs, cg).cs_ndir++;
	}
	UFS_UNLOCK(ump);
	if (DOINGSOFTDEP(ITOV(ip)))
		softdep_setup_inomapdep(bp, ip, cg * fs->fs_ipg + ipref, mode);
	bdwrite(bp);
	return ((ino_t)(cg * fs->fs_ipg + ipref));
}

/*
 * Free a block or fragment.
 *
 * The specified block or fragment is placed back in the
 * free map. If a fragment is deallocated, a possible
 * block reassembly is checked.
 */
static void
ffs_blkfree_cg(ump, fs, devvp, bno, size, inum, dephd)
	struct ufsmount *ump;
	struct fs *fs;
	struct vnode *devvp;
	ufs2_daddr_t bno;
	long size;
	ino_t inum;
	struct workhead *dephd;
{
	struct mount *mp;
	struct cg *cgp;
	struct buf *bp;
	ufs1_daddr_t fragno, cgbno;
	int i, blk, frags, bbase, error;
	u_int cg;
	u_int8_t *blksfree;
	struct cdev *dev;

	cg = dtog(fs, bno);
	if (devvp->v_type == VREG) {
		/* devvp is a snapshot */
		MPASS(devvp->v_mount->mnt_data == ump);
		dev = ump->um_devvp->v_rdev;
	} else if (devvp->v_type == VCHR) {
		/* devvp is a normal disk device */
		dev = devvp->v_rdev;
		ASSERT_VOP_LOCKED(devvp, "ffs_blkfree_cg");
	} else
		return;
#ifdef INVARIANTS
	if ((u_int)size > fs->fs_bsize || fragoff(fs, size) != 0 ||
	    fragnum(fs, bno) + numfrags(fs, size) > fs->fs_frag) {
		printf("dev=%s, bno = %jd, bsize = %ld, size = %ld, fs = %s\n",
		    devtoname(dev), (intmax_t)bno, (long)fs->fs_bsize,
		    size, fs->fs_fsmnt);
		panic("ffs_blkfree_cg: bad size");
	}
#endif
	if ((u_int)bno >= fs->fs_size) {
		printf("bad block %jd, ino %lu\n", (intmax_t)bno,
		    (u_long)inum);
		ffs_fserr(fs, inum, "bad block");
		return;
	}
	if ((error = ffs_getcg(fs, devvp, cg, &bp, &cgp)) != 0)
		return;
	cgbno = dtogd(fs, bno);
	blksfree = cg_blksfree(cgp);
	UFS_LOCK(ump);
	if (size == fs->fs_bsize) {
		fragno = fragstoblks(fs, cgbno);
		if (!ffs_isfreeblock(fs, blksfree, fragno)) {
			if (devvp->v_type == VREG) {
				UFS_UNLOCK(ump);
				/* devvp is a snapshot */
				brelse(bp);
				return;
			}
			printf("dev = %s, block = %jd, fs = %s\n",
			    devtoname(dev), (intmax_t)bno, fs->fs_fsmnt);
			panic("ffs_blkfree_cg: freeing free block");
		}
		ffs_setblock(fs, blksfree, fragno);
		ffs_clusteracct(fs, cgp, fragno, 1);
		cgp->cg_cs.cs_nbfree++;
		fs->fs_cstotal.cs_nbfree++;
		fs->fs_cs(fs, cg).cs_nbfree++;
	} else {
		bbase = cgbno - fragnum(fs, cgbno);
		/*
		 * decrement the counts associated with the old frags
		 */
		blk = blkmap(fs, blksfree, bbase);
		ffs_fragacct(fs, blk, cgp->cg_frsum, -1);
		/*
		 * deallocate the fragment
		 */
		frags = numfrags(fs, size);
		for (i = 0; i < frags; i++) {
			if (isset(blksfree, cgbno + i)) {
				printf("dev = %s, block = %jd, fs = %s\n",
				    devtoname(dev), (intmax_t)(bno + i),
				    fs->fs_fsmnt);
				panic("ffs_blkfree_cg: freeing free frag");
			}
			setbit(blksfree, cgbno + i);
		}
		cgp->cg_cs.cs_nffree += i;
		fs->fs_cstotal.cs_nffree += i;
		fs->fs_cs(fs, cg).cs_nffree += i;
		/*
		 * add back in counts associated with the new frags
		 */
		blk = blkmap(fs, blksfree, bbase);
		ffs_fragacct(fs, blk, cgp->cg_frsum, 1);
		/*
		 * if a complete block has been reassembled, account for it
		 */
		fragno = fragstoblks(fs, bbase);
		if (ffs_isblock(fs, blksfree, fragno)) {
			cgp->cg_cs.cs_nffree -= fs->fs_frag;
			fs->fs_cstotal.cs_nffree -= fs->fs_frag;
			fs->fs_cs(fs, cg).cs_nffree -= fs->fs_frag;
			ffs_clusteracct(fs, cgp, fragno, 1);
			cgp->cg_cs.cs_nbfree++;
			fs->fs_cstotal.cs_nbfree++;
			fs->fs_cs(fs, cg).cs_nbfree++;
		}
	}
	fs->fs_fmod = 1;
	ACTIVECLEAR(fs, cg);
	UFS_UNLOCK(ump);
	mp = UFSTOVFS(ump);
	if (MOUNTEDSOFTDEP(mp) && devvp->v_type == VCHR)
		softdep_setup_blkfree(UFSTOVFS(ump), bp, bno,
		    numfrags(fs, size), dephd);
	bdwrite(bp);
}

/*
 * Structures and routines associated with trim management.
 *
 * The following requests are passed to trim_lookup to indicate
 * the actions that should be taken.
 */
#define	NEW	1	/* if found, error else allocate and hash it */
#define	OLD	2	/* if not found, error, else return it */
#define	REPLACE	3	/* if not found, error else unhash and reallocate it */
#define	DONE	4	/* if not found, error else unhash and return it */
#define	SINGLE	5	/* don't look up, just allocate it and don't hash it */

MALLOC_DEFINE(M_TRIM, "ufs_trim", "UFS trim structures");

#define	TRIMLIST_HASH(ump, key) \
	(&(ump)->um_trimhash[(key) & (ump)->um_trimlisthashsize])

/*
 * These structures describe each of the block free requests aggregated
 * together to make up a trim request.
 */
struct trim_blkreq {
	TAILQ_ENTRY(trim_blkreq) blkreqlist;
	ufs2_daddr_t bno;
	long size;
	struct workhead *pdephd;
	struct workhead dephd;
};

/*
 * Description of a trim request.
 */
struct ffs_blkfree_trim_params {
	TAILQ_HEAD(, trim_blkreq) blklist;
	LIST_ENTRY(ffs_blkfree_trim_params) hashlist;
	struct task task;
	struct ufsmount *ump;
	struct vnode *devvp;
	ino_t inum;
	ufs2_daddr_t bno;
	long size;
	long key;
};

static void	ffs_blkfree_trim_completed(struct buf *);
static void	ffs_blkfree_trim_task(void *ctx, int pending __unused);
static struct	ffs_blkfree_trim_params *trim_lookup(struct ufsmount *,
		    struct vnode *, ufs2_daddr_t, long, ino_t, u_long, int);
static void	ffs_blkfree_sendtrim(struct ffs_blkfree_trim_params *);

/*
 * Called on trim completion to start a task to free the associated block(s).
 */
static void
ffs_blkfree_trim_completed(bp)
	struct buf *bp;
{
	struct ffs_blkfree_trim_params *tp;

	tp = bp->b_fsprivate1;
	free(bp, M_TRIM);
	TASK_INIT(&tp->task, 0, ffs_blkfree_trim_task, tp);
	taskqueue_enqueue(tp->ump->um_trim_tq, &tp->task);
}

/*
 * Trim completion task that free associated block(s).
 */
static void
ffs_blkfree_trim_task(ctx, pending)
	void *ctx;
	int pending;
{
	struct ffs_blkfree_trim_params *tp;
	struct trim_blkreq *blkelm;
	struct ufsmount *ump;

	tp = ctx;
	ump = tp->ump;
	while ((blkelm = TAILQ_FIRST(&tp->blklist)) != NULL) {
		ffs_blkfree_cg(ump, ump->um_fs, tp->devvp, blkelm->bno,
		    blkelm->size, tp->inum, blkelm->pdephd);
		TAILQ_REMOVE(&tp->blklist, blkelm, blkreqlist);
		free(blkelm, M_TRIM);
	}
	vn_finished_secondary_write(UFSTOVFS(ump));
	UFS_LOCK(ump);
	ump->um_trim_inflight -= 1;
	ump->um_trim_inflight_blks -= numfrags(ump->um_fs, tp->size);
	UFS_UNLOCK(ump);
	free(tp, M_TRIM);
}

/*
 * Lookup a trim request by inode number.
 * Allocate if requested (NEW, REPLACE, SINGLE).
 */
static struct ffs_blkfree_trim_params *
trim_lookup(ump, devvp, bno, size, inum, key, alloctype)
	struct ufsmount *ump;
	struct vnode *devvp;
	ufs2_daddr_t bno;
	long size;
	ino_t inum;
	u_long key;
	int alloctype;
{
	struct trimlist_hashhead *tphashhead;
	struct ffs_blkfree_trim_params *tp, *ntp;

	ntp = malloc(sizeof(struct ffs_blkfree_trim_params), M_TRIM, M_WAITOK);
	if (alloctype != SINGLE) {
		KASSERT(key >= FIRST_VALID_KEY, ("trim_lookup: invalid key"));
		UFS_LOCK(ump);
		tphashhead = TRIMLIST_HASH(ump, key);
		LIST_FOREACH(tp, tphashhead, hashlist)
			if (key == tp->key)
				break;
	}
	switch (alloctype) {
	case NEW:
		KASSERT(tp == NULL, ("trim_lookup: found trim"));
		break;
	case OLD:
		KASSERT(tp != NULL,
		    ("trim_lookup: missing call to ffs_blkrelease_start()"));
		UFS_UNLOCK(ump);
		free(ntp, M_TRIM);
		return (tp);
	case REPLACE:
		KASSERT(tp != NULL, ("trim_lookup: missing REPLACE trim"));
		LIST_REMOVE(tp, hashlist);
		/* tp will be freed by caller */
		break;
	case DONE:
		KASSERT(tp != NULL, ("trim_lookup: missing DONE trim"));
		LIST_REMOVE(tp, hashlist);
		UFS_UNLOCK(ump);
		free(ntp, M_TRIM);
		return (tp);
	}
	TAILQ_INIT(&ntp->blklist);
	ntp->ump = ump;
	ntp->devvp = devvp;
	ntp->bno = bno;
	ntp->size = size;
	ntp->inum = inum;
	ntp->key = key;
	if (alloctype != SINGLE) {
		LIST_INSERT_HEAD(tphashhead, ntp, hashlist);
		UFS_UNLOCK(ump);
	}
	return (ntp);
}

/*
 * Dispatch a trim request.
 */
static void
ffs_blkfree_sendtrim(tp)
	struct ffs_blkfree_trim_params *tp;
{
	struct ufsmount *ump;
	struct mount *mp;
	struct buf *bp;

	/*
	 * Postpone the set of the free bit in the cg bitmap until the
	 * BIO_DELETE is completed.  Otherwise, due to disk queue
	 * reordering, TRIM might be issued after we reuse the block
	 * and write some new data into it.
	 */
	ump = tp->ump;
	bp = malloc(sizeof(*bp), M_TRIM, M_WAITOK | M_ZERO);
	bp->b_iocmd = BIO_DELETE;
	bp->b_iooffset = dbtob(fsbtodb(ump->um_fs, tp->bno));
	bp->b_iodone = ffs_blkfree_trim_completed;
	bp->b_bcount = tp->size;
	bp->b_fsprivate1 = tp;
	UFS_LOCK(ump);
	ump->um_trim_total += 1;
	ump->um_trim_inflight += 1;
	ump->um_trim_inflight_blks += numfrags(ump->um_fs, tp->size);
	ump->um_trim_total_blks += numfrags(ump->um_fs, tp->size);
	UFS_UNLOCK(ump);

	mp = UFSTOVFS(ump);
	vn_start_secondary_write(NULL, &mp, 0);
	g_vfs_strategy(ump->um_bo, bp);
}

/*
 * Allocate a new key to use to identify a range of blocks.
 */
u_long
ffs_blkrelease_start(ump, devvp, inum)
	struct ufsmount *ump;
	struct vnode *devvp;
	ino_t inum;
{
	static u_long masterkey;
	u_long key;

	if (((ump->um_flags & UM_CANDELETE) == 0) || dotrimcons == 0)
		return (SINGLETON_KEY);
	do {
		key = atomic_fetchadd_long(&masterkey, 1);
	} while (key < FIRST_VALID_KEY);
	(void) trim_lookup(ump, devvp, 0, 0, inum, key, NEW);
	return (key);
}

/*
 * Deallocate a key that has been used to identify a range of blocks.
 */
void
ffs_blkrelease_finish(ump, key)
	struct ufsmount *ump;
	u_long key;
{
	struct ffs_blkfree_trim_params *tp;

	if (((ump->um_flags & UM_CANDELETE) == 0) || dotrimcons == 0)
		return;
	/*
	 * If the vfs.ffs.dotrimcons sysctl option is enabled while
	 * a file deletion is active, specifically after a call
	 * to ffs_blkrelease_start() but before the call to
	 * ffs_blkrelease_finish(), ffs_blkrelease_start() will
	 * have handed out SINGLETON_KEY rather than starting a
	 * collection sequence. Thus if we get a SINGLETON_KEY
	 * passed to ffs_blkrelease_finish(), we just return rather
	 * than trying to finish the nonexistent sequence.
	 */
	if (key == SINGLETON_KEY) {
#ifdef INVARIANTS
		printf("%s: vfs.ffs.dotrimcons enabled on active filesystem\n",
		    ump->um_mountp->mnt_stat.f_mntonname);
#endif
		return;
	}
	/*
	 * We are done with sending blocks using this key. Look up the key
	 * using the DONE alloctype (in tp) to request that it be unhashed
	 * as we will not be adding to it. If the key has never been used,
	 * tp->size will be zero, so we can just free tp. Otherwise the call
	 * to ffs_blkfree_sendtrim(tp) causes the block range described by
	 * tp to be issued (and then tp to be freed).
	 */
	tp = trim_lookup(ump, NULL, 0, 0, 0, key, DONE);
	if (tp->size == 0)
		free(tp, M_TRIM);
	else
		ffs_blkfree_sendtrim(tp);
}

/*
 * Setup to free a block or fragment.
 *
 * Check for snapshots that might want to claim the block.
 * If trims are requested, prepare a trim request. Attempt to
 * aggregate consecutive blocks into a single trim request.
 */
void
ffs_blkfree(ump, fs, devvp, bno, size, inum, vtype, dephd, key)
	struct ufsmount *ump;
	struct fs *fs;
	struct vnode *devvp;
	ufs2_daddr_t bno;
	long size;
	ino_t inum;
	enum vtype vtype;
	struct workhead *dephd;
	u_long key;
{
	struct ffs_blkfree_trim_params *tp, *ntp;
	struct trim_blkreq *blkelm;

	/*
	 * Check to see if a snapshot wants to claim the block.
	 * Check that devvp is a normal disk device, not a snapshot,
	 * it has a snapshot(s) associated with it, and one of the
	 * snapshots wants to claim the block.
	 */
	if (devvp->v_type == VCHR &&
	    (devvp->v_vflag & VV_COPYONWRITE) &&
	    ffs_snapblkfree(fs, devvp, bno, size, inum, vtype, dephd)) {
		return;
	}
	/*
	 * Nothing to delay if TRIM is not required for this block or TRIM
	 * is disabled or the operation is performed on a snapshot.
	 */
	if (key == NOTRIM_KEY || ((ump->um_flags & UM_CANDELETE) == 0) ||
	    devvp->v_type == VREG) {
		ffs_blkfree_cg(ump, fs, devvp, bno, size, inum, dephd);
		return;
	}
	blkelm = malloc(sizeof(struct trim_blkreq), M_TRIM, M_WAITOK);
	blkelm->bno = bno;
	blkelm->size = size;
	if (dephd == NULL) {
		blkelm->pdephd = NULL;
	} else {
		LIST_INIT(&blkelm->dephd);
		LIST_SWAP(dephd, &blkelm->dephd, worklist, wk_list);
		blkelm->pdephd = &blkelm->dephd;
	}
	if (key == SINGLETON_KEY) {
		/*
		 * Just a single non-contiguous piece. Use the SINGLE
		 * alloctype to return a trim request that will not be
		 * hashed for future lookup.
		 */
		tp = trim_lookup(ump, devvp, bno, size, inum, key, SINGLE);
		TAILQ_INSERT_HEAD(&tp->blklist, blkelm, blkreqlist);
		ffs_blkfree_sendtrim(tp);
		return;
	}
	/*
	 * The callers of this function are not tracking whether or not
	 * the blocks are contiguous. They are just saying that they
	 * are freeing a set of blocks. It is this code that determines
	 * the pieces of that range that are actually contiguous.
	 *
	 * Calling ffs_blkrelease_start() will have created an entry
	 * that we will use.
	 */
	tp = trim_lookup(ump, devvp, bno, size, inum, key, OLD);
	if (tp->size == 0) {
		/*
		 * First block of a potential range, set block and size
		 * for the trim block.
		 */
		tp->bno = bno;
		tp->size = size;
		TAILQ_INSERT_HEAD(&tp->blklist, blkelm, blkreqlist);
		return;
	}
	/*
	 * If this block is a continuation of the range (either
	 * follows at the end or preceeds in the front) then we
	 * add it to the front or back of the list and return.
	 *
	 * If it is not a continuation of the trim that we were
	 * building, using the REPLACE alloctype, we request that
	 * the old trim request (still in tp) be unhashed and a
	 * new range started (in ntp). The ffs_blkfree_sendtrim(tp)
	 * call causes the block range described by tp to be issued
	 * (and then tp to be freed).
	 */
	if (bno + numfrags(fs, size) == tp->bno) {
		TAILQ_INSERT_HEAD(&tp->blklist, blkelm, blkreqlist);
		tp->bno = bno;
		tp->size += size;
		return;
	} else if (bno == tp->bno + numfrags(fs, tp->size)) {
		TAILQ_INSERT_TAIL(&tp->blklist, blkelm, blkreqlist);
		tp->size += size;
		return;
	}
	ntp = trim_lookup(ump, devvp, bno, size, inum, key, REPLACE);
	TAILQ_INSERT_HEAD(&ntp->blklist, blkelm, blkreqlist);
	ffs_blkfree_sendtrim(tp);
}

#ifdef INVARIANTS
/*
 * Verify allocation of a block or fragment. Returns true if block or
 * fragment is allocated, false if it is free.
 */
static int
ffs_checkblk(ip, bno, size)
	struct inode *ip;
	ufs2_daddr_t bno;
	long size;
{
	struct fs *fs;
	struct cg *cgp;
	struct buf *bp;
	ufs1_daddr_t cgbno;
	int i, error, frags, free;
	u_int8_t *blksfree;

	fs = ITOFS(ip);
	if ((u_int)size > fs->fs_bsize || fragoff(fs, size) != 0) {
		printf("bsize = %ld, size = %ld, fs = %s\n",
		    (long)fs->fs_bsize, size, fs->fs_fsmnt);
		panic("ffs_checkblk: bad size");
	}
	if ((u_int)bno >= fs->fs_size)
		panic("ffs_checkblk: bad block %jd", (intmax_t)bno);
	error = ffs_getcg(fs, ITODEVVP(ip), dtog(fs, bno), &bp, &cgp);
	if (error)
		panic("ffs_checkblk: cylinder group read failed");
	blksfree = cg_blksfree(cgp);
	cgbno = dtogd(fs, bno);
	if (size == fs->fs_bsize) {
		free = ffs_isblock(fs, blksfree, fragstoblks(fs, cgbno));
	} else {
		frags = numfrags(fs, size);
		for (free = 0, i = 0; i < frags; i++)
			if (isset(blksfree, cgbno + i))
				free++;
		if (free != 0 && free != frags)
			panic("ffs_checkblk: partially free fragment");
	}
	brelse(bp);
	return (!free);
}
#endif /* INVARIANTS */

/*
 * Free an inode.
 */
int
ffs_vfree(pvp, ino, mode)
	struct vnode *pvp;
	ino_t ino;
	int mode;
{
	struct ufsmount *ump;

	if (DOINGSOFTDEP(pvp)) {
		softdep_freefile(pvp, ino, mode);
		return (0);
	}
	ump = VFSTOUFS(pvp->v_mount);
	return (ffs_freefile(ump, ump->um_fs, ump->um_devvp, ino, mode, NULL));
}

/*
 * Do the actual free operation.
 * The specified inode is placed back in the free map.
 */
int
ffs_freefile(ump, fs, devvp, ino, mode, wkhd)
	struct ufsmount *ump;
	struct fs *fs;
	struct vnode *devvp;
	ino_t ino;
	int mode;
	struct workhead *wkhd;
{
	struct cg *cgp;
	struct buf *bp;
	int error;
	u_int cg;
	u_int8_t *inosused;
	struct cdev *dev;

	cg = ino_to_cg(fs, ino);
	if (devvp->v_type == VREG) {
		/* devvp is a snapshot */
		MPASS(devvp->v_mount->mnt_data == ump);
		dev = ump->um_devvp->v_rdev;
	} else if (devvp->v_type == VCHR) {
		/* devvp is a normal disk device */
		dev = devvp->v_rdev;
	} else {
		bp = NULL;
		return (0);
	}
	if (ino >= fs->fs_ipg * fs->fs_ncg)
		panic("ffs_freefile: range: dev = %s, ino = %ju, fs = %s",
		    devtoname(dev), (uintmax_t)ino, fs->fs_fsmnt);
	if ((error = ffs_getcg(fs, devvp, cg, &bp, &cgp)) != 0)
		return (error);
	inosused = cg_inosused(cgp);
	ino %= fs->fs_ipg;
	if (isclr(inosused, ino)) {
		printf("dev = %s, ino = %ju, fs = %s\n", devtoname(dev),
		    (uintmax_t)(ino + cg * fs->fs_ipg), fs->fs_fsmnt);
		if (fs->fs_ronly == 0)
			panic("ffs_freefile: freeing free inode");
	}
	clrbit(inosused, ino);
	if (ino < cgp->cg_irotor)
		cgp->cg_irotor = ino;
	cgp->cg_cs.cs_nifree++;
	UFS_LOCK(ump);
	fs->fs_cstotal.cs_nifree++;
	fs->fs_cs(fs, cg).cs_nifree++;
	if ((mode & IFMT) == IFDIR) {
		cgp->cg_cs.cs_ndir--;
		fs->fs_cstotal.cs_ndir--;
		fs->fs_cs(fs, cg).cs_ndir--;
	}
	fs->fs_fmod = 1;
	ACTIVECLEAR(fs, cg);
	UFS_UNLOCK(ump);
	if (MOUNTEDSOFTDEP(UFSTOVFS(ump)) && devvp->v_type == VCHR)
		softdep_setup_inofree(UFSTOVFS(ump), bp,
		    ino + cg * fs->fs_ipg, wkhd);
	bdwrite(bp);
	return (0);
}

/*
 * Check to see if a file is free.
 * Used to check for allocated files in snapshots.
 */
int
ffs_checkfreefile(fs, devvp, ino)
	struct fs *fs;
	struct vnode *devvp;
	ino_t ino;
{
	struct cg *cgp;
	struct buf *bp;
	int ret, error;
	u_int cg;
	u_int8_t *inosused;

	cg = ino_to_cg(fs, ino);
	if ((devvp->v_type != VREG) && (devvp->v_type != VCHR))
		return (1);
	if (ino >= fs->fs_ipg * fs->fs_ncg)
		return (1);
	if ((error = ffs_getcg(fs, devvp, cg, &bp, &cgp)) != 0)
		return (1);
	inosused = cg_inosused(cgp);
	ino %= fs->fs_ipg;
	ret = isclr(inosused, ino);
	brelse(bp);
	return (ret);
}

/*
 * Find a block of the specified size in the specified cylinder group.
 *
 * It is a panic if a request is made to find a block if none are
 * available.
 */
static ufs1_daddr_t
ffs_mapsearch(fs, cgp, bpref, allocsiz)
	struct fs *fs;
	struct cg *cgp;
	ufs2_daddr_t bpref;
	int allocsiz;
{
	ufs1_daddr_t bno;
	int start, len, loc, i;
	int blk, field, subfield, pos;
	u_int8_t *blksfree;

	/*
	 * find the fragment by searching through the free block
	 * map for an appropriate bit pattern
	 */
	if (bpref)
		start = dtogd(fs, bpref) / NBBY;
	else
		start = cgp->cg_frotor / NBBY;
	blksfree = cg_blksfree(cgp);
	len = howmany(fs->fs_fpg, NBBY) - start;
	loc = scanc((u_int)len, (u_char *)&blksfree[start],
		fragtbl[fs->fs_frag],
		(u_char)(1 << (allocsiz - 1 + (fs->fs_frag % NBBY))));
	if (loc == 0) {
		len = start + 1;
		start = 0;
		loc = scanc((u_int)len, (u_char *)&blksfree[0],
			fragtbl[fs->fs_frag],
			(u_char)(1 << (allocsiz - 1 + (fs->fs_frag % NBBY))));
		if (loc == 0) {
			printf("start = %d, len = %d, fs = %s\n",
			    start, len, fs->fs_fsmnt);
			panic("ffs_alloccg: map corrupted");
			/* NOTREACHED */
		}
	}
	bno = (start + len - loc) * NBBY;
	cgp->cg_frotor = bno;
	/*
	 * found the byte in the map
	 * sift through the bits to find the selected frag
	 */
	for (i = bno + NBBY; bno < i; bno += fs->fs_frag) {
		blk = blkmap(fs, blksfree, bno);
		blk <<= 1;
		field = around[allocsiz];
		subfield = inside[allocsiz];
		for (pos = 0; pos <= fs->fs_frag - allocsiz; pos++) {
			if ((blk & field) == subfield)
				return (bno + pos);
			field <<= 1;
			subfield <<= 1;
		}
	}
	printf("bno = %lu, fs = %s\n", (u_long)bno, fs->fs_fsmnt);
	panic("ffs_alloccg: block not in map");
	return (-1);
}

static const struct statfs *
ffs_getmntstat(struct vnode *devvp)
{

	if (devvp->v_type == VCHR)
		return (&devvp->v_rdev->si_mountpt->mnt_stat);
	return (ffs_getmntstat(VFSTOUFS(devvp->v_mount)->um_devvp));
}

/*
 * Fetch and verify a cylinder group.
 */
int
ffs_getcg(fs, devvp, cg, bpp, cgpp)
	struct fs *fs;
	struct vnode *devvp;
	u_int cg;
	struct buf **bpp;
	struct cg **cgpp;
{
	struct buf *bp;
	struct cg *cgp;
	const struct statfs *sfs;
	int flags, error;

	*bpp = NULL;
	*cgpp = NULL;
	flags = 0;
	if ((fs->fs_metackhash & CK_CYLGRP) != 0)
		flags |= GB_CKHASH;
	error = breadn_flags(devvp, devvp->v_type == VREG ?
	    fragstoblks(fs, cgtod(fs, cg)) : fsbtodb(fs, cgtod(fs, cg)),
	    (int)fs->fs_cgsize, NULL, NULL, 0, NOCRED, flags,
	    ffs_ckhash_cg, &bp);
	if (error != 0)
		return (error);
	cgp = (struct cg *)bp->b_data;
	if ((fs->fs_metackhash & CK_CYLGRP) != 0 &&
	    (bp->b_flags & B_CKHASH) != 0 &&
	    cgp->cg_ckhash != bp->b_ckhash) {
		sfs = ffs_getmntstat(devvp);
		printf("UFS %s%s (%s) cylinder checksum failed: cg %u, cgp: "
		    "0x%x != bp: 0x%jx\n",
		    devvp->v_type == VCHR ? "" : "snapshot of ",
		    sfs->f_mntfromname, sfs->f_mntonname,
		    cg, cgp->cg_ckhash, (uintmax_t)bp->b_ckhash);
		bp->b_flags &= ~B_CKHASH;
		bp->b_flags |= B_INVAL | B_NOCACHE;
		brelse(bp);
		return (EIO);
	}
	if (!cg_chkmagic(cgp) || cgp->cg_cgx != cg) {
		sfs = ffs_getmntstat(devvp);
		printf("UFS %s%s (%s)",
		    devvp->v_type == VCHR ? "" : "snapshot of ",
		    sfs->f_mntfromname, sfs->f_mntonname);
		if (!cg_chkmagic(cgp))
			printf(" cg %u: bad magic number 0x%x should be 0x%x\n",
			    cg, cgp->cg_magic, CG_MAGIC);
		else
			printf(": wrong cylinder group cg %u != cgx %u\n", cg,
			    cgp->cg_cgx);
		bp->b_flags &= ~B_CKHASH;
		bp->b_flags |= B_INVAL | B_NOCACHE;
		brelse(bp);
		return (EIO);
	}
	bp->b_flags &= ~B_CKHASH;
	bp->b_xflags |= BX_BKGRDWRITE;
	/*
	 * If we are using check hashes on the cylinder group then we want
	 * to limit changing the cylinder group time to when we are actually
	 * going to write it to disk so that its check hash remains correct
	 * in memory. If the CK_CYLGRP flag is set the time is updated in
	 * ffs_bufwrite() as the buffer is queued for writing. Otherwise we
	 * update the time here as we have done historically.
	 */
	if ((fs->fs_metackhash & CK_CYLGRP) != 0)
		bp->b_xflags |= BX_CYLGRP;
	else
		cgp->cg_old_time = cgp->cg_time = time_second;
	*bpp = bp;
	*cgpp = cgp;
	return (0);
}

static void
ffs_ckhash_cg(bp)
	struct buf *bp;
{
	uint32_t ckhash;
	struct cg *cgp;

	cgp = (struct cg *)bp->b_data;
	ckhash = cgp->cg_ckhash;
	cgp->cg_ckhash = 0;
	bp->b_ckhash = calculate_crc32c(~0L, bp->b_data, bp->b_bcount);
	cgp->cg_ckhash = ckhash;
}

/*
 * Fserr prints the name of a filesystem with an error diagnostic.
 *
 * The form of the error message is:
 *	fs: error message
 */
void
ffs_fserr(fs, inum, cp)
	struct fs *fs;
	ino_t inum;
	char *cp;
{
	struct thread *td = curthread;	/* XXX */
	struct proc *p = td->td_proc;

	log(LOG_ERR, "pid %d (%s), uid %d inumber %ju on %s: %s\n",
	    p->p_pid, p->p_comm, td->td_ucred->cr_uid, (uintmax_t)inum,
	    fs->fs_fsmnt, cp);
}

/*
 * This function provides the capability for the fsck program to
 * update an active filesystem. Fourteen operations are provided:
 *
 * adjrefcnt(inode, amt) - adjusts the reference count on the
 *	specified inode by the specified amount. Under normal
 *	operation the count should always go down. Decrementing
 *	the count to zero will cause the inode to be freed.
 * adjblkcnt(inode, amt) - adjust the number of blocks used by the
 *	inode by the specified amount.
 * adjsize(inode, size) - set the size of the inode to the
 *	specified size.
 * adjndir, adjbfree, adjifree, adjffree, adjnumclusters(amt) -
 *	adjust the superblock summary.
 * freedirs(inode, count) - directory inodes [inode..inode + count - 1]
 *	are marked as free. Inodes should never have to be marked
 *	as in use.
 * freefiles(inode, count) - file inodes [inode..inode + count - 1]
 *	are marked as free. Inodes should never have to be marked
 *	as in use.
 * freeblks(blockno, size) - blocks [blockno..blockno + size - 1]
 *	are marked as free. Blocks should never have to be marked
 *	as in use.
 * setflags(flags, set/clear) - the fs_flags field has the specified
 *	flags set (second parameter +1) or cleared (second parameter -1).
 * setcwd(dirinode) - set the current directory to dirinode in the
 *	filesystem associated with the snapshot.
 * setdotdot(oldvalue, newvalue) - Verify that the inode number for ".."
 *	in the current directory is oldvalue then change it to newvalue.
 * unlink(nameptr, oldvalue) - Verify that the inode number associated
 *	with nameptr in the current directory is oldvalue then unlink it.
 *
 * The following functions may only be used on a quiescent filesystem
 * by the soft updates journal. They are not safe to be run on an active
 * filesystem.
 *
 * setinode(inode, dip) - the specified disk inode is replaced with the
 *	contents pointed to by dip.
 * setbufoutput(fd, flags) - output associated with the specified file
 *	descriptor (which must reference the character device supporting
 *	the filesystem) switches from using physio to running through the
 *	buffer cache when flags is set to 1. The descriptor reverts to
 *	physio for output when flags is set to zero.
 */

static int sysctl_ffs_fsck(SYSCTL_HANDLER_ARGS);

SYSCTL_PROC(_vfs_ffs, FFS_ADJ_REFCNT, adjrefcnt, CTLFLAG_WR|CTLTYPE_STRUCT,
	0, 0, sysctl_ffs_fsck, "S,fsck", "Adjust Inode Reference Count");

static SYSCTL_NODE(_vfs_ffs, FFS_ADJ_BLKCNT, adjblkcnt, CTLFLAG_WR,
	sysctl_ffs_fsck, "Adjust Inode Used Blocks Count");

static SYSCTL_NODE(_vfs_ffs, FFS_SET_SIZE, setsize, CTLFLAG_WR,
	sysctl_ffs_fsck, "Set the inode size");

static SYSCTL_NODE(_vfs_ffs, FFS_ADJ_NDIR, adjndir, CTLFLAG_WR,
	sysctl_ffs_fsck, "Adjust number of directories");

static SYSCTL_NODE(_vfs_ffs, FFS_ADJ_NBFREE, adjnbfree, CTLFLAG_WR,
	sysctl_ffs_fsck, "Adjust number of free blocks");

static SYSCTL_NODE(_vfs_ffs, FFS_ADJ_NIFREE, adjnifree, CTLFLAG_WR,
	sysctl_ffs_fsck, "Adjust number of free inodes");

static SYSCTL_NODE(_vfs_ffs, FFS_ADJ_NFFREE, adjnffree, CTLFLAG_WR,
	sysctl_ffs_fsck, "Adjust number of free frags");

static SYSCTL_NODE(_vfs_ffs, FFS_ADJ_NUMCLUSTERS, adjnumclusters, CTLFLAG_WR,
	sysctl_ffs_fsck, "Adjust number of free clusters");

static SYSCTL_NODE(_vfs_ffs, FFS_DIR_FREE, freedirs, CTLFLAG_WR,
	sysctl_ffs_fsck, "Free Range of Directory Inodes");

static SYSCTL_NODE(_vfs_ffs, FFS_FILE_FREE, freefiles, CTLFLAG_WR,
	sysctl_ffs_fsck, "Free Range of File Inodes");

static SYSCTL_NODE(_vfs_ffs, FFS_BLK_FREE, freeblks, CTLFLAG_WR,
	sysctl_ffs_fsck, "Free Range of Blocks");

static SYSCTL_NODE(_vfs_ffs, FFS_SET_FLAGS, setflags, CTLFLAG_WR,
	sysctl_ffs_fsck, "Change Filesystem Flags");

static SYSCTL_NODE(_vfs_ffs, FFS_SET_CWD, setcwd, CTLFLAG_WR,
	sysctl_ffs_fsck, "Set Current Working Directory");

static SYSCTL_NODE(_vfs_ffs, FFS_SET_DOTDOT, setdotdot, CTLFLAG_WR,
	sysctl_ffs_fsck, "Change Value of .. Entry");

static SYSCTL_NODE(_vfs_ffs, FFS_UNLINK, unlink, CTLFLAG_WR,
	sysctl_ffs_fsck, "Unlink a Duplicate Name");

static SYSCTL_NODE(_vfs_ffs, FFS_SET_INODE, setinode, CTLFLAG_WR,
	sysctl_ffs_fsck, "Update an On-Disk Inode");

static SYSCTL_NODE(_vfs_ffs, FFS_SET_BUFOUTPUT, setbufoutput, CTLFLAG_WR,
	sysctl_ffs_fsck, "Set Buffered Writing for Descriptor");

#define DEBUG 1
#ifdef DEBUG
static int fsckcmds = 0;
SYSCTL_INT(_debug, OID_AUTO, fsckcmds, CTLFLAG_RW, &fsckcmds, 0, "");
#endif /* DEBUG */

static int buffered_write(struct file *, struct uio *, struct ucred *,
	int, struct thread *);

static int
sysctl_ffs_fsck(SYSCTL_HANDLER_ARGS)
{
	struct thread *td = curthread;
	struct fsck_cmd cmd;
	struct ufsmount *ump;
	struct vnode *vp, *dvp, *fdvp;
	struct inode *ip, *dp;
	struct mount *mp;
	struct fs *fs;
	ufs2_daddr_t blkno;
	long blkcnt, blksize;
	u_long key;
	struct file *fp, *vfp;
	cap_rights_t rights;
	int filetype, error;
	static struct fileops *origops, bufferedops;

	if (req->newlen > sizeof cmd)
		return (EBADRPC);
	if ((error = SYSCTL_IN(req, &cmd, sizeof cmd)) != 0)
		return (error);
	if (cmd.version != FFS_CMD_VERSION)
		return (ERPCMISMATCH);
	if ((error = getvnode(td, cmd.handle,
	    cap_rights_init(&rights, CAP_FSCK), &fp)) != 0)
		return (error);
	vp = fp->f_data;
	if (vp->v_type != VREG && vp->v_type != VDIR) {
		fdrop(fp, td);
		return (EINVAL);
	}
	vn_start_write(vp, &mp, V_WAIT);
	if (mp == NULL ||
	    strncmp(mp->mnt_stat.f_fstypename, "ufs", MFSNAMELEN)) {
		vn_finished_write(mp);
		fdrop(fp, td);
		return (EINVAL);
	}
	ump = VFSTOUFS(mp);
	if ((mp->mnt_flag & MNT_RDONLY) &&
	    ump->um_fsckpid != td->td_proc->p_pid) {
		vn_finished_write(mp);
		fdrop(fp, td);
		return (EROFS);
	}
	fs = ump->um_fs;
	filetype = IFREG;

	switch (oidp->oid_number) {

	case FFS_SET_FLAGS:
#ifdef DEBUG
		if (fsckcmds)
			printf("%s: %s flags\n", mp->mnt_stat.f_mntonname,
			    cmd.size > 0 ? "set" : "clear");
#endif /* DEBUG */
		if (cmd.size > 0)
			fs->fs_flags |= (long)cmd.value;
		else
			fs->fs_flags &= ~(long)cmd.value;
		break;

	case FFS_ADJ_REFCNT:
#ifdef DEBUG
		if (fsckcmds) {
			printf("%s: adjust inode %jd link count by %jd\n",
			    mp->mnt_stat.f_mntonname, (intmax_t)cmd.value,
			    (intmax_t)cmd.size);
		}
#endif /* DEBUG */
		if ((error = ffs_vget(mp, (ino_t)cmd.value, LK_EXCLUSIVE, &vp)))
			break;
		ip = VTOI(vp);
		ip->i_nlink += cmd.size;
		DIP_SET(ip, i_nlink, ip->i_nlink);
		ip->i_effnlink += cmd.size;
		ip->i_flag |= IN_CHANGE | IN_MODIFIED;
		error = ffs_update(vp, 1);
		if (DOINGSOFTDEP(vp))
			softdep_change_linkcnt(ip);
		vput(vp);
		break;

	case FFS_ADJ_BLKCNT:
#ifdef DEBUG
		if (fsckcmds) {
			printf("%s: adjust inode %jd block count by %jd\n",
			    mp->mnt_stat.f_mntonname, (intmax_t)cmd.value,
			    (intmax_t)cmd.size);
		}
#endif /* DEBUG */
		if ((error = ffs_vget(mp, (ino_t)cmd.value, LK_EXCLUSIVE, &vp)))
			break;
		ip = VTOI(vp);
		DIP_SET(ip, i_blocks, DIP(ip, i_blocks) + cmd.size);
		ip->i_flag |= IN_CHANGE | IN_MODIFIED;
		error = ffs_update(vp, 1);
		vput(vp);
		break;

	case FFS_SET_SIZE:
#ifdef DEBUG
		if (fsckcmds) {
			printf("%s: set inode %jd size to %jd\n",
			    mp->mnt_stat.f_mntonname, (intmax_t)cmd.value,
			    (intmax_t)cmd.size);
		}
#endif /* DEBUG */
		if ((error = ffs_vget(mp, (ino_t)cmd.value, LK_EXCLUSIVE, &vp)))
			break;
		ip = VTOI(vp);
		DIP_SET(ip, i_size, cmd.size);
		ip->i_flag |= IN_CHANGE | IN_MODIFIED;
		error = ffs_update(vp, 1);
		vput(vp);
		break;

	case FFS_DIR_FREE:
		filetype = IFDIR;
		/* fall through */

	case FFS_FILE_FREE:
#ifdef DEBUG
		if (fsckcmds) {
			if (cmd.size == 1)
				printf("%s: free %s inode %ju\n",
				    mp->mnt_stat.f_mntonname,
				    filetype == IFDIR ? "directory" : "file",
				    (uintmax_t)cmd.value);
			else
				printf("%s: free %s inodes %ju-%ju\n",
				    mp->mnt_stat.f_mntonname,
				    filetype == IFDIR ? "directory" : "file",
				    (uintmax_t)cmd.value,
				    (uintmax_t)(cmd.value + cmd.size - 1));
		}
#endif /* DEBUG */
		while (cmd.size > 0) {
			if ((error = ffs_freefile(ump, fs, ump->um_devvp,
			    cmd.value, filetype, NULL)))
				break;
			cmd.size -= 1;
			cmd.value += 1;
		}
		break;

	case FFS_BLK_FREE:
#ifdef DEBUG
		if (fsckcmds) {
			if (cmd.size == 1)
				printf("%s: free block %jd\n",
				    mp->mnt_stat.f_mntonname,
				    (intmax_t)cmd.value);
			else
				printf("%s: free blocks %jd-%jd\n",
				    mp->mnt_stat.f_mntonname, 
				    (intmax_t)cmd.value,
				    (intmax_t)cmd.value + cmd.size - 1);
		}
#endif /* DEBUG */
		blkno = cmd.value;
		blkcnt = cmd.size;
		blksize = fs->fs_frag - (blkno % fs->fs_frag);
		key = ffs_blkrelease_start(ump, ump->um_devvp, UFS_ROOTINO);
		while (blkcnt > 0) {
			if (blkcnt < blksize)
				blksize = blkcnt;
			ffs_blkfree(ump, fs, ump->um_devvp, blkno,
			    blksize * fs->fs_fsize, UFS_ROOTINO, 
			    VDIR, NULL, key);
			blkno += blksize;
			blkcnt -= blksize;
			blksize = fs->fs_frag;
		}
		ffs_blkrelease_finish(ump, key);
		break;

	/*
	 * Adjust superblock summaries.  fsck(8) is expected to
	 * submit deltas when necessary.
	 */
	case FFS_ADJ_NDIR:
#ifdef DEBUG
		if (fsckcmds) {
			printf("%s: adjust number of directories by %jd\n",
			    mp->mnt_stat.f_mntonname, (intmax_t)cmd.value);
		}
#endif /* DEBUG */
		fs->fs_cstotal.cs_ndir += cmd.value;
		break;

	case FFS_ADJ_NBFREE:
#ifdef DEBUG
		if (fsckcmds) {
			printf("%s: adjust number of free blocks by %+jd\n",
			    mp->mnt_stat.f_mntonname, (intmax_t)cmd.value);
		}
#endif /* DEBUG */
		fs->fs_cstotal.cs_nbfree += cmd.value;
		break;

	case FFS_ADJ_NIFREE:
#ifdef DEBUG
		if (fsckcmds) {
			printf("%s: adjust number of free inodes by %+jd\n",
			    mp->mnt_stat.f_mntonname, (intmax_t)cmd.value);
		}
#endif /* DEBUG */
		fs->fs_cstotal.cs_nifree += cmd.value;
		break;

	case FFS_ADJ_NFFREE:
#ifdef DEBUG
		if (fsckcmds) {
			printf("%s: adjust number of free frags by %+jd\n",
			    mp->mnt_stat.f_mntonname, (intmax_t)cmd.value);
		}
#endif /* DEBUG */
		fs->fs_cstotal.cs_nffree += cmd.value;
		break;

	case FFS_ADJ_NUMCLUSTERS:
#ifdef DEBUG
		if (fsckcmds) {
			printf("%s: adjust number of free clusters by %+jd\n",
			    mp->mnt_stat.f_mntonname, (intmax_t)cmd.value);
		}
#endif /* DEBUG */
		fs->fs_cstotal.cs_numclusters += cmd.value;
		break;

	case FFS_SET_CWD:
#ifdef DEBUG
		if (fsckcmds) {
			printf("%s: set current directory to inode %jd\n",
			    mp->mnt_stat.f_mntonname, (intmax_t)cmd.value);
		}
#endif /* DEBUG */
		if ((error = ffs_vget(mp, (ino_t)cmd.value, LK_SHARED, &vp)))
			break;
		AUDIT_ARG_VNODE1(vp);
		if ((error = change_dir(vp, td)) != 0) {
			vput(vp);
			break;
		}
		VOP_UNLOCK(vp, 0);
		pwd_chdir(td, vp);
		break;

	case FFS_SET_DOTDOT:
#ifdef DEBUG
		if (fsckcmds) {
			printf("%s: change .. in cwd from %jd to %jd\n",
			    mp->mnt_stat.f_mntonname, (intmax_t)cmd.value,
			    (intmax_t)cmd.size);
		}
#endif /* DEBUG */
		/*
		 * First we have to get and lock the parent directory
		 * to which ".." points.
		 */
		error = ffs_vget(mp, (ino_t)cmd.value, LK_EXCLUSIVE, &fdvp);
		if (error)
			break;
		/*
		 * Now we get and lock the child directory containing "..".
		 */
		FILEDESC_SLOCK(td->td_proc->p_fd);
		dvp = td->td_proc->p_fd->fd_cdir;
		FILEDESC_SUNLOCK(td->td_proc->p_fd);
		if ((error = vget(dvp, LK_EXCLUSIVE, td)) != 0) {
			vput(fdvp);
			break;
		}
		dp = VTOI(dvp);
		dp->i_offset = 12;	/* XXX mastertemplate.dot_reclen */
		error = ufs_dirrewrite(dp, VTOI(fdvp), (ino_t)cmd.size,
		    DT_DIR, 0);
		cache_purge(fdvp);
		cache_purge(dvp);
		vput(dvp);
		vput(fdvp);
		break;

	case FFS_UNLINK:
#ifdef DEBUG
		if (fsckcmds) {
			char buf[32];

			if (copyinstr((char *)(intptr_t)cmd.value, buf,32,NULL))
				strncpy(buf, "Name_too_long", 32);
			printf("%s: unlink %s (inode %jd)\n",
			    mp->mnt_stat.f_mntonname, buf, (intmax_t)cmd.size);
		}
#endif /* DEBUG */
		/*
		 * kern_unlinkat will do its own start/finish writes and
		 * they do not nest, so drop ours here. Setting mp == NULL
		 * indicates that vn_finished_write is not needed down below.
		 */
		vn_finished_write(mp);
		mp = NULL;
		error = kern_unlinkat(td, AT_FDCWD, (char *)(intptr_t)cmd.value,
		    UIO_USERSPACE, 0, (ino_t)cmd.size);
		break;

	case FFS_SET_INODE:
		if (ump->um_fsckpid != td->td_proc->p_pid) {
			error = EPERM;
			break;
		}
#ifdef DEBUG
		if (fsckcmds) {
			printf("%s: update inode %jd\n",
			    mp->mnt_stat.f_mntonname, (intmax_t)cmd.value);
		}
#endif /* DEBUG */
		if ((error = ffs_vget(mp, (ino_t)cmd.value, LK_EXCLUSIVE, &vp)))
			break;
		AUDIT_ARG_VNODE1(vp);
		ip = VTOI(vp);
		if (I_IS_UFS1(ip))
			error = copyin((void *)(intptr_t)cmd.size, ip->i_din1,
			    sizeof(struct ufs1_dinode));
		else
			error = copyin((void *)(intptr_t)cmd.size, ip->i_din2,
			    sizeof(struct ufs2_dinode));
		if (error) {
			vput(vp);
			break;
		}
		ip->i_flag |= IN_CHANGE | IN_MODIFIED;
		error = ffs_update(vp, 1);
		vput(vp);
		break;

	case FFS_SET_BUFOUTPUT:
		if (ump->um_fsckpid != td->td_proc->p_pid) {
			error = EPERM;
			break;
		}
		if (ITOUMP(VTOI(vp)) != ump) {
			error = EINVAL;
			break;
		}
#ifdef DEBUG
		if (fsckcmds) {
			printf("%s: %s buffered output for descriptor %jd\n",
			    mp->mnt_stat.f_mntonname,
			    cmd.size == 1 ? "enable" : "disable",
			    (intmax_t)cmd.value);
		}
#endif /* DEBUG */
		if ((error = getvnode(td, cmd.value,
		    cap_rights_init(&rights, CAP_FSCK), &vfp)) != 0)
			break;
		if (vfp->f_vnode->v_type != VCHR) {
			fdrop(vfp, td);
			error = EINVAL;
			break;
		}
		if (origops == NULL) {
			origops = vfp->f_ops;
			bcopy((void *)origops, (void *)&bufferedops,
			    sizeof(bufferedops));
			bufferedops.fo_write = buffered_write;
		}
		if (cmd.size == 1)
			atomic_store_rel_ptr((volatile uintptr_t *)&vfp->f_ops,
			    (uintptr_t)&bufferedops);
		else
			atomic_store_rel_ptr((volatile uintptr_t *)&vfp->f_ops,
			    (uintptr_t)origops);
		fdrop(vfp, td);
		break;

	default:
#ifdef DEBUG
		if (fsckcmds) {
			printf("Invalid request %d from fsck\n",
			    oidp->oid_number);
		}
#endif /* DEBUG */
		error = EINVAL;
		break;

	}
	fdrop(fp, td);
	vn_finished_write(mp);
	return (error);
}

/*
 * Function to switch a descriptor to use the buffer cache to stage
 * its I/O. This is needed so that writes to the filesystem device
 * will give snapshots a chance to copy modified blocks for which it
 * needs to retain copies.
 */
static int
buffered_write(fp, uio, active_cred, flags, td)
	struct file *fp;
	struct uio *uio;
	struct ucred *active_cred;
	int flags;
	struct thread *td;
{
	struct vnode *devvp, *vp;
	struct inode *ip;
	struct buf *bp;
	struct fs *fs;
	struct filedesc *fdp;
	int error;
	daddr_t lbn;

	/*
	 * The devvp is associated with the /dev filesystem. To discover
	 * the filesystem with which the device is associated, we depend
	 * on the application setting the current directory to a location
	 * within the filesystem being written. Yes, this is an ugly hack.
	 */
	devvp = fp->f_vnode;
	if (!vn_isdisk(devvp, NULL))
		return (EINVAL);
	fdp = td->td_proc->p_fd;
	FILEDESC_SLOCK(fdp);
	vp = fdp->fd_cdir;
	vref(vp);
	FILEDESC_SUNLOCK(fdp);
	vn_lock(vp, LK_SHARED | LK_RETRY);
	/*
	 * Check that the current directory vnode indeed belongs to
	 * UFS before trying to dereference UFS-specific v_data fields.
	 */
	if (vp->v_op != &ffs_vnodeops1 && vp->v_op != &ffs_vnodeops2) {
		vput(vp);
		return (EINVAL);
	}
	ip = VTOI(vp);
	if (ITODEVVP(ip) != devvp) {
		vput(vp);
		return (EINVAL);
	}
	fs = ITOFS(ip);
	vput(vp);
	foffset_lock_uio(fp, uio, flags);
	vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
#ifdef DEBUG
	if (fsckcmds) {
		printf("%s: buffered write for block %jd\n",
		    fs->fs_fsmnt, (intmax_t)btodb(uio->uio_offset));
	}
#endif /* DEBUG */
	/*
	 * All I/O must be contained within a filesystem block, start on
	 * a fragment boundary, and be a multiple of fragments in length.
	 */
	if (uio->uio_resid > fs->fs_bsize - (uio->uio_offset % fs->fs_bsize) ||
	    fragoff(fs, uio->uio_offset) != 0 ||
	    fragoff(fs, uio->uio_resid) != 0) {
		error = EINVAL;
		goto out;
	}
	lbn = numfrags(fs, uio->uio_offset);
	bp = getblk(devvp, lbn, uio->uio_resid, 0, 0, 0);
	bp->b_flags |= B_RELBUF;
	if ((error = uiomove((char *)bp->b_data, uio->uio_resid, uio)) != 0) {
		brelse(bp);
		goto out;
	}
	error = bwrite(bp);
out:
	VOP_UNLOCK(devvp, 0);
	foffset_unlock_uio(fp, uio, flags | FOF_NEXTOFF);
	return (error);
}
