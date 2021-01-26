// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2001,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
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
#include "xfs_btree_staging.h"
#include "xfs_alloc_btree.h"
#include "xfs_alloc.h"
#include "xfs_extent_busy.h"
#include "xfs_error.h"
#include "xfs_trace.h"
#include "xfs_trans.h"


STATIC struct xfs_btree_cur *
xfs_allocbt_dup_cursor(
	struct xfs_btree_cur	*cur)
{
	return xfs_allocbt_init_cursor(cur->bc_mp, cur->bc_tp,
			cur->bc_ag.agbp, cur->bc_ag.agno,
			cur->bc_btnum);
}

STATIC void
xfs_allocbt_set_root(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr,
	int			inc)
{
	struct xfs_buf		*agbp = cur->bc_ag.agbp;
	struct xfs_agf		*agf = agbp->b_addr;
	int			btnum = cur->bc_btnum;
	struct xfs_perag	*pag = agbp->b_pag;

	ASSERT(ptr->s != 0);

	agf->agf_roots[btnum] = ptr->s;
	be32_add_cpu(&agf->agf_levels[btnum], inc);
	pag->pagf_levels[btnum] += inc;

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

	/* Allocate the new block from the freelist. If we can't, give up.  */
	error = xfs_alloc_get_freelist(cur->bc_tp, cur->bc_ag.agbp,
				       &bno, 1);
	if (error)
		return error;

	if (bno == NULLAGBLOCK) {
		*stat = 0;
		return 0;
	}

	xfs_extent_busy_reuse(cur->bc_mp, cur->bc_ag.agno, bno, 1, false);

	xfs_trans_agbtree_delta(cur->bc_tp, 1);
	new->s = cpu_to_be32(bno);

	*stat = 1;
	return 0;
}

STATIC int
xfs_allocbt_free_block(
	struct xfs_btree_cur	*cur,
	struct xfs_buf		*bp)
{
	struct xfs_buf		*agbp = cur->bc_ag.agbp;
	struct xfs_agf		*agf = agbp->b_addr;
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
	struct xfs_agf		*agf = cur->bc_ag.agbp->b_addr;
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
	pag = cur->bc_ag.agbp->b_pag;
	pag->pagf_longest = be32_to_cpu(len);
	xfs_alloc_log_agf(cur->bc_tp, cur->bc_ag.agbp, XFS_AGF_LONGEST);
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
	struct xfs_agf		*agf = cur->bc_ag.agbp->b_addr;

	ASSERT(cur->bc_ag.agno == be32_to_cpu(agf->agf_seqno));

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
	struct xfs_mount	*mp = bp->b_mount;
	struct xfs_btree_block	*block = XFS_BUF_TO_BLOCK(bp);
	struct xfs_perag	*pag = bp->b_pag;
	xfs_failaddr_t		fa;
	unsigned int		level;
	xfs_btnum_t		btnum = XFS_BTNUM_BNOi;

	if (!xfs_verify_magic(bp, block->bb_magic))
		return __this_address;

	if (xfs_sb_version_hascrc(&mp->m_sb)) {
		fa = xfs_btree_sblock_v5hdr_verify(bp);
		if (fa)
			return fa;
	}

	/*
	 * The perag may not be attached during grow operations or fully
	 * initialized from the AGF during log recovery. Therefore we can only
	 * check against maximum tree depth from those contexts.
	 *
	 * Otherwise check against the per-tree limit. Peek at one of the
	 * verifier magic values to determine the type of tree we're verifying
	 * against.
	 */
	level = be16_to_cpu(block->bb_level);
	if (bp->b_ops->magic[0] == cpu_to_be32(XFS_ABTC_MAGIC))
		btnum = XFS_BTNUM_CNTi;
	if (pag && pag->pagf_init) {
		if (level >= pag->pagf_levels[btnum])
			return __this_address;
	} else if (level >= mp->m_ag_maxlevels)
		return __this_address;

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

const struct xfs_buf_ops xfs_bnobt_buf_ops = {
	.name = "xfs_bnobt",
	.magic = { cpu_to_be32(XFS_ABTB_MAGIC),
		   cpu_to_be32(XFS_ABTB_CRC_MAGIC) },
	.verify_read = xfs_allocbt_read_verify,
	.verify_write = xfs_allocbt_write_verify,
	.verify_struct = xfs_allocbt_verify,
};

const struct xfs_buf_ops xfs_cntbt_buf_ops = {
	.name = "xfs_cntbt",
	.magic = { cpu_to_be32(XFS_ABTC_MAGIC),
		   cpu_to_be32(XFS_ABTC_CRC_MAGIC) },
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
	.buf_ops		= &xfs_bnobt_buf_ops,
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
	.buf_ops		= &xfs_cntbt_buf_ops,
	.diff_two_keys		= xfs_cntbt_diff_two_keys,
	.keys_inorder		= xfs_cntbt_keys_inorder,
	.recs_inorder		= xfs_cntbt_recs_inorder,
};

/* Allocate most of a new allocation btree cursor. */
STATIC struct xfs_btree_cur *
xfs_allocbt_init_common(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	xfs_agnumber_t		agno,
	xfs_btnum_t		btnum)
{
	struct xfs_btree_cur	*cur;

	ASSERT(btnum == XFS_BTNUM_BNO || btnum == XFS_BTNUM_CNT);

	cur = kmem_cache_zalloc(xfs_btree_cur_zone, GFP_NOFS | __GFP_NOFAIL);

	cur->bc_tp = tp;
	cur->bc_mp = mp;
	cur->bc_btnum = btnum;
	cur->bc_blocklog = mp->m_sb.sb_blocklog;

	if (btnum == XFS_BTNUM_CNT) {
		cur->bc_ops = &xfs_cntbt_ops;
		cur->bc_statoff = XFS_STATS_CALC_INDEX(xs_abtc_2);
		cur->bc_flags = XFS_BTREE_LASTREC_UPDATE;
	} else {
		cur->bc_ops = &xfs_bnobt_ops;
		cur->bc_statoff = XFS_STATS_CALC_INDEX(xs_abtb_2);
	}

	cur->bc_ag.agno = agno;
	cur->bc_ag.abt.active = false;

	if (xfs_sb_version_hascrc(&mp->m_sb))
		cur->bc_flags |= XFS_BTREE_CRC_BLOCKS;

	return cur;
}

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
	struct xfs_agf		*agf = agbp->b_addr;
	struct xfs_btree_cur	*cur;

	cur = xfs_allocbt_init_common(mp, tp, agno, btnum);
	if (btnum == XFS_BTNUM_CNT)
		cur->bc_nlevels = be32_to_cpu(agf->agf_levels[XFS_BTNUM_CNT]);
	else
		cur->bc_nlevels = be32_to_cpu(agf->agf_levels[XFS_BTNUM_BNO]);

	cur->bc_ag.agbp = agbp;

	return cur;
}

/* Create a free space btree cursor with a fake root for staging. */
struct xfs_btree_cur *
xfs_allocbt_stage_cursor(
	struct xfs_mount	*mp,
	struct xbtree_afakeroot	*afake,
	xfs_agnumber_t		agno,
	xfs_btnum_t		btnum)
{
	struct xfs_btree_cur	*cur;

	cur = xfs_allocbt_init_common(mp, NULL, agno, btnum);
	xfs_btree_stage_afakeroot(cur, afake);
	return cur;
}

/*
 * Install a new free space btree root.  Caller is responsible for invalidating
 * and freeing the old btree blocks.
 */
void
xfs_allocbt_commit_staged_btree(
	struct xfs_btree_cur	*cur,
	struct xfs_trans	*tp,
	struct xfs_buf		*agbp)
{
	struct xfs_agf		*agf = agbp->b_addr;
	struct xbtree_afakeroot	*afake = cur->bc_ag.afake;

	ASSERT(cur->bc_flags & XFS_BTREE_STAGING);

	agf->agf_roots[cur->bc_btnum] = cpu_to_be32(afake->af_root);
	agf->agf_levels[cur->bc_btnum] = cpu_to_be32(afake->af_levels);
	xfs_alloc_log_agf(tp, agbp, XFS_AGF_ROOTS | XFS_AGF_LEVELS);

	if (cur->bc_btnum == XFS_BTNUM_BNO) {
		xfs_btree_commit_afakeroot(cur, tp, agbp, &xfs_bnobt_ops);
	} else {
		cur->bc_flags |= XFS_BTREE_LASTREC_UPDATE;
		xfs_btree_commit_afakeroot(cur, tp, agbp, &xfs_cntbt_ops);
	}
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

/* Calculate the freespace btree size for some records. */
xfs_extlen_t
xfs_allocbt_calc_size(
	struct xfs_mount	*mp,
	unsigned long long	len)
{
	return xfs_btree_calc_size(mp->m_alloc_mnr, len);
}
