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
