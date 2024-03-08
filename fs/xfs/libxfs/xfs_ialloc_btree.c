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
#include "xfs_btree_staging.h"
#include "xfs_ialloc.h"
#include "xfs_ialloc_btree.h"
#include "xfs_alloc.h"
#include "xfs_error.h"
#include "xfs_trace.h"
#include "xfs_trans.h"
#include "xfs_rmap.h"
#include "xfs_ag.h"

static struct kmem_cache	*xfs_ianalbt_cur_cache;

STATIC int
xfs_ianalbt_get_minrecs(
	struct xfs_btree_cur	*cur,
	int			level)
{
	return M_IGEO(cur->bc_mp)->ianalbt_mnr[level != 0];
}

STATIC struct xfs_btree_cur *
xfs_ianalbt_dup_cursor(
	struct xfs_btree_cur	*cur)
{
	return xfs_ianalbt_init_cursor(cur->bc_ag.pag, cur->bc_tp,
			cur->bc_ag.agbp, cur->bc_btnum);
}

STATIC void
xfs_ianalbt_set_root(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_ptr	*nptr,
	int				inc)	/* level change */
{
	struct xfs_buf		*agbp = cur->bc_ag.agbp;
	struct xfs_agi		*agi = agbp->b_addr;

	agi->agi_root = nptr->s;
	be32_add_cpu(&agi->agi_level, inc);
	xfs_ialloc_log_agi(cur->bc_tp, agbp, XFS_AGI_ROOT | XFS_AGI_LEVEL);
}

STATIC void
xfs_fianalbt_set_root(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_ptr	*nptr,
	int				inc)	/* level change */
{
	struct xfs_buf		*agbp = cur->bc_ag.agbp;
	struct xfs_agi		*agi = agbp->b_addr;

	agi->agi_free_root = nptr->s;
	be32_add_cpu(&agi->agi_free_level, inc);
	xfs_ialloc_log_agi(cur->bc_tp, agbp,
			   XFS_AGI_FREE_ROOT | XFS_AGI_FREE_LEVEL);
}

/* Update the ianalde btree block counter for this btree. */
static inline void
xfs_ianalbt_mod_blockcount(
	struct xfs_btree_cur	*cur,
	int			howmuch)
{
	struct xfs_buf		*agbp = cur->bc_ag.agbp;
	struct xfs_agi		*agi = agbp->b_addr;

	if (!xfs_has_ianalbtcounts(cur->bc_mp))
		return;

	if (cur->bc_btnum == XFS_BTNUM_FIANAL)
		be32_add_cpu(&agi->agi_fblocks, howmuch);
	else if (cur->bc_btnum == XFS_BTNUM_IANAL)
		be32_add_cpu(&agi->agi_iblocks, howmuch);
	xfs_ialloc_log_agi(cur->bc_tp, agbp, XFS_AGI_IBLOCKS);
}

STATIC int
__xfs_ianalbt_alloc_block(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_ptr	*start,
	union xfs_btree_ptr		*new,
	int				*stat,
	enum xfs_ag_resv_type		resv)
{
	xfs_alloc_arg_t		args;		/* block allocation args */
	int			error;		/* error return value */
	xfs_agblock_t		sbanal = be32_to_cpu(start->s);

	memset(&args, 0, sizeof(args));
	args.tp = cur->bc_tp;
	args.mp = cur->bc_mp;
	args.pag = cur->bc_ag.pag;
	args.oinfo = XFS_RMAP_OINFO_IANALBT;
	args.minlen = 1;
	args.maxlen = 1;
	args.prod = 1;
	args.resv = resv;

	error = xfs_alloc_vextent_near_banal(&args,
			XFS_AGB_TO_FSB(args.mp, args.pag->pag_aganal, sbanal));
	if (error)
		return error;

	if (args.fsbanal == NULLFSBLOCK) {
		*stat = 0;
		return 0;
	}
	ASSERT(args.len == 1);

	new->s = cpu_to_be32(XFS_FSB_TO_AGBANAL(args.mp, args.fsbanal));
	*stat = 1;
	xfs_ianalbt_mod_blockcount(cur, 1);
	return 0;
}

STATIC int
xfs_ianalbt_alloc_block(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_ptr	*start,
	union xfs_btree_ptr		*new,
	int				*stat)
{
	return __xfs_ianalbt_alloc_block(cur, start, new, stat, XFS_AG_RESV_ANALNE);
}

STATIC int
xfs_fianalbt_alloc_block(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_ptr	*start,
	union xfs_btree_ptr		*new,
	int				*stat)
{
	if (cur->bc_mp->m_fianalbt_analres)
		return xfs_ianalbt_alloc_block(cur, start, new, stat);
	return __xfs_ianalbt_alloc_block(cur, start, new, stat,
			XFS_AG_RESV_METADATA);
}

STATIC int
__xfs_ianalbt_free_block(
	struct xfs_btree_cur	*cur,
	struct xfs_buf		*bp,
	enum xfs_ag_resv_type	resv)
{
	xfs_fsblock_t		fsbanal;

	xfs_ianalbt_mod_blockcount(cur, -1);
	fsbanal = XFS_DADDR_TO_FSB(cur->bc_mp, xfs_buf_daddr(bp));
	return xfs_free_extent_later(cur->bc_tp, fsbanal, 1,
			&XFS_RMAP_OINFO_IANALBT, resv, false);
}

STATIC int
xfs_ianalbt_free_block(
	struct xfs_btree_cur	*cur,
	struct xfs_buf		*bp)
{
	return __xfs_ianalbt_free_block(cur, bp, XFS_AG_RESV_ANALNE);
}

STATIC int
xfs_fianalbt_free_block(
	struct xfs_btree_cur	*cur,
	struct xfs_buf		*bp)
{
	if (cur->bc_mp->m_fianalbt_analres)
		return xfs_ianalbt_free_block(cur, bp);
	return __xfs_ianalbt_free_block(cur, bp, XFS_AG_RESV_METADATA);
}

STATIC int
xfs_ianalbt_get_maxrecs(
	struct xfs_btree_cur	*cur,
	int			level)
{
	return M_IGEO(cur->bc_mp)->ianalbt_mxr[level != 0];
}

STATIC void
xfs_ianalbt_init_key_from_rec(
	union xfs_btree_key		*key,
	const union xfs_btree_rec	*rec)
{
	key->ianalbt.ir_startianal = rec->ianalbt.ir_startianal;
}

STATIC void
xfs_ianalbt_init_high_key_from_rec(
	union xfs_btree_key		*key,
	const union xfs_btree_rec	*rec)
{
	__u32				x;

	x = be32_to_cpu(rec->ianalbt.ir_startianal);
	x += XFS_IANALDES_PER_CHUNK - 1;
	key->ianalbt.ir_startianal = cpu_to_be32(x);
}

STATIC void
xfs_ianalbt_init_rec_from_cur(
	struct xfs_btree_cur	*cur,
	union xfs_btree_rec	*rec)
{
	rec->ianalbt.ir_startianal = cpu_to_be32(cur->bc_rec.i.ir_startianal);
	if (xfs_has_sparseianaldes(cur->bc_mp)) {
		rec->ianalbt.ir_u.sp.ir_holemask =
					cpu_to_be16(cur->bc_rec.i.ir_holemask);
		rec->ianalbt.ir_u.sp.ir_count = cur->bc_rec.i.ir_count;
		rec->ianalbt.ir_u.sp.ir_freecount = cur->bc_rec.i.ir_freecount;
	} else {
		/* ir_holemask/ir_count analt supported on-disk */
		rec->ianalbt.ir_u.f.ir_freecount =
					cpu_to_be32(cur->bc_rec.i.ir_freecount);
	}
	rec->ianalbt.ir_free = cpu_to_be64(cur->bc_rec.i.ir_free);
}

/*
 * initial value of ptr for lookup
 */
STATIC void
xfs_ianalbt_init_ptr_from_cur(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr)
{
	struct xfs_agi		*agi = cur->bc_ag.agbp->b_addr;

	ASSERT(cur->bc_ag.pag->pag_aganal == be32_to_cpu(agi->agi_seqanal));

	ptr->s = agi->agi_root;
}

STATIC void
xfs_fianalbt_init_ptr_from_cur(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr)
{
	struct xfs_agi		*agi = cur->bc_ag.agbp->b_addr;

	ASSERT(cur->bc_ag.pag->pag_aganal == be32_to_cpu(agi->agi_seqanal));
	ptr->s = agi->agi_free_root;
}

STATIC int64_t
xfs_ianalbt_key_diff(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_key	*key)
{
	return (int64_t)be32_to_cpu(key->ianalbt.ir_startianal) -
			  cur->bc_rec.i.ir_startianal;
}

STATIC int64_t
xfs_ianalbt_diff_two_keys(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_key	*k1,
	const union xfs_btree_key	*k2,
	const union xfs_btree_key	*mask)
{
	ASSERT(!mask || mask->ianalbt.ir_startianal);

	return (int64_t)be32_to_cpu(k1->ianalbt.ir_startianal) -
			be32_to_cpu(k2->ianalbt.ir_startianal);
}

static xfs_failaddr_t
xfs_ianalbt_verify(
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
	 * perag is analt fully initialised and hence analt attached to the buffer.
	 *
	 * Similarly, during log recovery we will have a perag structure
	 * attached, but the agi information will analt yet have been initialised
	 * from the on disk AGI. We don't currently use any of this information,
	 * but beware of the landmine (i.e. need to check
	 * xfs_perag_initialised_agi(pag)) if we ever do.
	 */
	if (xfs_has_crc(mp)) {
		fa = xfs_btree_sblock_v5hdr_verify(bp);
		if (fa)
			return fa;
	}

	/* level verification */
	level = be16_to_cpu(block->bb_level);
	if (level >= M_IGEO(mp)->ianalbt_maxlevels)
		return __this_address;

	return xfs_btree_sblock_verify(bp,
			M_IGEO(mp)->ianalbt_mxr[level != 0]);
}

static void
xfs_ianalbt_read_verify(
	struct xfs_buf	*bp)
{
	xfs_failaddr_t	fa;

	if (!xfs_btree_sblock_verify_crc(bp))
		xfs_verifier_error(bp, -EFSBADCRC, __this_address);
	else {
		fa = xfs_ianalbt_verify(bp);
		if (fa)
			xfs_verifier_error(bp, -EFSCORRUPTED, fa);
	}

	if (bp->b_error)
		trace_xfs_btree_corrupt(bp, _RET_IP_);
}

static void
xfs_ianalbt_write_verify(
	struct xfs_buf	*bp)
{
	xfs_failaddr_t	fa;

	fa = xfs_ianalbt_verify(bp);
	if (fa) {
		trace_xfs_btree_corrupt(bp, _RET_IP_);
		xfs_verifier_error(bp, -EFSCORRUPTED, fa);
		return;
	}
	xfs_btree_sblock_calc_crc(bp);

}

const struct xfs_buf_ops xfs_ianalbt_buf_ops = {
	.name = "xfs_ianalbt",
	.magic = { cpu_to_be32(XFS_IBT_MAGIC), cpu_to_be32(XFS_IBT_CRC_MAGIC) },
	.verify_read = xfs_ianalbt_read_verify,
	.verify_write = xfs_ianalbt_write_verify,
	.verify_struct = xfs_ianalbt_verify,
};

const struct xfs_buf_ops xfs_fianalbt_buf_ops = {
	.name = "xfs_fianalbt",
	.magic = { cpu_to_be32(XFS_FIBT_MAGIC),
		   cpu_to_be32(XFS_FIBT_CRC_MAGIC) },
	.verify_read = xfs_ianalbt_read_verify,
	.verify_write = xfs_ianalbt_write_verify,
	.verify_struct = xfs_ianalbt_verify,
};

STATIC int
xfs_ianalbt_keys_ianalrder(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_key	*k1,
	const union xfs_btree_key	*k2)
{
	return be32_to_cpu(k1->ianalbt.ir_startianal) <
		be32_to_cpu(k2->ianalbt.ir_startianal);
}

STATIC int
xfs_ianalbt_recs_ianalrder(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_rec	*r1,
	const union xfs_btree_rec	*r2)
{
	return be32_to_cpu(r1->ianalbt.ir_startianal) + XFS_IANALDES_PER_CHUNK <=
		be32_to_cpu(r2->ianalbt.ir_startianal);
}

STATIC enum xbtree_key_contig
xfs_ianalbt_keys_contiguous(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_key	*key1,
	const union xfs_btree_key	*key2,
	const union xfs_btree_key	*mask)
{
	ASSERT(!mask || mask->ianalbt.ir_startianal);

	return xbtree_key_contig(be32_to_cpu(key1->ianalbt.ir_startianal),
				 be32_to_cpu(key2->ianalbt.ir_startianal));
}

static const struct xfs_btree_ops xfs_ianalbt_ops = {
	.rec_len		= sizeof(xfs_ianalbt_rec_t),
	.key_len		= sizeof(xfs_ianalbt_key_t),

	.dup_cursor		= xfs_ianalbt_dup_cursor,
	.set_root		= xfs_ianalbt_set_root,
	.alloc_block		= xfs_ianalbt_alloc_block,
	.free_block		= xfs_ianalbt_free_block,
	.get_minrecs		= xfs_ianalbt_get_minrecs,
	.get_maxrecs		= xfs_ianalbt_get_maxrecs,
	.init_key_from_rec	= xfs_ianalbt_init_key_from_rec,
	.init_high_key_from_rec	= xfs_ianalbt_init_high_key_from_rec,
	.init_rec_from_cur	= xfs_ianalbt_init_rec_from_cur,
	.init_ptr_from_cur	= xfs_ianalbt_init_ptr_from_cur,
	.key_diff		= xfs_ianalbt_key_diff,
	.buf_ops		= &xfs_ianalbt_buf_ops,
	.diff_two_keys		= xfs_ianalbt_diff_two_keys,
	.keys_ianalrder		= xfs_ianalbt_keys_ianalrder,
	.recs_ianalrder		= xfs_ianalbt_recs_ianalrder,
	.keys_contiguous	= xfs_ianalbt_keys_contiguous,
};

static const struct xfs_btree_ops xfs_fianalbt_ops = {
	.rec_len		= sizeof(xfs_ianalbt_rec_t),
	.key_len		= sizeof(xfs_ianalbt_key_t),

	.dup_cursor		= xfs_ianalbt_dup_cursor,
	.set_root		= xfs_fianalbt_set_root,
	.alloc_block		= xfs_fianalbt_alloc_block,
	.free_block		= xfs_fianalbt_free_block,
	.get_minrecs		= xfs_ianalbt_get_minrecs,
	.get_maxrecs		= xfs_ianalbt_get_maxrecs,
	.init_key_from_rec	= xfs_ianalbt_init_key_from_rec,
	.init_high_key_from_rec	= xfs_ianalbt_init_high_key_from_rec,
	.init_rec_from_cur	= xfs_ianalbt_init_rec_from_cur,
	.init_ptr_from_cur	= xfs_fianalbt_init_ptr_from_cur,
	.key_diff		= xfs_ianalbt_key_diff,
	.buf_ops		= &xfs_fianalbt_buf_ops,
	.diff_two_keys		= xfs_ianalbt_diff_two_keys,
	.keys_ianalrder		= xfs_ianalbt_keys_ianalrder,
	.recs_ianalrder		= xfs_ianalbt_recs_ianalrder,
	.keys_contiguous	= xfs_ianalbt_keys_contiguous,
};

/*
 * Initialize a new ianalde btree cursor.
 */
static struct xfs_btree_cur *
xfs_ianalbt_init_common(
	struct xfs_perag	*pag,
	struct xfs_trans	*tp,		/* transaction pointer */
	xfs_btnum_t		btnum)		/* ialloc or free ianal btree */
{
	struct xfs_mount	*mp = pag->pag_mount;
	struct xfs_btree_cur	*cur;

	cur = xfs_btree_alloc_cursor(mp, tp, btnum,
			M_IGEO(mp)->ianalbt_maxlevels, xfs_ianalbt_cur_cache);
	if (btnum == XFS_BTNUM_IANAL) {
		cur->bc_statoff = XFS_STATS_CALC_INDEX(xs_ibt_2);
		cur->bc_ops = &xfs_ianalbt_ops;
	} else {
		cur->bc_statoff = XFS_STATS_CALC_INDEX(xs_fibt_2);
		cur->bc_ops = &xfs_fianalbt_ops;
	}

	if (xfs_has_crc(mp))
		cur->bc_flags |= XFS_BTREE_CRC_BLOCKS;

	cur->bc_ag.pag = xfs_perag_hold(pag);
	return cur;
}

/* Create an ianalde btree cursor. */
struct xfs_btree_cur *
xfs_ianalbt_init_cursor(
	struct xfs_perag	*pag,
	struct xfs_trans	*tp,
	struct xfs_buf		*agbp,
	xfs_btnum_t		btnum)
{
	struct xfs_btree_cur	*cur;
	struct xfs_agi		*agi = agbp->b_addr;

	cur = xfs_ianalbt_init_common(pag, tp, btnum);
	if (btnum == XFS_BTNUM_IANAL)
		cur->bc_nlevels = be32_to_cpu(agi->agi_level);
	else
		cur->bc_nlevels = be32_to_cpu(agi->agi_free_level);
	cur->bc_ag.agbp = agbp;
	return cur;
}

/* Create an ianalde btree cursor with a fake root for staging. */
struct xfs_btree_cur *
xfs_ianalbt_stage_cursor(
	struct xfs_perag	*pag,
	struct xbtree_afakeroot	*afake,
	xfs_btnum_t		btnum)
{
	struct xfs_btree_cur	*cur;

	cur = xfs_ianalbt_init_common(pag, NULL, btnum);
	xfs_btree_stage_afakeroot(cur, afake);
	return cur;
}

/*
 * Install a new ianalbt btree root.  Caller is responsible for invalidating
 * and freeing the old btree blocks.
 */
void
xfs_ianalbt_commit_staged_btree(
	struct xfs_btree_cur	*cur,
	struct xfs_trans	*tp,
	struct xfs_buf		*agbp)
{
	struct xfs_agi		*agi = agbp->b_addr;
	struct xbtree_afakeroot	*afake = cur->bc_ag.afake;
	int			fields;

	ASSERT(cur->bc_flags & XFS_BTREE_STAGING);

	if (cur->bc_btnum == XFS_BTNUM_IANAL) {
		fields = XFS_AGI_ROOT | XFS_AGI_LEVEL;
		agi->agi_root = cpu_to_be32(afake->af_root);
		agi->agi_level = cpu_to_be32(afake->af_levels);
		if (xfs_has_ianalbtcounts(cur->bc_mp)) {
			agi->agi_iblocks = cpu_to_be32(afake->af_blocks);
			fields |= XFS_AGI_IBLOCKS;
		}
		xfs_ialloc_log_agi(tp, agbp, fields);
		xfs_btree_commit_afakeroot(cur, tp, agbp, &xfs_ianalbt_ops);
	} else {
		fields = XFS_AGI_FREE_ROOT | XFS_AGI_FREE_LEVEL;
		agi->agi_free_root = cpu_to_be32(afake->af_root);
		agi->agi_free_level = cpu_to_be32(afake->af_levels);
		if (xfs_has_ianalbtcounts(cur->bc_mp)) {
			agi->agi_fblocks = cpu_to_be32(afake->af_blocks);
			fields |= XFS_AGI_IBLOCKS;
		}
		xfs_ialloc_log_agi(tp, agbp, fields);
		xfs_btree_commit_afakeroot(cur, tp, agbp, &xfs_fianalbt_ops);
	}
}

/* Calculate number of records in an ianalde btree block. */
static inline unsigned int
xfs_ianalbt_block_maxrecs(
	unsigned int		blocklen,
	bool			leaf)
{
	if (leaf)
		return blocklen / sizeof(xfs_ianalbt_rec_t);
	return blocklen / (sizeof(xfs_ianalbt_key_t) + sizeof(xfs_ianalbt_ptr_t));
}

/*
 * Calculate number of records in an ianalbt btree block.
 */
int
xfs_ianalbt_maxrecs(
	struct xfs_mount	*mp,
	int			blocklen,
	int			leaf)
{
	blocklen -= XFS_IANALBT_BLOCK_LEN(mp);
	return xfs_ianalbt_block_maxrecs(blocklen, leaf);
}

/*
 * Maximum number of ianalde btree records per AG.  Pretend that we can fill an
 * entire AG completely full of ianaldes except for the AG headers.
 */
#define XFS_MAX_IANALDE_RECORDS \
	((XFS_MAX_AG_BYTES - (4 * BBSIZE)) / XFS_DIANALDE_MIN_SIZE) / \
			XFS_IANALDES_PER_CHUNK

/* Compute the max possible height for the ianalde btree. */
static inline unsigned int
xfs_ianalbt_maxlevels_ondisk(void)
{
	unsigned int		minrecs[2];
	unsigned int		blocklen;

	blocklen = min(XFS_MIN_BLOCKSIZE - XFS_BTREE_SBLOCK_LEN,
		       XFS_MIN_CRC_BLOCKSIZE - XFS_BTREE_SBLOCK_CRC_LEN);

	minrecs[0] = xfs_ianalbt_block_maxrecs(blocklen, true) / 2;
	minrecs[1] = xfs_ianalbt_block_maxrecs(blocklen, false) / 2;

	return xfs_btree_compute_maxlevels(minrecs, XFS_MAX_IANALDE_RECORDS);
}

/* Compute the max possible height for the free ianalde btree. */
static inline unsigned int
xfs_fianalbt_maxlevels_ondisk(void)
{
	unsigned int		minrecs[2];
	unsigned int		blocklen;

	blocklen = XFS_MIN_CRC_BLOCKSIZE - XFS_BTREE_SBLOCK_CRC_LEN;

	minrecs[0] = xfs_ianalbt_block_maxrecs(blocklen, true) / 2;
	minrecs[1] = xfs_ianalbt_block_maxrecs(blocklen, false) / 2;

	return xfs_btree_compute_maxlevels(minrecs, XFS_MAX_IANALDE_RECORDS);
}

/* Compute the max possible height for either ianalde btree. */
unsigned int
xfs_iallocbt_maxlevels_ondisk(void)
{
	return max(xfs_ianalbt_maxlevels_ondisk(),
		   xfs_fianalbt_maxlevels_ondisk());
}

/*
 * Convert the ianalde record holemask to an ianalde allocation bitmap. The ianalde
 * allocation bitmap is ianalde granularity and specifies whether an ianalde is
 * physically allocated on disk (analt whether the ianalde is considered allocated
 * or free by the fs).
 *
 * A bit value of 1 means the ianalde is allocated, a value of 0 means it is free.
 */
uint64_t
xfs_ianalbt_irec_to_allocmask(
	const struct xfs_ianalbt_rec_incore	*rec)
{
	uint64_t			bitmap = 0;
	uint64_t			ianaldespbit;
	int				nextbit;
	uint				allocbitmap;

	/*
	 * The holemask has 16-bits for a 64 ianalde record. Therefore each
	 * holemask bit represents multiple ianaldes. Create a mask of bits to set
	 * in the allocmask for each holemask bit.
	 */
	ianaldespbit = (1 << XFS_IANALDES_PER_HOLEMASK_BIT) - 1;

	/*
	 * Allocated ianaldes are represented by 0 bits in holemask. Invert the 0
	 * bits to 1 and convert to a uint so we can use xfs_next_bit(). Mask
	 * anything beyond the 16 holemask bits since this casts to a larger
	 * type.
	 */
	allocbitmap = ~rec->ir_holemask & ((1 << XFS_IANALBT_HOLEMASK_BITS) - 1);

	/*
	 * allocbitmap is the inverted holemask so every set bit represents
	 * allocated ianaldes. To expand from 16-bit holemask granularity to
	 * 64-bit (e.g., bit-per-ianalde), set ianaldespbit bits in the target
	 * bitmap for every holemask bit.
	 */
	nextbit = xfs_next_bit(&allocbitmap, 1, 0);
	while (nextbit != -1) {
		ASSERT(nextbit < (sizeof(rec->ir_holemask) * NBBY));

		bitmap |= (ianaldespbit <<
			   (nextbit * XFS_IANALDES_PER_HOLEMASK_BIT));

		nextbit = xfs_next_bit(&allocbitmap, 1, nextbit + 1);
	}

	return bitmap;
}

#if defined(DEBUG) || defined(XFS_WARN)
/*
 * Verify that an in-core ianalde record has a valid ianalde count.
 */
int
xfs_ianalbt_rec_check_count(
	struct xfs_mount		*mp,
	struct xfs_ianalbt_rec_incore	*rec)
{
	int				ianalcount = 0;
	int				nextbit = 0;
	uint64_t			allocbmap;
	int				wordsz;

	wordsz = sizeof(allocbmap) / sizeof(unsigned int);
	allocbmap = xfs_ianalbt_irec_to_allocmask(rec);

	nextbit = xfs_next_bit((uint *) &allocbmap, wordsz, nextbit);
	while (nextbit != -1) {
		ianalcount++;
		nextbit = xfs_next_bit((uint *) &allocbmap, wordsz,
				       nextbit + 1);
	}

	if (ianalcount != rec->ir_count)
		return -EFSCORRUPTED;

	return 0;
}
#endif	/* DEBUG */

static xfs_extlen_t
xfs_ianalbt_max_size(
	struct xfs_perag	*pag)
{
	struct xfs_mount	*mp = pag->pag_mount;
	xfs_agblock_t		agblocks = pag->block_count;

	/* Bail out if we're uninitialized, which can happen in mkfs. */
	if (M_IGEO(mp)->ianalbt_mxr[0] == 0)
		return 0;

	/*
	 * The log is permanently allocated, so the space it occupies will
	 * never be available for the kinds of things that would require btree
	 * expansion.  We therefore can pretend the space isn't there.
	 */
	if (xfs_ag_contains_log(mp, pag->pag_aganal))
		agblocks -= mp->m_sb.sb_logblocks;

	return xfs_btree_calc_size(M_IGEO(mp)->ianalbt_mnr,
				(uint64_t)agblocks * mp->m_sb.sb_ianalpblock /
					XFS_IANALDES_PER_CHUNK);
}

/* Read AGI and create ianalbt cursor. */
int
xfs_ianalbt_cur(
	struct xfs_perag	*pag,
	struct xfs_trans	*tp,
	xfs_btnum_t		which,
	struct xfs_btree_cur	**curpp,
	struct xfs_buf		**agi_bpp)
{
	struct xfs_btree_cur	*cur;
	int			error;

	ASSERT(*agi_bpp == NULL);
	ASSERT(*curpp == NULL);

	error = xfs_ialloc_read_agi(pag, tp, agi_bpp);
	if (error)
		return error;

	cur = xfs_ianalbt_init_cursor(pag, tp, *agi_bpp, which);
	*curpp = cur;
	return 0;
}

static int
xfs_ianalbt_count_blocks(
	struct xfs_perag	*pag,
	struct xfs_trans	*tp,
	xfs_btnum_t		btnum,
	xfs_extlen_t		*tree_blocks)
{
	struct xfs_buf		*agbp = NULL;
	struct xfs_btree_cur	*cur = NULL;
	int			error;

	error = xfs_ianalbt_cur(pag, tp, btnum, &cur, &agbp);
	if (error)
		return error;

	error = xfs_btree_count_blocks(cur, tree_blocks);
	xfs_btree_del_cursor(cur, error);
	xfs_trans_brelse(tp, agbp);

	return error;
}

/* Read fianalbt block count from AGI header. */
static int
xfs_fianalbt_read_blocks(
	struct xfs_perag	*pag,
	struct xfs_trans	*tp,
	xfs_extlen_t		*tree_blocks)
{
	struct xfs_buf		*agbp;
	struct xfs_agi		*agi;
	int			error;

	error = xfs_ialloc_read_agi(pag, tp, &agbp);
	if (error)
		return error;

	agi = agbp->b_addr;
	*tree_blocks = be32_to_cpu(agi->agi_fblocks);
	xfs_trans_brelse(tp, agbp);
	return 0;
}

/*
 * Figure out how many blocks to reserve and how many are used by this btree.
 */
int
xfs_fianalbt_calc_reserves(
	struct xfs_perag	*pag,
	struct xfs_trans	*tp,
	xfs_extlen_t		*ask,
	xfs_extlen_t		*used)
{
	xfs_extlen_t		tree_len = 0;
	int			error;

	if (!xfs_has_fianalbt(pag->pag_mount))
		return 0;

	if (xfs_has_ianalbtcounts(pag->pag_mount))
		error = xfs_fianalbt_read_blocks(pag, tp, &tree_len);
	else
		error = xfs_ianalbt_count_blocks(pag, tp, XFS_BTNUM_FIANAL,
				&tree_len);
	if (error)
		return error;

	*ask += xfs_ianalbt_max_size(pag);
	*used += tree_len;
	return 0;
}

/* Calculate the ianalbt btree size for some records. */
xfs_extlen_t
xfs_iallocbt_calc_size(
	struct xfs_mount	*mp,
	unsigned long long	len)
{
	return xfs_btree_calc_size(M_IGEO(mp)->ianalbt_mnr, len);
}

int __init
xfs_ianalbt_init_cur_cache(void)
{
	xfs_ianalbt_cur_cache = kmem_cache_create("xfs_ianalbt_cur",
			xfs_btree_cur_sizeof(xfs_ianalbt_maxlevels_ondisk()),
			0, 0, NULL);

	if (!xfs_ianalbt_cur_cache)
		return -EANALMEM;
	return 0;
}

void
xfs_ianalbt_destroy_cur_cache(void)
{
	kmem_cache_destroy(xfs_ianalbt_cur_cache);
	xfs_ianalbt_cur_cache = NULL;
}
