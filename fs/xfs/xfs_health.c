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

/*
 * Warn about metadata corruption that we detected but haven't fixed, and
 * make sure we're not sitting on anything that would get in the way of
 * recovery.
 */
void
xfs_health_unmount(
	struct xfs_mount	*mp)
{
	struct xfs_perag	*pag;
	xfs_agnumber_t		agno;
	unsigned int		sick = 0;
	unsigned int		checked = 0;
	bool			warn = false;

	if (xfs_is_shutdown(mp))
		return;

	/* Measure AG corruption levels. */
	for_each_perag(mp, agno, pag) {
		xfs_ag_measure_sickness(pag, &sick, &checked);
		if (sick) {
			trace_xfs_ag_unfixed_corruption(mp, agno, sick);
			warn = true;
		}
	}

	/* Measure realtime volume corruption levels. */
	xfs_rt_measure_sickness(mp, &sick, &checked);
	if (sick) {
		trace_xfs_rt_unfixed_corruption(mp, sick);
		warn = true;
	}

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
	ASSERT(!(mask & ~XFS_SICK_FS_PRIMARY));
	trace_xfs_fs_mark_sick(mp, mask);

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
	ASSERT(!(mask & ~XFS_SICK_FS_PRIMARY));
	trace_xfs_fs_mark_healthy(mp, mask);

	spin_lock(&mp->m_sb_lock);
	mp->m_fs_sick &= ~mask;
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

/* Mark unhealthy realtime metadata. */
void
xfs_rt_mark_sick(
	struct xfs_mount	*mp,
	unsigned int		mask)
{
	ASSERT(!(mask & ~XFS_SICK_RT_PRIMARY));
	trace_xfs_rt_mark_sick(mp, mask);

	spin_lock(&mp->m_sb_lock);
	mp->m_rt_sick |= mask;
	mp->m_rt_checked |= mask;
	spin_unlock(&mp->m_sb_lock);
}

/* Mark a realtime metadata healed. */
void
xfs_rt_mark_healthy(
	struct xfs_mount	*mp,
	unsigned int		mask)
{
	ASSERT(!(mask & ~XFS_SICK_RT_PRIMARY));
	trace_xfs_rt_mark_healthy(mp, mask);

	spin_lock(&mp->m_sb_lock);
	mp->m_rt_sick &= ~mask;
	mp->m_rt_checked |= mask;
	spin_unlock(&mp->m_sb_lock);
}

/* Sample which realtime metadata are unhealthy. */
void
xfs_rt_measure_sickness(
	struct xfs_mount	*mp,
	unsigned int		*sick,
	unsigned int		*checked)
{
	spin_lock(&mp->m_sb_lock);
	*sick = mp->m_rt_sick;
	*checked = mp->m_rt_checked;
	spin_unlock(&mp->m_sb_lock);
}

/* Mark unhealthy per-ag metadata. */
void
xfs_ag_mark_sick(
	struct xfs_perag	*pag,
	unsigned int		mask)
{
	ASSERT(!(mask & ~XFS_SICK_AG_PRIMARY));
	trace_xfs_ag_mark_sick(pag->pag_mount, pag->pag_agno, mask);

	spin_lock(&pag->pag_state_lock);
	pag->pag_sick |= mask;
	pag->pag_checked |= mask;
	spin_unlock(&pag->pag_state_lock);
}

/* Mark per-ag metadata ok. */
void
xfs_ag_mark_healthy(
	struct xfs_perag	*pag,
	unsigned int		mask)
{
	ASSERT(!(mask & ~XFS_SICK_AG_PRIMARY));
	trace_xfs_ag_mark_healthy(pag->pag_mount, pag->pag_agno, mask);

	spin_lock(&pag->pag_state_lock);
	pag->pag_sick &= ~mask;
	pag->pag_checked |= mask;
	spin_unlock(&pag->pag_state_lock);
}

/* Sample which per-ag metadata are unhealthy. */
void
xfs_ag_measure_sickness(
	struct xfs_perag	*pag,
	unsigned int		*sick,
	unsigned int		*checked)
{
	spin_lock(&pag->pag_state_lock);
	*sick = pag->pag_sick;
	*checked = pag->pag_checked;
	spin_unlock(&pag->pag_state_lock);
}

/* Mark the unhealthy parts of an inode. */
void
xfs_inode_mark_sick(
	struct xfs_inode	*ip,
	unsigned int		mask)
{
	ASSERT(!(mask & ~XFS_SICK_INO_PRIMARY));
	trace_xfs_inode_mark_sick(ip, mask);

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
	ASSERT(!(mask & ~XFS_SICK_INO_PRIMARY));
	trace_xfs_inode_mark_healthy(ip, mask);

	spin_lock(&ip->i_flags_lock);
	ip->i_sick &= ~mask;
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

static const struct ioctl_sick_map fs_map[] = {
	{ XFS_SICK_FS_COUNTERS,	XFS_FSOP_GEOM_SICK_COUNTERS},
	{ XFS_SICK_FS_UQUOTA,	XFS_FSOP_GEOM_SICK_UQUOTA },
	{ XFS_SICK_FS_GQUOTA,	XFS_FSOP_GEOM_SICK_GQUOTA },
	{ XFS_SICK_FS_PQUOTA,	XFS_FSOP_GEOM_SICK_PQUOTA },
	{ 0, 0 },
};

static const struct ioctl_sick_map rt_map[] = {
	{ XFS_SICK_RT_BITMAP,	XFS_FSOP_GEOM_SICK_RT_BITMAP },
	{ XFS_SICK_RT_SUMMARY,	XFS_FSOP_GEOM_SICK_RT_SUMMARY },
	{ 0, 0 },
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
	const struct ioctl_sick_map	*m;
	unsigned int			sick;
	unsigned int			checked;

	geo->sick = 0;
	geo->checked = 0;

	xfs_fs_measure_sickness(mp, &sick, &checked);
	for (m = fs_map; m->sick_mask; m++)
		xfgeo_health_tick(geo, sick, checked, m);

	xfs_rt_measure_sickness(mp, &sick, &checked);
	for (m = rt_map; m->sick_mask; m++)
		xfgeo_health_tick(geo, sick, checked, m);
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
	{ 0, 0 },
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

	xfs_ag_measure_sickness(pag, &sick, &checked);
	for (m = ag_map; m->sick_mask; m++) {
		if (checked & m->sick_mask)
			ageo->ag_checked |= m->ioctl_mask;
		if (sick & m->sick_mask)
			ageo->ag_sick |= m->ioctl_mask;
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
	{ 0, 0 },
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
	for (m = ino_map; m->sick_mask; m++) {
		if (checked & m->sick_mask)
			bs->bs_checked |= m->ioctl_mask;
		if (sick & m->sick_mask)
			bs->bs_sick |= m->ioctl_mask;
	}
}
