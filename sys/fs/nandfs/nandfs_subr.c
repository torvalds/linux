/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2012 Semihalf
 * Copyright (c) 2008, 2009 Reinoud Zandijk
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
 *
 * From: NetBSD: nilfs_subr.c,v 1.4 2009/07/29 17:06:57 reinoud
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/namei.h>
#include <sys/resourcevar.h>
#include <sys/kernel.h>
#include <sys/file.h>
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
#include <sys/libkern.h>

#include <geom/geom.h>
#include <geom/geom_vfs.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>

#include <machine/_inttypes.h>
#include "nandfs_mount.h"
#include "nandfs.h"
#include "nandfs_subr.h"

MALLOC_DEFINE(M_NANDFSMNT, "nandfs_mount", "NANDFS mount");
MALLOC_DEFINE(M_NANDFSTEMP, "nandfs_tmt", "NANDFS tmp");

uma_zone_t nandfs_node_zone;

void nandfs_bdflush(struct bufobj *bo, struct buf *bp);
int nandfs_bufsync(struct bufobj *bo, int waitfor);

struct buf_ops buf_ops_nandfs = {
	.bop_name	=	"buf_ops_nandfs",
	.bop_write	=	bufwrite,
	.bop_strategy	=	bufstrategy,
	.bop_sync	=	nandfs_bufsync,
	.bop_bdflush	=	nandfs_bdflush,
};

int
nandfs_bufsync(struct bufobj *bo, int waitfor)
{
	struct vnode *vp;
	int error = 0;

	vp = bo2vnode(bo);

	ASSERT_VOP_LOCKED(vp, __func__);
	error = nandfs_sync_file(vp);
	if (error)
		nandfs_warning("%s: cannot flush buffers err:%d\n",
		    __func__, error);

	return (error);
}

void
nandfs_bdflush(bo, bp)
	struct bufobj *bo;
	struct buf *bp;
{
	struct vnode *vp;
	int error;

	if (bo->bo_dirty.bv_cnt <= ((dirtybufthresh * 8) / 10))
		return;

	vp = bp->b_vp;
	if (NANDFS_SYS_NODE(VTON(vp)->nn_ino))
		return;

	if (NANDFS_IS_INDIRECT(bp))
		return;

	error = nandfs_sync_file(vp);
	if (error)
		nandfs_warning("%s: cannot flush buffers err:%d\n",
		    __func__, error);
}

int
nandfs_init(struct vfsconf *vfsp)
{

	nandfs_node_zone = uma_zcreate("nandfs node zone",
	    sizeof(struct nandfs_node), NULL, NULL, NULL, NULL, 0, 0);

	return (0);
}

int
nandfs_uninit(struct vfsconf *vfsp)
{

	uma_zdestroy(nandfs_node_zone);
	return (0);
}

/* Basic calculators */
uint64_t
nandfs_get_segnum_of_block(struct nandfs_device *nandfsdev,
    nandfs_daddr_t blocknr)
{
	uint64_t segnum, blks_per_seg;

	MPASS(blocknr >= nandfsdev->nd_fsdata.f_first_data_block);

	blks_per_seg = nandfsdev->nd_fsdata.f_blocks_per_segment;

	segnum = blocknr / blks_per_seg;
	segnum -= nandfsdev->nd_fsdata.f_first_data_block / blks_per_seg;

	DPRINTF(SYNC, ("%s: returning blocknr %jx -> segnum %jx\n", __func__,
	    blocknr, segnum));

	return (segnum);
}

void
nandfs_get_segment_range(struct nandfs_device *nandfsdev, uint64_t segnum,
    uint64_t *seg_start, uint64_t *seg_end)
{
	uint64_t blks_per_seg;

	blks_per_seg = nandfsdev->nd_fsdata.f_blocks_per_segment;
	*seg_start = nandfsdev->nd_fsdata.f_first_data_block +
	    blks_per_seg * segnum;
	if (seg_end != NULL)
		*seg_end = *seg_start + blks_per_seg -1;
}

void nandfs_calc_mdt_consts(struct nandfs_device *nandfsdev,
    struct nandfs_mdt *mdt, int entry_size)
{
	uint32_t blocksize = nandfsdev->nd_blocksize;

	mdt->entries_per_group = blocksize * 8;
	mdt->entries_per_block = blocksize / entry_size;

	mdt->blocks_per_group =
	    (mdt->entries_per_group -1) / mdt->entries_per_block + 1 + 1;
	mdt->groups_per_desc_block =
	    blocksize / sizeof(struct nandfs_block_group_desc);
	mdt->blocks_per_desc_block =
	    mdt->groups_per_desc_block * mdt->blocks_per_group + 1;
}

int
nandfs_dev_bread(struct nandfs_device *nandfsdev, nandfs_lbn_t blocknr,
    struct ucred *cred, int flags, struct buf **bpp)
{
	int blk2dev = nandfsdev->nd_blocksize / DEV_BSIZE;
	int error;

	DPRINTF(BLOCK, ("%s: read from block %jx vp %p\n", __func__,
	    blocknr * blk2dev, nandfsdev->nd_devvp));
	error = bread(nandfsdev->nd_devvp, blocknr * blk2dev,
	    nandfsdev->nd_blocksize, NOCRED, bpp);
	if (error)
		nandfs_error("%s: cannot read from device - blk:%jx\n",
		    __func__, blocknr);
	return (error);
}

/* Read on a node */
int
nandfs_bread(struct nandfs_node *node, nandfs_lbn_t blocknr,
    struct ucred *cred, int flags, struct buf **bpp)
{
	nandfs_daddr_t vblk;
	int error;

	DPRINTF(BLOCK, ("%s: vp:%p lbn:%#jx\n", __func__, NTOV(node),
	    blocknr));

	error = bread(NTOV(node), blocknr, node->nn_nandfsdev->nd_blocksize,
	    cred, bpp);

	KASSERT(error == 0, ("%s: vp:%p lbn:%#jx err:%d\n", __func__,
	    NTOV(node), blocknr, error));

	if (!nandfs_vblk_get(*bpp) &&
	    ((*bpp)->b_flags & B_CACHE) && node->nn_ino != NANDFS_DAT_INO) {
		nandfs_bmap_lookup(node, blocknr, &vblk);
		nandfs_vblk_set(*bpp, vblk);
	}
	return (error);
}

int
nandfs_bread_meta(struct nandfs_node *node, nandfs_lbn_t blocknr,
    struct ucred *cred, int flags, struct buf **bpp)
{
	nandfs_daddr_t vblk;
	int error;

	DPRINTF(BLOCK, ("%s: vp:%p lbn:%#jx\n", __func__, NTOV(node),
	    blocknr));

	error = bread(NTOV(node), blocknr, node->nn_nandfsdev->nd_blocksize,
	    cred, bpp);

	KASSERT(error == 0, ("%s: vp:%p lbn:%#jx err:%d\n", __func__,
	    NTOV(node), blocknr, error));

	if (!nandfs_vblk_get(*bpp) &&
	    ((*bpp)->b_flags & B_CACHE) && node->nn_ino != NANDFS_DAT_INO) {
		nandfs_bmap_lookup(node, blocknr, &vblk);
		nandfs_vblk_set(*bpp, vblk);
	}

	return (error);
}

int
nandfs_bdestroy(struct nandfs_node *node, nandfs_daddr_t vblk)
{
	int error;

	if (!NANDFS_SYS_NODE(node->nn_ino))
		NANDFS_WRITEASSERT(node->nn_nandfsdev);

	error = nandfs_vblock_end(node->nn_nandfsdev, vblk);
	if (error) {
		nandfs_error("%s: ending vblk: %jx failed\n",
		    __func__, (uintmax_t)vblk);
		return (error);
	}
	node->nn_inode.i_blocks--;

	return (0);
}

int
nandfs_bcreate(struct nandfs_node *node, nandfs_lbn_t blocknr,
    struct ucred *cred, int flags, struct buf **bpp)
{
	int error;

	ASSERT_VOP_LOCKED(NTOV(node), __func__);
	if (!NANDFS_SYS_NODE(node->nn_ino))
		NANDFS_WRITEASSERT(node->nn_nandfsdev);

	DPRINTF(BLOCK, ("%s: vp:%p lbn:%#jx\n", __func__, NTOV(node),
	    blocknr));

	*bpp = getblk(NTOV(node), blocknr, node->nn_nandfsdev->nd_blocksize,
	    0, 0, 0);

	KASSERT((*bpp), ("%s: vp:%p lbn:%#jx\n", __func__,
	    NTOV(node), blocknr));

	if (*bpp) {
		vfs_bio_clrbuf(*bpp);
		(*bpp)->b_blkno = ~(0); /* To avoid VOP_BMAP in bdwrite */
		error = nandfs_bmap_insert_block(node, blocknr, *bpp);
		if (error) {
			nandfs_warning("%s: failed bmap insert node:%p"
			    " blk:%jx\n", __func__, node, blocknr);
			brelse(*bpp);
			return (error);
		}
		node->nn_inode.i_blocks++;

		return (0);
	}

	return (-1);
}

int
nandfs_bcreate_meta(struct nandfs_node *node, nandfs_lbn_t blocknr,
    struct ucred *cred, int flags, struct buf **bpp)
{
	struct nandfs_device *fsdev;
	nandfs_daddr_t vblk;
	int error;

	ASSERT_VOP_LOCKED(NTOV(node), __func__);
	NANDFS_WRITEASSERT(node->nn_nandfsdev);

	DPRINTF(BLOCK, ("%s: vp:%p lbn:%#jx\n", __func__, NTOV(node),
	    blocknr));

	fsdev = node->nn_nandfsdev;

	*bpp = getblk(NTOV(node), blocknr, node->nn_nandfsdev->nd_blocksize,
	    0, 0, 0);

	KASSERT((*bpp), ("%s: vp:%p lbn:%#jx\n", __func__,
	    NTOV(node), blocknr));

	memset((*bpp)->b_data, 0, fsdev->nd_blocksize);

	vfs_bio_clrbuf(*bpp);
	(*bpp)->b_blkno = ~(0); /* To avoid VOP_BMAP in bdwrite */

	nandfs_buf_set(*bpp, NANDFS_VBLK_ASSIGNED);

	if (node->nn_ino != NANDFS_DAT_INO) {
		error = nandfs_vblock_alloc(fsdev, &vblk);
		if (error) {
			nandfs_buf_clear(*bpp, NANDFS_VBLK_ASSIGNED);
			brelse(*bpp);
			return (error);
		}
	} else
		vblk = fsdev->nd_fakevblk++;

	nandfs_vblk_set(*bpp, vblk);

	nandfs_bmap_insert_block(node, blocknr, *bpp);
	return (0);
}

/* Translate index to a file block number and an entry */
void
nandfs_mdt_trans(struct nandfs_mdt *mdt, uint64_t index,
    nandfs_lbn_t *blocknr, uint32_t *entry_in_block)
{
	uint64_t blknr;
	uint64_t group, group_offset, blocknr_in_group;
	uint64_t desc_block, desc_offset;

	/* Calculate our offset in the file */
	group = index / mdt->entries_per_group;
	group_offset = index % mdt->entries_per_group;
	desc_block = group / mdt->groups_per_desc_block;
	desc_offset = group % mdt->groups_per_desc_block;
	blocknr_in_group = group_offset / mdt->entries_per_block;

	/* To descgroup offset */
	blknr = 1 + desc_block * mdt->blocks_per_desc_block;

	/* To group offset */
	blknr += desc_offset * mdt->blocks_per_group;

	/* To actual file block */
	blknr += 1 + blocknr_in_group;

	*blocknr = blknr;
	*entry_in_block = group_offset % mdt->entries_per_block;
}

void
nandfs_mdt_trans_blk(struct nandfs_mdt *mdt, uint64_t index,
    uint64_t *desc, uint64_t *bitmap, nandfs_lbn_t *blocknr,
    uint32_t *entry_in_block)
{
	uint64_t blknr;
	uint64_t group, group_offset, blocknr_in_group;
	uint64_t desc_block, desc_offset;

	/* Calculate our offset in the file */
	group = index / mdt->entries_per_group;
	group_offset = index % mdt->entries_per_group;
	desc_block = group / mdt->groups_per_desc_block;
	desc_offset = group % mdt->groups_per_desc_block;
	blocknr_in_group = group_offset / mdt->entries_per_block;

	/* To descgroup offset */
	*desc = desc_block * mdt->blocks_per_desc_block;
	blknr = 1 + desc_block * mdt->blocks_per_desc_block;

	/* To group offset */
	blknr += desc_offset * mdt->blocks_per_group;
	*bitmap = blknr;

	/* To actual file block */
	blknr += 1 + blocknr_in_group;

	*blocknr = blknr;
	*entry_in_block = group_offset % mdt->entries_per_block;

	DPRINTF(ALLOC,
	    ("%s: desc_buf: %jx bitmap_buf: %jx entry_buf: %jx entry: %x\n",
	    __func__, (uintmax_t)*desc, (uintmax_t)*bitmap,
	    (uintmax_t)*blocknr, *entry_in_block));
}

int
nandfs_vtop(struct nandfs_node *node, nandfs_daddr_t vblocknr,
    nandfs_daddr_t *pblocknr)
{
	struct nandfs_node *dat_node;
	struct nandfs_dat_entry *entry;
	struct buf *bp;
	nandfs_lbn_t ldatblknr;
	uint32_t entry_in_block;
	int locked, error;

	if (node->nn_ino == NANDFS_DAT_INO || node->nn_ino == NANDFS_GC_INO) {
		*pblocknr = vblocknr;
		return (0);
	}

	/* only translate valid vblocknrs */
	if (vblocknr == 0)
		return (0);

	dat_node = node->nn_nandfsdev->nd_dat_node;
	nandfs_mdt_trans(&node->nn_nandfsdev->nd_dat_mdt, vblocknr, &ldatblknr,
	    &entry_in_block);

	locked = NANDFS_VOP_ISLOCKED(NTOV(dat_node));
	if (!locked)
		VOP_LOCK(NTOV(dat_node), LK_SHARED);
	error = nandfs_bread(dat_node, ldatblknr, NOCRED, 0, &bp);
	if (error) {
		DPRINTF(TRANSLATE, ("vtop: can't read in DAT block %#jx!\n",
		    (uintmax_t)ldatblknr));
		brelse(bp);
		VOP_UNLOCK(NTOV(dat_node), 0);
		return (error);
	}

	/* Get our translation */
	entry = ((struct nandfs_dat_entry *) bp->b_data) + entry_in_block;
	DPRINTF(TRANSLATE, ("\tentry %p data %p entry_in_block %x\n",
	    entry, bp->b_data, entry_in_block))
	DPRINTF(TRANSLATE, ("\tvblk %#jx -> %#jx for cp [%#jx-%#jx]\n",
	    (uintmax_t)vblocknr, (uintmax_t)entry->de_blocknr,
	    (uintmax_t)entry->de_start, (uintmax_t)entry->de_end));

	*pblocknr = entry->de_blocknr;
	brelse(bp);
	if (!locked)
		VOP_UNLOCK(NTOV(dat_node), 0);

	MPASS(*pblocknr >= node->nn_nandfsdev->nd_fsdata.f_first_data_block ||
	    *pblocknr == 0);

	return (0);
}

int
nandfs_segsum_valid(struct nandfs_segment_summary *segsum)
{

	return (segsum->ss_magic == NANDFS_SEGSUM_MAGIC);
}

int
nandfs_load_segsum(struct nandfs_device *fsdev, nandfs_daddr_t blocknr,
    struct nandfs_segment_summary *segsum)
{
	struct buf *bp;
	int error;

	DPRINTF(VOLUMES, ("nandfs: try segsum at block %jx\n",
	    (uintmax_t)blocknr));

	error = nandfs_dev_bread(fsdev, blocknr, NOCRED, 0, &bp);
	if (error)
		return (error);

	memcpy(segsum, bp->b_data, sizeof(struct nandfs_segment_summary));
	brelse(bp);

	if (!nandfs_segsum_valid(segsum)) {
		DPRINTF(VOLUMES, ("%s: bad magic pseg:%jx\n", __func__,
		    blocknr));
		return (EINVAL);
	}

	return (error);
}

static int
nandfs_load_super_root(struct nandfs_device *nandfsdev,
    struct nandfs_segment_summary *segsum, uint64_t pseg)
{
	struct nandfs_super_root super_root;
	struct buf *bp;
	uint64_t blocknr;
	uint32_t super_root_crc, comp_crc;
	int off, error;

	/* Check if there is a superroot */
	if ((segsum->ss_flags & NANDFS_SS_SR) == 0) {
		DPRINTF(VOLUMES, ("%s: no super root in pseg:%jx\n", __func__,
		    pseg));
		return (ENOENT);
	}

	/* Get our super root, located at the end of the pseg */
	blocknr = pseg + segsum->ss_nblocks - 1;
	DPRINTF(VOLUMES, ("%s: try at %#jx\n", __func__, (uintmax_t)blocknr));

	error = nandfs_dev_bread(nandfsdev, blocknr, NOCRED, 0, &bp);
	if (error)
		return (error);

	memcpy(&super_root, bp->b_data, sizeof(struct nandfs_super_root));
	brelse(bp);

	/* Check super root CRC */
	super_root_crc = super_root.sr_sum;
	off = sizeof(super_root.sr_sum);
	comp_crc = crc32((uint8_t *)&super_root + off,
	    NANDFS_SR_BYTES - off);

	if (super_root_crc != comp_crc) {
		DPRINTF(VOLUMES, ("%s: invalid crc:%#x [expect:%#x]\n",
		    __func__, super_root_crc, comp_crc));
		return (EINVAL);
	}

	nandfsdev->nd_super_root = super_root;
	DPRINTF(VOLUMES, ("%s: got valid superroot\n", __func__));

	return (0);
}

/*
 * Search for the last super root recorded.
 */
int
nandfs_search_super_root(struct nandfs_device *nandfsdev)
{
	struct nandfs_super_block *super;
	struct nandfs_segment_summary segsum;
	uint64_t seg_start, seg_end, cno, seq, create, pseg;
	uint64_t segnum;
	int error, found;

	error = found = 0;

	/* Search for last super root */
	pseg = nandfsdev->nd_super.s_last_pseg;
	segnum = nandfs_get_segnum_of_block(nandfsdev, pseg);

	cno = nandfsdev->nd_super.s_last_cno;
	create = seq = 0;
	DPRINTF(VOLUMES, ("%s: start in pseg %#jx\n", __func__,
	    (uintmax_t)pseg));

	for (;;) {
		error = nandfs_load_segsum(nandfsdev, pseg, &segsum);
		if (error)
			break;

		if (segsum.ss_seq < seq || segsum.ss_create < create)
			break;

		/* Try to load super root */
		if (segsum.ss_flags & NANDFS_SS_SR) {
			error = nandfs_load_super_root(nandfsdev, &segsum, pseg);
			if (error)
				break;	/* confused */
			found = 1;

			super = &nandfsdev->nd_super;
			nandfsdev->nd_last_segsum = segsum;
			super->s_last_pseg = pseg;
			super->s_last_cno = cno++;
			super->s_last_seq = segsum.ss_seq;
			super->s_state = NANDFS_VALID_FS;
			seq = segsum.ss_seq;
			create = segsum.ss_create;
		} else {
			seq = segsum.ss_seq;
			create = segsum.ss_create;
		}

		/* Calculate next partial segment location */
		pseg += segsum.ss_nblocks;
		DPRINTF(VOLUMES, ("%s: next partial seg is %jx\n", __func__,
		    (uintmax_t)pseg));

		/* Did we reach the end of the segment? if so, go to the next */
		nandfs_get_segment_range(nandfsdev, segnum, &seg_start,
		    &seg_end);
		if (pseg >= seg_end) {
			pseg = segsum.ss_next;
			DPRINTF(VOLUMES,
			    (" partial seg oor next is %jx[%jx - %jx]\n",
			    (uintmax_t)pseg, (uintmax_t)seg_start,
			    (uintmax_t)seg_end));
		}
		segnum = nandfs_get_segnum_of_block(nandfsdev, pseg);
	}

	if (error && !found)
		return (error);

	return (0);
}

int
nandfs_get_node_raw(struct nandfs_device *nandfsdev, struct nandfsmount *nmp,
    uint64_t ino, struct nandfs_inode *inode, struct nandfs_node **nodep)
{
	struct nandfs_node *node;
	struct vnode *nvp;
	struct mount *mp;
	int error;

	*nodep = NULL;

	/* Associate with mountpoint if present */
	if (nmp) {
		mp = nmp->nm_vfs_mountp;
		error = getnewvnode("nandfs", mp, &nandfs_vnodeops, &nvp);
		if (error)
			return (error);
	} else {
		mp = NULL;
		error = getnewvnode("snandfs", mp, &nandfs_system_vnodeops,
		    &nvp);
		if (error)
			return (error);
	}

	if (mp)
		NANDFS_WRITELOCK(nandfsdev);

	DPRINTF(IFILE, ("%s: ino: %#jx -> vp: %p\n",
	    __func__, (uintmax_t)ino, nvp));
	/* Lock node */
	lockmgr(nvp->v_vnlock, LK_EXCLUSIVE, NULL);

	if (mp) {
		error = insmntque(nvp, mp);
		if (error != 0) {
			*nodep = NULL;
			return (error);
		}
	}

	node = uma_zalloc(nandfs_node_zone, M_WAITOK | M_ZERO);

	/* Crosslink */
	node->nn_vnode = nvp;
	nvp->v_bufobj.bo_ops = &buf_ops_nandfs;
	node->nn_nmp = nmp;
	node->nn_nandfsdev = nandfsdev;
	nvp->v_data = node;

	/* Initiase NANDFS node */
	node->nn_ino = ino;
	if (inode != NULL)
		node->nn_inode = *inode;

	nandfs_vinit(nvp, ino);

	/* Return node */
	*nodep = node;
	DPRINTF(IFILE, ("%s: ino:%#jx vp:%p node:%p\n",
	    __func__, (uintmax_t)ino, nvp, *nodep));

	return (0);
}

int
nandfs_get_node(struct nandfsmount *nmp, uint64_t ino,
    struct nandfs_node **nodep)
{
	struct nandfs_device *nandfsdev;
	struct nandfs_inode inode, *entry;
	struct vnode *nvp, *vpp;
	struct thread *td;
	struct buf *bp;
	uint64_t ivblocknr;
	uint32_t entry_in_block;
	int error;

	/* Look up node in hash table */
	td = curthread;
	*nodep = NULL;

	if ((ino < NANDFS_ATIME_INO) && (ino != NANDFS_ROOT_INO)) {
		printf("nandfs_get_node: system ino %"PRIu64" not in mount "
		    "point!\n", ino);
		return (ENOENT);
	}

	error = vfs_hash_get(nmp->nm_vfs_mountp, ino, LK_EXCLUSIVE, td, &nvp,
	    NULL, NULL);
	if (error)
		return (error);

	if (nvp != NULL) {
		*nodep = (struct nandfs_node *)nvp->v_data;
		return (0);
	}

	/* Look up inode structure in mountpoints ifile */
	nandfsdev = nmp->nm_nandfsdev;
	nandfs_mdt_trans(&nandfsdev->nd_ifile_mdt, ino, &ivblocknr,
	    &entry_in_block);

	VOP_LOCK(NTOV(nmp->nm_ifile_node), LK_SHARED);
	error = nandfs_bread(nmp->nm_ifile_node, ivblocknr, NOCRED, 0, &bp);
	if (error) {
		brelse(bp);
		VOP_UNLOCK(NTOV(nmp->nm_ifile_node), 0);
		return (ENOENT);
	}

	/* Get inode entry */
	entry = (struct nandfs_inode *) bp->b_data + entry_in_block;
	memcpy(&inode, entry, sizeof(struct nandfs_inode));
	brelse(bp);
	VOP_UNLOCK(NTOV(nmp->nm_ifile_node), 0);

	/* Get node */
	error = nandfs_get_node_raw(nmp->nm_nandfsdev, nmp, ino, &inode, nodep);
	if (error) {
		*nodep = NULL;
		return (error);
	}

	nvp = (*nodep)->nn_vnode;
	error = vfs_hash_insert(nvp, ino, 0, td, &vpp, NULL, NULL);
	if (error) {
		*nodep = NULL;
		return (error);
	}

	return (error);
}

void
nandfs_dispose_node(struct nandfs_node **nodep)
{
	struct nandfs_node *node;
	struct vnode *vp;

	/* Protect against rogue values */
	node = *nodep;
	if (!node) {
		return;
	}
	DPRINTF(NODE, ("nandfs_dispose_node: %p\n", *nodep));

	vp = NTOV(node);
	vp->v_data = NULL;

	/* Free our associated memory */
	uma_zfree(nandfs_node_zone, node);

	*nodep = NULL;
}

int
nandfs_lookup_name_in_dir(struct vnode *dvp, const char *name, int namelen,
    uint64_t *ino, int *found, uint64_t *off)
{
	struct nandfs_node *dir_node = VTON(dvp);
	struct nandfs_dir_entry	*ndirent;
	struct buf *bp;
	uint64_t file_size, diroffset, blkoff;
	uint64_t blocknr;
	uint32_t blocksize = dir_node->nn_nandfsdev->nd_blocksize;
	uint8_t *pos, name_len;
	int error;

	*found = 0;

	DPRINTF(VNCALL, ("%s: %s file\n", __func__, name));
	if (dvp->v_type != VDIR) {
		return (ENOTDIR);
	}

	/* Get directory filesize */
	file_size = dir_node->nn_inode.i_size;

	/* Walk the directory */
	diroffset = 0;
	blocknr = 0;
	blkoff = 0;
	error = nandfs_bread(dir_node, blocknr, NOCRED, 0, &bp);
	if (error) {
		brelse(bp);
		return (EIO);
	}

	while (diroffset < file_size) {
		if (blkoff >= blocksize) {
			blkoff = 0; blocknr++;
			brelse(bp);
			error = nandfs_bread(dir_node, blocknr, NOCRED, 0,
			    &bp);
			if (error) {
				brelse(bp);
				return (EIO);
			}
		}

		/* Read in one dirent */
		pos = (uint8_t *) bp->b_data + blkoff;
		ndirent = (struct nandfs_dir_entry *) pos;
		name_len = ndirent->name_len;

		if ((name_len == namelen) &&
		    (strncmp(name, ndirent->name, name_len) == 0) &&
		    (ndirent->inode != 0)) {
			*ino = ndirent->inode;
			*off = diroffset;
			DPRINTF(LOOKUP, ("found `%.*s` with ino %"PRIx64"\n",
			    name_len, ndirent->name, *ino));
			*found = 1;
			break;
		}

		/* Advance */
		diroffset += ndirent->rec_len;
		blkoff += ndirent->rec_len;
	}
	brelse(bp);

	return (error);
}

int
nandfs_get_fsinfo(struct nandfsmount *nmp, struct nandfs_fsinfo *fsinfo)
{
	struct nandfs_device *fsdev;

	fsdev = nmp->nm_nandfsdev;

	memcpy(&fsinfo->fs_fsdata, &fsdev->nd_fsdata, sizeof(fsdev->nd_fsdata));
	memcpy(&fsinfo->fs_super, &fsdev->nd_super, sizeof(fsdev->nd_super));
	snprintf(fsinfo->fs_dev, sizeof(fsinfo->fs_dev),
	    "%s", nmp->nm_vfs_mountp->mnt_stat.f_mntfromname);

	return (0);
}

void
nandfs_inode_init(struct nandfs_inode *inode, uint16_t mode)
{
	struct timespec ts;

	vfs_timestamp(&ts);

	inode->i_blocks = 0;
	inode->i_size = 0;
	inode->i_ctime = ts.tv_sec;
	inode->i_ctime_nsec = ts.tv_nsec;
	inode->i_mtime = ts.tv_sec;
	inode->i_mtime_nsec = ts.tv_nsec;
	inode->i_mode = mode;
	inode->i_links_count = 1;
	if (S_ISDIR(mode))
		inode->i_links_count = 2;
	inode->i_flags = 0;

	inode->i_special = 0;
	memset(inode->i_db, 0, sizeof(inode->i_db));
	memset(inode->i_ib, 0, sizeof(inode->i_ib));
}

void
nandfs_inode_destroy(struct nandfs_inode *inode)
{

	MPASS(inode->i_blocks == 0);
	bzero(inode, sizeof(*inode));
}

int
nandfs_fs_full(struct nandfs_device *nffsdev)
{
	uint64_t space, bps;

	bps = nffsdev->nd_fsdata.f_blocks_per_segment;
	space = (nffsdev->nd_clean_segs - 1) * bps;

	DPRINTF(BUF, ("%s: bufs:%jx space:%jx\n", __func__,
	    (uintmax_t)nffsdev->nd_dirty_bufs, (uintmax_t)space));

	if (nffsdev->nd_dirty_bufs + (nffsdev->nd_segs_reserved * bps) >= space)
		return (1);

	return (0);
}

static int
_nandfs_dirty_buf(struct buf *bp, int dirty_meta, int force)
{
	struct nandfs_device *nffsdev;
	struct nandfs_node *node;
	uint64_t ino, bps;

	if (NANDFS_ISGATHERED(bp)) {
		bqrelse(bp);
		return (0);
	}
	if ((bp->b_flags & (B_MANAGED | B_DELWRI)) == (B_MANAGED | B_DELWRI)) {
		bqrelse(bp);
		return (0);
	}

	node = VTON(bp->b_vp);
	nffsdev = node->nn_nandfsdev;
	DPRINTF(BUF, ("%s: buf:%p\n", __func__, bp));
	ino = node->nn_ino;

	if (nandfs_fs_full(nffsdev) && !NANDFS_SYS_NODE(ino) && !force) {
		brelse(bp);
		return (ENOSPC);
	}

	bp->b_flags |= B_MANAGED;
	bdwrite(bp);

	nandfs_dirty_bufs_increment(nffsdev);

	KASSERT((bp->b_vp), ("vp missing for bp"));
	KASSERT((nandfs_vblk_get(bp) || ino == NANDFS_DAT_INO),
	    ("bp vblk is 0"));

	/*
	 * To maintain consistency of FS we need to force making
	 * meta buffers dirty, even if free space is low.
	 */
	if (dirty_meta && ino != NANDFS_GC_INO)
		nandfs_bmap_dirty_blocks(VTON(bp->b_vp), bp, 1);

	bps = nffsdev->nd_fsdata.f_blocks_per_segment;

	if (nffsdev->nd_dirty_bufs >= (bps * nandfs_max_dirty_segs)) {
		mtx_lock(&nffsdev->nd_sync_mtx);
		if (nffsdev->nd_syncing == 0) {
			DPRINTF(SYNC, ("%s: wakeup gc\n", __func__));
			nffsdev->nd_syncing = 1;
			wakeup(&nffsdev->nd_syncing);
		}
		mtx_unlock(&nffsdev->nd_sync_mtx);
	}

	return (0);
}

int
nandfs_dirty_buf(struct buf *bp, int force)
{

	return (_nandfs_dirty_buf(bp, 1, force));
}

int
nandfs_dirty_buf_meta(struct buf *bp, int force)
{

	return (_nandfs_dirty_buf(bp, 0, force));
}

void
nandfs_undirty_buf_fsdev(struct nandfs_device *nffsdev, struct buf *bp)
{

	BUF_ASSERT_HELD(bp);

	if (bp->b_flags & B_DELWRI) {
		bp->b_flags &= ~(B_DELWRI|B_MANAGED);
		nandfs_dirty_bufs_decrement(nffsdev);
	}
	/*
	 * Since it is now being written, we can clear its deferred write flag.
	 */
	bp->b_flags &= ~B_DEFERRED;

	brelse(bp);
}

void
nandfs_undirty_buf(struct buf *bp)
{
	struct nandfs_node *node;

	node = VTON(bp->b_vp);

	nandfs_undirty_buf_fsdev(node->nn_nandfsdev, bp);
}

void
nandfs_vblk_set(struct buf *bp, nandfs_daddr_t blocknr)
{

	nandfs_daddr_t *vblk = (nandfs_daddr_t *)(&bp->b_fsprivate1);
	*vblk = blocknr;
}

nandfs_daddr_t
nandfs_vblk_get(struct buf *bp)
{

	nandfs_daddr_t *vblk = (nandfs_daddr_t *)(&bp->b_fsprivate1);
	return (*vblk);
}

void
nandfs_buf_set(struct buf *bp, uint32_t bits)
{
	uintptr_t flags;

	flags = (uintptr_t)bp->b_fsprivate3;
	flags |= (uintptr_t)bits;
	bp->b_fsprivate3 = (void *)flags;
}

void
nandfs_buf_clear(struct buf *bp, uint32_t bits)
{
	uintptr_t flags;

	flags = (uintptr_t)bp->b_fsprivate3;
	flags &= ~(uintptr_t)bits;
	bp->b_fsprivate3 = (void *)flags;
}

int
nandfs_buf_check(struct buf *bp, uint32_t bits)
{
	uintptr_t flags;

	flags = (uintptr_t)bp->b_fsprivate3;
	if (flags & bits)
		return (1);
	return (0);
}

int
nandfs_erase(struct nandfs_device *fsdev, off_t offset, size_t size)
{
	DPRINTF(BLOCK, ("%s: performing erase at offset %jx size %zx\n",
	    __func__, offset, size));

	MPASS(size % fsdev->nd_erasesize == 0);

	return (g_delete_data(fsdev->nd_gconsumer, offset, size));
}

int
nandfs_vop_islocked(struct vnode *vp)
{
	int islocked;

	islocked = VOP_ISLOCKED(vp);
	return (islocked == LK_EXCLUSIVE || islocked == LK_SHARED);
}

nandfs_daddr_t
nandfs_block_to_dblock(struct nandfs_device *fsdev, nandfs_lbn_t block)
{

	return (btodb(block * fsdev->nd_blocksize));
}
