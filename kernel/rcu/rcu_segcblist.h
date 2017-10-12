/*
 * RCU segmented callback lists, internal-to-rcu header file
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * Copyright IBM Corporation, 2017
 *
 * Authors: Paul E. McKenney <paulmck@linux.vnet.ibm.com>
 */

#include <linux/rcu_segcblist.h>

/*
 * Account for the fact that a previously dequeued callback turned out
 * to be marked as lazy.
 */
static inline void rcu_cblist_dequeued_lazy(struct rcu_cblist *rclp)
{
	rclp->len_lazy--;
}

void rcu_cblist_init(struct rcu_cblist *rclp);
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
	return !rsclp->head;
}

/* Return number of callbacks in segmented callback list. */
static inline long rcu_segcblist_n_cbs(struct rcu_segcblist *rsclp)
{
	return READ_ONCE(rsclp->len);
}

/* Return number of lazy callbacks in segmented callback list. */
static inline long rcu_segcblist_n_lazy_cbs(struct rcu_segcblist *rsclp)
{
	return rsclp->len_lazy;
}

/* Return number of lazy callbacks in segmented callback list. */
static inline long rcu_segcblist_n_nonlazy_cbs(struct rcu_segcblist *rsclp)
{
	return rsclp->len - rsclp->len_lazy;
}

/*
 * Is the specified rcu_segcblist enabled, for example, not corresponding
 * to an offline or callback-offloaded CPU?
 */
static inline bool rcu_segcblist_is_enabled(struct rcu_segcblist *rsclp)
{
	return !!rsclp->tails[RCU_NEXT_TAIL];
}

/*
 * Are all segments following the specified segment of the specified
 * rcu_segcblist structure empty of callbacks?  (The specified
 * segment might well contain callbacks.)
 */
static inline bool rcu_segcblist_restempty(struct rcu_segcblist *rsclp, int seg)
{
	return !*rsclp->tails[seg];
}

/*
 * Interim function to return rcu_segcblist head pointer.  Longer term, the
 * rcu_segcblist will be used more pervasively, removing the need for this
 * function.
 */
static inline struct rcu_head *rcu_segcblist_head(struct rcu_segcblist *rsclp)
{
	return rsclp->head;
}

/*
 * Interim function to return rcu_segcblist head pointer.  Longer term, the
 * rcu_segcblist will be used more pervasively, removing the need for this
 * function.
 */
static inline struct rcu_head **rcu_segcblist_tail(struct rcu_segcblist *rsclp)
{
	WARN_ON_ONCE(rcu_segcblist_empty(rsclp));
	return rsclp->tails[RCU_NEXT_TAIL];
}

void rcu_segcblist_init(struct rcu_segcblist *rsclp);
void rcu_segcblist_disable(struct rcu_segcblist *rsclp);
bool rcu_segcblist_ready_cbs(struct rcu_segcblist *rsclp);
bool rcu_segcblist_pend_cbs(struct rcu_segcblist *rsclp);
struct rcu_head *rcu_segcblist_first_cb(struct rcu_segcblist *rsclp);
struct rcu_head *rcu_segcblist_first_pend_cb(struct rcu_segcblist *rsclp);
void rcu_segcblist_enqueue(struct rcu_segcblist *rsclp,
			   struct rcu_head *rhp, bool lazy);
bool rcu_segcblist_entrain(struct rcu_segcblist *rsclp,
			   struct rcu_head *rhp, bool lazy);
void rcu_segcblist_extract_count(struct rcu_segcblist *rsclp,
				 struct rcu_cblist *rclp);
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
bool rcu_segcblist_future_gp_needed(struct rcu_segcblist *rsclp,
				    unsigned long seq);
void rcu_segcblist_merge(struct rcu_segcblist *dst_rsclp,
			 struct rcu_segcblist *src_rsclp);
