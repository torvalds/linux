// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2022-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs_platform.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_rtgroup.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_rmap.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/repair.h"

/* Set us up with a transaction and an empty context. */
int
xchk_setup_rgsuperblock(
	struct xfs_scrub	*sc)
{
	if (xchk_need_intent_drain(sc))
		xchk_fsgates_enable(sc, XCHK_FSGATES_DRAIN);
	return xchk_trans_alloc(sc, 0);
}

/* Cross-reference with the other rt metadata. */
STATIC void
xchk_rgsuperblock_xref(
	struct xfs_scrub	*sc)
{
	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return;

	xchk_xref_is_used_rt_space(sc, xfs_rgbno_to_rtb(sc->sr.rtg, 0),
			sc->mp->m_sb.sb_rextsize);
	xchk_xref_is_only_rt_owned_by(sc, 0, sc->mp->m_sb.sb_rextsize,
			&XFS_RMAP_OINFO_FS);
}

int
xchk_rgsuperblock(
	struct xfs_scrub	*sc)
{
	struct xfs_buf		*bp = NULL;
	xfs_rgnumber_t		rgno = sc->sm->sm_agno;
	unsigned int		flags;
	int			error;

	/*
	 * Only rtgroup 0 has a superblock.  We may someday want to use higher
	 * rgno for other functions, similar to what we do with the primary
	 * super scrub function.
	 */
	if (rgno != 0)
		return -ENOENT;

	/*
	 * Grab an active reference to the rtgroup structure.  If we can't get
	 * it, we're racing with something that's tearing down the group, so
	 * signal that the group no longer exists.  Take the rtbitmap in shared
	 * mode so that the group can't change while we're doing things.
	 */
	error = xchk_rtgroup_init_existing(sc, rgno);
	if (!xchk_xref_process_error(sc, 0, 0, &error))
		return error;

	if (xfs_has_rtrmapbt(sc->mp))
		flags = XFS_RTGLOCK_BITMAP | XFS_RTGLOCK_RMAP;
	else
		flags = XFS_RTGLOCK_BITMAP_SHARED;

	error = xchk_rtgroup_lock(sc, flags);
	if (error)
		return error;

	/*
	 * Read the rt super from disk in case it's been corrupted since mount
	 * time.  Crashing with a bad rt super may prevent remount, so we want
	 * to fix these things ASAP.
	 */
	error = xfs_buf_read_uncached(sc->mp->m_rtdev_targp, XFS_RTSB_DADDR,
			sc->mp->m_sb.sb_blocksize >> BBSHIFT, &bp,
			&xfs_rtsb_buf_ops);
	if (!xchk_process_rt_error(sc, 0, 0, &error))
		return error;
	xfs_buf_relse(bp);

	xchk_rgsuperblock_xref(sc);
	return 0;
}

#ifdef CONFIG_XFS_ONLINE_REPAIR
int
xrep_rgsuperblock(
	struct xfs_scrub	*sc)
{
	struct xfs_buf		*sb_bp;
	struct xfs_buf		*rtsb_bp;
	int			error;

	ASSERT(rtg_rgno(sc->sr.rtg) == 0);

	sb_bp = xfs_trans_getsb(sc->tp);
	xfs_log_sb(sc->tp);
	rtsb_bp = xfs_log_rtsb(sc->tp, sb_bp);
	if (!rtsb_bp)
		return 0;

	/* synchronous transaction to flush/release the buffer log item */
	xfs_trans_set_sync(sc->tp);
	error = xrep_trans_commit(sc);
	if (error)
		return error;

	/* write the rt super out immediately */
	xfs_buf_lock(rtsb_bp);
	xfs_buf_hold(rtsb_bp);
	error = xfs_bwrite(rtsb_bp);
	xfs_buf_relse(rtsb_bp);
	return error;
}
#endif /* CONFIG_XFS_ONLINE_REPAIR */
