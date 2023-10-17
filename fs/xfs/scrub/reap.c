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
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"
#include "scrub/repair.h"
#include "scrub/bitmap.h"
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
 * The caller is responsible for locking the AG headers for the entire rebuild
 * operation so that nothing else can sneak in and change the AG state while
 * we're not looking.  We must also invalidate any buffers associated with
 * @bitmap.
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
	error = xrep_fix_freelist(sc, true);
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
	xfs_extent_busy_insert(sc->tp, sc->sa.pag, agbno, 1,
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
	xfs_agblock_t		agbno,
	xfs_extlen_t		*aglenp)
{
	struct xfs_scrub	*sc = rs->sc;
	struct xfs_perag	*pag = sc->sa.pag;
	struct xfs_mount	*mp = sc->mp;
	xfs_agnumber_t		agno = sc->sa.pag->pag_agno;
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
		xfs_agblock_t	fsbcount;
		xfs_agblock_t	max_fsbs;

		/*
		 * Max buffer size is the max remote xattr buffer size, which
		 * is one fs block larger than 64k.
		 */
		max_fsbs = min_t(xfs_agblock_t, agbno_next - bno,
				xfs_attr3_rmt_blocks(mp, XFS_XATTR_SIZE_MAX));

		for (fsbcount = 1; fsbcount < max_fsbs; fsbcount++) {
			struct xfs_buf	*bp = NULL;
			xfs_daddr_t	daddr;
			int		error;

			daddr = XFS_AGB_TO_DADDR(mp, agno, bno);
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
	trace_xreap_agextent_binval(sc->sa.pag, agbno, *aglenp);
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
	trace_xreap_agextent_select(sc->sa.pag, agbno, len, *crosslinked);
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

	fsbno = XFS_AGB_TO_FSB(sc->mp, sc->sa.pag->pag_agno, agbno);

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
		trace_xreap_dispose_unmap_extent(sc->sa.pag, agbno, *aglenp);

		rs->force_roll = true;
		return xfs_rmap_free(sc->tp, sc->sa.agf_bp, sc->sa.pag, agbno,
				*aglenp, rs->oinfo);
	}

	trace_xreap_dispose_free_extent(sc->sa.pag, agbno, *aglenp);

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
	 */
	error = __xfs_free_extent_later(sc->tp, fsbno, *aglenp, rs->oinfo,
			rs->resv, true);
	if (error)
		return error;

	rs->deferred++;
	return 0;
}

/*
 * Break an AG metadata extent into sub-extents by fate (crosslinked, not
 * crosslinked), and dispose of each sub-extent separately.
 */
STATIC int
xreap_agmeta_extent(
	uint64_t		fsbno,
	uint64_t		len,
	void			*priv)
{
	struct xreap_state	*rs = priv;
	struct xfs_scrub	*sc = rs->sc;
	xfs_agblock_t		agbno = fsbno;
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
