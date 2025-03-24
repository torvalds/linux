// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_ag.h"
#include "xfs_trace.h"

/*
 * Use a static key here to reduce the overhead of xfs_defer_drain_rele.  If
 * the compiler supports jump labels, the static branch will be replaced by a
 * nop sled when there are no xfs_defer_drain_wait callers.  Online fsck is
 * currently the only caller, so this is a reasonable tradeoff.
 *
 * Note: Patching the kernel code requires taking the cpu hotplug lock.  Other
 * parts of the kernel allocate memory with that lock held, which means that
 * XFS callers cannot hold any locks that might be used by memory reclaim or
 * writeback when calling the static_branch_{inc,dec} functions.
 */
static DEFINE_STATIC_KEY_FALSE(xfs_defer_drain_waiter_gate);

void
xfs_defer_drain_wait_disable(void)
{
	static_branch_dec(&xfs_defer_drain_waiter_gate);
}

void
xfs_defer_drain_wait_enable(void)
{
	static_branch_inc(&xfs_defer_drain_waiter_gate);
}

void
xfs_defer_drain_init(
	struct xfs_defer_drain	*dr)
{
	atomic_set(&dr->dr_count, 0);
	init_waitqueue_head(&dr->dr_waiters);
}

void
xfs_defer_drain_free(struct xfs_defer_drain	*dr)
{
	ASSERT(atomic_read(&dr->dr_count) == 0);
}

/* Increase the pending intent count. */
static inline void xfs_defer_drain_grab(struct xfs_defer_drain *dr)
{
	atomic_inc(&dr->dr_count);
}

static inline bool has_waiters(struct wait_queue_head *wq_head)
{
	/*
	 * This memory barrier is paired with the one in set_current_state on
	 * the waiting side.
	 */
	smp_mb__after_atomic();
	return waitqueue_active(wq_head);
}

/* Decrease the pending intent count, and wake any waiters, if appropriate. */
static inline void xfs_defer_drain_rele(struct xfs_defer_drain *dr)
{
	if (atomic_dec_and_test(&dr->dr_count) &&
	    static_branch_unlikely(&xfs_defer_drain_waiter_gate) &&
	    has_waiters(&dr->dr_waiters))
		wake_up(&dr->dr_waiters);
}

/* Are there intents pending? */
static inline bool xfs_defer_drain_busy(struct xfs_defer_drain *dr)
{
	return atomic_read(&dr->dr_count) > 0;
}

/*
 * Wait for the pending intent count for a drain to hit zero.
 *
 * Callers must not hold any locks that would prevent intents from being
 * finished.
 */
static inline int xfs_defer_drain_wait(struct xfs_defer_drain *dr)
{
	return wait_event_killable(dr->dr_waiters, !xfs_defer_drain_busy(dr));
}

/*
 * Get a passive reference to the group that contains a fsbno and declare an
 * intent to update its metadata.
 *
 * Other threads that need exclusive access can decide to back off if they see
 * declared intentions.
 */
struct xfs_group *
xfs_group_intent_get(
	struct xfs_mount	*mp,
	xfs_fsblock_t		fsbno,
	enum xfs_group_type	type)
{
	struct xfs_group	*xg;

	xg = xfs_group_get_by_fsb(mp, fsbno, type);
	if (!xg)
		return NULL;
	trace_xfs_group_intent_hold(xg, __return_address);
	xfs_defer_drain_grab(&xg->xg_intents_drain);
	return xg;
}

/*
 * Release our intent to update this groups metadata, and then release our
 * passive ref to it.
 */
void
xfs_group_intent_put(
	struct xfs_group	*xg)
{
	trace_xfs_group_intent_rele(xg, __return_address);
	xfs_defer_drain_rele(&xg->xg_intents_drain);
	xfs_group_put(xg);
}

/*
 * Wait for the intent update count for this AG to hit zero.
 * Callers must not hold any AG header buffers.
 */
int
xfs_group_intent_drain(
	struct xfs_group	*xg)
{
	trace_xfs_group_wait_intents(xg, __return_address);
	return xfs_defer_drain_wait(&xg->xg_intents_drain);
}

/*
 * Has anyone declared an intent to update this group?
 */
bool
xfs_group_intent_busy(
	struct xfs_group	*xg)
{
	return xfs_defer_drain_busy(&xg->xg_intents_drain);
}
