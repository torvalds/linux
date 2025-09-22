/*	$OpenBSD: ext2fs_bmap.c,v 1.29 2024/04/13 23:44:11 jsg Exp $	*/
/*	$NetBSD: ext2fs_bmap.c,v 1.5 2000/03/30 12:41:11 augustss Exp $	*/

/*
 * Copyright (c) 1997 Manuel Bouyer.
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
 * Modified for ext2fs by Manuel Bouyer.
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
#include <ufs/ext2fs/ext2fs.h>
#include <ufs/ext2fs/ext2fs_extents.h>
#include <ufs/ext2fs/ext2fs_extern.h>

static int	ext4_bmapext(struct vnode *, daddr_t, daddr_t *, struct indir *,
    int *, int *);
static int	ext2fs_bmaparray(struct vnode *, daddr_t, daddr_t *, struct indir *,
    int *, int *);

/*
 * Bmap converts a the logical block number of a file to its physical block
 * number on the disk. The conversion is done by using the logical block
 * number to index into the array of block pointers described by the dinode.
 */
int
ext2fs_bmap(void *v)
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

	if (VTOI(ap->a_vp)->i_e2din->e2di_flags & EXT4_EXTENTS) {
		return (ext4_bmapext(ap->a_vp, ap->a_bn, ap->a_bnp, NULL, NULL,
		    ap->a_runp));
	}
	return (ext2fs_bmaparray(ap->a_vp, ap->a_bn, ap->a_bnp, NULL, NULL,
		ap->a_runp));
}

/*
 * Logical block number of a file -> physical block number on disk within ext4 extents.
 */
int
ext4_bmapext(struct vnode *vp, daddr_t bn, daddr_t *bnp, struct indir *ap, int *nump, int *runp)
{
	struct inode *ip;
	struct m_ext2fs *fs;
	struct ext4_extent *ep;
	struct ext4_extent_path path;
	daddr_t pos;

	ip = VTOI(vp);
	fs = ip->i_e2fs;

	if (runp != NULL)
		*runp = 0;
	if (nump != NULL)
		*nump = 0;

	ext4_ext_find_extent(fs, ip, bn, &path);
	if ((ep = path.ep_ext) == NULL)
		return (EIO);

	pos = bn - ep->e_blk + (((daddr_t)ep->e_start_hi << 32) | ep->e_start_lo);
	if ((*bnp = fsbtodb(fs, pos)) == 0)
		*bnp = -1;
	return (0);
}

/*
 * Indirect blocks are now on the vnode for the file.  They are given negative
 * logical block numbers.  Indirect blocks are addressed by the negative
 * address of the first data block to which they point.  Double indirect blocks
 * are addressed by one less than the address of the first indirect block to
 * which they point.  Triple indirect blocks are addressed by one less than
 * the address of the first double indirect block to which they point.
 *
 * ext2fs_bmaparray does the bmap conversion, and if requested returns the
 * array of logical blocks which must be traversed to get to a block.
 * Each entry contains the offset into that block that gets you to the
 * next block and the disk address of the block (if it is assigned).
 */

int
ext2fs_bmaparray(struct vnode *vp, daddr_t bn, daddr_t *bnp,
    struct indir *ap, int *nump, int *runp)
{
	struct inode *ip;
	struct buf *bp;
	struct ufsmount *ump;
	struct mount *mp;
	struct vnode *devvp;
	struct indir a[NIADDR+1], *xap;
	int32_t daddr;
	long metalbn;
	int error, maxrun = 0, num;

	ip = VTOI(vp);
	mp = vp->v_mount;
	ump = VFSTOUFS(mp);
#ifdef DIAGNOSTIC
	if ((ap != NULL && nump == NULL) || (ap == NULL && nump != NULL))
		panic("ext2fs_bmaparray: invalid arguments");
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
		*bnp = blkptrtodb(ump, letoh32(ip->i_e2fs_blocks[bn]));
		if (*bnp == 0)
			*bnp = -1;
		else if (runp)
			for (++bn; bn < NDADDR && *runp < maxrun &&
			    is_sequential(ump,
			    letoh32(ip->i_e2fs_blocks[bn - 1]),
			    letoh32(ip->i_e2fs_blocks[bn]));
			    ++bn, ++*runp)
				/* nothing */;
		return (0);
	}


	/* Get disk address out of indirect block array */
	daddr = letoh32(ip->i_e2fs_blocks[NDADDR + xap->in_off]);

	devvp = VFSTOUFS(vp->v_mount)->um_devvp;

#ifdef DIAGNOSTIC
	if (num > NIADDR + 1 || num < 1) {
		printf("ext2fs_bmaparray: num=%d\n", num);
		panic("ext2fs_bmaparray: num");
	}
#endif
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
			panic("ext2fs_bmaparry: indirect block not in cache");
#endif
		else {
			bp->b_blkno = blkptrtodb(ump, daddr);
			bp->b_flags |= B_READ;
			VOP_STRATEGY(bp->b_vp, bp);
			curproc->p_ru.ru_inblock++;		/* XXX */
			bcstats.pendingreads++;
			if ((error = biowait(bp)) != 0) {
				brelse(bp);
				return (error);
			}
		}

		daddr = letoh32(((int32_t *)bp->b_data)[xap->in_off]);
		if (num == 1 && daddr && runp)
			for (bn = xap->in_off + 1;
			    bn < MNINDIR(ump) && *runp < maxrun &&
			    is_sequential(ump, ((u_int32_t *)bp->b_data)[bn - 1],
			    ((u_int32_t *)bp->b_data)[bn]);
			    ++bn, ++*runp)
				/* nothing */;
	}
	if (bp)
		brelse(bp);

	daddr = blkptrtodb(ump, daddr);
	*bnp = daddr == 0 ? -1 : daddr;
	return (0);
}
