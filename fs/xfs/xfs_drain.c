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
 * Use a static key here to reduce the overhead of xfs_drain_rele.  If the
 * compiler supports jump labels, the static branch will be replaced by a nop
 * sled when there are no xfs_drain_wait callers.  Online fsck is currently
 * the only caller, so this is a reasonable tradeoff.
 *
 * Note: Patching the kernel code requires taking the cpu hotplug lock.  Other
 * parts of the kernel allocate memory with that lock held, which means that
 * XFS callers cannot hold any locks that might be used by memory reclaim or
 * writeback when calling the static_branch_{inc,dec} functions.
 */
static DEFINE_STATIC_KEY_FALSE(xfs_drain_waiter_gate);

void
xfs_drain_wait_disable(void)
{
	static_branch_dec(&xfs_drain_waiter_gate);
}

void
xfs_drain_wait_enable(void)
{
	static_branch_inc(&xfs_drain_waiter_gate);
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
	    static_branch_unlikely(&xfs_drain_waiter_gate) &&
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
 * Get a passive reference to the AG that contains a fsbno and declare an intent
 * to update its metadata.
 */
struct xfs_perag *
xfs_perag_intent_get(
	struct xfs_mount	*mp,
	xfs_fsblock_t		fsbno)
{
	struct xfs_perag	*pag;

	pag = xfs_perag_get(mp, XFS_FSB_TO_AGNO(mp, fsbno));
	if (!pag)
		return NULL;

	xfs_perag_intent_hold(pag);
	return pag;
}

/*
 * Release our intent to update this AG's metadata, and then release our
 * passive ref to the AG.
 */
void
xfs_perag_intent_put(
	struct xfs_perag	*pag)
{
	xfs_perag_intent_rele(pag);
	xfs_perag_put(pag);
}

/*
 * Declare an intent to update AG metadata.  Other threads that need exclusive
 * access can decide to back off if they see declared intentions.
 */
void
xfs_perag_intent_hold(
	struct xfs_perag	*pag)
{
	trace_xfs_perag_intent_hold(pag, __return_address);
	xfs_defer_drain_grab(&pag->pag_intents_drain);
}

/* Release our intent to update this AG's metadata. */
void
xfs_perag_intent_rele(
	struct xfs_perag	*pag)
{
	trace_xfs_perag_intent_rele(pag, __return_address);
	xfs_defer_drain_rele(&pag->pag_intents_drain);
}

/*
 * Wait for the intent update count for this AG to hit zero.
 * Callers must not hold any AG header buffers.
 */
int
xfs_perag_intent_drain(
	struct xfs_perag	*pag)
{
	trace_xfs_perag_wait_intents(pag, __return_address);
	return xfs_defer_drain_wait(&pag->pag_intents_drain);
}

/* Has anyone declared an intent to update this AG? */
bool
xfs_perag_intent_busy(
	struct xfs_perag	*pag)
{
	return xfs_defer_drain_busy(&pag->pag_intents_drain);
}
