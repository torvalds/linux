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
