// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
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
#include "xfs_refcount_btree.h"
#include "xfs_refcount.h"
#include "xfs_alloc.h"
#include "xfs_error.h"
#include "xfs_trace.h"
#include "xfs_trans.h"
#include "xfs_bit.h"
#include "xfs_rmap.h"
#include "xfs_ag.h"

static struct kmem_cache	*xfs_refcountbt_cur_cache;

static struct xfs_btree_cur *
xfs_refcountbt_dup_cursor(
	struct xfs_btree_cur	*cur)
{
	return xfs_refcountbt_init_cursor(cur->bc_mp, cur->bc_tp,
			cur->bc_ag.agbp, cur->bc_ag.pag);
}

STATIC void
xfs_refcountbt_set_root(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_ptr	*ptr,
	int				inc)
{
	struct xfs_buf		*agbp = cur->bc_ag.agbp;
	struct xfs_agf		*agf = agbp->b_addr;
	struct xfs_perag	*pag = agbp->b_pag;

	ASSERT(ptr->s != 0);

	agf->agf_refcount_root = ptr->s;
	be32_add_cpu(&agf->agf_refcount_level, inc);
	pag->pagf_refcount_level += inc;

	xfs_alloc_log_agf(cur->bc_tp, agbp,
			XFS_AGF_REFCOUNT_ROOT | XFS_AGF_REFCOUNT_LEVEL);
}

STATIC int
xfs_refcountbt_alloc_block(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_ptr	*start,
	union xfs_btree_ptr		*new,
	int				*stat)
{
	struct xfs_buf		*agbp = cur->bc_ag.agbp;
	struct xfs_agf		*agf = agbp->b_addr;
	struct xfs_alloc_arg	args;		/* block allocation args */
	int			error;		/* error return value */

	memset(&args, 0, sizeof(args));
	args.tp = cur->bc_tp;
	args.mp = cur->bc_mp;
	args.pag = cur->bc_ag.pag;
	args.oinfo = XFS_RMAP_OINFO_REFC;
	args.minlen = args.maxlen = args.prod = 1;
	args.resv = XFS_AG_RESV_METADATA;

	error = xfs_alloc_vextent_near_bno(&args,
			XFS_AGB_TO_FSB(args.mp, args.pag->pag_agno,
					xfs_refc_block(args.mp)));
	if (error)
		goto out_error;
	trace_xfs_refcountbt_alloc_block(cur->bc_mp, cur->bc_ag.pag->pag_agno,
			args.agbno, 1);
	if (args.fsbno == NULLFSBLOCK) {
		*stat = 0;
		return 0;
	}
	ASSERT(args.agno == cur->bc_ag.pag->pag_agno);
	ASSERT(args.len == 1);

	new->s = cpu_to_be32(args.agbno);
	be32_add_cpu(&agf->agf_refcount_blocks, 1);
	xfs_alloc_log_agf(cur->bc_tp, agbp, XFS_AGF_REFCOUNT_BLOCKS);

	*stat = 1;
	return 0;

out_error:
	return error;
}

STATIC int
xfs_refcountbt_free_block(
	struct xfs_btree_cur	*cur,
	struct xfs_buf		*bp)
{
	struct xfs_mount	*mp = cur->bc_mp;
	struct xfs_buf		*agbp = cur->bc_ag.agbp;
	struct xfs_agf		*agf = agbp->b_addr;
	xfs_fsblock_t		fsbno = XFS_DADDR_TO_FSB(mp, xfs_buf_daddr(bp));

	trace_xfs_refcountbt_free_block(cur->bc_mp, cur->bc_ag.pag->pag_agno,
			XFS_FSB_TO_AGBNO(cur->bc_mp, fsbno), 1);
	be32_add_cpu(&agf->agf_refcount_blocks, -1);
	xfs_alloc_log_agf(cur->bc_tp, agbp, XFS_AGF_REFCOUNT_BLOCKS);
	return xfs_free_extent_later(cur->bc_tp, fsbno, 1,
			&XFS_RMAP_OINFO_REFC, XFS_AG_RESV_METADATA, false);
}

STATIC int
xfs_refcountbt_get_minrecs(
	struct xfs_btree_cur	*cur,
	int			level)
{
	return cur->bc_mp->m_refc_mnr[level != 0];
}

STATIC int
xfs_refcountbt_get_maxrecs(
	struct xfs_btree_cur	*cur,
	int			level)
{
	return cur->bc_mp->m_refc_mxr[level != 0];
}

STATIC void
xfs_refcountbt_init_key_from_rec(
	union xfs_btree_key		*key,
	const union xfs_btree_rec	*rec)
{
	key->refc.rc_startblock = rec->refc.rc_startblock;
}

STATIC void
xfs_refcountbt_init_high_key_from_rec(
	union xfs_btree_key		*key,
	const union xfs_btree_rec	*rec)
{
	__u32				x;

	x = be32_to_cpu(rec->refc.rc_startblock);
	x += be32_to_cpu(rec->refc.rc_blockcount) - 1;
	key->refc.rc_startblock = cpu_to_be32(x);
}

STATIC void
xfs_refcountbt_init_rec_from_cur(
	struct xfs_btree_cur	*cur,
	union xfs_btree_rec	*rec)
{
	const struct xfs_refcount_irec *irec = &cur->bc_rec.rc;
	uint32_t		start;

	start = xfs_refcount_encode_startblock(irec->rc_startblock,
			irec->rc_domain);
	rec->refc.rc_startblock = cpu_to_be32(start);
	rec->refc.rc_blockcount = cpu_to_be32(cur->bc_rec.rc.rc_blockcount);
	rec->refc.rc_refcount = cpu_to_be32(cur->bc_rec.rc.rc_refcount);
}

STATIC void
xfs_refcountbt_init_ptr_from_cur(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr)
{
	struct xfs_agf		*agf = cur->bc_ag.agbp->b_addr;

	ASSERT(cur->bc_ag.pag->pag_agno == be32_to_cpu(agf->agf_seqno));

	ptr->s = agf->agf_refcount_root;
}

STATIC int64_t
xfs_refcountbt_key_diff(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_key	*key)
{
	const struct xfs_refcount_key	*kp = &key->refc;
	const struct xfs_refcount_irec	*irec = &cur->bc_rec.rc;
	uint32_t			start;

	start = xfs_refcount_encode_startblock(irec->rc_startblock,
			irec->rc_domain);
	return (int64_t)be32_to_cpu(kp->rc_startblock) - start;
}

STATIC int64_t
xfs_refcountbt_diff_two_keys(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_key	*k1,
	const union xfs_btree_key	*k2,
	const union xfs_btree_key	*mask)
{
	ASSERT(!mask || mask->refc.rc_startblock);

	return (int64_t)be32_to_cpu(k1->refc.rc_startblock) -
			be32_to_cpu(k2->refc.rc_startblock);
}

STATIC xfs_failaddr_t
xfs_refcountbt_verify(
	struct xfs_buf		*bp)
{
	struct xfs_mount	*mp = bp->b_mount;
	struct xfs_btree_block	*block = XFS_BUF_TO_BLOCK(bp);
	struct xfs_perag	*pag = bp->b_pag;
	xfs_failaddr_t		fa;
	unsigned int		level;

	if (!xfs_verify_magic(bp, block->bb_magic))
		return __this_address;

	if (!xfs_has_reflink(mp))
		return __this_address;
	fa = xfs_btree_sblock_v5hdr_verify(bp);
	if (fa)
		return fa;

	level = be16_to_cpu(block->bb_level);
	if (pag && xfs_perag_initialised_agf(pag)) {
		unsigned int	maxlevel = pag->pagf_refcount_level;

#ifdef CONFIG_XFS_ONLINE_REPAIR
		/*
		 * Online repair could be rewriting the refcount btree, so
		 * we'll validate against the larger of either tree while this
		 * is going on.
		 */
		maxlevel = max_t(unsigned int, maxlevel,
				pag->pagf_repair_refcount_level);
#endif
		if (level >= maxlevel)
			return __this_address;
	} else if (level >= mp->m_refc_maxlevels)
		return __this_address;

	return xfs_btree_sblock_verify(bp, mp->m_refc_mxr[level != 0]);
}

STATIC void
xfs_refcountbt_read_verify(
	struct xfs_buf	*bp)
{
	xfs_failaddr_t	fa;

	if (!xfs_btree_sblock_verify_crc(bp))
		xfs_verifier_error(bp, -EFSBADCRC, __this_address);
	else {
		fa = xfs_refcountbt_verify(bp);
		if (fa)
			xfs_verifier_error(bp, -EFSCORRUPTED, fa);
	}

	if (bp->b_error)
		trace_xfs_btree_corrupt(bp, _RET_IP_);
}

STATIC void
xfs_refcountbt_write_verify(
	struct xfs_buf	*bp)
{
	xfs_failaddr_t	fa;

	fa = xfs_refcountbt_verify(bp);
	if (fa) {
		trace_xfs_btree_corrupt(bp, _RET_IP_);
		xfs_verifier_error(bp, -EFSCORRUPTED, fa);
		return;
	}
	xfs_btree_sblock_calc_crc(bp);

}

const struct xfs_buf_ops xfs_refcountbt_buf_ops = {
	.name			= "xfs_refcountbt",
	.magic			= { 0, cpu_to_be32(XFS_REFC_CRC_MAGIC) },
	.verify_read		= xfs_refcountbt_read_verify,
	.verify_write		= xfs_refcountbt_write_verify,
	.verify_struct		= xfs_refcountbt_verify,
};

STATIC int
xfs_refcountbt_keys_inorder(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_key	*k1,
	const union xfs_btree_key	*k2)
{
	return be32_to_cpu(k1->refc.rc_startblock) <
	       be32_to_cpu(k2->refc.rc_startblock);
}

STATIC int
xfs_refcountbt_recs_inorder(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_rec	*r1,
	const union xfs_btree_rec	*r2)
{
	return  be32_to_cpu(r1->refc.rc_startblock) +
		be32_to_cpu(r1->refc.rc_blockcount) <=
		be32_to_cpu(r2->refc.rc_startblock);
}

STATIC enum xbtree_key_contig
xfs_refcountbt_keys_contiguous(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_key	*key1,
	const union xfs_btree_key	*key2,
	const union xfs_btree_key	*mask)
{
	ASSERT(!mask || mask->refc.rc_startblock);

	return xbtree_key_contig(be32_to_cpu(key1->refc.rc_startblock),
				 be32_to_cpu(key2->refc.rc_startblock));
}

static const struct xfs_btree_ops xfs_refcountbt_ops = {
	.rec_len		= sizeof(struct xfs_refcount_rec),
	.key_len		= sizeof(struct xfs_refcount_key),

	.dup_cursor		= xfs_refcountbt_dup_cursor,
	.set_root		= xfs_refcountbt_set_root,
	.alloc_block		= xfs_refcountbt_alloc_block,
	.free_block		= xfs_refcountbt_free_block,
	.get_minrecs		= xfs_refcountbt_get_minrecs,
	.get_maxrecs		= xfs_refcountbt_get_maxrecs,
	.init_key_from_rec	= xfs_refcountbt_init_key_from_rec,
	.init_high_key_from_rec	= xfs_refcountbt_init_high_key_from_rec,
	.init_rec_from_cur	= xfs_refcountbt_init_rec_from_cur,
	.init_ptr_from_cur	= xfs_refcountbt_init_ptr_from_cur,
	.key_diff		= xfs_refcountbt_key_diff,
	.buf_ops		= &xfs_refcountbt_buf_ops,
	.diff_two_keys		= xfs_refcountbt_diff_two_keys,
	.keys_inorder		= xfs_refcountbt_keys_inorder,
	.recs_inorder		= xfs_refcountbt_recs_inorder,
	.keys_contiguous	= xfs_refcountbt_keys_contiguous,
};

/*
 * Initialize a new refcount btree cursor.
 */
static struct xfs_btree_cur *
xfs_refcountbt_init_common(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	struct xfs_perag	*pag)
{
	struct xfs_btree_cur	*cur;

	ASSERT(pag->pag_agno < mp->m_sb.sb_agcount);

	cur = xfs_btree_alloc_cursor(mp, tp, XFS_BTNUM_REFC,
			mp->m_refc_maxlevels, xfs_refcountbt_cur_cache);
	cur->bc_statoff = XFS_STATS_CALC_INDEX(xs_refcbt_2);

	cur->bc_flags |= XFS_BTREE_CRC_BLOCKS;

	cur->bc_ag.pag = xfs_perag_hold(pag);
	cur->bc_ag.refc.nr_ops = 0;
	cur->bc_ag.refc.shape_changes = 0;
	cur->bc_ops = &xfs_refcountbt_ops;
	return cur;
}

/* Create a btree cursor. */
struct xfs_btree_cur *
xfs_refcountbt_init_cursor(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	struct xfs_buf		*agbp,
	struct xfs_perag	*pag)
{
	struct xfs_agf		*agf = agbp->b_addr;
	struct xfs_btree_cur	*cur;

	cur = xfs_refcountbt_init_common(mp, tp, pag);
	cur->bc_nlevels = be32_to_cpu(agf->agf_refcount_level);
	cur->bc_ag.agbp = agbp;
	return cur;
}

/* Create a btree cursor with a fake root for staging. */
struct xfs_btree_cur *
xfs_refcountbt_stage_cursor(
	struct xfs_mount	*mp,
	struct xbtree_afakeroot	*afake,
	struct xfs_perag	*pag)
{
	struct xfs_btree_cur	*cur;

	cur = xfs_refcountbt_init_common(mp, NULL, pag);
	xfs_btree_stage_afakeroot(cur, afake);
	return cur;
}

/*
 * Swap in the new btree root.  Once we pass this point the newly rebuilt btree
 * is in place and we have to kill off all the old btree blocks.
 */
void
xfs_refcountbt_commit_staged_btree(
	struct xfs_btree_cur	*cur,
	struct xfs_trans	*tp,
	struct xfs_buf		*agbp)
{
	struct xfs_agf		*agf = agbp->b_addr;
	struct xbtree_afakeroot	*afake = cur->bc_ag.afake;

	ASSERT(cur->bc_flags & XFS_BTREE_STAGING);

	agf->agf_refcount_root = cpu_to_be32(afake->af_root);
	agf->agf_refcount_level = cpu_to_be32(afake->af_levels);
	agf->agf_refcount_blocks = cpu_to_be32(afake->af_blocks);
	xfs_alloc_log_agf(tp, agbp, XFS_AGF_REFCOUNT_BLOCKS |
				    XFS_AGF_REFCOUNT_ROOT |
				    XFS_AGF_REFCOUNT_LEVEL);
	xfs_btree_commit_afakeroot(cur, tp, agbp, &xfs_refcountbt_ops);
}

/* Calculate number of records in a refcount btree block. */
static inline unsigned int
xfs_refcountbt_block_maxrecs(
	unsigned int		blocklen,
	bool			leaf)
{
	if (leaf)
		return blocklen / sizeof(struct xfs_refcount_rec);
	return blocklen / (sizeof(struct xfs_refcount_key) +
			   sizeof(xfs_refcount_ptr_t));
}

/*
 * Calculate the number of records in a refcount btree block.
 */
int
xfs_refcountbt_maxrecs(
	int			blocklen,
	bool			leaf)
{
	blocklen -= XFS_REFCOUNT_BLOCK_LEN;
	return xfs_refcountbt_block_maxrecs(blocklen, leaf);
}

/* Compute the max possible height of the maximally sized refcount btree. */
unsigned int
xfs_refcountbt_maxlevels_ondisk(void)
{
	unsigned int		minrecs[2];
	unsigned int		blocklen;

	blocklen = XFS_MIN_CRC_BLOCKSIZE - XFS_BTREE_SBLOCK_CRC_LEN;

	minrecs[0] = xfs_refcountbt_block_maxrecs(blocklen, true) / 2;
	minrecs[1] = xfs_refcountbt_block_maxrecs(blocklen, false) / 2;

	return xfs_btree_compute_maxlevels(minrecs, XFS_MAX_CRC_AG_BLOCKS);
}

/* Compute the maximum height of a refcount btree. */
void
xfs_refcountbt_compute_maxlevels(
	struct xfs_mount		*mp)
{
	if (!xfs_has_reflink(mp)) {
		mp->m_refc_maxlevels = 0;
		return;
	}

	mp->m_refc_maxlevels = xfs_btree_compute_maxlevels(
			mp->m_refc_mnr, mp->m_sb.sb_agblocks);
	ASSERT(mp->m_refc_maxlevels <= xfs_refcountbt_maxlevels_ondisk());
}

/* Calculate the refcount btree size for some records. */
xfs_extlen_t
xfs_refcountbt_calc_size(
	struct xfs_mount	*mp,
	unsigned long long	len)
{
	return xfs_btree_calc_size(mp->m_refc_mnr, len);
}

/*
 * Calculate the maximum refcount btree size.
 */
xfs_extlen_t
xfs_refcountbt_max_size(
	struct xfs_mount	*mp,
	xfs_agblock_t		agblocks)
{
	/* Bail out if we're uninitialized, which can happen in mkfs. */
	if (mp->m_refc_mxr[0] == 0)
		return 0;

	return xfs_refcountbt_calc_size(mp, agblocks);
}

/*
 * Figure out how many blocks to reserve and how many are used by this btree.
 */
int
xfs_refcountbt_calc_reserves(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	struct xfs_perag	*pag,
	xfs_extlen_t		*ask,
	xfs_extlen_t		*used)
{
	struct xfs_buf		*agbp;
	struct xfs_agf		*agf;
	xfs_agblock_t		agblocks;
	xfs_extlen_t		tree_len;
	int			error;

	if (!xfs_has_reflink(mp))
		return 0;

	error = xfs_alloc_read_agf(pag, tp, 0, &agbp);
	if (error)
		return error;

	agf = agbp->b_addr;
	agblocks = be32_to_cpu(agf->agf_length);
	tree_len = be32_to_cpu(agf->agf_refcount_blocks);
	xfs_trans_brelse(tp, agbp);

	/*
	 * The log is permanently allocated, so the space it occupies will
	 * never be available for the kinds of things that would require btree
	 * expansion.  We therefore can pretend the space isn't there.
	 */
	if (xfs_ag_contains_log(mp, pag->pag_agno))
		agblocks -= mp->m_sb.sb_logblocks;

	*ask += xfs_refcountbt_max_size(mp, agblocks);
	*used += tree_len;

	return error;
}

int __init
xfs_refcountbt_init_cur_cache(void)
{
	xfs_refcountbt_cur_cache = kmem_cache_create("xfs_refcbt_cur",
			xfs_btree_cur_sizeof(xfs_refcountbt_maxlevels_ondisk()),
			0, 0, NULL);

	if (!xfs_refcountbt_cur_cache)
		return -ENOMEM;
	return 0;
}

void
xfs_refcountbt_destroy_cur_cache(void)
{
	kmem_cache_destroy(xfs_refcountbt_cur_cache);
	xfs_refcountbt_cur_cache = NULL;
}
