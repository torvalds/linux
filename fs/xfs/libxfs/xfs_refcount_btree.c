/*
 * Copyright (C) 2016 Oracle.  All Rights Reserved.
 *
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_btree.h"
#include "xfs_bmap.h"
#include "xfs_refcount_btree.h"
#include "xfs_alloc.h"
#include "xfs_error.h"
#include "xfs_trace.h"
#include "xfs_cksum.h"
#include "xfs_trans.h"
#include "xfs_bit.h"

static struct xfs_btree_cur *
xfs_refcountbt_dup_cursor(
	struct xfs_btree_cur	*cur)
{
	return xfs_refcountbt_init_cursor(cur->bc_mp, cur->bc_tp,
			cur->bc_private.a.agbp, cur->bc_private.a.agno,
			cur->bc_private.a.dfops);
}

STATIC bool
xfs_refcountbt_verify(
	struct xfs_buf		*bp)
{
	struct xfs_mount	*mp = bp->b_target->bt_mount;
	struct xfs_btree_block	*block = XFS_BUF_TO_BLOCK(bp);
	struct xfs_perag	*pag = bp->b_pag;
	unsigned int		level;

	if (block->bb_magic != cpu_to_be32(XFS_REFC_CRC_MAGIC))
		return false;

	if (!xfs_sb_version_hasreflink(&mp->m_sb))
		return false;
	if (!xfs_btree_sblock_v5hdr_verify(bp))
		return false;

	level = be16_to_cpu(block->bb_level);
	if (pag && pag->pagf_init) {
		if (level >= pag->pagf_refcount_level)
			return false;
	} else if (level >= mp->m_refc_maxlevels)
		return false;

	return xfs_btree_sblock_verify(bp, mp->m_refc_mxr[level != 0]);
}

STATIC void
xfs_refcountbt_read_verify(
	struct xfs_buf	*bp)
{
	if (!xfs_btree_sblock_verify_crc(bp))
		xfs_buf_ioerror(bp, -EFSBADCRC);
	else if (!xfs_refcountbt_verify(bp))
		xfs_buf_ioerror(bp, -EFSCORRUPTED);

	if (bp->b_error) {
		trace_xfs_btree_corrupt(bp, _RET_IP_);
		xfs_verifier_error(bp);
	}
}

STATIC void
xfs_refcountbt_write_verify(
	struct xfs_buf	*bp)
{
	if (!xfs_refcountbt_verify(bp)) {
		trace_xfs_btree_corrupt(bp, _RET_IP_);
		xfs_buf_ioerror(bp, -EFSCORRUPTED);
		xfs_verifier_error(bp);
		return;
	}
	xfs_btree_sblock_calc_crc(bp);

}

const struct xfs_buf_ops xfs_refcountbt_buf_ops = {
	.name			= "xfs_refcountbt",
	.verify_read		= xfs_refcountbt_read_verify,
	.verify_write		= xfs_refcountbt_write_verify,
};

static const struct xfs_btree_ops xfs_refcountbt_ops = {
	.rec_len		= sizeof(struct xfs_refcount_rec),
	.key_len		= sizeof(struct xfs_refcount_key),

	.dup_cursor		= xfs_refcountbt_dup_cursor,
	.buf_ops		= &xfs_refcountbt_buf_ops,
};

/*
 * Allocate a new refcount btree cursor.
 */
struct xfs_btree_cur *
xfs_refcountbt_init_cursor(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	struct xfs_buf		*agbp,
	xfs_agnumber_t		agno,
	struct xfs_defer_ops	*dfops)
{
	struct xfs_agf		*agf = XFS_BUF_TO_AGF(agbp);
	struct xfs_btree_cur	*cur;

	ASSERT(agno != NULLAGNUMBER);
	ASSERT(agno < mp->m_sb.sb_agcount);
	cur = kmem_zone_zalloc(xfs_btree_cur_zone, KM_NOFS);

	cur->bc_tp = tp;
	cur->bc_mp = mp;
	cur->bc_btnum = XFS_BTNUM_REFC;
	cur->bc_blocklog = mp->m_sb.sb_blocklog;
	cur->bc_ops = &xfs_refcountbt_ops;

	cur->bc_nlevels = be32_to_cpu(agf->agf_refcount_level);

	cur->bc_private.a.agbp = agbp;
	cur->bc_private.a.agno = agno;
	cur->bc_private.a.dfops = dfops;
	cur->bc_flags |= XFS_BTREE_CRC_BLOCKS;

	cur->bc_private.a.priv.refc.nr_ops = 0;
	cur->bc_private.a.priv.refc.shape_changes = 0;

	return cur;
}

/*
 * Calculate the number of records in a refcount btree block.
 */
int
xfs_refcountbt_maxrecs(
	struct xfs_mount	*mp,
	int			blocklen,
	bool			leaf)
{
	blocklen -= XFS_REFCOUNT_BLOCK_LEN;

	if (leaf)
		return blocklen / sizeof(struct xfs_refcount_rec);
	return blocklen / (sizeof(struct xfs_refcount_key) +
			   sizeof(xfs_refcount_ptr_t));
}

/* Compute the maximum height of a refcount btree. */
void
xfs_refcountbt_compute_maxlevels(
	struct xfs_mount		*mp)
{
	mp->m_refc_maxlevels = xfs_btree_compute_maxlevels(mp,
			mp->m_refc_mnr, mp->m_sb.sb_agblocks);
}
