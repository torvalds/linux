/*
 * Copyright (C) 2018 Oracle.  All Rights Reserved.
 *
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_btree.h"
#include "xfs_bit.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_inode.h"
#include "xfs_icache.h"
#include "xfs_alloc.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc.h"
#include "xfs_ialloc_btree.h"
#include "xfs_rmap.h"
#include "xfs_rmap_btree.h"
#include "xfs_refcount.h"
#include "xfs_refcount_btree.h"
#include "xfs_extent_busy.h"
#include "xfs_ag_resv.h"
#include "xfs_trans_space.h"
#include "scrub/xfs_scrub.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"
#include "scrub/repair.h"

/*
 * Attempt to repair some metadata, if the metadata is corrupt and userspace
 * told us to fix it.  This function returns -EAGAIN to mean "re-run scrub",
 * and will set *fixed to true if it thinks it repaired anything.
 */
int
xfs_repair_attempt(
	struct xfs_inode		*ip,
	struct xfs_scrub_context	*sc,
	bool				*fixed)
{
	int				error = 0;

	trace_xfs_repair_attempt(ip, sc->sm, error);

	xfs_scrub_ag_btcur_free(&sc->sa);

	/* Repair whatever's broken. */
	ASSERT(sc->ops->repair);
	error = sc->ops->repair(sc);
	trace_xfs_repair_done(ip, sc->sm, error);
	switch (error) {
	case 0:
		/*
		 * Repair succeeded.  Commit the fixes and perform a second
		 * scrub so that we can tell userspace if we fixed the problem.
		 */
		sc->sm->sm_flags &= ~XFS_SCRUB_FLAGS_OUT;
		*fixed = true;
		return -EAGAIN;
	case -EDEADLOCK:
	case -EAGAIN:
		/* Tell the caller to try again having grabbed all the locks. */
		if (!sc->try_harder) {
			sc->try_harder = true;
			return -EAGAIN;
		}
		/*
		 * We tried harder but still couldn't grab all the resources
		 * we needed to fix it.  The corruption has not been fixed,
		 * so report back to userspace.
		 */
		return -EFSCORRUPTED;
	default:
		return error;
	}
}

/*
 * Complain about unfixable problems in the filesystem.  We don't log
 * corruptions when IFLAG_REPAIR wasn't set on the assumption that the driver
 * program is xfs_scrub, which will call back with IFLAG_REPAIR set if the
 * administrator isn't running xfs_scrub in no-repairs mode.
 *
 * Use this helper function because _ratelimited silently declares a static
 * structure to track rate limiting information.
 */
void
xfs_repair_failure(
	struct xfs_mount		*mp)
{
	xfs_alert_ratelimited(mp,
"Corruption not fixed during online repair.  Unmount and run xfs_repair.");
}

/*
 * Repair probe -- userspace uses this to probe if we're willing to repair a
 * given mountpoint.
 */
int
xfs_repair_probe(
	struct xfs_scrub_context	*sc)
{
	int				error = 0;

	if (xfs_scrub_should_terminate(sc, &error))
		return error;

	return 0;
}

/*
 * Roll a transaction, keeping the AG headers locked and reinitializing
 * the btree cursors.
 */
int
xfs_repair_roll_ag_trans(
	struct xfs_scrub_context	*sc)
{
	int				error;

	/* Keep the AG header buffers locked so we can keep going. */
	xfs_trans_bhold(sc->tp, sc->sa.agi_bp);
	xfs_trans_bhold(sc->tp, sc->sa.agf_bp);
	xfs_trans_bhold(sc->tp, sc->sa.agfl_bp);

	/* Roll the transaction. */
	error = xfs_trans_roll(&sc->tp);
	if (error)
		goto out_release;

	/* Join AG headers to the new transaction. */
	xfs_trans_bjoin(sc->tp, sc->sa.agi_bp);
	xfs_trans_bjoin(sc->tp, sc->sa.agf_bp);
	xfs_trans_bjoin(sc->tp, sc->sa.agfl_bp);

	return 0;

out_release:
	/*
	 * Rolling failed, so release the hold on the buffers.  The
	 * buffers will be released during teardown on our way out
	 * of the kernel.
	 */
	xfs_trans_bhold_release(sc->tp, sc->sa.agi_bp);
	xfs_trans_bhold_release(sc->tp, sc->sa.agf_bp);
	xfs_trans_bhold_release(sc->tp, sc->sa.agfl_bp);

	return error;
}

/*
 * Does the given AG have enough space to rebuild a btree?  Neither AG
 * reservation can be critical, and we must have enough space (factoring
 * in AG reservations) to construct a whole btree.
 */
bool
xfs_repair_ag_has_space(
	struct xfs_perag		*pag,
	xfs_extlen_t			nr_blocks,
	enum xfs_ag_resv_type		type)
{
	return  !xfs_ag_resv_critical(pag, XFS_AG_RESV_RMAPBT) &&
		!xfs_ag_resv_critical(pag, XFS_AG_RESV_METADATA) &&
		pag->pagf_freeblks > xfs_ag_resv_needed(pag, type) + nr_blocks;
}

/*
 * Figure out how many blocks to reserve for an AG repair.  We calculate the
 * worst case estimate for the number of blocks we'd need to rebuild one of
 * any type of per-AG btree.
 */
xfs_extlen_t
xfs_repair_calc_ag_resblks(
	struct xfs_scrub_context	*sc)
{
	struct xfs_mount		*mp = sc->mp;
	struct xfs_scrub_metadata	*sm = sc->sm;
	struct xfs_perag		*pag;
	struct xfs_buf			*bp;
	xfs_agino_t			icount = 0;
	xfs_extlen_t			aglen = 0;
	xfs_extlen_t			usedlen;
	xfs_extlen_t			freelen;
	xfs_extlen_t			bnobt_sz;
	xfs_extlen_t			inobt_sz;
	xfs_extlen_t			rmapbt_sz;
	xfs_extlen_t			refcbt_sz;
	int				error;

	if (!(sm->sm_flags & XFS_SCRUB_IFLAG_REPAIR))
		return 0;

	/* Use in-core counters if possible. */
	pag = xfs_perag_get(mp, sm->sm_agno);
	if (pag->pagi_init)
		icount = pag->pagi_count;

	/*
	 * Otherwise try to get the actual counters from disk; if not, make
	 * some worst case assumptions.
	 */
	if (icount == 0) {
		error = xfs_ialloc_read_agi(mp, NULL, sm->sm_agno, &bp);
		if (error) {
			icount = mp->m_sb.sb_agblocks / mp->m_sb.sb_inopblock;
		} else {
			icount = pag->pagi_count;
			xfs_buf_relse(bp);
		}
	}

	/* Now grab the block counters from the AGF. */
	error = xfs_alloc_read_agf(mp, NULL, sm->sm_agno, 0, &bp);
	if (error) {
		aglen = mp->m_sb.sb_agblocks;
		freelen = aglen;
		usedlen = aglen;
	} else {
		aglen = be32_to_cpu(XFS_BUF_TO_AGF(bp)->agf_length);
		freelen = pag->pagf_freeblks;
		usedlen = aglen - freelen;
		xfs_buf_relse(bp);
	}
	xfs_perag_put(pag);

	trace_xfs_repair_calc_ag_resblks(mp, sm->sm_agno, icount, aglen,
			freelen, usedlen);

	/*
	 * Figure out how many blocks we'd need worst case to rebuild
	 * each type of btree.  Note that we can only rebuild the
	 * bnobt/cntbt or inobt/finobt as pairs.
	 */
	bnobt_sz = 2 * xfs_allocbt_calc_size(mp, freelen);
	if (xfs_sb_version_hassparseinodes(&mp->m_sb))
		inobt_sz = xfs_iallocbt_calc_size(mp, icount /
				XFS_INODES_PER_HOLEMASK_BIT);
	else
		inobt_sz = xfs_iallocbt_calc_size(mp, icount /
				XFS_INODES_PER_CHUNK);
	if (xfs_sb_version_hasfinobt(&mp->m_sb))
		inobt_sz *= 2;
	if (xfs_sb_version_hasreflink(&mp->m_sb))
		refcbt_sz = xfs_refcountbt_calc_size(mp, usedlen);
	else
		refcbt_sz = 0;
	if (xfs_sb_version_hasrmapbt(&mp->m_sb)) {
		/*
		 * Guess how many blocks we need to rebuild the rmapbt.
		 * For non-reflink filesystems we can't have more records than
		 * used blocks.  However, with reflink it's possible to have
		 * more than one rmap record per AG block.  We don't know how
		 * many rmaps there could be in the AG, so we start off with
		 * what we hope is an generous over-estimation.
		 */
		if (xfs_sb_version_hasreflink(&mp->m_sb))
			rmapbt_sz = xfs_rmapbt_calc_size(mp,
					(unsigned long long)aglen * 2);
		else
			rmapbt_sz = xfs_rmapbt_calc_size(mp, usedlen);
	} else {
		rmapbt_sz = 0;
	}

	trace_xfs_repair_calc_ag_resblks_btsize(mp, sm->sm_agno, bnobt_sz,
			inobt_sz, rmapbt_sz, refcbt_sz);

	return max(max(bnobt_sz, inobt_sz), max(rmapbt_sz, refcbt_sz));
}

/* Allocate a block in an AG. */
int
xfs_repair_alloc_ag_block(
	struct xfs_scrub_context	*sc,
	struct xfs_owner_info		*oinfo,
	xfs_fsblock_t			*fsbno,
	enum xfs_ag_resv_type		resv)
{
	struct xfs_alloc_arg		args = {0};
	xfs_agblock_t			bno;
	int				error;

	switch (resv) {
	case XFS_AG_RESV_AGFL:
	case XFS_AG_RESV_RMAPBT:
		error = xfs_alloc_get_freelist(sc->tp, sc->sa.agf_bp, &bno, 1);
		if (error)
			return error;
		if (bno == NULLAGBLOCK)
			return -ENOSPC;
		xfs_extent_busy_reuse(sc->mp, sc->sa.agno, bno,
				1, false);
		*fsbno = XFS_AGB_TO_FSB(sc->mp, sc->sa.agno, bno);
		if (resv == XFS_AG_RESV_RMAPBT)
			xfs_ag_resv_rmapbt_alloc(sc->mp, sc->sa.agno);
		return 0;
	default:
		break;
	}

	args.tp = sc->tp;
	args.mp = sc->mp;
	args.oinfo = *oinfo;
	args.fsbno = XFS_AGB_TO_FSB(args.mp, sc->sa.agno, 0);
	args.minlen = 1;
	args.maxlen = 1;
	args.prod = 1;
	args.type = XFS_ALLOCTYPE_THIS_AG;
	args.resv = resv;

	error = xfs_alloc_vextent(&args);
	if (error)
		return error;
	if (args.fsbno == NULLFSBLOCK)
		return -ENOSPC;
	ASSERT(args.len == 1);
	*fsbno = args.fsbno;

	return 0;
}

/* Initialize a new AG btree root block with zero entries. */
int
xfs_repair_init_btblock(
	struct xfs_scrub_context	*sc,
	xfs_fsblock_t			fsb,
	struct xfs_buf			**bpp,
	xfs_btnum_t			btnum,
	const struct xfs_buf_ops	*ops)
{
	struct xfs_trans		*tp = sc->tp;
	struct xfs_mount		*mp = sc->mp;
	struct xfs_buf			*bp;

	trace_xfs_repair_init_btblock(mp, XFS_FSB_TO_AGNO(mp, fsb),
			XFS_FSB_TO_AGBNO(mp, fsb), btnum);

	ASSERT(XFS_FSB_TO_AGNO(mp, fsb) == sc->sa.agno);
	bp = xfs_trans_get_buf(tp, mp->m_ddev_targp, XFS_FSB_TO_DADDR(mp, fsb),
			XFS_FSB_TO_BB(mp, 1), 0);
	xfs_buf_zero(bp, 0, BBTOB(bp->b_length));
	xfs_btree_init_block(mp, bp, btnum, 0, 0, sc->sa.agno, 0);
	xfs_trans_buf_set_type(tp, bp, XFS_BLFT_BTREE_BUF);
	xfs_trans_log_buf(tp, bp, 0, bp->b_length);
	bp->b_ops = ops;
	*bpp = bp;

	return 0;
}

/*
 * Reconstructing per-AG Btrees
 *
 * When a space btree is corrupt, we don't bother trying to fix it.  Instead,
 * we scan secondary space metadata to derive the records that should be in
 * the damaged btree, initialize a fresh btree root, and insert the records.
 * Note that for rebuilding the rmapbt we scan all the primary data to
 * generate the new records.
 *
 * However, that leaves the matter of removing all the metadata describing the
 * old broken structure.  For primary metadata we use the rmap data to collect
 * every extent with a matching rmap owner (exlist); we then iterate all other
 * metadata structures with the same rmap owner to collect the extents that
 * cannot be removed (sublist).  We then subtract sublist from exlist to
 * derive the blocks that were used by the old btree.  These blocks can be
 * reaped.
 *
 * For rmapbt reconstructions we must use different tactics for extent
 * collection.  First we iterate all primary metadata (this excludes the old
 * rmapbt, obviously) to generate new rmap records.  The gaps in the rmap
 * records are collected as exlist.  The bnobt records are collected as
 * sublist.  As with the other btrees we subtract sublist from exlist, and the
 * result (since the rmapbt lives in the free space) are the blocks from the
 * old rmapbt.
 */

/* Collect a dead btree extent for later disposal. */
int
xfs_repair_collect_btree_extent(
	struct xfs_scrub_context	*sc,
	struct xfs_repair_extent_list	*exlist,
	xfs_fsblock_t			fsbno,
	xfs_extlen_t			len)
{
	struct xfs_repair_extent	*rex;

	trace_xfs_repair_collect_btree_extent(sc->mp,
			XFS_FSB_TO_AGNO(sc->mp, fsbno),
			XFS_FSB_TO_AGBNO(sc->mp, fsbno), len);

	rex = kmem_alloc(sizeof(struct xfs_repair_extent), KM_MAYFAIL);
	if (!rex)
		return -ENOMEM;

	INIT_LIST_HEAD(&rex->list);
	rex->fsbno = fsbno;
	rex->len = len;
	list_add_tail(&rex->list, &exlist->list);

	return 0;
}

/*
 * An error happened during the rebuild so the transaction will be cancelled.
 * The fs will shut down, and the administrator has to unmount and run repair.
 * Therefore, free all the memory associated with the list so we can die.
 */
void
xfs_repair_cancel_btree_extents(
	struct xfs_scrub_context	*sc,
	struct xfs_repair_extent_list	*exlist)
{
	struct xfs_repair_extent	*rex;
	struct xfs_repair_extent	*n;

	for_each_xfs_repair_extent_safe(rex, n, exlist) {
		list_del(&rex->list);
		kmem_free(rex);
	}
}

/* Compare two btree extents. */
static int
xfs_repair_btree_extent_cmp(
	void				*priv,
	struct list_head		*a,
	struct list_head		*b)
{
	struct xfs_repair_extent	*ap;
	struct xfs_repair_extent	*bp;

	ap = container_of(a, struct xfs_repair_extent, list);
	bp = container_of(b, struct xfs_repair_extent, list);

	if (ap->fsbno > bp->fsbno)
		return 1;
	if (ap->fsbno < bp->fsbno)
		return -1;
	return 0;
}

/*
 * Remove all the blocks mentioned in @sublist from the extents in @exlist.
 *
 * The intent is that callers will iterate the rmapbt for all of its records
 * for a given owner to generate @exlist; and iterate all the blocks of the
 * metadata structures that are not being rebuilt and have the same rmapbt
 * owner to generate @sublist.  This routine subtracts all the extents
 * mentioned in sublist from all the extents linked in @exlist, which leaves
 * @exlist as the list of blocks that are not accounted for, which we assume
 * are the dead blocks of the old metadata structure.  The blocks mentioned in
 * @exlist can be reaped.
 */
#define LEFT_ALIGNED	(1 << 0)
#define RIGHT_ALIGNED	(1 << 1)
int
xfs_repair_subtract_extents(
	struct xfs_scrub_context	*sc,
	struct xfs_repair_extent_list	*exlist,
	struct xfs_repair_extent_list	*sublist)
{
	struct list_head		*lp;
	struct xfs_repair_extent	*ex;
	struct xfs_repair_extent	*newex;
	struct xfs_repair_extent	*subex;
	xfs_fsblock_t			sub_fsb;
	xfs_extlen_t			sub_len;
	int				state;
	int				error = 0;

	if (list_empty(&exlist->list) || list_empty(&sublist->list))
		return 0;
	ASSERT(!list_empty(&sublist->list));

	list_sort(NULL, &exlist->list, xfs_repair_btree_extent_cmp);
	list_sort(NULL, &sublist->list, xfs_repair_btree_extent_cmp);

	/*
	 * Now that we've sorted both lists, we iterate exlist once, rolling
	 * forward through sublist and/or exlist as necessary until we find an
	 * overlap or reach the end of either list.  We do not reset lp to the
	 * head of exlist nor do we reset subex to the head of sublist.  The
	 * list traversal is similar to merge sort, but we're deleting
	 * instead.  In this manner we avoid O(n^2) operations.
	 */
	subex = list_first_entry(&sublist->list, struct xfs_repair_extent,
			list);
	lp = exlist->list.next;
	while (lp != &exlist->list) {
		ex = list_entry(lp, struct xfs_repair_extent, list);

		/*
		 * Advance subex and/or ex until we find a pair that
		 * intersect or we run out of extents.
		 */
		while (subex->fsbno + subex->len <= ex->fsbno) {
			if (list_is_last(&subex->list, &sublist->list))
				goto out;
			subex = list_next_entry(subex, list);
		}
		if (subex->fsbno >= ex->fsbno + ex->len) {
			lp = lp->next;
			continue;
		}

		/* trim subex to fit the extent we have */
		sub_fsb = subex->fsbno;
		sub_len = subex->len;
		if (subex->fsbno < ex->fsbno) {
			sub_len -= ex->fsbno - subex->fsbno;
			sub_fsb = ex->fsbno;
		}
		if (sub_len > ex->len)
			sub_len = ex->len;

		state = 0;
		if (sub_fsb == ex->fsbno)
			state |= LEFT_ALIGNED;
		if (sub_fsb + sub_len == ex->fsbno + ex->len)
			state |= RIGHT_ALIGNED;
		switch (state) {
		case LEFT_ALIGNED:
			/* Coincides with only the left. */
			ex->fsbno += sub_len;
			ex->len -= sub_len;
			break;
		case RIGHT_ALIGNED:
			/* Coincides with only the right. */
			ex->len -= sub_len;
			lp = lp->next;
			break;
		case LEFT_ALIGNED | RIGHT_ALIGNED:
			/* Total overlap, just delete ex. */
			lp = lp->next;
			list_del(&ex->list);
			kmem_free(ex);
			break;
		case 0:
			/*
			 * Deleting from the middle: add the new right extent
			 * and then shrink the left extent.
			 */
			newex = kmem_alloc(sizeof(struct xfs_repair_extent),
					KM_MAYFAIL);
			if (!newex) {
				error = -ENOMEM;
				goto out;
			}
			INIT_LIST_HEAD(&newex->list);
			newex->fsbno = sub_fsb + sub_len;
			newex->len = ex->fsbno + ex->len - newex->fsbno;
			list_add(&newex->list, &ex->list);
			ex->len = sub_fsb - ex->fsbno;
			lp = lp->next;
			break;
		default:
			ASSERT(0);
			break;
		}
	}

out:
	return error;
}
#undef LEFT_ALIGNED
#undef RIGHT_ALIGNED
