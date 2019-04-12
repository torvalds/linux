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
#include "xfs_bit.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"
#include "xfs_inode.h"
#include "xfs_trace.h"
#include "xfs_health.h"

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

	if (XFS_FORCED_SHUTDOWN(mp))
		return;

	/* Measure AG corruption levels. */
	for (agno = 0; agno < mp->m_sb.sb_agcount; agno++) {
		pag = xfs_perag_get(mp, agno);
		xfs_ag_measure_sickness(pag, &sick, &checked);
		if (sick) {
			trace_xfs_ag_unfixed_corruption(mp, agno, sick);
			warn = true;
		}
		xfs_perag_put(pag);
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
