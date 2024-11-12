/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018 Red Hat, Inc.
 */
#ifndef __LIBXFS_GROUP_H
#define __LIBXFS_GROUP_H 1

struct xfs_group {
	struct xfs_mount	*xg_mount;
	uint32_t		xg_gno;
	enum xfs_group_type	xg_type;
	atomic_t		xg_ref;		/* passive reference count */
	atomic_t		xg_active_ref;	/* active reference count */

	/* Precalculated geometry info */
	uint32_t		xg_block_count;	/* max usable gbno */
	uint32_t		xg_min_gbno;	/* min usable gbno */

#ifdef __KERNEL__
	/* -- kernel only structures below this line -- */

	/*
	 * Track freed but not yet committed extents.
	 */
	struct xfs_extent_busy_tree *xg_busy_extents;

	/*
	 * Bitsets of per-ag metadata that have been checked and/or are sick.
	 * Callers should hold xg_state_lock before accessing this field.
	 */
	uint16_t		xg_checked;
	uint16_t		xg_sick;
	spinlock_t		xg_state_lock;

	/*
	 * We use xfs_drain to track the number of deferred log intent items
	 * that have been queued (but not yet processed) so that waiters (e.g.
	 * scrub) will not lock resources when other threads are in the middle
	 * of processing a chain of intent items only to find momentary
	 * inconsistencies.
	 */
	struct xfs_defer_drain	xg_intents_drain;

	/*
	 * Hook to feed rmapbt updates to an active online repair.
	 */
	struct xfs_hooks	xg_rmap_update_hooks;
#endif /* __KERNEL__ */
};

struct xfs_group *xfs_group_get(struct xfs_mount *mp, uint32_t index,
		enum xfs_group_type type);
struct xfs_group *xfs_group_get_by_fsb(struct xfs_mount *mp,
		xfs_fsblock_t fsbno, enum xfs_group_type type);
struct xfs_group *xfs_group_hold(struct xfs_group *xg);
void xfs_group_put(struct xfs_group *xg);

struct xfs_group *xfs_group_grab(struct xfs_mount *mp, uint32_t index,
		enum xfs_group_type type);
struct xfs_group *xfs_group_next_range(struct xfs_mount *mp,
		struct xfs_group *xg, uint32_t start_index, uint32_t end_index,
		enum xfs_group_type type);
struct xfs_group *xfs_group_grab_next_mark(struct xfs_mount *mp,
		struct xfs_group *xg, xa_mark_t mark, enum xfs_group_type type);
void xfs_group_rele(struct xfs_group *xg);

void xfs_group_free(struct xfs_mount *mp, uint32_t index,
		enum xfs_group_type type, void (*uninit)(struct xfs_group *xg));
int xfs_group_insert(struct xfs_mount *mp, struct xfs_group *xg,
		uint32_t index, enum xfs_group_type);

#define xfs_group_set_mark(_xg, _mark) \
	xa_set_mark(&(_xg)->xg_mount->m_groups[(_xg)->xg_type].xa, \
			(_xg)->xg_gno, (_mark))
#define xfs_group_clear_mark(_xg, _mark) \
	xa_clear_mark(&(_xg)->xg_mount->m_groups[(_xg)->xg_type].xa, \
			(_xg)->xg_gno, (_mark))
#define xfs_group_marked(_mp, _type, _mark) \
	xa_marked(&(_mp)->m_groups[(_type)].xa, (_mark))

static inline xfs_agblock_t
xfs_group_max_blocks(
	struct xfs_group	*xg)
{
	return xg->xg_mount->m_groups[xg->xg_type].blocks;
}

static inline xfs_fsblock_t
xfs_group_start_fsb(
	struct xfs_group	*xg)
{
	return ((xfs_fsblock_t)xg->xg_gno) <<
		xg->xg_mount->m_groups[xg->xg_type].blklog;
}

static inline xfs_fsblock_t
xfs_gbno_to_fsb(
	struct xfs_group	*xg,
	xfs_agblock_t		gbno)
{
	return xfs_group_start_fsb(xg) | gbno;
}

static inline xfs_daddr_t
xfs_gbno_to_daddr(
	struct xfs_group	*xg,
	xfs_agblock_t		gbno)
{
	struct xfs_mount	*mp = xg->xg_mount;
	uint32_t		blocks = mp->m_groups[xg->xg_type].blocks;

	return XFS_FSB_TO_BB(mp, (xfs_fsblock_t)xg->xg_gno * blocks + gbno);
}

static inline uint32_t
xfs_fsb_to_gno(
	struct xfs_mount	*mp,
	xfs_fsblock_t		fsbno,
	enum xfs_group_type	type)
{
	if (!mp->m_groups[type].blklog)
		return 0;
	return fsbno >> mp->m_groups[type].blklog;
}

static inline xfs_agblock_t
xfs_fsb_to_gbno(
	struct xfs_mount	*mp,
	xfs_fsblock_t		fsbno,
	enum xfs_group_type	type)
{
	return fsbno & mp->m_groups[type].blkmask;
}

static inline bool
xfs_verify_gbno(
	struct xfs_group	*xg,
	uint32_t		gbno)
{
	if (gbno >= xg->xg_block_count)
		return false;
	if (gbno < xg->xg_min_gbno)
		return false;
	return true;
}

static inline bool
xfs_verify_gbext(
	struct xfs_group	*xg,
	uint32_t		gbno,
	uint32_t		glen)
{
	uint32_t		end;

	if (!xfs_verify_gbno(xg, gbno))
		return false;
	if (glen == 0 || check_add_overflow(gbno, glen - 1, &end))
		return false;
	if (!xfs_verify_gbno(xg, end))
		return false;
	return true;
}

#endif /* __LIBXFS_GROUP_H */
