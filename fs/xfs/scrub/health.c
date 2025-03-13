// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2019-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_btree.h"
#include "xfs_ag.h"
#include "xfs_health.h"
#include "xfs_rtgroup.h"
#include "scrub/scrub.h"
#include "scrub/health.h"
#include "scrub/common.h"

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
	XHG_NONE = 1,
	XHG_FS,
	XHG_AG,
	XHG_INO,
	XHG_RTGROUP,
};

struct xchk_health_map {
	enum xchk_health_group	group;
	unsigned int		sick_mask;
};

static const struct xchk_health_map type_to_health_flag[XFS_SCRUB_TYPE_NR] = {
	[XFS_SCRUB_TYPE_PROBE]		= { XHG_NONE,  0 },
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
	[XFS_SCRUB_TYPE_RTBITMAP]	= { XHG_RTGROUP, XFS_SICK_RG_BITMAP },
	[XFS_SCRUB_TYPE_RTSUM]		= { XHG_RTGROUP, XFS_SICK_RG_SUMMARY },
	[XFS_SCRUB_TYPE_UQUOTA]		= { XHG_FS,  XFS_SICK_FS_UQUOTA },
	[XFS_SCRUB_TYPE_GQUOTA]		= { XHG_FS,  XFS_SICK_FS_GQUOTA },
	[XFS_SCRUB_TYPE_PQUOTA]		= { XHG_FS,  XFS_SICK_FS_PQUOTA },
	[XFS_SCRUB_TYPE_FSCOUNTERS]	= { XHG_FS,  XFS_SICK_FS_COUNTERS },
	[XFS_SCRUB_TYPE_QUOTACHECK]	= { XHG_FS,  XFS_SICK_FS_QUOTACHECK },
	[XFS_SCRUB_TYPE_NLINKS]		= { XHG_FS,  XFS_SICK_FS_NLINKS },
	[XFS_SCRUB_TYPE_DIRTREE]	= { XHG_INO, XFS_SICK_INO_DIRTREE },
	[XFS_SCRUB_TYPE_METAPATH]	= { XHG_FS,  XFS_SICK_FS_METAPATH },
	[XFS_SCRUB_TYPE_RGSUPER]	= { XHG_RTGROUP, XFS_SICK_RG_SUPER },
	[XFS_SCRUB_TYPE_RTRMAPBT]	= { XHG_RTGROUP, XFS_SICK_RG_RMAPBT },
	[XFS_SCRUB_TYPE_RTREFCBT]	= { XHG_RTGROUP, XFS_SICK_RG_REFCNTBT },
};

/* Return the health status mask for this scrub type. */
unsigned int
xchk_health_mask_for_scrub_type(
	__u32			scrub_type)
{
	return type_to_health_flag[scrub_type].sick_mask;
}

/*
 * If the scrub state is clean, add @mask to the scrub sick mask to clear
 * additional sick flags from the metadata object's sick state.
 */
void
xchk_mark_healthy_if_clean(
	struct xfs_scrub	*sc,
	unsigned int		mask)
{
	if (!(sc->sm->sm_flags & (XFS_SCRUB_OFLAG_CORRUPT |
				  XFS_SCRUB_OFLAG_XCORRUPT)))
		sc->healthy_mask |= mask;
}

/*
 * If we're scrubbing a piece of file metadata for the first time, does it look
 * like it has been zapped?  Skip the check if we just repaired the metadata
 * and are revalidating it.
 */
bool
xchk_file_looks_zapped(
	struct xfs_scrub	*sc,
	unsigned int		mask)
{
	ASSERT((mask & ~XFS_SICK_INO_ZAPPED) == 0);

	if (sc->flags & XREP_ALREADY_FIXED)
		return false;

	return xfs_inode_has_sickness(sc->ip, mask);
}

/*
 * Scrub gave the filesystem a clean bill of health, so clear all the indirect
 * markers of past problems (at least for the fs and ags) so that we can be
 * healthy again.
 */
STATIC void
xchk_mark_all_healthy(
	struct xfs_mount	*mp)
{
	struct xfs_perag	*pag = NULL;
	struct xfs_rtgroup	*rtg = NULL;

	xfs_fs_mark_healthy(mp, XFS_SICK_FS_INDIRECT);
	while ((pag = xfs_perag_next(mp, pag)))
		xfs_group_mark_healthy(pag_group(pag), XFS_SICK_AG_INDIRECT);
	while ((rtg = xfs_rtgroup_next(mp, rtg)))
		xfs_group_mark_healthy(rtg_group(rtg), XFS_SICK_RG_INDIRECT);
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
	struct xfs_rtgroup	*rtg;
	unsigned int		mask = sc->sick_mask;
	bool			bad;

	/*
	 * The HEALTHY scrub type is a request from userspace to clear all the
	 * indirect flags after a clean scan of the entire filesystem.  As such
	 * there's no sick flag defined for it, so we branch here ahead of the
	 * mask check.
	 */
	if (sc->sm->sm_type == XFS_SCRUB_TYPE_HEALTHY &&
	    !(sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)) {
		xchk_mark_all_healthy(sc->mp);
		return;
	}

	bad = (sc->sm->sm_flags & (XFS_SCRUB_OFLAG_CORRUPT |
				   XFS_SCRUB_OFLAG_XCORRUPT));
	if (!bad)
		mask |= sc->healthy_mask;
	switch (type_to_health_flag[sc->sm->sm_type].group) {
	case XHG_NONE:
		break;
	case XHG_AG:
		if (!mask)
			return;
		pag = xfs_perag_get(sc->mp, sc->sm->sm_agno);
		if (bad)
			xfs_group_mark_corrupt(pag_group(pag), mask);
		else
			xfs_group_mark_healthy(pag_group(pag), mask);
		xfs_perag_put(pag);
		break;
	case XHG_INO:
		if (!sc->ip)
			return;
		/*
		 * If we're coming in for repairs then we don't want sickness
		 * flags to propagate to the incore health status if the inode
		 * gets inactivated before we can fix it.
		 */
		if (sc->sm->sm_flags & XFS_SCRUB_IFLAG_REPAIR)
			mask |= XFS_SICK_INO_FORGET;
		if (!mask)
			return;
		if (bad)
			xfs_inode_mark_corrupt(sc->ip, mask);
		else
			xfs_inode_mark_healthy(sc->ip, mask);
		break;
	case XHG_FS:
		if (!mask)
			return;
		if (bad)
			xfs_fs_mark_corrupt(sc->mp, mask);
		else
			xfs_fs_mark_healthy(sc->mp, mask);
		break;
	case XHG_RTGROUP:
		if (!mask)
			return;
		rtg = xfs_rtgroup_get(sc->mp, sc->sm->sm_agno);
		if (bad)
			xfs_group_mark_corrupt(rtg_group(rtg), mask);
		else
			xfs_group_mark_healthy(rtg_group(rtg), mask);
		xfs_rtgroup_put(rtg);
		break;
	default:
		ASSERT(0);
		break;
	}
}

/* Is the given per-AG btree healthy enough for scanning? */
void
xchk_ag_btree_del_cursor_if_sick(
	struct xfs_scrub	*sc,
	struct xfs_btree_cur	**curp,
	unsigned int		sm_type)
{
	unsigned int		mask = (*curp)->bc_ops->sick_mask;

	/*
	 * We always want the cursor if it's the same type as whatever we're
	 * scrubbing, even if we already know the structure is corrupt.
	 *
	 * Otherwise, we're only interested in the btree for cross-referencing.
	 * If we know the btree is bad then don't bother, just set XFAIL.
	 */
	if (sc->sm->sm_type == sm_type)
		return;

	/*
	 * If we just repaired some AG metadata, sc->sick_mask will reflect all
	 * the per-AG metadata types that were repaired.  Exclude these from
	 * the filesystem health query because we have not yet updated the
	 * health status and we want everything to be scanned.
	 */
	if ((sc->flags & XREP_ALREADY_FIXED) &&
	    type_to_health_flag[sc->sm->sm_type].group == XHG_AG)
		mask &= ~sc->sick_mask;

	if (xfs_group_has_sickness((*curp)->bc_group, mask)) {
		sc->sm->sm_flags |= XFS_SCRUB_OFLAG_XFAIL;
		xfs_btree_del_cursor(*curp, XFS_BTREE_NOERROR);
		*curp = NULL;
	}
}

/*
 * Quick scan to double-check that there isn't any evidence of lingering
 * primary health problems.  If we're still clear, then the health update will
 * take care of clearing the indirect evidence.
 */
int
xchk_health_record(
	struct xfs_scrub	*sc)
{
	struct xfs_mount	*mp = sc->mp;
	struct xfs_perag	*pag = NULL;
	struct xfs_rtgroup	*rtg = NULL;
	unsigned int		sick;
	unsigned int		checked;

	xfs_fs_measure_sickness(mp, &sick, &checked);
	if (sick & XFS_SICK_FS_PRIMARY)
		xchk_set_corrupt(sc);

	while ((pag = xfs_perag_next(mp, pag))) {
		xfs_group_measure_sickness(pag_group(pag), &sick, &checked);
		if (sick & XFS_SICK_AG_PRIMARY)
			xchk_set_corrupt(sc);
	}

	while ((rtg = xfs_rtgroup_next(mp, rtg))) {
		xfs_group_measure_sickness(rtg_group(rtg), &sick, &checked);
		if (sick & XFS_SICK_RG_PRIMARY)
			xchk_set_corrupt(sc);
	}

	return 0;
}
