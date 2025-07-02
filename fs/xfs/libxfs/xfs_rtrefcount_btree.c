// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2021-2024 Oracle.  All Rights Reserved.
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
#include "xfs_rtrefcount_btree.h"
#include "xfs_refcount.h"
#include "xfs_trace.h"
#include "xfs_cksum.h"
#include "xfs_error.h"
#include "xfs_extent_busy.h"
#include "xfs_rtgroup.h"
#include "xfs_rtbitmap.h"
#include "xfs_metafile.h"
#include "xfs_health.h"

static struct kmem_cache	*xfs_rtrefcountbt_cur_cache;

/*
 * Realtime Reference Count btree.
 *
 * This is a btree used to track the owner(s) of a given extent in the realtime
 * device.  See the comments in xfs_refcount_btree.c for more information.
 *
 * This tree is basically the same as the regular refcount btree except that
 * it's rooted in an inode.
 */

static struct xfs_btree_cur *
xfs_rtrefcountbt_dup_cursor(
	struct xfs_btree_cur	*cur)
{
	return xfs_rtrefcountbt_init_cursor(cur->bc_tp, to_rtg(cur->bc_group));
}

STATIC int
xfs_rtrefcountbt_get_minrecs(
	struct xfs_btree_cur	*cur,
	int			level)
{
	if (level == cur->bc_nlevels - 1) {
		struct xfs_ifork	*ifp = xfs_btree_ifork_ptr(cur);

		return xfs_rtrefcountbt_maxrecs(cur->bc_mp, ifp->if_broot_bytes,
				level == 0) / 2;
	}

	return cur->bc_mp->m_rtrefc_mnr[level != 0];
}

STATIC int
xfs_rtrefcountbt_get_maxrecs(
	struct xfs_btree_cur	*cur,
	int			level)
{
	if (level == cur->bc_nlevels - 1) {
		struct xfs_ifork	*ifp = xfs_btree_ifork_ptr(cur);

		return xfs_rtrefcountbt_maxrecs(cur->bc_mp, ifp->if_broot_bytes,
				level == 0);
	}

	return cur->bc_mp->m_rtrefc_mxr[level != 0];
}

/*
 * Calculate number of records in a realtime refcount btree inode root.
 */
unsigned int
xfs_rtrefcountbt_droot_maxrecs(
	unsigned int		blocklen,
	bool			leaf)
{
	blocklen -= sizeof(struct xfs_rtrefcount_root);

	if (leaf)
		return blocklen / sizeof(struct xfs_refcount_rec);
	return blocklen / (2 * sizeof(struct xfs_refcount_key) +
			sizeof(xfs_rtrefcount_ptr_t));
}

/*
 * Get the maximum records we could store in the on-disk format.
 *
 * For non-root nodes this is equivalent to xfs_rtrefcountbt_get_maxrecs, but
 * for the root node this checks the available space in the dinode fork so that
 * we can resize the in-memory buffer to match it.  After a resize to the
 * maximum size this function returns the same value as
 * xfs_rtrefcountbt_get_maxrecs for the root node, too.
 */
STATIC int
xfs_rtrefcountbt_get_dmaxrecs(
	struct xfs_btree_cur	*cur,
	int			level)
{
	if (level != cur->bc_nlevels - 1)
		return cur->bc_mp->m_rtrefc_mxr[level != 0];
	return xfs_rtrefcountbt_droot_maxrecs(cur->bc_ino.forksize, level == 0);
}

STATIC void
xfs_rtrefcountbt_init_key_from_rec(
	union xfs_btree_key		*key,
	const union xfs_btree_rec	*rec)
{
	key->refc.rc_startblock = rec->refc.rc_startblock;
}

STATIC void
xfs_rtrefcountbt_init_high_key_from_rec(
	union xfs_btree_key		*key,
	const union xfs_btree_rec	*rec)
{
	__u32				x;

	x = be32_to_cpu(rec->refc.rc_startblock);
	x += be32_to_cpu(rec->refc.rc_blockcount) - 1;
	key->refc.rc_startblock = cpu_to_be32(x);
}

STATIC void
xfs_rtrefcountbt_init_rec_from_cur(
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
xfs_rtrefcountbt_init_ptr_from_cur(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr)
{
	ptr->l = 0;
}

STATIC int
xfs_rtrefcountbt_cmp_key_with_cur(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_key	*key)
{
	const struct xfs_refcount_key	*kp = &key->refc;
	const struct xfs_refcount_irec	*irec = &cur->bc_rec.rc;
	uint32_t			start;

	start = xfs_refcount_encode_startblock(irec->rc_startblock,
			irec->rc_domain);
	return cmp_int(be32_to_cpu(kp->rc_startblock), start);
}

STATIC int
xfs_rtrefcountbt_cmp_two_keys(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_key	*k1,
	const union xfs_btree_key	*k2,
	const union xfs_btree_key	*mask)
{
	ASSERT(!mask || mask->refc.rc_startblock);

	return cmp_int(be32_to_cpu(k1->refc.rc_startblock),
		       be32_to_cpu(k2->refc.rc_startblock));
}

static xfs_failaddr_t
xfs_rtrefcountbt_verify(
	struct xfs_buf		*bp)
{
	struct xfs_mount	*mp = bp->b_target->bt_mount;
	struct xfs_btree_block	*block = XFS_BUF_TO_BLOCK(bp);
	xfs_failaddr_t		fa;
	int			level;

	if (!xfs_verify_magic(bp, block->bb_magic))
		return __this_address;

	if (!xfs_has_reflink(mp))
		return __this_address;
	fa = xfs_btree_fsblock_v5hdr_verify(bp, XFS_RMAP_OWN_UNKNOWN);
	if (fa)
		return fa;
	level = be16_to_cpu(block->bb_level);
	if (level > mp->m_rtrefc_maxlevels)
		return __this_address;

	return xfs_btree_fsblock_verify(bp, mp->m_rtrefc_mxr[level != 0]);
}

static void
xfs_rtrefcountbt_read_verify(
	struct xfs_buf	*bp)
{
	xfs_failaddr_t	fa;

	if (!xfs_btree_fsblock_verify_crc(bp))
		xfs_verifier_error(bp, -EFSBADCRC, __this_address);
	else {
		fa = xfs_rtrefcountbt_verify(bp);
		if (fa)
			xfs_verifier_error(bp, -EFSCORRUPTED, fa);
	}

	if (bp->b_error)
		trace_xfs_btree_corrupt(bp, _RET_IP_);
}

static void
xfs_rtrefcountbt_write_verify(
	struct xfs_buf	*bp)
{
	xfs_failaddr_t	fa;

	fa = xfs_rtrefcountbt_verify(bp);
	if (fa) {
		trace_xfs_btree_corrupt(bp, _RET_IP_);
		xfs_verifier_error(bp, -EFSCORRUPTED, fa);
		return;
	}
	xfs_btree_fsblock_calc_crc(bp);

}

const struct xfs_buf_ops xfs_rtrefcountbt_buf_ops = {
	.name			= "xfs_rtrefcountbt",
	.magic			= { 0, cpu_to_be32(XFS_RTREFC_CRC_MAGIC) },
	.verify_read		= xfs_rtrefcountbt_read_verify,
	.verify_write		= xfs_rtrefcountbt_write_verify,
	.verify_struct		= xfs_rtrefcountbt_verify,
};

STATIC int
xfs_rtrefcountbt_keys_inorder(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_key	*k1,
	const union xfs_btree_key	*k2)
{
	return be32_to_cpu(k1->refc.rc_startblock) <
	       be32_to_cpu(k2->refc.rc_startblock);
}

STATIC int
xfs_rtrefcountbt_recs_inorder(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_rec	*r1,
	const union xfs_btree_rec	*r2)
{
	return  be32_to_cpu(r1->refc.rc_startblock) +
		be32_to_cpu(r1->refc.rc_blockcount) <=
		be32_to_cpu(r2->refc.rc_startblock);
}

STATIC enum xbtree_key_contig
xfs_rtrefcountbt_keys_contiguous(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_key	*key1,
	const union xfs_btree_key	*key2,
	const union xfs_btree_key	*mask)
{
	ASSERT(!mask || mask->refc.rc_startblock);

	return xbtree_key_contig(be32_to_cpu(key1->refc.rc_startblock),
				 be32_to_cpu(key2->refc.rc_startblock));
}

static inline void
xfs_rtrefcountbt_move_ptrs(
	struct xfs_mount	*mp,
	struct xfs_btree_block	*broot,
	short			old_size,
	size_t			new_size,
	unsigned int		numrecs)
{
	void			*dptr;
	void			*sptr;

	sptr = xfs_rtrefcount_broot_ptr_addr(mp, broot, 1, old_size);
	dptr = xfs_rtrefcount_broot_ptr_addr(mp, broot, 1, new_size);
	memmove(dptr, sptr, numrecs * sizeof(xfs_rtrefcount_ptr_t));
}

static struct xfs_btree_block *
xfs_rtrefcountbt_broot_realloc(
	struct xfs_btree_cur	*cur,
	unsigned int		new_numrecs)
{
	struct xfs_mount	*mp = cur->bc_mp;
	struct xfs_ifork	*ifp = xfs_btree_ifork_ptr(cur);
	struct xfs_btree_block	*broot;
	unsigned int		new_size;
	unsigned int		old_size = ifp->if_broot_bytes;
	const unsigned int	level = cur->bc_nlevels - 1;

	new_size = xfs_rtrefcount_broot_space_calc(mp, level, new_numrecs);

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
		old_numrecs = xfs_rtrefcountbt_maxrecs(mp, old_size, level);
		broot = xfs_broot_realloc(ifp, new_size);
		if (level > 0)
			xfs_rtrefcountbt_move_ptrs(mp, broot, old_size,
					new_size, old_numrecs);
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
		xfs_rtrefcountbt_move_ptrs(mp, ifp->if_broot, old_size,
				new_size, new_numrecs);
	broot = xfs_broot_realloc(ifp, new_size);

out_broot:
	ASSERT(xfs_rtrefcount_droot_space(broot) <=
	       xfs_inode_fork_size(cur->bc_ino.ip, cur->bc_ino.whichfork));
	return broot;
}

const struct xfs_btree_ops xfs_rtrefcountbt_ops = {
	.name			= "rtrefcount",
	.type			= XFS_BTREE_TYPE_INODE,
	.geom_flags		= XFS_BTGEO_IROOT_RECORDS,

	.rec_len		= sizeof(struct xfs_refcount_rec),
	.key_len		= sizeof(struct xfs_refcount_key),
	.ptr_len		= XFS_BTREE_LONG_PTR_LEN,

	.lru_refs		= XFS_REFC_BTREE_REF,
	.statoff		= XFS_STATS_CALC_INDEX(xs_rtrefcbt_2),
	.sick_mask		= XFS_SICK_RG_REFCNTBT,

	.dup_cursor		= xfs_rtrefcountbt_dup_cursor,
	.alloc_block		= xfs_btree_alloc_metafile_block,
	.free_block		= xfs_btree_free_metafile_block,
	.get_minrecs		= xfs_rtrefcountbt_get_minrecs,
	.get_maxrecs		= xfs_rtrefcountbt_get_maxrecs,
	.get_dmaxrecs		= xfs_rtrefcountbt_get_dmaxrecs,
	.init_key_from_rec	= xfs_rtrefcountbt_init_key_from_rec,
	.init_high_key_from_rec	= xfs_rtrefcountbt_init_high_key_from_rec,
	.init_rec_from_cur	= xfs_rtrefcountbt_init_rec_from_cur,
	.init_ptr_from_cur	= xfs_rtrefcountbt_init_ptr_from_cur,
	.cmp_key_with_cur	= xfs_rtrefcountbt_cmp_key_with_cur,
	.buf_ops		= &xfs_rtrefcountbt_buf_ops,
	.cmp_two_keys		= xfs_rtrefcountbt_cmp_two_keys,
	.keys_inorder		= xfs_rtrefcountbt_keys_inorder,
	.recs_inorder		= xfs_rtrefcountbt_recs_inorder,
	.keys_contiguous	= xfs_rtrefcountbt_keys_contiguous,
	.broot_realloc		= xfs_rtrefcountbt_broot_realloc,
};

/* Allocate a new rt refcount btree cursor. */
struct xfs_btree_cur *
xfs_rtrefcountbt_init_cursor(
	struct xfs_trans	*tp,
	struct xfs_rtgroup	*rtg)
{
	struct xfs_inode	*ip = rtg_refcount(rtg);
	struct xfs_mount	*mp = rtg_mount(rtg);
	struct xfs_btree_cur	*cur;

	xfs_assert_ilocked(ip, XFS_ILOCK_SHARED | XFS_ILOCK_EXCL);

	cur = xfs_btree_alloc_cursor(mp, tp, &xfs_rtrefcountbt_ops,
			mp->m_rtrefc_maxlevels, xfs_rtrefcountbt_cur_cache);

	cur->bc_ino.ip = ip;
	cur->bc_refc.nr_ops = 0;
	cur->bc_refc.shape_changes = 0;
	cur->bc_group = xfs_group_hold(rtg_group(rtg));
	cur->bc_nlevels = be16_to_cpu(ip->i_df.if_broot->bb_level) + 1;
	cur->bc_ino.forksize = xfs_inode_fork_size(ip, XFS_DATA_FORK);
	cur->bc_ino.whichfork = XFS_DATA_FORK;
	return cur;
}

/*
 * Install a new rt reverse mapping btree root.  Caller is responsible for
 * invalidating and freeing the old btree blocks.
 */
void
xfs_rtrefcountbt_commit_staged_btree(
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

/* Calculate number of records in a realtime refcount btree block. */
static inline unsigned int
xfs_rtrefcountbt_block_maxrecs(
	unsigned int		blocklen,
	bool			leaf)
{

	if (leaf)
		return blocklen / sizeof(struct xfs_refcount_rec);
	return blocklen / (sizeof(struct xfs_refcount_key) +
			   sizeof(xfs_rtrefcount_ptr_t));
}

/*
 * Calculate number of records in an refcount btree block.
 */
unsigned int
xfs_rtrefcountbt_maxrecs(
	struct xfs_mount	*mp,
	unsigned int		blocklen,
	bool			leaf)
{
	blocklen -= XFS_RTREFCOUNT_BLOCK_LEN;
	return xfs_rtrefcountbt_block_maxrecs(blocklen, leaf);
}

/* Compute the max possible height for realtime refcount btrees. */
unsigned int
xfs_rtrefcountbt_maxlevels_ondisk(void)
{
	unsigned int		minrecs[2];
	unsigned int		blocklen;

	blocklen = XFS_MIN_CRC_BLOCKSIZE - XFS_BTREE_LBLOCK_CRC_LEN;

	minrecs[0] = xfs_rtrefcountbt_block_maxrecs(blocklen, true) / 2;
	minrecs[1] = xfs_rtrefcountbt_block_maxrecs(blocklen, false) / 2;

	/* We need at most one record for every block in an rt group. */
	return xfs_btree_compute_maxlevels(minrecs, XFS_MAX_RGBLOCKS);
}

int __init
xfs_rtrefcountbt_init_cur_cache(void)
{
	xfs_rtrefcountbt_cur_cache = kmem_cache_create("xfs_rtrefcountbt_cur",
			xfs_btree_cur_sizeof(
					xfs_rtrefcountbt_maxlevels_ondisk()),
			0, 0, NULL);

	if (!xfs_rtrefcountbt_cur_cache)
		return -ENOMEM;
	return 0;
}

void
xfs_rtrefcountbt_destroy_cur_cache(void)
{
	kmem_cache_destroy(xfs_rtrefcountbt_cur_cache);
	xfs_rtrefcountbt_cur_cache = NULL;
}

/* Compute the maximum height of a realtime refcount btree. */
void
xfs_rtrefcountbt_compute_maxlevels(
	struct xfs_mount	*mp)
{
	unsigned int		d_maxlevels, r_maxlevels;

	if (!xfs_has_rtreflink(mp)) {
		mp->m_rtrefc_maxlevels = 0;
		return;
	}

	/*
	 * The realtime refcountbt lives on the data device, which means that
	 * its maximum height is constrained by the size of the data device and
	 * the height required to store one refcount record for each rtextent
	 * in an rt group.
	 */
	d_maxlevels = xfs_btree_space_to_height(mp->m_rtrefc_mnr,
				mp->m_sb.sb_dblocks);
	r_maxlevels = xfs_btree_compute_maxlevels(mp->m_rtrefc_mnr,
				mp->m_sb.sb_rgextents);

	/* Add one level to handle the inode root level. */
	mp->m_rtrefc_maxlevels = min(d_maxlevels, r_maxlevels) + 1;
}

/* Calculate the rtrefcount btree size for some records. */
unsigned long long
xfs_rtrefcountbt_calc_size(
	struct xfs_mount	*mp,
	unsigned long long	len)
{
	return xfs_btree_calc_size(mp->m_rtrefc_mnr, len);
}

/*
 * Calculate the maximum refcount btree size.
 */
static unsigned long long
xfs_rtrefcountbt_max_size(
	struct xfs_mount	*mp,
	xfs_rtblock_t		rtblocks)
{
	/* Bail out if we're uninitialized, which can happen in mkfs. */
	if (mp->m_rtrefc_mxr[0] == 0)
		return 0;

	return xfs_rtrefcountbt_calc_size(mp, rtblocks);
}

/*
 * Figure out how many blocks to reserve and how many are used by this btree.
 * We need enough space to hold one record for every rt extent in the rtgroup.
 */
xfs_filblks_t
xfs_rtrefcountbt_calc_reserves(
	struct xfs_mount	*mp)
{
	if (!xfs_has_rtreflink(mp))
		return 0;

	return xfs_rtrefcountbt_max_size(mp, mp->m_sb.sb_rgextents);
}

/*
 * Convert on-disk form of btree root to in-memory form.
 */
STATIC void
xfs_rtrefcountbt_from_disk(
	struct xfs_inode		*ip,
	struct xfs_rtrefcount_root	*dblock,
	int				dblocklen,
	struct xfs_btree_block		*rblock)
{
	struct xfs_mount		*mp = ip->i_mount;
	struct xfs_refcount_key	*fkp;
	__be64				*fpp;
	struct xfs_refcount_key	*tkp;
	__be64				*tpp;
	struct xfs_refcount_rec	*frp;
	struct xfs_refcount_rec	*trp;
	unsigned int			numrecs;
	unsigned int			maxrecs;
	unsigned int			rblocklen;

	rblocklen = xfs_rtrefcount_broot_space(mp, dblock);

	xfs_btree_init_block(mp, rblock, &xfs_rtrefcountbt_ops, 0, 0,
			ip->i_ino);

	rblock->bb_level = dblock->bb_level;
	rblock->bb_numrecs = dblock->bb_numrecs;

	if (be16_to_cpu(rblock->bb_level) > 0) {
		maxrecs = xfs_rtrefcountbt_droot_maxrecs(dblocklen, false);
		fkp = xfs_rtrefcount_droot_key_addr(dblock, 1);
		tkp = xfs_rtrefcount_key_addr(rblock, 1);
		fpp = xfs_rtrefcount_droot_ptr_addr(dblock, 1, maxrecs);
		tpp = xfs_rtrefcount_broot_ptr_addr(mp, rblock, 1, rblocklen);
		numrecs = be16_to_cpu(dblock->bb_numrecs);
		memcpy(tkp, fkp, 2 * sizeof(*fkp) * numrecs);
		memcpy(tpp, fpp, sizeof(*fpp) * numrecs);
	} else {
		frp = xfs_rtrefcount_droot_rec_addr(dblock, 1);
		trp = xfs_rtrefcount_rec_addr(rblock, 1);
		numrecs = be16_to_cpu(dblock->bb_numrecs);
		memcpy(trp, frp, sizeof(*frp) * numrecs);
	}
}

/* Load a realtime reference count btree root in from disk. */
int
xfs_iformat_rtrefcount(
	struct xfs_inode	*ip,
	struct xfs_dinode	*dip)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_rtrefcount_root *dfp = XFS_DFORK_PTR(dip, XFS_DATA_FORK);
	struct xfs_btree_block	*broot;
	unsigned int		numrecs;
	unsigned int		level;
	int			dsize;

	/*
	 * growfs must create the rtrefcount inodes before adding a realtime
	 * volume to the filesystem, so we cannot use the rtrefcount predicate
	 * here.
	 */
	if (!xfs_has_reflink(ip->i_mount)) {
		xfs_inode_mark_sick(ip, XFS_SICK_INO_CORE);
		return -EFSCORRUPTED;
	}

	dsize = XFS_DFORK_SIZE(dip, mp, XFS_DATA_FORK);
	numrecs = be16_to_cpu(dfp->bb_numrecs);
	level = be16_to_cpu(dfp->bb_level);

	if (level > mp->m_rtrefc_maxlevels ||
	    xfs_rtrefcount_droot_space_calc(level, numrecs) > dsize) {
		xfs_inode_mark_sick(ip, XFS_SICK_INO_CORE);
		return -EFSCORRUPTED;
	}

	broot = xfs_broot_alloc(xfs_ifork_ptr(ip, XFS_DATA_FORK),
			xfs_rtrefcount_broot_space_calc(mp, level, numrecs));
	if (broot)
		xfs_rtrefcountbt_from_disk(ip, dfp, dsize, broot);
	return 0;
}

/*
 * Convert in-memory form of btree root to on-disk form.
 */
void
xfs_rtrefcountbt_to_disk(
	struct xfs_mount		*mp,
	struct xfs_btree_block		*rblock,
	int				rblocklen,
	struct xfs_rtrefcount_root	*dblock,
	int				dblocklen)
{
	struct xfs_refcount_key	*fkp;
	__be64				*fpp;
	struct xfs_refcount_key	*tkp;
	__be64				*tpp;
	struct xfs_refcount_rec	*frp;
	struct xfs_refcount_rec	*trp;
	unsigned int			maxrecs;
	unsigned int			numrecs;

	ASSERT(rblock->bb_magic == cpu_to_be32(XFS_RTREFC_CRC_MAGIC));
	ASSERT(uuid_equal(&rblock->bb_u.l.bb_uuid, &mp->m_sb.sb_meta_uuid));
	ASSERT(rblock->bb_u.l.bb_blkno == cpu_to_be64(XFS_BUF_DADDR_NULL));
	ASSERT(rblock->bb_u.l.bb_leftsib == cpu_to_be64(NULLFSBLOCK));
	ASSERT(rblock->bb_u.l.bb_rightsib == cpu_to_be64(NULLFSBLOCK));

	dblock->bb_level = rblock->bb_level;
	dblock->bb_numrecs = rblock->bb_numrecs;

	if (be16_to_cpu(rblock->bb_level) > 0) {
		maxrecs = xfs_rtrefcountbt_droot_maxrecs(dblocklen, false);
		fkp = xfs_rtrefcount_key_addr(rblock, 1);
		tkp = xfs_rtrefcount_droot_key_addr(dblock, 1);
		fpp = xfs_rtrefcount_broot_ptr_addr(mp, rblock, 1, rblocklen);
		tpp = xfs_rtrefcount_droot_ptr_addr(dblock, 1, maxrecs);
		numrecs = be16_to_cpu(rblock->bb_numrecs);
		memcpy(tkp, fkp, 2 * sizeof(*fkp) * numrecs);
		memcpy(tpp, fpp, sizeof(*fpp) * numrecs);
	} else {
		frp = xfs_rtrefcount_rec_addr(rblock, 1);
		trp = xfs_rtrefcount_droot_rec_addr(dblock, 1);
		numrecs = be16_to_cpu(rblock->bb_numrecs);
		memcpy(trp, frp, sizeof(*frp) * numrecs);
	}
}

/* Flush a realtime reference count btree root out to disk. */
void
xfs_iflush_rtrefcount(
	struct xfs_inode	*ip,
	struct xfs_dinode	*dip)
{
	struct xfs_ifork	*ifp = xfs_ifork_ptr(ip, XFS_DATA_FORK);
	struct xfs_rtrefcount_root *dfp = XFS_DFORK_PTR(dip, XFS_DATA_FORK);

	ASSERT(ifp->if_broot != NULL);
	ASSERT(ifp->if_broot_bytes > 0);
	ASSERT(xfs_rtrefcount_droot_space(ifp->if_broot) <=
			xfs_inode_fork_size(ip, XFS_DATA_FORK));
	xfs_rtrefcountbt_to_disk(ip->i_mount, ifp->if_broot,
			ifp->if_broot_bytes, dfp,
			XFS_DFORK_SIZE(dip, ip->i_mount, XFS_DATA_FORK));
}

/*
 * Create a realtime refcount btree inode.
 */
int
xfs_rtrefcountbt_create(
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
	broot = xfs_broot_realloc(ifp,
			xfs_rtrefcount_broot_space_calc(mp, 0, 0));
	if (broot)
		xfs_btree_init_block(mp, broot, &xfs_rtrefcountbt_ops, 0, 0,
				ip->i_ino);
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE | XFS_ILOG_DBROOT);
	return 0;
}
