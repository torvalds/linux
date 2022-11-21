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

static struct kmem_cache	*xfs_inobt_cur_cache;

STATIC int
xfs_inobt_get_minrecs(
	struct xfs_btree_cur	*cur,
	int			level)
{
	return M_IGEO(cur->bc_mp)->inobt_mnr[level != 0];
}

STATIC struct xfs_btree_cur *
xfs_inobt_dup_cursor(
	struct xfs_btree_cur	*cur)
{
	return xfs_inobt_init_cursor(cur->bc_mp, cur->bc_tp,
			cur->bc_ag.agbp, cur->bc_ag.pag, cur->bc_btnum);
}

STATIC void
xfs_inobt_set_root(
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
xfs_finobt_set_root(
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

/* Update the inode btree block counter for this btree. */
static inline void
xfs_inobt_mod_blockcount(
	struct xfs_btree_cur	*cur,
	int			howmuch)
{
	struct xfs_buf		*agbp = cur->bc_ag.agbp;
	struct xfs_agi		*agi = agbp->b_addr;

	if (!xfs_has_inobtcounts(cur->bc_mp))
		return;

	if (cur->bc_btnum == XFS_BTNUM_FINO)
		be32_add_cpu(&agi->agi_fblocks, howmuch);
	else if (cur->bc_btnum == XFS_BTNUM_INO)
		be32_add_cpu(&agi->agi_iblocks, howmuch);
	xfs_ialloc_log_agi(cur->bc_tp, agbp, XFS_AGI_IBLOCKS);
}

STATIC int
__xfs_inobt_alloc_block(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_ptr	*start,
	union xfs_btree_ptr		*new,
	int				*stat,
	enum xfs_ag_resv_type		resv)
{
	xfs_alloc_arg_t		args;		/* block allocation args */
	int			error;		/* error return value */
	xfs_agblock_t		sbno = be32_to_cpu(start->s);

	memset(&args, 0, sizeof(args));
	args.tp = cur->bc_tp;
	args.mp = cur->bc_mp;
	args.oinfo = XFS_RMAP_OINFO_INOBT;
	args.fsbno = XFS_AGB_TO_FSB(args.mp, cur->bc_ag.pag->pag_agno, sbno);
	args.minlen = 1;
	args.maxlen = 1;
	args.prod = 1;
	args.type = XFS_ALLOCTYPE_NEAR_BNO;
	args.resv = resv;

	error = xfs_alloc_vextent(&args);
	if (error)
		return error;

	if (args.fsbno == NULLFSBLOCK) {
		*stat = 0;
		return 0;
	}
	ASSERT(args.len == 1);

	new->s = cpu_to_be32(XFS_FSB_TO_AGBNO(args.mp, args.fsbno));
	*stat = 1;
	xfs_inobt_mod_blockcount(cur, 1);
	return 0;
}

STATIC int
xfs_inobt_alloc_block(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_ptr	*start,
	union xfs_btree_ptr		*new,
	int				*stat)
{
	return __xfs_inobt_alloc_block(cur, start, new, stat, XFS_AG_RESV_NONE);
}

STATIC int
xfs_finobt_alloc_block(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_ptr	*start,
	union xfs_btree_ptr		*new,
	int				*stat)
{
	if (cur->bc_mp->m_finobt_nores)
		return xfs_inobt_alloc_block(cur, start, new, stat);
	return __xfs_inobt_alloc_block(cur, start, new, stat,
			XFS_AG_RESV_METADATA);
}

STATIC int
__xfs_inobt_free_block(
	struct xfs_btree_cur	*cur,
	struct xfs_buf		*bp,
	enum xfs_ag_resv_type	resv)
{
	xfs_inobt_mod_blockcount(cur, -1);
	return xfs_free_extent(cur->bc_tp,
			XFS_DADDR_TO_FSB(cur->bc_mp, xfs_buf_daddr(bp)), 1,
			&XFS_RMAP_OINFO_INOBT, resv);
}

STATIC int
xfs_inobt_free_block(
	struct xfs_btree_cur	*cur,
	struct xfs_buf		*bp)
{
	return __xfs_inobt_free_block(cur, bp, XFS_AG_RESV_NONE);
}

STATIC int
xfs_finobt_free_block(
	struct xfs_btree_cur	*cur,
	struct xfs_buf		*bp)
{
	if (cur->bc_mp->m_finobt_nores)
		return xfs_inobt_free_block(cur, bp);
	return __xfs_inobt_free_block(cur, bp, XFS_AG_RESV_METADATA);
}

STATIC int
xfs_inobt_get_maxrecs(
	struct xfs_btree_cur	*cur,
	int			level)
{
	return M_IGEO(cur->bc_mp)->inobt_mxr[level != 0];
}

STATIC void
xfs_inobt_init_key_from_rec(
	union xfs_btree_key		*key,
	const union xfs_btree_rec	*rec)
{
	key->inobt.ir_startino = rec->inobt.ir_startino;
}

STATIC void
xfs_inobt_init_high_key_from_rec(
	union xfs_btree_key		*key,
	const union xfs_btree_rec	*rec)
{
	__u32				x;

	x = be32_to_cpu(rec->inobt.ir_startino);
	x += XFS_INODES_PER_CHUNK - 1;
	key->inobt.ir_startino = cpu_to_be32(x);
}

STATIC void
xfs_inobt_init_rec_from_cur(
	struct xfs_btree_cur	*cur,
	union xfs_btree_rec	*rec)
{
	rec->inobt.ir_startino = cpu_to_be32(cur->bc_rec.i.ir_startino);
	if (xfs_has_sparseinodes(cur->bc_mp)) {
		rec->inobt.ir_u.sp.ir_holemask =
					cpu_to_be16(cur->bc_rec.i.ir_holemask);
		rec->inobt.ir_u.sp.ir_count = cur->bc_rec.i.ir_count;
		rec->inobt.ir_u.sp.ir_freecount = cur->bc_rec.i.ir_freecount;
	} else {
		/* ir_holemask/ir_count not supported on-disk */
		rec->inobt.ir_u.f.ir_freecount =
					cpu_to_be32(cur->bc_rec.i.ir_freecount);
	}
	rec->inobt.ir_free = cpu_to_be64(cur->bc_rec.i.ir_free);
}

/*
 * initial value of ptr for lookup
 */
STATIC void
xfs_inobt_init_ptr_from_cur(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr)
{
	struct xfs_agi		*agi = cur->bc_ag.agbp->b_addr;

	ASSERT(cur->bc_ag.pag->pag_agno == be32_to_cpu(agi->agi_seqno));

	ptr->s = agi->agi_root;
}

STATIC void
xfs_finobt_init_ptr_from_cur(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr)
{
	struct xfs_agi		*agi = cur->bc_ag.agbp->b_addr;

	ASSERT(cur->bc_ag.pag->pag_agno == be32_to_cpu(agi->agi_seqno));
	ptr->s = agi->agi_free_root;
}

STATIC int64_t
xfs_inobt_key_diff(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_key	*key)
{
	return (int64_t)be32_to_cpu(key->inobt.ir_startino) -
			  cur->bc_rec.i.ir_startino;
}

STATIC int64_t
xfs_inobt_diff_two_keys(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_key	*k1,
	const union xfs_btree_key	*k2)
{
	return (int64_t)be32_to_cpu(k1->inobt.ir_startino) -
			  be32_to_cpu(k2->inobt.ir_startino);
}

static xfs_failaddr_t
xfs_inobt_verify(
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
	 * perag is not fully initialised and hence not attached to the buffer.
	 *
	 * Similarly, during log recovery we will have a perag structure
	 * attached, but the agi information will not yet have been initialised
	 * from the on disk AGI. We don't currently use any of this information,
	 * but beware of the landmine (i.e. need to check pag->pagi_init) if we
	 * ever do.
	 */
	if (xfs_has_crc(mp)) {
		fa = xfs_btree_sblock_v5hdr_verify(bp);
		if (fa)
			return fa;
	}

	/* level verification */
	level = be16_to_cpu(block->bb_level);
	if (level >= M_IGEO(mp)->inobt_maxlevels)
		return __this_address;

	return xfs_btree_sblock_verify(bp,
			M_IGEO(mp)->inobt_mxr[level != 0]);
}

static void
xfs_inobt_read_verify(
	struct xfs_buf	*bp)
{
	xfs_failaddr_t	fa;

	if (!xfs_btree_sblock_verify_crc(bp))
		xfs_verifier_error(bp, -EFSBADCRC, __this_address);
	else {
		fa = xfs_inobt_verify(bp);
		if (fa)
			xfs_verifier_error(bp, -EFSCORRUPTED, fa);
	}

	if (bp->b_error)
		trace_xfs_btree_corrupt(bp, _RET_IP_);
}

static void
xfs_inobt_write_verify(
	struct xfs_buf	*bp)
{
	xfs_failaddr_t	fa;

	fa = xfs_inobt_verify(bp);
	if (fa) {
		trace_xfs_btree_corrupt(bp, _RET_IP_);
		xfs_verifier_error(bp, -EFSCORRUPTED, fa);
		return;
	}
	xfs_btree_sblock_calc_crc(bp);

}

const struct xfs_buf_ops xfs_inobt_buf_ops = {
	.name = "xfs_inobt",
	.magic = { cpu_to_be32(XFS_IBT_MAGIC), cpu_to_be32(XFS_IBT_CRC_MAGIC) },
	.verify_read = xfs_inobt_read_verify,
	.verify_write = xfs_inobt_write_verify,
	.verify_struct = xfs_inobt_verify,
};

const struct xfs_buf_ops xfs_finobt_buf_ops = {
	.name = "xfs_finobt",
	.magic = { cpu_to_be32(XFS_FIBT_MAGIC),
		   cpu_to_be32(XFS_FIBT_CRC_MAGIC) },
	.verify_read = xfs_inobt_read_verify,
	.verify_write = xfs_inobt_write_verify,
	.verify_struct = xfs_inobt_verify,
};

STATIC int
xfs_inobt_keys_inorder(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_key	*k1,
	const union xfs_btree_key	*k2)
{
	return be32_to_cpu(k1->inobt.ir_startino) <
		be32_to_cpu(k2->inobt.ir_startino);
}

STATIC int
xfs_inobt_recs_inorder(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_rec	*r1,
	const union xfs_btree_rec	*r2)
{
	return be32_to_cpu(r1->inobt.ir_startino) + XFS_INODES_PER_CHUNK <=
		be32_to_cpu(r2->inobt.ir_startino);
}

static const struct xfs_btree_ops xfs_inobt_ops = {
	.rec_len		= sizeof(xfs_inobt_rec_t),
	.key_len		= sizeof(xfs_inobt_key_t),

	.dup_cursor		= xfs_inobt_dup_cursor,
	.set_root		= xfs_inobt_set_root,
	.alloc_block		= xfs_inobt_alloc_block,
	.free_block		= xfs_inobt_free_block,
	.get_minrecs		= xfs_inobt_get_minrecs,
	.get_maxrecs		= xfs_inobt_get_maxrecs,
	.init_key_from_rec	= xfs_inobt_init_key_from_rec,
	.init_high_key_from_rec	= xfs_inobt_init_high_key_from_rec,
	.init_rec_from_cur	= xfs_inobt_init_rec_from_cur,
	.init_ptr_from_cur	= xfs_inobt_init_ptr_from_cur,
	.key_diff		= xfs_inobt_key_diff,
	.buf_ops		= &xfs_inobt_buf_ops,
	.diff_two_keys		= xfs_inobt_diff_two_keys,
	.keys_inorder		= xfs_inobt_keys_inorder,
	.recs_inorder		= xfs_inobt_recs_inorder,
};

static const struct xfs_btree_ops xfs_finobt_ops = {
	.rec_len		= sizeof(xfs_inobt_rec_t),
	.key_len		= sizeof(xfs_inobt_key_t),

	.dup_cursor		= xfs_inobt_dup_cursor,
	.set_root		= xfs_finobt_set_root,
	.alloc_block		= xfs_finobt_alloc_block,
	.free_block		= xfs_finobt_free_block,
	.get_minrecs		= xfs_inobt_get_minrecs,
	.get_maxrecs		= xfs_inobt_get_maxrecs,
	.init_key_from_rec	= xfs_inobt_init_key_from_rec,
	.init_high_key_from_rec	= xfs_inobt_init_high_key_from_rec,
	.init_rec_from_cur	= xfs_inobt_init_rec_from_cur,
	.init_ptr_from_cur	= xfs_finobt_init_ptr_from_cur,
	.key_diff		= xfs_inobt_key_diff,
	.buf_ops		= &xfs_finobt_buf_ops,
	.diff_two_keys		= xfs_inobt_diff_two_keys,
	.keys_inorder		= xfs_inobt_keys_inorder,
	.recs_inorder		= xfs_inobt_recs_inorder,
};

/*
 * Initialize a new inode btree cursor.
 */
static struct xfs_btree_cur *
xfs_inobt_init_common(
	struct xfs_mount	*mp,		/* file system mount point */
	struct xfs_trans	*tp,		/* transaction pointer */
	struct xfs_perag	*pag,
	xfs_btnum_t		btnum)		/* ialloc or free ino btree */
{
	struct xfs_btree_cur	*cur;

	cur = xfs_btree_alloc_cursor(mp, tp, btnum,
			M_IGEO(mp)->inobt_maxlevels, xfs_inobt_cur_cache);
	if (btnum == XFS_BTNUM_INO) {
		cur->bc_statoff = XFS_STATS_CALC_INDEX(xs_ibt_2);
		cur->bc_ops = &xfs_inobt_ops;
	} else {
		cur->bc_statoff = XFS_STATS_CALC_INDEX(xs_fibt_2);
		cur->bc_ops = &xfs_finobt_ops;
	}

	if (xfs_has_crc(mp))
		cur->bc_flags |= XFS_BTREE_CRC_BLOCKS;

	/* take a reference for the cursor */
	atomic_inc(&pag->pag_ref);
	cur->bc_ag.pag = pag;
	return cur;
}

/* Create an inode btree cursor. */
struct xfs_btree_cur *
xfs_inobt_init_cursor(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	struct xfs_buf		*agbp,
	struct xfs_perag	*pag,
	xfs_btnum_t		btnum)
{
	struct xfs_btree_cur	*cur;
	struct xfs_agi		*agi = agbp->b_addr;

	cur = xfs_inobt_init_common(mp, tp, pag, btnum);
	if (btnum == XFS_BTNUM_INO)
		cur->bc_nlevels = be32_to_cpu(agi->agi_level);
	else
		cur->bc_nlevels = be32_to_cpu(agi->agi_free_level);
	cur->bc_ag.agbp = agbp;
	return cur;
}

/* Create an inode btree cursor with a fake root for staging. */
struct xfs_btree_cur *
xfs_inobt_stage_cursor(
	struct xfs_mount	*mp,
	struct xbtree_afakeroot	*afake,
	struct xfs_perag	*pag,
	xfs_btnum_t		btnum)
{
	struct xfs_btree_cur	*cur;

	cur = xfs_inobt_init_common(mp, NULL, pag, btnum);
	xfs_btree_stage_afakeroot(cur, afake);
	return cur;
}

/*
 * Install a new inobt btree root.  Caller is responsible for invalidating
 * and freeing the old btree blocks.
 */
void
xfs_inobt_commit_staged_btree(
	struct xfs_btree_cur	*cur,
	struct xfs_trans	*tp,
	struct xfs_buf		*agbp)
{
	struct xfs_agi		*agi = agbp->b_addr;
	struct xbtree_afakeroot	*afake = cur->bc_ag.afake;
	int			fields;

	ASSERT(cur->bc_flags & XFS_BTREE_STAGING);

	if (cur->bc_btnum == XFS_BTNUM_INO) {
		fields = XFS_AGI_ROOT | XFS_AGI_LEVEL;
		agi->agi_root = cpu_to_be32(afake->af_root);
		agi->agi_level = cpu_to_be32(afake->af_levels);
		if (xfs_has_inobtcounts(cur->bc_mp)) {
			agi->agi_iblocks = cpu_to_be32(afake->af_blocks);
			fields |= XFS_AGI_IBLOCKS;
		}
		xfs_ialloc_log_agi(tp, agbp, fields);
		xfs_btree_commit_afakeroot(cur, tp, agbp, &xfs_inobt_ops);
	} else {
		fields = XFS_AGI_FREE_ROOT | XFS_AGI_FREE_LEVEL;
		agi->agi_free_root = cpu_to_be32(afake->af_root);
		agi->agi_free_level = cpu_to_be32(afake->af_levels);
		if (xfs_has_inobtcounts(cur->bc_mp)) {
			agi->agi_fblocks = cpu_to_be32(afake->af_blocks);
			fields |= XFS_AGI_IBLOCKS;
		}
		xfs_ialloc_log_agi(tp, agbp, fields);
		xfs_btree_commit_afakeroot(cur, tp, agbp, &xfs_finobt_ops);
	}
}

/* Calculate number of records in an inode btree block. */
static inline unsigned int
xfs_inobt_block_maxrecs(
	unsigned int		blocklen,
	bool			leaf)
{
	if (leaf)
		return blocklen / sizeof(xfs_inobt_rec_t);
	return blocklen / (sizeof(xfs_inobt_key_t) + sizeof(xfs_inobt_ptr_t));
}

/*
 * Calculate number of records in an inobt btree block.
 */
int
xfs_inobt_maxrecs(
	struct xfs_mount	*mp,
	int			blocklen,
	int			leaf)
{
	blocklen -= XFS_INOBT_BLOCK_LEN(mp);
	return xfs_inobt_block_maxrecs(blocklen, leaf);
}

/*
 * Maximum number of inode btree records per AG.  Pretend that we can fill an
 * entire AG completely full of inodes except for the AG headers.
 */
#define XFS_MAX_INODE_RECORDS \
	((XFS_MAX_AG_BYTES - (4 * BBSIZE)) / XFS_DINODE_MIN_SIZE) / \
			XFS_INODES_PER_CHUNK

/* Compute the max possible height for the inode btree. */
static inline unsigned int
xfs_inobt_maxlevels_ondisk(void)
{
	unsigned int		minrecs[2];
	unsigned int		blocklen;

	blocklen = min(XFS_MIN_BLOCKSIZE - XFS_BTREE_SBLOCK_LEN,
		       XFS_MIN_CRC_BLOCKSIZE - XFS_BTREE_SBLOCK_CRC_LEN);

	minrecs[0] = xfs_inobt_block_maxrecs(blocklen, true) / 2;
	minrecs[1] = xfs_inobt_block_maxrecs(blocklen, false) / 2;

	return xfs_btree_compute_maxlevels(minrecs, XFS_MAX_INODE_RECORDS);
}

/* Compute the max possible height for the free inode btree. */
static inline unsigned int
xfs_finobt_maxlevels_ondisk(void)
{
	unsigned int		minrecs[2];
	unsigned int		blocklen;

	blocklen = XFS_MIN_CRC_BLOCKSIZE - XFS_BTREE_SBLOCK_CRC_LEN;

	minrecs[0] = xfs_inobt_block_maxrecs(blocklen, true) / 2;
	minrecs[1] = xfs_inobt_block_maxrecs(blocklen, false) / 2;

	return xfs_btree_compute_maxlevels(minrecs, XFS_MAX_INODE_RECORDS);
}

/* Compute the max possible height for either inode btree. */
unsigned int
xfs_iallocbt_maxlevels_ondisk(void)
{
	return max(xfs_inobt_maxlevels_ondisk(),
		   xfs_finobt_maxlevels_ondisk());
}

/*
 * Convert the inode record holemask to an inode allocation bitmap. The inode
 * allocation bitmap is inode granularity and specifies whether an inode is
 * physically allocated on disk (not whether the inode is considered allocated
 * or free by the fs).
 *
 * A bit value of 1 means the inode is allocated, a value of 0 means it is free.
 */
uint64_t
xfs_inobt_irec_to_allocmask(
	struct xfs_inobt_rec_incore	*rec)
{
	uint64_t			bitmap = 0;
	uint64_t			inodespbit;
	int				nextbit;
	uint				allocbitmap;

	/*
	 * The holemask has 16-bits for a 64 inode record. Therefore each
	 * holemask bit represents multiple inodes. Create a mask of bits to set
	 * in the allocmask for each holemask bit.
	 */
	inodespbit = (1 << XFS_INODES_PER_HOLEMASK_BIT) - 1;

	/*
	 * Allocated inodes are represented by 0 bits in holemask. Invert the 0
	 * bits to 1 and convert to a uint so we can use xfs_next_bit(). Mask
	 * anything beyond the 16 holemask bits since this casts to a larger
	 * type.
	 */
	allocbitmap = ~rec->ir_holemask & ((1 << XFS_INOBT_HOLEMASK_BITS) - 1);

	/*
	 * allocbitmap is the inverted holemask so every set bit represents
	 * allocated inodes. To expand from 16-bit holemask granularity to
	 * 64-bit (e.g., bit-per-inode), set inodespbit bits in the target
	 * bitmap for every holemask bit.
	 */
	nextbit = xfs_next_bit(&allocbitmap, 1, 0);
	while (nextbit != -1) {
		ASSERT(nextbit < (sizeof(rec->ir_holemask) * NBBY));

		bitmap |= (inodespbit <<
			   (nextbit * XFS_INODES_PER_HOLEMASK_BIT));

		nextbit = xfs_next_bit(&allocbitmap, 1, nextbit + 1);
	}

	return bitmap;
}

#if defined(DEBUG) || defined(XFS_WARN)
/*
 * Verify that an in-core inode record has a valid inode count.
 */
int
xfs_inobt_rec_check_count(
	struct xfs_mount		*mp,
	struct xfs_inobt_rec_incore	*rec)
{
	int				inocount = 0;
	int				nextbit = 0;
	uint64_t			allocbmap;
	int				wordsz;

	wordsz = sizeof(allocbmap) / sizeof(unsigned int);
	allocbmap = xfs_inobt_irec_to_allocmask(rec);

	nextbit = xfs_next_bit((uint *) &allocbmap, wordsz, nextbit);
	while (nextbit != -1) {
		inocount++;
		nextbit = xfs_next_bit((uint *) &allocbmap, wordsz,
				       nextbit + 1);
	}

	if (inocount != rec->ir_count)
		return -EFSCORRUPTED;

	return 0;
}
#endif	/* DEBUG */

static xfs_extlen_t
xfs_inobt_max_size(
	struct xfs_mount	*mp,
	xfs_agnumber_t		agno)
{
	xfs_agblock_t		agblocks = xfs_ag_block_count(mp, agno);

	/* Bail out if we're uninitialized, which can happen in mkfs. */
	if (M_IGEO(mp)->inobt_mxr[0] == 0)
		return 0;

	/*
	 * The log is permanently allocated, so the space it occupies will
	 * never be available for the kinds of things that would require btree
	 * expansion.  We therefore can pretend the space isn't there.
	 */
	if (mp->m_sb.sb_logstart &&
	    XFS_FSB_TO_AGNO(mp, mp->m_sb.sb_logstart) == agno)
		agblocks -= mp->m_sb.sb_logblocks;

	return xfs_btree_calc_size(M_IGEO(mp)->inobt_mnr,
				(uint64_t)agblocks * mp->m_sb.sb_inopblock /
					XFS_INODES_PER_CHUNK);
}

/* Read AGI and create inobt cursor. */
int
xfs_inobt_cur(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	struct xfs_perag	*pag,
	xfs_btnum_t		which,
	struct xfs_btree_cur	**curpp,
	struct xfs_buf		**agi_bpp)
{
	struct xfs_btree_cur	*cur;
	int			error;

	ASSERT(*agi_bpp == NULL);
	ASSERT(*curpp == NULL);

	error = xfs_ialloc_read_agi(mp, tp, pag->pag_agno, agi_bpp);
	if (error)
		return error;

	cur = xfs_inobt_init_cursor(mp, tp, *agi_bpp, pag, which);
	*curpp = cur;
	return 0;
}

static int
xfs_inobt_count_blocks(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	struct xfs_perag	*pag,
	xfs_btnum_t		btnum,
	xfs_extlen_t		*tree_blocks)
{
	struct xfs_buf		*agbp = NULL;
	struct xfs_btree_cur	*cur = NULL;
	int			error;

	error = xfs_inobt_cur(mp, tp, pag, btnum, &cur, &agbp);
	if (error)
		return error;

	error = xfs_btree_count_blocks(cur, tree_blocks);
	xfs_btree_del_cursor(cur, error);
	xfs_trans_brelse(tp, agbp);

	return error;
}

/* Read finobt block count from AGI header. */
static int
xfs_finobt_read_blocks(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	struct xfs_perag	*pag,
	xfs_extlen_t		*tree_blocks)
{
	struct xfs_buf		*agbp;
	struct xfs_agi		*agi;
	int			error;

	error = xfs_ialloc_read_agi(mp, tp, pag->pag_agno, &agbp);
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
xfs_finobt_calc_reserves(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	struct xfs_perag	*pag,
	xfs_extlen_t		*ask,
	xfs_extlen_t		*used)
{
	xfs_extlen_t		tree_len = 0;
	int			error;

	if (!xfs_has_finobt(mp))
		return 0;

	if (xfs_has_inobtcounts(mp))
		error = xfs_finobt_read_blocks(mp, tp, pag, &tree_len);
	else
		error = xfs_inobt_count_blocks(mp, tp, pag, XFS_BTNUM_FINO,
				&tree_len);
	if (error)
		return error;

	*ask += xfs_inobt_max_size(mp, pag->pag_agno);
	*used += tree_len;
	return 0;
}

/* Calculate the inobt btree size for some records. */
xfs_extlen_t
xfs_iallocbt_calc_size(
	struct xfs_mount	*mp,
	unsigned long long	len)
{
	return xfs_btree_calc_size(M_IGEO(mp)->inobt_mnr, len);
}

int __init
xfs_inobt_init_cur_cache(void)
{
	xfs_inobt_cur_cache = kmem_cache_create("xfs_inobt_cur",
			xfs_btree_cur_sizeof(xfs_inobt_maxlevels_ondisk()),
			0, 0, NULL);

	if (!xfs_inobt_cur_cache)
		return -ENOMEM;
	return 0;
}

void
xfs_inobt_destroy_cur_cache(void)
{
	kmem_cache_destroy(xfs_inobt_cur_cache);
	xfs_inobt_cur_cache = NULL;
}
