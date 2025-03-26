// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2020-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_inode.h"
#include "xfs_trans.h"
#include "xfs_bmap.h"
#include "xfs_icache.h"
#include "xfs_quota.h"
#include "xfs_exchmaps.h"
#include "xfs_trace.h"
#include "xfs_bmap_btree.h"
#include "xfs_trans_space.h"
#include "xfs_error.h"
#include "xfs_errortag.h"
#include "xfs_health.h"
#include "xfs_exchmaps_item.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"
#include "xfs_attr_leaf.h"
#include "xfs_attr.h"
#include "xfs_dir2_priv.h"
#include "xfs_dir2.h"
#include "xfs_symlink_remote.h"

struct kmem_cache	*xfs_exchmaps_intent_cache;

/* bmbt mappings adjacent to a pair of records. */
struct xfs_exchmaps_adjacent {
	struct xfs_bmbt_irec		left1;
	struct xfs_bmbt_irec		right1;
	struct xfs_bmbt_irec		left2;
	struct xfs_bmbt_irec		right2;
};

#define ADJACENT_INIT { \
	.left1  = { .br_startblock = HOLESTARTBLOCK }, \
	.right1 = { .br_startblock = HOLESTARTBLOCK }, \
	.left2  = { .br_startblock = HOLESTARTBLOCK }, \
	.right2 = { .br_startblock = HOLESTARTBLOCK }, \
}

/* Information to reset reflink flag / CoW fork state after an exchange. */

/*
 * If the reflink flag is set on either inode, make sure it has an incore CoW
 * fork, since all reflink inodes must have them.  If there's a CoW fork and it
 * has mappings in it, make sure the inodes are tagged appropriately so that
 * speculative preallocations can be GC'd if we run low of space.
 */
static inline void
xfs_exchmaps_ensure_cowfork(
	struct xfs_inode	*ip)
{
	struct xfs_ifork	*cfork;

	if (xfs_is_reflink_inode(ip))
		xfs_ifork_init_cow(ip);

	cfork = xfs_ifork_ptr(ip, XFS_COW_FORK);
	if (!cfork)
		return;
	if (cfork->if_bytes > 0)
		xfs_inode_set_cowblocks_tag(ip);
	else
		xfs_inode_clear_cowblocks_tag(ip);
}

/*
 * Adjust the on-disk inode size upwards if needed so that we never add
 * mappings into the file past EOF.  This is crucial so that log recovery won't
 * get confused by the sudden appearance of post-eof mappings.
 */
STATIC void
xfs_exchmaps_update_size(
	struct xfs_trans	*tp,
	struct xfs_inode	*ip,
	struct xfs_bmbt_irec	*imap,
	xfs_fsize_t		new_isize)
{
	struct xfs_mount	*mp = tp->t_mountp;
	xfs_fsize_t		len;

	if (new_isize < 0)
		return;

	len = min(XFS_FSB_TO_B(mp, imap->br_startoff + imap->br_blockcount),
		  new_isize);

	if (len <= ip->i_disk_size)
		return;

	trace_xfs_exchmaps_update_inode_size(ip, len);

	ip->i_disk_size = len;
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
}

/* Advance the incore state tracking after exchanging a mapping. */
static inline void
xmi_advance(
	struct xfs_exchmaps_intent	*xmi,
	const struct xfs_bmbt_irec	*irec)
{
	xmi->xmi_startoff1 += irec->br_blockcount;
	xmi->xmi_startoff2 += irec->br_blockcount;
	xmi->xmi_blockcount -= irec->br_blockcount;
}

/* Do we still have more mappings to exchange? */
static inline bool
xmi_has_more_exchange_work(const struct xfs_exchmaps_intent *xmi)
{
	return xmi->xmi_blockcount > 0;
}

/* Do we have post-operation cleanups to perform? */
static inline bool
xmi_has_postop_work(const struct xfs_exchmaps_intent *xmi)
{
	return xmi->xmi_flags & (XFS_EXCHMAPS_CLEAR_INO1_REFLINK |
				 XFS_EXCHMAPS_CLEAR_INO2_REFLINK |
				 __XFS_EXCHMAPS_INO2_SHORTFORM);
}

/* Check all mappings to make sure we can actually exchange them. */
int
xfs_exchmaps_check_forks(
	struct xfs_mount		*mp,
	const struct xfs_exchmaps_req	*req)
{
	struct xfs_ifork		*ifp1, *ifp2;
	int				whichfork = xfs_exchmaps_reqfork(req);

	/* No fork? */
	ifp1 = xfs_ifork_ptr(req->ip1, whichfork);
	ifp2 = xfs_ifork_ptr(req->ip2, whichfork);
	if (!ifp1 || !ifp2)
		return -EINVAL;

	/* We don't know how to exchange local format forks. */
	if (ifp1->if_format == XFS_DINODE_FMT_LOCAL ||
	    ifp2->if_format == XFS_DINODE_FMT_LOCAL)
		return -EINVAL;

	return 0;
}

#ifdef CONFIG_XFS_QUOTA
/* Log the actual updates to the quota accounting. */
static inline void
xfs_exchmaps_update_quota(
	struct xfs_trans		*tp,
	struct xfs_exchmaps_intent	*xmi,
	struct xfs_bmbt_irec		*irec1,
	struct xfs_bmbt_irec		*irec2)
{
	int64_t				ip1_delta = 0, ip2_delta = 0;
	unsigned int			qflag;

	qflag = XFS_IS_REALTIME_INODE(xmi->xmi_ip1) ? XFS_TRANS_DQ_RTBCOUNT :
						      XFS_TRANS_DQ_BCOUNT;

	if (xfs_bmap_is_real_extent(irec1)) {
		ip1_delta -= irec1->br_blockcount;
		ip2_delta += irec1->br_blockcount;
	}

	if (xfs_bmap_is_real_extent(irec2)) {
		ip1_delta += irec2->br_blockcount;
		ip2_delta -= irec2->br_blockcount;
	}

	xfs_trans_mod_dquot_byino(tp, xmi->xmi_ip1, qflag, ip1_delta);
	xfs_trans_mod_dquot_byino(tp, xmi->xmi_ip2, qflag, ip2_delta);
}
#else
# define xfs_exchmaps_update_quota(tp, xmi, irec1, irec2)	((void)0)
#endif

/* Decide if we want to skip this mapping from file1. */
static inline bool
xfs_exchmaps_can_skip_mapping(
	struct xfs_exchmaps_intent	*xmi,
	struct xfs_bmbt_irec		*irec)
{
	struct xfs_mount		*mp = xmi->xmi_ip1->i_mount;

	/* Do not skip this mapping if the caller did not tell us to. */
	if (!(xmi->xmi_flags & XFS_EXCHMAPS_INO1_WRITTEN))
		return false;

	/* Do not skip mapped, written mappings. */
	if (xfs_bmap_is_written_extent(irec))
		return false;

	/*
	 * The mapping is unwritten or a hole.  It cannot be a delalloc
	 * reservation because we already excluded those.  It cannot be an
	 * unwritten extent with dirty page cache because we flushed the page
	 * cache.  For files where the allocation unit is 1FSB (files on the
	 * data dev, rt files if the extent size is 1FSB), we can safely
	 * skip this mapping.
	 */
	if (!xfs_inode_has_bigrtalloc(xmi->xmi_ip1))
		return true;

	/*
	 * For a realtime file with a multi-fsb allocation unit, the decision
	 * is trickier because we can only swap full allocation units.
	 * Unwritten mappings can appear in the middle of an rtx if the rtx is
	 * partially written, but they can also appear for preallocations.
	 *
	 * If the mapping is a hole, skip it entirely.  Holes should align with
	 * rtx boundaries.
	 */
	if (!xfs_bmap_is_real_extent(irec))
		return true;

	/*
	 * All mappings below this point are unwritten.
	 *
	 * - If the beginning is not aligned to an rtx, trim the end of the
	 *   mapping so that it does not cross an rtx boundary, and swap it.
	 *
	 * - If both ends are aligned to an rtx, skip the entire mapping.
	 */
	if (!isaligned_64(irec->br_startoff, mp->m_sb.sb_rextsize)) {
		xfs_fileoff_t	new_end;

		new_end = roundup_64(irec->br_startoff, mp->m_sb.sb_rextsize);
		irec->br_blockcount = min(irec->br_blockcount,
					  new_end - irec->br_startoff);
		return false;
	}
	if (isaligned_64(irec->br_blockcount, mp->m_sb.sb_rextsize))
		return true;

	/*
	 * All mappings below this point are unwritten, start on an rtx
	 * boundary, and do not end on an rtx boundary.
	 *
	 * - If the mapping is longer than one rtx, trim the end of the mapping
	 *   down to an rtx boundary and skip it.
	 *
	 * - The mapping is shorter than one rtx.  Swap it.
	 */
	if (irec->br_blockcount > mp->m_sb.sb_rextsize) {
		xfs_fileoff_t	new_end;

		new_end = rounddown_64(irec->br_startoff + irec->br_blockcount,
				mp->m_sb.sb_rextsize);
		irec->br_blockcount = new_end - irec->br_startoff;
		return true;
	}

	return false;
}

/*
 * Walk forward through the file ranges in @xmi until we find two different
 * mappings to exchange.  If there is work to do, return the mappings;
 * otherwise we've reached the end of the range and xmi_blockcount will be
 * zero.
 *
 * If the walk skips over a pair of mappings to the same storage, save them as
 * the left records in @adj (if provided) so that the simulation phase can
 * avoid an extra lookup.
  */
static int
xfs_exchmaps_find_mappings(
	struct xfs_exchmaps_intent	*xmi,
	struct xfs_bmbt_irec		*irec1,
	struct xfs_bmbt_irec		*irec2,
	struct xfs_exchmaps_adjacent	*adj)
{
	int				nimaps;
	int				bmap_flags;
	int				error;

	bmap_flags = xfs_bmapi_aflag(xfs_exchmaps_whichfork(xmi));

	for (; xmi_has_more_exchange_work(xmi); xmi_advance(xmi, irec1)) {
		/* Read mapping from the first file */
		nimaps = 1;
		error = xfs_bmapi_read(xmi->xmi_ip1, xmi->xmi_startoff1,
				xmi->xmi_blockcount, irec1, &nimaps,
				bmap_flags);
		if (error)
			return error;
		if (nimaps != 1 ||
		    irec1->br_startblock == DELAYSTARTBLOCK ||
		    irec1->br_startoff != xmi->xmi_startoff1) {
			/*
			 * We should never get no mapping or a delalloc mapping
			 * or something that doesn't match what we asked for,
			 * since the caller flushed both inodes and we hold the
			 * ILOCKs for both inodes.
			 */
			ASSERT(0);
			return -EINVAL;
		}

		if (xfs_exchmaps_can_skip_mapping(xmi, irec1)) {
			trace_xfs_exchmaps_mapping1_skip(xmi->xmi_ip1, irec1);
			continue;
		}

		/* Read mapping from the second file */
		nimaps = 1;
		error = xfs_bmapi_read(xmi->xmi_ip2, xmi->xmi_startoff2,
				irec1->br_blockcount, irec2, &nimaps,
				bmap_flags);
		if (error)
			return error;
		if (nimaps != 1 ||
		    irec2->br_startblock == DELAYSTARTBLOCK ||
		    irec2->br_startoff != xmi->xmi_startoff2) {
			/*
			 * We should never get no mapping or a delalloc mapping
			 * or something that doesn't match what we asked for,
			 * since the caller flushed both inodes and we hold the
			 * ILOCKs for both inodes.
			 */
			ASSERT(0);
			return -EINVAL;
		}

		/*
		 * We can only exchange as many blocks as the smaller of the
		 * two mapping maps.
		 */
		irec1->br_blockcount = min(irec1->br_blockcount,
					   irec2->br_blockcount);

		trace_xfs_exchmaps_mapping1(xmi->xmi_ip1, irec1);
		trace_xfs_exchmaps_mapping2(xmi->xmi_ip2, irec2);

		/* We found something to exchange, so return it. */
		if (irec1->br_startblock != irec2->br_startblock)
			return 0;

		/*
		 * Two mappings pointing to the same physical block must not
		 * have different states; that's filesystem corruption.  Move
		 * on to the next mapping if they're both holes or both point
		 * to the same physical space extent.
		 */
		if (irec1->br_state != irec2->br_state) {
			xfs_bmap_mark_sick(xmi->xmi_ip1,
					xfs_exchmaps_whichfork(xmi));
			xfs_bmap_mark_sick(xmi->xmi_ip2,
					xfs_exchmaps_whichfork(xmi));
			return -EFSCORRUPTED;
		}

		/*
		 * Save the mappings if we're estimating work and skipping
		 * these identical mappings.
		 */
		if (adj) {
			memcpy(&adj->left1, irec1, sizeof(*irec1));
			memcpy(&adj->left2, irec2, sizeof(*irec2));
		}
	}

	return 0;
}

/* Exchange these two mappings. */
static void
xfs_exchmaps_one_step(
	struct xfs_trans		*tp,
	struct xfs_exchmaps_intent	*xmi,
	struct xfs_bmbt_irec		*irec1,
	struct xfs_bmbt_irec		*irec2)
{
	int				whichfork = xfs_exchmaps_whichfork(xmi);

	xfs_exchmaps_update_quota(tp, xmi, irec1, irec2);

	/* Remove both mappings. */
	xfs_bmap_unmap_extent(tp, xmi->xmi_ip1, whichfork, irec1);
	xfs_bmap_unmap_extent(tp, xmi->xmi_ip2, whichfork, irec2);

	/*
	 * Re-add both mappings.  We exchange the file offsets between the two
	 * maps and add the opposite map, which has the effect of filling the
	 * logical offsets we just unmapped, but with with the physical mapping
	 * information exchanged.
	 */
	swap(irec1->br_startoff, irec2->br_startoff);
	xfs_bmap_map_extent(tp, xmi->xmi_ip1, whichfork, irec2);
	xfs_bmap_map_extent(tp, xmi->xmi_ip2, whichfork, irec1);

	/* Make sure we're not adding mappings past EOF. */
	if (whichfork == XFS_DATA_FORK) {
		xfs_exchmaps_update_size(tp, xmi->xmi_ip1, irec2,
				xmi->xmi_isize1);
		xfs_exchmaps_update_size(tp, xmi->xmi_ip2, irec1,
				xmi->xmi_isize2);
	}

	/*
	 * Advance our cursor and exit.   The caller (either defer ops or log
	 * recovery) will log the XMD item, and if *blockcount is nonzero, it
	 * will log a new XMI item for the remainder and call us back.
	 */
	xmi_advance(xmi, irec1);
}

/* Convert inode2's leaf attr fork back to shortform, if possible.. */
STATIC int
xfs_exchmaps_attr_to_sf(
	struct xfs_trans		*tp,
	struct xfs_exchmaps_intent	*xmi)
{
	struct xfs_da_args	args = {
		.dp		= xmi->xmi_ip2,
		.geo		= tp->t_mountp->m_attr_geo,
		.whichfork	= XFS_ATTR_FORK,
		.trans		= tp,
		.owner		= xmi->xmi_ip2->i_ino,
	};
	struct xfs_buf		*bp;
	int			forkoff;
	int			error;

	if (!xfs_attr_is_leaf(xmi->xmi_ip2))
		return 0;

	error = xfs_attr3_leaf_read(tp, xmi->xmi_ip2, xmi->xmi_ip2->i_ino, 0,
			&bp);
	if (error)
		return error;

	forkoff = xfs_attr_shortform_allfit(bp, xmi->xmi_ip2);
	if (forkoff == 0)
		return 0;

	return xfs_attr3_leaf_to_shortform(bp, &args, forkoff);
}

/* Convert inode2's block dir fork back to shortform, if possible.. */
STATIC int
xfs_exchmaps_dir_to_sf(
	struct xfs_trans		*tp,
	struct xfs_exchmaps_intent	*xmi)
{
	struct xfs_da_args	args = {
		.dp		= xmi->xmi_ip2,
		.geo		= tp->t_mountp->m_dir_geo,
		.whichfork	= XFS_DATA_FORK,
		.trans		= tp,
		.owner		= xmi->xmi_ip2->i_ino,
	};
	struct xfs_dir2_sf_hdr	sfh;
	struct xfs_buf		*bp;
	int			size;
	int			error = 0;

	if (xfs_dir2_format(&args, &error) != XFS_DIR2_FMT_BLOCK)
		return error;

	error = xfs_dir3_block_read(tp, xmi->xmi_ip2, xmi->xmi_ip2->i_ino, &bp);
	if (error)
		return error;

	size = xfs_dir2_block_sfsize(xmi->xmi_ip2, bp->b_addr, &sfh);
	if (size > xfs_inode_data_fork_size(xmi->xmi_ip2))
		return 0;

	return xfs_dir2_block_to_sf(&args, bp, size, &sfh);
}

/* Convert inode2's remote symlink target back to shortform, if possible. */
STATIC int
xfs_exchmaps_link_to_sf(
	struct xfs_trans		*tp,
	struct xfs_exchmaps_intent	*xmi)
{
	struct xfs_inode		*ip = xmi->xmi_ip2;
	struct xfs_ifork		*ifp = xfs_ifork_ptr(ip, XFS_DATA_FORK);
	char				*buf;
	int				error;

	if (ifp->if_format == XFS_DINODE_FMT_LOCAL ||
	    ip->i_disk_size > xfs_inode_data_fork_size(ip))
		return 0;

	/* Read the current symlink target into a buffer. */
	buf = kmalloc(ip->i_disk_size + 1,
			GFP_KERNEL | __GFP_NOLOCKDEP | __GFP_NOFAIL);
	if (!buf) {
		ASSERT(0);
		return -ENOMEM;
	}

	error = xfs_symlink_remote_read(ip, buf);
	if (error)
		goto free;

	/* Remove the blocks. */
	error = xfs_symlink_remote_truncate(tp, ip);
	if (error)
		goto free;

	/* Convert fork to local format and log our changes. */
	xfs_idestroy_fork(ifp);
	ifp->if_bytes = 0;
	ifp->if_format = XFS_DINODE_FMT_LOCAL;
	xfs_init_local_fork(ip, XFS_DATA_FORK, buf, ip->i_disk_size);
	xfs_trans_log_inode(tp, ip, XFS_ILOG_DDATA | XFS_ILOG_CORE);
free:
	kfree(buf);
	return error;
}

/* Clear the reflink flag after an exchange. */
static inline void
xfs_exchmaps_clear_reflink(
	struct xfs_trans	*tp,
	struct xfs_inode	*ip)
{
	trace_xfs_reflink_unset_inode_flag(ip);

	ip->i_diflags2 &= ~XFS_DIFLAG2_REFLINK;
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
}

/* Finish whatever work might come after an exchange operation. */
static int
xfs_exchmaps_do_postop_work(
	struct xfs_trans		*tp,
	struct xfs_exchmaps_intent	*xmi)
{
	if (xmi->xmi_flags & __XFS_EXCHMAPS_INO2_SHORTFORM) {
		int			error = 0;

		if (xmi->xmi_flags & XFS_EXCHMAPS_ATTR_FORK)
			error = xfs_exchmaps_attr_to_sf(tp, xmi);
		else if (S_ISDIR(VFS_I(xmi->xmi_ip2)->i_mode))
			error = xfs_exchmaps_dir_to_sf(tp, xmi);
		else if (S_ISLNK(VFS_I(xmi->xmi_ip2)->i_mode))
			error = xfs_exchmaps_link_to_sf(tp, xmi);
		xmi->xmi_flags &= ~__XFS_EXCHMAPS_INO2_SHORTFORM;
		if (error)
			return error;
	}

	if (xmi->xmi_flags & XFS_EXCHMAPS_CLEAR_INO1_REFLINK) {
		xfs_exchmaps_clear_reflink(tp, xmi->xmi_ip1);
		xmi->xmi_flags &= ~XFS_EXCHMAPS_CLEAR_INO1_REFLINK;
	}

	if (xmi->xmi_flags & XFS_EXCHMAPS_CLEAR_INO2_REFLINK) {
		xfs_exchmaps_clear_reflink(tp, xmi->xmi_ip2);
		xmi->xmi_flags &= ~XFS_EXCHMAPS_CLEAR_INO2_REFLINK;
	}

	return 0;
}

/* Finish one step in a mapping exchange operation, possibly relogging. */
int
xfs_exchmaps_finish_one(
	struct xfs_trans		*tp,
	struct xfs_exchmaps_intent	*xmi)
{
	struct xfs_bmbt_irec		irec1, irec2;
	int				error;

	if (xmi_has_more_exchange_work(xmi)) {
		/*
		 * If the operation state says that some range of the files
		 * have not yet been exchanged, look for mappings in that range
		 * to exchange.  If we find some mappings, exchange them.
		 */
		error = xfs_exchmaps_find_mappings(xmi, &irec1, &irec2, NULL);
		if (error)
			return error;

		if (xmi_has_more_exchange_work(xmi))
			xfs_exchmaps_one_step(tp, xmi, &irec1, &irec2);

		/*
		 * If the caller asked us to exchange the file sizes after the
		 * exchange and either we just exchanged the last mappings in
		 * the range or we didn't find anything to exchange, update the
		 * ondisk file sizes.
		 */
		if ((xmi->xmi_flags & XFS_EXCHMAPS_SET_SIZES) &&
		    !xmi_has_more_exchange_work(xmi)) {
			xmi->xmi_ip1->i_disk_size = xmi->xmi_isize1;
			xmi->xmi_ip2->i_disk_size = xmi->xmi_isize2;

			xfs_trans_log_inode(tp, xmi->xmi_ip1, XFS_ILOG_CORE);
			xfs_trans_log_inode(tp, xmi->xmi_ip2, XFS_ILOG_CORE);
		}
	} else if (xmi_has_postop_work(xmi)) {
		/*
		 * Now that we're finished with the exchange operation,
		 * complete the post-op cleanup work.
		 */
		error = xfs_exchmaps_do_postop_work(tp, xmi);
		if (error)
			return error;
	}

	if (XFS_TEST_ERROR(false, tp->t_mountp, XFS_ERRTAG_EXCHMAPS_FINISH_ONE))
		return -EIO;

	/* If we still have work to do, ask for a new transaction. */
	if (xmi_has_more_exchange_work(xmi) || xmi_has_postop_work(xmi)) {
		trace_xfs_exchmaps_defer(tp->t_mountp, xmi);
		return -EAGAIN;
	}

	/*
	 * If we reach here, we've finished all the exchange work and the post
	 * operation work.  The last thing we need to do before returning to
	 * the caller is to make sure that COW forks are set up correctly.
	 */
	if (!(xmi->xmi_flags & XFS_EXCHMAPS_ATTR_FORK)) {
		xfs_exchmaps_ensure_cowfork(xmi->xmi_ip1);
		xfs_exchmaps_ensure_cowfork(xmi->xmi_ip2);
	}

	return 0;
}

/*
 * Compute the amount of bmbt blocks we should reserve for each file.  In the
 * worst case, each exchange will fill a hole with a new mapping, which could
 * result in a btree split every time we add a new leaf block.
 */
static inline uint64_t
xfs_exchmaps_bmbt_blocks(
	struct xfs_mount		*mp,
	const struct xfs_exchmaps_req	*req)
{
	return howmany_64(req->nr_exchanges,
					XFS_MAX_CONTIG_BMAPS_PER_BLOCK(mp)) *
			XFS_EXTENTADD_SPACE_RES(mp, xfs_exchmaps_reqfork(req));
}

/* Compute the space we should reserve for the rmap btree expansions. */
static inline uint64_t
xfs_exchmaps_rmapbt_blocks(
	struct xfs_mount		*mp,
	const struct xfs_exchmaps_req	*req)
{
	if (!xfs_has_rmapbt(mp))
		return 0;
	if (XFS_IS_REALTIME_INODE(req->ip1))
		return howmany_64(req->nr_exchanges,
					XFS_MAX_CONTIG_RTRMAPS_PER_BLOCK(mp)) *
			XFS_RTRMAPADD_SPACE_RES(mp);

	return howmany_64(req->nr_exchanges,
					XFS_MAX_CONTIG_RMAPS_PER_BLOCK(mp)) *
			XFS_RMAPADD_SPACE_RES(mp);
}

/* Estimate the bmbt and rmapbt overhead required to exchange mappings. */
int
xfs_exchmaps_estimate_overhead(
	struct xfs_exchmaps_req		*req)
{
	struct xfs_mount		*mp = req->ip1->i_mount;
	xfs_filblks_t			bmbt_blocks;
	xfs_filblks_t			rmapbt_blocks;
	xfs_filblks_t			resblks = req->resblks;

	/*
	 * Compute the number of bmbt and rmapbt blocks we might need to handle
	 * the estimated number of exchanges.
	 */
	bmbt_blocks = xfs_exchmaps_bmbt_blocks(mp, req);
	rmapbt_blocks = xfs_exchmaps_rmapbt_blocks(mp, req);

	trace_xfs_exchmaps_overhead(mp, bmbt_blocks, rmapbt_blocks);

	/* Make sure the change in file block count doesn't overflow. */
	if (check_add_overflow(req->ip1_bcount, bmbt_blocks, &req->ip1_bcount))
		return -EFBIG;
	if (check_add_overflow(req->ip2_bcount, bmbt_blocks, &req->ip2_bcount))
		return -EFBIG;

	/*
	 * Add together the number of blocks we need to handle btree growth,
	 * then add it to the number of blocks we need to reserve to this
	 * transaction.
	 */
	if (check_add_overflow(resblks, bmbt_blocks, &resblks))
		return -ENOSPC;
	if (check_add_overflow(resblks, bmbt_blocks, &resblks))
		return -ENOSPC;
	if (check_add_overflow(resblks, rmapbt_blocks, &resblks))
		return -ENOSPC;
	if (check_add_overflow(resblks, rmapbt_blocks, &resblks))
		return -ENOSPC;

	/* Can't actually reserve more than UINT_MAX blocks. */
	if (req->resblks > UINT_MAX)
		return -ENOSPC;

	req->resblks = resblks;
	trace_xfs_exchmaps_final_estimate(req);
	return 0;
}

/* Decide if we can merge two real mappings. */
static inline bool
xmi_can_merge(
	const struct xfs_bmbt_irec	*b1,
	const struct xfs_bmbt_irec	*b2)
{
	/* Don't merge holes. */
	if (b1->br_startblock == HOLESTARTBLOCK ||
	    b2->br_startblock == HOLESTARTBLOCK)
		return false;

	/* We don't merge holes. */
	if (!xfs_bmap_is_real_extent(b1) || !xfs_bmap_is_real_extent(b2))
		return false;

	if (b1->br_startoff   + b1->br_blockcount == b2->br_startoff &&
	    b1->br_startblock + b1->br_blockcount == b2->br_startblock &&
	    b1->br_state			  == b2->br_state &&
	    b1->br_blockcount + b2->br_blockcount <= XFS_MAX_BMBT_EXTLEN)
		return true;

	return false;
}

/*
 * Decide if we can merge three mappings.  Caller must ensure all three
 * mappings must not be holes or delalloc reservations.
 */
static inline bool
xmi_can_merge_all(
	const struct xfs_bmbt_irec	*l,
	const struct xfs_bmbt_irec	*m,
	const struct xfs_bmbt_irec	*r)
{
	xfs_filblks_t			new_len;

	new_len = l->br_blockcount + m->br_blockcount + r->br_blockcount;
	return new_len <= XFS_MAX_BMBT_EXTLEN;
}

#define CLEFT_CONTIG	0x01
#define CRIGHT_CONTIG	0x02
#define CHOLE		0x04
#define CBOTH_CONTIG	(CLEFT_CONTIG | CRIGHT_CONTIG)

#define NLEFT_CONTIG	0x10
#define NRIGHT_CONTIG	0x20
#define NHOLE		0x40
#define NBOTH_CONTIG	(NLEFT_CONTIG | NRIGHT_CONTIG)

/* Estimate the effect of a single exchange on mapping count. */
static inline int
xmi_delta_nextents_step(
	struct xfs_mount		*mp,
	const struct xfs_bmbt_irec	*left,
	const struct xfs_bmbt_irec	*curr,
	const struct xfs_bmbt_irec	*new,
	const struct xfs_bmbt_irec	*right)
{
	bool				lhole, rhole, chole, nhole;
	unsigned int			state = 0;
	int				ret = 0;

	lhole = left->br_startblock == HOLESTARTBLOCK;
	rhole = right->br_startblock == HOLESTARTBLOCK;
	chole = curr->br_startblock == HOLESTARTBLOCK;
	nhole = new->br_startblock == HOLESTARTBLOCK;

	if (chole)
		state |= CHOLE;
	if (!lhole && !chole && xmi_can_merge(left, curr))
		state |= CLEFT_CONTIG;
	if (!rhole && !chole && xmi_can_merge(curr, right))
		state |= CRIGHT_CONTIG;
	if ((state & CBOTH_CONTIG) == CBOTH_CONTIG &&
	    !xmi_can_merge_all(left, curr, right))
		state &= ~CRIGHT_CONTIG;

	if (nhole)
		state |= NHOLE;
	if (!lhole && !nhole && xmi_can_merge(left, new))
		state |= NLEFT_CONTIG;
	if (!rhole && !nhole && xmi_can_merge(new, right))
		state |= NRIGHT_CONTIG;
	if ((state & NBOTH_CONTIG) == NBOTH_CONTIG &&
	    !xmi_can_merge_all(left, new, right))
		state &= ~NRIGHT_CONTIG;

	switch (state & (CLEFT_CONTIG | CRIGHT_CONTIG | CHOLE)) {
	case CLEFT_CONTIG | CRIGHT_CONTIG:
		/*
		 * left/curr/right are the same mapping, so deleting curr
		 * causes 2 new mappings to be created.
		 */
		ret += 2;
		break;
	case 0:
		/*
		 * curr is not contiguous with any mapping, so we remove curr
		 * completely
		 */
		ret--;
		break;
	case CHOLE:
		/* hole, do nothing */
		break;
	case CLEFT_CONTIG:
	case CRIGHT_CONTIG:
		/* trim either left or right, no change */
		break;
	}

	switch (state & (NLEFT_CONTIG | NRIGHT_CONTIG | NHOLE)) {
	case NLEFT_CONTIG | NRIGHT_CONTIG:
		/*
		 * left/curr/right will become the same mapping, so adding
		 * curr causes the deletion of right.
		 */
		ret--;
		break;
	case 0:
		/* new is not contiguous with any mapping */
		ret++;
		break;
	case NHOLE:
		/* hole, do nothing. */
		break;
	case NLEFT_CONTIG:
	case NRIGHT_CONTIG:
		/* new is absorbed into left or right, no change */
		break;
	}

	trace_xfs_exchmaps_delta_nextents_step(mp, left, curr, new, right, ret,
			state);
	return ret;
}

/* Make sure we don't overflow the extent (mapping) counters. */
static inline int
xmi_ensure_delta_nextents(
	struct xfs_exchmaps_req	*req,
	struct xfs_inode	*ip,
	int64_t			delta)
{
	struct xfs_mount	*mp = ip->i_mount;
	int			whichfork = xfs_exchmaps_reqfork(req);
	struct xfs_ifork	*ifp = xfs_ifork_ptr(ip, whichfork);
	uint64_t		new_nextents;
	xfs_extnum_t		max_nextents;

	if (delta < 0)
		return 0;

	/*
	 * It's always an error if the delta causes integer overflow.  delta
	 * needs an explicit cast here to avoid warnings about implicit casts
	 * coded into the overflow check.
	 */
	if (check_add_overflow(ifp->if_nextents, (uint64_t)delta,
				&new_nextents))
		return -EFBIG;

	if (XFS_TEST_ERROR(false, mp, XFS_ERRTAG_REDUCE_MAX_IEXTENTS) &&
	    new_nextents > 10)
		return -EFBIG;

	/*
	 * We always promote both inodes to have large extent counts if the
	 * superblock feature is enabled, so we only need to check against the
	 * theoretical maximum.
	 */
	max_nextents = xfs_iext_max_nextents(xfs_has_large_extent_counts(mp),
					     whichfork);
	if (new_nextents > max_nextents)
		return -EFBIG;

	return 0;
}

/* Find the next mapping after irec. */
static inline int
xmi_next(
	struct xfs_inode		*ip,
	int				bmap_flags,
	const struct xfs_bmbt_irec	*irec,
	struct xfs_bmbt_irec		*nrec)
{
	xfs_fileoff_t			off;
	xfs_filblks_t			blockcount;
	int				nimaps = 1;
	int				error;

	off = irec->br_startoff + irec->br_blockcount;
	blockcount = XFS_MAX_FILEOFF - off;
	error = xfs_bmapi_read(ip, off, blockcount, nrec, &nimaps, bmap_flags);
	if (error)
		return error;
	if (nrec->br_startblock == DELAYSTARTBLOCK ||
	    nrec->br_startoff != off) {
		/*
		 * If we don't get the mapping we want, return a zero-length
		 * mapping, which our estimator function will pretend is a hole.
		 * We shouldn't get delalloc reservations.
		 */
		nrec->br_startblock = HOLESTARTBLOCK;
	}

	return 0;
}

int __init
xfs_exchmaps_intent_init_cache(void)
{
	xfs_exchmaps_intent_cache = kmem_cache_create("xfs_exchmaps_intent",
			sizeof(struct xfs_exchmaps_intent),
			0, 0, NULL);

	return xfs_exchmaps_intent_cache != NULL ? 0 : -ENOMEM;
}

void
xfs_exchmaps_intent_destroy_cache(void)
{
	kmem_cache_destroy(xfs_exchmaps_intent_cache);
	xfs_exchmaps_intent_cache = NULL;
}

/*
 * Decide if we will exchange the reflink flags between the two files after the
 * exchange.  The only time we want to do this is if we're exchanging all
 * mappings under EOF and the inode reflink flags have different states.
 */
static inline bool
xmi_can_exchange_reflink_flags(
	const struct xfs_exchmaps_req	*req,
	unsigned int			reflink_state)
{
	struct xfs_mount		*mp = req->ip1->i_mount;

	if (hweight32(reflink_state) != 1)
		return false;
	if (req->startoff1 != 0 || req->startoff2 != 0)
		return false;
	if (req->blockcount != XFS_B_TO_FSB(mp, req->ip1->i_disk_size))
		return false;
	if (req->blockcount != XFS_B_TO_FSB(mp, req->ip2->i_disk_size))
		return false;
	return true;
}


/* Allocate and initialize a new incore intent item from a request. */
struct xfs_exchmaps_intent *
xfs_exchmaps_init_intent(
	const struct xfs_exchmaps_req	*req)
{
	struct xfs_exchmaps_intent	*xmi;
	unsigned int			rs = 0;

	xmi = kmem_cache_zalloc(xfs_exchmaps_intent_cache,
			GFP_NOFS | __GFP_NOFAIL);
	INIT_LIST_HEAD(&xmi->xmi_list);
	xmi->xmi_ip1 = req->ip1;
	xmi->xmi_ip2 = req->ip2;
	xmi->xmi_startoff1 = req->startoff1;
	xmi->xmi_startoff2 = req->startoff2;
	xmi->xmi_blockcount = req->blockcount;
	xmi->xmi_isize1 = xmi->xmi_isize2 = -1;
	xmi->xmi_flags = req->flags & XFS_EXCHMAPS_PARAMS;

	if (xfs_exchmaps_whichfork(xmi) == XFS_ATTR_FORK) {
		xmi->xmi_flags |= __XFS_EXCHMAPS_INO2_SHORTFORM;
		return xmi;
	}

	if (req->flags & XFS_EXCHMAPS_SET_SIZES) {
		xmi->xmi_flags |= XFS_EXCHMAPS_SET_SIZES;
		xmi->xmi_isize1 = req->ip2->i_disk_size;
		xmi->xmi_isize2 = req->ip1->i_disk_size;
	}

	/* Record the state of each inode's reflink flag before the op. */
	if (xfs_is_reflink_inode(req->ip1))
		rs |= 1;
	if (xfs_is_reflink_inode(req->ip2))
		rs |= 2;

	/*
	 * Figure out if we're clearing the reflink flags (which effectively
	 * exchanges them) after the operation.
	 */
	if (xmi_can_exchange_reflink_flags(req, rs)) {
		if (rs & 1)
			xmi->xmi_flags |= XFS_EXCHMAPS_CLEAR_INO1_REFLINK;
		if (rs & 2)
			xmi->xmi_flags |= XFS_EXCHMAPS_CLEAR_INO2_REFLINK;
	}

	if (S_ISDIR(VFS_I(xmi->xmi_ip2)->i_mode) ||
	    S_ISLNK(VFS_I(xmi->xmi_ip2)->i_mode))
		xmi->xmi_flags |= __XFS_EXCHMAPS_INO2_SHORTFORM;

	return xmi;
}

/*
 * Estimate the number of exchange operations and the number of file blocks
 * in each file that will be affected by the exchange operation.
 */
int
xfs_exchmaps_estimate(
	struct xfs_exchmaps_req		*req)
{
	struct xfs_exchmaps_intent	*xmi;
	struct xfs_bmbt_irec		irec1, irec2;
	struct xfs_exchmaps_adjacent	adj = ADJACENT_INIT;
	xfs_filblks_t			ip1_blocks = 0, ip2_blocks = 0;
	int64_t				d_nexts1, d_nexts2;
	int				bmap_flags;
	int				error;

	ASSERT(!(req->flags & ~XFS_EXCHMAPS_PARAMS));

	bmap_flags = xfs_bmapi_aflag(xfs_exchmaps_reqfork(req));
	xmi = xfs_exchmaps_init_intent(req);

	/*
	 * To guard against the possibility of overflowing the extent counters,
	 * we have to estimate an upper bound on the potential increase in that
	 * counter.  We can split the mapping at each end of the range, and for
	 * each step of the exchange we can split the mapping that we're
	 * working on if the mappings do not align.
	 */
	d_nexts1 = d_nexts2 = 3;

	while (xmi_has_more_exchange_work(xmi)) {
		/*
		 * Walk through the file ranges until we find something to
		 * exchange.  Because we're simulating the exchange, pass in
		 * adj to capture skipped mappings for correct estimation of
		 * bmbt record merges.
		 */
		error = xfs_exchmaps_find_mappings(xmi, &irec1, &irec2, &adj);
		if (error)
			goto out_free;
		if (!xmi_has_more_exchange_work(xmi))
			break;

		/* Update accounting. */
		if (xfs_bmap_is_real_extent(&irec1))
			ip1_blocks += irec1.br_blockcount;
		if (xfs_bmap_is_real_extent(&irec2))
			ip2_blocks += irec2.br_blockcount;
		req->nr_exchanges++;

		/* Read the next mappings from both files. */
		error = xmi_next(req->ip1, bmap_flags, &irec1, &adj.right1);
		if (error)
			goto out_free;

		error = xmi_next(req->ip2, bmap_flags, &irec2, &adj.right2);
		if (error)
			goto out_free;

		/* Update extent count deltas. */
		d_nexts1 += xmi_delta_nextents_step(req->ip1->i_mount,
				&adj.left1, &irec1, &irec2, &adj.right1);

		d_nexts2 += xmi_delta_nextents_step(req->ip1->i_mount,
				&adj.left2, &irec2, &irec1, &adj.right2);

		/* Now pretend we exchanged the mappings. */
		if (xmi_can_merge(&adj.left2, &irec1))
			adj.left2.br_blockcount += irec1.br_blockcount;
		else
			memcpy(&adj.left2, &irec1, sizeof(irec1));

		if (xmi_can_merge(&adj.left1, &irec2))
			adj.left1.br_blockcount += irec2.br_blockcount;
		else
			memcpy(&adj.left1, &irec2, sizeof(irec2));

		xmi_advance(xmi, &irec1);
	}

	/* Account for the blocks that are being exchanged. */
	if (XFS_IS_REALTIME_INODE(req->ip1) &&
	    xfs_exchmaps_reqfork(req) == XFS_DATA_FORK) {
		req->ip1_rtbcount = ip1_blocks;
		req->ip2_rtbcount = ip2_blocks;
	} else {
		req->ip1_bcount = ip1_blocks;
		req->ip2_bcount = ip2_blocks;
	}

	/*
	 * Make sure that both forks have enough slack left in their extent
	 * counters that the exchange operation will not overflow.
	 */
	trace_xfs_exchmaps_delta_nextents(req, d_nexts1, d_nexts2);
	if (req->ip1 == req->ip2) {
		error = xmi_ensure_delta_nextents(req, req->ip1,
				d_nexts1 + d_nexts2);
	} else {
		error = xmi_ensure_delta_nextents(req, req->ip1, d_nexts1);
		if (error)
			goto out_free;
		error = xmi_ensure_delta_nextents(req, req->ip2, d_nexts2);
	}
	if (error)
		goto out_free;

	trace_xfs_exchmaps_initial_estimate(req);
	error = xfs_exchmaps_estimate_overhead(req);
out_free:
	kmem_cache_free(xfs_exchmaps_intent_cache, xmi);
	return error;
}

/* Set the reflink flag before an operation. */
static inline void
xfs_exchmaps_set_reflink(
	struct xfs_trans	*tp,
	struct xfs_inode	*ip)
{
	trace_xfs_reflink_set_inode_flag(ip);

	ip->i_diflags2 |= XFS_DIFLAG2_REFLINK;
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
}

/*
 * If either file has shared blocks and we're exchanging data forks, we must
 * flag the other file as having shared blocks so that we get the shared-block
 * rmap functions if we need to fix up the rmaps.
 */
void
xfs_exchmaps_ensure_reflink(
	struct xfs_trans			*tp,
	const struct xfs_exchmaps_intent	*xmi)
{
	unsigned int				rs = 0;

	if (xfs_is_reflink_inode(xmi->xmi_ip1))
		rs |= 1;
	if (xfs_is_reflink_inode(xmi->xmi_ip2))
		rs |= 2;

	if ((rs & 1) && !xfs_is_reflink_inode(xmi->xmi_ip2))
		xfs_exchmaps_set_reflink(tp, xmi->xmi_ip2);

	if ((rs & 2) && !xfs_is_reflink_inode(xmi->xmi_ip1))
		xfs_exchmaps_set_reflink(tp, xmi->xmi_ip1);
}

/* Set the large extent count flag before an operation if needed. */
static inline void
xfs_exchmaps_ensure_large_extent_counts(
	struct xfs_trans	*tp,
	struct xfs_inode	*ip)
{
	if (xfs_inode_has_large_extent_counts(ip))
		return;

	ip->i_diflags2 |= XFS_DIFLAG2_NREXT64;
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
}

/* Widen the extent counter fields of both inodes if necessary. */
void
xfs_exchmaps_upgrade_extent_counts(
	struct xfs_trans			*tp,
	const struct xfs_exchmaps_intent	*xmi)
{
	if (!xfs_has_large_extent_counts(tp->t_mountp))
		return;

	xfs_exchmaps_ensure_large_extent_counts(tp, xmi->xmi_ip1);
	xfs_exchmaps_ensure_large_extent_counts(tp, xmi->xmi_ip2);
}

/*
 * Schedule an exchange a range of mappings from one inode to another.
 *
 * The use of file mapping exchange log intent items ensures the operation can
 * be resumed even if the system goes down.  The caller must commit the
 * transaction to start the work.
 *
 * The caller must ensure the inodes must be joined to the transaction and
 * ILOCKd; they will still be joined to the transaction at exit.
 */
void
xfs_exchange_mappings(
	struct xfs_trans		*tp,
	const struct xfs_exchmaps_req	*req)
{
	struct xfs_exchmaps_intent	*xmi;

	BUILD_BUG_ON(XFS_EXCHMAPS_INTERNAL_FLAGS & XFS_EXCHMAPS_LOGGED_FLAGS);

	xfs_assert_ilocked(req->ip1, XFS_ILOCK_EXCL);
	xfs_assert_ilocked(req->ip2, XFS_ILOCK_EXCL);
	ASSERT(!(req->flags & ~XFS_EXCHMAPS_LOGGED_FLAGS));
	if (req->flags & XFS_EXCHMAPS_SET_SIZES)
		ASSERT(!(req->flags & XFS_EXCHMAPS_ATTR_FORK));
	ASSERT(xfs_has_exchange_range(tp->t_mountp));

	if (req->blockcount == 0)
		return;

	xmi = xfs_exchmaps_init_intent(req);
	xfs_exchmaps_defer_add(tp, xmi);
	xfs_exchmaps_ensure_reflink(tp, xmi);
	xfs_exchmaps_upgrade_extent_counts(tp, xmi);
}
