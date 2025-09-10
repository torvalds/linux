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
#include "xfs_metafile.h"
#include "xfs_rmap.h"
#include "xfs_rtrmap_btree.h"
#include "xfs_trace.h"
#include "xfs_cksum.h"
#include "xfs_error.h"
#include "xfs_extent_busy.h"
#include "xfs_rtgroup.h"
#include "xfs_bmap.h"
#include "xfs_health.h"
#include "xfs_buf_mem.h"
#include "xfs_btree_mem.h"

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

STATIC int
xfs_rtrmapbt_get_minrecs(
	struct xfs_btree_cur	*cur,
	int			level)
{
	if (level == cur->bc_nlevels - 1) {
		struct xfs_ifork	*ifp = xfs_btree_ifork_ptr(cur);

		return xfs_rtrmapbt_maxrecs(cur->bc_mp, ifp->if_broot_bytes,
				level == 0) / 2;
	}

	return cur->bc_mp->m_rtrmap_mnr[level != 0];
}

STATIC int
xfs_rtrmapbt_get_maxrecs(
	struct xfs_btree_cur	*cur,
	int			level)
{
	if (level == cur->bc_nlevels - 1) {
		struct xfs_ifork	*ifp = xfs_btree_ifork_ptr(cur);

		return xfs_rtrmapbt_maxrecs(cur->bc_mp, ifp->if_broot_bytes,
				level == 0);
	}

	return cur->bc_mp->m_rtrmap_mxr[level != 0];
}

/* Calculate number of records in the ondisk realtime rmap btree inode root. */
unsigned int
xfs_rtrmapbt_droot_maxrecs(
	unsigned int		blocklen,
	bool			leaf)
{
	blocklen -= sizeof(struct xfs_rtrmap_root);

	if (leaf)
		return blocklen / sizeof(struct xfs_rmap_rec);
	return blocklen / (2 * sizeof(struct xfs_rmap_key) +
			sizeof(xfs_rtrmap_ptr_t));
}

/*
 * Get the maximum records we could store in the on-disk format.
 *
 * For non-root nodes this is equivalent to xfs_rtrmapbt_get_maxrecs, but
 * for the root node this checks the available space in the dinode fork
 * so that we can resize the in-memory buffer to match it.  After a
 * resize to the maximum size this function returns the same value
 * as xfs_rtrmapbt_get_maxrecs for the root node, too.
 */
STATIC int
xfs_rtrmapbt_get_dmaxrecs(
	struct xfs_btree_cur	*cur,
	int			level)
{
	if (level != cur->bc_nlevels - 1)
		return cur->bc_mp->m_rtrmap_mxr[level != 0];
	return xfs_rtrmapbt_droot_maxrecs(cur->bc_ino.forksize, level == 0);
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
xfs_rtrmapbt_init_key_from_rec(
	union xfs_btree_key		*key,
	const union xfs_btree_rec	*rec)
{
	key->rmap.rm_startblock = rec->rmap.rm_startblock;
	key->rmap.rm_owner = rec->rmap.rm_owner;
	key->rmap.rm_offset = ondisk_rec_offset_to_key(rec);
}

STATIC void
xfs_rtrmapbt_init_high_key_from_rec(
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
xfs_rtrmapbt_init_rec_from_cur(
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
xfs_rtrmapbt_init_ptr_from_cur(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr)
{
	ptr->l = 0;
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
xfs_rtrmapbt_cmp_key_with_cur(
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
xfs_rtrmapbt_cmp_two_keys(
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

STATIC int
xfs_rtrmapbt_keys_inorder(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_key	*k1,
	const union xfs_btree_key	*k2)
{
	uint32_t			x;
	uint32_t			y;
	uint64_t			a;
	uint64_t			b;

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
xfs_rtrmapbt_recs_inorder(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_rec	*r1,
	const union xfs_btree_rec	*r2)
{
	uint32_t			x;
	uint32_t			y;
	uint64_t			a;
	uint64_t			b;

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
xfs_rtrmapbt_keys_contiguous(
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

static inline void
xfs_rtrmapbt_move_ptrs(
	struct xfs_mount	*mp,
	struct xfs_btree_block	*broot,
	short			old_size,
	size_t			new_size,
	unsigned int		numrecs)
{
	void			*dptr;
	void			*sptr;

	sptr = xfs_rtrmap_broot_ptr_addr(mp, broot, 1, old_size);
	dptr = xfs_rtrmap_broot_ptr_addr(mp, broot, 1, new_size);
	memmove(dptr, sptr, numrecs * sizeof(xfs_rtrmap_ptr_t));
}

static struct xfs_btree_block *
xfs_rtrmapbt_broot_realloc(
	struct xfs_btree_cur	*cur,
	unsigned int		new_numrecs)
{
	struct xfs_mount	*mp = cur->bc_mp;
	struct xfs_ifork	*ifp = xfs_btree_ifork_ptr(cur);
	struct xfs_btree_block	*broot;
	unsigned int		new_size;
	unsigned int		old_size = ifp->if_broot_bytes;
	const unsigned int	level = cur->bc_nlevels - 1;

	new_size = xfs_rtrmap_broot_space_calc(mp, level, new_numrecs);

	/* Handle the nop case quietly. */
	if (new_size == old_size)
		return ifp->if_broot;

	if (new_size > old_size) {
		unsigned int	old_numrecs;

		/*
		 * If there wasn't any memory allocated before, just allocate
		 * it now and get out.
		 */
		if (old_size == 0)
			return xfs_broot_realloc(ifp, new_size);

		/*
		 * If there is already an existing if_broot, then we need to
		 * realloc it and possibly move the node block pointers because
		 * those are not butted up against the btree block header.
		 */
		old_numrecs = xfs_rtrmapbt_maxrecs(mp, old_size, level == 0);
		broot = xfs_broot_realloc(ifp, new_size);
		if (level > 0)
			xfs_rtrmapbt_move_ptrs(mp, broot, old_size, new_size,
					old_numrecs);
		goto out_broot;
	}

	/*
	 * We're reducing numrecs.  If we're going all the way to zero, just
	 * free the block.
	 */
	ASSERT(ifp->if_broot != NULL && old_size > 0);
	if (new_size == 0)
		return xfs_broot_realloc(ifp, 0);

	/*
	 * Shrink the btree root by possibly moving the rtrmapbt pointers,
	 * since they are not butted up against the btree block header.  Then
	 * reallocate broot.
	 */
	if (level > 0)
		xfs_rtrmapbt_move_ptrs(mp, ifp->if_broot, old_size, new_size,
				new_numrecs);
	broot = xfs_broot_realloc(ifp, new_size);

out_broot:
	ASSERT(xfs_rtrmap_droot_space(broot) <=
	       xfs_inode_fork_size(cur->bc_ino.ip, cur->bc_ino.whichfork));
	return broot;
}

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
	.sick_mask		= XFS_SICK_RG_RMAPBT,

	.dup_cursor		= xfs_rtrmapbt_dup_cursor,
	.alloc_block		= xfs_btree_alloc_metafile_block,
	.free_block		= xfs_btree_free_metafile_block,
	.get_minrecs		= xfs_rtrmapbt_get_minrecs,
	.get_maxrecs		= xfs_rtrmapbt_get_maxrecs,
	.get_dmaxrecs		= xfs_rtrmapbt_get_dmaxrecs,
	.init_key_from_rec	= xfs_rtrmapbt_init_key_from_rec,
	.init_high_key_from_rec	= xfs_rtrmapbt_init_high_key_from_rec,
	.init_rec_from_cur	= xfs_rtrmapbt_init_rec_from_cur,
	.init_ptr_from_cur	= xfs_rtrmapbt_init_ptr_from_cur,
	.cmp_key_with_cur	= xfs_rtrmapbt_cmp_key_with_cur,
	.buf_ops		= &xfs_rtrmapbt_buf_ops,
	.cmp_two_keys		= xfs_rtrmapbt_cmp_two_keys,
	.keys_inorder		= xfs_rtrmapbt_keys_inorder,
	.recs_inorder		= xfs_rtrmapbt_recs_inorder,
	.keys_contiguous	= xfs_rtrmapbt_keys_contiguous,
	.broot_realloc		= xfs_rtrmapbt_broot_realloc,
};

/* Allocate a new rt rmap btree cursor. */
struct xfs_btree_cur *
xfs_rtrmapbt_init_cursor(
	struct xfs_trans	*tp,
	struct xfs_rtgroup	*rtg)
{
	struct xfs_inode	*ip = rtg_rmap(rtg);
	struct xfs_mount	*mp = rtg_mount(rtg);
	struct xfs_btree_cur	*cur;

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

#ifdef CONFIG_XFS_BTREE_IN_MEM
/*
 * Validate an in-memory realtime rmap btree block.  Callers are allowed to
 * generate an in-memory btree even if the ondisk feature is not enabled.
 */
static xfs_failaddr_t
xfs_rtrmapbt_mem_verify(
	struct xfs_buf		*bp)
{
	struct xfs_mount	*mp = bp->b_mount;
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
	if (xfs_has_rmapbt(mp)) {
		if (level >= mp->m_rtrmap_maxlevels)
			return __this_address;
	} else {
		if (level >= xfs_rtrmapbt_maxlevels_ondisk())
			return __this_address;
	}

	maxrecs = xfs_rtrmapbt_maxrecs(mp, XFBNO_BLOCKSIZE, level == 0);
	return xfs_btree_memblock_verify(bp, maxrecs);
}

static void
xfs_rtrmapbt_mem_rw_verify(
	struct xfs_buf	*bp)
{
	xfs_failaddr_t	fa = xfs_rtrmapbt_mem_verify(bp);

	if (fa)
		xfs_verifier_error(bp, -EFSCORRUPTED, fa);
}

/* skip crc checks on in-memory btrees to save time */
static const struct xfs_buf_ops xfs_rtrmapbt_mem_buf_ops = {
	.name			= "xfs_rtrmapbt_mem",
	.magic			= { 0, cpu_to_be32(XFS_RTRMAP_CRC_MAGIC) },
	.verify_read		= xfs_rtrmapbt_mem_rw_verify,
	.verify_write		= xfs_rtrmapbt_mem_rw_verify,
	.verify_struct		= xfs_rtrmapbt_mem_verify,
};

const struct xfs_btree_ops xfs_rtrmapbt_mem_ops = {
	.type			= XFS_BTREE_TYPE_MEM,
	.geom_flags		= XFS_BTGEO_OVERLAPPING,

	.rec_len		= sizeof(struct xfs_rmap_rec),
	/* Overlapping btree; 2 keys per pointer. */
	.key_len		= 2 * sizeof(struct xfs_rmap_key),
	.ptr_len		= XFS_BTREE_LONG_PTR_LEN,

	.lru_refs		= XFS_RMAP_BTREE_REF,
	.statoff		= XFS_STATS_CALC_INDEX(xs_rtrmap_mem_2),

	.dup_cursor		= xfbtree_dup_cursor,
	.set_root		= xfbtree_set_root,
	.alloc_block		= xfbtree_alloc_block,
	.free_block		= xfbtree_free_block,
	.get_minrecs		= xfbtree_get_minrecs,
	.get_maxrecs		= xfbtree_get_maxrecs,
	.init_key_from_rec	= xfs_rtrmapbt_init_key_from_rec,
	.init_high_key_from_rec	= xfs_rtrmapbt_init_high_key_from_rec,
	.init_rec_from_cur	= xfs_rtrmapbt_init_rec_from_cur,
	.init_ptr_from_cur	= xfbtree_init_ptr_from_cur,
	.cmp_key_with_cur	= xfs_rtrmapbt_cmp_key_with_cur,
	.buf_ops		= &xfs_rtrmapbt_mem_buf_ops,
	.cmp_two_keys		= xfs_rtrmapbt_cmp_two_keys,
	.keys_inorder		= xfs_rtrmapbt_keys_inorder,
	.recs_inorder		= xfs_rtrmapbt_recs_inorder,
	.keys_contiguous	= xfs_rtrmapbt_keys_contiguous,
};

/* Create a cursor for an in-memory btree. */
struct xfs_btree_cur *
xfs_rtrmapbt_mem_cursor(
	struct xfs_rtgroup	*rtg,
	struct xfs_trans	*tp,
	struct xfbtree		*xfbt)
{
	struct xfs_mount	*mp = rtg_mount(rtg);
	struct xfs_btree_cur	*cur;

	cur = xfs_btree_alloc_cursor(mp, tp, &xfs_rtrmapbt_mem_ops,
			mp->m_rtrmap_maxlevels, xfs_rtrmapbt_cur_cache);
	cur->bc_mem.xfbtree = xfbt;
	cur->bc_nlevels = xfbt->nlevels;
	cur->bc_group = xfs_group_hold(rtg_group(rtg));
	return cur;
}

/* Create an in-memory realtime rmap btree. */
int
xfs_rtrmapbt_mem_init(
	struct xfs_mount	*mp,
	struct xfbtree		*xfbt,
	struct xfs_buftarg	*btp,
	xfs_rgnumber_t		rgno)
{
	xfbt->owner = rgno;
	return xfbtree_init(mp, xfbt, btp, &xfs_rtrmapbt_mem_ops);
}
#endif /* CONFIG_XFS_BTREE_IN_MEM */

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
	ASSERT(ifake->if_fork->if_format == XFS_DINODE_FMT_META_BTREE);

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
	unsigned long long	max_dblocks;
	unsigned int		minrecs[2];
	unsigned int		blocklen;

	blocklen = XFS_MIN_CRC_BLOCKSIZE - XFS_BTREE_LBLOCK_CRC_LEN;

	minrecs[0] = xfs_rtrmapbt_block_maxrecs(blocklen, true) / 2;
	minrecs[1] = xfs_rtrmapbt_block_maxrecs(blocklen, false) / 2;

	/*
	 * Compute the asymptotic maxlevels for an rtrmapbt on any rtreflink fs.
	 *
	 * On a reflink filesystem, each block in an rtgroup can have up to
	 * 2^32 (per the refcount record format) owners, which means that
	 * theoretically we could face up to 2^64 rmap records.  However, we're
	 * likely to run out of blocks in the data device long before that
	 * happens, which means that we must compute the max height based on
	 * what the btree will look like if it consumes almost all the blocks
	 * in the data device due to maximal sharing factor.
	 */
	max_dblocks = -1U; /* max ag count */
	max_dblocks *= XFS_MAX_CRC_AG_BLOCKS;
	return xfs_btree_space_to_height(minrecs, max_dblocks);
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
	 *
	 * On a reflink filesystem, each rt block can have up to 2^32 (per the
	 * refcount record format) owners, which means that theoretically we
	 * could face up to 2^64 rmap records.  This makes the computation of
	 * maxlevels based on record count meaningless, so we only consider the
	 * size of the data device.
	 */
	d_maxlevels = xfs_btree_space_to_height(mp->m_rtrmap_mnr,
				mp->m_sb.sb_dblocks);
	if (xfs_has_rtreflink(mp)) {
		mp->m_rtrmap_maxlevels = d_maxlevels + 1;
		return;
	}

	r_maxlevels = xfs_btree_compute_maxlevels(mp->m_rtrmap_mnr,
				mp->m_groups[XG_TYPE_RTG].blocks);

	/* Add one level to handle the inode root level. */
	mp->m_rtrmap_maxlevels = min(d_maxlevels, r_maxlevels) + 1;
}

/* Calculate the rtrmap btree size for some records. */
unsigned long long
xfs_rtrmapbt_calc_size(
	struct xfs_mount	*mp,
	unsigned long long	len)
{
	return xfs_btree_calc_size(mp->m_rtrmap_mnr, len);
}

/*
 * Calculate the maximum rmap btree size.
 */
static unsigned long long
xfs_rtrmapbt_max_size(
	struct xfs_mount	*mp,
	xfs_rtblock_t		rtblocks)
{
	/* Bail out if we're uninitialized, which can happen in mkfs. */
	if (mp->m_rtrmap_mxr[0] == 0)
		return 0;

	return xfs_rtrmapbt_calc_size(mp, rtblocks);
}

/*
 * Figure out how many blocks to reserve and how many are used by this btree.
 */
xfs_filblks_t
xfs_rtrmapbt_calc_reserves(
	struct xfs_mount	*mp)
{
	uint32_t		blocks = mp->m_groups[XG_TYPE_RTG].blocks;

	if (!xfs_has_rtrmapbt(mp))
		return 0;

	/* Reserve 1% of the rtgroup or enough for 1 block per record. */
	return max_t(xfs_filblks_t, blocks / 100,
			xfs_rtrmapbt_max_size(mp, blocks));
}

/* Convert on-disk form of btree root to in-memory form. */
STATIC void
xfs_rtrmapbt_from_disk(
	struct xfs_inode	*ip,
	struct xfs_rtrmap_root	*dblock,
	unsigned int		dblocklen,
	struct xfs_btree_block	*rblock)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_rmap_key	*fkp;
	__be64			*fpp;
	struct xfs_rmap_key	*tkp;
	__be64			*tpp;
	struct xfs_rmap_rec	*frp;
	struct xfs_rmap_rec	*trp;
	unsigned int		rblocklen = xfs_rtrmap_broot_space(mp, dblock);
	unsigned int		numrecs;
	unsigned int		maxrecs;

	xfs_btree_init_block(mp, rblock, &xfs_rtrmapbt_ops, 0, 0, ip->i_ino);

	rblock->bb_level = dblock->bb_level;
	rblock->bb_numrecs = dblock->bb_numrecs;
	numrecs = be16_to_cpu(dblock->bb_numrecs);

	if (be16_to_cpu(rblock->bb_level) > 0) {
		maxrecs = xfs_rtrmapbt_droot_maxrecs(dblocklen, false);
		fkp = xfs_rtrmap_droot_key_addr(dblock, 1);
		tkp = xfs_rtrmap_key_addr(rblock, 1);
		fpp = xfs_rtrmap_droot_ptr_addr(dblock, 1, maxrecs);
		tpp = xfs_rtrmap_broot_ptr_addr(mp, rblock, 1, rblocklen);
		memcpy(tkp, fkp, 2 * sizeof(*fkp) * numrecs);
		memcpy(tpp, fpp, sizeof(*fpp) * numrecs);
	} else {
		frp = xfs_rtrmap_droot_rec_addr(dblock, 1);
		trp = xfs_rtrmap_rec_addr(rblock, 1);
		memcpy(trp, frp, sizeof(*frp) * numrecs);
	}
}

/* Load a realtime reverse mapping btree root in from disk. */
int
xfs_iformat_rtrmap(
	struct xfs_inode	*ip,
	struct xfs_dinode	*dip)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_rtrmap_root	*dfp = XFS_DFORK_PTR(dip, XFS_DATA_FORK);
	struct xfs_btree_block	*broot;
	unsigned int		numrecs;
	unsigned int		level;
	int			dsize;

	/*
	 * growfs must create the rtrmap inodes before adding a realtime volume
	 * to the filesystem, so we cannot use the rtrmapbt predicate here.
	 */
	if (!xfs_has_rmapbt(ip->i_mount)) {
		xfs_inode_mark_sick(ip, XFS_SICK_INO_CORE);
		return -EFSCORRUPTED;
	}

	dsize = XFS_DFORK_SIZE(dip, mp, XFS_DATA_FORK);
	numrecs = be16_to_cpu(dfp->bb_numrecs);
	level = be16_to_cpu(dfp->bb_level);

	if (level > mp->m_rtrmap_maxlevels ||
	    xfs_rtrmap_droot_space_calc(level, numrecs) > dsize) {
		xfs_inode_mark_sick(ip, XFS_SICK_INO_CORE);
		return -EFSCORRUPTED;
	}

	broot = xfs_broot_alloc(xfs_ifork_ptr(ip, XFS_DATA_FORK),
			xfs_rtrmap_broot_space_calc(mp, level, numrecs));
	if (broot)
		xfs_rtrmapbt_from_disk(ip, dfp, dsize, broot);
	return 0;
}

/* Convert in-memory form of btree root to on-disk form. */
void
xfs_rtrmapbt_to_disk(
	struct xfs_mount	*mp,
	struct xfs_btree_block	*rblock,
	unsigned int		rblocklen,
	struct xfs_rtrmap_root	*dblock,
	unsigned int		dblocklen)
{
	struct xfs_rmap_key	*fkp;
	__be64			*fpp;
	struct xfs_rmap_key	*tkp;
	__be64			*tpp;
	struct xfs_rmap_rec	*frp;
	struct xfs_rmap_rec	*trp;
	unsigned int		numrecs;
	unsigned int		maxrecs;

	ASSERT(rblock->bb_magic == cpu_to_be32(XFS_RTRMAP_CRC_MAGIC));
	ASSERT(uuid_equal(&rblock->bb_u.l.bb_uuid, &mp->m_sb.sb_meta_uuid));
	ASSERT(rblock->bb_u.l.bb_blkno == cpu_to_be64(XFS_BUF_DADDR_NULL));
	ASSERT(rblock->bb_u.l.bb_leftsib == cpu_to_be64(NULLFSBLOCK));
	ASSERT(rblock->bb_u.l.bb_rightsib == cpu_to_be64(NULLFSBLOCK));

	dblock->bb_level = rblock->bb_level;
	dblock->bb_numrecs = rblock->bb_numrecs;
	numrecs = be16_to_cpu(rblock->bb_numrecs);

	if (be16_to_cpu(rblock->bb_level) > 0) {
		maxrecs = xfs_rtrmapbt_droot_maxrecs(dblocklen, false);
		fkp = xfs_rtrmap_key_addr(rblock, 1);
		tkp = xfs_rtrmap_droot_key_addr(dblock, 1);
		fpp = xfs_rtrmap_broot_ptr_addr(mp, rblock, 1, rblocklen);
		tpp = xfs_rtrmap_droot_ptr_addr(dblock, 1, maxrecs);
		memcpy(tkp, fkp, 2 * sizeof(*fkp) * numrecs);
		memcpy(tpp, fpp, sizeof(*fpp) * numrecs);
	} else {
		frp = xfs_rtrmap_rec_addr(rblock, 1);
		trp = xfs_rtrmap_droot_rec_addr(dblock, 1);
		memcpy(trp, frp, sizeof(*frp) * numrecs);
	}
}

/* Flush a realtime reverse mapping btree root out to disk. */
void
xfs_iflush_rtrmap(
	struct xfs_inode	*ip,
	struct xfs_dinode	*dip)
{
	struct xfs_ifork	*ifp = xfs_ifork_ptr(ip, XFS_DATA_FORK);
	struct xfs_rtrmap_root	*dfp = XFS_DFORK_PTR(dip, XFS_DATA_FORK);

	ASSERT(ifp->if_broot != NULL);
	ASSERT(ifp->if_broot_bytes > 0);
	ASSERT(xfs_rtrmap_droot_space(ifp->if_broot) <=
			xfs_inode_fork_size(ip, XFS_DATA_FORK));
	xfs_rtrmapbt_to_disk(ip->i_mount, ifp->if_broot, ifp->if_broot_bytes,
			dfp, XFS_DFORK_SIZE(dip, ip->i_mount, XFS_DATA_FORK));
}

/*
 * Create a realtime rmap btree inode.
 */
int
xfs_rtrmapbt_create(
	struct xfs_rtgroup	*rtg,
	struct xfs_inode	*ip,
	struct xfs_trans	*tp,
	bool			init)
{
	struct xfs_ifork	*ifp = xfs_ifork_ptr(ip, XFS_DATA_FORK);
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_btree_block	*broot;

	ifp->if_format = XFS_DINODE_FMT_META_BTREE;
	ASSERT(ifp->if_broot_bytes == 0);
	ASSERT(ifp->if_bytes == 0);

	/* Initialize the empty incore btree root. */
	broot = xfs_broot_realloc(ifp, xfs_rtrmap_broot_space_calc(mp, 0, 0));
	if (broot)
		xfs_btree_init_block(mp, broot, &xfs_rtrmapbt_ops, 0, 0,
				ip->i_ino);
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE | XFS_ILOG_DBROOT);

	return 0;
}

/*
 * Initialize an rmap for a realtime superblock using the potentially updated
 * rt geometry in the provided @mp.
 */
int
xfs_rtrmapbt_init_rtsb(
	struct xfs_mount	*mp,
	struct xfs_rtgroup	*rtg,
	struct xfs_trans	*tp)
{
	struct xfs_rmap_irec	rmap = {
		.rm_blockcount	= mp->m_sb.sb_rextsize,
		.rm_owner	= XFS_RMAP_OWN_FS,
	};
	struct xfs_btree_cur	*cur;
	int			error;

	ASSERT(xfs_has_rtsb(mp));
	ASSERT(rtg_rgno(rtg) == 0);

	cur = xfs_rtrmapbt_init_cursor(tp, rtg);
	error = xfs_rmap_map_raw(cur, &rmap);
	xfs_btree_del_cursor(cur, error);
	return error;
}

/*
 * Return the highest rgbno currently tracked by the rmap for this rtg.
 */
xfs_rgblock_t
xfs_rtrmap_highest_rgbno(
	struct xfs_rtgroup	*rtg)
{
	struct xfs_btree_block	*block = rtg_rmap(rtg)->i_df.if_broot;
	union xfs_btree_key	key = {};
	struct xfs_btree_cur	*cur;

	if (block->bb_numrecs == 0)
		return NULLRGBLOCK;
	cur = xfs_rtrmapbt_init_cursor(NULL, rtg);
	xfs_btree_get_keys(cur, block, &key);
	xfs_btree_del_cursor(cur, XFS_BTREE_NOERROR);
	return be32_to_cpu(key.__rmap_bigkey[1].rm_startblock);
}
