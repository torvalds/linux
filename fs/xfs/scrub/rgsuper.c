// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2022-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
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
	return xchk_trans_alloc(sc, 0);
}

/* Cross-reference with the other rt metadata. */
STATIC void
xchk_rgsuperblock_xref(
	struct xfs_scrub	*sc)
{
	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return;

	xchk_xref_is_used_rt_space(sc, xfs_rgbno_to_rtb(sc->sr.rtg, 0), 1);
	xchk_xref_is_only_rt_owned_by(sc, 0, 1, &XFS_RMAP_OINFO_FS);
}

int
xchk_rgsuperblock(
	struct xfs_scrub	*sc)
{
	xfs_rgnumber_t		rgno = sc->sm->sm_agno;
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
	error = xchk_rtgroup_init_existing(sc, rgno, &sc->sr);
	if (!xchk_xref_process_error(sc, 0, 0, &error))
		return error;

	error = xchk_rtgroup_lock(sc, &sc->sr, XFS_RTGLOCK_BITMAP_SHARED);
	if (error)
		return error;

	/*
	 * Since we already validated the rt superblock at mount time, we don't
	 * need to check its contents again.  All we need is to cross-reference.
	 */
	xchk_rgsuperblock_xref(sc);
	return 0;
}

#ifdef CONFIG_XFS_ONLINE_REPAIR
int
xrep_rgsuperblock(
	struct xfs_scrub	*sc)
{
	ASSERT(rtg_rgno(sc->sr.rtg) == 0);

	xfs_log_sb(sc->tp);
	return 0;
}
#endif /* CONFIG_XFS_ONLINE_REPAIR */
