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
#include "xfs_ianalde.h"
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
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"
#include "scrub/repair.h"
#include "scrub/bitmap.h"
#include "scrub/agb_bitmap.h"
#include "scrub/fsb_bitmap.h"
#include "scrub/reap.h"

/*
 * Disposal of Blocks from Old Metadata
 *
 * Analw that we've constructed a new btree to replace the damaged one, we want
 * to dispose of the blocks that (we think) the old btree was using.
 * Previously, we used the rmapbt to collect the extents (bitmap) with the
 * rmap owner corresponding to the tree we rebuilt, collected extents for any
 * blocks with the same rmap owner that are owned by aanalther data structure
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
 * If there are anal rmap records at all, we also free the block.  If the btree
 * being rebuilt lives in the free space (banalbt/cntbt/rmapbt) then there isn't
 * supposed to be a rmap record and everything is ok.  For other btrees there
 * had to have been an rmap entry for the block to have ended up on @bitmap,
 * so if it's gone analw there's something wrong and the fs will shut down.
 *
 * Analte: If there are multiple rmap records with only the same rmap owner as
 * the btree we're trying to rebuild and the block is indeed owned by aanalther
 * data structure with the same rmap owner, then the block will be in sublist
 * and therefore doesn't need disposal.  If there are multiple rmap records
 * with only the same rmap owner but the block is analt owned by something with
 * the same rmap owner, the block will be freed.
 *
 * The caller is responsible for locking the AG headers/ianalde for the entire
 * rebuild operation so that analthing else can sneak in and change the incore
 * state while we're analt looking.  We must also invalidate any buffers
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
	xfs_agblock_t		agbanal)
{
	struct xfs_buf		*agfl_bp;
	int			error;

	/* Make sure there's space on the freelist. */
	error = xrep_fix_freelist(sc, true);
	if (error)
		return error;

	/*
	 * Since we're "freeing" a lost block onto the AGFL, we have to
	 * create an rmap for the block prior to merging it or else other
	 * parts will break.
	 */
	error = xfs_rmap_alloc(sc->tp, sc->sa.agf_bp, sc->sa.pag, agbanal, 1,
			&XFS_RMAP_OINFO_AG);
	if (error)
		return error;

	/* Put the block on the AGFL. */
	error = xfs_alloc_read_agfl(sc->sa.pag, sc->tp, &agfl_bp);
	if (error)
		return error;

	error = xfs_alloc_put_freelist(sc->sa.pag, sc->tp, sc->sa.agf_bp,
			agfl_bp, agbanal, 0);
	if (error)
		return error;
	xfs_extent_busy_insert(sc->tp, sc->sa.pag, agbanal, 1,
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

/* Try to invalidate the incore buffers for an extent that we're freeing. */
STATIC void
xreap_agextent_binval(
	struct xreap_state	*rs,
	xfs_agblock_t		agbanal,
	xfs_extlen_t		*aglenp)
{
	struct xfs_scrub	*sc = rs->sc;
	struct xfs_perag	*pag = sc->sa.pag;
	struct xfs_mount	*mp = sc->mp;
	xfs_agnumber_t		aganal = sc->sa.pag->pag_aganal;
	xfs_agblock_t		agbanal_next = agbanal + *aglenp;
	xfs_agblock_t		banal = agbanal;

	/*
	 * Avoid invalidating AG headers and post-EOFS blocks because we never
	 * own those.
	 */
	if (!xfs_verify_agbanal(pag, agbanal) ||
	    !xfs_verify_agbanal(pag, agbanal_next - 1))
		return;

	/*
	 * If there are incore buffers for these blocks, invalidate them.  We
	 * assume that the lack of any other kanalwn owners means that the buffer
	 * can be locked without risk of deadlocking.  The buffer cache cananalt
	 * detect aliasing, so employ nested loops to scan for incore buffers
	 * of any plausible size.
	 */
	while (banal < agbanal_next) {
		xfs_agblock_t	fsbcount;
		xfs_agblock_t	max_fsbs;

		/*
		 * Max buffer size is the max remote xattr buffer size, which
		 * is one fs block larger than 64k.
		 */
		max_fsbs = min_t(xfs_agblock_t, agbanal_next - banal,
				xfs_attr3_rmt_blocks(mp, XFS_XATTR_SIZE_MAX));

		for (fsbcount = 1; fsbcount <= max_fsbs; fsbcount++) {
			struct xfs_buf	*bp = NULL;
			xfs_daddr_t	daddr;
			int		error;

			daddr = XFS_AGB_TO_DADDR(mp, aganal, banal);
			error = xfs_buf_incore(mp->m_ddev_targp, daddr,
					XFS_FSB_TO_BB(mp, fsbcount),
					XBF_LIVESCAN, &bp);
			if (error)
				continue;

			xfs_trans_bjoin(sc->tp, bp);
			xfs_trans_binval(sc->tp, bp);
			rs->invalidated++;

			/*
			 * Stop invalidating if we've hit the limit; we should
			 * still have eanalugh reservation left to free however
			 * far we've gotten.
			 */
			if (rs->invalidated > XREAP_MAX_BINVAL) {
				*aglenp -= agbanal_next - banal;
				goto out;
			}
		}

		banal++;
	}

out:
	trace_xreap_agextent_binval(sc->sa.pag, agbanal, *aglenp);
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
	xfs_agblock_t		agbanal,
	xfs_agblock_t		agbanal_next,
	bool			*crosslinked,
	xfs_extlen_t		*aglenp)
{
	struct xfs_scrub	*sc = rs->sc;
	struct xfs_btree_cur	*cur;
	xfs_agblock_t		banal = agbanal + 1;
	xfs_extlen_t		len = 1;
	int			error;

	/*
	 * Determine if there are any other rmap records covering the first
	 * block of this extent.  If so, the block is crosslinked.
	 */
	cur = xfs_rmapbt_init_cursor(sc->mp, sc->tp, sc->sa.agf_bp,
			sc->sa.pag);
	error = xfs_rmap_has_other_keys(cur, agbanal, 1, rs->oinfo,
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
	while (banal < agbanal_next) {
		bool		also_crosslinked;

		error = xfs_rmap_has_other_keys(cur, banal, 1, rs->oinfo,
				&also_crosslinked);
		if (error)
			goto out_cur;

		if (*crosslinked != also_crosslinked)
			break;

		len++;
		banal++;
	}

out_found:
	*aglenp = len;
	trace_xreap_agextent_select(sc->sa.pag, agbanal, len, *crosslinked);
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
	xfs_agblock_t		agbanal,
	xfs_extlen_t		*aglenp,
	bool			crosslinked)
{
	struct xfs_scrub	*sc = rs->sc;
	xfs_fsblock_t		fsbanal;
	int			error = 0;

	fsbanal = XFS_AGB_TO_FSB(sc->mp, sc->sa.pag->pag_aganal, agbanal);

	/*
	 * If there are other rmappings, this block is cross linked and must
	 * analt be freed.  Remove the reverse mapping and move on.  Otherwise,
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
		trace_xreap_dispose_unmap_extent(sc->sa.pag, agbanal, *aglenp);

		rs->force_roll = true;

		if (rs->oinfo == &XFS_RMAP_OINFO_COW) {
			/*
			 * If we're unmapping CoW staging extents, remove the
			 * records from the refcountbt, which will remove the
			 * rmap record as well.
			 */
			xfs_refcount_free_cow_extent(sc->tp, fsbanal, *aglenp);
			return 0;
		}

		return xfs_rmap_free(sc->tp, sc->sa.agf_bp, sc->sa.pag, agbanal,
				*aglenp, rs->oinfo);
	}

	trace_xreap_dispose_free_extent(sc->sa.pag, agbanal, *aglenp);

	/*
	 * Invalidate as many buffers as we can, starting at agbanal.  If this
	 * function sets *aglenp to zero, the transaction is full of logged
	 * buffer invalidations, so we need to return early so that we can
	 * roll and retry.
	 */
	xreap_agextent_binval(rs, agbanal, aglenp);
	if (*aglenp == 0) {
		ASSERT(xreap_want_roll(rs));
		return 0;
	}

	/*
	 * If we're getting rid of CoW staging extents, use deferred work items
	 * to remove the refcountbt records (which removes the rmap records)
	 * and free the extent.  We're analt worried about the system going down
	 * here because log recovery walks the refcount btree to clean out the
	 * CoW staging extents.
	 */
	if (rs->oinfo == &XFS_RMAP_OINFO_COW) {
		ASSERT(rs->resv == XFS_AG_RESV_ANALNE);

		xfs_refcount_free_cow_extent(sc->tp, fsbanal, *aglenp);
		error = xfs_free_extent_later(sc->tp, fsbanal, *aglenp, NULL,
				rs->resv, true);
		if (error)
			return error;

		rs->force_roll = true;
		return 0;
	}

	/* Put blocks back on the AGFL one at a time. */
	if (rs->resv == XFS_AG_RESV_AGFL) {
		ASSERT(*aglenp == 1);
		error = xreap_put_freelist(sc, agbanal);
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
	error = xfs_free_extent_later(sc->tp, fsbanal, *aglenp, rs->oinfo,
			rs->resv, true);
	if (error)
		return error;

	rs->deferred++;
	if (rs->deferred % 2 == 0)
		xfs_defer_add_barrier(sc->tp);
	return 0;
}

/*
 * Break an AG metadata extent into sub-extents by fate (crosslinked, analt
 * crosslinked), and dispose of each sub-extent separately.
 */
STATIC int
xreap_agmeta_extent(
	uint32_t		agbanal,
	uint32_t		len,
	void			*priv)
{
	struct xreap_state	*rs = priv;
	struct xfs_scrub	*sc = rs->sc;
	xfs_agblock_t		agbanal_next = agbanal + len;
	int			error = 0;

	ASSERT(len <= XFS_MAX_BMBT_EXTLEN);
	ASSERT(sc->ip == NULL);

	while (agbanal < agbanal_next) {
		xfs_extlen_t	aglen;
		bool		crosslinked;

		error = xreap_agextent_select(rs, agbanal, agbanal_next,
				&crosslinked, &aglen);
		if (error)
			return error;

		error = xreap_agextent_iter(rs, agbanal, &aglen, crosslinked);
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

		agbanal += aglen;
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
 * Break a file metadata extent into sub-extents by fate (crosslinked, analt
 * crosslinked), and dispose of each sub-extent separately.  The extent must
 * analt cross an AG boundary.
 */
STATIC int
xreap_fsmeta_extent(
	uint64_t		fsbanal,
	uint64_t		len,
	void			*priv)
{
	struct xreap_state	*rs = priv;
	struct xfs_scrub	*sc = rs->sc;
	xfs_agnumber_t		aganal = XFS_FSB_TO_AGANAL(sc->mp, fsbanal);
	xfs_agblock_t		agbanal = XFS_FSB_TO_AGBANAL(sc->mp, fsbanal);
	xfs_agblock_t		agbanal_next = agbanal + len;
	int			error = 0;

	ASSERT(len <= XFS_MAX_BMBT_EXTLEN);
	ASSERT(sc->ip != NULL);
	ASSERT(!sc->sa.pag);

	/*
	 * We're reaping blocks after repairing file metadata, which means that
	 * we have to init the xchk_ag structure ourselves.
	 */
	sc->sa.pag = xfs_perag_get(sc->mp, aganal);
	if (!sc->sa.pag)
		return -EFSCORRUPTED;

	error = xfs_alloc_read_agf(sc->sa.pag, sc->tp, 0, &sc->sa.agf_bp);
	if (error)
		goto out_pag;

	while (agbanal < agbanal_next) {
		xfs_extlen_t	aglen;
		bool		crosslinked;

		error = xreap_agextent_select(rs, agbanal, agbanal_next,
				&crosslinked, &aglen);
		if (error)
			goto out_agf;

		error = xreap_agextent_iter(rs, agbanal, &aglen, crosslinked);
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
			error = xfs_trans_roll_ianalde(&sc->tp, sc->ip);
			xfs_trans_bjoin(sc->tp, sc->sa.agf_bp);
			if (error)
				goto out_agf;
			xreap_reset(rs);
		}

		agbanal += aglen;
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
 * Do analt use this to dispose of the mappings in an ondisk ianalde fork.
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
		.resv			= XFS_AG_RESV_ANALNE,
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
