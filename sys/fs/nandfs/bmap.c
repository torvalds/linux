/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Semihalf
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/namei.h>
#include <sys/kernel.h>
#include <sys/stat.h>
#include <sys/buf.h>
#include <sys/bio.h>
#include <sys/proc.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/signalvar.h>
#include <sys/malloc.h>
#include <sys/dirent.h>
#include <sys/lockf.h>
#include <sys/ktr.h>
#include <sys/kdb.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_object.h>
#include <vm/vnode_pager.h>

#include <machine/_inttypes.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_object.h>
#include <vm/vnode_pager.h>

#include "nandfs_mount.h"
#include "nandfs.h"
#include "nandfs_subr.h"
#include "bmap.h"

static int bmap_getlbns(struct nandfs_node *, nandfs_lbn_t,
    struct nandfs_indir *, int *);

int
bmap_lookup(struct nandfs_node *node, nandfs_lbn_t lblk, nandfs_daddr_t *vblk)
{
	struct nandfs_inode *ip;
	struct nandfs_indir a[NANDFS_NIADDR + 1], *ap;
	nandfs_daddr_t daddr;
	struct buf *bp;
	int error;
	int num, *nump;

	DPRINTF(BMAP, ("%s: node %p lblk %jx enter\n", __func__, node, lblk));
	ip = &node->nn_inode;

	ap = a;
	nump = &num;

	error = bmap_getlbns(node, lblk, ap, nump);
	if (error)
		return (error);

	if (num == 0) {
		*vblk = ip->i_db[lblk];
		return (0);
	}

	DPRINTF(BMAP, ("%s: node %p lblk=%jx trying ip->i_ib[%x]\n", __func__,
	    node, lblk, ap->in_off));
	daddr = ip->i_ib[ap->in_off];
	for (bp = NULL, ++ap; --num; ap++) {
		if (daddr == 0) {
			DPRINTF(BMAP, ("%s: node %p lblk=%jx returning with "
			    "vblk 0\n", __func__, node, lblk));
			*vblk = 0;
			return (0);
		}
		if (ap->in_lbn == lblk) {
			DPRINTF(BMAP, ("%s: node %p lblk=%jx ap->in_lbn=%jx "
			    "returning address of indirect block (%jx)\n",
			    __func__, node, lblk, ap->in_lbn, daddr));
			*vblk = daddr;
			return (0);
		}

		DPRINTF(BMAP, ("%s: node %p lblk=%jx reading block "
		    "ap->in_lbn=%jx\n", __func__, node, lblk, ap->in_lbn));

		error = nandfs_bread_meta(node, ap->in_lbn, NOCRED, 0, &bp);
		if (error) {
			brelse(bp);
			return (error);
		}

		daddr = ((nandfs_daddr_t *)bp->b_data)[ap->in_off];
		brelse(bp);
	}

	DPRINTF(BMAP, ("%s: node %p lblk=%jx returning with %jx\n", __func__,
	    node, lblk, daddr));
	*vblk = daddr;

	return (0);
}

int
bmap_dirty_meta(struct nandfs_node *node, nandfs_lbn_t lblk, int force)
{
	struct nandfs_indir a[NANDFS_NIADDR+1], *ap;
#ifdef DEBUG
	nandfs_daddr_t daddr;
#endif
	struct buf *bp;
	int error;
	int num, *nump;

	DPRINTF(BMAP, ("%s: node %p lblk=%jx\n", __func__, node, lblk));

	ap = a;
	nump = &num;

	error = bmap_getlbns(node, lblk, ap, nump);
	if (error)
		return (error);

	/*
	 * Direct block, nothing to do
	 */
	if (num == 0)
		return (0);

	DPRINTF(BMAP, ("%s: node %p reading blocks\n", __func__, node));

	for (bp = NULL, ++ap; --num; ap++) {
		error = nandfs_bread_meta(node, ap->in_lbn, NOCRED, 0, &bp);
		if (error) {
			brelse(bp);
			return (error);
		}

#ifdef DEBUG
		daddr = ((nandfs_daddr_t *)bp->b_data)[ap->in_off];
		MPASS(daddr != 0 || node->nn_ino == 3);
#endif

		error = nandfs_dirty_buf_meta(bp, force);
		if (error)
			return (error);
	}

	return (0);
}

int
bmap_insert_block(struct nandfs_node *node, nandfs_lbn_t lblk,
    nandfs_daddr_t vblk)
{
	struct nandfs_inode *ip;
	struct nandfs_indir a[NANDFS_NIADDR+1], *ap;
	struct buf *bp;
	nandfs_daddr_t daddr;
	int error;
	int num, *nump, i;

	DPRINTF(BMAP, ("%s: node %p lblk=%jx vblk=%jx\n", __func__, node, lblk,
	    vblk));

	ip = &node->nn_inode;

	ap = a;
	nump = &num;

	error = bmap_getlbns(node, lblk, ap, nump);
	if (error)
		return (error);

	DPRINTF(BMAP, ("%s: node %p lblk=%jx vblk=%jx got num=%d\n", __func__,
	    node, lblk, vblk, num));

	if (num == 0) {
		DPRINTF(BMAP, ("%s: node %p lblk=%jx direct block\n", __func__,
		    node, lblk));
		ip->i_db[lblk] = vblk;
		return (0);
	}

	DPRINTF(BMAP, ("%s: node %p lblk=%jx indirect block level %d\n",
	    __func__, node, lblk, ap->in_off));

	if (num == 1) {
		DPRINTF(BMAP, ("%s: node %p lblk=%jx indirect block: inserting "
		    "%jx as vblk for indirect block %d\n", __func__, node,
		    lblk, vblk, ap->in_off));
		ip->i_ib[ap->in_off] = vblk;
		return (0);
	}

	bp = NULL;
	daddr = ip->i_ib[a[0].in_off];
	for (i = 1; i < num; i++) {
		if (bp)
			brelse(bp);
		if (daddr == 0) {
			DPRINTF(BMAP, ("%s: node %p lblk=%jx vblk=%jx create "
			    "block %jx %d\n", __func__, node, lblk, vblk,
			    a[i].in_lbn, a[i].in_off));
			error = nandfs_bcreate_meta(node, a[i].in_lbn, NOCRED,
			    0, &bp);
			if (error)
				return (error);
		} else {
			DPRINTF(BMAP, ("%s: node %p lblk=%jx vblk=%jx read "
			    "block %jx %d\n", __func__, node, daddr, vblk,
			    a[i].in_lbn, a[i].in_off));
			error = nandfs_bread_meta(node, a[i].in_lbn, NOCRED, 0, &bp);
			if (error) {
				brelse(bp);
				return (error);
			}
		}
		daddr = ((nandfs_daddr_t *)bp->b_data)[a[i].in_off];
	}
	i--;

	DPRINTF(BMAP,
	    ("%s: bmap node %p lblk=%jx vblk=%jx inserting vblk level %d at "
	    "offset %d at %jx\n", __func__, node, lblk, vblk, i, a[i].in_off,
	    daddr));

	if (!bp) {
		nandfs_error("%s: cannot find indirect block\n", __func__);
		return (-1);
	}
	((nandfs_daddr_t *)bp->b_data)[a[i].in_off] = vblk;

	error = nandfs_dirty_buf_meta(bp, 0);
	if (error) {
		nandfs_warning("%s: dirty failed buf: %p\n", __func__, bp);
		return (error);
	}
	DPRINTF(BMAP, ("%s: exiting node %p lblk=%jx vblk=%jx\n", __func__,
	    node, lblk, vblk));

	return (error);
}

CTASSERT(NANDFS_NIADDR <= 3);
#define SINGLE	0	/* index of single indirect block */
#define DOUBLE	1	/* index of double indirect block */
#define TRIPLE	2	/* index of triple indirect block */

static __inline nandfs_lbn_t
lbn_offset(struct nandfs_device *fsdev, int level)
{
	nandfs_lbn_t res;

	for (res = 1; level > 0; level--)
		res *= MNINDIR(fsdev);
	return (res);
}

static nandfs_lbn_t
blocks_inside(struct nandfs_device *fsdev, int level, struct nandfs_indir *nip)
{
	nandfs_lbn_t blocks;

	for (blocks = 1; level >= SINGLE; level--, nip++) {
		MPASS(nip->in_off >= 0 && nip->in_off < MNINDIR(fsdev));
		blocks += nip->in_off * lbn_offset(fsdev, level);
	}

	return (blocks);
}

static int
bmap_truncate_indirect(struct nandfs_node *node, int level, nandfs_lbn_t *left,
    int *cleaned, struct nandfs_indir *ap, struct nandfs_indir *fp,
    nandfs_daddr_t *copy)
{
	struct buf *bp;
	nandfs_lbn_t i, lbn, nlbn, factor, tosub;
	struct nandfs_device *fsdev;
	int error, lcleaned, modified;

	DPRINTF(BMAP, ("%s: node %p level %d left %jx\n", __func__,
	    node, level, *left));

	fsdev = node->nn_nandfsdev;

	MPASS(ap->in_off >= 0 && ap->in_off < MNINDIR(fsdev));

	factor = lbn_offset(fsdev, level);
	lbn = ap->in_lbn;

	error = nandfs_bread_meta(node, lbn, NOCRED, 0, &bp);
	if (error) {
		if (bp != NULL)
			brelse(bp);
		return (error);
	}

	bcopy(bp->b_data, copy, fsdev->nd_blocksize);
	bqrelse(bp);

	modified = 0;

	i = ap->in_off;

	if (ap != fp)
		ap++;
	for (nlbn = lbn + 1 - i * factor; i >= 0 && *left > 0; i--,
	    nlbn += factor) {
		lcleaned = 0;

		DPRINTF(BMAP,
		    ("%s: node %p i=%jx nlbn=%jx left=%jx ap=%p vblk %jx\n",
		    __func__, node, i, nlbn, *left, ap, copy[i]));

		if (copy[i] == 0) {
			tosub = blocks_inside(fsdev, level - 1, ap);
			if (tosub > *left)
				tosub = 0;

			*left -= tosub;
		} else {
			if (level > SINGLE) {
				if (ap == fp)
					ap->in_lbn = nlbn;

				error = bmap_truncate_indirect(node, level - 1,
				    left, &lcleaned, ap, fp,
				    copy + MNINDIR(fsdev));
				if (error)
					return (error);
			} else {
				error = nandfs_bdestroy(node, copy[i]);
				if (error)
					return (error);
				lcleaned = 1;
				*left -= 1;
			}
		}

		if (lcleaned) {
			if (level > SINGLE) {
				error = nandfs_vblock_end(fsdev, copy[i]);
				if (error)
					return (error);
			}
			copy[i] = 0;
			modified++;
		}

		ap = fp;
	}

	if (i == -1)
		*cleaned = 1;

	error = nandfs_bread_meta(node, lbn, NOCRED, 0, &bp);
	if (error) {
		brelse(bp);
		return (error);
	}
	if (modified)
		bcopy(copy, bp->b_data, fsdev->nd_blocksize);

	/* Force success even if we can't dirty the buffer metadata when freeing space */
	nandfs_dirty_buf_meta(bp, 1);

	return (0);
}

int
bmap_truncate_mapping(struct nandfs_node *node, nandfs_lbn_t lastblk,
    nandfs_lbn_t todo)
{
	struct nandfs_inode *ip;
	struct nandfs_indir a[NANDFS_NIADDR + 1], f[NANDFS_NIADDR], *ap;
	nandfs_daddr_t indir_lbn[NANDFS_NIADDR];
	nandfs_daddr_t *copy;
	int error, level;
	nandfs_lbn_t left, tosub;
	struct nandfs_device *fsdev;
	int cleaned, i;
	int num, *nump;

	DPRINTF(BMAP, ("%s: node %p lastblk %jx truncating by %jx\n", __func__,
	    node, lastblk, todo));

	ip = &node->nn_inode;
	fsdev = node->nn_nandfsdev;

	ap = a;
	nump = &num;

	error = bmap_getlbns(node, lastblk, ap, nump);
	if (error)
		return (error);

	indir_lbn[SINGLE] = -NANDFS_NDADDR;
	indir_lbn[DOUBLE] = indir_lbn[SINGLE] - MNINDIR(fsdev) - 1;
	indir_lbn[TRIPLE] = indir_lbn[DOUBLE] - MNINDIR(fsdev)
	    * MNINDIR(fsdev) - 1;

	for (i = 0; i < NANDFS_NIADDR; i++) {
		f[i].in_off = MNINDIR(fsdev) - 1;
		f[i].in_lbn = 0xdeadbeef;
	}

	left = todo;

#ifdef DEBUG
	a[num].in_off = -1;
#endif

	ap++;
	num -= 2;

	if (num < 0)
		goto direct;

	copy = malloc(MNINDIR(fsdev) * sizeof(nandfs_daddr_t) * (num + 1),
	    M_NANDFSTEMP, M_WAITOK);

	for (level = num; level >= SINGLE && left > 0; level--) {
		cleaned = 0;

		if (ip->i_ib[level] == 0) {
			tosub = blocks_inside(fsdev, level, ap);
			if (tosub > left)
				left = 0;
			else
				left -= tosub;
		} else {
			if (ap == f)
				ap->in_lbn = indir_lbn[level];
			error = bmap_truncate_indirect(node, level, &left,
			    &cleaned, ap, f, copy);
			if (error) {
				free(copy, M_NANDFSTEMP);
				nandfs_error("%s: error %d when truncate "
				    "at level %d\n", __func__, error, level);
				return (error);
			}
		}

		if (cleaned) {
			nandfs_vblock_end(fsdev, ip->i_ib[level]);
			ip->i_ib[level] = 0;
		}

		ap = f;
	}

	free(copy, M_NANDFSTEMP);

direct:
	if (num < 0)
		i = lastblk;
	else
		i = NANDFS_NDADDR - 1;

	for (; i >= 0 && left > 0; i--) {
		if (ip->i_db[i] != 0) {
			error = nandfs_bdestroy(node, ip->i_db[i]);
			if (error) {
				nandfs_error("%s: cannot destroy "
				    "block %jx, error %d\n", __func__,
				    (uintmax_t)ip->i_db[i], error);
				return (error);
			}
			ip->i_db[i] = 0;
		}

		left--;
	}

	KASSERT(left == 0,
	    ("truncated wrong number of blocks (%jd should be 0)", left));

	return (error);
}

nandfs_lbn_t
get_maxfilesize(struct nandfs_device *fsdev)
{
	struct nandfs_indir f[NANDFS_NIADDR];
	nandfs_lbn_t max;
	int i;

	max = NANDFS_NDADDR;

	for (i = 0; i < NANDFS_NIADDR; i++) {
		f[i].in_off = MNINDIR(fsdev) - 1;
		max += blocks_inside(fsdev, i, f);
	}

	max *= fsdev->nd_blocksize;

	return (max);
}

/*
 * This is ufs_getlbns with minor modifications.
 */
/*
 * Create an array of logical block number/offset pairs which represent the
 * path of indirect blocks required to access a data block.  The first "pair"
 * contains the logical block number of the appropriate single, double or
 * triple indirect block and the offset into the inode indirect block array.
 * Note, the logical block number of the inode single/double/triple indirect
 * block appears twice in the array, once with the offset into the i_ib and
 * once with the offset into the page itself.
 */
static int
bmap_getlbns(struct nandfs_node *node, nandfs_lbn_t bn, struct nandfs_indir *ap, int *nump)
{
	nandfs_daddr_t blockcnt;
	nandfs_lbn_t metalbn, realbn;
	struct nandfs_device *fsdev;
	int i, numlevels, off;

	fsdev = node->nn_nandfsdev;

	DPRINTF(BMAP, ("%s: node %p bn=%jx mnindir=%zd enter\n", __func__,
	    node, bn, MNINDIR(fsdev)));

	if (nump)
		*nump = 0;
	numlevels = 0;
	realbn = bn;

	if (bn < 0)
		bn = -bn;

	/* The first NANDFS_NDADDR blocks are direct blocks. */
	if (bn < NANDFS_NDADDR)
		return (0);

	/*
	 * Determine the number of levels of indirection.  After this loop
	 * is done, blockcnt indicates the number of data blocks possible
	 * at the previous level of indirection, and NANDFS_NIADDR - i is the
	 * number of levels of indirection needed to locate the requested block.
	 */
	for (blockcnt = 1, i = NANDFS_NIADDR, bn -= NANDFS_NDADDR;; i--, bn -= blockcnt) {
		DPRINTF(BMAP, ("%s: blockcnt=%jd i=%d bn=%jd\n", __func__,
		    blockcnt, i, bn));
		if (i == 0)
			return (EFBIG);
		blockcnt *= MNINDIR(fsdev);
		if (bn < blockcnt)
			break;
	}

	/* Calculate the address of the first meta-block. */
	if (realbn >= 0)
		metalbn = -(realbn - bn + NANDFS_NIADDR - i);
	else
		metalbn = -(-realbn - bn + NANDFS_NIADDR - i);

	/*
	 * At each iteration, off is the offset into the bap array which is
	 * an array of disk addresses at the current level of indirection.
	 * The logical block number and the offset in that block are stored
	 * into the argument array.
	 */
	ap->in_lbn = metalbn;
	ap->in_off = off = NANDFS_NIADDR - i;

	DPRINTF(BMAP, ("%s: initial: ap->in_lbn=%jx ap->in_off=%d\n", __func__,
	    metalbn, off));

	ap++;
	for (++numlevels; i <= NANDFS_NIADDR; i++) {
		/* If searching for a meta-data block, quit when found. */
		if (metalbn == realbn)
			break;

		blockcnt /= MNINDIR(fsdev);
		off = (bn / blockcnt) % MNINDIR(fsdev);

		++numlevels;
		ap->in_lbn = metalbn;
		ap->in_off = off;

		DPRINTF(BMAP, ("%s: in_lbn=%jx in_off=%d\n", __func__,
		    ap->in_lbn, ap->in_off));
		++ap;

		metalbn -= -1 + off * blockcnt;
	}
	if (nump)
		*nump = numlevels;

	DPRINTF(BMAP, ("%s: numlevels=%d\n", __func__, numlevels));

	return (0);
}
