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
#include "xfs_bit.h"
#include "xfs_mount.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_ialloc_btree.h"
#include "xfs_alloc.h"
#include "xfs_error.h"
#include "xfs_trace.h"
#include "xfs_trans.h"
#include "xfs_rmap.h"


STATIC int
xfs_iyesbt_get_minrecs(
	struct xfs_btree_cur	*cur,
	int			level)
{
	return M_IGEO(cur->bc_mp)->iyesbt_mnr[level != 0];
}

STATIC struct xfs_btree_cur *
xfs_iyesbt_dup_cursor(
	struct xfs_btree_cur	*cur)
{
	return xfs_iyesbt_init_cursor(cur->bc_mp, cur->bc_tp,
			cur->bc_private.a.agbp, cur->bc_private.a.agyes,
			cur->bc_btnum);
}

STATIC void
xfs_iyesbt_set_root(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*nptr,
	int			inc)	/* level change */
{
	struct xfs_buf		*agbp = cur->bc_private.a.agbp;
	struct xfs_agi		*agi = XFS_BUF_TO_AGI(agbp);

	agi->agi_root = nptr->s;
	be32_add_cpu(&agi->agi_level, inc);
	xfs_ialloc_log_agi(cur->bc_tp, agbp, XFS_AGI_ROOT | XFS_AGI_LEVEL);
}

STATIC void
xfs_fiyesbt_set_root(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*nptr,
	int			inc)	/* level change */
{
	struct xfs_buf		*agbp = cur->bc_private.a.agbp;
	struct xfs_agi		*agi = XFS_BUF_TO_AGI(agbp);

	agi->agi_free_root = nptr->s;
	be32_add_cpu(&agi->agi_free_level, inc);
	xfs_ialloc_log_agi(cur->bc_tp, agbp,
			   XFS_AGI_FREE_ROOT | XFS_AGI_FREE_LEVEL);
}

STATIC int
__xfs_iyesbt_alloc_block(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*start,
	union xfs_btree_ptr	*new,
	int			*stat,
	enum xfs_ag_resv_type	resv)
{
	xfs_alloc_arg_t		args;		/* block allocation args */
	int			error;		/* error return value */
	xfs_agblock_t		sbyes = be32_to_cpu(start->s);

	memset(&args, 0, sizeof(args));
	args.tp = cur->bc_tp;
	args.mp = cur->bc_mp;
	args.oinfo = XFS_RMAP_OINFO_INOBT;
	args.fsbyes = XFS_AGB_TO_FSB(args.mp, cur->bc_private.a.agyes, sbyes);
	args.minlen = 1;
	args.maxlen = 1;
	args.prod = 1;
	args.type = XFS_ALLOCTYPE_NEAR_BNO;
	args.resv = resv;

	error = xfs_alloc_vextent(&args);
	if (error)
		return error;

	if (args.fsbyes == NULLFSBLOCK) {
		*stat = 0;
		return 0;
	}
	ASSERT(args.len == 1);

	new->s = cpu_to_be32(XFS_FSB_TO_AGBNO(args.mp, args.fsbyes));
	*stat = 1;
	return 0;
}

STATIC int
xfs_iyesbt_alloc_block(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*start,
	union xfs_btree_ptr	*new,
	int			*stat)
{
	return __xfs_iyesbt_alloc_block(cur, start, new, stat, XFS_AG_RESV_NONE);
}

STATIC int
xfs_fiyesbt_alloc_block(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*start,
	union xfs_btree_ptr	*new,
	int			*stat)
{
	if (cur->bc_mp->m_fiyesbt_yesres)
		return xfs_iyesbt_alloc_block(cur, start, new, stat);
	return __xfs_iyesbt_alloc_block(cur, start, new, stat,
			XFS_AG_RESV_METADATA);
}

STATIC int
__xfs_iyesbt_free_block(
	struct xfs_btree_cur	*cur,
	struct xfs_buf		*bp,
	enum xfs_ag_resv_type	resv)
{
	return xfs_free_extent(cur->bc_tp,
			XFS_DADDR_TO_FSB(cur->bc_mp, XFS_BUF_ADDR(bp)), 1,
			&XFS_RMAP_OINFO_INOBT, resv);
}

STATIC int
xfs_iyesbt_free_block(
	struct xfs_btree_cur	*cur,
	struct xfs_buf		*bp)
{
	return __xfs_iyesbt_free_block(cur, bp, XFS_AG_RESV_NONE);
}

STATIC int
xfs_fiyesbt_free_block(
	struct xfs_btree_cur	*cur,
	struct xfs_buf		*bp)
{
	if (cur->bc_mp->m_fiyesbt_yesres)
		return xfs_iyesbt_free_block(cur, bp);
	return __xfs_iyesbt_free_block(cur, bp, XFS_AG_RESV_METADATA);
}

STATIC int
xfs_iyesbt_get_maxrecs(
	struct xfs_btree_cur	*cur,
	int			level)
{
	return M_IGEO(cur->bc_mp)->iyesbt_mxr[level != 0];
}

STATIC void
xfs_iyesbt_init_key_from_rec(
	union xfs_btree_key	*key,
	union xfs_btree_rec	*rec)
{
	key->iyesbt.ir_startiyes = rec->iyesbt.ir_startiyes;
}

STATIC void
xfs_iyesbt_init_high_key_from_rec(
	union xfs_btree_key	*key,
	union xfs_btree_rec	*rec)
{
	__u32			x;

	x = be32_to_cpu(rec->iyesbt.ir_startiyes);
	x += XFS_INODES_PER_CHUNK - 1;
	key->iyesbt.ir_startiyes = cpu_to_be32(x);
}

STATIC void
xfs_iyesbt_init_rec_from_cur(
	struct xfs_btree_cur	*cur,
	union xfs_btree_rec	*rec)
{
	rec->iyesbt.ir_startiyes = cpu_to_be32(cur->bc_rec.i.ir_startiyes);
	if (xfs_sb_version_hassparseiyesdes(&cur->bc_mp->m_sb)) {
		rec->iyesbt.ir_u.sp.ir_holemask =
					cpu_to_be16(cur->bc_rec.i.ir_holemask);
		rec->iyesbt.ir_u.sp.ir_count = cur->bc_rec.i.ir_count;
		rec->iyesbt.ir_u.sp.ir_freecount = cur->bc_rec.i.ir_freecount;
	} else {
		/* ir_holemask/ir_count yest supported on-disk */
		rec->iyesbt.ir_u.f.ir_freecount =
					cpu_to_be32(cur->bc_rec.i.ir_freecount);
	}
	rec->iyesbt.ir_free = cpu_to_be64(cur->bc_rec.i.ir_free);
}

/*
 * initial value of ptr for lookup
 */
STATIC void
xfs_iyesbt_init_ptr_from_cur(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr)
{
	struct xfs_agi		*agi = XFS_BUF_TO_AGI(cur->bc_private.a.agbp);

	ASSERT(cur->bc_private.a.agyes == be32_to_cpu(agi->agi_seqyes));

	ptr->s = agi->agi_root;
}

STATIC void
xfs_fiyesbt_init_ptr_from_cur(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr)
{
	struct xfs_agi		*agi = XFS_BUF_TO_AGI(cur->bc_private.a.agbp);

	ASSERT(cur->bc_private.a.agyes == be32_to_cpu(agi->agi_seqyes));
	ptr->s = agi->agi_free_root;
}

STATIC int64_t
xfs_iyesbt_key_diff(
	struct xfs_btree_cur	*cur,
	union xfs_btree_key	*key)
{
	return (int64_t)be32_to_cpu(key->iyesbt.ir_startiyes) -
			  cur->bc_rec.i.ir_startiyes;
}

STATIC int64_t
xfs_iyesbt_diff_two_keys(
	struct xfs_btree_cur	*cur,
	union xfs_btree_key	*k1,
	union xfs_btree_key	*k2)
{
	return (int64_t)be32_to_cpu(k1->iyesbt.ir_startiyes) -
			  be32_to_cpu(k2->iyesbt.ir_startiyes);
}

static xfs_failaddr_t
xfs_iyesbt_verify(
	struct xfs_buf		*bp)
{
	struct xfs_mount	*mp = bp->b_mount;
	struct xfs_btree_block	*block = XFS_BUF_TO_BLOCK(bp);
	xfs_failaddr_t		fa;
	unsigned int		level;

	if (!xfs_verify_magic(bp, block->bb_magic))
		return __this_address;

	/*
	 * During growfs operations, we can't verify the exact owner as the
	 * perag is yest fully initialised and hence yest attached to the buffer.
	 *
	 * Similarly, during log recovery we will have a perag structure
	 * attached, but the agi information will yest yet have been initialised
	 * from the on disk AGI. We don't currently use any of this information,
	 * but beware of the landmine (i.e. need to check pag->pagi_init) if we
	 * ever do.
	 */
	if (xfs_sb_version_hascrc(&mp->m_sb)) {
		fa = xfs_btree_sblock_v5hdr_verify(bp);
		if (fa)
			return fa;
	}

	/* level verification */
	level = be16_to_cpu(block->bb_level);
	if (level >= M_IGEO(mp)->iyesbt_maxlevels)
		return __this_address;

	return xfs_btree_sblock_verify(bp,
			M_IGEO(mp)->iyesbt_mxr[level != 0]);
}

static void
xfs_iyesbt_read_verify(
	struct xfs_buf	*bp)
{
	xfs_failaddr_t	fa;

	if (!xfs_btree_sblock_verify_crc(bp))
		xfs_verifier_error(bp, -EFSBADCRC, __this_address);
	else {
		fa = xfs_iyesbt_verify(bp);
		if (fa)
			xfs_verifier_error(bp, -EFSCORRUPTED, fa);
	}

	if (bp->b_error)
		trace_xfs_btree_corrupt(bp, _RET_IP_);
}

static void
xfs_iyesbt_write_verify(
	struct xfs_buf	*bp)
{
	xfs_failaddr_t	fa;

	fa = xfs_iyesbt_verify(bp);
	if (fa) {
		trace_xfs_btree_corrupt(bp, _RET_IP_);
		xfs_verifier_error(bp, -EFSCORRUPTED, fa);
		return;
	}
	xfs_btree_sblock_calc_crc(bp);

}

const struct xfs_buf_ops xfs_iyesbt_buf_ops = {
	.name = "xfs_iyesbt",
	.magic = { cpu_to_be32(XFS_IBT_MAGIC), cpu_to_be32(XFS_IBT_CRC_MAGIC) },
	.verify_read = xfs_iyesbt_read_verify,
	.verify_write = xfs_iyesbt_write_verify,
	.verify_struct = xfs_iyesbt_verify,
};

const struct xfs_buf_ops xfs_fiyesbt_buf_ops = {
	.name = "xfs_fiyesbt",
	.magic = { cpu_to_be32(XFS_FIBT_MAGIC),
		   cpu_to_be32(XFS_FIBT_CRC_MAGIC) },
	.verify_read = xfs_iyesbt_read_verify,
	.verify_write = xfs_iyesbt_write_verify,
	.verify_struct = xfs_iyesbt_verify,
};

STATIC int
xfs_iyesbt_keys_iyesrder(
	struct xfs_btree_cur	*cur,
	union xfs_btree_key	*k1,
	union xfs_btree_key	*k2)
{
	return be32_to_cpu(k1->iyesbt.ir_startiyes) <
		be32_to_cpu(k2->iyesbt.ir_startiyes);
}

STATIC int
xfs_iyesbt_recs_iyesrder(
	struct xfs_btree_cur	*cur,
	union xfs_btree_rec	*r1,
	union xfs_btree_rec	*r2)
{
	return be32_to_cpu(r1->iyesbt.ir_startiyes) + XFS_INODES_PER_CHUNK <=
		be32_to_cpu(r2->iyesbt.ir_startiyes);
}

static const struct xfs_btree_ops xfs_iyesbt_ops = {
	.rec_len		= sizeof(xfs_iyesbt_rec_t),
	.key_len		= sizeof(xfs_iyesbt_key_t),

	.dup_cursor		= xfs_iyesbt_dup_cursor,
	.set_root		= xfs_iyesbt_set_root,
	.alloc_block		= xfs_iyesbt_alloc_block,
	.free_block		= xfs_iyesbt_free_block,
	.get_minrecs		= xfs_iyesbt_get_minrecs,
	.get_maxrecs		= xfs_iyesbt_get_maxrecs,
	.init_key_from_rec	= xfs_iyesbt_init_key_from_rec,
	.init_high_key_from_rec	= xfs_iyesbt_init_high_key_from_rec,
	.init_rec_from_cur	= xfs_iyesbt_init_rec_from_cur,
	.init_ptr_from_cur	= xfs_iyesbt_init_ptr_from_cur,
	.key_diff		= xfs_iyesbt_key_diff,
	.buf_ops		= &xfs_iyesbt_buf_ops,
	.diff_two_keys		= xfs_iyesbt_diff_two_keys,
	.keys_iyesrder		= xfs_iyesbt_keys_iyesrder,
	.recs_iyesrder		= xfs_iyesbt_recs_iyesrder,
};

static const struct xfs_btree_ops xfs_fiyesbt_ops = {
	.rec_len		= sizeof(xfs_iyesbt_rec_t),
	.key_len		= sizeof(xfs_iyesbt_key_t),

	.dup_cursor		= xfs_iyesbt_dup_cursor,
	.set_root		= xfs_fiyesbt_set_root,
	.alloc_block		= xfs_fiyesbt_alloc_block,
	.free_block		= xfs_fiyesbt_free_block,
	.get_minrecs		= xfs_iyesbt_get_minrecs,
	.get_maxrecs		= xfs_iyesbt_get_maxrecs,
	.init_key_from_rec	= xfs_iyesbt_init_key_from_rec,
	.init_high_key_from_rec	= xfs_iyesbt_init_high_key_from_rec,
	.init_rec_from_cur	= xfs_iyesbt_init_rec_from_cur,
	.init_ptr_from_cur	= xfs_fiyesbt_init_ptr_from_cur,
	.key_diff		= xfs_iyesbt_key_diff,
	.buf_ops		= &xfs_fiyesbt_buf_ops,
	.diff_two_keys		= xfs_iyesbt_diff_two_keys,
	.keys_iyesrder		= xfs_iyesbt_keys_iyesrder,
	.recs_iyesrder		= xfs_iyesbt_recs_iyesrder,
};

/*
 * Allocate a new iyesde btree cursor.
 */
struct xfs_btree_cur *				/* new iyesde btree cursor */
xfs_iyesbt_init_cursor(
	struct xfs_mount	*mp,		/* file system mount point */
	struct xfs_trans	*tp,		/* transaction pointer */
	struct xfs_buf		*agbp,		/* buffer for agi structure */
	xfs_agnumber_t		agyes,		/* allocation group number */
	xfs_btnum_t		btnum)		/* ialloc or free iyes btree */
{
	struct xfs_agi		*agi = XFS_BUF_TO_AGI(agbp);
	struct xfs_btree_cur	*cur;

	cur = kmem_zone_zalloc(xfs_btree_cur_zone, KM_NOFS);

	cur->bc_tp = tp;
	cur->bc_mp = mp;
	cur->bc_btnum = btnum;
	if (btnum == XFS_BTNUM_INO) {
		cur->bc_nlevels = be32_to_cpu(agi->agi_level);
		cur->bc_ops = &xfs_iyesbt_ops;
		cur->bc_statoff = XFS_STATS_CALC_INDEX(xs_ibt_2);
	} else {
		cur->bc_nlevels = be32_to_cpu(agi->agi_free_level);
		cur->bc_ops = &xfs_fiyesbt_ops;
		cur->bc_statoff = XFS_STATS_CALC_INDEX(xs_fibt_2);
	}

	cur->bc_blocklog = mp->m_sb.sb_blocklog;

	if (xfs_sb_version_hascrc(&mp->m_sb))
		cur->bc_flags |= XFS_BTREE_CRC_BLOCKS;

	cur->bc_private.a.agbp = agbp;
	cur->bc_private.a.agyes = agyes;

	return cur;
}

/*
 * Calculate number of records in an iyesbt btree block.
 */
int
xfs_iyesbt_maxrecs(
	struct xfs_mount	*mp,
	int			blocklen,
	int			leaf)
{
	blocklen -= XFS_INOBT_BLOCK_LEN(mp);

	if (leaf)
		return blocklen / sizeof(xfs_iyesbt_rec_t);
	return blocklen / (sizeof(xfs_iyesbt_key_t) + sizeof(xfs_iyesbt_ptr_t));
}

/*
 * Convert the iyesde record holemask to an iyesde allocation bitmap. The iyesde
 * allocation bitmap is iyesde granularity and specifies whether an iyesde is
 * physically allocated on disk (yest whether the iyesde is considered allocated
 * or free by the fs).
 *
 * A bit value of 1 means the iyesde is allocated, a value of 0 means it is free.
 */
uint64_t
xfs_iyesbt_irec_to_allocmask(
	struct xfs_iyesbt_rec_incore	*rec)
{
	uint64_t			bitmap = 0;
	uint64_t			iyesdespbit;
	int				nextbit;
	uint				allocbitmap;

	/*
	 * The holemask has 16-bits for a 64 iyesde record. Therefore each
	 * holemask bit represents multiple iyesdes. Create a mask of bits to set
	 * in the allocmask for each holemask bit.
	 */
	iyesdespbit = (1 << XFS_INODES_PER_HOLEMASK_BIT) - 1;

	/*
	 * Allocated iyesdes are represented by 0 bits in holemask. Invert the 0
	 * bits to 1 and convert to a uint so we can use xfs_next_bit(). Mask
	 * anything beyond the 16 holemask bits since this casts to a larger
	 * type.
	 */
	allocbitmap = ~rec->ir_holemask & ((1 << XFS_INOBT_HOLEMASK_BITS) - 1);

	/*
	 * allocbitmap is the inverted holemask so every set bit represents
	 * allocated iyesdes. To expand from 16-bit holemask granularity to
	 * 64-bit (e.g., bit-per-iyesde), set iyesdespbit bits in the target
	 * bitmap for every holemask bit.
	 */
	nextbit = xfs_next_bit(&allocbitmap, 1, 0);
	while (nextbit != -1) {
		ASSERT(nextbit < (sizeof(rec->ir_holemask) * NBBY));

		bitmap |= (iyesdespbit <<
			   (nextbit * XFS_INODES_PER_HOLEMASK_BIT));

		nextbit = xfs_next_bit(&allocbitmap, 1, nextbit + 1);
	}

	return bitmap;
}

#if defined(DEBUG) || defined(XFS_WARN)
/*
 * Verify that an in-core iyesde record has a valid iyesde count.
 */
int
xfs_iyesbt_rec_check_count(
	struct xfs_mount		*mp,
	struct xfs_iyesbt_rec_incore	*rec)
{
	int				iyescount = 0;
	int				nextbit = 0;
	uint64_t			allocbmap;
	int				wordsz;

	wordsz = sizeof(allocbmap) / sizeof(unsigned int);
	allocbmap = xfs_iyesbt_irec_to_allocmask(rec);

	nextbit = xfs_next_bit((uint *) &allocbmap, wordsz, nextbit);
	while (nextbit != -1) {
		iyescount++;
		nextbit = xfs_next_bit((uint *) &allocbmap, wordsz,
				       nextbit + 1);
	}

	if (iyescount != rec->ir_count)
		return -EFSCORRUPTED;

	return 0;
}
#endif	/* DEBUG */

static xfs_extlen_t
xfs_iyesbt_max_size(
	struct xfs_mount	*mp,
	xfs_agnumber_t		agyes)
{
	xfs_agblock_t		agblocks = xfs_ag_block_count(mp, agyes);

	/* Bail out if we're uninitialized, which can happen in mkfs. */
	if (M_IGEO(mp)->iyesbt_mxr[0] == 0)
		return 0;

	/*
	 * The log is permanently allocated, so the space it occupies will
	 * never be available for the kinds of things that would require btree
	 * expansion.  We therefore can pretend the space isn't there.
	 */
	if (mp->m_sb.sb_logstart &&
	    XFS_FSB_TO_AGNO(mp, mp->m_sb.sb_logstart) == agyes)
		agblocks -= mp->m_sb.sb_logblocks;

	return xfs_btree_calc_size(M_IGEO(mp)->iyesbt_mnr,
				(uint64_t)agblocks * mp->m_sb.sb_iyespblock /
					XFS_INODES_PER_CHUNK);
}

/* Read AGI and create iyesbt cursor. */
int
xfs_iyesbt_cur(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	xfs_agnumber_t		agyes,
	xfs_btnum_t		which,
	struct xfs_btree_cur	**curpp,
	struct xfs_buf		**agi_bpp)
{
	struct xfs_btree_cur	*cur;
	int			error;

	ASSERT(*agi_bpp == NULL);
	ASSERT(*curpp == NULL);

	error = xfs_ialloc_read_agi(mp, tp, agyes, agi_bpp);
	if (error)
		return error;

	cur = xfs_iyesbt_init_cursor(mp, tp, *agi_bpp, agyes, which);
	if (!cur) {
		xfs_trans_brelse(tp, *agi_bpp);
		*agi_bpp = NULL;
		return -ENOMEM;
	}
	*curpp = cur;
	return 0;
}

static int
xfs_iyesbt_count_blocks(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	xfs_agnumber_t		agyes,
	xfs_btnum_t		btnum,
	xfs_extlen_t		*tree_blocks)
{
	struct xfs_buf		*agbp = NULL;
	struct xfs_btree_cur	*cur = NULL;
	int			error;

	error = xfs_iyesbt_cur(mp, tp, agyes, btnum, &cur, &agbp);
	if (error)
		return error;

	error = xfs_btree_count_blocks(cur, tree_blocks);
	xfs_btree_del_cursor(cur, error);
	xfs_trans_brelse(tp, agbp);

	return error;
}

/*
 * Figure out how many blocks to reserve and how many are used by this btree.
 */
int
xfs_fiyesbt_calc_reserves(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	xfs_agnumber_t		agyes,
	xfs_extlen_t		*ask,
	xfs_extlen_t		*used)
{
	xfs_extlen_t		tree_len = 0;
	int			error;

	if (!xfs_sb_version_hasfiyesbt(&mp->m_sb))
		return 0;

	error = xfs_iyesbt_count_blocks(mp, tp, agyes, XFS_BTNUM_FINO, &tree_len);
	if (error)
		return error;

	*ask += xfs_iyesbt_max_size(mp, agyes);
	*used += tree_len;
	return 0;
}

/* Calculate the iyesbt btree size for some records. */
xfs_extlen_t
xfs_iallocbt_calc_size(
	struct xfs_mount	*mp,
	unsigned long long	len)
{
	return xfs_btree_calc_size(M_IGEO(mp)->iyesbt_mnr, len);
}
