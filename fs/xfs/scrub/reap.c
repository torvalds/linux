// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_btree.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_inode.h"
#include "xfs_alloc.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc.h"
#include "xfs_ialloc_btree.h"
#include "xfs_rmap.h"
#include "xfs_rmap_btree.h"
#include "xfs_refcount.h"
#include "xfs_refcount_btree.h"
#include "xfs_extent_busy.h"
#include "xfs_ag.h"
#include "xfs_ag_resv.h"
#include "xfs_quota.h"
#include "xfs_qm.h"
#include "xfs_bmap.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"
#include "xfs_attr.h"
#include "xfs_attr_remote.h"
#include "xfs_defer.h"
#include "xfs_metafile.h"
#include "xfs_rtgroup.h"
#include "xfs_rtrmap_btree.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"
#include "scrub/repair.h"
#include "scrub/bitmap.h"
#include "scrub/agb_bitmap.h"
#include "scrub/fsb_bitmap.h"
#include "scrub/rtb_bitmap.h"
#include "scrub/reap.h"

/*
 * Disposal of Blocks from Old Metadata
 *
 * Now that we've constructed a new btree to replace the damaged one, we want
 * to dispose of the blocks that (we think) the old btree was using.
 * Previously, we used the rmapbt to collect the extents (bitmap) with the
 * rmap owner corresponding to the tree we rebuilt, collected extents for any
 * blocks with the same rmap owner that are owned by another data structure
 * (sublist), and subtracted sublist from bitmap.  In theory the extents
 * remaining in bitmap are the old btree's blocks.
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
 * had to have been an rmap entry for the block to have ended up on @bitmap,
 * so if it's gone now there's something wrong and the fs will shut down.
 *
 * Note: If there are multiple rmap records with only the same rmap owner as
 * the btree we're trying to rebuild and the block is indeed owned by another
 * data structure with the same rmap owner, then the block will be in sublist
 * and therefore doesn't need disposal.  If there are multiple rmap records
 * with only the same rmap owner but the block is not owned by something with
 * the same rmap owner, the block will be freed.
 *
 * The caller is responsible for locking the AG headers/inode for the entire
 * rebuild operation so that nothing else can sneak in and change the incore
 * state while we're not looking.  We must also invalidate any buffers
 * associated with @bitmap.
 */

/* Information about reaping extents after a repair. */
struct xreap_state {
	struct xfs_scrub		*sc;

	/* Reverse mapping owner and metadata reservation type. */
	const struct xfs_owner_info	*oinfo;
	enum xfs_ag_resv_type		resv;

	/* If true, roll the transaction before reaping the next extent. */
	bool				force_roll;

	/* Number of deferred reaps attached to the current transaction. */
	unsigned int			deferred;

	/* Number of invalidated buffers logged to the current transaction. */
	unsigned int			invalidated;

	/* Number of deferred reaps queued during the whole reap sequence. */
	unsigned long long		total_deferred;
};

/* Put a block back on the AGFL. */
STATIC int
xreap_put_freelist(
	struct xfs_scrub	*sc,
	xfs_agblock_t		agbno)
{
	struct xfs_buf		*agfl_bp;
	int			error;

	/* Make sure there's space on the freelist. */
	error = xrep_fix_freelist(sc, 0);
	if (error)
		return error;

	/*
	 * Since we're "freeing" a lost block onto the AGFL, we have to
	 * create an rmap for the block prior to merging it or else other
	 * parts will break.
	 */
	error = xfs_rmap_alloc(sc->tp, sc->sa.agf_bp, sc->sa.pag, agbno, 1,
			&XFS_RMAP_OINFO_AG);
	if (error)
		return error;

	/* Put the block on the AGFL. */
	error = xfs_alloc_read_agfl(sc->sa.pag, sc->tp, &agfl_bp);
	if (error)
		return error;

	error = xfs_alloc_put_freelist(sc->sa.pag, sc->tp, sc->sa.agf_bp,
			agfl_bp, agbno, 0);
	if (error)
		return error;
	xfs_extent_busy_insert(sc->tp, pag_group(sc->sa.pag), agbno, 1,
			XFS_EXTENT_BUSY_SKIP_DISCARD);

	return 0;
}

/* Are there any uncommitted reap operations? */
static inline bool xreap_dirty(const struct xreap_state *rs)
{
	if (rs->force_roll)
		return true;
	if (rs->deferred)
		return true;
	if (rs->invalidated)
		return true;
	if (rs->total_deferred)
		return true;
	return false;
}

#define XREAP_MAX_BINVAL	(2048)

/*
 * Decide if we want to roll the transaction after reaping an extent.  We don't
 * want to overrun the transaction reservation, so we prohibit more than
 * 128 EFIs per transaction.  For the same reason, we limit the number
 * of buffer invalidations to 2048.
 */
static inline bool xreap_want_roll(const struct xreap_state *rs)
{
	if (rs->force_roll)
		return true;
	if (rs->deferred > XREP_MAX_ITRUNCATE_EFIS)
		return true;
	if (rs->invalidated > XREAP_MAX_BINVAL)
		return true;
	return false;
}

static inline void xreap_reset(struct xreap_state *rs)
{
	rs->total_deferred += rs->deferred;
	rs->deferred = 0;
	rs->invalidated = 0;
	rs->force_roll = false;
}

#define XREAP_MAX_DEFER_CHAIN		(2048)

/*
 * Decide if we want to finish the deferred ops that are attached to the scrub
 * transaction.  We don't want to queue huge chains of deferred ops because
 * that can consume a lot of log space and kernel memory.  Hence we trigger a
 * xfs_defer_finish if there are more than 2048 deferred reap operations or the
 * caller did some real work.
 */
static inline bool
xreap_want_defer_finish(const struct xreap_state *rs)
{
	if (rs->force_roll)
		return true;
	if (rs->total_deferred > XREAP_MAX_DEFER_CHAIN)
		return true;
	return false;
}

static inline void xreap_defer_finish_reset(struct xreap_state *rs)
{
	rs->total_deferred = 0;
	rs->deferred = 0;
	rs->invalidated = 0;
	rs->force_roll = false;
}

/*
 * Compute the maximum length of a buffer cache scan (in units of sectors),
 * given a quantity of fs blocks.
 */
xfs_daddr_t
xrep_bufscan_max_sectors(
	struct xfs_mount	*mp,
	xfs_extlen_t		fsblocks)
{
	int			max_fsbs;

	/* Remote xattr values are the largest buffers that we support. */
	max_fsbs = xfs_attr3_max_rmt_blocks(mp);

	return XFS_FSB_TO_BB(mp, min_t(xfs_extlen_t, fsblocks, max_fsbs));
}

/*
 * Return an incore buffer from a sector scan, or NULL if there are no buffers
 * left to return.
 */
struct xfs_buf *
xrep_bufscan_advance(
	struct xfs_mount	*mp,
	struct xrep_bufscan	*scan)
{
	scan->__sector_count += scan->daddr_step;
	while (scan->__sector_count <= scan->max_sectors) {
		struct xfs_buf	*bp = NULL;
		int		error;

		error = xfs_buf_incore(mp->m_ddev_targp, scan->daddr,
				scan->__sector_count, XBF_LIVESCAN, &bp);
		if (!error)
			return bp;

		scan->__sector_count += scan->daddr_step;
	}

	return NULL;
}

/* Try to invalidate the incore buffers for an extent that we're freeing. */
STATIC void
xreap_agextent_binval(
	struct xreap_state	*rs,
	xfs_agblock_t		agbno,
	xfs_extlen_t		*aglenp)
{
	struct xfs_scrub	*sc = rs->sc;
	struct xfs_perag	*pag = sc->sa.pag;
	struct xfs_mount	*mp = sc->mp;
	xfs_agblock_t		agbno_next = agbno + *aglenp;
	xfs_agblock_t		bno = agbno;

	/*
	 * Avoid invalidating AG headers and post-EOFS blocks because we never
	 * own those.
	 */
	if (!xfs_verify_agbno(pag, agbno) ||
	    !xfs_verify_agbno(pag, agbno_next - 1))
		return;

	/*
	 * If there are incore buffers for these blocks, invalidate them.  We
	 * assume that the lack of any other known owners means that the buffer
	 * can be locked without risk of deadlocking.  The buffer cache cannot
	 * detect aliasing, so employ nested loops to scan for incore buffers
	 * of any plausible size.
	 */
	while (bno < agbno_next) {
		struct xrep_bufscan	scan = {
			.daddr		= xfs_agbno_to_daddr(pag, bno),
			.max_sectors	= xrep_bufscan_max_sectors(mp,
							agbno_next - bno),
			.daddr_step	= XFS_FSB_TO_BB(mp, 1),
		};
		struct xfs_buf	*bp;

		while ((bp = xrep_bufscan_advance(mp, &scan)) != NULL) {
			xfs_trans_bjoin(sc->tp, bp);
			xfs_trans_binval(sc->tp, bp);
			rs->invalidated++;

			/*
			 * Stop invalidating if we've hit the limit; we should
			 * still have enough reservation left to free however
			 * far we've gotten.
			 */
			if (rs->invalidated > XREAP_MAX_BINVAL) {
				*aglenp -= agbno_next - bno;
				goto out;
			}
		}

		bno++;
	}

out:
	trace_xreap_agextent_binval(pag_group(sc->sa.pag), agbno, *aglenp);
}

/*
 * Figure out the longest run of blocks that we can dispose of with a single
 * call.  Cross-linked blocks should have their reverse mappings removed, but
 * single-owner extents can be freed.  AGFL blocks can only be put back one at
 * a time.
 */
STATIC int
xreap_agextent_select(
	struct xreap_state	*rs,
	xfs_agblock_t		agbno,
	xfs_agblock_t		agbno_next,
	bool			*crosslinked,
	xfs_extlen_t		*aglenp)
{
	struct xfs_scrub	*sc = rs->sc;
	struct xfs_btree_cur	*cur;
	xfs_agblock_t		bno = agbno + 1;
	xfs_extlen_t		len = 1;
	int			error;

	/*
	 * Determine if there are any other rmap records covering the first
	 * block of this extent.  If so, the block is crosslinked.
	 */
	cur = xfs_rmapbt_init_cursor(sc->mp, sc->tp, sc->sa.agf_bp,
			sc->sa.pag);
	error = xfs_rmap_has_other_keys(cur, agbno, 1, rs->oinfo,
			crosslinked);
	if (error)
		goto out_cur;

	/* AGFL blocks can only be deal with one at a time. */
	if (rs->resv == XFS_AG_RESV_AGFL)
		goto out_found;

	/*
	 * Figure out how many of the subsequent blocks have the same crosslink
	 * status.
	 */
	while (bno < agbno_next) {
		bool		also_crosslinked;

		error = xfs_rmap_has_other_keys(cur, bno, 1, rs->oinfo,
				&also_crosslinked);
		if (error)
			goto out_cur;

		if (*crosslinked != also_crosslinked)
			break;

		len++;
		bno++;
	}

out_found:
	*aglenp = len;
	trace_xreap_agextent_select(pag_group(sc->sa.pag), agbno, len,
			*crosslinked);
out_cur:
	xfs_btree_del_cursor(cur, error);
	return error;
}

/*
 * Dispose of as much of the beginning of this AG extent as possible.  The
 * number of blocks disposed of will be returned in @aglenp.
 */
STATIC int
xreap_agextent_iter(
	struct xreap_state	*rs,
	xfs_agblock_t		agbno,
	xfs_extlen_t		*aglenp,
	bool			crosslinked)
{
	struct xfs_scrub	*sc = rs->sc;
	xfs_fsblock_t		fsbno;
	int			error = 0;

	ASSERT(rs->resv != XFS_AG_RESV_METAFILE);

	fsbno = xfs_agbno_to_fsb(sc->sa.pag, agbno);

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
	if (crosslinked) {
		trace_xreap_dispose_unmap_extent(pag_group(sc->sa.pag), agbno,
				*aglenp);

		rs->force_roll = true;

		if (rs->oinfo == &XFS_RMAP_OINFO_COW) {
			/*
			 * If we're unmapping CoW staging extents, remove the
			 * records from the refcountbt, which will remove the
			 * rmap record as well.
			 */
			xfs_refcount_free_cow_extent(sc->tp, false, fsbno,
					*aglenp);
			return 0;
		}

		return xfs_rmap_free(sc->tp, sc->sa.agf_bp, sc->sa.pag, agbno,
				*aglenp, rs->oinfo);
	}

	trace_xreap_dispose_free_extent(pag_group(sc->sa.pag), agbno, *aglenp);

	/*
	 * Invalidate as many buffers as we can, starting at agbno.  If this
	 * function sets *aglenp to zero, the transaction is full of logged
	 * buffer invalidations, so we need to return early so that we can
	 * roll and retry.
	 */
	xreap_agextent_binval(rs, agbno, aglenp);
	if (*aglenp == 0) {
		ASSERT(xreap_want_roll(rs));
		return 0;
	}

	/*
	 * If we're getting rid of CoW staging extents, use deferred work items
	 * to remove the refcountbt records (which removes the rmap records)
	 * and free the extent.  We're not worried about the system going down
	 * here because log recovery walks the refcount btree to clean out the
	 * CoW staging extents.
	 */
	if (rs->oinfo == &XFS_RMAP_OINFO_COW) {
		ASSERT(rs->resv == XFS_AG_RESV_NONE);

		xfs_refcount_free_cow_extent(sc->tp, false, fsbno, *aglenp);
		error = xfs_free_extent_later(sc->tp, fsbno, *aglenp, NULL,
				rs->resv, XFS_FREE_EXTENT_SKIP_DISCARD);
		if (error)
			return error;

		rs->force_roll = true;
		return 0;
	}

	/* Put blocks back on the AGFL one at a time. */
	if (rs->resv == XFS_AG_RESV_AGFL) {
		ASSERT(*aglenp == 1);
		error = xreap_put_freelist(sc, agbno);
		if (error)
			return error;

		rs->force_roll = true;
		return 0;
	}

	/*
	 * Use deferred frees to get rid of the old btree blocks to try to
	 * minimize the window in which we could crash and lose the old blocks.
	 * Add a defer ops barrier every other extent to avoid stressing the
	 * system with large EFIs.
	 */
	error = xfs_free_extent_later(sc->tp, fsbno, *aglenp, rs->oinfo,
			rs->resv, XFS_FREE_EXTENT_SKIP_DISCARD);
	if (error)
		return error;

	rs->deferred++;
	if (rs->deferred % 2 == 0)
		xfs_defer_add_barrier(sc->tp);
	return 0;
}

/*
 * Break an AG metadata extent into sub-extents by fate (crosslinked, not
 * crosslinked), and dispose of each sub-extent separately.
 */
STATIC int
xreap_agmeta_extent(
	uint32_t		agbno,
	uint32_t		len,
	void			*priv)
{
	struct xreap_state	*rs = priv;
	struct xfs_scrub	*sc = rs->sc;
	xfs_agblock_t		agbno_next = agbno + len;
	int			error = 0;

	ASSERT(len <= XFS_MAX_BMBT_EXTLEN);
	ASSERT(sc->ip == NULL);

	while (agbno < agbno_next) {
		xfs_extlen_t	aglen;
		bool		crosslinked;

		error = xreap_agextent_select(rs, agbno, agbno_next,
				&crosslinked, &aglen);
		if (error)
			return error;

		error = xreap_agextent_iter(rs, agbno, &aglen, crosslinked);
		if (error)
			return error;

		if (xreap_want_defer_finish(rs)) {
			error = xrep_defer_finish(sc);
			if (error)
				return error;
			xreap_defer_finish_reset(rs);
		} else if (xreap_want_roll(rs)) {
			error = xrep_roll_ag_trans(sc);
			if (error)
				return error;
			xreap_reset(rs);
		}

		agbno += aglen;
	}

	return 0;
}

/* Dispose of every block of every AG metadata extent in the bitmap. */
int
xrep_reap_agblocks(
	struct xfs_scrub		*sc,
	struct xagb_bitmap		*bitmap,
	const struct xfs_owner_info	*oinfo,
	enum xfs_ag_resv_type		type)
{
	struct xreap_state		rs = {
		.sc			= sc,
		.oinfo			= oinfo,
		.resv			= type,
	};
	int				error;

	ASSERT(xfs_has_rmapbt(sc->mp));
	ASSERT(sc->ip == NULL);

	error = xagb_bitmap_walk(bitmap, xreap_agmeta_extent, &rs);
	if (error)
		return error;

	if (xreap_dirty(&rs))
		return xrep_defer_finish(sc);

	return 0;
}

/*
 * Break a file metadata extent into sub-extents by fate (crosslinked, not
 * crosslinked), and dispose of each sub-extent separately.  The extent must
 * not cross an AG boundary.
 */
STATIC int
xreap_fsmeta_extent(
	uint64_t		fsbno,
	uint64_t		len,
	void			*priv)
{
	struct xreap_state	*rs = priv;
	struct xfs_scrub	*sc = rs->sc;
	xfs_agnumber_t		agno = XFS_FSB_TO_AGNO(sc->mp, fsbno);
	xfs_agblock_t		agbno = XFS_FSB_TO_AGBNO(sc->mp, fsbno);
	xfs_agblock_t		agbno_next = agbno + len;
	int			error = 0;

	ASSERT(len <= XFS_MAX_BMBT_EXTLEN);
	ASSERT(sc->ip != NULL);
	ASSERT(!sc->sa.pag);

	/*
	 * We're reaping blocks after repairing file metadata, which means that
	 * we have to init the xchk_ag structure ourselves.
	 */
	sc->sa.pag = xfs_perag_get(sc->mp, agno);
	if (!sc->sa.pag)
		return -EFSCORRUPTED;

	error = xfs_alloc_read_agf(sc->sa.pag, sc->tp, 0, &sc->sa.agf_bp);
	if (error)
		goto out_pag;

	while (agbno < agbno_next) {
		xfs_extlen_t	aglen;
		bool		crosslinked;

		error = xreap_agextent_select(rs, agbno, agbno_next,
				&crosslinked, &aglen);
		if (error)
			goto out_agf;

		error = xreap_agextent_iter(rs, agbno, &aglen, crosslinked);
		if (error)
			goto out_agf;

		if (xreap_want_defer_finish(rs)) {
			/*
			 * Holds the AGF buffer across the deferred chain
			 * processing.
			 */
			error = xrep_defer_finish(sc);
			if (error)
				goto out_agf;
			xreap_defer_finish_reset(rs);
		} else if (xreap_want_roll(rs)) {
			/*
			 * Hold the AGF buffer across the transaction roll so
			 * that we don't have to reattach it to the scrub
			 * context.
			 */
			xfs_trans_bhold(sc->tp, sc->sa.agf_bp);
			error = xfs_trans_roll_inode(&sc->tp, sc->ip);
			xfs_trans_bjoin(sc->tp, sc->sa.agf_bp);
			if (error)
				goto out_agf;
			xreap_reset(rs);
		}

		agbno += aglen;
	}

out_agf:
	xfs_trans_brelse(sc->tp, sc->sa.agf_bp);
	sc->sa.agf_bp = NULL;
out_pag:
	xfs_perag_put(sc->sa.pag);
	sc->sa.pag = NULL;
	return error;
}

/*
 * Dispose of every block of every fs metadata extent in the bitmap.
 * Do not use this to dispose of the mappings in an ondisk inode fork.
 */
int
xrep_reap_fsblocks(
	struct xfs_scrub		*sc,
	struct xfsb_bitmap		*bitmap,
	const struct xfs_owner_info	*oinfo)
{
	struct xreap_state		rs = {
		.sc			= sc,
		.oinfo			= oinfo,
		.resv			= XFS_AG_RESV_NONE,
	};
	int				error;

	ASSERT(xfs_has_rmapbt(sc->mp));
	ASSERT(sc->ip != NULL);

	error = xfsb_bitmap_walk(bitmap, xreap_fsmeta_extent, &rs);
	if (error)
		return error;

	if (xreap_dirty(&rs))
		return xrep_defer_finish(sc);

	return 0;
}

#ifdef CONFIG_XFS_RT
/*
 * Figure out the longest run of blocks that we can dispose of with a single
 * call.  Cross-linked blocks should have their reverse mappings removed, but
 * single-owner extents can be freed.  Units are rt blocks, not rt extents.
 */
STATIC int
xreap_rgextent_select(
	struct xreap_state	*rs,
	xfs_rgblock_t		rgbno,
	xfs_rgblock_t		rgbno_next,
	bool			*crosslinked,
	xfs_extlen_t		*rglenp)
{
	struct xfs_scrub	*sc = rs->sc;
	struct xfs_btree_cur	*cur;
	xfs_rgblock_t		bno = rgbno + 1;
	xfs_extlen_t		len = 1;
	int			error;

	/*
	 * Determine if there are any other rmap records covering the first
	 * block of this extent.  If so, the block is crosslinked.
	 */
	cur = xfs_rtrmapbt_init_cursor(sc->tp, sc->sr.rtg);
	error = xfs_rmap_has_other_keys(cur, rgbno, 1, rs->oinfo,
			crosslinked);
	if (error)
		goto out_cur;

	/*
	 * Figure out how many of the subsequent blocks have the same crosslink
	 * status.
	 */
	while (bno < rgbno_next) {
		bool		also_crosslinked;

		error = xfs_rmap_has_other_keys(cur, bno, 1, rs->oinfo,
				&also_crosslinked);
		if (error)
			goto out_cur;

		if (*crosslinked != also_crosslinked)
			break;

		len++;
		bno++;
	}

	*rglenp = len;
	trace_xreap_agextent_select(rtg_group(sc->sr.rtg), rgbno, len,
			*crosslinked);
out_cur:
	xfs_btree_del_cursor(cur, error);
	return error;
}

/*
 * Dispose of as much of the beginning of this rtgroup extent as possible.
 * The number of blocks disposed of will be returned in @rglenp.
 */
STATIC int
xreap_rgextent_iter(
	struct xreap_state	*rs,
	xfs_rgblock_t		rgbno,
	xfs_extlen_t		*rglenp,
	bool			crosslinked)
{
	struct xfs_scrub	*sc = rs->sc;
	xfs_rtblock_t		rtbno;
	int			error;

	/*
	 * The only caller so far is CoW fork repair, so we only know how to
	 * unlink or free CoW staging extents.  Here we don't have to worry
	 * about invalidating buffers!
	 */
	if (rs->oinfo != &XFS_RMAP_OINFO_COW) {
		ASSERT(rs->oinfo == &XFS_RMAP_OINFO_COW);
		return -EFSCORRUPTED;
	}
	ASSERT(rs->resv == XFS_AG_RESV_NONE);

	rtbno = xfs_rgbno_to_rtb(sc->sr.rtg, rgbno);

	/*
	 * If there are other rmappings, this block is cross linked and must
	 * not be freed.  Remove the forward and reverse mapping and move on.
	 */
	if (crosslinked) {
		trace_xreap_dispose_unmap_extent(rtg_group(sc->sr.rtg), rgbno,
				*rglenp);

		xfs_refcount_free_cow_extent(sc->tp, true, rtbno, *rglenp);
		rs->deferred++;
		return 0;
	}

	trace_xreap_dispose_free_extent(rtg_group(sc->sr.rtg), rgbno, *rglenp);

	/*
	 * The CoW staging extent is not crosslinked.  Use deferred work items
	 * to remove the refcountbt records (which removes the rmap records)
	 * and free the extent.  We're not worried about the system going down
	 * here because log recovery walks the refcount btree to clean out the
	 * CoW staging extents.
	 */
	xfs_refcount_free_cow_extent(sc->tp, true, rtbno, *rglenp);
	error = xfs_free_extent_later(sc->tp, rtbno, *rglenp, NULL,
			rs->resv,
			XFS_FREE_EXTENT_REALTIME |
			XFS_FREE_EXTENT_SKIP_DISCARD);
	if (error)
		return error;

	rs->deferred++;
	return 0;
}

#define XREAP_RTGLOCK_ALL	(XFS_RTGLOCK_BITMAP | \
				 XFS_RTGLOCK_RMAP | \
				 XFS_RTGLOCK_REFCOUNT)

/*
 * Break a rt file metadata extent into sub-extents by fate (crosslinked, not
 * crosslinked), and dispose of each sub-extent separately.  The extent must
 * be aligned to a realtime extent.
 */
STATIC int
xreap_rtmeta_extent(
	uint64_t		rtbno,
	uint64_t		len,
	void			*priv)
{
	struct xreap_state	*rs = priv;
	struct xfs_scrub	*sc = rs->sc;
	xfs_rgblock_t		rgbno = xfs_rtb_to_rgbno(sc->mp, rtbno);
	xfs_rgblock_t		rgbno_next = rgbno + len;
	int			error = 0;

	ASSERT(sc->ip != NULL);
	ASSERT(!sc->sr.rtg);

	/*
	 * We're reaping blocks after repairing file metadata, which means that
	 * we have to init the xchk_ag structure ourselves.
	 */
	sc->sr.rtg = xfs_rtgroup_get(sc->mp, xfs_rtb_to_rgno(sc->mp, rtbno));
	if (!sc->sr.rtg)
		return -EFSCORRUPTED;

	xfs_rtgroup_lock(sc->sr.rtg, XREAP_RTGLOCK_ALL);

	while (rgbno < rgbno_next) {
		xfs_extlen_t	rglen;
		bool		crosslinked;

		error = xreap_rgextent_select(rs, rgbno, rgbno_next,
				&crosslinked, &rglen);
		if (error)
			goto out_unlock;

		error = xreap_rgextent_iter(rs, rgbno, &rglen, crosslinked);
		if (error)
			goto out_unlock;

		if (xreap_want_defer_finish(rs)) {
			error = xfs_defer_finish(&sc->tp);
			if (error)
				goto out_unlock;
			xreap_defer_finish_reset(rs);
		} else if (xreap_want_roll(rs)) {
			error = xfs_trans_roll_inode(&sc->tp, sc->ip);
			if (error)
				goto out_unlock;
			xreap_reset(rs);
		}

		rgbno += rglen;
	}

out_unlock:
	xfs_rtgroup_unlock(sc->sr.rtg, XREAP_RTGLOCK_ALL);
	xfs_rtgroup_put(sc->sr.rtg);
	sc->sr.rtg = NULL;
	return error;
}

/*
 * Dispose of every block of every rt metadata extent in the bitmap.
 * Do not use this to dispose of the mappings in an ondisk inode fork.
 */
int
xrep_reap_rtblocks(
	struct xfs_scrub		*sc,
	struct xrtb_bitmap		*bitmap,
	const struct xfs_owner_info	*oinfo)
{
	struct xreap_state		rs = {
		.sc			= sc,
		.oinfo			= oinfo,
		.resv			= XFS_AG_RESV_NONE,
	};
	int				error;

	ASSERT(xfs_has_rmapbt(sc->mp));
	ASSERT(sc->ip != NULL);

	error = xrtb_bitmap_walk(bitmap, xreap_rtmeta_extent, &rs);
	if (error)
		return error;

	if (xreap_dirty(&rs))
		return xrep_defer_finish(sc);

	return 0;
}
#endif /* CONFIG_XFS_RT */

/*
 * Dispose of every block of an old metadata btree that used to be rooted in a
 * metadata directory file.
 */
int
xrep_reap_metadir_fsblocks(
	struct xfs_scrub		*sc,
	struct xfsb_bitmap		*bitmap)
{
	/*
	 * Reap old metadir btree blocks with XFS_AG_RESV_NONE because the old
	 * blocks are no longer mapped by the inode, and inode metadata space
	 * reservations can only account freed space to the i_nblocks.
	 */
	struct xfs_owner_info		oinfo;
	struct xreap_state		rs = {
		.sc			= sc,
		.oinfo			= &oinfo,
		.resv			= XFS_AG_RESV_NONE,
	};
	int				error;

	ASSERT(xfs_has_rmapbt(sc->mp));
	ASSERT(sc->ip != NULL);
	ASSERT(xfs_is_metadir_inode(sc->ip));

	xfs_rmap_ino_bmbt_owner(&oinfo, sc->ip->i_ino, XFS_DATA_FORK);

	error = xfsb_bitmap_walk(bitmap, xreap_fsmeta_extent, &rs);
	if (error)
		return error;

	if (xreap_dirty(&rs)) {
		error = xrep_defer_finish(sc);
		if (error)
			return error;
	}

	return xrep_reset_metafile_resv(sc);
}

/*
 * Metadata files are not supposed to share blocks with anything else.
 * If blocks are shared, we remove the reverse mapping (thus reducing the
 * crosslink factor); if blocks are not shared, we also need to free them.
 *
 * This first step determines the longest subset of the passed-in imap
 * (starting at its beginning) that is either crosslinked or not crosslinked.
 * The blockcount will be adjust down as needed.
 */
STATIC int
xreap_bmapi_select(
	struct xfs_scrub	*sc,
	struct xfs_inode	*ip,
	int			whichfork,
	struct xfs_bmbt_irec	*imap,
	bool			*crosslinked)
{
	struct xfs_owner_info	oinfo;
	struct xfs_btree_cur	*cur;
	xfs_filblks_t		len = 1;
	xfs_agblock_t		bno;
	xfs_agblock_t		agbno;
	xfs_agblock_t		agbno_next;
	int			error;

	agbno = XFS_FSB_TO_AGBNO(sc->mp, imap->br_startblock);
	agbno_next = agbno + imap->br_blockcount;

	cur = xfs_rmapbt_init_cursor(sc->mp, sc->tp, sc->sa.agf_bp,
			sc->sa.pag);

	xfs_rmap_ino_owner(&oinfo, ip->i_ino, whichfork, imap->br_startoff);
	error = xfs_rmap_has_other_keys(cur, agbno, 1, &oinfo, crosslinked);
	if (error)
		goto out_cur;

	bno = agbno + 1;
	while (bno < agbno_next) {
		bool		also_crosslinked;

		oinfo.oi_offset++;
		error = xfs_rmap_has_other_keys(cur, bno, 1, &oinfo,
				&also_crosslinked);
		if (error)
			goto out_cur;

		if (also_crosslinked != *crosslinked)
			break;

		len++;
		bno++;
	}

	imap->br_blockcount = len;
	trace_xreap_bmapi_select(pag_group(sc->sa.pag), agbno, len,
			*crosslinked);
out_cur:
	xfs_btree_del_cursor(cur, error);
	return error;
}

/*
 * Decide if this buffer can be joined to a transaction.  This is true for most
 * buffers, but there are two cases that we want to catch: large remote xattr
 * value buffers are not logged and can overflow the buffer log item dirty
 * bitmap size; and oversized cached buffers if things have really gone
 * haywire.
 */
static inline bool
xreap_buf_loggable(
	const struct xfs_buf	*bp)
{
	int			i;

	for (i = 0; i < bp->b_map_count; i++) {
		int		chunks;
		int		map_size;

		chunks = DIV_ROUND_UP(BBTOB(bp->b_maps[i].bm_len),
				XFS_BLF_CHUNK);
		map_size = DIV_ROUND_UP(chunks, NBWORD);
		if (map_size > XFS_BLF_DATAMAP_SIZE)
			return false;
	}

	return true;
}

/*
 * Invalidate any buffers for this file mapping.  The @imap blockcount may be
 * adjusted downward if we need to roll the transaction.
 */
STATIC int
xreap_bmapi_binval(
	struct xfs_scrub	*sc,
	struct xfs_inode	*ip,
	int			whichfork,
	struct xfs_bmbt_irec	*imap)
{
	struct xfs_mount	*mp = sc->mp;
	struct xfs_perag	*pag = sc->sa.pag;
	int			bmap_flags = xfs_bmapi_aflag(whichfork);
	xfs_fileoff_t		off;
	xfs_fileoff_t		max_off;
	xfs_extlen_t		scan_blocks;
	xfs_agblock_t		bno;
	xfs_agblock_t		agbno;
	xfs_agblock_t		agbno_next;
	unsigned int		invalidated = 0;
	int			error;

	/*
	 * Avoid invalidating AG headers and post-EOFS blocks because we never
	 * own those.
	 */
	agbno = bno = XFS_FSB_TO_AGBNO(sc->mp, imap->br_startblock);
	agbno_next = agbno + imap->br_blockcount;
	if (!xfs_verify_agbno(pag, agbno) ||
	    !xfs_verify_agbno(pag, agbno_next - 1))
		return 0;

	/*
	 * Buffers for file blocks can span multiple contiguous mappings.  This
	 * means that for each block in the mapping, there could exist an
	 * xfs_buf indexed by that block with any length up to the maximum
	 * buffer size (remote xattr values) or to the next hole in the fork.
	 * To set up our binval scan, first we need to figure out the location
	 * of the next hole.
	 */
	off = imap->br_startoff + imap->br_blockcount;
	max_off = off + xfs_attr3_max_rmt_blocks(mp);
	while (off < max_off) {
		struct xfs_bmbt_irec	hmap;
		int			nhmaps = 1;

		error = xfs_bmapi_read(ip, off, max_off - off, &hmap,
				&nhmaps, bmap_flags);
		if (error)
			return error;
		if (nhmaps != 1 || hmap.br_startblock == DELAYSTARTBLOCK) {
			ASSERT(0);
			return -EFSCORRUPTED;
		}

		if (!xfs_bmap_is_real_extent(&hmap))
			break;

		off = hmap.br_startoff + hmap.br_blockcount;
	}
	scan_blocks = off - imap->br_startoff;

	trace_xreap_bmapi_binval_scan(sc, imap, scan_blocks);

	/*
	 * If there are incore buffers for these blocks, invalidate them.  If
	 * we can't (try)lock the buffer we assume it's owned by someone else
	 * and leave it alone.  The buffer cache cannot detect aliasing, so
	 * employ nested loops to detect incore buffers of any plausible size.
	 */
	while (bno < agbno_next) {
		struct xrep_bufscan	scan = {
			.daddr		= xfs_agbno_to_daddr(pag, bno),
			.max_sectors	= xrep_bufscan_max_sectors(mp,
								scan_blocks),
			.daddr_step	= XFS_FSB_TO_BB(mp, 1),
		};
		struct xfs_buf		*bp;

		while ((bp = xrep_bufscan_advance(mp, &scan)) != NULL) {
			if (xreap_buf_loggable(bp)) {
				xfs_trans_bjoin(sc->tp, bp);
				xfs_trans_binval(sc->tp, bp);
			} else {
				xfs_buf_stale(bp);
				xfs_buf_relse(bp);
			}
			invalidated++;

			/*
			 * Stop invalidating if we've hit the limit; we should
			 * still have enough reservation left to free however
			 * much of the mapping we've seen so far.
			 */
			if (invalidated > XREAP_MAX_BINVAL) {
				imap->br_blockcount = agbno_next - bno;
				goto out;
			}
		}

		bno++;
		scan_blocks--;
	}

out:
	trace_xreap_bmapi_binval(pag_group(sc->sa.pag), agbno,
			imap->br_blockcount);
	return 0;
}

/*
 * Dispose of as much of the beginning of this file fork mapping as possible.
 * The number of blocks disposed of is returned in @imap->br_blockcount.
 */
STATIC int
xrep_reap_bmapi_iter(
	struct xfs_scrub		*sc,
	struct xfs_inode		*ip,
	int				whichfork,
	struct xfs_bmbt_irec		*imap,
	bool				crosslinked)
{
	int				error;

	if (crosslinked) {
		/*
		 * If there are other rmappings, this block is cross linked and
		 * must not be freed.  Remove the reverse mapping, leave the
		 * buffer cache in its possibly confused state, and move on.
		 * We don't want to risk discarding valid data buffers from
		 * anybody else who thinks they own the block, even though that
		 * runs the risk of stale buffer warnings in the future.
		 */
		trace_xreap_dispose_unmap_extent(pag_group(sc->sa.pag),
				XFS_FSB_TO_AGBNO(sc->mp, imap->br_startblock),
				imap->br_blockcount);

		/*
		 * Schedule removal of the mapping from the fork.  We use
		 * deferred log intents in this function to control the exact
		 * sequence of metadata updates.
		 */
		xfs_bmap_unmap_extent(sc->tp, ip, whichfork, imap);
		xfs_trans_mod_dquot_byino(sc->tp, ip, XFS_TRANS_DQ_BCOUNT,
				-(int64_t)imap->br_blockcount);
		xfs_rmap_unmap_extent(sc->tp, ip, whichfork, imap);
		return 0;
	}

	/*
	 * If the block is not crosslinked, we can invalidate all the incore
	 * buffers for the extent, and then free the extent.  This is a bit of
	 * a mess since we don't detect discontiguous buffers that are indexed
	 * by a block starting before the first block of the extent but overlap
	 * anyway.
	 */
	trace_xreap_dispose_free_extent(pag_group(sc->sa.pag),
			XFS_FSB_TO_AGBNO(sc->mp, imap->br_startblock),
			imap->br_blockcount);

	/*
	 * Invalidate as many buffers as we can, starting at the beginning of
	 * this mapping.  If this function sets blockcount to zero, the
	 * transaction is full of logged buffer invalidations, so we need to
	 * return early so that we can roll and retry.
	 */
	error = xreap_bmapi_binval(sc, ip, whichfork, imap);
	if (error || imap->br_blockcount == 0)
		return error;

	/*
	 * Schedule removal of the mapping from the fork.  We use deferred log
	 * intents in this function to control the exact sequence of metadata
	 * updates.
	 */
	xfs_bmap_unmap_extent(sc->tp, ip, whichfork, imap);
	xfs_trans_mod_dquot_byino(sc->tp, ip, XFS_TRANS_DQ_BCOUNT,
			-(int64_t)imap->br_blockcount);
	return xfs_free_extent_later(sc->tp, imap->br_startblock,
			imap->br_blockcount, NULL, XFS_AG_RESV_NONE,
			XFS_FREE_EXTENT_SKIP_DISCARD);
}

/*
 * Dispose of as much of this file extent as we can.  Upon successful return,
 * the imap will reflect the mapping that was removed from the fork.
 */
STATIC int
xreap_ifork_extent(
	struct xfs_scrub		*sc,
	struct xfs_inode		*ip,
	int				whichfork,
	struct xfs_bmbt_irec		*imap)
{
	xfs_agnumber_t			agno;
	bool				crosslinked;
	int				error;

	ASSERT(sc->sa.pag == NULL);

	trace_xreap_ifork_extent(sc, ip, whichfork, imap);

	agno = XFS_FSB_TO_AGNO(sc->mp, imap->br_startblock);
	sc->sa.pag = xfs_perag_get(sc->mp, agno);
	if (!sc->sa.pag)
		return -EFSCORRUPTED;

	error = xfs_alloc_read_agf(sc->sa.pag, sc->tp, 0, &sc->sa.agf_bp);
	if (error)
		goto out_pag;

	/*
	 * Decide the fate of the blocks at the beginning of the mapping, then
	 * update the mapping to use it with the unmap calls.
	 */
	error = xreap_bmapi_select(sc, ip, whichfork, imap, &crosslinked);
	if (error)
		goto out_agf;

	error = xrep_reap_bmapi_iter(sc, ip, whichfork, imap, crosslinked);
	if (error)
		goto out_agf;

out_agf:
	xfs_trans_brelse(sc->tp, sc->sa.agf_bp);
	sc->sa.agf_bp = NULL;
out_pag:
	xfs_perag_put(sc->sa.pag);
	sc->sa.pag = NULL;
	return error;
}

/*
 * Dispose of each block mapped to the given fork of the given file.  Callers
 * must hold ILOCK_EXCL, and ip can only be sc->ip or sc->tempip.  The fork
 * must not have any delalloc reservations.
 */
int
xrep_reap_ifork(
	struct xfs_scrub	*sc,
	struct xfs_inode	*ip,
	int			whichfork)
{
	xfs_fileoff_t		off = 0;
	int			bmap_flags = xfs_bmapi_aflag(whichfork);
	int			error;

	ASSERT(xfs_has_rmapbt(sc->mp));
	ASSERT(ip == sc->ip || ip == sc->tempip);
	ASSERT(whichfork == XFS_ATTR_FORK || !XFS_IS_REALTIME_INODE(ip));

	while (off < XFS_MAX_FILEOFF) {
		struct xfs_bmbt_irec	imap;
		int			nimaps = 1;

		/* Read the next extent, skip past holes and delalloc. */
		error = xfs_bmapi_read(ip, off, XFS_MAX_FILEOFF - off, &imap,
				&nimaps, bmap_flags);
		if (error)
			return error;
		if (nimaps != 1 || imap.br_startblock == DELAYSTARTBLOCK) {
			ASSERT(0);
			return -EFSCORRUPTED;
		}

		/*
		 * If this is a real space mapping, reap as much of it as we
		 * can in a single transaction.
		 */
		if (xfs_bmap_is_real_extent(&imap)) {
			error = xreap_ifork_extent(sc, ip, whichfork, &imap);
			if (error)
				return error;

			error = xfs_defer_finish(&sc->tp);
			if (error)
				return error;
		}

		off = imap.br_startoff + imap.br_blockcount;
	}

	return 0;
}
