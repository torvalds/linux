/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2012 Semihalf.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/namei.h>
#include <sys/rwlock.h>
#include <sys/sysctl.h>
#include <sys/vnode.h>
#include <sys/buf.h>
#include <sys/bio.h>
#include <sys/libkern.h>

#include <ddb/ddb.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>

#include <geom/geom.h>
#include <geom/geom_vfs.h>

#include <fs/nandfs/nandfs_mount.h>
#include <fs/nandfs/nandfs.h>
#include <fs/nandfs/nandfs_subr.h>

static int
nandfs_new_segment(struct nandfs_device *fsdev)
{
	int error = 0;
	uint64_t new;

	error = nandfs_alloc_segment(fsdev, &new);
	if (!error) {
		fsdev->nd_seg_num = fsdev->nd_next_seg_num;
		fsdev->nd_next_seg_num = new;
	}
	DPRINTF(SYNC, ("%s: new segment %jx next %jx error %d\n",
	    __func__, (uintmax_t)fsdev->nd_seg_num, (uintmax_t)new, error));
	if (error)
		nandfs_error("%s: cannot create segment error %d\n",
		    __func__, error);

	return (error);
}

static int
create_segment(struct nandfs_seginfo *seginfo)
{
	struct nandfs_segment *seg;
	struct nandfs_device *fsdev;
	struct nandfs_segment *prev;
	struct buf *bp;
	uint64_t start_block, curr;
	uint32_t blks_per_seg, nblocks;
	int error;

	fsdev = seginfo->fsdev;
	prev = seginfo->curseg;
	blks_per_seg = fsdev->nd_fsdata.f_blocks_per_segment;
	nblocks = fsdev->nd_last_segsum.ss_nblocks;

	if (!prev) {
		vfs_timestamp(&fsdev->nd_ts);
		/* Touch current segment */
		error = nandfs_touch_segment(fsdev, fsdev->nd_seg_num);
		if (error) {
			nandfs_error("%s: cannot preallocate segment %jx\n",
			    __func__, fsdev->nd_seg_num);
			return (error);
		}
		error = nandfs_touch_segment(fsdev, 0);
		if (error) {
			nandfs_error("%s: cannot dirty block with segment 0\n",
			    __func__);
			return (error);
		}
		start_block = fsdev->nd_last_pseg + (uint64_t)nblocks;
		/*
		 * XXX Hack
		 */
		if (blks_per_seg - (start_block % blks_per_seg) - 1 == 0)
			start_block++;
		curr = nandfs_get_segnum_of_block(fsdev, start_block);
		/* Allocate new segment if last one is full */
		if (fsdev->nd_seg_num != curr) {
			error = nandfs_new_segment(fsdev);
			if (error) {
				nandfs_error("%s: cannot create new segment\n",
				    __func__);
				return (error);
			}
			/*
			 * XXX Hack
			 */
			nandfs_get_segment_range(fsdev, fsdev->nd_seg_num, &start_block, NULL);
		}
	} else {
		nandfs_get_segment_range(fsdev, fsdev->nd_next_seg_num,
		    &start_block, NULL);

		/* Touch current segment and allocate and touch new one */
		error = nandfs_new_segment(fsdev);
		if (error) {
			nandfs_error("%s: cannot create next segment\n",
			    __func__);
			return (error);
		}

		/* Reiterate in case new buf is dirty */
		seginfo->reiterate = 1;
	}

	/* Allocate and initialize nandfs_segment structure */
	seg = malloc(sizeof(*seg), M_DEVBUF, M_WAITOK|M_ZERO);
	TAILQ_INIT(&seg->segsum);
	TAILQ_INIT(&seg->data);
	seg->fsdev = fsdev;
	seg->start_block = start_block;
	seg->num_blocks = blks_per_seg - (start_block % blks_per_seg) - 1;
	seg->seg_num = fsdev->nd_seg_num;
	seg->seg_next = fsdev->nd_next_seg_num;
	seg->segsum_blocks = 1;
	seg->bytes_left = fsdev->nd_blocksize -
	    sizeof(struct nandfs_segment_summary);
	seg->segsum_bytes = sizeof(struct nandfs_segment_summary);

	/* Allocate buffer for segment summary */
	bp = getblk(fsdev->nd_devvp, nandfs_block_to_dblock(fsdev,
	    seg->start_block), fsdev->nd_blocksize, 0, 0, 0);
	bzero(bp->b_data, seginfo->fsdev->nd_blocksize);
	bp->b_bufobj = &seginfo->fsdev->nd_devvp->v_bufobj;
	bp->b_flags |= B_MANAGED;

	/* Add buffer to segment */
	TAILQ_INSERT_TAIL(&seg->segsum, bp, b_cluster.cluster_entry);
	seg->current_off = bp->b_data + sizeof(struct nandfs_segment_summary);

	DPRINTF(SYNC, ("%s: seg %p : initial settings: start %#jx size :%#x\n",
	    __func__, seg, (uintmax_t)seg->start_block, seg->num_blocks));
	DPRINTF(SYNC, ("%s: seg->seg_num %#jx cno %#jx next %#jx\n", __func__,
	    (uintmax_t)seg->seg_num, (uintmax_t)(fsdev->nd_last_cno + 1),
	    (uintmax_t)seg->seg_next));

	if (!prev)
		LIST_INSERT_HEAD(&seginfo->seg_list, seg, seg_link);
	else
		LIST_INSERT_AFTER(prev, seg, seg_link);

	seginfo->curseg = seg;

	return (0);
}

static int
delete_segment(struct nandfs_seginfo *seginfo)
{
	struct nandfs_segment *seg, *tseg;
	struct buf *bp, *tbp;

	LIST_FOREACH_SAFE(seg, &seginfo->seg_list, seg_link, tseg) {
		TAILQ_FOREACH_SAFE(bp, &seg->segsum, b_cluster.cluster_entry,
		    tbp) {
			TAILQ_REMOVE(&seg->segsum, bp, b_cluster.cluster_entry);
			bp->b_flags &= ~B_MANAGED;
			brelse(bp);
		}

		LIST_REMOVE(seg, seg_link);
		free(seg, M_DEVBUF);
	}

	return (0);
}

static int
create_seginfo(struct nandfs_device *fsdev, struct nandfs_seginfo **seginfo)
{
	struct nandfs_seginfo *info;

	info = malloc(sizeof(*info), M_DEVBUF, M_WAITOK);

	LIST_INIT(&info->seg_list);
	info->fsdev = fsdev;
	info->curseg = NULL;
	info->blocks = 0;
	*seginfo = info;
	fsdev->nd_seginfo = info;
	return (0);
}

static int
delete_seginfo(struct nandfs_seginfo *seginfo)
{
	struct nandfs_device *nffsdev;

	nffsdev = seginfo->fsdev;
	delete_segment(seginfo);
	nffsdev->nd_seginfo = NULL;
	free(seginfo, M_DEVBUF);

	return (0);
}

static int
nandfs_create_superroot_block(struct nandfs_seginfo *seginfo,
    struct buf **newbp)
{
	struct buf *bp;
	int error;

	bp = nandfs_geteblk(seginfo->fsdev->nd_blocksize, GB_NOWAIT_BD);

	bzero(bp->b_data, seginfo->fsdev->nd_blocksize);
	bp->b_bufobj = &seginfo->fsdev->nd_devvp->v_bufobj;
	bp->b_flags |= B_MANAGED;

	if (!(seginfo->curseg) || !seginfo->curseg->num_blocks) {
		error = create_segment(seginfo);
		if (error) {
			brelse(bp);
			nandfs_error("%s: no segment for superroot\n",
			    __func__);
			return (error);
		}
	}

	TAILQ_INSERT_TAIL(&seginfo->curseg->data, bp, b_cluster.cluster_entry);

	seginfo->curseg->nblocks++;
	seginfo->curseg->num_blocks--;
	seginfo->blocks++;

	*newbp = bp;
	return (0);
}

static int
nandfs_add_superroot(struct nandfs_seginfo *seginfo)
{
	struct nandfs_device *fsdev;
	struct nandfs_super_root *sr;
	struct buf *bp = NULL;
	uint64_t crc_skip;
	uint32_t crc_calc;
	int error;

	fsdev = seginfo->fsdev;

	error = nandfs_create_superroot_block(seginfo, &bp);
	if (error) {
		nandfs_error("%s: cannot add superroot\n", __func__);
		return (error);
	}

	sr = (struct nandfs_super_root *)bp->b_data;
	/* Save superroot CRC */
	sr->sr_bytes = NANDFS_SR_BYTES;
	sr->sr_flags = 0;
	sr->sr_nongc_ctime = 0;

	memcpy(&sr->sr_dat, &fsdev->nd_dat_node->nn_inode,
	    sizeof(struct nandfs_inode));
	memcpy(&sr->sr_cpfile, &fsdev->nd_cp_node->nn_inode,
	    sizeof(struct nandfs_inode));
	memcpy(&sr->sr_sufile, &fsdev->nd_su_node->nn_inode,
	    sizeof(struct nandfs_inode));

	crc_skip = sizeof(sr->sr_sum);
	crc_calc = crc32((uint8_t *)sr + crc_skip, NANDFS_SR_BYTES - crc_skip);

	sr->sr_sum = crc_calc;

	bp->b_flags |= B_MANAGED;
	bp->b_bufobj = &seginfo->fsdev->nd_devvp->v_bufobj;

	bp->b_flags &= ~B_INVAL;
	nandfs_dirty_bufs_increment(fsdev);
	DPRINTF(SYNC, ("%s: bp:%p\n", __func__, bp));

	return (0);
}

static int
nandfs_add_segsum_block(struct nandfs_seginfo *seginfo, struct buf **newbp)
{
	struct nandfs_device *fsdev;
	nandfs_daddr_t blk;
	struct buf *bp;
	int error;

	if (!(seginfo->curseg) || seginfo->curseg->num_blocks <= 1) {
		error = create_segment(seginfo);
		if (error) {
			nandfs_error("%s: error:%d when creating segment\n",
			    __func__, error);
			return (error);
		}
		*newbp = TAILQ_FIRST(&seginfo->curseg->segsum);
		return (0);
	}

	fsdev = seginfo->fsdev;
	blk = nandfs_block_to_dblock(fsdev, seginfo->curseg->start_block +
	    seginfo->curseg->segsum_blocks);

	bp = getblk(fsdev->nd_devvp, blk, fsdev->nd_blocksize, 0, 0, 0);

	bzero(bp->b_data, seginfo->fsdev->nd_blocksize);
	bp->b_bufobj = &seginfo->fsdev->nd_devvp->v_bufobj;
	bp->b_flags |= B_MANAGED;

	TAILQ_INSERT_TAIL(&seginfo->curseg->segsum, bp,
	    b_cluster.cluster_entry);
	seginfo->curseg->num_blocks--;

	seginfo->curseg->segsum_blocks++;
	seginfo->curseg->bytes_left = seginfo->fsdev->nd_blocksize;
	seginfo->curseg->current_off = bp->b_data;
	seginfo->blocks++;

	*newbp = bp;

	DPRINTF(SYNC, ("%s: bp %p\n", __func__, bp));

	return (0);
}

static int
nandfs_add_blocks(struct nandfs_seginfo *seginfo, struct nandfs_node *node,
    struct buf *bp)
{
	union nandfs_binfo *binfo;
	struct buf *seg_bp;
	int error;

	if (!(seginfo->curseg) || !seginfo->curseg->num_blocks) {
		error = create_segment(seginfo);
		if (error) {
			nandfs_error("%s: error:%d when creating segment\n",
			    __func__, error);
			return (error);
		}
	}

	if (seginfo->curseg->bytes_left < sizeof(union nandfs_binfo)) {
		error = nandfs_add_segsum_block(seginfo, &seg_bp);
		if (error) {
			nandfs_error("%s: error:%d when adding segsum\n",
			    __func__, error);
			return (error);
		}
	}
	binfo = (union nandfs_binfo *)seginfo->curseg->current_off;

	if (node->nn_ino != NANDFS_DAT_INO) {
		binfo->bi_v.bi_blkoff = bp->b_lblkno;
		binfo->bi_v.bi_ino = node->nn_ino;
	} else {
		binfo->bi_dat.bi_blkoff = bp->b_lblkno;
		binfo->bi_dat.bi_ino = node->nn_ino;
		if (NANDFS_IS_INDIRECT(bp))
			binfo->bi_dat.bi_level = 1;
		else
			binfo->bi_dat.bi_level = 0;
	}
	binfo++;

	seginfo->curseg->bytes_left -= sizeof(union nandfs_binfo);
	seginfo->curseg->segsum_bytes += sizeof(union nandfs_binfo);
	seginfo->curseg->current_off = (char *)binfo;

	TAILQ_INSERT_TAIL(&seginfo->curseg->data, bp, b_cluster.cluster_entry);

	seginfo->curseg->nbinfos++;
	seginfo->curseg->nblocks++;
	seginfo->curseg->num_blocks--;
	seginfo->blocks++;

	DPRINTF(SYNC, ("%s: bp (%p) number %x (left %x)\n",
	    __func__, bp, seginfo->curseg->nblocks,
	    seginfo->curseg->num_blocks));
	return (0);
}

static int
nandfs_iterate_dirty_buf(struct vnode *vp, struct nandfs_seginfo *seginfo,
    uint8_t hold)
{
	struct buf *bp, *tbd;
	struct bufobj *bo;
	struct nandfs_node *node;
	int error;

	node = VTON(vp);
	bo = &vp->v_bufobj;

	ASSERT_VOP_ELOCKED(vp, __func__);

	/* Iterate dirty data bufs */
	TAILQ_FOREACH_SAFE(bp, &bo->bo_dirty.bv_hd, b_bobufs, tbd) {
		DPRINTF(SYNC, ("%s: vp (%p): bp (%p) with lblkno %jx ino %jx "
		    "add buf\n", __func__, vp, bp, bp->b_lblkno, node->nn_ino));

		if (!(NANDFS_ISGATHERED(bp))) {
			error = nandfs_bmap_update_dat(node,
			    nandfs_vblk_get(bp), bp);
			if (error)
				return (error);
			NANDFS_GATHER(bp);
			nandfs_add_blocks(seginfo, node, bp);
		}
	}

	return (0);
}

static int
nandfs_iterate_system_vnode(struct nandfs_node *node,
    struct nandfs_seginfo *seginfo)
{
	struct vnode *vp;
	int nblocks;
	uint8_t hold = 0;

	if (node->nn_ino != NANDFS_IFILE_INO)
		hold = 1;

	vp = NTOV(node);

	nblocks = vp->v_bufobj.bo_dirty.bv_cnt;
	DPRINTF(SYNC, ("%s: vp (%p): nblocks %x ino %jx\n",
	    __func__, vp, nblocks, node->nn_ino));

	if (nblocks)
		nandfs_iterate_dirty_buf(vp, seginfo, hold);

	return (0);
}

static int
nandfs_iterate_dirty_vnodes(struct mount *mp, struct nandfs_seginfo *seginfo)
{
	struct nandfs_node *nandfs_node;
	struct vnode *vp, *mvp;
	struct thread *td;
	struct bufobj *bo;
	int error, update;

	td = curthread;

	MNT_VNODE_FOREACH_ACTIVE(vp, mp, mvp) {
		update = 0;

		if (mp->mnt_syncer == vp || VOP_ISLOCKED(vp)) {
			VI_UNLOCK(vp);
			continue;
		}
		if (vget(vp, LK_EXCLUSIVE | LK_INTERLOCK | LK_NOWAIT, td) != 0)
			continue;

		nandfs_node = VTON(vp);
		if (nandfs_node->nn_flags & IN_MODIFIED) {
			nandfs_node->nn_flags &= ~(IN_MODIFIED);
			update = 1;
		}

		bo = &vp->v_bufobj;
		BO_LOCK(bo);
		if (vp->v_bufobj.bo_dirty.bv_cnt) {
			error = nandfs_iterate_dirty_buf(vp, seginfo, 0);
			if (error) {
				nandfs_error("%s: cannot iterate vnode:%p "
				    "err:%d\n", __func__, vp, error);
				vput(vp);
				BO_UNLOCK(bo);
				return (error);
			}
			update = 1;
		} else
			vput(vp);
		BO_UNLOCK(bo);

		if (update)
			nandfs_node_update(nandfs_node);
	}

	return (0);
}

static int
nandfs_update_phys_block(struct nandfs_device *fsdev, struct buf *bp,
    uint64_t phys_blknr, union nandfs_binfo *binfo)
{
	struct nandfs_node *node, *dat;
	struct vnode *vp;
	uint64_t new_blknr;
	int error;

	vp = bp->b_vp;
	node = VTON(vp);
	new_blknr = nandfs_vblk_get(bp);
	dat = fsdev->nd_dat_node;

	DPRINTF(BMAP, ("%s: ino %#jx lblk %#jx: vblk %#jx -> %#jx\n",
	    __func__, (uintmax_t)node->nn_ino, (uintmax_t)bp->b_lblkno,
	    (uintmax_t)new_blknr, (uintmax_t)phys_blknr));

	if (node->nn_ino != NANDFS_DAT_INO) {
		KASSERT((new_blknr != 0), ("vblk for bp %p is 0", bp));

		nandfs_vblock_assign(fsdev, new_blknr, phys_blknr);
		binfo->bi_v.bi_vblocknr = new_blknr;
		binfo->bi_v.bi_blkoff = bp->b_lblkno;
		binfo->bi_v.bi_ino = node->nn_ino;
	} else {
		VOP_LOCK(NTOV(dat), LK_EXCLUSIVE);
		error = nandfs_bmap_update_block(node, bp, phys_blknr);
		if (error) {
			nandfs_error("%s: error updating block:%jx for bp:%p\n",
			    __func__, (uintmax_t)phys_blknr, bp);
			VOP_UNLOCK(NTOV(dat), 0);
			return (error);
		}
		VOP_UNLOCK(NTOV(dat), 0);
		binfo->bi_dat.bi_blkoff = bp->b_lblkno;
		binfo->bi_dat.bi_ino = node->nn_ino;
		if (NANDFS_IS_INDIRECT(bp))
			binfo->bi_dat.bi_level = 1;
		else
			binfo->bi_dat.bi_level = 0;
	}

	return (0);
}

#define	NBINFO(off) ((off) + sizeof(union nandfs_binfo))
static int
nandfs_segment_assign_pblk(struct nandfs_segment *nfsseg)
{
	struct nandfs_device *fsdev;
	union nandfs_binfo *binfo;
	struct buf *bp, *seg_bp;
	uint64_t blocknr;
	uint32_t curr_off, blocksize;
	int error;

	fsdev = nfsseg->fsdev;
	blocksize = fsdev->nd_blocksize;

	blocknr = nfsseg->start_block + nfsseg->segsum_blocks;
	seg_bp = TAILQ_FIRST(&nfsseg->segsum);
	DPRINTF(SYNC, ("%s: seg:%p segsum bp:%p data:%p\n",
	    __func__, nfsseg, seg_bp, seg_bp->b_data));

	binfo = (union nandfs_binfo *)(seg_bp->b_data +
	    sizeof(struct nandfs_segment_summary));
	curr_off = sizeof(struct nandfs_segment_summary);

	TAILQ_FOREACH(bp, &nfsseg->data, b_cluster.cluster_entry) {
		KASSERT((bp->b_vp), ("bp %p has not vp", bp));

		DPRINTF(BMAP, ("\n\n%s: assign buf %p for ino %#jx next %p\n",
		    __func__, bp, (uintmax_t)VTON(bp->b_vp)->nn_ino,
		    TAILQ_NEXT(bp, b_cluster.cluster_entry)));

		if (NBINFO(curr_off) > blocksize) {
			seg_bp = TAILQ_NEXT(seg_bp, b_cluster.cluster_entry);
			binfo = (union nandfs_binfo *)seg_bp->b_data;
			curr_off = 0;
			DPRINTF(SYNC, ("%s: next segsum %p data %p\n",
			    __func__, seg_bp, seg_bp->b_data));
		}

		error = nandfs_update_phys_block(fsdev, bp, blocknr, binfo);
		if (error) {
			nandfs_error("%s: err:%d when updatinng phys block:%jx"
			    " for bp:%p and binfo:%p\n", __func__, error,
			    (uintmax_t)blocknr, bp, binfo);
			return (error);
		}
		binfo++;
		curr_off = NBINFO(curr_off);

		blocknr++;
	}

	return (0);
}

static int
nandfs_seginfo_assign_pblk(struct nandfs_seginfo *seginfo)
{
	struct nandfs_segment *nfsseg;
	int error = 0;

	LIST_FOREACH(nfsseg, &seginfo->seg_list, seg_link) {
		error = nandfs_segment_assign_pblk(nfsseg);
		if (error)
			break;
	}

	return (error);
}

static struct nandfs_segment_summary *
nandfs_fill_segsum(struct nandfs_segment *seg, int has_sr)
{
	struct nandfs_segment_summary *ss;
	struct nandfs_device *fsdev;
	struct buf *bp;
	uint32_t rest, segsum_size, blocksize, crc_calc;
	uint16_t flags;
	uint8_t *crc_area, crc_skip;

	DPRINTF(SYNC, ("%s: seg %#jx nblocks %#x sumbytes %#x\n",
	    __func__, (uintmax_t) seg->seg_num,
	    seg->nblocks + seg->segsum_blocks,
	    seg->segsum_bytes));

	fsdev = seg->fsdev;

	flags = NANDFS_SS_LOGBGN | NANDFS_SS_LOGEND;
	if (has_sr)
		flags |= NANDFS_SS_SR;

	bp = TAILQ_FIRST(&seg->segsum);
	ss = (struct nandfs_segment_summary *) bp->b_data;
	ss->ss_magic = NANDFS_SEGSUM_MAGIC;
	ss->ss_bytes = sizeof(struct nandfs_segment_summary);
	ss->ss_flags = flags;
	ss->ss_seq = ++(fsdev->nd_seg_sequence);
	ss->ss_create = fsdev->nd_ts.tv_sec;
	nandfs_get_segment_range(fsdev, seg->seg_next, &ss->ss_next, NULL);
	ss->ss_nblocks = seg->nblocks + seg->segsum_blocks;
	ss->ss_nbinfos = seg->nbinfos;
	ss->ss_sumbytes = seg->segsum_bytes;

	crc_skip = sizeof(ss->ss_datasum) + sizeof(ss->ss_sumsum);
	blocksize = seg->fsdev->nd_blocksize;

	segsum_size = seg->segsum_bytes - crc_skip;
	rest = min(seg->segsum_bytes, blocksize) - crc_skip;
	crc_area = (uint8_t *)ss + crc_skip;
	crc_calc = ~0U;
	while (segsum_size > 0) {
		crc_calc = crc32_raw(crc_area, rest, crc_calc);
		segsum_size -= rest;
		if (!segsum_size)
			break;
		bp = TAILQ_NEXT(bp, b_cluster.cluster_entry);
		crc_area = (uint8_t *)bp->b_data;
		rest = segsum_size <= blocksize ? segsum_size : blocksize;
	}
	ss->ss_sumsum = crc_calc ^ ~0U;

	return (ss);

}

static int
nandfs_save_buf(struct buf *bp, uint64_t blocknr, struct nandfs_device *fsdev)
{
	struct bufobj *bo;
	int error;

	bo = &fsdev->nd_devvp->v_bufobj;

	bp->b_blkno = nandfs_block_to_dblock(fsdev, blocknr);
	bp->b_iooffset = dbtob(bp->b_blkno);

	KASSERT(bp->b_bufobj != NULL, ("no bufobj for %p", bp));
	if (bp->b_bufobj != bo) {
		BO_LOCK(bp->b_bufobj);
		BUF_LOCK(bp, LK_EXCLUSIVE | LK_NOWAIT | LK_INTERLOCK,
		    BO_LOCKPTR(bp->b_bufobj));
		KASSERT(BUF_ISLOCKED(bp), ("Problem with locking buffer"));
	}

	DPRINTF(SYNC, ("%s: buf: %p offset %#jx blk %#jx size %#x\n",
	    __func__, bp, (uintmax_t)bp->b_offset, (uintmax_t)blocknr,
	    fsdev->nd_blocksize));

	NANDFS_UNGATHER(bp);
	nandfs_buf_clear(bp, 0xffffffff);
	bp->b_flags &= ~(B_ASYNC|B_INVAL|B_MANAGED);
	error = bwrite(bp);
	if (error) {
		nandfs_error("%s: error:%d when writing buffer:%p\n",
		    __func__, error, bp);
		return (error);
	}
	return (error);
}

static void
nandfs_clean_buf(struct nandfs_device *fsdev, struct buf *bp)
{

	DPRINTF(SYNC, ("%s: buf: %p\n", __func__, bp));

	NANDFS_UNGATHER(bp);
	nandfs_buf_clear(bp, 0xffffffff);
	bp->b_flags &= ~(B_ASYNC|B_INVAL|B_MANAGED);
	nandfs_undirty_buf_fsdev(fsdev, bp);
}

static void
nandfs_clean_segblocks(struct nandfs_segment *seg, uint8_t unlock)
{
	struct nandfs_device *fsdev = seg->fsdev;
	struct nandfs_segment *next_seg;
	struct buf *bp, *tbp, *next_bp;
	struct vnode *vp, *next_vp;

	VOP_LOCK(fsdev->nd_devvp, LK_EXCLUSIVE);
	TAILQ_FOREACH_SAFE(bp, &seg->segsum, b_cluster.cluster_entry, tbp) {
		TAILQ_REMOVE(&seg->segsum, bp, b_cluster.cluster_entry);
		nandfs_clean_buf(fsdev, bp);
	}

	TAILQ_FOREACH_SAFE(bp, &seg->data, b_cluster.cluster_entry, tbp) {
		TAILQ_REMOVE(&seg->data, bp, b_cluster.cluster_entry);

		/*
		 * If bp is not super-root and vnode is not currently
		 * locked lock it.
		 */
		vp = bp->b_vp;
		next_vp = NULL;
		next_bp = TAILQ_NEXT(bp,  b_cluster.cluster_entry);
		if (!next_bp) {
			next_seg = LIST_NEXT(seg, seg_link);
			if (next_seg)
				next_bp = TAILQ_FIRST(&next_seg->data);
		}

		if (next_bp)
			next_vp = next_bp->b_vp;

		nandfs_clean_buf(fsdev, bp);

		if (unlock && vp != NULL && next_vp != vp &&
		    !NANDFS_SYS_NODE(VTON(vp)->nn_ino))
			vput(vp);

		nandfs_dirty_bufs_decrement(fsdev);
	}

	VOP_UNLOCK(fsdev->nd_devvp, 0);
}

static int
nandfs_save_segblocks(struct nandfs_segment *seg, uint8_t unlock)
{
	struct nandfs_device *fsdev = seg->fsdev;
	struct nandfs_segment *next_seg;
	struct buf *bp, *tbp, *next_bp;
	struct vnode *vp, *next_vp;
	uint64_t blocknr;
	uint32_t i = 0;
	int error = 0;

	VOP_LOCK(fsdev->nd_devvp, LK_EXCLUSIVE);
	TAILQ_FOREACH_SAFE(bp, &seg->segsum, b_cluster.cluster_entry, tbp) {
		TAILQ_REMOVE(&seg->segsum, bp, b_cluster.cluster_entry);
		blocknr = seg->start_block + i;
		error = nandfs_save_buf(bp, blocknr, fsdev);
		if (error) {
			nandfs_error("%s: error saving buf: %p blocknr:%jx\n",
			    __func__, bp, (uintmax_t)blocknr);
			goto out;
		}
		i++;
	}

	i = 0;
	TAILQ_FOREACH_SAFE(bp, &seg->data, b_cluster.cluster_entry, tbp) {
		TAILQ_REMOVE(&seg->data, bp, b_cluster.cluster_entry);

		blocknr = seg->start_block + seg->segsum_blocks + i;
		/*
		 * If bp is not super-root and vnode is not currently
		 * locked lock it.
		 */
		vp = bp->b_vp;
		next_vp = NULL;
		next_bp = TAILQ_NEXT(bp,  b_cluster.cluster_entry);
		if (!next_bp) {
			next_seg = LIST_NEXT(seg, seg_link);
			if (next_seg)
				next_bp = TAILQ_FIRST(&next_seg->data);
		}

		if (next_bp)
			next_vp = next_bp->b_vp;

		error = nandfs_save_buf(bp, blocknr, fsdev);
		if (error) {
			nandfs_error("%s: error saving buf: %p blknr: %jx\n",
			    __func__, bp, (uintmax_t)blocknr);
			if (unlock && vp != NULL && next_vp != vp &&
			    !NANDFS_SYS_NODE(VTON(vp)->nn_ino))
				vput(vp);
			goto out;
		}

		if (unlock && vp != NULL && next_vp != vp &&
		    !NANDFS_SYS_NODE(VTON(vp)->nn_ino))
			vput(vp);

		i++;
		nandfs_dirty_bufs_decrement(fsdev);
	}
out:
	if (error) {
		nandfs_clean_segblocks(seg, unlock);
		VOP_UNLOCK(fsdev->nd_devvp, 0);
		return (error);
	}

	VOP_UNLOCK(fsdev->nd_devvp, 0);
	return (error);
}


static void
clean_seginfo(struct nandfs_seginfo *seginfo, uint8_t unlock)
{
	struct nandfs_segment *seg;

	DPRINTF(SYNC, ("%s: seginfo %p\n", __func__, seginfo));

	LIST_FOREACH(seg, &seginfo->seg_list, seg_link) {
		nandfs_clean_segblocks(seg, unlock);
	}
}

static int
save_seginfo(struct nandfs_seginfo *seginfo, uint8_t unlock)
{
	struct nandfs_segment *seg;
	struct nandfs_device *fsdev;
	struct nandfs_segment_summary *ss;
	int error = 0;

	fsdev = seginfo->fsdev;

	DPRINTF(SYNC, ("%s: seginfo %p\n", __func__, seginfo));

	LIST_FOREACH(seg, &seginfo->seg_list, seg_link) {
		if (LIST_NEXT(seg, seg_link)) {
			nandfs_fill_segsum(seg, 0);
			error = nandfs_save_segblocks(seg, unlock);
			if (error) {
				nandfs_error("%s: error:%d saving seg:%p\n",
				    __func__, error, seg);
				goto out;
			}
		} else {
			ss = nandfs_fill_segsum(seg, 1);
			fsdev->nd_last_segsum = *ss;
			error = nandfs_save_segblocks(seg, unlock);
			if (error) {
				nandfs_error("%s: error:%d saving seg:%p\n",
				    __func__, error, seg);
				goto out;
			}
			fsdev->nd_last_cno++;
			fsdev->nd_last_pseg = seg->start_block;
		}
	}
out:
	if (error)
		clean_seginfo(seginfo, unlock);
	return (error);
}

static void
nandfs_invalidate_bufs(struct nandfs_device *fsdev, uint64_t segno)
{
	uint64_t start, end;
	struct buf *bp, *tbd;
	struct bufobj *bo;

	nandfs_get_segment_range(fsdev, segno, &start, &end);

	bo = &NTOV(fsdev->nd_gc_node)->v_bufobj;

	BO_LOCK(bo);
restart_locked_gc:
	TAILQ_FOREACH_SAFE(bp, &bo->bo_clean.bv_hd, b_bobufs, tbd) {
		if (!(bp->b_lblkno >= start && bp->b_lblkno <= end))
			continue;

		if (BUF_LOCK(bp, LK_EXCLUSIVE | LK_NOWAIT, NULL))
			goto restart_locked_gc;

		bremfree(bp);
		bp->b_flags |= (B_INVAL | B_RELBUF);
		bp->b_flags &= ~(B_ASYNC | B_MANAGED);
		BO_UNLOCK(bo);
		brelse(bp);
		BO_LOCK(bo);
	}
	BO_UNLOCK(bo);
}

/* Process segments marks to free by cleaner */
static void
nandfs_process_segments(struct nandfs_device *fsdev)
{
	uint64_t saved_segment;
	int i;

	if (fsdev->nd_free_base) {
		saved_segment = nandfs_get_segnum_of_block(fsdev,
		    fsdev->nd_super.s_last_pseg);
		for (i = 0; i < fsdev->nd_free_count; i++) {
			if (fsdev->nd_free_base[i] == NANDFS_NOSEGMENT)
				continue;
			/* Update superblock if clearing segment point by it */
			if (fsdev->nd_free_base[i] == saved_segment) {
				nandfs_write_superblock(fsdev);
				saved_segment = nandfs_get_segnum_of_block(
				    fsdev, fsdev->nd_super.s_last_pseg);
			}
			nandfs_invalidate_bufs(fsdev, fsdev->nd_free_base[i]);
			nandfs_clear_segment(fsdev, fsdev->nd_free_base[i]);
		}

		free(fsdev->nd_free_base, M_NANDFSTEMP);
		fsdev->nd_free_base = NULL;
		fsdev->nd_free_count = 0;
	}
}

/* Collect and write dirty buffers */
int
nandfs_sync_file(struct vnode *vp)
{
	struct nandfs_device *fsdev;
	struct nandfs_node *nandfs_node;
	struct nandfsmount *nmp;
	struct nandfs_node *dat, *su, *ifile, *cp;
	struct nandfs_seginfo *seginfo = NULL;
	struct nandfs_segment *seg;
	int update, error;
	int cno_changed;

	ASSERT_VOP_LOCKED(vp, __func__);
	DPRINTF(SYNC, ("%s: START\n", __func__));

	error = 0;
	nmp = VFSTONANDFS(vp->v_mount);
	fsdev = nmp->nm_nandfsdev;

	dat = fsdev->nd_dat_node;
	su = fsdev->nd_su_node;
	cp = fsdev->nd_cp_node;
	ifile = nmp->nm_ifile_node;

	NANDFS_WRITEASSERT(fsdev);
	if (lockmgr(&fsdev->nd_seg_const, LK_UPGRADE, NULL) != 0) {
		DPRINTF(SYNC, ("%s: lost shared lock\n", __func__));
		if (lockmgr(&fsdev->nd_seg_const, LK_EXCLUSIVE, NULL) != 0)
			panic("couldn't lock exclusive");
	}
	DPRINTF(SYNC, ("%s: got lock\n", __func__));

	VOP_LOCK(NTOV(su), LK_EXCLUSIVE);
	create_seginfo(fsdev, &seginfo);

	update = 0;

	nandfs_node = VTON(vp);
	if (nandfs_node->nn_flags & IN_MODIFIED) {
		nandfs_node->nn_flags &= ~(IN_MODIFIED);
		update = 1;
	}

	if (vp->v_bufobj.bo_dirty.bv_cnt) {
		error = nandfs_iterate_dirty_buf(vp, seginfo, 0);
		if (error) {
			clean_seginfo(seginfo, 0);
			delete_seginfo(seginfo);
			VOP_UNLOCK(NTOV(su), 0);
			lockmgr(&fsdev->nd_seg_const, LK_DOWNGRADE, NULL);
			nandfs_error("%s: err:%d iterating dirty bufs vp:%p",
			    __func__, error, vp);
			return (error);
		}
		update = 1;
	}

	if (update) {
		VOP_LOCK(NTOV(ifile), LK_EXCLUSIVE);
		error = nandfs_node_update(nandfs_node);
		if (error) {
			clean_seginfo(seginfo, 0);
			delete_seginfo(seginfo);
			VOP_UNLOCK(NTOV(ifile), 0);
			VOP_UNLOCK(NTOV(su), 0);
			lockmgr(&fsdev->nd_seg_const, LK_DOWNGRADE, NULL);
			nandfs_error("%s: err:%d updating vp:%p",
			    __func__, error, vp);
			return (error);
		}
		VOP_UNLOCK(NTOV(ifile), 0);
	}

	cno_changed = 0;
	if (seginfo->blocks) {
		VOP_LOCK(NTOV(cp), LK_EXCLUSIVE);
		cno_changed = 1;
		/* Create new checkpoint */
		error = nandfs_get_checkpoint(fsdev, cp, fsdev->nd_last_cno + 1);
		if (error) {
			clean_seginfo(seginfo, 0);
			delete_seginfo(seginfo);
			VOP_UNLOCK(NTOV(cp), 0);
			VOP_UNLOCK(NTOV(su), 0);
			lockmgr(&fsdev->nd_seg_const, LK_DOWNGRADE, NULL);
			nandfs_error("%s: err:%d getting cp:%jx",
			    __func__, error, fsdev->nd_last_cno + 1);
			return (error);
		}

		/* Reiterate all blocks and assign physical block number */
		nandfs_seginfo_assign_pblk(seginfo);

		/* Fill checkpoint data */
		error = nandfs_set_checkpoint(fsdev, cp, fsdev->nd_last_cno + 1,
		    &ifile->nn_inode, seginfo->blocks);
		if (error) {
			clean_seginfo(seginfo, 0);
			delete_seginfo(seginfo);
			VOP_UNLOCK(NTOV(cp), 0);
			VOP_UNLOCK(NTOV(su), 0);
			lockmgr(&fsdev->nd_seg_const, LK_DOWNGRADE, NULL);
			nandfs_error("%s: err:%d setting cp:%jx",
			    __func__, error, fsdev->nd_last_cno + 1);
			return (error);
		}

		VOP_UNLOCK(NTOV(cp), 0);
		LIST_FOREACH(seg, &seginfo->seg_list, seg_link)
			nandfs_update_segment(fsdev, seg->seg_num,
			    seg->nblocks + seg->segsum_blocks);

		VOP_LOCK(NTOV(dat), LK_EXCLUSIVE);
		error = save_seginfo(seginfo, 0);
		if (error) {
			clean_seginfo(seginfo, 0);
			delete_seginfo(seginfo);
			VOP_UNLOCK(NTOV(dat), 0);
			VOP_UNLOCK(NTOV(su), 0);
			lockmgr(&fsdev->nd_seg_const, LK_DOWNGRADE, NULL);
			nandfs_error("%s: err:%d updating seg",
			    __func__, error);
			return (error);
		}
		VOP_UNLOCK(NTOV(dat), 0);
	}

	VOP_UNLOCK(NTOV(su), 0);

	delete_seginfo(seginfo);
	lockmgr(&fsdev->nd_seg_const, LK_DOWNGRADE, NULL);

	if (cno_changed && !error) {
		if (nandfs_cps_between_sblocks != 0 &&
		    fsdev->nd_last_cno % nandfs_cps_between_sblocks == 0)
			nandfs_write_superblock(fsdev);
	}

	ASSERT_VOP_LOCKED(vp, __func__);
	DPRINTF(SYNC, ("%s: END error %d\n", __func__, error));
	return (error);
}

int
nandfs_segment_constructor(struct nandfsmount *nmp, int flags)
{
	struct nandfs_device *fsdev;
	struct nandfs_seginfo *seginfo = NULL;
	struct nandfs_segment *seg;
	struct nandfs_node *dat, *su, *ifile, *cp, *gc;
	int cno_changed, error;

	DPRINTF(SYNC, ("%s: START\n", __func__));
	fsdev = nmp->nm_nandfsdev;

	lockmgr(&fsdev->nd_seg_const, LK_EXCLUSIVE, NULL);
	DPRINTF(SYNC, ("%s: git lock\n", __func__));
again:
	create_seginfo(fsdev, &seginfo);

	dat = fsdev->nd_dat_node;
	su = fsdev->nd_su_node;
	cp = fsdev->nd_cp_node;
	gc = fsdev->nd_gc_node;
	ifile = nmp->nm_ifile_node;

	VOP_LOCK(NTOV(su), LK_EXCLUSIVE);
	VOP_LOCK(NTOV(ifile), LK_EXCLUSIVE);
	VOP_LOCK(NTOV(gc), LK_EXCLUSIVE);
	VOP_LOCK(NTOV(cp), LK_EXCLUSIVE);

	nandfs_iterate_system_vnode(gc, seginfo);
	nandfs_iterate_dirty_vnodes(nmp->nm_vfs_mountp, seginfo);
	nandfs_iterate_system_vnode(ifile, seginfo);
	nandfs_iterate_system_vnode(su, seginfo);

	cno_changed = 0;
	if (seginfo->blocks || flags) {
		cno_changed = 1;
		/* Create new checkpoint */
		error = nandfs_get_checkpoint(fsdev, cp, fsdev->nd_last_cno + 1);
		if (error) {
			clean_seginfo(seginfo, 0);
			delete_seginfo(seginfo);
			goto error_locks;
		}

		/* Collect blocks from system files */
		nandfs_iterate_system_vnode(cp, seginfo);
		nandfs_iterate_system_vnode(su, seginfo);
		VOP_LOCK(NTOV(dat), LK_EXCLUSIVE);
		nandfs_iterate_system_vnode(dat, seginfo);
		VOP_UNLOCK(NTOV(dat), 0);
reiterate:
		seginfo->reiterate = 0;
		nandfs_iterate_system_vnode(su, seginfo);
		VOP_LOCK(NTOV(dat), LK_EXCLUSIVE);
		nandfs_iterate_system_vnode(dat, seginfo);
		VOP_UNLOCK(NTOV(dat), 0);
		if (seginfo->reiterate)
			goto reiterate;
		if (!(seginfo->curseg) || !seginfo->curseg->num_blocks) {
			error = create_segment(seginfo);
			if (error) {
				clean_seginfo(seginfo, 0);
				delete_seginfo(seginfo);
				goto error_locks;
			}
			goto reiterate;
		}

		/* Reiterate all blocks and assign physical block number */
		nandfs_seginfo_assign_pblk(seginfo);

		/* Fill superroot */
		error = nandfs_add_superroot(seginfo);
		if (error) {
			clean_seginfo(seginfo, 0);
			delete_seginfo(seginfo);
			goto error_locks;
		}
		KASSERT(!(seginfo->reiterate), ("reiteration after superroot"));

		/* Fill checkpoint data */
		nandfs_set_checkpoint(fsdev, cp, fsdev->nd_last_cno + 1,
		    &ifile->nn_inode, seginfo->blocks);

		LIST_FOREACH(seg, &seginfo->seg_list, seg_link)
			nandfs_update_segment(fsdev, seg->seg_num,
			    seg->nblocks + seg->segsum_blocks);

		VOP_LOCK(NTOV(dat), LK_EXCLUSIVE);
		error = save_seginfo(seginfo, 1);
		if (error) {
			clean_seginfo(seginfo, 1);
			delete_seginfo(seginfo);
			goto error_dat;
		}
		VOP_UNLOCK(NTOV(dat), 0);
	}

	VOP_UNLOCK(NTOV(cp), 0);
	VOP_UNLOCK(NTOV(gc), 0);
	VOP_UNLOCK(NTOV(ifile), 0);

	nandfs_process_segments(fsdev);

	VOP_UNLOCK(NTOV(su), 0);

	delete_seginfo(seginfo);

	/*
	 * XXX: a hack, will go away soon
	 */
	if ((NTOV(dat)->v_bufobj.bo_dirty.bv_cnt != 0 ||
	    NTOV(cp)->v_bufobj.bo_dirty.bv_cnt != 0 ||
	    NTOV(gc)->v_bufobj.bo_dirty.bv_cnt != 0 ||
	    NTOV(ifile)->v_bufobj.bo_dirty.bv_cnt != 0 ||
	    NTOV(su)->v_bufobj.bo_dirty.bv_cnt != 0) &&
	    (flags & NANDFS_UMOUNT)) {
		DPRINTF(SYNC, ("%s: RERUN\n", __func__));
		goto again;
	}

	MPASS(fsdev->nd_free_base == NULL);

	lockmgr(&fsdev->nd_seg_const, LK_RELEASE, NULL);

	if (cno_changed) {
		if ((nandfs_cps_between_sblocks != 0 &&
		    fsdev->nd_last_cno % nandfs_cps_between_sblocks == 0) ||
		    flags & NANDFS_UMOUNT)
			nandfs_write_superblock(fsdev);
	}

	DPRINTF(SYNC, ("%s: END\n", __func__));
	return (0);
error_dat:
	VOP_UNLOCK(NTOV(dat), 0);
error_locks:
	VOP_UNLOCK(NTOV(cp), 0);
	VOP_UNLOCK(NTOV(gc), 0);
	VOP_UNLOCK(NTOV(ifile), 0);
	VOP_UNLOCK(NTOV(su), 0);
	lockmgr(&fsdev->nd_seg_const, LK_RELEASE, NULL);

	return (error);
}

#ifdef DDB
/*
 * Show details about the given NANDFS mount point.
 */
DB_SHOW_COMMAND(nandfs, db_show_nandfs)
{
	struct mount *mp;
	struct nandfs_device *nffsdev;
	struct nandfs_segment *seg;
	struct nandfsmount *nmp;
	struct buf *bp;
	struct vnode *vp;

	if (!have_addr) {
		db_printf("\nUsage: show nandfs <mount_addr>\n");
		return;
	}

	mp = (struct mount *)addr;
	db_printf("%p %s on %s (%s)\n", mp, mp->mnt_stat.f_mntfromname,
	    mp->mnt_stat.f_mntonname, mp->mnt_stat.f_fstypename);


	nmp = (struct nandfsmount *)(mp->mnt_data);
	nffsdev = nmp->nm_nandfsdev;
	db_printf("dev vnode:%p\n", nffsdev->nd_devvp);
	db_printf("blocksize:%jx last cno:%jx last pseg:%jx seg num:%jx\n",
	    (uintmax_t)nffsdev->nd_blocksize, (uintmax_t)nffsdev->nd_last_cno,
	    (uintmax_t)nffsdev->nd_last_pseg, (uintmax_t)nffsdev->nd_seg_num);
	db_printf("system nodes: dat:%p cp:%p su:%p ifile:%p gc:%p\n",
	    nffsdev->nd_dat_node, nffsdev->nd_cp_node, nffsdev->nd_su_node,
	    nmp->nm_ifile_node, nffsdev->nd_gc_node);

	if (nffsdev->nd_seginfo != NULL) {
		LIST_FOREACH(seg, &nffsdev->nd_seginfo->seg_list, seg_link) {
			db_printf("seg: %p\n", seg);
			TAILQ_FOREACH(bp, &seg->segsum,
			    b_cluster.cluster_entry)
				db_printf("segbp %p\n", bp);
			TAILQ_FOREACH(bp, &seg->data,
			    b_cluster.cluster_entry) {
				vp = bp->b_vp;
				db_printf("bp:%p bp->b_vp:%p ino:%jx\n", bp, vp,
				    (uintmax_t)(vp ? VTON(vp)->nn_ino : 0));
			}
		}
	}
}
#endif
