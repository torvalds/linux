/*	$OpenBSD: ufs_bmap.c,v 1.37 2021/12/12 09:14:59 visa Exp $	*/
/*	$NetBSD: ufs_bmap.c,v 1.3 1996/02/09 22:36:00 christos Exp $	*/

/*
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
 *	@(#)ufs_bmap.c	8.6 (Berkeley) 1/21/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/specdev.h>

#include <ufs/ufs/quota.h>
#include <ufs/ufs/inode.h>
#include <ufs/ufs/ufsmount.h>
#include <ufs/ufs/ufs_extern.h>

/*
 * Bmap converts a the logical block number of a file to its physical block
 * number on the disk. The conversion is done by using the logical block
 * number to index into the array of block pointers described by the dinode.
 */
int
ufs_bmap(void *v)
{
	struct vop_bmap_args *ap = v;

	/*
	 * Check for underlying vnode requests and ensure that logical
	 * to physical mapping is requested.
	 */
	if (ap->a_vpp != NULL)
		*ap->a_vpp = VTOI(ap->a_vp)->i_devvp;
	if (ap->a_bnp == NULL)
		return (0);

	return (ufs_bmaparray(ap->a_vp, ap->a_bn, ap->a_bnp, NULL, NULL,
	    ap->a_runp));
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
ufs_bmaparray(struct vnode *vp, daddr_t bn, daddr_t *bnp, struct indir *ap,
    int *nump, int *runp)
{
	struct inode *ip;
	struct buf *bp;
	struct ufsmount *ump;
	struct mount *mp;
	struct vnode *devvp;
	struct indir a[NIADDR+1], *xap;
	daddr_t daddr, metalbn;
	int error, maxrun = 0, num;

	ip = VTOI(vp);
	mp = vp->v_mount;
	ump = VFSTOUFS(mp);
#ifdef DIAGNOSTIC
	if ((ap != NULL && nump == NULL) || (ap == NULL && nump != NULL))
		panic("ufs_bmaparray: invalid arguments");
#endif

	if (runp) {
		/*
		 * XXX
		 * If MAXBSIZE is the largest transfer the disks can handle,
		 * we probably want maxrun to be 1 block less so that we
		 * don't create a block larger than the device can handle.
		 */
		*runp = 0;
		maxrun = MAXBSIZE / mp->mnt_stat.f_iosize - 1;
	}

	xap = ap == NULL ? a : ap;
	if (!nump)
		nump = &num;
	if ((error = ufs_getlbns(vp, bn, xap, nump)) != 0)
		return (error);

	num = *nump;
	if (num == 0) {
		*bnp = blkptrtodb(ump, DIP(ip, db[bn]));
		if (*bnp == 0)
			*bnp = -1;
		else if (runp)
			for (++bn; bn < NDADDR && *runp < maxrun &&
			    is_sequential(ump, DIP(ip, db[bn - 1]),
			        DIP(ip, db[bn]));
			    ++bn, ++*runp);
		return (0);
	}


	/* Get disk address out of indirect block array */
	daddr = DIP(ip, ib[xap->in_off]);

	devvp = VFSTOUFS(vp->v_mount)->um_devvp;
	for (bp = NULL, ++xap; --num; ++xap) {
		/* 
		 * Exit the loop if there is no disk address assigned yet and
		 * the indirect block isn't in the cache, or if we were
		 * looking for an indirect block and we've found it.
		 */

		metalbn = xap->in_lbn;
		if ((daddr == 0 && !incore(vp, metalbn)) || metalbn == bn)
			break;
		/*
		 * If we get here, we've either got the block in the cache
		 * or we have a disk address for it, go fetch it.
		 */
		if (bp)
			brelse(bp);

		xap->in_exists = 1;
		bp = getblk(vp, metalbn, mp->mnt_stat.f_iosize, 0, INFSLP);
		if (bp->b_flags & (B_DONE | B_DELWRI)) {
			;
		}
#ifdef DIAGNOSTIC
		else if (!daddr)
			panic("ufs_bmaparray: indirect block not in cache");
#endif
		else {
			bp->b_blkno = blkptrtodb(ump, daddr);
			bp->b_flags |= B_READ;
			bcstats.pendingreads++;
			bcstats.numreads++;
			VOP_STRATEGY(bp->b_vp, bp);
			curproc->p_ru.ru_inblock++;		/* XXX */
			if ((error = biowait(bp)) != 0) {
				brelse(bp);
				return (error);
			}
		}

#ifdef FFS2
		if (ip->i_ump->um_fstype == UM_UFS2) {
			daddr = ((int64_t *)bp->b_data)[xap->in_off];
			if (num == 1 && daddr && runp)
				for (bn = xap->in_off + 1;
				    bn < MNINDIR(ump) && *runp < maxrun &&
				    is_sequential(ump,
					((int64_t *)bp->b_data)[bn - 1],
					((int64_t *)bp->b_data)[bn]);
				    ++bn, ++*runp);

                        continue;
		}

#endif /* FFS2 */

		daddr = ((int32_t *)bp->b_data)[xap->in_off];
		if (num == 1 && daddr && runp)
			for (bn = xap->in_off + 1;
			    bn < MNINDIR(ump) && *runp < maxrun &&
			    is_sequential(ump,
				((int32_t *)bp->b_data)[bn - 1],
				((int32_t *)bp->b_data)[bn]);
			    ++bn, ++*runp);
	}
	if (bp)
		brelse(bp);

	daddr = blkptrtodb(ump, daddr);
	*bnp = daddr == 0 ? -1 : daddr;
	return (0);
}

/*
 * Create an array of logical block number/offset pairs which represent the
 * path of indirect blocks required to access a data block.  The first "pair"
 * contains the logical block number of the appropriate single, double or
 * triple indirect block and the offset into the inode indirect block array.
 * Note, the logical block number of the inode single/double/triple indirect
 * block appears twice in the array, once with the offset into the i_ffs_ib and
 * once with the offset into the page itself.
 */
int
ufs_getlbns(struct vnode *vp, daddr_t bn, struct indir *ap, int *nump)
{
	daddr_t metalbn, realbn;
	struct ufsmount *ump;
	int64_t blockcnt;
	int i, numlevels, off;

	ump = VFSTOUFS(vp->v_mount);
	if (nump)
		*nump = 0;
	numlevels = 0;
	realbn = bn;
	if (bn < 0)
		bn = -bn;

#ifdef DIAGNOSTIC
	if (realbn < 0 && realbn > -NDADDR) {
		panic ("ufs_getlbns: Invalid indirect block %lld specified",
		    (long long)realbn);
	}
#endif

	/* The first NDADDR blocks are direct blocks. */
	if (bn < NDADDR)
		return (0);

	/* 
	 * Determine the number of levels of indirection.  After this loop
	 * is done, blockcnt indicates the number of data blocks possible
	 * at the given level of indirection, and NIADDR - i is the number
	 * of levels of indirection needed to locate the requested block.
	 */
	for (blockcnt = 1, i = NIADDR, bn -= NDADDR;; i--, bn -= blockcnt) {
		if (i == 0)
			return (EFBIG);
		blockcnt *= MNINDIR(ump);
		if (bn < blockcnt)
			break;
	}

	/* Calculate the address of the first meta-block. */
	if (realbn >= 0)
		metalbn = -(realbn - bn + NIADDR - i);
	else
		metalbn = -(-realbn - bn + NIADDR - i);

	/* 
	 * At each iteration, off is the offset into the bap array which is
	 * an array of disk addresses at the current level of indirection.
	 * The logical block number and the offset in that block are stored
	 * into the argument array.
	 */
	ap->in_lbn = metalbn;
	ap->in_off = off = NIADDR - i;
	ap->in_exists = 0;
	ap++;
	for (++numlevels; i <= NIADDR; i++) {
		/* If searching for a meta-data block, quit when found. */
		if (metalbn == realbn)
			break;

		blockcnt /= MNINDIR(ump);
		off = (bn / blockcnt) % MNINDIR(ump);

		++numlevels;
		ap->in_lbn = metalbn;
		ap->in_off = off;
		ap->in_exists = 0;
		++ap;

		metalbn -= -1 + off * blockcnt;
	}
#ifdef DIAGNOSTIC
	if (realbn < 0 && metalbn != realbn) {
		panic("ufs_getlbns: indirect block %lld not found",
		    (long long)realbn);
	}
#endif
	if (nump)
		*nump = numlevels;
	return (0);
}
