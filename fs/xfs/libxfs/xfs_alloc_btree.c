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
#include "xfs_mount.h"
#include "xfs_btree.h"
#include "xfs_btree_staging.h"
#include "xfs_alloc_btree.h"
#include "xfs_alloc.h"
#include "xfs_extent_busy.h"
#include "xfs_error.h"
#include "xfs_health.h"
#include "xfs_trace.h"
#include "xfs_trans.h"
#include "xfs_ag.h"

static struct kmem_cache	*xfs_allocbt_cur_cache;

STATIC struct xfs_btree_cur *
xfs_bnobt_dup_cursor(
	struct xfs_btree_cur	*cur)
{
	return xfs_bnobt_init_cursor(cur->bc_mp, cur->bc_tp, cur->bc_ag.agbp,
			to_perag(cur->bc_group));
}

STATIC struct xfs_btree_cur *
xfs_cntbt_dup_cursor(
	struct xfs_btree_cur	*cur)
{
	return xfs_cntbt_init_cursor(cur->bc_mp, cur->bc_tp, cur->bc_ag.agbp,
			to_perag(cur->bc_group));
}

STATIC void
xfs_allocbt_set_root(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_ptr	*ptr,
	int				inc)
{
	struct xfs_perag		*pag = to_perag(cur->bc_group);
	struct xfs_buf			*agbp = cur->bc_ag.agbp;
	struct xfs_agf			*agf = agbp->b_addr;

	ASSERT(ptr->s != 0);

	if (xfs_btree_is_bno(cur->bc_ops)) {
		agf->agf_bno_root = ptr->s;
		be32_add_cpu(&agf->agf_bno_level, inc);
		pag->pagf_bno_level += inc;
	} else {
		agf->agf_cnt_root = ptr->s;
		be32_add_cpu(&agf->agf_cnt_level, inc);
		pag->pagf_cnt_level += inc;
	}

	xfs_alloc_log_agf(cur->bc_tp, agbp, XFS_AGF_ROOTS | XFS_AGF_LEVELS);
}

STATIC int
xfs_allocbt_alloc_block(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_ptr	*start,
	union xfs_btree_ptr		*new,
	int				*stat)
{
	int			error;
	xfs_agblock_t		bno;

	/* Allocate the new block from the freelist. If we can't, give up.  */
	error = xfs_alloc_get_freelist(to_perag(cur->bc_group), cur->bc_tp,
			cur->bc_ag.agbp, &bno, 1);
	if (error)
		return error;

	if (bno == NULLAGBLOCK) {
		*stat = 0;
		return 0;
	}

	atomic64_inc(&cur->bc_mp->m_allocbt_blks);
	xfs_extent_busy_reuse(cur->bc_group, bno, 1, false);

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
	xfs_agblock_t		bno;
	int			error;

	bno = xfs_daddr_to_agbno(cur->bc_mp, xfs_buf_daddr(bp));
	error = xfs_alloc_put_freelist(to_perag(cur->bc_group), cur->bc_tp,
			agbp, NULL, bno, 1);
	if (error)
		return error;

	atomic64_dec(&cur->bc_mp->m_allocbt_blks);
	xfs_extent_busy_insert(cur->bc_tp, pag_group(agbp->b_pag), bno, 1,
			      XFS_EXTENT_BUSY_SKIP_DISCARD);
	return 0;
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
	union xfs_btree_key		*key,
	const union xfs_btree_rec	*rec)
{
	key->alloc.ar_startblock = rec->alloc.ar_startblock;
	key->alloc.ar_blockcount = rec->alloc.ar_blockcount;
}

STATIC void
xfs_bnobt_init_high_key_from_rec(
	union xfs_btree_key		*key,
	const union xfs_btree_rec	*rec)
{
	__u32				x;

	x = be32_to_cpu(rec->alloc.ar_startblock);
	x += be32_to_cpu(rec->alloc.ar_blockcount) - 1;
	key->alloc.ar_startblock = cpu_to_be32(x);
	key->alloc.ar_blockcount = 0;
}

STATIC void
xfs_cntbt_init_high_key_from_rec(
	union xfs_btree_key		*key,
	const union xfs_btree_rec	*rec)
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

	ASSERT(cur->bc_group->xg_gno == be32_to_cpu(agf->agf_seqno));

	if (xfs_btree_is_bno(cur->bc_ops))
		ptr->s = agf->agf_bno_root;
	else
		ptr->s = agf->agf_cnt_root;
}

STATIC int64_t
xfs_bnobt_key_diff(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_key	*key)
{
	struct xfs_alloc_rec_incore	*rec = &cur->bc_rec.a;
	const struct xfs_alloc_rec	*kp = &key->alloc;

	return (int64_t)be32_to_cpu(kp->ar_startblock) - rec->ar_startblock;
}

STATIC int64_t
xfs_cntbt_key_diff(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_key	*key)
{
	struct xfs_alloc_rec_incore	*rec = &cur->bc_rec.a;
	const struct xfs_alloc_rec	*kp = &key->alloc;
	int64_t				diff;

	diff = (int64_t)be32_to_cpu(kp->ar_blockcount) - rec->ar_blockcount;
	if (diff)
		return diff;

	return (int64_t)be32_to_cpu(kp->ar_startblock) - rec->ar_startblock;
}

STATIC int64_t
xfs_bnobt_diff_two_keys(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_key	*k1,
	const union xfs_btree_key	*k2,
	const union xfs_btree_key	*mask)
{
	ASSERT(!mask || mask->alloc.ar_startblock);

	return (int64_t)be32_to_cpu(k1->alloc.ar_startblock) -
			be32_to_cpu(k2->alloc.ar_startblock);
}

STATIC int64_t
xfs_cntbt_diff_two_keys(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_key	*k1,
	const union xfs_btree_key	*k2,
	const union xfs_btree_key	*mask)
{
	int64_t				diff;

	ASSERT(!mask || (mask->alloc.ar_blockcount &&
			 mask->alloc.ar_startblock));

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

	if (!xfs_verify_magic(bp, block->bb_magic))
		return __this_address;

	if (xfs_has_crc(mp)) {
		fa = xfs_btree_agblock_v5hdr_verify(bp);
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
	if (pag && xfs_perag_initialised_agf(pag)) {
		unsigned int	maxlevel, repair_maxlevel = 0;

		/*
		 * Online repair could be rewriting the free space btrees, so
		 * we'll validate against the larger of either tree while this
		 * is going on.
		 */
		if (bp->b_ops->magic[0] == cpu_to_be32(XFS_ABTC_MAGIC)) {
			maxlevel = pag->pagf_cnt_level;
#ifdef CONFIG_XFS_ONLINE_REPAIR
			repair_maxlevel = pag->pagf_repair_cnt_level;
#endif
		} else {
			maxlevel = pag->pagf_bno_level;
#ifdef CONFIG_XFS_ONLINE_REPAIR
			repair_maxlevel = pag->pagf_repair_bno_level;
#endif
		}

		if (level >= max(maxlevel, repair_maxlevel))
			return __this_address;
	} else if (level >= mp->m_alloc_maxlevels)
		return __this_address;

	return xfs_btree_agblock_verify(bp, mp->m_alloc_mxr[level != 0]);
}

static void
xfs_allocbt_read_verify(
	struct xfs_buf	*bp)
{
	xfs_failaddr_t	fa;

	if (!xfs_btree_agblock_verify_crc(bp))
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
	xfs_btree_agblock_calc_crc(bp);

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
	struct xfs_btree_cur		*cur,
	const union xfs_btree_key	*k1,
	const union xfs_btree_key	*k2)
{
	return be32_to_cpu(k1->alloc.ar_startblock) <
	       be32_to_cpu(k2->alloc.ar_startblock);
}

STATIC int
xfs_bnobt_recs_inorder(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_rec	*r1,
	const union xfs_btree_rec	*r2)
{
	return be32_to_cpu(r1->alloc.ar_startblock) +
		be32_to_cpu(r1->alloc.ar_blockcount) <=
		be32_to_cpu(r2->alloc.ar_startblock);
}

STATIC int
xfs_cntbt_keys_inorder(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_key	*k1,
	const union xfs_btree_key	*k2)
{
	return be32_to_cpu(k1->alloc.ar_blockcount) <
		be32_to_cpu(k2->alloc.ar_blockcount) ||
		(k1->alloc.ar_blockcount == k2->alloc.ar_blockcount &&
		 be32_to_cpu(k1->alloc.ar_startblock) <
		 be32_to_cpu(k2->alloc.ar_startblock));
}

STATIC int
xfs_cntbt_recs_inorder(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_rec	*r1,
	const union xfs_btree_rec	*r2)
{
	return be32_to_cpu(r1->alloc.ar_blockcount) <
		be32_to_cpu(r2->alloc.ar_blockcount) ||
		(r1->alloc.ar_blockcount == r2->alloc.ar_blockcount &&
		 be32_to_cpu(r1->alloc.ar_startblock) <
		 be32_to_cpu(r2->alloc.ar_startblock));
}

STATIC enum xbtree_key_contig
xfs_allocbt_keys_contiguous(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_key	*key1,
	const union xfs_btree_key	*key2,
	const union xfs_btree_key	*mask)
{
	ASSERT(!mask || mask->alloc.ar_startblock);

	return xbtree_key_contig(be32_to_cpu(key1->alloc.ar_startblock),
				 be32_to_cpu(key2->alloc.ar_startblock));
}

const struct xfs_btree_ops xfs_bnobt_ops = {
	.name			= "bno",
	.type			= XFS_BTREE_TYPE_AG,

	.rec_len		= sizeof(xfs_alloc_rec_t),
	.key_len		= sizeof(xfs_alloc_key_t),
	.ptr_len		= XFS_BTREE_SHORT_PTR_LEN,

	.lru_refs		= XFS_ALLOC_BTREE_REF,
	.statoff		= XFS_STATS_CALC_INDEX(xs_abtb_2),
	.sick_mask		= XFS_SICK_AG_BNOBT,

	.dup_cursor		= xfs_bnobt_dup_cursor,
	.set_root		= xfs_allocbt_set_root,
	.alloc_block		= xfs_allocbt_alloc_block,
	.free_block		= xfs_allocbt_free_block,
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
	.keys_contiguous	= xfs_allocbt_keys_contiguous,
};

const struct xfs_btree_ops xfs_cntbt_ops = {
	.name			= "cnt",
	.type			= XFS_BTREE_TYPE_AG,

	.rec_len		= sizeof(xfs_alloc_rec_t),
	.key_len		= sizeof(xfs_alloc_key_t),
	.ptr_len		= XFS_BTREE_SHORT_PTR_LEN,

	.lru_refs		= XFS_ALLOC_BTREE_REF,
	.statoff		= XFS_STATS_CALC_INDEX(xs_abtc_2),
	.sick_mask		= XFS_SICK_AG_CNTBT,

	.dup_cursor		= xfs_cntbt_dup_cursor,
	.set_root		= xfs_allocbt_set_root,
	.alloc_block		= xfs_allocbt_alloc_block,
	.free_block		= xfs_allocbt_free_block,
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
	.keys_contiguous	= NULL, /* not needed right now */
};

/*
 * Allocate a new bnobt cursor.
 *
 * For staging cursors tp and agbp are NULL.
 */
struct xfs_btree_cur *
xfs_bnobt_init_cursor(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	struct xfs_buf		*agbp,
	struct xfs_perag	*pag)
{
	struct xfs_btree_cur	*cur;

	cur = xfs_btree_alloc_cursor(mp, tp, &xfs_bnobt_ops,
			mp->m_alloc_maxlevels, xfs_allocbt_cur_cache);
	cur->bc_group = xfs_group_hold(pag_group(pag));
	cur->bc_ag.agbp = agbp;
	if (agbp) {
		struct xfs_agf		*agf = agbp->b_addr;

		cur->bc_nlevels = be32_to_cpu(agf->agf_bno_level);
	}
	return cur;
}

/*
 * Allocate a new cntbt cursor.
 *
 * For staging cursors tp and agbp are NULL.
 */
struct xfs_btree_cur *
xfs_cntbt_init_cursor(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	struct xfs_buf		*agbp,
	struct xfs_perag	*pag)
{
	struct xfs_btree_cur	*cur;

	cur = xfs_btree_alloc_cursor(mp, tp, &xfs_cntbt_ops,
			mp->m_alloc_maxlevels, xfs_allocbt_cur_cache);
	cur->bc_group = xfs_group_hold(pag_group(pag));
	cur->bc_ag.agbp = agbp;
	if (agbp) {
		struct xfs_agf		*agf = agbp->b_addr;

		cur->bc_nlevels = be32_to_cpu(agf->agf_cnt_level);
	}
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

	if (xfs_btree_is_bno(cur->bc_ops)) {
		agf->agf_bno_root = cpu_to_be32(afake->af_root);
		agf->agf_bno_level = cpu_to_be32(afake->af_levels);
	} else {
		agf->agf_cnt_root = cpu_to_be32(afake->af_root);
		agf->agf_cnt_level = cpu_to_be32(afake->af_levels);
	}
	xfs_alloc_log_agf(tp, agbp, XFS_AGF_ROOTS | XFS_AGF_LEVELS);

	xfs_btree_commit_afakeroot(cur, tp, agbp);
}

/* Calculate number of records in an alloc btree block. */
static inline unsigned int
xfs_allocbt_block_maxrecs(
	unsigned int		blocklen,
	bool			leaf)
{
	if (leaf)
		return blocklen / sizeof(xfs_alloc_rec_t);
	return blocklen / (sizeof(xfs_alloc_key_t) + sizeof(xfs_alloc_ptr_t));
}

/*
 * Calculate number of records in an alloc btree block.
 */
unsigned int
xfs_allocbt_maxrecs(
	struct xfs_mount	*mp,
	unsigned int		blocklen,
	bool			leaf)
{
	blocklen -= XFS_ALLOC_BLOCK_LEN(mp);
	return xfs_allocbt_block_maxrecs(blocklen, leaf);
}

/* Free space btrees are at their largest when every other block is free. */
#define XFS_MAX_FREESP_RECORDS	((XFS_MAX_AG_BLOCKS + 1) / 2)

/* Compute the max possible height for free space btrees. */
unsigned int
xfs_allocbt_maxlevels_ondisk(void)
{
	unsigned int		minrecs[2];
	unsigned int		blocklen;

	blocklen = min(XFS_MIN_BLOCKSIZE - XFS_BTREE_SBLOCK_LEN,
		       XFS_MIN_CRC_BLOCKSIZE - XFS_BTREE_SBLOCK_CRC_LEN);

	minrecs[0] = xfs_allocbt_block_maxrecs(blocklen, true) / 2;
	minrecs[1] = xfs_allocbt_block_maxrecs(blocklen, false) / 2;

	return xfs_btree_compute_maxlevels(minrecs, XFS_MAX_FREESP_RECORDS);
}

/* Calculate the freespace btree size for some records. */
xfs_extlen_t
xfs_allocbt_calc_size(
	struct xfs_mount	*mp,
	unsigned long long	len)
{
	return xfs_btree_calc_size(mp->m_alloc_mnr, len);
}

int __init
xfs_allocbt_init_cur_cache(void)
{
	xfs_allocbt_cur_cache = kmem_cache_create("xfs_bnobt_cur",
			xfs_btree_cur_sizeof(xfs_allocbt_maxlevels_ondisk()),
			0, 0, NULL);

	if (!xfs_allocbt_cur_cache)
		return -ENOMEM;
	return 0;
}

void
xfs_allocbt_destroy_cur_cache(void)
{
	kmem_cache_destroy(xfs_allocbt_cur_cache);
	xfs_allocbt_cur_cache = NULL;
}
