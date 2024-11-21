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

STATIC int64_t
xfs_rtrmapbt_key_diff(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_key	*key)
{
	struct xfs_rmap_irec		*rec = &cur->bc_rec.r;
	const struct xfs_rmap_key	*kp = &key->rmap;
	__u64				x, y;
	int64_t				d;

	d = (int64_t)be32_to_cpu(kp->rm_startblock) - rec->rm_startblock;
	if (d)
		return d;

	x = be64_to_cpu(kp->rm_owner);
	y = rec->rm_owner;
	if (x > y)
		return 1;
	else if (y > x)
		return -1;

	x = offset_keymask(be64_to_cpu(kp->rm_offset));
	y = offset_keymask(xfs_rmap_irec_offset_pack(rec));
	if (x > y)
		return 1;
	else if (y > x)
		return -1;
	return 0;
}

STATIC int64_t
xfs_rtrmapbt_diff_two_keys(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_key	*k1,
	const union xfs_btree_key	*k2,
	const union xfs_btree_key	*mask)
{
	const struct xfs_rmap_key	*kp1 = &k1->rmap;
	const struct xfs_rmap_key	*kp2 = &k2->rmap;
	int64_t				d;
	__u64				x, y;

	/* Doesn't make sense to mask off the physical space part */
	ASSERT(!mask || mask->rmap.rm_startblock);

	d = (int64_t)be32_to_cpu(kp1->rm_startblock) -
		     be32_to_cpu(kp2->rm_startblock);
	if (d)
		return d;

	if (!mask || mask->rmap.rm_owner) {
		x = be64_to_cpu(kp1->rm_owner);
		y = be64_to_cpu(kp2->rm_owner);
		if (x > y)
			return 1;
		else if (y > x)
			return -1;
	}

	if (!mask || mask->rmap.rm_offset) {
		/* Doesn't make sense to allow offset but not owner */
		ASSERT(!mask || mask->rmap.rm_owner);

		x = offset_keymask(be64_to_cpu(kp1->rm_offset));
		y = offset_keymask(be64_to_cpu(kp2->rm_offset));
		if (x > y)
			return 1;
		else if (y > x)
			return -1;
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

	.dup_cursor		= xfs_rtrmapbt_dup_cursor,
	.alloc_block		= xfs_btree_alloc_metafile_block,
	.free_block		= xfs_btree_free_metafile_block,
	.get_minrecs		= xfs_rtrmapbt_get_minrecs,
	.get_maxrecs		= xfs_rtrmapbt_get_maxrecs,
	.init_key_from_rec	= xfs_rtrmapbt_init_key_from_rec,
	.init_high_key_from_rec	= xfs_rtrmapbt_init_high_key_from_rec,
	.init_rec_from_cur	= xfs_rtrmapbt_init_rec_from_cur,
	.init_ptr_from_cur	= xfs_rtrmapbt_init_ptr_from_cur,
	.key_diff		= xfs_rtrmapbt_key_diff,
	.buf_ops		= &xfs_rtrmapbt_buf_ops,
	.diff_two_keys		= xfs_rtrmapbt_diff_two_keys,
	.keys_inorder		= xfs_rtrmapbt_keys_inorder,
	.recs_inorder		= xfs_rtrmapbt_recs_inorder,
	.keys_contiguous	= xfs_rtrmapbt_keys_contiguous,
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
	unsigned int		minrecs[2];
	unsigned int		blocklen;

	blocklen = XFS_MIN_CRC_BLOCKSIZE - XFS_BTREE_LBLOCK_CRC_LEN;

	minrecs[0] = xfs_rtrmapbt_block_maxrecs(blocklen, true) / 2;
	minrecs[1] = xfs_rtrmapbt_block_maxrecs(blocklen, false) / 2;

	/* We need at most one record for every block in an rt group. */
	return xfs_btree_compute_maxlevels(minrecs, XFS_MAX_RGBLOCKS);
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
	 */
	d_maxlevels = xfs_btree_space_to_height(mp->m_rtrmap_mnr,
				mp->m_sb.sb_dblocks);
	r_maxlevels = xfs_btree_compute_maxlevels(mp->m_rtrmap_mnr,
				mp->m_groups[XG_TYPE_RTG].blocks);

	/* Add one level to handle the inode root level. */
	mp->m_rtrmap_maxlevels = min(d_maxlevels, r_maxlevels) + 1;
}

/* Calculate the rtrmap btree size for some records. */
static unsigned long long
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
