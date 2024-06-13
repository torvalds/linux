/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * RCU segmented callback lists, internal-to-rcu header file
 *
 * Copyright IBM Corporation, 2017
 *
 * Authors: Paul E. McKenney <paulmck@linux.ibm.com>
 */

#include <linux/rcu_segcblist.h>

/* Return number of callbacks in the specified callback list. */
static inline long rcu_cblist_n_cbs(struct rcu_cblist *rclp)
{
	return READ_ONCE(rclp->len);
}

/* Return number of callbacks in segmented callback list by summing seglen. */
long rcu_segcblist_n_segment_cbs(struct rcu_segcblist *rsclp);

void rcu_cblist_init(struct rcu_cblist *rclp);
void rcu_cblist_enqueue(struct rcu_cblist *rclp, struct rcu_head *rhp);
void rcu_cblist_flush_enqueue(struct rcu_cblist *drclp,
			      struct rcu_cblist *srclp,
			      struct rcu_head *rhp);
struct rcu_head *rcu_cblist_dequeue(struct rcu_cblist *rclp);

/*
 * Is the specified rcu_segcblist structure empty?
 *
 * But careful!  The fact that the ->head field is NULL does not
 * necessarily imply that there are no callbacks associated with
 * this structure.  When callbacks are being invoked, they are
 * removed as a group.  If callback invocation must be preempted,
 * the remaining callbacks will be added back to the list.  Either
 * way, the counts are updated later.
 *
 * So it is often the case that rcu_segcblist_n_cbs() should be used
 * instead.
 */
static inline bool rcu_segcblist_empty(struct rcu_segcblist *rsclp)
{
	return !READ_ONCE(rsclp->head);
}

/* Return number of callbacks in segmented callback list. */
static inline long rcu_segcblist_n_cbs(struct rcu_segcblist *rsclp)
{
#ifdef CONFIG_RCU_NOCB_CPU
	return atomic_long_read(&rsclp->len);
#else
	return READ_ONCE(rsclp->len);
#endif
}

static inline void rcu_segcblist_set_flags(struct rcu_segcblist *rsclp,
					   int flags)
{
	WRITE_ONCE(rsclp->flags, rsclp->flags | flags);
}

static inline void rcu_segcblist_clear_flags(struct rcu_segcblist *rsclp,
					     int flags)
{
	WRITE_ONCE(rsclp->flags, rsclp->flags & ~flags);
}

static inline bool rcu_segcblist_test_flags(struct rcu_segcblist *rsclp,
					    int flags)
{
	return READ_ONCE(rsclp->flags) & flags;
}

/*
 * Is the specified rcu_segcblist enabled, for example, not corresponding
 * to an offline CPU?
 */
static inline bool rcu_segcblist_is_enabled(struct rcu_segcblist *rsclp)
{
	return rcu_segcblist_test_flags(rsclp, SEGCBLIST_ENABLED);
}

/*
 * Is the specified rcu_segcblist NOCB offloaded (or in the middle of the
 * [de]offloading process)?
 */
static inline bool rcu_segcblist_is_offloaded(struct rcu_segcblist *rsclp)
{
	if (IS_ENABLED(CONFIG_RCU_NOCB_CPU) &&
	    rcu_segcblist_test_flags(rsclp, SEGCBLIST_LOCKING))
		return true;

	return false;
}

static inline bool rcu_segcblist_completely_offloaded(struct rcu_segcblist *rsclp)
{
	if (IS_ENABLED(CONFIG_RCU_NOCB_CPU) &&
	    !rcu_segcblist_test_flags(rsclp, SEGCBLIST_RCU_CORE))
		return true;

	return false;
}

/*
 * Are all segments following the specified segment of the specified
 * rcu_segcblist structure empty of callbacks?  (The specified
 * segment might well contain callbacks.)
 */
static inline bool rcu_segcblist_restempty(struct rcu_segcblist *rsclp, int seg)
{
	return !READ_ONCE(*READ_ONCE(rsclp->tails[seg]));
}

/*
 * Is the specified segment of the specified rcu_segcblist structure
 * empty of callbacks?
 */
static inline bool rcu_segcblist_segempty(struct rcu_segcblist *rsclp, int seg)
{
	if (seg == RCU_DONE_TAIL)
		return &rsclp->head == rsclp->tails[RCU_DONE_TAIL];
	return rsclp->tails[seg - 1] == rsclp->tails[seg];
}

void rcu_segcblist_inc_len(struct rcu_segcblist *rsclp);
void rcu_segcblist_add_len(struct rcu_segcblist *rsclp, long v);
void rcu_segcblist_init(struct rcu_segcblist *rsclp);
void rcu_segcblist_disable(struct rcu_segcblist *rsclp);
void rcu_segcblist_offload(struct rcu_segcblist *rsclp, bool offload);
bool rcu_segcblist_ready_cbs(struct rcu_segcblist *rsclp);
bool rcu_segcblist_pend_cbs(struct rcu_segcblist *rsclp);
struct rcu_head *rcu_segcblist_first_cb(struct rcu_segcblist *rsclp);
struct rcu_head *rcu_segcblist_first_pend_cb(struct rcu_segcblist *rsclp);
bool rcu_segcblist_nextgp(struct rcu_segcblist *rsclp, unsigned long *lp);
void rcu_segcblist_enqueue(struct rcu_segcblist *rsclp,
			   struct rcu_head *rhp);
bool rcu_segcblist_entrain(struct rcu_segcblist *rsclp,
			   struct rcu_head *rhp);
void rcu_segcblist_extract_done_cbs(struct rcu_segcblist *rsclp,
				    struct rcu_cblist *rclp);
void rcu_segcblist_extract_pend_cbs(struct rcu_segcblist *rsclp,
				    struct rcu_cblist *rclp);
void rcu_segcblist_insert_count(struct rcu_segcblist *rsclp,
				struct rcu_cblist *rclp);
void rcu_segcblist_insert_done_cbs(struct rcu_segcblist *rsclp,
				   struct rcu_cblist *rclp);
void rcu_segcblist_insert_pend_cbs(struct rcu_segcblist *rsclp,
				   struct rcu_cblist *rclp);
void rcu_segcblist_advance(struct rcu_segcblist *rsclp, unsigned long seq);
bool rcu_segcblist_accelerate(struct rcu_segcblist *rsclp, unsigned long seq);
void rcu_segcblist_merge(struct rcu_segcblist *dst_rsclp,
			 struct rcu_segcblist *src_rsclp);
