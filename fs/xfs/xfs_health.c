// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2019 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_trace.h"
#include "xfs_health.h"
#include "xfs_ag.h"
#include "xfs_btree.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"
#include "xfs_quota_defs.h"
#include "xfs_rtgroup.h"

static void
xfs_health_unmount_group(
	struct xfs_group	*xg,
	bool			*warn)
{
	unsigned int		sick = 0;
	unsigned int		checked = 0;

	xfs_group_measure_sickness(xg, &sick, &checked);
	if (sick) {
		trace_xfs_group_unfixed_corruption(xg, sick);
		*warn = true;
	}
}

/*
 * Warn about metadata corruption that we detected but haven't fixed, and
 * make sure we're not sitting on anything that would get in the way of
 * recovery.
 */
void
xfs_health_unmount(
	struct xfs_mount	*mp)
{
	struct xfs_perag	*pag = NULL;
	struct xfs_rtgroup	*rtg = NULL;
	unsigned int		sick = 0;
	unsigned int		checked = 0;
	bool			warn = false;

	if (xfs_is_shutdown(mp))
		return;

	/* Measure AG corruption levels. */
	while ((pag = xfs_perag_next(mp, pag)))
		xfs_health_unmount_group(pag_group(pag), &warn);

	/* Measure realtime group corruption levels. */
	while ((rtg = xfs_rtgroup_next(mp, rtg)))
		xfs_health_unmount_group(rtg_group(rtg), &warn);

	/*
	 * Measure fs corruption and keep the sample around for the warning.
	 * See the note below for why we exempt FS_COUNTERS.
	 */
	xfs_fs_measure_sickness(mp, &sick, &checked);
	if (sick & ~XFS_SICK_FS_COUNTERS) {
		trace_xfs_fs_unfixed_corruption(mp, sick);
		warn = true;
	}

	if (warn) {
		xfs_warn(mp,
"Uncorrected metadata errors detected; please run xfs_repair.");

		/*
		 * We discovered uncorrected metadata problems at some point
		 * during this filesystem mount and have advised the
		 * administrator to run repair once the unmount completes.
		 *
		 * However, we must be careful -- when FSCOUNTERS are flagged
		 * unhealthy, the unmount procedure omits writing the clean
		 * unmount record to the log so that the next mount will run
		 * recovery and recompute the summary counters.  In other
		 * words, we leave a dirty log to get the counters fixed.
		 *
		 * Unfortunately, xfs_repair cannot recover dirty logs, so if
		 * there were filesystem problems, FSCOUNTERS was flagged, and
		 * the administrator takes our advice to run xfs_repair,
		 * they'll have to zap the log before repairing structures.
		 * We don't really want to encourage this, so we mark the
		 * FSCOUNTERS healthy so that a subsequent repair run won't see
		 * a dirty log.
		 */
		if (sick & XFS_SICK_FS_COUNTERS)
			xfs_fs_mark_healthy(mp, XFS_SICK_FS_COUNTERS);
	}
}

/* Mark unhealthy per-fs metadata. */
void
xfs_fs_mark_sick(
	struct xfs_mount	*mp,
	unsigned int		mask)
{
	ASSERT(!(mask & ~XFS_SICK_FS_ALL));
	trace_xfs_fs_mark_sick(mp, mask);

	spin_lock(&mp->m_sb_lock);
	mp->m_fs_sick |= mask;
	spin_unlock(&mp->m_sb_lock);
}

/* Mark per-fs metadata as having been checked and found unhealthy by fsck. */
void
xfs_fs_mark_corrupt(
	struct xfs_mount	*mp,
	unsigned int		mask)
{
	ASSERT(!(mask & ~XFS_SICK_FS_ALL));
	trace_xfs_fs_mark_corrupt(mp, mask);

	spin_lock(&mp->m_sb_lock);
	mp->m_fs_sick |= mask;
	mp->m_fs_checked |= mask;
	spin_unlock(&mp->m_sb_lock);
}

/* Mark a per-fs metadata healed. */
void
xfs_fs_mark_healthy(
	struct xfs_mount	*mp,
	unsigned int		mask)
{
	ASSERT(!(mask & ~XFS_SICK_FS_ALL));
	trace_xfs_fs_mark_healthy(mp, mask);

	spin_lock(&mp->m_sb_lock);
	mp->m_fs_sick &= ~mask;
	if (!(mp->m_fs_sick & XFS_SICK_FS_PRIMARY))
		mp->m_fs_sick &= ~XFS_SICK_FS_SECONDARY;
	mp->m_fs_checked |= mask;
	spin_unlock(&mp->m_sb_lock);
}

/* Sample which per-fs metadata are unhealthy. */
void
xfs_fs_measure_sickness(
	struct xfs_mount	*mp,
	unsigned int		*sick,
	unsigned int		*checked)
{
	spin_lock(&mp->m_sb_lock);
	*sick = mp->m_fs_sick;
	*checked = mp->m_fs_checked;
	spin_unlock(&mp->m_sb_lock);
}

/* Mark unhealthy per-ag metadata given a raw AG number. */
void
xfs_agno_mark_sick(
	struct xfs_mount	*mp,
	xfs_agnumber_t		agno,
	unsigned int		mask)
{
	struct xfs_perag	*pag = xfs_perag_get(mp, agno);

	/* per-ag structure not set up yet? */
	if (!pag)
		return;

	xfs_ag_mark_sick(pag, mask);
	xfs_perag_put(pag);
}

static inline void
xfs_group_check_mask(
	struct xfs_group	*xg,
	unsigned int		mask)
{
	if (xg->xg_type == XG_TYPE_AG)
		ASSERT(!(mask & ~XFS_SICK_AG_ALL));
	else
		ASSERT(!(mask & ~XFS_SICK_RG_ALL));
}

/* Mark unhealthy per-ag metadata. */
void
xfs_group_mark_sick(
	struct xfs_group	*xg,
	unsigned int		mask)
{
	xfs_group_check_mask(xg, mask);
	trace_xfs_group_mark_sick(xg, mask);

	spin_lock(&xg->xg_state_lock);
	xg->xg_sick |= mask;
	spin_unlock(&xg->xg_state_lock);
}

/*
 * Mark per-group metadata as having been checked and found unhealthy by fsck.
 */
void
xfs_group_mark_corrupt(
	struct xfs_group	*xg,
	unsigned int		mask)
{
	xfs_group_check_mask(xg, mask);
	trace_xfs_group_mark_corrupt(xg, mask);

	spin_lock(&xg->xg_state_lock);
	xg->xg_sick |= mask;
	xg->xg_checked |= mask;
	spin_unlock(&xg->xg_state_lock);
}

/*
 * Mark per-group metadata ok.
 */
void
xfs_group_mark_healthy(
	struct xfs_group	*xg,
	unsigned int		mask)
{
	xfs_group_check_mask(xg, mask);
	trace_xfs_group_mark_healthy(xg, mask);

	spin_lock(&xg->xg_state_lock);
	xg->xg_sick &= ~mask;
	if (!(xg->xg_sick & XFS_SICK_AG_PRIMARY))
		xg->xg_sick &= ~XFS_SICK_AG_SECONDARY;
	xg->xg_checked |= mask;
	spin_unlock(&xg->xg_state_lock);
}

/* Sample which per-ag metadata are unhealthy. */
void
xfs_group_measure_sickness(
	struct xfs_group	*xg,
	unsigned int		*sick,
	unsigned int		*checked)
{
	spin_lock(&xg->xg_state_lock);
	*sick = xg->xg_sick;
	*checked = xg->xg_checked;
	spin_unlock(&xg->xg_state_lock);
}

/* Mark unhealthy per-rtgroup metadata given a raw rt group number. */
void
xfs_rgno_mark_sick(
	struct xfs_mount	*mp,
	xfs_rgnumber_t		rgno,
	unsigned int		mask)
{
	struct xfs_rtgroup	*rtg = xfs_rtgroup_get(mp, rgno);

	/* per-rtgroup structure not set up yet? */
	if (!rtg)
		return;

	xfs_group_mark_sick(rtg_group(rtg), mask);
	xfs_rtgroup_put(rtg);
}

/* Mark the unhealthy parts of an inode. */
void
xfs_inode_mark_sick(
	struct xfs_inode	*ip,
	unsigned int		mask)
{
	ASSERT(!(mask & ~XFS_SICK_INO_ALL));
	trace_xfs_inode_mark_sick(ip, mask);

	spin_lock(&ip->i_flags_lock);
	ip->i_sick |= mask;
	spin_unlock(&ip->i_flags_lock);

	/*
	 * Keep this inode around so we don't lose the sickness report.  Scrub
	 * grabs inodes with DONTCACHE assuming that most inode are ok, which
	 * is not the case here.
	 */
	spin_lock(&VFS_I(ip)->i_lock);
	VFS_I(ip)->i_state &= ~I_DONTCACHE;
	spin_unlock(&VFS_I(ip)->i_lock);
}

/* Mark inode metadata as having been checked and found unhealthy by fsck. */
void
xfs_inode_mark_corrupt(
	struct xfs_inode	*ip,
	unsigned int		mask)
{
	ASSERT(!(mask & ~XFS_SICK_INO_ALL));
	trace_xfs_inode_mark_corrupt(ip, mask);

	spin_lock(&ip->i_flags_lock);
	ip->i_sick |= mask;
	ip->i_checked |= mask;
	spin_unlock(&ip->i_flags_lock);

	/*
	 * Keep this inode around so we don't lose the sickness report.  Scrub
	 * grabs inodes with DONTCACHE assuming that most inode are ok, which
	 * is not the case here.
	 */
	spin_lock(&VFS_I(ip)->i_lock);
	VFS_I(ip)->i_state &= ~I_DONTCACHE;
	spin_unlock(&VFS_I(ip)->i_lock);
}

/* Mark parts of an inode healed. */
void
xfs_inode_mark_healthy(
	struct xfs_inode	*ip,
	unsigned int		mask)
{
	ASSERT(!(mask & ~XFS_SICK_INO_ALL));
	trace_xfs_inode_mark_healthy(ip, mask);

	spin_lock(&ip->i_flags_lock);
	ip->i_sick &= ~mask;
	if (!(ip->i_sick & XFS_SICK_INO_PRIMARY))
		ip->i_sick &= ~XFS_SICK_INO_SECONDARY;
	ip->i_checked |= mask;
	spin_unlock(&ip->i_flags_lock);
}

/* Sample which parts of an inode are unhealthy. */
void
xfs_inode_measure_sickness(
	struct xfs_inode	*ip,
	unsigned int		*sick,
	unsigned int		*checked)
{
	spin_lock(&ip->i_flags_lock);
	*sick = ip->i_sick;
	*checked = ip->i_checked;
	spin_unlock(&ip->i_flags_lock);
}

/* Mappings between internal sick masks and ioctl sick masks. */

struct ioctl_sick_map {
	unsigned int		sick_mask;
	unsigned int		ioctl_mask;
};

#define for_each_sick_map(map, m) \
	for ((m) = (map); (m) < (map) + ARRAY_SIZE(map); (m)++)

static const struct ioctl_sick_map fs_map[] = {
	{ XFS_SICK_FS_COUNTERS,	XFS_FSOP_GEOM_SICK_COUNTERS},
	{ XFS_SICK_FS_UQUOTA,	XFS_FSOP_GEOM_SICK_UQUOTA },
	{ XFS_SICK_FS_GQUOTA,	XFS_FSOP_GEOM_SICK_GQUOTA },
	{ XFS_SICK_FS_PQUOTA,	XFS_FSOP_GEOM_SICK_PQUOTA },
	{ XFS_SICK_FS_QUOTACHECK, XFS_FSOP_GEOM_SICK_QUOTACHECK },
	{ XFS_SICK_FS_NLINKS,	XFS_FSOP_GEOM_SICK_NLINKS },
	{ XFS_SICK_FS_METADIR,	XFS_FSOP_GEOM_SICK_METADIR },
	{ XFS_SICK_FS_METAPATH,	XFS_FSOP_GEOM_SICK_METAPATH },
};

static const struct ioctl_sick_map rt_map[] = {
	{ XFS_SICK_RG_BITMAP,	XFS_FSOP_GEOM_SICK_RT_BITMAP },
	{ XFS_SICK_RG_SUMMARY,	XFS_FSOP_GEOM_SICK_RT_SUMMARY },
};

static inline void
xfgeo_health_tick(
	struct xfs_fsop_geom		*geo,
	unsigned int			sick,
	unsigned int			checked,
	const struct ioctl_sick_map	*m)
{
	if (checked & m->sick_mask)
		geo->checked |= m->ioctl_mask;
	if (sick & m->sick_mask)
		geo->sick |= m->ioctl_mask;
}

/* Fill out fs geometry health info. */
void
xfs_fsop_geom_health(
	struct xfs_mount		*mp,
	struct xfs_fsop_geom		*geo)
{
	struct xfs_rtgroup		*rtg = NULL;
	const struct ioctl_sick_map	*m;
	unsigned int			sick;
	unsigned int			checked;

	geo->sick = 0;
	geo->checked = 0;

	xfs_fs_measure_sickness(mp, &sick, &checked);
	for_each_sick_map(fs_map, m)
		xfgeo_health_tick(geo, sick, checked, m);

	while ((rtg = xfs_rtgroup_next(mp, rtg))) {
		xfs_group_measure_sickness(rtg_group(rtg), &sick, &checked);
		for_each_sick_map(rt_map, m)
			xfgeo_health_tick(geo, sick, checked, m);
	}
}

static const struct ioctl_sick_map ag_map[] = {
	{ XFS_SICK_AG_SB,	XFS_AG_GEOM_SICK_SB },
	{ XFS_SICK_AG_AGF,	XFS_AG_GEOM_SICK_AGF },
	{ XFS_SICK_AG_AGFL,	XFS_AG_GEOM_SICK_AGFL },
	{ XFS_SICK_AG_AGI,	XFS_AG_GEOM_SICK_AGI },
	{ XFS_SICK_AG_BNOBT,	XFS_AG_GEOM_SICK_BNOBT },
	{ XFS_SICK_AG_CNTBT,	XFS_AG_GEOM_SICK_CNTBT },
	{ XFS_SICK_AG_INOBT,	XFS_AG_GEOM_SICK_INOBT },
	{ XFS_SICK_AG_FINOBT,	XFS_AG_GEOM_SICK_FINOBT },
	{ XFS_SICK_AG_RMAPBT,	XFS_AG_GEOM_SICK_RMAPBT },
	{ XFS_SICK_AG_REFCNTBT,	XFS_AG_GEOM_SICK_REFCNTBT },
	{ XFS_SICK_AG_INODES,	XFS_AG_GEOM_SICK_INODES },
};

/* Fill out ag geometry health info. */
void
xfs_ag_geom_health(
	struct xfs_perag		*pag,
	struct xfs_ag_geometry		*ageo)
{
	const struct ioctl_sick_map	*m;
	unsigned int			sick;
	unsigned int			checked;

	ageo->ag_sick = 0;
	ageo->ag_checked = 0;

	xfs_group_measure_sickness(pag_group(pag), &sick, &checked);
	for_each_sick_map(ag_map, m) {
		if (checked & m->sick_mask)
			ageo->ag_checked |= m->ioctl_mask;
		if (sick & m->sick_mask)
			ageo->ag_sick |= m->ioctl_mask;
	}
}

static const struct ioctl_sick_map rtgroup_map[] = {
	{ XFS_SICK_RG_SUPER,	XFS_RTGROUP_GEOM_SICK_SUPER },
	{ XFS_SICK_RG_BITMAP,	XFS_RTGROUP_GEOM_SICK_BITMAP },
	{ XFS_SICK_RG_SUMMARY,	XFS_RTGROUP_GEOM_SICK_SUMMARY },
};

/* Fill out rtgroup geometry health info. */
void
xfs_rtgroup_geom_health(
	struct xfs_rtgroup	*rtg,
	struct xfs_rtgroup_geometry *rgeo)
{
	const struct ioctl_sick_map	*m;
	unsigned int			sick;
	unsigned int			checked;

	rgeo->rg_sick = 0;
	rgeo->rg_checked = 0;

	xfs_group_measure_sickness(rtg_group(rtg), &sick, &checked);
	for_each_sick_map(rtgroup_map, m) {
		if (checked & m->sick_mask)
			rgeo->rg_checked |= m->ioctl_mask;
		if (sick & m->sick_mask)
			rgeo->rg_sick |= m->ioctl_mask;
	}
}

static const struct ioctl_sick_map ino_map[] = {
	{ XFS_SICK_INO_CORE,	XFS_BS_SICK_INODE },
	{ XFS_SICK_INO_BMBTD,	XFS_BS_SICK_BMBTD },
	{ XFS_SICK_INO_BMBTA,	XFS_BS_SICK_BMBTA },
	{ XFS_SICK_INO_BMBTC,	XFS_BS_SICK_BMBTC },
	{ XFS_SICK_INO_DIR,	XFS_BS_SICK_DIR },
	{ XFS_SICK_INO_XATTR,	XFS_BS_SICK_XATTR },
	{ XFS_SICK_INO_SYMLINK,	XFS_BS_SICK_SYMLINK },
	{ XFS_SICK_INO_PARENT,	XFS_BS_SICK_PARENT },
	{ XFS_SICK_INO_BMBTD_ZAPPED,	XFS_BS_SICK_BMBTD },
	{ XFS_SICK_INO_BMBTA_ZAPPED,	XFS_BS_SICK_BMBTA },
	{ XFS_SICK_INO_DIR_ZAPPED,	XFS_BS_SICK_DIR },
	{ XFS_SICK_INO_SYMLINK_ZAPPED,	XFS_BS_SICK_SYMLINK },
	{ XFS_SICK_INO_DIRTREE,	XFS_BS_SICK_DIRTREE },
};

/* Fill out bulkstat health info. */
void
xfs_bulkstat_health(
	struct xfs_inode		*ip,
	struct xfs_bulkstat		*bs)
{
	const struct ioctl_sick_map	*m;
	unsigned int			sick;
	unsigned int			checked;

	bs->bs_sick = 0;
	bs->bs_checked = 0;

	xfs_inode_measure_sickness(ip, &sick, &checked);
	for_each_sick_map(ino_map, m) {
		if (checked & m->sick_mask)
			bs->bs_checked |= m->ioctl_mask;
		if (sick & m->sick_mask)
			bs->bs_sick |= m->ioctl_mask;
	}
}

/* Mark a block mapping sick. */
void
xfs_bmap_mark_sick(
	struct xfs_inode	*ip,
	int			whichfork)
{
	unsigned int		mask;

	switch (whichfork) {
	case XFS_DATA_FORK:
		mask = XFS_SICK_INO_BMBTD;
		break;
	case XFS_ATTR_FORK:
		mask = XFS_SICK_INO_BMBTA;
		break;
	case XFS_COW_FORK:
		mask = XFS_SICK_INO_BMBTC;
		break;
	default:
		ASSERT(0);
		return;
	}

	xfs_inode_mark_sick(ip, mask);
}

/* Record observations of btree corruption with the health tracking system. */
void
xfs_btree_mark_sick(
	struct xfs_btree_cur		*cur)
{
	if (xfs_btree_is_bmap(cur->bc_ops)) {
		xfs_bmap_mark_sick(cur->bc_ino.ip, cur->bc_ino.whichfork);
	/* no health state tracking for ephemeral btrees */
	} else if (cur->bc_ops->type != XFS_BTREE_TYPE_MEM) {
		ASSERT(cur->bc_group);
		ASSERT(cur->bc_ops->sick_mask);
		xfs_group_mark_sick(cur->bc_group, cur->bc_ops->sick_mask);
	}
}

/*
 * Record observations of dir/attr btree corruption with the health tracking
 * system.
 */
void
xfs_dirattr_mark_sick(
	struct xfs_inode	*ip,
	int			whichfork)
{
	unsigned int		mask;

	switch (whichfork) {
	case XFS_DATA_FORK:
		mask = XFS_SICK_INO_DIR;
		break;
	case XFS_ATTR_FORK:
		mask = XFS_SICK_INO_XATTR;
		break;
	default:
		ASSERT(0);
		return;
	}

	xfs_inode_mark_sick(ip, mask);
}

/*
 * Record observations of dir/attr btree corruption with the health tracking
 * system.
 */
void
xfs_da_mark_sick(
	struct xfs_da_args	*args)
{
	xfs_dirattr_mark_sick(args->dp, args->whichfork);
}
