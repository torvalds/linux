// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2014 Red Hat, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_trans.h"
#include "xfs_alloc.h"
#include "xfs_btree.h"
#include "xfs_btree_staging.h"
#include "xfs_rmap.h"
#include "xfs_rmap_btree.h"
#include "xfs_health.h"
#include "xfs_trace.h"
#include "xfs_error.h"
#include "xfs_extent_busy.h"
#include "xfs_ag.h"
#include "xfs_ag_resv.h"
#include "xfs_buf_mem.h"
#include "xfs_btree_mem.h"

static struct kmem_cache	*xfs_rmapbt_cur_cache;

/*
 * Reverse map btree.
 *
 * This is a per-ag tree used to track the owner(s) of a given extent. With
 * reflink it is possible for there to be multiple owners, which is a departure
 * from classic XFS. Owner records for data extents are inserted when the
 * extent is mapped and removed when an extent is unmapped.  Owner records for
 * all other block types (i.e. metadata) are inserted when an extent is
 * allocated and removed when an extent is freed. There can only be one owner
 * of a metadata extent, usually an inode or some other metadata structure like
 * an AG btree.
 *
 * The rmap btree is part of the free space management, so blocks for the tree
 * are sourced from the agfl. Hence we need transaction reservation support for
 * this tree so that the freelist is always large enough. This also impacts on
 * the minimum space we need to leave free in the AG.
 *
 * The tree is ordered by [ag block, owner, offset]. This is a large key size,
 * but it is the only way to enforce unique keys when a block can be owned by
 * multiple files at any offset. There's no need to order/search by extent
 * size for online updating/management of the tree. It is intended that most
 * reverse lookups will be to find the owner(s) of a particular block, or to
 * try to recover tree and file data from corrupt primary metadata.
 */

static struct xfs_btree_cur *
xfs_rmapbt_dup_cursor(
	struct xfs_btree_cur	*cur)
{
	return xfs_rmapbt_init_cursor(cur->bc_mp, cur->bc_tp,
				cur->bc_ag.agbp, to_perag(cur->bc_group));
}

STATIC void
xfs_rmapbt_set_root(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_ptr	*ptr,
	int				inc)
{
	struct xfs_buf			*agbp = cur->bc_ag.agbp;
	struct xfs_agf			*agf = agbp->b_addr;
	struct xfs_perag		*pag = to_perag(cur->bc_group);

	ASSERT(ptr->s != 0);

	agf->agf_rmap_root = ptr->s;
	be32_add_cpu(&agf->agf_rmap_level, inc);
	pag->pagf_rmap_level += inc;

	xfs_alloc_log_agf(cur->bc_tp, agbp, XFS_AGF_ROOTS | XFS_AGF_LEVELS);
}

STATIC int
xfs_rmapbt_alloc_block(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_ptr	*start,
	union xfs_btree_ptr		*new,
	int				*stat)
{
	struct xfs_buf		*agbp = cur->bc_ag.agbp;
	struct xfs_agf		*agf = agbp->b_addr;
	struct xfs_perag	*pag = to_perag(cur->bc_group);
	struct xfs_alloc_arg    args = { .len = 1 };
	int			error;
	xfs_agblock_t		bno;

	/* Allocate the new block from the freelist. If we can't, give up.  */
	error = xfs_alloc_get_freelist(pag, cur->bc_tp, cur->bc_ag.agbp,
				       &bno, 1);
	if (error)
		return error;
	if (bno == NULLAGBLOCK) {
		*stat = 0;
		return 0;
	}

	xfs_extent_busy_reuse(pag_group(pag), bno, 1, false);

	new->s = cpu_to_be32(bno);
	be32_add_cpu(&agf->agf_rmap_blocks, 1);
	xfs_alloc_log_agf(cur->bc_tp, agbp, XFS_AGF_RMAP_BLOCKS);

	/*
	 * Since rmapbt blocks are sourced from the AGFL, they are allocated one
	 * at a time and the reservation updates don't require a transaction.
	 */
	xfs_ag_resv_alloc_extent(pag, XFS_AG_RESV_RMAPBT, &args);

	*stat = 1;
	return 0;
}

STATIC int
xfs_rmapbt_free_block(
	struct xfs_btree_cur	*cur,
	struct xfs_buf		*bp)
{
	struct xfs_buf		*agbp = cur->bc_ag.agbp;
	struct xfs_agf		*agf = agbp->b_addr;
	struct xfs_perag	*pag = to_perag(cur->bc_group);
	xfs_agblock_t		bno;
	int			error;

	bno = xfs_daddr_to_agbno(cur->bc_mp, xfs_buf_daddr(bp));
	be32_add_cpu(&agf->agf_rmap_blocks, -1);
	xfs_alloc_log_agf(cur->bc_tp, agbp, XFS_AGF_RMAP_BLOCKS);
	error = xfs_alloc_put_freelist(pag, cur->bc_tp, agbp, NULL, bno, 1);
	if (error)
		return error;

	xfs_extent_busy_insert(cur->bc_tp, pag_group(pag), bno, 1,
			      XFS_EXTENT_BUSY_SKIP_DISCARD);

	xfs_ag_resv_free_extent(pag, XFS_AG_RESV_RMAPBT, NULL, 1);
	return 0;
}

STATIC int
xfs_rmapbt_get_minrecs(
	struct xfs_btree_cur	*cur,
	int			level)
{
	return cur->bc_mp->m_rmap_mnr[level != 0];
}

STATIC int
xfs_rmapbt_get_maxrecs(
	struct xfs_btree_cur	*cur,
	int			level)
{
	return cur->bc_mp->m_rmap_mxr[level != 0];
}

/*
 * Convert the ondisk record's offset field into the ondisk key's offset field.
 * Fork and bmbt are significant parts of the rmap record key, but written
 * status is merely a record attribute.
 */
static inline __be64 ondisk_rec_offset_to_key(const union xfs_btree_rec *rec)
{
	return rec->rmap.rm_offset & ~cpu_to_be64(XFS_RMAP_OFF_UNWRITTEN);
}

STATIC void
xfs_rmapbt_init_key_from_rec(
	union xfs_btree_key		*key,
	const union xfs_btree_rec	*rec)
{
	key->rmap.rm_startblock = rec->rmap.rm_startblock;
	key->rmap.rm_owner = rec->rmap.rm_owner;
	key->rmap.rm_offset = ondisk_rec_offset_to_key(rec);
}

/*
 * The high key for a reverse mapping record can be computed by shifting
 * the startblock and offset to the highest value that would still map
 * to that record.  In practice this means that we add blockcount-1 to
 * the startblock for all records, and if the record is for a data/attr
 * fork mapping, we add blockcount-1 to the offset too.
 */
STATIC void
xfs_rmapbt_init_high_key_from_rec(
	union xfs_btree_key		*key,
	const union xfs_btree_rec	*rec)
{
	uint64_t			off;
	int				adj;

	adj = be32_to_cpu(rec->rmap.rm_blockcount) - 1;

	key->rmap.rm_startblock = rec->rmap.rm_startblock;
	be32_add_cpu(&key->rmap.rm_startblock, adj);
	key->rmap.rm_owner = rec->rmap.rm_owner;
	key->rmap.rm_offset = ondisk_rec_offset_to_key(rec);
	if (XFS_RMAP_NON_INODE_OWNER(be64_to_cpu(rec->rmap.rm_owner)) ||
	    XFS_RMAP_IS_BMBT_BLOCK(be64_to_cpu(rec->rmap.rm_offset)))
		return;
	off = be64_to_cpu(key->rmap.rm_offset);
	off = (XFS_RMAP_OFF(off) + adj) | (off & ~XFS_RMAP_OFF_MASK);
	key->rmap.rm_offset = cpu_to_be64(off);
}

STATIC void
xfs_rmapbt_init_rec_from_cur(
	struct xfs_btree_cur	*cur,
	union xfs_btree_rec	*rec)
{
	rec->rmap.rm_startblock = cpu_to_be32(cur->bc_rec.r.rm_startblock);
	rec->rmap.rm_blockcount = cpu_to_be32(cur->bc_rec.r.rm_blockcount);
	rec->rmap.rm_owner = cpu_to_be64(cur->bc_rec.r.rm_owner);
	rec->rmap.rm_offset = cpu_to_be64(
			xfs_rmap_irec_offset_pack(&cur->bc_rec.r));
}

STATIC void
xfs_rmapbt_init_ptr_from_cur(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr)
{
	struct xfs_agf		*agf = cur->bc_ag.agbp->b_addr;

	ASSERT(cur->bc_group->xg_gno == be32_to_cpu(agf->agf_seqno));

	ptr->s = agf->agf_rmap_root;
}

/*
 * Mask the appropriate parts of the ondisk key field for a key comparison.
 * Fork and bmbt are significant parts of the rmap record key, but written
 * status is merely a record attribute.
 */
static inline uint64_t offset_keymask(uint64_t offset)
{
	return offset & ~XFS_RMAP_OFF_UNWRITTEN;
}

STATIC int
xfs_rmapbt_cmp_key_with_cur(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_key	*key)
{
	struct xfs_rmap_irec		*rec = &cur->bc_rec.r;
	const struct xfs_rmap_key	*kp = &key->rmap;

	return cmp_int(be32_to_cpu(kp->rm_startblock), rec->rm_startblock) ?:
	       cmp_int(be64_to_cpu(kp->rm_owner), rec->rm_owner) ?:
	       cmp_int(offset_keymask(be64_to_cpu(kp->rm_offset)),
		       offset_keymask(xfs_rmap_irec_offset_pack(rec)));
}

STATIC int
xfs_rmapbt_cmp_two_keys(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_key	*k1,
	const union xfs_btree_key	*k2,
	const union xfs_btree_key	*mask)
{
	const struct xfs_rmap_key	*kp1 = &k1->rmap;
	const struct xfs_rmap_key	*kp2 = &k2->rmap;
	int				d;

	/* Doesn't make sense to mask off the physical space part */
	ASSERT(!mask || mask->rmap.rm_startblock);

	d = cmp_int(be32_to_cpu(kp1->rm_startblock),
		    be32_to_cpu(kp2->rm_startblock));
	if (d)
		return d;

	if (!mask || mask->rmap.rm_owner) {
		d = cmp_int(be64_to_cpu(kp1->rm_owner),
			    be64_to_cpu(kp2->rm_owner));
		if (d)
			return d;
	}

	if (!mask || mask->rmap.rm_offset) {
		/* Doesn't make sense to allow offset but not owner */
		ASSERT(!mask || mask->rmap.rm_owner);

		d = cmp_int(offset_keymask(be64_to_cpu(kp1->rm_offset)),
			    offset_keymask(be64_to_cpu(kp2->rm_offset)));
		if (d)
			return d;
	}

	return 0;
}

static xfs_failaddr_t
xfs_rmapbt_verify(
	struct xfs_buf		*bp)
{
	struct xfs_mount	*mp = bp->b_mount;
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
	if (!xfs_verify_magic(bp, block->bb_magic))
		return __this_address;

	if (!xfs_has_rmapbt(mp))
		return __this_address;
	fa = xfs_btree_agblock_v5hdr_verify(bp);
	if (fa)
		return fa;

	level = be16_to_cpu(block->bb_level);
	if (pag && xfs_perag_initialised_agf(pag)) {
		unsigned int	maxlevel = pag->pagf_rmap_level;

#ifdef CONFIG_XFS_ONLINE_REPAIR
		/*
		 * Online repair could be rewriting the free space btrees, so
		 * we'll validate against the larger of either tree while this
		 * is going on.
		 */
		maxlevel = max_t(unsigned int, maxlevel,
				pag->pagf_repair_rmap_level);
#endif
		if (level >= maxlevel)
			return __this_address;
	} else if (level >= mp->m_rmap_maxlevels)
		return __this_address;

	return xfs_btree_agblock_verify(bp, mp->m_rmap_mxr[level != 0]);
}

static void
xfs_rmapbt_read_verify(
	struct xfs_buf	*bp)
{
	xfs_failaddr_t	fa;

	if (!xfs_btree_agblock_verify_crc(bp))
		xfs_verifier_error(bp, -EFSBADCRC, __this_address);
	else {
		fa = xfs_rmapbt_verify(bp);
		if (fa)
			xfs_verifier_error(bp, -EFSCORRUPTED, fa);
	}

	if (bp->b_error)
		trace_xfs_btree_corrupt(bp, _RET_IP_);
}

static void
xfs_rmapbt_write_verify(
	struct xfs_buf	*bp)
{
	xfs_failaddr_t	fa;

	fa = xfs_rmapbt_verify(bp);
	if (fa) {
		trace_xfs_btree_corrupt(bp, _RET_IP_);
		xfs_verifier_error(bp, -EFSCORRUPTED, fa);
		return;
	}
	xfs_btree_agblock_calc_crc(bp);

}

const struct xfs_buf_ops xfs_rmapbt_buf_ops = {
	.name			= "xfs_rmapbt",
	.magic			= { 0, cpu_to_be32(XFS_RMAP_CRC_MAGIC) },
	.verify_read		= xfs_rmapbt_read_verify,
	.verify_write		= xfs_rmapbt_write_verify,
	.verify_struct		= xfs_rmapbt_verify,
};

STATIC int
xfs_rmapbt_keys_inorder(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_key	*k1,
	const union xfs_btree_key	*k2)
{
	uint32_t		x;
	uint32_t		y;
	uint64_t		a;
	uint64_t		b;

	x = be32_to_cpu(k1->rmap.rm_startblock);
	y = be32_to_cpu(k2->rmap.rm_startblock);
	if (x < y)
		return 1;
	else if (x > y)
		return 0;
	a = be64_to_cpu(k1->rmap.rm_owner);
	b = be64_to_cpu(k2->rmap.rm_owner);
	if (a < b)
		return 1;
	else if (a > b)
		return 0;
	a = offset_keymask(be64_to_cpu(k1->rmap.rm_offset));
	b = offset_keymask(be64_to_cpu(k2->rmap.rm_offset));
	if (a <= b)
		return 1;
	return 0;
}

STATIC int
xfs_rmapbt_recs_inorder(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_rec	*r1,
	const union xfs_btree_rec	*r2)
{
	uint32_t		x;
	uint32_t		y;
	uint64_t		a;
	uint64_t		b;

	x = be32_to_cpu(r1->rmap.rm_startblock);
	y = be32_to_cpu(r2->rmap.rm_startblock);
	if (x < y)
		return 1;
	else if (x > y)
		return 0;
	a = be64_to_cpu(r1->rmap.rm_owner);
	b = be64_to_cpu(r2->rmap.rm_owner);
	if (a < b)
		return 1;
	else if (a > b)
		return 0;
	a = offset_keymask(be64_to_cpu(r1->rmap.rm_offset));
	b = offset_keymask(be64_to_cpu(r2->rmap.rm_offset));
	if (a <= b)
		return 1;
	return 0;
}

STATIC enum xbtree_key_contig
xfs_rmapbt_keys_contiguous(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_key	*key1,
	const union xfs_btree_key	*key2,
	const union xfs_btree_key	*mask)
{
	ASSERT(!mask || mask->rmap.rm_startblock);

	/*
	 * We only support checking contiguity of the physical space component.
	 * If any callers ever need more specificity than that, they'll have to
	 * implement it here.
	 */
	ASSERT(!mask || (!mask->rmap.rm_owner && !mask->rmap.rm_offset));

	return xbtree_key_contig(be32_to_cpu(key1->rmap.rm_startblock),
				 be32_to_cpu(key2->rmap.rm_startblock));
}

const struct xfs_btree_ops xfs_rmapbt_ops = {
	.name			= "rmap",
	.type			= XFS_BTREE_TYPE_AG,
	.geom_flags		= XFS_BTGEO_OVERLAPPING,

	.rec_len		= sizeof(struct xfs_rmap_rec),
	/* Overlapping btree; 2 keys per pointer. */
	.key_len		= 2 * sizeof(struct xfs_rmap_key),
	.ptr_len		= XFS_BTREE_SHORT_PTR_LEN,

	.lru_refs		= XFS_RMAP_BTREE_REF,
	.statoff		= XFS_STATS_CALC_INDEX(xs_rmap_2),
	.sick_mask		= XFS_SICK_AG_RMAPBT,

	.dup_cursor		= xfs_rmapbt_dup_cursor,
	.set_root		= xfs_rmapbt_set_root,
	.alloc_block		= xfs_rmapbt_alloc_block,
	.free_block		= xfs_rmapbt_free_block,
	.get_minrecs		= xfs_rmapbt_get_minrecs,
	.get_maxrecs		= xfs_rmapbt_get_maxrecs,
	.init_key_from_rec	= xfs_rmapbt_init_key_from_rec,
	.init_high_key_from_rec	= xfs_rmapbt_init_high_key_from_rec,
	.init_rec_from_cur	= xfs_rmapbt_init_rec_from_cur,
	.init_ptr_from_cur	= xfs_rmapbt_init_ptr_from_cur,
	.cmp_key_with_cur	= xfs_rmapbt_cmp_key_with_cur,
	.buf_ops		= &xfs_rmapbt_buf_ops,
	.cmp_two_keys		= xfs_rmapbt_cmp_two_keys,
	.keys_inorder		= xfs_rmapbt_keys_inorder,
	.recs_inorder		= xfs_rmapbt_recs_inorder,
	.keys_contiguous	= xfs_rmapbt_keys_contiguous,
};

/*
 * Create a new reverse mapping btree cursor.
 *
 * For staging cursors tp and agbp are NULL.
 */
struct xfs_btree_cur *
xfs_rmapbt_init_cursor(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	struct xfs_buf		*agbp,
	struct xfs_perag	*pag)
{
	struct xfs_btree_cur	*cur;

	cur = xfs_btree_alloc_cursor(mp, tp, &xfs_rmapbt_ops,
			mp->m_rmap_maxlevels, xfs_rmapbt_cur_cache);
	cur->bc_group = xfs_group_hold(pag_group(pag));
	cur->bc_ag.agbp = agbp;
	if (agbp) {
		struct xfs_agf		*agf = agbp->b_addr;

		cur->bc_nlevels = be32_to_cpu(agf->agf_rmap_level);
	}
	return cur;
}

#ifdef CONFIG_XFS_BTREE_IN_MEM
static inline unsigned int
xfs_rmapbt_mem_block_maxrecs(
	unsigned int		blocklen,
	bool			leaf)
{
	if (leaf)
		return blocklen / sizeof(struct xfs_rmap_rec);
	return blocklen /
		(2 * sizeof(struct xfs_rmap_key) + sizeof(__be64));
}

/*
 * Validate an in-memory rmap btree block.  Callers are allowed to generate an
 * in-memory btree even if the ondisk feature is not enabled.
 */
static xfs_failaddr_t
xfs_rmapbt_mem_verify(
	struct xfs_buf		*bp)
{
	struct xfs_btree_block	*block = XFS_BUF_TO_BLOCK(bp);
	xfs_failaddr_t		fa;
	unsigned int		level;
	unsigned int		maxrecs;

	if (!xfs_verify_magic(bp, block->bb_magic))
		return __this_address;

	fa = xfs_btree_fsblock_v5hdr_verify(bp, XFS_RMAP_OWN_UNKNOWN);
	if (fa)
		return fa;

	level = be16_to_cpu(block->bb_level);
	if (level >= xfs_rmapbt_maxlevels_ondisk())
		return __this_address;

	maxrecs = xfs_rmapbt_mem_block_maxrecs(
			XFBNO_BLOCKSIZE - XFS_BTREE_LBLOCK_CRC_LEN, level == 0);
	return xfs_btree_memblock_verify(bp, maxrecs);
}

static void
xfs_rmapbt_mem_rw_verify(
	struct xfs_buf	*bp)
{
	xfs_failaddr_t	fa = xfs_rmapbt_mem_verify(bp);

	if (fa)
		xfs_verifier_error(bp, -EFSCORRUPTED, fa);
}

/* skip crc checks on in-memory btrees to save time */
static const struct xfs_buf_ops xfs_rmapbt_mem_buf_ops = {
	.name			= "xfs_rmapbt_mem",
	.magic			= { 0, cpu_to_be32(XFS_RMAP_CRC_MAGIC) },
	.verify_read		= xfs_rmapbt_mem_rw_verify,
	.verify_write		= xfs_rmapbt_mem_rw_verify,
	.verify_struct		= xfs_rmapbt_mem_verify,
};

const struct xfs_btree_ops xfs_rmapbt_mem_ops = {
	.name			= "mem_rmap",
	.type			= XFS_BTREE_TYPE_MEM,
	.geom_flags		= XFS_BTGEO_OVERLAPPING,

	.rec_len		= sizeof(struct xfs_rmap_rec),
	/* Overlapping btree; 2 keys per pointer. */
	.key_len		= 2 * sizeof(struct xfs_rmap_key),
	.ptr_len		= XFS_BTREE_LONG_PTR_LEN,

	.lru_refs		= XFS_RMAP_BTREE_REF,
	.statoff		= XFS_STATS_CALC_INDEX(xs_rmap_mem_2),

	.dup_cursor		= xfbtree_dup_cursor,
	.set_root		= xfbtree_set_root,
	.alloc_block		= xfbtree_alloc_block,
	.free_block		= xfbtree_free_block,
	.get_minrecs		= xfbtree_get_minrecs,
	.get_maxrecs		= xfbtree_get_maxrecs,
	.init_key_from_rec	= xfs_rmapbt_init_key_from_rec,
	.init_high_key_from_rec	= xfs_rmapbt_init_high_key_from_rec,
	.init_rec_from_cur	= xfs_rmapbt_init_rec_from_cur,
	.init_ptr_from_cur	= xfbtree_init_ptr_from_cur,
	.cmp_key_with_cur	= xfs_rmapbt_cmp_key_with_cur,
	.buf_ops		= &xfs_rmapbt_mem_buf_ops,
	.cmp_two_keys		= xfs_rmapbt_cmp_two_keys,
	.keys_inorder		= xfs_rmapbt_keys_inorder,
	.recs_inorder		= xfs_rmapbt_recs_inorder,
	.keys_contiguous	= xfs_rmapbt_keys_contiguous,
};

/* Create a cursor for an in-memory btree. */
struct xfs_btree_cur *
xfs_rmapbt_mem_cursor(
	struct xfs_perag	*pag,
	struct xfs_trans	*tp,
	struct xfbtree		*xfbt)
{
	struct xfs_btree_cur	*cur;

	cur = xfs_btree_alloc_cursor(pag_mount(pag), tp, &xfs_rmapbt_mem_ops,
			xfs_rmapbt_maxlevels_ondisk(), xfs_rmapbt_cur_cache);
	cur->bc_mem.xfbtree = xfbt;
	cur->bc_nlevels = xfbt->nlevels;

	cur->bc_group = xfs_group_hold(pag_group(pag));
	return cur;
}

/* Create an in-memory rmap btree. */
int
xfs_rmapbt_mem_init(
	struct xfs_mount	*mp,
	struct xfbtree		*xfbt,
	struct xfs_buftarg	*btp,
	xfs_agnumber_t		agno)
{
	xfbt->owner = agno;
	return xfbtree_init(mp, xfbt, btp, &xfs_rmapbt_mem_ops);
}

/* Compute the max possible height for reverse mapping btrees in memory. */
static unsigned int
xfs_rmapbt_mem_maxlevels(void)
{
	unsigned int		minrecs[2];
	unsigned int		blocklen;

	blocklen = XFBNO_BLOCKSIZE - XFS_BTREE_LBLOCK_CRC_LEN;

	minrecs[0] = xfs_rmapbt_mem_block_maxrecs(blocklen, true) / 2;
	minrecs[1] = xfs_rmapbt_mem_block_maxrecs(blocklen, false) / 2;

	/*
	 * How tall can an in-memory rmap btree become if we filled the entire
	 * AG with rmap records?
	 */
	return xfs_btree_compute_maxlevels(minrecs,
			XFS_MAX_AG_BYTES / sizeof(struct xfs_rmap_rec));
}
#else
# define xfs_rmapbt_mem_maxlevels()	(0)
#endif /* CONFIG_XFS_BTREE_IN_MEM */

/*
 * Install a new reverse mapping btree root.  Caller is responsible for
 * invalidating and freeing the old btree blocks.
 */
void
xfs_rmapbt_commit_staged_btree(
	struct xfs_btree_cur	*cur,
	struct xfs_trans	*tp,
	struct xfs_buf		*agbp)
{
	struct xfs_agf		*agf = agbp->b_addr;
	struct xbtree_afakeroot	*afake = cur->bc_ag.afake;

	ASSERT(cur->bc_flags & XFS_BTREE_STAGING);

	agf->agf_rmap_root = cpu_to_be32(afake->af_root);
	agf->agf_rmap_level = cpu_to_be32(afake->af_levels);
	agf->agf_rmap_blocks = cpu_to_be32(afake->af_blocks);
	xfs_alloc_log_agf(tp, agbp, XFS_AGF_ROOTS | XFS_AGF_LEVELS |
				    XFS_AGF_RMAP_BLOCKS);
	xfs_btree_commit_afakeroot(cur, tp, agbp);
}

/* Calculate number of records in a reverse mapping btree block. */
static inline unsigned int
xfs_rmapbt_block_maxrecs(
	unsigned int		blocklen,
	bool			leaf)
{
	if (leaf)
		return blocklen / sizeof(struct xfs_rmap_rec);
	return blocklen /
		(2 * sizeof(struct xfs_rmap_key) + sizeof(xfs_rmap_ptr_t));
}

/*
 * Calculate number of records in an rmap btree block.
 */
unsigned int
xfs_rmapbt_maxrecs(
	struct xfs_mount	*mp,
	unsigned int		blocklen,
	bool			leaf)
{
	blocklen -= XFS_RMAP_BLOCK_LEN;
	return xfs_rmapbt_block_maxrecs(blocklen, leaf);
}

/* Compute the max possible height for reverse mapping btrees. */
unsigned int
xfs_rmapbt_maxlevels_ondisk(void)
{
	unsigned int		minrecs[2];
	unsigned int		blocklen;

	blocklen = XFS_MIN_CRC_BLOCKSIZE - XFS_BTREE_SBLOCK_CRC_LEN;

	minrecs[0] = xfs_rmapbt_block_maxrecs(blocklen, true) / 2;
	minrecs[1] = xfs_rmapbt_block_maxrecs(blocklen, false) / 2;

	/*
	 * Compute the asymptotic maxlevels for an rmapbt on any reflink fs.
	 *
	 * On a reflink filesystem, each AG block can have up to 2^32 (per the
	 * refcount record format) owners, which means that theoretically we
	 * could face up to 2^64 rmap records.  However, we're likely to run
	 * out of blocks in the AG long before that happens, which means that
	 * we must compute the max height based on what the btree will look
	 * like if it consumes almost all the blocks in the AG due to maximal
	 * sharing factor.
	 */
	return max(xfs_btree_space_to_height(minrecs, XFS_MAX_CRC_AG_BLOCKS),
		   xfs_rmapbt_mem_maxlevels());
}

/* Compute the maximum height of an rmap btree. */
void
xfs_rmapbt_compute_maxlevels(
	struct xfs_mount		*mp)
{
	if (!xfs_has_rmapbt(mp)) {
		mp->m_rmap_maxlevels = 0;
		return;
	}

	if (xfs_has_reflink(mp)) {
		/*
		 * Compute the asymptotic maxlevels for an rmap btree on a
		 * filesystem that supports reflink.
		 *
		 * On a reflink filesystem, each AG block can have up to 2^32
		 * (per the refcount record format) owners, which means that
		 * theoretically we could face up to 2^64 rmap records.
		 * However, we're likely to run out of blocks in the AG long
		 * before that happens, which means that we must compute the
		 * max height based on what the btree will look like if it
		 * consumes almost all the blocks in the AG due to maximal
		 * sharing factor.
		 */
		mp->m_rmap_maxlevels = xfs_btree_space_to_height(mp->m_rmap_mnr,
				mp->m_sb.sb_agblocks);
	} else {
		/*
		 * If there's no block sharing, compute the maximum rmapbt
		 * height assuming one rmap record per AG block.
		 */
		mp->m_rmap_maxlevels = xfs_btree_compute_maxlevels(
				mp->m_rmap_mnr, mp->m_sb.sb_agblocks);
	}
	ASSERT(mp->m_rmap_maxlevels <= xfs_rmapbt_maxlevels_ondisk());
}

/* Calculate the refcount btree size for some records. */
xfs_extlen_t
xfs_rmapbt_calc_size(
	struct xfs_mount	*mp,
	unsigned long long	len)
{
	return xfs_btree_calc_size(mp->m_rmap_mnr, len);
}

/*
 * Calculate the maximum refcount btree size.
 */
xfs_extlen_t
xfs_rmapbt_max_size(
	struct xfs_mount	*mp,
	xfs_agblock_t		agblocks)
{
	/* Bail out if we're uninitialized, which can happen in mkfs. */
	if (mp->m_rmap_mxr[0] == 0)
		return 0;

	return xfs_rmapbt_calc_size(mp, agblocks);
}

/*
 * Figure out how many blocks to reserve and how many are used by this btree.
 */
int
xfs_rmapbt_calc_reserves(
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

	if (!xfs_has_rmapbt(mp))
		return 0;

	error = xfs_alloc_read_agf(pag, tp, 0, &agbp);
	if (error)
		return error;

	agf = agbp->b_addr;
	agblocks = be32_to_cpu(agf->agf_length);
	tree_len = be32_to_cpu(agf->agf_rmap_blocks);
	xfs_trans_brelse(tp, agbp);

	/*
	 * The log is permanently allocated, so the space it occupies will
	 * never be available for the kinds of things that would require btree
	 * expansion.  We therefore can pretend the space isn't there.
	 */
	if (xfs_ag_contains_log(mp, pag_agno(pag)))
		agblocks -= mp->m_sb.sb_logblocks;

	/* Reserve 1% of the AG or enough for 1 block per record. */
	*ask += max(agblocks / 100, xfs_rmapbt_max_size(mp, agblocks));
	*used += tree_len;

	return error;
}

int __init
xfs_rmapbt_init_cur_cache(void)
{
	xfs_rmapbt_cur_cache = kmem_cache_create("xfs_rmapbt_cur",
			xfs_btree_cur_sizeof(xfs_rmapbt_maxlevels_ondisk()),
			0, 0, NULL);

	if (!xfs_rmapbt_cur_cache)
		return -ENOMEM;
	return 0;
}

void
xfs_rmapbt_destroy_cur_cache(void)
{
	kmem_cache_destroy(xfs_rmapbt_cur_cache);
	xfs_rmapbt_cur_cache = NULL;
}
