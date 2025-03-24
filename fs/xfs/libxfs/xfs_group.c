// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Red Hat, Inc.
 */

#include "xfs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_error.h"
#include "xfs_trace.h"
#include "xfs_extent_busy.h"
#include "xfs_group.h"

/*
 * Groups can have passive and active references.
 *
 * For passive references the code freeing a group is responsible for cleaning
 * up objects that hold the passive references (e.g. cached buffers).
 * Routines manipulating passive references are xfs_group_get, xfs_group_hold
 * and xfs_group_put.
 *
 * Active references are for short term access to the group for walking trees or
 * accessing state. If a group is being shrunk or offlined, the lookup will fail
 * to find that group and return NULL instead.
 * Routines manipulating active references are xfs_group_grab and
 * xfs_group_rele.
 */

struct xfs_group *
xfs_group_get(
	struct xfs_mount	*mp,
	uint32_t		index,
	enum xfs_group_type	type)
{
	struct xfs_group	*xg;

	rcu_read_lock();
	xg = xa_load(&mp->m_groups[type].xa, index);
	if (xg) {
		trace_xfs_group_get(xg, _RET_IP_);
		ASSERT(atomic_read(&xg->xg_ref) >= 0);
		atomic_inc(&xg->xg_ref);
	}
	rcu_read_unlock();
	return xg;
}

struct xfs_group *
xfs_group_hold(
	struct xfs_group	*xg)
{
	ASSERT(atomic_read(&xg->xg_ref) > 0 ||
	       atomic_read(&xg->xg_active_ref) > 0);

	trace_xfs_group_hold(xg, _RET_IP_);
	atomic_inc(&xg->xg_ref);
	return xg;
}

void
xfs_group_put(
	struct xfs_group	*xg)
{
	trace_xfs_group_put(xg, _RET_IP_);

	ASSERT(atomic_read(&xg->xg_ref) > 0);
	atomic_dec(&xg->xg_ref);
}

struct xfs_group *
xfs_group_grab(
	struct xfs_mount	*mp,
	uint32_t		index,
	enum xfs_group_type	type)
{
	struct xfs_group	*xg;

	rcu_read_lock();
	xg = xa_load(&mp->m_groups[type].xa, index);
	if (xg) {
		trace_xfs_group_grab(xg, _RET_IP_);
		if (!atomic_inc_not_zero(&xg->xg_active_ref))
			xg = NULL;
	}
	rcu_read_unlock();
	return xg;
}

/*
 * Iterate to the next group.  To start the iteration at @start_index, a %NULL
 * @xg is passed, else the previous group returned from this function.  The
 * caller should break out of the loop when this returns %NULL.  If the caller
 * wants to break out of a loop that did not finish it needs to release the
 * active reference to @xg using xfs_group_rele() itself.
 */
struct xfs_group *
xfs_group_next_range(
	struct xfs_mount	*mp,
	struct xfs_group	*xg,
	uint32_t		start_index,
	uint32_t		end_index,
	enum xfs_group_type	type)
{
	uint32_t		index = start_index;

	if (xg) {
		index = xg->xg_gno + 1;
		xfs_group_rele(xg);
	}
	if (index > end_index)
		return NULL;
	return xfs_group_grab(mp, index, type);
}

/*
 * Find the next group after @xg, or the first group if @xg is NULL.
 */
struct xfs_group *
xfs_group_grab_next_mark(
	struct xfs_mount	*mp,
	struct xfs_group	*xg,
	xa_mark_t		mark,
	enum xfs_group_type	type)
{
	unsigned long		index = 0;

	if (xg) {
		index = xg->xg_gno + 1;
		xfs_group_rele(xg);
	}

	rcu_read_lock();
	xg = xa_find(&mp->m_groups[type].xa, &index, ULONG_MAX, mark);
	if (xg) {
		trace_xfs_group_grab_next_tag(xg, _RET_IP_);
		if (!atomic_inc_not_zero(&xg->xg_active_ref))
			xg = NULL;
	}
	rcu_read_unlock();
	return xg;
}

void
xfs_group_rele(
	struct xfs_group	*xg)
{
	trace_xfs_group_rele(xg, _RET_IP_);
	atomic_dec(&xg->xg_active_ref);
}

void
xfs_group_free(
	struct xfs_mount	*mp,
	uint32_t		index,
	enum xfs_group_type	type,
	void			(*uninit)(struct xfs_group *xg))
{
	struct xfs_group	*xg = xa_erase(&mp->m_groups[type].xa, index);

	XFS_IS_CORRUPT(mp, atomic_read(&xg->xg_ref) != 0);

	xfs_defer_drain_free(&xg->xg_intents_drain);
#ifdef __KERNEL__
	kfree(xg->xg_busy_extents);
#endif

	if (uninit)
		uninit(xg);

	/* drop the mount's active reference */
	xfs_group_rele(xg);
	XFS_IS_CORRUPT(mp, atomic_read(&xg->xg_active_ref) != 0);
	kfree_rcu_mightsleep(xg);
}

int
xfs_group_insert(
	struct xfs_mount	*mp,
	struct xfs_group	*xg,
	uint32_t		index,
	enum xfs_group_type	type)
{
	int			error;

	xg->xg_mount = mp;
	xg->xg_gno = index;
	xg->xg_type = type;

#ifdef __KERNEL__
	xg->xg_busy_extents = xfs_extent_busy_alloc();
	if (!xg->xg_busy_extents)
		return -ENOMEM;
	spin_lock_init(&xg->xg_state_lock);
	xfs_hooks_init(&xg->xg_rmap_update_hooks);
#endif
	xfs_defer_drain_init(&xg->xg_intents_drain);

	/* Active ref owned by mount indicates group is online. */
	atomic_set(&xg->xg_active_ref, 1);

	error = xa_insert(&mp->m_groups[type].xa, index, xg, GFP_KERNEL);
	if (error) {
		WARN_ON_ONCE(error == -EBUSY);
		goto out_drain;
	}

	return 0;
out_drain:
	xfs_defer_drain_free(&xg->xg_intents_drain);
#ifdef __KERNEL__
	kfree(xg->xg_busy_extents);
#endif
	return error;
}

struct xfs_group *
xfs_group_get_by_fsb(
	struct xfs_mount	*mp,
	xfs_fsblock_t		fsbno,
	enum xfs_group_type	type)
{
	return xfs_group_get(mp, xfs_fsb_to_gno(mp, fsbno, type), type);
}
