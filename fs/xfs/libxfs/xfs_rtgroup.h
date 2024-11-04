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

enum xfs_rtg_inodes {
	XFS_RTGI_BITMAP,	/* allocation bitmap */
	XFS_RTGI_SUMMARY,	/* allocation summary */

	XFS_RTGI_MAX,
};

#ifdef MAX_LOCKDEP_SUBCLASSES
static_assert(XFS_RTGI_MAX <= MAX_LOCKDEP_SUBCLASSES);
#endif

/*
 * Realtime group incore structure, similar to the per-AG structure.
 */
struct xfs_rtgroup {
	struct xfs_group	rtg_group;

	/* per-rtgroup metadata inodes */
	struct xfs_inode	*rtg_inodes[XFS_RTGI_MAX];

	/* Number of blocks in this group */
	xfs_rtxnum_t		rtg_extents;

	/*
	 * Cache of rt summary level per bitmap block with the invariant that
	 * rtg_rsum_cache[bbno] > the maximum i for which rsum[i][bbno] != 0,
	 * or 0 if rsum[i][bbno] == 0 for all i.
	 *
	 * Reads and writes are serialized by the rsumip inode lock.
	 */
	uint8_t			*rtg_rsum_cache;
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
xfs_rgbno_to_rtb(
	struct xfs_rtgroup	*rtg,
	xfs_rgblock_t		rgbno)
{
	return xfs_gbno_to_fsb(rtg_group(rtg), rgbno);
}

static inline xfs_rgnumber_t
xfs_rtb_to_rgno(
	struct xfs_mount	*mp,
	xfs_rtblock_t		rtbno)
{
	return xfs_fsb_to_gno(mp, rtbno, XG_TYPE_RTG);
}

static inline xfs_rgblock_t
xfs_rtb_to_rgbno(
	struct xfs_mount	*mp,
	xfs_rtblock_t		rtbno)
{
	return xfs_fsb_to_gbno(mp, rtbno, XG_TYPE_RTG);
}

/* Is rtbno the start of a RT group? */
static inline bool
xfs_rtbno_is_group_start(
	struct xfs_mount	*mp,
	xfs_rtblock_t		rtbno)
{
	return (rtbno & mp->m_groups[XG_TYPE_RTG].blkmask) == 0;
}

/* Convert an rtgroups rt extent number into an rgbno. */
static inline xfs_rgblock_t
xfs_rtx_to_rgbno(
	struct xfs_rtgroup	*rtg,
	xfs_rtxnum_t		rtx)
{
	struct xfs_mount	*mp = rtg_mount(rtg);

	if (likely(mp->m_rtxblklog >= 0))
		return rtx << mp->m_rtxblklog;
	return rtx * mp->m_sb.sb_rextsize;
}

static inline xfs_daddr_t
xfs_rtb_to_daddr(
	struct xfs_mount	*mp,
	xfs_rtblock_t		rtbno)
{
	struct xfs_groups	*g = &mp->m_groups[XG_TYPE_RTG];
	xfs_rgnumber_t		rgno = xfs_rtb_to_rgno(mp, rtbno);
	uint64_t		start_bno = (xfs_rtblock_t)rgno * g->blocks;

	return XFS_FSB_TO_BB(mp, start_bno + (rtbno & g->blkmask));
}

static inline xfs_rtblock_t
xfs_daddr_to_rtb(
	struct xfs_mount	*mp,
	xfs_daddr_t		daddr)
{
	xfs_rfsblock_t		bno = XFS_BB_TO_FSBT(mp, daddr);

	if (xfs_has_rtgroups(mp)) {
		struct xfs_groups *g = &mp->m_groups[XG_TYPE_RTG];
		xfs_rgnumber_t	rgno;
		uint32_t	rgbno;

		rgno = div_u64_rem(bno, g->blocks, &rgbno);
		return ((xfs_rtblock_t)rgno << g->blklog) + rgbno;
	}

	return bno;
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
void xfs_rtgroup_calc_geometry(struct xfs_mount *mp, struct xfs_rtgroup *rtg,
		xfs_rgnumber_t rgno, xfs_rgnumber_t rgcount,
		xfs_rtbxlen_t rextents);

int xfs_update_last_rtgroup_size(struct xfs_mount *mp,
		xfs_rgnumber_t prev_rgcount);

/* Lock the rt bitmap inode in exclusive mode */
#define XFS_RTGLOCK_BITMAP		(1U << 0)
/* Lock the rt bitmap inode in shared mode */
#define XFS_RTGLOCK_BITMAP_SHARED	(1U << 1)

#define XFS_RTGLOCK_ALL_FLAGS	(XFS_RTGLOCK_BITMAP | \
				 XFS_RTGLOCK_BITMAP_SHARED)

void xfs_rtgroup_lock(struct xfs_rtgroup *rtg, unsigned int rtglock_flags);
void xfs_rtgroup_unlock(struct xfs_rtgroup *rtg, unsigned int rtglock_flags);
void xfs_rtgroup_trans_join(struct xfs_trans *tp, struct xfs_rtgroup *rtg,
		unsigned int rtglock_flags);

int xfs_rtgroup_get_geometry(struct xfs_rtgroup *rtg,
		struct xfs_rtgroup_geometry *rgeo);

int xfs_rtginode_mkdir_parent(struct xfs_mount *mp);
int xfs_rtginode_load_parent(struct xfs_trans *tp);

const char *xfs_rtginode_name(enum xfs_rtg_inodes type);
enum xfs_metafile_type xfs_rtginode_metafile_type(enum xfs_rtg_inodes type);
bool xfs_rtginode_enabled(struct xfs_rtgroup *rtg, enum xfs_rtg_inodes type);
void xfs_rtginode_mark_sick(struct xfs_rtgroup *rtg, enum xfs_rtg_inodes type);
int xfs_rtginode_load(struct xfs_rtgroup *rtg, enum xfs_rtg_inodes type,
		struct xfs_trans *tp);
int xfs_rtginode_create(struct xfs_rtgroup *rtg, enum xfs_rtg_inodes type,
		bool init);
void xfs_rtginode_irele(struct xfs_inode **ipp);

static inline const char *xfs_rtginode_path(xfs_rgnumber_t rgno,
		enum xfs_rtg_inodes type)
{
	return kasprintf(GFP_KERNEL, "%u.%s", rgno, xfs_rtginode_name(type));
}

void xfs_update_rtsb(struct xfs_buf *rtsb_bp,
		const struct xfs_buf *sb_bp);
struct xfs_buf *xfs_log_rtsb(struct xfs_trans *tp,
		const struct xfs_buf *sb_bp);
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
# define xfs_rtgroup_lock(rtg, gf)		((void)0)
# define xfs_rtgroup_unlock(rtg, gf)		((void)0)
# define xfs_rtgroup_trans_join(tp, rtg, gf)	((void)0)
# define xfs_update_rtsb(bp, sb_bp)	((void)0)
# define xfs_log_rtsb(tp, sb_bp)	(NULL)
# define xfs_rtgroup_get_geometry(rtg, rgeo)	(-EOPNOTSUPP)
#endif /* CONFIG_XFS_RT */

#endif /* __LIBXFS_RTGROUP_H */
