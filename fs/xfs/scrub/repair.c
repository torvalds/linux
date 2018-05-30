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
#include "xfs_quota.h"
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

/*
 * Disposal of Blocks from Old per-AG Btrees
 *
 * Now that we've constructed a new btree to replace the damaged one, we want
 * to dispose of the blocks that (we think) the old btree was using.
 * Previously, we used the rmapbt to collect the extents (exlist) with the
 * rmap owner corresponding to the tree we rebuilt, collected extents for any
 * blocks with the same rmap owner that are owned by another data structure
 * (sublist), and subtracted sublist from exlist.  In theory the extents
 * remaining in exlist are the old btree's blocks.
 *
 * Unfortunately, it's possible that the btree was crosslinked with other
 * blocks on disk.  The rmap data can tell us if there are multiple owners, so
 * if the rmapbt says there is an owner of this block other than @oinfo, then
 * the block is crosslinked.  Remove the reverse mapping and continue.
 *
 * If there is one rmap record, we can free the block, which removes the
 * reverse mapping but doesn't add the block to the free space.  Our repair
 * strategy is to hope the other metadata objects crosslinked on this block
 * will be rebuilt (atop different blocks), thereby removing all the cross
 * links.
 *
 * If there are no rmap records at all, we also free the block.  If the btree
 * being rebuilt lives in the free space (bnobt/cntbt/rmapbt) then there isn't
 * supposed to be a rmap record and everything is ok.  For other btrees there
 * had to have been an rmap entry for the block to have ended up on @exlist,
 * so if it's gone now there's something wrong and the fs will shut down.
 *
 * Note: If there are multiple rmap records with only the same rmap owner as
 * the btree we're trying to rebuild and the block is indeed owned by another
 * data structure with the same rmap owner, then the block will be in sublist
 * and therefore doesn't need disposal.  If there are multiple rmap records
 * with only the same rmap owner but the block is not owned by something with
 * the same rmap owner, the block will be freed.
 *
 * The caller is responsible for locking the AG headers for the entire rebuild
 * operation so that nothing else can sneak in and change the AG state while
 * we're not looking.  We also assume that the caller already invalidated any
 * buffers associated with @exlist.
 */

/*
 * Invalidate buffers for per-AG btree blocks we're dumping.  This function
 * is not intended for use with file data repairs; we have bunmapi for that.
 */
int
xfs_repair_invalidate_blocks(
	struct xfs_scrub_context	*sc,
	struct xfs_repair_extent_list	*exlist)
{
	struct xfs_repair_extent	*rex;
	struct xfs_repair_extent	*n;
	struct xfs_buf			*bp;
	xfs_fsblock_t			fsbno;
	xfs_agblock_t			i;

	/*
	 * For each block in each extent, see if there's an incore buffer for
	 * exactly that block; if so, invalidate it.  The buffer cache only
	 * lets us look for one buffer at a time, so we have to look one block
	 * at a time.  Avoid invalidating AG headers and post-EOFS blocks
	 * because we never own those; and if we can't TRYLOCK the buffer we
	 * assume it's owned by someone else.
	 */
	for_each_xfs_repair_extent_safe(rex, n, exlist) {
		for (fsbno = rex->fsbno, i = rex->len; i > 0; fsbno++, i--) {
			/* Skip AG headers and post-EOFS blocks */
			if (!xfs_verify_fsbno(sc->mp, fsbno))
				continue;
			bp = xfs_buf_incore(sc->mp->m_ddev_targp,
					XFS_FSB_TO_DADDR(sc->mp, fsbno),
					XFS_FSB_TO_BB(sc->mp, 1), XBF_TRYLOCK);
			if (bp) {
				xfs_trans_bjoin(sc->tp, bp);
				xfs_trans_binval(sc->tp, bp);
			}
		}
	}

	return 0;
}

/* Ensure the freelist is the correct size. */
int
xfs_repair_fix_freelist(
	struct xfs_scrub_context	*sc,
	bool				can_shrink)
{
	struct xfs_alloc_arg		args = {0};

	args.mp = sc->mp;
	args.tp = sc->tp;
	args.agno = sc->sa.agno;
	args.alignment = 1;
	args.pag = sc->sa.pag;

	return xfs_alloc_fix_freelist(&args,
			can_shrink ? 0 : XFS_ALLOC_FLAG_NOSHRINK);
}

/*
 * Put a block back on the AGFL.
 */
STATIC int
xfs_repair_put_freelist(
	struct xfs_scrub_context	*sc,
	xfs_agblock_t			agbno)
{
	struct xfs_owner_info		oinfo;
	int				error;

	/* Make sure there's space on the freelist. */
	error = xfs_repair_fix_freelist(sc, true);
	if (error)
		return error;

	/*
	 * Since we're "freeing" a lost block onto the AGFL, we have to
	 * create an rmap for the block prior to merging it or else other
	 * parts will break.
	 */
	xfs_rmap_ag_owner(&oinfo, XFS_RMAP_OWN_AG);
	error = xfs_rmap_alloc(sc->tp, sc->sa.agf_bp, sc->sa.agno, agbno, 1,
			&oinfo);
	if (error)
		return error;

	/* Put the block on the AGFL. */
	error = xfs_alloc_put_freelist(sc->tp, sc->sa.agf_bp, sc->sa.agfl_bp,
			agbno, 0);
	if (error)
		return error;
	xfs_extent_busy_insert(sc->tp, sc->sa.agno, agbno, 1,
			XFS_EXTENT_BUSY_SKIP_DISCARD);

	return 0;
}

/* Dispose of a single metadata block. */
STATIC int
xfs_repair_dispose_btree_block(
	struct xfs_scrub_context	*sc,
	xfs_fsblock_t			fsbno,
	struct xfs_owner_info		*oinfo,
	enum xfs_ag_resv_type		resv)
{
	struct xfs_btree_cur		*cur;
	struct xfs_buf			*agf_bp = NULL;
	xfs_agnumber_t			agno;
	xfs_agblock_t			agbno;
	bool				has_other_rmap;
	int				error;

	agno = XFS_FSB_TO_AGNO(sc->mp, fsbno);
	agbno = XFS_FSB_TO_AGBNO(sc->mp, fsbno);

	/*
	 * If we are repairing per-inode metadata, we need to read in the AGF
	 * buffer.  Otherwise, we're repairing a per-AG structure, so reuse
	 * the AGF buffer that the setup functions already grabbed.
	 */
	if (sc->ip) {
		error = xfs_alloc_read_agf(sc->mp, sc->tp, agno, 0, &agf_bp);
		if (error)
			return error;
		if (!agf_bp)
			return -ENOMEM;
	} else {
		agf_bp = sc->sa.agf_bp;
	}
	cur = xfs_rmapbt_init_cursor(sc->mp, sc->tp, agf_bp, agno);

	/* Can we find any other rmappings? */
	error = xfs_rmap_has_other_keys(cur, agbno, 1, oinfo, &has_other_rmap);
	if (error)
		goto out_cur;
	xfs_btree_del_cursor(cur, XFS_BTREE_NOERROR);

	/*
	 * If there are other rmappings, this block is cross linked and must
	 * not be freed.  Remove the reverse mapping and move on.  Otherwise,
	 * we were the only owner of the block, so free the extent, which will
	 * also remove the rmap.
	 *
	 * XXX: XFS doesn't support detecting the case where a single block
	 * metadata structure is crosslinked with a multi-block structure
	 * because the buffer cache doesn't detect aliasing problems, so we
	 * can't fix 100% of crosslinking problems (yet).  The verifiers will
	 * blow on writeout, the filesystem will shut down, and the admin gets
	 * to run xfs_repair.
	 */
	if (has_other_rmap)
		error = xfs_rmap_free(sc->tp, agf_bp, agno, agbno, 1, oinfo);
	else if (resv == XFS_AG_RESV_AGFL)
		error = xfs_repair_put_freelist(sc, agbno);
	else
		error = xfs_free_extent(sc->tp, fsbno, 1, oinfo, resv);
	if (agf_bp != sc->sa.agf_bp)
		xfs_trans_brelse(sc->tp, agf_bp);
	if (error)
		return error;

	if (sc->ip)
		return xfs_trans_roll_inode(&sc->tp, sc->ip);
	return xfs_repair_roll_ag_trans(sc);

out_cur:
	xfs_btree_del_cursor(cur, XFS_BTREE_ERROR);
	if (agf_bp != sc->sa.agf_bp)
		xfs_trans_brelse(sc->tp, agf_bp);
	return error;
}

/* Dispose of btree blocks from an old per-AG btree. */
int
xfs_repair_reap_btree_extents(
	struct xfs_scrub_context	*sc,
	struct xfs_repair_extent_list	*exlist,
	struct xfs_owner_info		*oinfo,
	enum xfs_ag_resv_type		type)
{
	struct xfs_repair_extent	*rex;
	struct xfs_repair_extent	*n;
	int				error = 0;

	ASSERT(xfs_sb_version_hasrmapbt(&sc->mp->m_sb));

	/* Dispose of every block from the old btree. */
	for_each_xfs_repair_extent_safe(rex, n, exlist) {
		ASSERT(sc->ip != NULL ||
		       XFS_FSB_TO_AGNO(sc->mp, rex->fsbno) == sc->sa.agno);

		trace_xfs_repair_dispose_btree_extent(sc->mp,
				XFS_FSB_TO_AGNO(sc->mp, rex->fsbno),
				XFS_FSB_TO_AGBNO(sc->mp, rex->fsbno), rex->len);

		for (; rex->len > 0; rex->len--, rex->fsbno++) {
			error = xfs_repair_dispose_btree_block(sc, rex->fsbno,
					oinfo, type);
			if (error)
				goto out;
		}
		list_del(&rex->list);
		kmem_free(rex);
	}

out:
	xfs_repair_cancel_btree_extents(sc, exlist);
	return error;
}

/*
 * Finding per-AG Btree Roots for AGF/AGI Reconstruction
 *
 * If the AGF or AGI become slightly corrupted, it may be necessary to rebuild
 * the AG headers by using the rmap data to rummage through the AG looking for
 * btree roots.  This is not guaranteed to work if the AG is heavily damaged
 * or the rmap data are corrupt.
 *
 * Callers of xfs_repair_find_ag_btree_roots must lock the AGF and AGFL
 * buffers if the AGF is being rebuilt; or the AGF and AGI buffers if the
 * AGI is being rebuilt.  It must maintain these locks until it's safe for
 * other threads to change the btrees' shapes.  The caller provides
 * information about the btrees to look for by passing in an array of
 * xfs_repair_find_ag_btree with the (rmap owner, buf_ops, magic) fields set.
 * The (root, height) fields will be set on return if anything is found.  The
 * last element of the array should have a NULL buf_ops to mark the end of the
 * array.
 *
 * For every rmapbt record matching any of the rmap owners in btree_info,
 * read each block referenced by the rmap record.  If the block is a btree
 * block from this filesystem matching any of the magic numbers and has a
 * level higher than what we've already seen, remember the block and the
 * height of the tree required to have such a block.  When the call completes,
 * we return the highest block we've found for each btree description; those
 * should be the roots.
 */

struct xfs_repair_findroot {
	struct xfs_scrub_context	*sc;
	struct xfs_buf			*agfl_bp;
	struct xfs_agf			*agf;
	struct xfs_repair_find_ag_btree	*btree_info;
};

/* See if our block is in the AGFL. */
STATIC int
xfs_repair_findroot_agfl_walk(
	struct xfs_mount		*mp,
	xfs_agblock_t			bno,
	void				*priv)
{
	xfs_agblock_t			*agbno = priv;

	return (*agbno == bno) ? XFS_BTREE_QUERY_RANGE_ABORT : 0;
}

/* Does this block match the btree information passed in? */
STATIC int
xfs_repair_findroot_block(
	struct xfs_repair_findroot	*ri,
	struct xfs_repair_find_ag_btree	*fab,
	uint64_t			owner,
	xfs_agblock_t			agbno,
	bool				*found_it)
{
	struct xfs_mount		*mp = ri->sc->mp;
	struct xfs_buf			*bp;
	struct xfs_btree_block		*btblock;
	xfs_daddr_t			daddr;
	int				error;

	daddr = XFS_AGB_TO_DADDR(mp, ri->sc->sa.agno, agbno);

	/*
	 * Blocks in the AGFL have stale contents that might just happen to
	 * have a matching magic and uuid.  We don't want to pull these blocks
	 * in as part of a tree root, so we have to filter out the AGFL stuff
	 * here.  If the AGFL looks insane we'll just refuse to repair.
	 */
	if (owner == XFS_RMAP_OWN_AG) {
		error = xfs_agfl_walk(mp, ri->agf, ri->agfl_bp,
				xfs_repair_findroot_agfl_walk, &agbno);
		if (error == XFS_BTREE_QUERY_RANGE_ABORT)
			return 0;
		if (error)
			return error;
	}

	error = xfs_trans_read_buf(mp, ri->sc->tp, mp->m_ddev_targp, daddr,
			mp->m_bsize, 0, &bp, NULL);
	if (error)
		return error;

	/*
	 * Does this look like a block matching our fs and higher than any
	 * other block we've found so far?  If so, reattach buffer verifiers
	 * so the AIL won't complain if the buffer is also dirty.
	 */
	btblock = XFS_BUF_TO_BLOCK(bp);
	if (be32_to_cpu(btblock->bb_magic) != fab->magic)
		goto out;
	if (xfs_sb_version_hascrc(&mp->m_sb) &&
	    !uuid_equal(&btblock->bb_u.s.bb_uuid, &mp->m_sb.sb_meta_uuid))
		goto out;
	bp->b_ops = fab->buf_ops;

	/* Ignore this block if it's lower in the tree than we've seen. */
	if (fab->root != NULLAGBLOCK &&
	    xfs_btree_get_level(btblock) < fab->height)
		goto out;

	/* Make sure we pass the verifiers. */
	bp->b_ops->verify_read(bp);
	if (bp->b_error)
		goto out;
	fab->root = agbno;
	fab->height = xfs_btree_get_level(btblock) + 1;
	*found_it = true;

	trace_xfs_repair_findroot_block(mp, ri->sc->sa.agno, agbno,
			be32_to_cpu(btblock->bb_magic), fab->height - 1);
out:
	xfs_trans_brelse(ri->sc->tp, bp);
	return error;
}

/*
 * Do any of the blocks in this rmap record match one of the btrees we're
 * looking for?
 */
STATIC int
xfs_repair_findroot_rmap(
	struct xfs_btree_cur		*cur,
	struct xfs_rmap_irec		*rec,
	void				*priv)
{
	struct xfs_repair_findroot	*ri = priv;
	struct xfs_repair_find_ag_btree	*fab;
	xfs_agblock_t			b;
	bool				found_it;
	int				error = 0;

	/* Ignore anything that isn't AG metadata. */
	if (!XFS_RMAP_NON_INODE_OWNER(rec->rm_owner))
		return 0;

	/* Otherwise scan each block + btree type. */
	for (b = 0; b < rec->rm_blockcount; b++) {
		found_it = false;
		for (fab = ri->btree_info; fab->buf_ops; fab++) {
			if (rec->rm_owner != fab->rmap_owner)
				continue;
			error = xfs_repair_findroot_block(ri, fab,
					rec->rm_owner, rec->rm_startblock + b,
					&found_it);
			if (error)
				return error;
			if (found_it)
				break;
		}
	}

	return 0;
}

/* Find the roots of the per-AG btrees described in btree_info. */
int
xfs_repair_find_ag_btree_roots(
	struct xfs_scrub_context	*sc,
	struct xfs_buf			*agf_bp,
	struct xfs_repair_find_ag_btree	*btree_info,
	struct xfs_buf			*agfl_bp)
{
	struct xfs_mount		*mp = sc->mp;
	struct xfs_repair_findroot	ri;
	struct xfs_repair_find_ag_btree	*fab;
	struct xfs_btree_cur		*cur;
	int				error;

	ASSERT(xfs_buf_islocked(agf_bp));
	ASSERT(agfl_bp == NULL || xfs_buf_islocked(agfl_bp));

	ri.sc = sc;
	ri.btree_info = btree_info;
	ri.agf = XFS_BUF_TO_AGF(agf_bp);
	ri.agfl_bp = agfl_bp;
	for (fab = btree_info; fab->buf_ops; fab++) {
		ASSERT(agfl_bp || fab->rmap_owner != XFS_RMAP_OWN_AG);
		ASSERT(XFS_RMAP_NON_INODE_OWNER(fab->rmap_owner));
		fab->root = NULLAGBLOCK;
		fab->height = 0;
	}

	cur = xfs_rmapbt_init_cursor(mp, sc->tp, agf_bp, sc->sa.agno);
	error = xfs_rmap_query_all(cur, xfs_repair_findroot_rmap, &ri);
	xfs_btree_del_cursor(cur, error ? XFS_BTREE_ERROR : XFS_BTREE_NOERROR);

	return error;
}

/* Force a quotacheck the next time we mount. */
void
xfs_repair_force_quotacheck(
	struct xfs_scrub_context	*sc,
	uint				dqtype)
{
	uint				flag;

	flag = xfs_quota_chkd_flag(dqtype);
	if (!(flag & sc->mp->m_qflags))
		return;

	sc->mp->m_qflags &= ~flag;
	spin_lock(&sc->mp->m_sb_lock);
	sc->mp->m_sb.sb_qflags &= ~flag;
	spin_unlock(&sc->mp->m_sb_lock);
	xfs_log_sb(sc->tp);
}

/*
 * Attach dquots to this inode, or schedule quotacheck to fix them.
 *
 * This function ensures that the appropriate dquots are attached to an inode.
 * We cannot allow the dquot code to allocate an on-disk dquot block here
 * because we're already in transaction context with the inode locked.  The
 * on-disk dquot should already exist anyway.  If the quota code signals
 * corruption or missing quota information, schedule quotacheck, which will
 * repair corruptions in the quota metadata.
 */
int
xfs_repair_ino_dqattach(
	struct xfs_scrub_context	*sc)
{
	int				error;

	error = xfs_qm_dqattach_locked(sc->ip, false);
	switch (error) {
	case -EFSBADCRC:
	case -EFSCORRUPTED:
	case -ENOENT:
		xfs_err_ratelimited(sc->mp,
"inode %llu repair encountered quota error %d, quotacheck forced.",
				(unsigned long long)sc->ip->i_ino, error);
		if (XFS_IS_UQUOTA_ON(sc->mp) && !sc->ip->i_udquot)
			xfs_repair_force_quotacheck(sc, XFS_DQ_USER);
		if (XFS_IS_GQUOTA_ON(sc->mp) && !sc->ip->i_gdquot)
			xfs_repair_force_quotacheck(sc, XFS_DQ_GROUP);
		if (XFS_IS_PQUOTA_ON(sc->mp) && !sc->ip->i_pdquot)
			xfs_repair_force_quotacheck(sc, XFS_DQ_PROJ);
		/* fall through */
	case -ESRCH:
		error = 0;
		break;
	default:
		break;
	}

	return error;
}
