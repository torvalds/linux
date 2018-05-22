/*
 * Copyright (c) 2000-2001,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
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
#include "xfs_alloc_btree.h"
#include "xfs_alloc.h"
#include "xfs_extent_busy.h"
#include "xfs_error.h"
#include "xfs_trace.h"
#include "xfs_cksum.h"
#include "xfs_trans.h"


STATIC struct xfs_btree_cur *
xfs_allocbt_dup_cursor(
	struct xfs_btree_cur	*cur)
{
	return xfs_allocbt_init_cursor(cur->bc_mp, cur->bc_tp,
			cur->bc_private.a.agbp, cur->bc_private.a.agno,
			cur->bc_btnum);
}

STATIC void
xfs_allocbt_set_root(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr,
	int			inc)
{
	struct xfs_buf		*agbp = cur->bc_private.a.agbp;
	struct xfs_agf		*agf = XFS_BUF_TO_AGF(agbp);
	xfs_agnumber_t		seqno = be32_to_cpu(agf->agf_seqno);
	int			btnum = cur->bc_btnum;
	struct xfs_perag	*pag = xfs_perag_get(cur->bc_mp, seqno);

	ASSERT(ptr->s != 0);

	agf->agf_roots[btnum] = ptr->s;
	be32_add_cpu(&agf->agf_levels[btnum], inc);
	pag->pagf_levels[btnum] += inc;
	xfs_perag_put(pag);

	xfs_alloc_log_agf(cur->bc_tp, agbp, XFS_AGF_ROOTS | XFS_AGF_LEVELS);
}

STATIC int
xfs_allocbt_alloc_block(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*start,
	union xfs_btree_ptr	*new,
	int			*stat)
{
	int			error;
	xfs_agblock_t		bno;

	XFS_BTREE_TRACE_CURSOR(cur, XBT_ENTRY);

	/* Allocate the new block from the freelist. If we can't, give up.  */
	error = xfs_alloc_get_freelist(cur->bc_tp, cur->bc_private.a.agbp,
				       &bno, 1);
	if (error) {
		XFS_BTREE_TRACE_CURSOR(cur, XBT_ERROR);
		return error;
	}

	if (bno == NULLAGBLOCK) {
		XFS_BTREE_TRACE_CURSOR(cur, XBT_EXIT);
		*stat = 0;
		return 0;
	}

	xfs_extent_busy_reuse(cur->bc_mp, cur->bc_private.a.agno, bno, 1, false);

	xfs_trans_agbtree_delta(cur->bc_tp, 1);
	new->s = cpu_to_be32(bno);

	XFS_BTREE_TRACE_CURSOR(cur, XBT_EXIT);
	*stat = 1;
	return 0;
}

STATIC int
xfs_allocbt_free_block(
	struct xfs_btree_cur	*cur,
	struct xfs_buf		*bp)
{
	struct xfs_buf		*agbp = cur->bc_private.a.agbp;
	struct xfs_agf		*agf = XFS_BUF_TO_AGF(agbp);
	xfs_agblock_t		bno;
	int			error;

	bno = xfs_daddr_to_agbno(cur->bc_mp, XFS_BUF_ADDR(bp));
	error = xfs_alloc_put_freelist(cur->bc_tp, agbp, NULL, bno, 1);
	if (error)
		return error;

	xfs_extent_busy_insert(cur->bc_tp, be32_to_cpu(agf->agf_seqno), bno, 1,
			      XFS_EXTENT_BUSY_SKIP_DISCARD);
	xfs_trans_agbtree_delta(cur->bc_tp, -1);
	return 0;
}

/*
 * Update the longest extent in the AGF
 */
STATIC void
xfs_allocbt_update_lastrec(
	struct xfs_btree_cur	*cur,
	struct xfs_btree_block	*block,
	union xfs_btree_rec	*rec,
	int			ptr,
	int			reason)
{
	struct xfs_agf		*agf = XFS_BUF_TO_AGF(cur->bc_private.a.agbp);
	xfs_agnumber_t		seqno = be32_to_cpu(agf->agf_seqno);
	struct xfs_perag	*pag;
	__be32			len;
	int			numrecs;

	ASSERT(cur->bc_btnum == XFS_BTNUM_CNT);

	switch (reason) {
	case LASTREC_UPDATE:
		/*
		 * If this is the last leaf block and it's the last record,
		 * then update the size of the longest extent in the AG.
		 */
		if (ptr != xfs_btree_get_numrecs(block))
			return;
		len = rec->alloc.ar_blockcount;
		break;
	case LASTREC_INSREC:
		if (be32_to_cpu(rec->alloc.ar_blockcount) <=
		    be32_to_cpu(agf->agf_longest))
			return;
		len = rec->alloc.ar_blockcount;
		break;
	case LASTREC_DELREC:
		numrecs = xfs_btree_get_numrecs(block);
		if (ptr <= numrecs)
			return;
		ASSERT(ptr == numrecs + 1);

		if (numrecs) {
			xfs_alloc_rec_t *rrp;

			rrp = XFS_ALLOC_REC_ADDR(cur->bc_mp, block, numrecs);
			len = rrp->ar_blockcount;
		} else {
			len = 0;
		}

		break;
	default:
		ASSERT(0);
		return;
	}

	agf->agf_longest = len;
	pag = xfs_perag_get(cur->bc_mp, seqno);
	pag->pagf_longest = be32_to_cpu(len);
	xfs_perag_put(pag);
	xfs_alloc_log_agf(cur->bc_tp, cur->bc_private.a.agbp, XFS_AGF_LONGEST);
}

STATIC int
xfs_allocbt_get_minrecs(
	struct xfs_btree_cur	*cur,
	int			level)
{
	return cur->bc_mp->m_alloc_mnr[level != 0];
}

STATIC int
xfs_allocbt_get_maxrecs(
	struct xfs_btree_cur	*cur,
	int			level)
{
	return cur->bc_mp->m_alloc_mxr[level != 0];
}

STATIC void
xfs_allocbt_init_key_from_rec(
	union xfs_btree_key	*key,
	union xfs_btree_rec	*rec)
{
	key->alloc.ar_startblock = rec->alloc.ar_startblock;
	key->alloc.ar_blockcount = rec->alloc.ar_blockcount;
}

STATIC void
xfs_bnobt_init_high_key_from_rec(
	union xfs_btree_key	*key,
	union xfs_btree_rec	*rec)
{
	__u32			x;

	x = be32_to_cpu(rec->alloc.ar_startblock);
	x += be32_to_cpu(rec->alloc.ar_blockcount) - 1;
	key->alloc.ar_startblock = cpu_to_be32(x);
	key->alloc.ar_blockcount = 0;
}

STATIC void
xfs_cntbt_init_high_key_from_rec(
	union xfs_btree_key	*key,
	union xfs_btree_rec	*rec)
{
	key->alloc.ar_blockcount = rec->alloc.ar_blockcount;
	key->alloc.ar_startblock = 0;
}

STATIC void
xfs_allocbt_init_rec_from_cur(
	struct xfs_btree_cur	*cur,
	union xfs_btree_rec	*rec)
{
	rec->alloc.ar_startblock = cpu_to_be32(cur->bc_rec.a.ar_startblock);
	rec->alloc.ar_blockcount = cpu_to_be32(cur->bc_rec.a.ar_blockcount);
}

STATIC void
xfs_allocbt_init_ptr_from_cur(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr)
{
	struct xfs_agf		*agf = XFS_BUF_TO_AGF(cur->bc_private.a.agbp);

	ASSERT(cur->bc_private.a.agno == be32_to_cpu(agf->agf_seqno));
	ASSERT(agf->agf_roots[cur->bc_btnum] != 0);

	ptr->s = agf->agf_roots[cur->bc_btnum];
}

STATIC int64_t
xfs_bnobt_key_diff(
	struct xfs_btree_cur	*cur,
	union xfs_btree_key	*key)
{
	xfs_alloc_rec_incore_t	*rec = &cur->bc_rec.a;
	xfs_alloc_key_t		*kp = &key->alloc;

	return (int64_t)be32_to_cpu(kp->ar_startblock) - rec->ar_startblock;
}

STATIC int64_t
xfs_cntbt_key_diff(
	struct xfs_btree_cur	*cur,
	union xfs_btree_key	*key)
{
	xfs_alloc_rec_incore_t	*rec = &cur->bc_rec.a;
	xfs_alloc_key_t		*kp = &key->alloc;
	int64_t			diff;

	diff = (int64_t)be32_to_cpu(kp->ar_blockcount) - rec->ar_blockcount;
	if (diff)
		return diff;

	return (int64_t)be32_to_cpu(kp->ar_startblock) - rec->ar_startblock;
}

STATIC int64_t
xfs_bnobt_diff_two_keys(
	struct xfs_btree_cur	*cur,
	union xfs_btree_key	*k1,
	union xfs_btree_key	*k2)
{
	return (int64_t)be32_to_cpu(k1->alloc.ar_startblock) -
			  be32_to_cpu(k2->alloc.ar_startblock);
}

STATIC int64_t
xfs_cntbt_diff_two_keys(
	struct xfs_btree_cur	*cur,
	union xfs_btree_key	*k1,
	union xfs_btree_key	*k2)
{
	int64_t			diff;

	diff =  be32_to_cpu(k1->alloc.ar_blockcount) -
		be32_to_cpu(k2->alloc.ar_blockcount);
	if (diff)
		return diff;

	return  be32_to_cpu(k1->alloc.ar_startblock) -
		be32_to_cpu(k2->alloc.ar_startblock);
}

static xfs_failaddr_t
xfs_allocbt_verify(
	struct xfs_buf		*bp)
{
	struct xfs_mount	*mp = bp->b_target->bt_mount;
	struct xfs_btree_block	*block = XFS_BUF_TO_BLOCK(bp);
	struct xfs_perag	*pag = bp->b_pag;
	xfs_failaddr_t		fa;
	unsigned int		level;

	/*
	 * magic number and level verification
	 *
	 * During growfs operations, we can't verify the exact level or owner as
	 * the perag is not fully initialised and hence not attached to the
	 * buffer.  In this case, check against the maximum tree depth.
	 *
	 * Similarly, during log recovery we will have a perag structure
	 * attached, but the agf information will not yet have been initialised
	 * from the on disk AGF. Again, we can only check against maximum limits
	 * in this case.
	 */
	level = be16_to_cpu(block->bb_level);
	switch (block->bb_magic) {
	case cpu_to_be32(XFS_ABTB_CRC_MAGIC):
		fa = xfs_btree_sblock_v5hdr_verify(bp);
		if (fa)
			return fa;
		/* fall through */
	case cpu_to_be32(XFS_ABTB_MAGIC):
		if (pag && pag->pagf_init) {
			if (level >= pag->pagf_levels[XFS_BTNUM_BNOi])
				return __this_address;
		} else if (level >= mp->m_ag_maxlevels)
			return __this_address;
		break;
	case cpu_to_be32(XFS_ABTC_CRC_MAGIC):
		fa = xfs_btree_sblock_v5hdr_verify(bp);
		if (fa)
			return fa;
		/* fall through */
	case cpu_to_be32(XFS_ABTC_MAGIC):
		if (pag && pag->pagf_init) {
			if (level >= pag->pagf_levels[XFS_BTNUM_CNTi])
				return __this_address;
		} else if (level >= mp->m_ag_maxlevels)
			return __this_address;
		break;
	default:
		return __this_address;
	}

	return xfs_btree_sblock_verify(bp, mp->m_alloc_mxr[level != 0]);
}

static void
xfs_allocbt_read_verify(
	struct xfs_buf	*bp)
{
	xfs_failaddr_t	fa;

	if (!xfs_btree_sblock_verify_crc(bp))
		xfs_verifier_error(bp, -EFSBADCRC, __this_address);
	else {
		fa = xfs_allocbt_verify(bp);
		if (fa)
			xfs_verifier_error(bp, -EFSCORRUPTED, fa);
	}

	if (bp->b_error)
		trace_xfs_btree_corrupt(bp, _RET_IP_);
}

static void
xfs_allocbt_write_verify(
	struct xfs_buf	*bp)
{
	xfs_failaddr_t	fa;

	fa = xfs_allocbt_verify(bp);
	if (fa) {
		trace_xfs_btree_corrupt(bp, _RET_IP_);
		xfs_verifier_error(bp, -EFSCORRUPTED, fa);
		return;
	}
	xfs_btree_sblock_calc_crc(bp);

}

const struct xfs_buf_ops xfs_allocbt_buf_ops = {
	.name = "xfs_allocbt",
	.verify_read = xfs_allocbt_read_verify,
	.verify_write = xfs_allocbt_write_verify,
	.verify_struct = xfs_allocbt_verify,
};


STATIC int
xfs_bnobt_keys_inorder(
	struct xfs_btree_cur	*cur,
	union xfs_btree_key	*k1,
	union xfs_btree_key	*k2)
{
	return be32_to_cpu(k1->alloc.ar_startblock) <
	       be32_to_cpu(k2->alloc.ar_startblock);
}

STATIC int
xfs_bnobt_recs_inorder(
	struct xfs_btree_cur	*cur,
	union xfs_btree_rec	*r1,
	union xfs_btree_rec	*r2)
{
	return be32_to_cpu(r1->alloc.ar_startblock) +
		be32_to_cpu(r1->alloc.ar_blockcount) <=
		be32_to_cpu(r2->alloc.ar_startblock);
}

STATIC int
xfs_cntbt_keys_inorder(
	struct xfs_btree_cur	*cur,
	union xfs_btree_key	*k1,
	union xfs_btree_key	*k2)
{
	return be32_to_cpu(k1->alloc.ar_blockcount) <
		be32_to_cpu(k2->alloc.ar_blockcount) ||
		(k1->alloc.ar_blockcount == k2->alloc.ar_blockcount &&
		 be32_to_cpu(k1->alloc.ar_startblock) <
		 be32_to_cpu(k2->alloc.ar_startblock));
}

STATIC int
xfs_cntbt_recs_inorder(
	struct xfs_btree_cur	*cur,
	union xfs_btree_rec	*r1,
	union xfs_btree_rec	*r2)
{
	return be32_to_cpu(r1->alloc.ar_blockcount) <
		be32_to_cpu(r2->alloc.ar_blockcount) ||
		(r1->alloc.ar_blockcount == r2->alloc.ar_blockcount &&
		 be32_to_cpu(r1->alloc.ar_startblock) <
		 be32_to_cpu(r2->alloc.ar_startblock));
}

static const struct xfs_btree_ops xfs_bnobt_ops = {
	.rec_len		= sizeof(xfs_alloc_rec_t),
	.key_len		= sizeof(xfs_alloc_key_t),

	.dup_cursor		= xfs_allocbt_dup_cursor,
	.set_root		= xfs_allocbt_set_root,
	.alloc_block		= xfs_allocbt_alloc_block,
	.free_block		= xfs_allocbt_free_block,
	.update_lastrec		= xfs_allocbt_update_lastrec,
	.get_minrecs		= xfs_allocbt_get_minrecs,
	.get_maxrecs		= xfs_allocbt_get_maxrecs,
	.init_key_from_rec	= xfs_allocbt_init_key_from_rec,
	.init_high_key_from_rec	= xfs_bnobt_init_high_key_from_rec,
	.init_rec_from_cur	= xfs_allocbt_init_rec_from_cur,
	.init_ptr_from_cur	= xfs_allocbt_init_ptr_from_cur,
	.key_diff		= xfs_bnobt_key_diff,
	.buf_ops		= &xfs_allocbt_buf_ops,
	.diff_two_keys		= xfs_bnobt_diff_two_keys,
	.keys_inorder		= xfs_bnobt_keys_inorder,
	.recs_inorder		= xfs_bnobt_recs_inorder,
};

static const struct xfs_btree_ops xfs_cntbt_ops = {
	.rec_len		= sizeof(xfs_alloc_rec_t),
	.key_len		= sizeof(xfs_alloc_key_t),

	.dup_cursor		= xfs_allocbt_dup_cursor,
	.set_root		= xfs_allocbt_set_root,
	.alloc_block		= xfs_allocbt_alloc_block,
	.free_block		= xfs_allocbt_free_block,
	.update_lastrec		= xfs_allocbt_update_lastrec,
	.get_minrecs		= xfs_allocbt_get_minrecs,
	.get_maxrecs		= xfs_allocbt_get_maxrecs,
	.init_key_from_rec	= xfs_allocbt_init_key_from_rec,
	.init_high_key_from_rec	= xfs_cntbt_init_high_key_from_rec,
	.init_rec_from_cur	= xfs_allocbt_init_rec_from_cur,
	.init_ptr_from_cur	= xfs_allocbt_init_ptr_from_cur,
	.key_diff		= xfs_cntbt_key_diff,
	.buf_ops		= &xfs_allocbt_buf_ops,
	.diff_two_keys		= xfs_cntbt_diff_two_keys,
	.keys_inorder		= xfs_cntbt_keys_inorder,
	.recs_inorder		= xfs_cntbt_recs_inorder,
};

/*
 * Allocate a new allocation btree cursor.
 */
struct xfs_btree_cur *			/* new alloc btree cursor */
xfs_allocbt_init_cursor(
	struct xfs_mount	*mp,		/* file system mount point */
	struct xfs_trans	*tp,		/* transaction pointer */
	struct xfs_buf		*agbp,		/* buffer for agf structure */
	xfs_agnumber_t		agno,		/* allocation group number */
	xfs_btnum_t		btnum)		/* btree identifier */
{
	struct xfs_agf		*agf = XFS_BUF_TO_AGF(agbp);
	struct xfs_btree_cur	*cur;

	ASSERT(btnum == XFS_BTNUM_BNO || btnum == XFS_BTNUM_CNT);

	cur = kmem_zone_zalloc(xfs_btree_cur_zone, KM_NOFS);

	cur->bc_tp = tp;
	cur->bc_mp = mp;
	cur->bc_btnum = btnum;
	cur->bc_blocklog = mp->m_sb.sb_blocklog;

	if (btnum == XFS_BTNUM_CNT) {
		cur->bc_statoff = XFS_STATS_CALC_INDEX(xs_abtc_2);
		cur->bc_ops = &xfs_cntbt_ops;
		cur->bc_nlevels = be32_to_cpu(agf->agf_levels[XFS_BTNUM_CNT]);
		cur->bc_flags = XFS_BTREE_LASTREC_UPDATE;
	} else {
		cur->bc_statoff = XFS_STATS_CALC_INDEX(xs_abtb_2);
		cur->bc_ops = &xfs_bnobt_ops;
		cur->bc_nlevels = be32_to_cpu(agf->agf_levels[XFS_BTNUM_BNO]);
	}

	cur->bc_private.a.agbp = agbp;
	cur->bc_private.a.agno = agno;

	if (xfs_sb_version_hascrc(&mp->m_sb))
		cur->bc_flags |= XFS_BTREE_CRC_BLOCKS;

	return cur;
}

/*
 * Calculate number of records in an alloc btree block.
 */
int
xfs_allocbt_maxrecs(
	struct xfs_mount	*mp,
	int			blocklen,
	int			leaf)
{
	blocklen -= XFS_ALLOC_BLOCK_LEN(mp);

	if (leaf)
		return blocklen / sizeof(xfs_alloc_rec_t);
	return blocklen / (sizeof(xfs_alloc_key_t) + sizeof(xfs_alloc_ptr_t));
}
