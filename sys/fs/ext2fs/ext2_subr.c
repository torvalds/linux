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
 *	@(#)ffs_subr.c	8.2 (Berkeley) 9/21/93
 * $FreeBSD$
 */

#include <sys/param.h>

#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/lock.h>
#include <sys/ucred.h>
#include <sys/vnode.h>

#include <fs/ext2fs/fs.h>
#include <fs/ext2fs/inode.h>
#include <fs/ext2fs/ext2fs.h>
#include <fs/ext2fs/ext2_extern.h>
#include <fs/ext2fs/fs.h>
#include <fs/ext2fs/ext2_extents.h>
#include <fs/ext2fs/ext2_mount.h>
#include <fs/ext2fs/ext2_dinode.h>

/*
 * Return buffer with the contents of block "offset" from the beginning of
 * directory "ip".  If "res" is non-zero, fill it in with a pointer to the
 * remaining space in the directory.
 */
int
ext2_blkatoff(struct vnode *vp, off_t offset, char **res, struct buf **bpp)
{
	struct inode *ip;
	struct m_ext2fs *fs;
	struct buf *bp;
	e2fs_lbn_t lbn;
	int error, bsize;

	ip = VTOI(vp);
	fs = ip->i_e2fs;
	lbn = lblkno(fs, offset);
	bsize = blksize(fs, ip, lbn);

	if ((error = bread(vp, lbn, bsize, NOCRED, &bp)) != 0) {
		brelse(bp);
		return (error);
	}
	error = ext2_dir_blk_csum_verify(ip, bp);
	if (error != 0) {
		brelse(bp);
		return (error);
	}
	if (res)
		*res = (char *)bp->b_data + blkoff(fs, offset);

	*bpp = bp;

	return (0);
}

/*
 * Update the cluster map because of an allocation of free like ffs.
 *
 * Cnt == 1 means free; cnt == -1 means allocating.
 */
void
ext2_clusteracct(struct m_ext2fs *fs, char *bbp, int cg, e4fs_daddr_t bno, int cnt)
{
	int32_t *sump = fs->e2fs_clustersum[cg].cs_sum;
	int32_t *lp;
	e4fs_daddr_t start, end, loc, forw, back;
	int bit, i;

	/* Initialize the cluster summary array. */
	if (fs->e2fs_clustersum[cg].cs_init == 0) {
		int run = 0;

		bit = 1;
		loc = 0;

		for (i = 0; i < fs->e2fs->e2fs_fpg; i++) {
			if ((bbp[loc] & bit) == 0)
				run++;
			else if (run != 0) {
				if (run > fs->e2fs_contigsumsize)
					run = fs->e2fs_contigsumsize;
				sump[run]++;
				run = 0;
			}
			if ((i & (NBBY - 1)) != (NBBY - 1))
				bit <<= 1;
			else {
				loc++;
				bit = 1;
			}
		}
		if (run != 0) {
			if (run > fs->e2fs_contigsumsize)
				run = fs->e2fs_contigsumsize;
			sump[run]++;
		}
		fs->e2fs_clustersum[cg].cs_init = 1;
	}

	if (fs->e2fs_contigsumsize <= 0)
		return;

	/* Find the size of the cluster going forward. */
	start = bno + 1;
	end = start + fs->e2fs_contigsumsize;
	if (end > fs->e2fs->e2fs_fpg)
		end = fs->e2fs->e2fs_fpg;
	loc = start / NBBY;
	bit = 1 << (start % NBBY);
	for (i = start; i < end; i++) {
		if ((bbp[loc] & bit) != 0)
			break;
		if ((i & (NBBY - 1)) != (NBBY - 1))
			bit <<= 1;
		else {
			loc++;
			bit = 1;
		}
	}
	forw = i - start;

	/* Find the size of the cluster going backward. */
	start = bno - 1;
	end = start - fs->e2fs_contigsumsize;
	if (end < 0)
		end = -1;
	loc = start / NBBY;
	bit = 1 << (start % NBBY);
	for (i = start; i > end; i--) {
		if ((bbp[loc] & bit) != 0)
			break;
		if ((i & (NBBY - 1)) != 0)
			bit >>= 1;
		else {
			loc--;
			bit = 1 << (NBBY - 1);
		}
	}
	back = start - i;

	/*
	 * Account for old cluster and the possibly new forward and
	 * back clusters.
	 */
	i = back + forw + 1;
	if (i > fs->e2fs_contigsumsize)
		i = fs->e2fs_contigsumsize;
	sump[i] += cnt;
	if (back > 0)
		sump[back] -= cnt;
	if (forw > 0)
		sump[forw] -= cnt;

	/* Update cluster summary information. */
	lp = &sump[fs->e2fs_contigsumsize];
	for (i = fs->e2fs_contigsumsize; i > 0; i--)
		if (*lp-- > 0)
			break;
	fs->e2fs_maxcluster[cg] = i;
}
