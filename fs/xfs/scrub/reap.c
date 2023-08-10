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
struct xrep_reap_state {
	struct xfs_scrub		*sc;

	/* Reverse mapping owner and metadata reservation type. */
	const struct xfs_owner_info	*oinfo;
	enum xfs_ag_resv_type		resv;

	/* Number of deferred reaps attached to the current transaction. */
	unsigned int			deferred;
};

/* Put a block back on the AGFL. */
STATIC int
xrep_put_freelist(
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

/* Try to invalidate the incore buffer for a block that we're about to free. */
STATIC void
xrep_block_reap_binval(
	struct xfs_scrub	*sc,
	xfs_fsblock_t		fsbno)
{
	struct xfs_buf		*bp = NULL;
	int			error;

	/*
	 * If there's an incore buffer for exactly this block, invalidate it.
	 * Avoid invalidating AG headers and post-EOFS blocks because we never
	 * own those.
	 */
	if (!xfs_verify_fsbno(sc->mp, fsbno))
		return;

	/*
	 * We assume that the lack of any other known owners means that the
	 * buffer can be locked without risk of deadlocking.
	 */
	error = xfs_buf_incore(sc->mp->m_ddev_targp,
			XFS_FSB_TO_DADDR(sc->mp, fsbno),
			XFS_FSB_TO_BB(sc->mp, 1), 0, &bp);
	if (error)
		return;

	xfs_trans_bjoin(sc->tp, bp);
	xfs_trans_binval(sc->tp, bp);
}

/* Dispose of a single block. */
STATIC int
xrep_reap_block(
	uint64_t			fsbno,
	void				*priv)
{
	struct xrep_reap_state		*rs = priv;
	struct xfs_scrub		*sc = rs->sc;
	struct xfs_btree_cur		*cur;
	xfs_agnumber_t			agno;
	xfs_agblock_t			agbno;
	bool				has_other_rmap;
	bool				need_roll = true;
	int				error;

	agno = XFS_FSB_TO_AGNO(sc->mp, fsbno);
	agbno = XFS_FSB_TO_AGBNO(sc->mp, fsbno);

	/* We don't support reaping file extents yet. */
	if (sc->ip != NULL || sc->sa.pag->pag_agno != agno) {
		ASSERT(0);
		return -EFSCORRUPTED;
	}

	cur = xfs_rmapbt_init_cursor(sc->mp, sc->tp, sc->sa.agf_bp, sc->sa.pag);

	/* Can we find any other rmappings? */
	error = xfs_rmap_has_other_keys(cur, agbno, 1, rs->oinfo,
			&has_other_rmap);
	xfs_btree_del_cursor(cur, error);
	if (error)
		return error;

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
	if (has_other_rmap) {
		trace_xrep_dispose_unmap_extent(sc->sa.pag, agbno, 1);

		error = xfs_rmap_free(sc->tp, sc->sa.agf_bp, sc->sa.pag, agbno,
				1, rs->oinfo);
		if (error)
			return error;

		goto roll_out;
	}

	trace_xrep_dispose_free_extent(sc->sa.pag, agbno, 1);

	xrep_block_reap_binval(sc, fsbno);

	if (rs->resv == XFS_AG_RESV_AGFL) {
		error = xrep_put_freelist(sc, agbno);
	} else {
		/*
		 * Use deferred frees to get rid of the old btree blocks to try
		 * to minimize the window in which we could crash and lose the
		 * old blocks.  However, we still need to roll the transaction
		 * every 100 or so EFIs so that we don't exceed the log
		 * reservation.
		 */
		error = __xfs_free_extent_later(sc->tp, fsbno, 1, rs->oinfo,
				rs->resv, true);
		if (error)
			return error;
		rs->deferred++;
		need_roll = rs->deferred > 100;
	}
	if (error || !need_roll)
		return error;

roll_out:
	rs->deferred = 0;
	return xrep_roll_ag_trans(sc);
}

/* Dispose of every block of every extent in the bitmap. */
int
xrep_reap_extents(
	struct xfs_scrub		*sc,
	struct xbitmap			*bitmap,
	const struct xfs_owner_info	*oinfo,
	enum xfs_ag_resv_type		type)
{
	struct xrep_reap_state		rs = {
		.sc			= sc,
		.oinfo			= oinfo,
		.resv			= type,
	};
	int				error;

	ASSERT(xfs_has_rmapbt(sc->mp));

	error = xbitmap_walk_bits(bitmap, xrep_reap_block, &rs);
	if (error || rs.deferred == 0)
		return error;

	return xrep_roll_ag_trans(sc);
}
