/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2022-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __LIBXFS_RTGROUP_H
#define __LIBXFS_RTGROUP_H 1

#include "xfs_group.h"

struct xfs_mount;
struct xfs_trans;

/*
 * Realtime group incore structure, similar to the per-AG structure.
 */
struct xfs_rtgroup {
	struct xfs_group	rtg_group;

	/* Number of blocks in this group */
	xfs_rtxnum_t		rtg_extents;
};

static inline struct xfs_rtgroup *to_rtg(struct xfs_group *xg)
{
	return container_of(xg, struct xfs_rtgroup, rtg_group);
}

static inline struct xfs_group *rtg_group(struct xfs_rtgroup *rtg)
{
	return &rtg->rtg_group;
}

static inline struct xfs_mount *rtg_mount(const struct xfs_rtgroup *rtg)
{
	return rtg->rtg_group.xg_mount;
}

static inline xfs_rgnumber_t rtg_rgno(const struct xfs_rtgroup *rtg)
{
	return rtg->rtg_group.xg_gno;
}

/* Passive rtgroup references */
static inline struct xfs_rtgroup *
xfs_rtgroup_get(
	struct xfs_mount	*mp,
	xfs_rgnumber_t		rgno)
{
	return to_rtg(xfs_group_get(mp, rgno, XG_TYPE_RTG));
}

static inline struct xfs_rtgroup *
xfs_rtgroup_hold(
	struct xfs_rtgroup	*rtg)
{
	return to_rtg(xfs_group_hold(rtg_group(rtg)));
}

static inline void
xfs_rtgroup_put(
	struct xfs_rtgroup	*rtg)
{
	xfs_group_put(rtg_group(rtg));
}

/* Active rtgroup references */
static inline struct xfs_rtgroup *
xfs_rtgroup_grab(
	struct xfs_mount	*mp,
	xfs_rgnumber_t		rgno)
{
	return to_rtg(xfs_group_grab(mp, rgno, XG_TYPE_RTG));
}

static inline void
xfs_rtgroup_rele(
	struct xfs_rtgroup	*rtg)
{
	xfs_group_rele(rtg_group(rtg));
}

static inline struct xfs_rtgroup *
xfs_rtgroup_next_range(
	struct xfs_mount	*mp,
	struct xfs_rtgroup	*rtg,
	xfs_rgnumber_t		start_rgno,
	xfs_rgnumber_t		end_rgno)
{
	return to_rtg(xfs_group_next_range(mp, rtg ? rtg_group(rtg) : NULL,
			start_rgno, end_rgno, XG_TYPE_RTG));
}

static inline struct xfs_rtgroup *
xfs_rtgroup_next(
	struct xfs_mount	*mp,
	struct xfs_rtgroup	*rtg)
{
	return xfs_rtgroup_next_range(mp, rtg, 0, mp->m_sb.sb_rgcount - 1);
}

static inline xfs_rtblock_t
xfs_rgno_start_rtb(
	struct xfs_mount	*mp,
	xfs_rgnumber_t		rgno)
{
	if (mp->m_rgblklog >= 0)
		return ((xfs_rtblock_t)rgno << mp->m_rgblklog);
	return ((xfs_rtblock_t)rgno * mp->m_rgblocks);
}

static inline xfs_rtblock_t
__xfs_rgbno_to_rtb(
	struct xfs_mount	*mp,
	xfs_rgnumber_t		rgno,
	xfs_rgblock_t		rgbno)
{
	return xfs_rgno_start_rtb(mp, rgno) + rgbno;
}

static inline xfs_rtblock_t
xfs_rgbno_to_rtb(
	struct xfs_rtgroup	*rtg,
	xfs_rgblock_t		rgbno)
{
	return __xfs_rgbno_to_rtb(rtg_mount(rtg), rtg_rgno(rtg), rgbno);
}

static inline xfs_rgnumber_t
xfs_rtb_to_rgno(
	struct xfs_mount	*mp,
	xfs_rtblock_t		rtbno)
{
	if (!xfs_has_rtgroups(mp))
		return 0;

	if (mp->m_rgblklog >= 0)
		return rtbno >> mp->m_rgblklog;

	return div_u64(rtbno, mp->m_rgblocks);
}

static inline uint64_t
__xfs_rtb_to_rgbno(
	struct xfs_mount	*mp,
	xfs_rtblock_t		rtbno)
{
	uint32_t		rem;

	if (!xfs_has_rtgroups(mp))
		return rtbno;

	if (mp->m_rgblklog >= 0)
		return rtbno & mp->m_rgblkmask;

	div_u64_rem(rtbno, mp->m_rgblocks, &rem);
	return rem;
}

static inline xfs_rgblock_t
xfs_rtb_to_rgbno(
	struct xfs_mount	*mp,
	xfs_rtblock_t		rtbno)
{
	return __xfs_rtb_to_rgbno(mp, rtbno);
}

static inline xfs_daddr_t
xfs_rtb_to_daddr(
	struct xfs_mount	*mp,
	xfs_rtblock_t		rtbno)
{
	return rtbno << mp->m_blkbb_log;
}

static inline xfs_rtblock_t
xfs_daddr_to_rtb(
	struct xfs_mount	*mp,
	xfs_daddr_t		daddr)
{
	return daddr >> mp->m_blkbb_log;
}

#ifdef CONFIG_XFS_RT
int xfs_rtgroup_alloc(struct xfs_mount *mp, xfs_rgnumber_t rgno,
		xfs_rgnumber_t rgcount, xfs_rtbxlen_t rextents);
void xfs_rtgroup_free(struct xfs_mount *mp, xfs_rgnumber_t rgno);

void xfs_free_rtgroups(struct xfs_mount *mp, xfs_rgnumber_t first_rgno,
		xfs_rgnumber_t end_rgno);
int xfs_initialize_rtgroups(struct xfs_mount *mp, xfs_rgnumber_t first_rgno,
		xfs_rgnumber_t end_rgno, xfs_rtbxlen_t rextents);

xfs_rtxnum_t __xfs_rtgroup_extents(struct xfs_mount *mp, xfs_rgnumber_t rgno,
		xfs_rgnumber_t rgcount, xfs_rtbxlen_t rextents);
xfs_rtxnum_t xfs_rtgroup_extents(struct xfs_mount *mp, xfs_rgnumber_t rgno);

int xfs_update_last_rtgroup_size(struct xfs_mount *mp,
		xfs_rgnumber_t prev_rgcount);
#else
static inline void xfs_free_rtgroups(struct xfs_mount *mp,
		xfs_rgnumber_t first_rgno, xfs_rgnumber_t end_rgno)
{
}

static inline int xfs_initialize_rtgroups(struct xfs_mount *mp,
		xfs_rgnumber_t first_rgno, xfs_rgnumber_t end_rgno,
		xfs_rtbxlen_t rextents)
{
	return 0;
}

# define xfs_rtgroup_extents(mp, rgno)		(0)
# define xfs_update_last_rtgroup_size(mp, rgno)	(-EOPNOTSUPP)
#endif /* CONFIG_XFS_RT */

#endif /* __LIBXFS_RTGROUP_H */
