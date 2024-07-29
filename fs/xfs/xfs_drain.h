// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef XFS_DRAIN_H_
#define XFS_DRAIN_H_

struct xfs_perag;

#ifdef CONFIG_XFS_DRAIN_INTENTS
/*
 * Passive drain mechanism.  This data structure tracks a count of some items
 * and contains a waitqueue for callers who would like to wake up when the
 * count hits zero.
 */
struct xfs_defer_drain {
	/* Number of items pending in some part of the filesystem. */
	atomic_t		dr_count;

	/* Queue to wait for dri_count to go to zero */
	struct wait_queue_head	dr_waiters;
};

void xfs_defer_drain_init(struct xfs_defer_drain *dr);
void xfs_defer_drain_free(struct xfs_defer_drain *dr);

void xfs_drain_wait_disable(void);
void xfs_drain_wait_enable(void);

/*
 * Deferred Work Intent Drains
 * ===========================
 *
 * When a writer thread executes a chain of log intent items, the AG header
 * buffer locks will cycle during a transaction roll to get from one intent
 * item to the next in a chain.  Although scrub takes all AG header buffer
 * locks, this isn't sufficient to guard against scrub checking an AG while
 * that writer thread is in the middle of finishing a chain because there's no
 * higher level locking primitive guarding allocation groups.
 *
 * When there's a collision, cross-referencing between data structures (e.g.
 * rmapbt and refcountbt) yields false corruption events; if repair is running,
 * this results in incorrect repairs, which is catastrophic.
 *
 * The solution is to the perag structure the count of active intents and make
 * scrub wait until it has both AG header buffer locks and the intent counter
 * reaches zero.  It is therefore critical that deferred work threads hold the
 * AGI or AGF buffers when decrementing the intent counter.
 *
 * Given a list of deferred work items, the deferred work manager will complete
 * a work item and all the sub-items that the parent item creates before moving
 * on to the next work item in the list.  This is also true for all levels of
 * sub-items.  Writer threads are permitted to queue multiple work items
 * targetting the same AG, so a deferred work item (such as a BUI) that creates
 * sub-items (such as RUIs) must bump the intent counter and maintain it until
 * the sub-items can themselves bump the intent counter.
 *
 * Therefore, the intent count tracks entire lifetimes of deferred work items.
 * All functions that create work items must increment the intent counter as
 * soon as the item is added to the transaction and cannot drop the counter
 * until the item is finished or cancelled.
 */
struct xfs_perag *xfs_perag_intent_get(struct xfs_mount *mp,
		xfs_fsblock_t fsbno);
void xfs_perag_intent_put(struct xfs_perag *pag);

void xfs_perag_intent_hold(struct xfs_perag *pag);
void xfs_perag_intent_rele(struct xfs_perag *pag);

int xfs_perag_intent_drain(struct xfs_perag *pag);
bool xfs_perag_intent_busy(struct xfs_perag *pag);
#else
struct xfs_defer_drain { /* empty */ };

#define xfs_defer_drain_free(dr)		((void)0)
#define xfs_defer_drain_init(dr)		((void)0)

#define xfs_perag_intent_get(mp, fsbno) \
	xfs_perag_get((mp), XFS_FSB_TO_AGNO(mp, fsbno))
#define xfs_perag_intent_put(pag)		xfs_perag_put(pag)

static inline void xfs_perag_intent_hold(struct xfs_perag *pag) { }
static inline void xfs_perag_intent_rele(struct xfs_perag *pag) { }

#endif /* CONFIG_XFS_DRAIN_INTENTS */

#endif /* XFS_DRAIN_H_ */
