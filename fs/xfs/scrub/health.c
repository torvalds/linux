// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2019 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_btree.h"
#include "xfs_ag.h"
#include "xfs_health.h"
#include "scrub/scrub.h"
#include "scrub/health.h"

/*
 * Scrub and In-Core Filesystem Health Assessments
 * ===============================================
 *
 * Online scrub and repair have the time and the ability to perform stronger
 * checks than we can do from the metadata verifiers, because they can
 * cross-reference records between data structures.  Therefore, scrub is in a
 * good position to update the online filesystem health assessments to reflect
 * the good/bad state of the data structure.
 *
 * We therefore extend scrub in the following ways to achieve this:
 *
 * 1. Create a "sick_mask" field in the scrub context.  When we're setting up a
 * scrub call, set this to the default XFS_SICK_* flag(s) for the selected
 * scrub type (call it A).  Scrub and repair functions can override the default
 * sick_mask value if they choose.
 *
 * 2. If the scrubber returns a runtime error code, we exit making no changes
 * to the incore sick state.
 *
 * 3. If the scrubber finds that A is clean, use sick_mask to clear the incore
 * sick flags before exiting.
 *
 * 4. If the scrubber finds that A is corrupt, use sick_mask to set the incore
 * sick flags.  If the user didn't want to repair then we exit, leaving the
 * metadata structure unfixed and the sick flag set.
 *
 * 5. Now we know that A is corrupt and the user wants to repair, so run the
 * repairer.  If the repairer returns an error code, we exit with that error
 * code, having made no further changes to the incore sick state.
 *
 * 6. If repair rebuilds A correctly and the subsequent re-scrub of A is clean,
 * use sick_mask to clear the incore sick flags.  This should have the effect
 * that A is no longer marked sick.
 *
 * 7. If repair rebuilds A incorrectly, the re-scrub will find it corrupt and
 * use sick_mask to set the incore sick flags.  This should have no externally
 * visible effect since we already set them in step (4).
 *
 * There are some complications to this story, however.  For certain types of
 * complementary metadata indices (e.g. inobt/finobt), it is easier to rebuild
 * both structures at the same time.  The following principles apply to this
 * type of repair strategy:
 *
 * 8. Any repair function that rebuilds multiple structures should update
 * sick_mask_visible to reflect whatever other structures are rebuilt, and
 * verify that all the rebuilt structures can pass a scrub check.  The outcomes
 * of 5-7 still apply, but with a sick_mask that covers everything being
 * rebuilt.
 */

/* Map our scrub type to a sick mask and a set of health update functions. */

enum xchk_health_group {
	XHG_FS = 1,
	XHG_RT,
	XHG_AG,
	XHG_INO,
};

struct xchk_health_map {
	enum xchk_health_group	group;
	unsigned int		sick_mask;
};

static const struct xchk_health_map type_to_health_flag[XFS_SCRUB_TYPE_NR] = {
	[XFS_SCRUB_TYPE_SB]		= { XHG_AG,  XFS_SICK_AG_SB },
	[XFS_SCRUB_TYPE_AGF]		= { XHG_AG,  XFS_SICK_AG_AGF },
	[XFS_SCRUB_TYPE_AGFL]		= { XHG_AG,  XFS_SICK_AG_AGFL },
	[XFS_SCRUB_TYPE_AGI]		= { XHG_AG,  XFS_SICK_AG_AGI },
	[XFS_SCRUB_TYPE_BNOBT]		= { XHG_AG,  XFS_SICK_AG_BNOBT },
	[XFS_SCRUB_TYPE_CNTBT]		= { XHG_AG,  XFS_SICK_AG_CNTBT },
	[XFS_SCRUB_TYPE_INOBT]		= { XHG_AG,  XFS_SICK_AG_INOBT },
	[XFS_SCRUB_TYPE_FINOBT]		= { XHG_AG,  XFS_SICK_AG_FINOBT },
	[XFS_SCRUB_TYPE_RMAPBT]		= { XHG_AG,  XFS_SICK_AG_RMAPBT },
	[XFS_SCRUB_TYPE_REFCNTBT]	= { XHG_AG,  XFS_SICK_AG_REFCNTBT },
	[XFS_SCRUB_TYPE_INODE]		= { XHG_INO, XFS_SICK_INO_CORE },
	[XFS_SCRUB_TYPE_BMBTD]		= { XHG_INO, XFS_SICK_INO_BMBTD },
	[XFS_SCRUB_TYPE_BMBTA]		= { XHG_INO, XFS_SICK_INO_BMBTA },
	[XFS_SCRUB_TYPE_BMBTC]		= { XHG_INO, XFS_SICK_INO_BMBTC },
	[XFS_SCRUB_TYPE_DIR]		= { XHG_INO, XFS_SICK_INO_DIR },
	[XFS_SCRUB_TYPE_XATTR]		= { XHG_INO, XFS_SICK_INO_XATTR },
	[XFS_SCRUB_TYPE_SYMLINK]	= { XHG_INO, XFS_SICK_INO_SYMLINK },
	[XFS_SCRUB_TYPE_PARENT]		= { XHG_INO, XFS_SICK_INO_PARENT },
	[XFS_SCRUB_TYPE_RTBITMAP]	= { XHG_RT,  XFS_SICK_RT_BITMAP },
	[XFS_SCRUB_TYPE_RTSUM]		= { XHG_RT,  XFS_SICK_RT_SUMMARY },
	[XFS_SCRUB_TYPE_UQUOTA]		= { XHG_FS,  XFS_SICK_FS_UQUOTA },
	[XFS_SCRUB_TYPE_GQUOTA]		= { XHG_FS,  XFS_SICK_FS_GQUOTA },
	[XFS_SCRUB_TYPE_PQUOTA]		= { XHG_FS,  XFS_SICK_FS_PQUOTA },
	[XFS_SCRUB_TYPE_FSCOUNTERS]	= { XHG_FS,  XFS_SICK_FS_COUNTERS },
};

/* Return the health status mask for this scrub type. */
unsigned int
xchk_health_mask_for_scrub_type(
	__u32			scrub_type)
{
	return type_to_health_flag[scrub_type].sick_mask;
}

/*
 * Update filesystem health assessments based on what we found and did.
 *
 * If the scrubber finds errors, we mark sick whatever's mentioned in
 * sick_mask, no matter whether this is a first scan or an
 * evaluation of repair effectiveness.
 *
 * Otherwise, no direct corruption was found, so mark whatever's in
 * sick_mask as healthy.
 */
void
xchk_update_health(
	struct xfs_scrub	*sc)
{
	struct xfs_perag	*pag;
	bool			bad;

	if (!sc->sick_mask)
		return;

	bad = (sc->sm->sm_flags & (XFS_SCRUB_OFLAG_CORRUPT |
				   XFS_SCRUB_OFLAG_XCORRUPT));
	switch (type_to_health_flag[sc->sm->sm_type].group) {
	case XHG_AG:
		pag = xfs_perag_get(sc->mp, sc->sm->sm_agno);
		if (bad)
			xfs_ag_mark_sick(pag, sc->sick_mask);
		else
			xfs_ag_mark_healthy(pag, sc->sick_mask);
		xfs_perag_put(pag);
		break;
	case XHG_INO:
		if (!sc->ip)
			return;
		if (bad)
			xfs_inode_mark_sick(sc->ip, sc->sick_mask);
		else
			xfs_inode_mark_healthy(sc->ip, sc->sick_mask);
		break;
	case XHG_FS:
		if (bad)
			xfs_fs_mark_sick(sc->mp, sc->sick_mask);
		else
			xfs_fs_mark_healthy(sc->mp, sc->sick_mask);
		break;
	case XHG_RT:
		if (bad)
			xfs_rt_mark_sick(sc->mp, sc->sick_mask);
		else
			xfs_rt_mark_healthy(sc->mp, sc->sick_mask);
		break;
	default:
		ASSERT(0);
		break;
	}
}

/* Is the given per-AG btree healthy enough for scanning? */
bool
xchk_ag_btree_healthy_enough(
	struct xfs_scrub	*sc,
	struct xfs_perag	*pag,
	xfs_btnum_t		btnum)
{
	unsigned int		mask = 0;

	/*
	 * We always want the cursor if it's the same type as whatever we're
	 * scrubbing, even if we already know the structure is corrupt.
	 *
	 * Otherwise, we're only interested in the btree for cross-referencing.
	 * If we know the btree is bad then don't bother, just set XFAIL.
	 */
	switch (btnum) {
	case XFS_BTNUM_BNO:
		if (sc->sm->sm_type == XFS_SCRUB_TYPE_BNOBT)
			return true;
		mask = XFS_SICK_AG_BNOBT;
		break;
	case XFS_BTNUM_CNT:
		if (sc->sm->sm_type == XFS_SCRUB_TYPE_CNTBT)
			return true;
		mask = XFS_SICK_AG_CNTBT;
		break;
	case XFS_BTNUM_INO:
		if (sc->sm->sm_type == XFS_SCRUB_TYPE_INOBT)
			return true;
		mask = XFS_SICK_AG_INOBT;
		break;
	case XFS_BTNUM_FINO:
		if (sc->sm->sm_type == XFS_SCRUB_TYPE_FINOBT)
			return true;
		mask = XFS_SICK_AG_FINOBT;
		break;
	case XFS_BTNUM_RMAP:
		if (sc->sm->sm_type == XFS_SCRUB_TYPE_RMAPBT)
			return true;
		mask = XFS_SICK_AG_RMAPBT;
		break;
	case XFS_BTNUM_REFC:
		if (sc->sm->sm_type == XFS_SCRUB_TYPE_REFCNTBT)
			return true;
		mask = XFS_SICK_AG_REFCNTBT;
		break;
	default:
		ASSERT(0);
		return true;
	}

	if (xfs_ag_has_sickness(pag, mask)) {
		sc->sm->sm_flags |= XFS_SCRUB_OFLAG_XFAIL;
		return false;
	}

	return true;
}
