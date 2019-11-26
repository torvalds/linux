// SPDX-License-Identifier: GPL-2.0+
/*
 * RCU segmented callback lists, function definitions
 *
 * Copyright IBM Corporation, 2017
 *
 * Authors: Paul E. McKenney <paulmck@linux.ibm.com>
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/rcupdate.h>

#include "rcu_segcblist.h"

/* Initialize simple callback list. */
void rcu_cblist_init(struct rcu_cblist *rclp)
{
	rclp->head = NULL;
	rclp->tail = &rclp->head;
	rclp->len = 0;
	rclp->len_lazy = 0;
}

/*
 * Enqueue an rcu_head structure onto the specified callback list.
 * This function assumes that the callback is non-lazy because it
 * is intended for use by no-CBs CPUs, which do not distinguish
 * between lazy and non-lazy RCU callbacks.
 */
void rcu_cblist_enqueue(struct rcu_cblist *rclp, struct rcu_head *rhp)
{
	*rclp->tail = rhp;
	rclp->tail = &rhp->next;
	WRITE_ONCE(rclp->len, rclp->len + 1);
}

/*
 * Flush the second rcu_cblist structure onto the first one, obliterating
 * any contents of the first.  If rhp is non-NULL, enqueue it as the sole
 * element of the second rcu_cblist structure, but ensuring that the second
 * rcu_cblist structure, if initially non-empty, always appears non-empty
 * throughout the process.  If rdp is NULL, the second rcu_cblist structure
 * is instead initialized to empty.
 */
void rcu_cblist_flush_enqueue(struct rcu_cblist *drclp,
			      struct rcu_cblist *srclp,
			      struct rcu_head *rhp)
{
	drclp->head = srclp->head;
	if (drclp->head)
		drclp->tail = srclp->tail;
	else
		drclp->tail = &drclp->head;
	drclp->len = srclp->len;
	drclp->len_lazy = srclp->len_lazy;
	if (!rhp) {
		rcu_cblist_init(srclp);
	} else {
		rhp->next = NULL;
		srclp->head = rhp;
		srclp->tail = &rhp->next;
		WRITE_ONCE(srclp->len, 1);
		srclp->len_lazy = 0;
	}
}

/*
 * Dequeue the oldest rcu_head structure from the specified callback
 * list.  This function assumes that the callback is non-lazy, but
 * the caller can later invoke rcu_cblist_dequeued_lazy() if it
 * finds otherwise (and if it cares about laziness).  This allows
 * different users to have different ways of determining laziness.
 */
struct rcu_head *rcu_cblist_dequeue(struct rcu_cblist *rclp)
{
	struct rcu_head *rhp;

	rhp = rclp->head;
	if (!rhp)
		return NULL;
	rclp->len--;
	rclp->head = rhp->next;
	if (!rclp->head)
		rclp->tail = &rclp->head;
	return rhp;
}

/* Set the length of an rcu_segcblist structure. */
static void rcu_segcblist_set_len(struct rcu_segcblist *rsclp, long v)
{
#ifdef CONFIG_RCU_NOCB_CPU
	atomic_long_set(&rsclp->len, v);
#else
	WRITE_ONCE(rsclp->len, v);
#endif
}

/*
 * Increase the numeric length of an rcu_segcblist structure by the
 * specified amount, which can be negative.  This can cause the ->len
 * field to disagree with the actual number of callbacks on the structure.
 * This increase is fully ordered with respect to the callers accesses
 * both before and after.
 */
static void rcu_segcblist_add_len(struct rcu_segcblist *rsclp, long v)
{
#ifdef CONFIG_RCU_NOCB_CPU
	smp_mb__before_atomic(); /* Up to the caller! */
	atomic_long_add(v, &rsclp->len);
	smp_mb__after_atomic(); /* Up to the caller! */
#else
	smp_mb(); /* Up to the caller! */
	WRITE_ONCE(rsclp->len, rsclp->len + v);
	smp_mb(); /* Up to the caller! */
#endif
}

/*
 * Increase the numeric length of an rcu_segcblist structure by one.
 * This can cause the ->len field to disagree with the actual number of
 * callbacks on the structure.  This increase is fully ordered with respect
 * to the callers accesses both before and after.
 */
void rcu_segcblist_inc_len(struct rcu_segcblist *rsclp)
{
	rcu_segcblist_add_len(rsclp, 1);
}

/*
 * Exchange the numeric length of the specified rcu_segcblist structure
 * with the specified value.  This can cause the ->len field to disagree
 * with the actual number of callbacks on the structure.  This exchange is
 * fully ordered with respect to the callers accesses both before and after.
 */
static long rcu_segcblist_xchg_len(struct rcu_segcblist *rsclp, long v)
{
#ifdef CONFIG_RCU_NOCB_CPU
	return atomic_long_xchg(&rsclp->len, v);
#else
	long ret = rsclp->len;

	smp_mb(); /* Up to the caller! */
	WRITE_ONCE(rsclp->len, v);
	smp_mb(); /* Up to the caller! */
	return ret;
#endif
}

/*
 * Initialize an rcu_segcblist structure.
 */
void rcu_segcblist_init(struct rcu_segcblist *rsclp)
{
	int i;

	BUILD_BUG_ON(RCU_NEXT_TAIL + 1 != ARRAY_SIZE(rsclp->gp_seq));
	BUILD_BUG_ON(ARRAY_SIZE(rsclp->tails) != ARRAY_SIZE(rsclp->gp_seq));
	rsclp->head = NULL;
	for (i = 0; i < RCU_CBLIST_NSEGS; i++)
		rsclp->tails[i] = &rsclp->head;
	rcu_segcblist_set_len(rsclp, 0);
	rsclp->len_lazy = 0;
	rsclp->enabled = 1;
}

/*
 * Disable the specified rcu_segcblist structure, so that callbacks can
 * no longer be posted to it.  This structure must be empty.
 */
void rcu_segcblist_disable(struct rcu_segcblist *rsclp)
{
	WARN_ON_ONCE(!rcu_segcblist_empty(rsclp));
	WARN_ON_ONCE(rcu_segcblist_n_cbs(rsclp));
	WARN_ON_ONCE(rcu_segcblist_n_lazy_cbs(rsclp));
	rsclp->enabled = 0;
}

/*
 * Mark the specified rcu_segcblist structure as offloaded.  This
 * structure must be empty.
 */
void rcu_segcblist_offload(struct rcu_segcblist *rsclp)
{
	rsclp->offloaded = 1;
}

/*
 * Does the specified rcu_segcblist structure contain callbacks that
 * are ready to be invoked?
 */
bool rcu_segcblist_ready_cbs(struct rcu_segcblist *rsclp)
{
	return rcu_segcblist_is_enabled(rsclp) &&
	       &rsclp->head != rsclp->tails[RCU_DONE_TAIL];
}

/*
 * Does the specified rcu_segcblist structure contain callbacks that
 * are still pending, that is, not yet ready to be invoked?
 */
bool rcu_segcblist_pend_cbs(struct rcu_segcblist *rsclp)
{
	return rcu_segcblist_is_enabled(rsclp) &&
	       !rcu_segcblist_restempty(rsclp, RCU_DONE_TAIL);
}

/*
 * Return a pointer to the first callback in the specified rcu_segcblist
 * structure.  This is useful for diagnostics.
 */
struct rcu_head *rcu_segcblist_first_cb(struct rcu_segcblist *rsclp)
{
	if (rcu_segcblist_is_enabled(rsclp))
		return rsclp->head;
	return NULL;
}

/*
 * Return a pointer to the first pending callback in the specified
 * rcu_segcblist structure.  This is useful just after posting a given
 * callback -- if that callback is the first pending callback, then
 * you cannot rely on someone else having already started up the required
 * grace period.
 */
struct rcu_head *rcu_segcblist_first_pend_cb(struct rcu_segcblist *rsclp)
{
	if (rcu_segcblist_is_enabled(rsclp))
		return *rsclp->tails[RCU_DONE_TAIL];
	return NULL;
}

/*
 * Return false if there are no CBs awaiting grace periods, otherwise,
 * return true and store the nearest waited-upon grace period into *lp.
 */
bool rcu_segcblist_nextgp(struct rcu_segcblist *rsclp, unsigned long *lp)
{
	if (!rcu_segcblist_pend_cbs(rsclp))
		return false;
	*lp = rsclp->gp_seq[RCU_WAIT_TAIL];
	return true;
}

/*
 * Enqueue the specified callback onto the specified rcu_segcblist
 * structure, updating accounting as needed.  Note that the ->len
 * field may be accessed locklessly, hence the WRITE_ONCE().
 * The ->len field is used by rcu_barrier() and friends to determine
 * if it must post a callback on this structure, and it is OK
 * for rcu_barrier() to sometimes post callbacks needlessly, but
 * absolutely not OK for it to ever miss posting a callback.
 */
void rcu_segcblist_enqueue(struct rcu_segcblist *rsclp,
			   struct rcu_head *rhp, bool lazy)
{
	rcu_segcblist_inc_len(rsclp);
	if (lazy)
		rsclp->len_lazy++;
	smp_mb(); /* Ensure counts are updated before callback is enqueued. */
	rhp->next = NULL;
	WRITE_ONCE(*rsclp->tails[RCU_NEXT_TAIL], rhp);
	WRITE_ONCE(rsclp->tails[RCU_NEXT_TAIL], &rhp->next);
}

/*
 * Entrain the specified callback onto the specified rcu_segcblist at
 * the end of the last non-empty segment.  If the entire rcu_segcblist
 * is empty, make no change, but return false.
 *
 * This is intended for use by rcu_barrier()-like primitives, -not-
 * for normal grace-period use.  IMPORTANT:  The callback you enqueue
 * will wait for all prior callbacks, NOT necessarily for a grace
 * period.  You have been warned.
 */
bool rcu_segcblist_entrain(struct rcu_segcblist *rsclp,
			   struct rcu_head *rhp, bool lazy)
{
	int i;

	if (rcu_segcblist_n_cbs(rsclp) == 0)
		return false;
	rcu_segcblist_inc_len(rsclp);
	if (lazy)
		rsclp->len_lazy++;
	smp_mb(); /* Ensure counts are updated before callback is entrained. */
	rhp->next = NULL;
	for (i = RCU_NEXT_TAIL; i > RCU_DONE_TAIL; i--)
		if (rsclp->tails[i] != rsclp->tails[i - 1])
			break;
	WRITE_ONCE(*rsclp->tails[i], rhp);
	for (; i <= RCU_NEXT_TAIL; i++)
		WRITE_ONCE(rsclp->tails[i], &rhp->next);
	return true;
}

/*
 * Extract only the counts from the specified rcu_segcblist structure,
 * and place them in the specified rcu_cblist structure.  This function
 * supports both callback orphaning and invocation, hence the separation
 * of counts and callbacks.  (Callbacks ready for invocation must be
 * orphaned and adopted separately from pending callbacks, but counts
 * apply to all callbacks.  Locking must be used to make sure that
 * both orphaned-callbacks lists are consistent.)
 */
void rcu_segcblist_extract_count(struct rcu_segcblist *rsclp,
					       struct rcu_cblist *rclp)
{
	rclp->len_lazy += rsclp->len_lazy;
	rsclp->len_lazy = 0;
	rclp->len = rcu_segcblist_xchg_len(rsclp, 0);
}

/*
 * Extract only those callbacks ready to be invoked from the specified
 * rcu_segcblist structure and place them in the specified rcu_cblist
 * structure.
 */
void rcu_segcblist_extract_done_cbs(struct rcu_segcblist *rsclp,
				    struct rcu_cblist *rclp)
{
	int i;

	if (!rcu_segcblist_ready_cbs(rsclp))
		return; /* Nothing to do. */
	*rclp->tail = rsclp->head;
	WRITE_ONCE(rsclp->head, *rsclp->tails[RCU_DONE_TAIL]);
	WRITE_ONCE(*rsclp->tails[RCU_DONE_TAIL], NULL);
	rclp->tail = rsclp->tails[RCU_DONE_TAIL];
	for (i = RCU_CBLIST_NSEGS - 1; i >= RCU_DONE_TAIL; i--)
		if (rsclp->tails[i] == rsclp->tails[RCU_DONE_TAIL])
			WRITE_ONCE(rsclp->tails[i], &rsclp->head);
}

/*
 * Extract only those callbacks still pending (not yet ready to be
 * invoked) from the specified rcu_segcblist structure and place them in
 * the specified rcu_cblist structure.  Note that this loses information
 * about any callbacks that might have been partway done waiting for
 * their grace period.  Too bad!  They will have to start over.
 */
void rcu_segcblist_extract_pend_cbs(struct rcu_segcblist *rsclp,
				    struct rcu_cblist *rclp)
{
	int i;

	if (!rcu_segcblist_pend_cbs(rsclp))
		return; /* Nothing to do. */
	*rclp->tail = *rsclp->tails[RCU_DONE_TAIL];
	rclp->tail = rsclp->tails[RCU_NEXT_TAIL];
	WRITE_ONCE(*rsclp->tails[RCU_DONE_TAIL], NULL);
	for (i = RCU_DONE_TAIL + 1; i < RCU_CBLIST_NSEGS; i++)
		WRITE_ONCE(rsclp->tails[i], rsclp->tails[RCU_DONE_TAIL]);
}

/*
 * Insert counts from the specified rcu_cblist structure in the
 * specified rcu_segcblist structure.
 */
void rcu_segcblist_insert_count(struct rcu_segcblist *rsclp,
				struct rcu_cblist *rclp)
{
	rsclp->len_lazy += rclp->len_lazy;
	rcu_segcblist_add_len(rsclp, rclp->len);
	rclp->len_lazy = 0;
	rclp->len = 0;
}

/*
 * Move callbacks from the specified rcu_cblist to the beginning of the
 * done-callbacks segment of the specified rcu_segcblist.
 */
void rcu_segcblist_insert_done_cbs(struct rcu_segcblist *rsclp,
				   struct rcu_cblist *rclp)
{
	int i;

	if (!rclp->head)
		return; /* No callbacks to move. */
	*rclp->tail = rsclp->head;
	WRITE_ONCE(rsclp->head, rclp->head);
	for (i = RCU_DONE_TAIL; i < RCU_CBLIST_NSEGS; i++)
		if (&rsclp->head == rsclp->tails[i])
			WRITE_ONCE(rsclp->tails[i], rclp->tail);
		else
			break;
	rclp->head = NULL;
	rclp->tail = &rclp->head;
}

/*
 * Move callbacks from the specified rcu_cblist to the end of the
 * new-callbacks segment of the specified rcu_segcblist.
 */
void rcu_segcblist_insert_pend_cbs(struct rcu_segcblist *rsclp,
				   struct rcu_cblist *rclp)
{
	if (!rclp->head)
		return; /* Nothing to do. */
	WRITE_ONCE(*rsclp->tails[RCU_NEXT_TAIL], rclp->head);
	WRITE_ONCE(rsclp->tails[RCU_NEXT_TAIL], rclp->tail);
	rclp->head = NULL;
	rclp->tail = &rclp->head;
}

/*
 * Advance the callbacks in the specified rcu_segcblist structure based
 * on the current value passed in for the grace-period counter.
 */
void rcu_segcblist_advance(struct rcu_segcblist *rsclp, unsigned long seq)
{
	int i, j;

	WARN_ON_ONCE(!rcu_segcblist_is_enabled(rsclp));
	if (rcu_segcblist_restempty(rsclp, RCU_DONE_TAIL))
		return;

	/*
	 * Find all callbacks whose ->gp_seq numbers indicate that they
	 * are ready to invoke, and put them into the RCU_DONE_TAIL segment.
	 */
	for (i = RCU_WAIT_TAIL; i < RCU_NEXT_TAIL; i++) {
		if (ULONG_CMP_LT(seq, rsclp->gp_seq[i]))
			break;
		WRITE_ONCE(rsclp->tails[RCU_DONE_TAIL], rsclp->tails[i]);
	}

	/* If no callbacks moved, nothing more need be done. */
	if (i == RCU_WAIT_TAIL)
		return;

	/* Clean up tail pointers that might have been misordered above. */
	for (j = RCU_WAIT_TAIL; j < i; j++)
		WRITE_ONCE(rsclp->tails[j], rsclp->tails[RCU_DONE_TAIL]);

	/*
	 * Callbacks moved, so clean up the misordered ->tails[] pointers
	 * that now point into the middle of the list of ready-to-invoke
	 * callbacks.  The overall effect is to copy down the later pointers
	 * into the gap that was created by the now-ready segments.
	 */
	for (j = RCU_WAIT_TAIL; i < RCU_NEXT_TAIL; i++, j++) {
		if (rsclp->tails[j] == rsclp->tails[RCU_NEXT_TAIL])
			break;  /* No more callbacks. */
		WRITE_ONCE(rsclp->tails[j], rsclp->tails[i]);
		rsclp->gp_seq[j] = rsclp->gp_seq[i];
	}
}

/*
 * "Accelerate" callbacks based on more-accurate grace-period information.
 * The reason for this is that RCU does not synchronize the beginnings and
 * ends of grace periods, and that callbacks are posted locally.  This in
 * turn means that the callbacks must be labelled conservatively early
 * on, as getting exact information would degrade both performance and
 * scalability.  When more accurate grace-period information becomes
 * available, previously posted callbacks can be "accelerated", marking
 * them to complete at the end of the earlier grace period.
 *
 * This function operates on an rcu_segcblist structure, and also the
 * grace-period sequence number seq at which new callbacks would become
 * ready to invoke.  Returns true if there are callbacks that won't be
 * ready to invoke until seq, false otherwise.
 */
bool rcu_segcblist_accelerate(struct rcu_segcblist *rsclp, unsigned long seq)
{
	int i;

	WARN_ON_ONCE(!rcu_segcblist_is_enabled(rsclp));
	if (rcu_segcblist_restempty(rsclp, RCU_DONE_TAIL))
		return false;

	/*
	 * Find the segment preceding the oldest segment of callbacks
	 * whose ->gp_seq[] completion is at or after that passed in via
	 * "seq", skipping any empty segments.  This oldest segment, along
	 * with any later segments, can be merged in with any newly arrived
	 * callbacks in the RCU_NEXT_TAIL segment, and assigned "seq"
	 * as their ->gp_seq[] grace-period completion sequence number.
	 */
	for (i = RCU_NEXT_READY_TAIL; i > RCU_DONE_TAIL; i--)
		if (rsclp->tails[i] != rsclp->tails[i - 1] &&
		    ULONG_CMP_LT(rsclp->gp_seq[i], seq))
			break;

	/*
	 * If all the segments contain callbacks that correspond to
	 * earlier grace-period sequence numbers than "seq", leave.
	 * Assuming that the rcu_segcblist structure has enough
	 * segments in its arrays, this can only happen if some of
	 * the non-done segments contain callbacks that really are
	 * ready to invoke.  This situation will get straightened
	 * out by the next call to rcu_segcblist_advance().
	 *
	 * Also advance to the oldest segment of callbacks whose
	 * ->gp_seq[] completion is at or after that passed in via "seq",
	 * skipping any empty segments.
	 */
	if (++i >= RCU_NEXT_TAIL)
		return false;

	/*
	 * Merge all later callbacks, including newly arrived callbacks,
	 * into the segment located by the for-loop above.  Assign "seq"
	 * as the ->gp_seq[] value in order to correctly handle the case
	 * where there were no pending callbacks in the rcu_segcblist
	 * structure other than in the RCU_NEXT_TAIL segment.
	 */
	for (; i < RCU_NEXT_TAIL; i++) {
		WRITE_ONCE(rsclp->tails[i], rsclp->tails[RCU_NEXT_TAIL]);
		rsclp->gp_seq[i] = seq;
	}
	return true;
}

/*
 * Merge the source rcu_segcblist structure into the destination
 * rcu_segcblist structure, then initialize the source.  Any pending
 * callbacks from the source get to start over.  It is best to
 * advance and accelerate both the destination and the source
 * before merging.
 */
void rcu_segcblist_merge(struct rcu_segcblist *dst_rsclp,
			 struct rcu_segcblist *src_rsclp)
{
	struct rcu_cblist donecbs;
	struct rcu_cblist pendcbs;

	rcu_cblist_init(&donecbs);
	rcu_cblist_init(&pendcbs);
	rcu_segcblist_extract_count(src_rsclp, &donecbs);
	rcu_segcblist_extract_done_cbs(src_rsclp, &donecbs);
	rcu_segcblist_extract_pend_cbs(src_rsclp, &pendcbs);
	rcu_segcblist_insert_count(dst_rsclp, &donecbs);
	rcu_segcblist_insert_done_cbs(dst_rsclp, &donecbs);
	rcu_segcblist_insert_pend_cbs(dst_rsclp, &pendcbs);
	rcu_segcblist_init(src_rsclp);
}
