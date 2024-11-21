// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2018-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_bit.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_inode.h"
#include "xfs_trans.h"
#include "xfs_alloc.h"
#include "xfs_btree.h"
#include "xfs_btree_staging.h"
#include "xfs_rtrmap_btree.h"
#include "xfs_trace.h"
#include "xfs_cksum.h"
#include "xfs_error.h"
#include "xfs_extent_busy.h"
#include "xfs_rtgroup.h"

static struct kmem_cache	*xfs_rtrmapbt_cur_cache;

/*
 * Realtime Reverse Map btree.
 *
 * This is a btree used to track the owner(s) of a given extent in the realtime
 * device.  See the comments in xfs_rmap_btree.c for more information.
 *
 * This tree is basically the same as the regular rmap btree except that it
 * is rooted in an inode and does not live in free space.
 */

static struct xfs_btree_cur *
xfs_rtrmapbt_dup_cursor(
	struct xfs_btree_cur	*cur)
{
	return xfs_rtrmapbt_init_cursor(cur->bc_tp, to_rtg(cur->bc_group));
}

static xfs_failaddr_t
xfs_rtrmapbt_verify(
	struct xfs_buf		*bp)
{
	struct xfs_mount	*mp = bp->b_target->bt_mount;
	struct xfs_btree_block	*block = XFS_BUF_TO_BLOCK(bp);
	xfs_failaddr_t		fa;
	int			level;

	if (!xfs_verify_magic(bp, block->bb_magic))
		return __this_address;

	if (!xfs_has_rmapbt(mp))
		return __this_address;
	fa = xfs_btree_fsblock_v5hdr_verify(bp, XFS_RMAP_OWN_UNKNOWN);
	if (fa)
		return fa;
	level = be16_to_cpu(block->bb_level);
	if (level > mp->m_rtrmap_maxlevels)
		return __this_address;

	return xfs_btree_fsblock_verify(bp, mp->m_rtrmap_mxr[level != 0]);
}

static void
xfs_rtrmapbt_read_verify(
	struct xfs_buf	*bp)
{
	xfs_failaddr_t	fa;

	if (!xfs_btree_fsblock_verify_crc(bp))
		xfs_verifier_error(bp, -EFSBADCRC, __this_address);
	else {
		fa = xfs_rtrmapbt_verify(bp);
		if (fa)
			xfs_verifier_error(bp, -EFSCORRUPTED, fa);
	}

	if (bp->b_error)
		trace_xfs_btree_corrupt(bp, _RET_IP_);
}

static void
xfs_rtrmapbt_write_verify(
	struct xfs_buf	*bp)
{
	xfs_failaddr_t	fa;

	fa = xfs_rtrmapbt_verify(bp);
	if (fa) {
		trace_xfs_btree_corrupt(bp, _RET_IP_);
		xfs_verifier_error(bp, -EFSCORRUPTED, fa);
		return;
	}
	xfs_btree_fsblock_calc_crc(bp);

}

const struct xfs_buf_ops xfs_rtrmapbt_buf_ops = {
	.name			= "xfs_rtrmapbt",
	.magic			= { 0, cpu_to_be32(XFS_RTRMAP_CRC_MAGIC) },
	.verify_read		= xfs_rtrmapbt_read_verify,
	.verify_write		= xfs_rtrmapbt_write_verify,
	.verify_struct		= xfs_rtrmapbt_verify,
};

const struct xfs_btree_ops xfs_rtrmapbt_ops = {
	.name			= "rtrmap",
	.type			= XFS_BTREE_TYPE_INODE,
	.geom_flags		= XFS_BTGEO_OVERLAPPING |
				  XFS_BTGEO_IROOT_RECORDS,

	.rec_len		= sizeof(struct xfs_rmap_rec),
	/* Overlapping btree; 2 keys per pointer. */
	.key_len		= 2 * sizeof(struct xfs_rmap_key),
	.ptr_len		= XFS_BTREE_LONG_PTR_LEN,

	.lru_refs		= XFS_RMAP_BTREE_REF,
	.statoff		= XFS_STATS_CALC_INDEX(xs_rtrmap_2),

	.dup_cursor		= xfs_rtrmapbt_dup_cursor,
	.buf_ops		= &xfs_rtrmapbt_buf_ops,
};

/* Allocate a new rt rmap btree cursor. */
struct xfs_btree_cur *
xfs_rtrmapbt_init_cursor(
	struct xfs_trans	*tp,
	struct xfs_rtgroup	*rtg)
{
	struct xfs_inode	*ip = NULL;
	struct xfs_mount	*mp = rtg_mount(rtg);
	struct xfs_btree_cur	*cur;

	return NULL; /* XXX */

	xfs_assert_ilocked(ip, XFS_ILOCK_SHARED | XFS_ILOCK_EXCL);

	cur = xfs_btree_alloc_cursor(mp, tp, &xfs_rtrmapbt_ops,
			mp->m_rtrmap_maxlevels, xfs_rtrmapbt_cur_cache);

	cur->bc_ino.ip = ip;
	cur->bc_group = xfs_group_hold(rtg_group(rtg));
	cur->bc_ino.whichfork = XFS_DATA_FORK;
	cur->bc_nlevels = be16_to_cpu(ip->i_df.if_broot->bb_level) + 1;
	cur->bc_ino.forksize = xfs_inode_fork_size(ip, XFS_DATA_FORK);

	return cur;
}

/*
 * Install a new rt reverse mapping btree root.  Caller is responsible for
 * invalidating and freeing the old btree blocks.
 */
void
xfs_rtrmapbt_commit_staged_btree(
	struct xfs_btree_cur	*cur,
	struct xfs_trans	*tp)
{
	struct xbtree_ifakeroot	*ifake = cur->bc_ino.ifake;
	struct xfs_ifork	*ifp;
	int			flags = XFS_ILOG_CORE | XFS_ILOG_DBROOT;

	ASSERT(cur->bc_flags & XFS_BTREE_STAGING);

	/*
	 * Free any resources hanging off the real fork, then shallow-copy the
	 * staging fork's contents into the real fork to transfer everything
	 * we just built.
	 */
	ifp = xfs_ifork_ptr(cur->bc_ino.ip, XFS_DATA_FORK);
	xfs_idestroy_fork(ifp);
	memcpy(ifp, ifake->if_fork, sizeof(struct xfs_ifork));

	cur->bc_ino.ip->i_projid = cur->bc_group->xg_gno;
	xfs_trans_log_inode(tp, cur->bc_ino.ip, flags);
	xfs_btree_commit_ifakeroot(cur, tp, XFS_DATA_FORK);
}

/* Calculate number of records in a rt reverse mapping btree block. */
static inline unsigned int
xfs_rtrmapbt_block_maxrecs(
	unsigned int		blocklen,
	bool			leaf)
{
	if (leaf)
		return blocklen / sizeof(struct xfs_rmap_rec);
	return blocklen /
		(2 * sizeof(struct xfs_rmap_key) + sizeof(xfs_rtrmap_ptr_t));
}

/*
 * Calculate number of records in an rt reverse mapping btree block.
 */
unsigned int
xfs_rtrmapbt_maxrecs(
	struct xfs_mount	*mp,
	unsigned int		blocklen,
	bool			leaf)
{
	blocklen -= XFS_RTRMAP_BLOCK_LEN;
	return xfs_rtrmapbt_block_maxrecs(blocklen, leaf);
}

/* Compute the max possible height for realtime reverse mapping btrees. */
unsigned int
xfs_rtrmapbt_maxlevels_ondisk(void)
{
	unsigned int		minrecs[2];
	unsigned int		blocklen;

	blocklen = XFS_MIN_CRC_BLOCKSIZE - XFS_BTREE_LBLOCK_CRC_LEN;

	minrecs[0] = xfs_rtrmapbt_block_maxrecs(blocklen, true) / 2;
	minrecs[1] = xfs_rtrmapbt_block_maxrecs(blocklen, false) / 2;

	/* We need at most one record for every block in an rt group. */
	return xfs_btree_compute_maxlevels(minrecs, XFS_MAX_RGBLOCKS);
}

int __init
xfs_rtrmapbt_init_cur_cache(void)
{
	xfs_rtrmapbt_cur_cache = kmem_cache_create("xfs_rtrmapbt_cur",
			xfs_btree_cur_sizeof(xfs_rtrmapbt_maxlevels_ondisk()),
			0, 0, NULL);

	if (!xfs_rtrmapbt_cur_cache)
		return -ENOMEM;
	return 0;
}

void
xfs_rtrmapbt_destroy_cur_cache(void)
{
	kmem_cache_destroy(xfs_rtrmapbt_cur_cache);
	xfs_rtrmapbt_cur_cache = NULL;
}

/* Compute the maximum height of an rt reverse mapping btree. */
void
xfs_rtrmapbt_compute_maxlevels(
	struct xfs_mount	*mp)
{
	unsigned int		d_maxlevels, r_maxlevels;

	if (!xfs_has_rtrmapbt(mp)) {
		mp->m_rtrmap_maxlevels = 0;
		return;
	}

	/*
	 * The realtime rmapbt lives on the data device, which means that its
	 * maximum height is constrained by the size of the data device and
	 * the height required to store one rmap record for each block in an
	 * rt group.
	 */
	d_maxlevels = xfs_btree_space_to_height(mp->m_rtrmap_mnr,
				mp->m_sb.sb_dblocks);
	r_maxlevels = xfs_btree_compute_maxlevels(mp->m_rtrmap_mnr,
				mp->m_groups[XG_TYPE_RTG].blocks);

	/* Add one level to handle the inode root level. */
	mp->m_rtrmap_maxlevels = min(d_maxlevels, r_maxlevels) + 1;
}
