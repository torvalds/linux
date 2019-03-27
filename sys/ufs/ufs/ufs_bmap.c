/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 *	@(#)ufs_bmap.c	8.7 (Berkeley) 3/21/95
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/racct.h>
#include <sys/resourcevar.h>
#include <sys/stat.h>

#include <ufs/ufs/extattr.h>
#include <ufs/ufs/quota.h>
#include <ufs/ufs/inode.h>
#include <ufs/ufs/ufsmount.h>
#include <ufs/ufs/ufs_extern.h>

/*
 * Bmap converts the logical block number of a file to its physical block
 * number on the disk. The conversion is done by using the logical block
 * number to index into the array of block pointers described by the dinode.
 */
int
ufs_bmap(ap)
	struct vop_bmap_args /* {
		struct vnode *a_vp;
		daddr_t a_bn;
		struct bufobj **a_bop;
		daddr_t *a_bnp;
		int *a_runp;
		int *a_runb;
	} */ *ap;
{
	ufs2_daddr_t blkno;
	int error;

	/*
	 * Check for underlying vnode requests and ensure that logical
	 * to physical mapping is requested.
	 */
	if (ap->a_bop != NULL)
		*ap->a_bop = &VFSTOUFS(ap->a_vp->v_mount)->um_devvp->v_bufobj;
	if (ap->a_bnp == NULL)
		return (0);

	error = ufs_bmaparray(ap->a_vp, ap->a_bn, &blkno, NULL,
	    ap->a_runp, ap->a_runb);
	*ap->a_bnp = blkno;
	return (error);
}

/*
 * Indirect blocks are now on the vnode for the file.  They are given negative
 * logical block numbers.  Indirect blocks are addressed by the negative
 * address of the first data block to which they point.  Double indirect blocks
 * are addressed by one less than the address of the first indirect block to
 * which they point.  Triple indirect blocks are addressed by one less than
 * the address of the first double indirect block to which they point.
 *
 * ufs_bmaparray does the bmap conversion, and if requested returns the
 * array of logical blocks which must be traversed to get to a block.
 * Each entry contains the offset into that block that gets you to the
 * next block and the disk address of the block (if it is assigned).
 */

int
ufs_bmaparray(vp, bn, bnp, nbp, runp, runb)
	struct vnode *vp;
	ufs2_daddr_t bn;
	ufs2_daddr_t *bnp;
	struct buf *nbp;
	int *runp;
	int *runb;
{
	struct inode *ip;
	struct buf *bp;
	struct ufsmount *ump;
	struct mount *mp;
	struct indir a[UFS_NIADDR+1], *ap;
	ufs2_daddr_t daddr;
	ufs_lbn_t metalbn;
	int error, num, maxrun = 0;
	int *nump;

	ap = NULL;
	ip = VTOI(vp);
	mp = vp->v_mount;
	ump = VFSTOUFS(mp);

	if (runp) {
		maxrun = mp->mnt_iosize_max / mp->mnt_stat.f_iosize - 1;
		*runp = 0;
	}

	if (runb) {
		*runb = 0;
	}


	ap = a;
	nump = &num;
	error = ufs_getlbns(vp, bn, ap, nump);
	if (error)
		return (error);

	num = *nump;
	if (num == 0) {
		if (bn >= 0 && bn < UFS_NDADDR) {
			*bnp = blkptrtodb(ump, DIP(ip, i_db[bn]));
		} else if (bn < 0 && bn >= -UFS_NXADDR) {
			*bnp = blkptrtodb(ump, ip->i_din2->di_extb[-1 - bn]);
			if (*bnp == 0)
				*bnp = -1;
			if (nbp == NULL)
				panic("ufs_bmaparray: mapping ext data");
			nbp->b_xflags |= BX_ALTDATA;
			return (0);
		} else {
			panic("ufs_bmaparray: blkno out of range");
		}
		/*
		 * Since this is FFS independent code, we are out of
		 * scope for the definitions of BLK_NOCOPY and
		 * BLK_SNAP, but we do know that they will fall in
		 * the range 1..um_seqinc, so we use that test and
		 * return a request for a zeroed out buffer if attempts
		 * are made to read a BLK_NOCOPY or BLK_SNAP block.
		 */
		if ((ip->i_flags & SF_SNAPSHOT) && DIP(ip, i_db[bn]) > 0 &&
		    DIP(ip, i_db[bn]) < ump->um_seqinc) {
			*bnp = -1;
		} else if (*bnp == 0) {
			if (ip->i_flags & SF_SNAPSHOT)
				*bnp = blkptrtodb(ump, bn * ump->um_seqinc);
			else
				*bnp = -1;
		} else if (runp) {
			ufs2_daddr_t bnb = bn;
			for (++bn; bn < UFS_NDADDR && *runp < maxrun &&
			    is_sequential(ump, DIP(ip, i_db[bn - 1]),
			    DIP(ip, i_db[bn]));
			    ++bn, ++*runp);
			bn = bnb;
			if (runb && (bn > 0)) {
				for (--bn; (bn >= 0) && (*runb < maxrun) &&
					is_sequential(ump, DIP(ip, i_db[bn]),
						DIP(ip, i_db[bn+1]));
						--bn, ++*runb);
			}
		}
		return (0);
	}


	/* Get disk address out of indirect block array */
	daddr = DIP(ip, i_ib[ap->in_off]);

	for (bp = NULL, ++ap; --num; ++ap) {
		/*
		 * Exit the loop if there is no disk address assigned yet and
		 * the indirect block isn't in the cache, or if we were
		 * looking for an indirect block and we've found it.
		 */

		metalbn = ap->in_lbn;
		if ((daddr == 0 && !incore(&vp->v_bufobj, metalbn)) || metalbn == bn)
			break;
		/*
		 * If we get here, we've either got the block in the cache
		 * or we have a disk address for it, go fetch it.
		 */
		if (bp)
			bqrelse(bp);

		bp = getblk(vp, metalbn, mp->mnt_stat.f_iosize, 0, 0, 0);
		if ((bp->b_flags & B_CACHE) == 0) {
#ifdef INVARIANTS
			if (!daddr)
				panic("ufs_bmaparray: indirect block not in cache");
#endif
			bp->b_blkno = blkptrtodb(ump, daddr);
			bp->b_iocmd = BIO_READ;
			bp->b_flags &= ~B_INVAL;
			bp->b_ioflags &= ~BIO_ERROR;
			vfs_busy_pages(bp, 0);
			bp->b_iooffset = dbtob(bp->b_blkno);
			bstrategy(bp);
#ifdef RACCT
			if (racct_enable) {
				PROC_LOCK(curproc);
				racct_add_buf(curproc, bp, 0);
				PROC_UNLOCK(curproc);
			}
#endif /* RACCT */
			curthread->td_ru.ru_inblock++;
			error = bufwait(bp);
			if (error) {
				brelse(bp);
				return (error);
			}
		}

		if (I_IS_UFS1(ip)) {
			daddr = ((ufs1_daddr_t *)bp->b_data)[ap->in_off];
			if (num == 1 && daddr && runp) {
				for (bn = ap->in_off + 1;
				    bn < MNINDIR(ump) && *runp < maxrun &&
				    is_sequential(ump,
				    ((ufs1_daddr_t *)bp->b_data)[bn - 1],
				    ((ufs1_daddr_t *)bp->b_data)[bn]);
				    ++bn, ++*runp);
				bn = ap->in_off;
				if (runb && bn) {
					for (--bn; bn >= 0 && *runb < maxrun &&
					    is_sequential(ump,
					    ((ufs1_daddr_t *)bp->b_data)[bn],
					    ((ufs1_daddr_t *)bp->b_data)[bn+1]);
					    --bn, ++*runb);
				}
			}
			continue;
		}
		daddr = ((ufs2_daddr_t *)bp->b_data)[ap->in_off];
		if (num == 1 && daddr && runp) {
			for (bn = ap->in_off + 1;
			    bn < MNINDIR(ump) && *runp < maxrun &&
			    is_sequential(ump,
			    ((ufs2_daddr_t *)bp->b_data)[bn - 1],
			    ((ufs2_daddr_t *)bp->b_data)[bn]);
			    ++bn, ++*runp);
			bn = ap->in_off;
			if (runb && bn) {
				for (--bn; bn >= 0 && *runb < maxrun &&
				    is_sequential(ump,
				    ((ufs2_daddr_t *)bp->b_data)[bn],
				    ((ufs2_daddr_t *)bp->b_data)[bn + 1]);
				    --bn, ++*runb);
			}
		}
	}
	if (bp)
		bqrelse(bp);

	/*
	 * Since this is FFS independent code, we are out of scope for the
	 * definitions of BLK_NOCOPY and BLK_SNAP, but we do know that they
	 * will fall in the range 1..um_seqinc, so we use that test and
	 * return a request for a zeroed out buffer if attempts are made
	 * to read a BLK_NOCOPY or BLK_SNAP block.
	 */
	if ((ip->i_flags & SF_SNAPSHOT) && daddr > 0 && daddr < ump->um_seqinc){
		*bnp = -1;
		return (0);
	}
	*bnp = blkptrtodb(ump, daddr);
	if (*bnp == 0) {
		if (ip->i_flags & SF_SNAPSHOT)
			*bnp = blkptrtodb(ump, bn * ump->um_seqinc);
		else
			*bnp = -1;
	}
	return (0);
}

/*
 * Create an array of logical block number/offset pairs which represent the
 * path of indirect blocks required to access a data block.  The first "pair"
 * contains the logical block number of the appropriate single, double or
 * triple indirect block and the offset into the inode indirect block array.
 * Note, the logical block number of the inode single/double/triple indirect
 * block appears twice in the array, once with the offset into the i_ib and
 * once with the offset into the page itself.
 */
int
ufs_getlbns(vp, bn, ap, nump)
	struct vnode *vp;
	ufs2_daddr_t bn;
	struct indir *ap;
	int *nump;
{
	ufs2_daddr_t blockcnt;
	ufs_lbn_t metalbn, realbn;
	struct ufsmount *ump;
	int i, numlevels, off;

	ump = VFSTOUFS(vp->v_mount);
	if (nump)
		*nump = 0;
	numlevels = 0;
	realbn = bn;
	if (bn < 0)
		bn = -bn;

	/* The first UFS_NDADDR blocks are direct blocks. */
	if (bn < UFS_NDADDR)
		return (0);

	/*
	 * Determine the number of levels of indirection.  After this loop
	 * is done, blockcnt indicates the number of data blocks possible
	 * at the previous level of indirection, and UFS_NIADDR - i is the
	 * number of levels of indirection needed to locate the requested block.
	 */
	for (blockcnt = 1, i = UFS_NIADDR, bn -= UFS_NDADDR; ;
	    i--, bn -= blockcnt) {
		if (i == 0)
			return (EFBIG);
		blockcnt *= MNINDIR(ump);
		if (bn < blockcnt)
			break;
	}

	/* Calculate the address of the first meta-block. */
	if (realbn >= 0)
		metalbn = -(realbn - bn + UFS_NIADDR - i);
	else
		metalbn = -(-realbn - bn + UFS_NIADDR - i);

	/*
	 * At each iteration, off is the offset into the bap array which is
	 * an array of disk addresses at the current level of indirection.
	 * The logical block number and the offset in that block are stored
	 * into the argument array.
	 */
	ap->in_lbn = metalbn;
	ap->in_off = off = UFS_NIADDR - i;
	ap++;
	for (++numlevels; i <= UFS_NIADDR; i++) {
		/* If searching for a meta-data block, quit when found. */
		if (metalbn == realbn)
			break;

		blockcnt /= MNINDIR(ump);
		off = (bn / blockcnt) % MNINDIR(ump);

		++numlevels;
		ap->in_lbn = metalbn;
		ap->in_off = off;
		++ap;

		metalbn -= -1 + off * blockcnt;
	}
	if (nump)
		*nump = numlevels;
	return (0);
}
