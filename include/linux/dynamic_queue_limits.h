/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Dynamic queue limits (dql) - Definitions
 *
 * Copyright (c) 2011, Tom Herbert <therbert@google.com>
 *
 * This header file contains the definitions for dynamic queue limits (dql).
 * dql would be used in conjunction with a producer/consumer type queue
 * (possibly a HW queue).  Such a queue would have these general properties:
 *
 *   1) Objects are queued up to some limit specified as number of objects.
 *   2) Periodically a completion process executes which retires consumed
 *      objects.
 *   3) Starvation occurs when limit has been reached, all queued data has
 *      actually been consumed, but completion processing has not yet run
 *      so queuing new data is blocked.
 *   4) Minimizing the amount of queued data is desirable.
 *
 * The goal of dql is to calculate the limit as the minimum number of objects
 * needed to prevent starvation.
 *
 * The primary functions of dql are:
 *    dql_queued - called when objects are enqueued to record number of objects
 *    dql_avail - returns how many objects are available to be queued based
 *      on the object limit and how many objects are already enqueued
 *    dql_completed - called at completion time to indicate how many objects
 *      were retired from the queue
 *
 * The dql implementation does not implement any locking for the dql data
 * structures, the higher layer should provide this.  dql_queued should
 * be serialized to prevent concurrent execution of the function; this
 * is also true for  dql_completed.  However, dql_queued and dlq_completed  can
 * be executed concurrently (i.e. they can be protected by different locks).
 */

#ifndef _LINUX_DQL_H
#define _LINUX_DQL_H

#ifdef __KERNEL__

struct dql {
	/* Fields accessed in enqueue path (dql_queued) */
	unsigned int	num_queued;		/* Total ever queued */
	unsigned int	adj_limit;		/* limit + num_completed */
	unsigned int	last_obj_cnt;		/* Count at last queuing */

	/* Fields accessed only by completion path (dql_completed) */

	unsigned int	limit ____cacheline_aligned_in_smp; /* Current limit */
	unsigned int	num_completed;		/* Total ever completed */

	unsigned int	prev_ovlimit;		/* Previous over limit */
	unsigned int	prev_num_queued;	/* Previous queue total */
	unsigned int	prev_last_obj_cnt;	/* Previous queuing cnt */

	unsigned int	lowest_slack;		/* Lowest slack found */
	unsigned long	slack_start_time;	/* Time slacks seen */

	/* Configuration */
	unsigned int	max_limit;		/* Max limit */
	unsigned int	min_limit;		/* Minimum limit */
	unsigned int	slack_hold_time;	/* Time to measure slack */
};

/* Set some static maximums */
#define DQL_MAX_OBJECT (UINT_MAX / 16)
#define DQL_MAX_LIMIT ((UINT_MAX / 2) - DQL_MAX_OBJECT)

/*
 * Record number of objects queued. Assumes that caller has already checked
 * availability in the queue with dql_avail.
 */
static inline void dql_queued(struct dql *dql, unsigned int count)
{
	BUG_ON(count > DQL_MAX_OBJECT);

	dql->last_obj_cnt = count;

	/* We want to force a write first, so that cpu do not attempt
	 * to get cache line containing last_obj_cnt, num_queued, adj_limit
	 * in Shared state, but directly does a Request For Ownership
	 * It is only a hint, we use barrier() only.
	 */
	barrier();

	dql->num_queued += count;
}

/* Returns how many objects can be queued, < 0 indicates over limit. */
static inline int dql_avail(const struct dql *dql)
{
	return ACCESS_ONCE(dql->adj_limit) - ACCESS_ONCE(dql->num_queued);
}

/* Record number of completed objects and recalculate the limit. */
void dql_completed(struct dql *dql, unsigned int count);

/* Reset dql state */
void dql_reset(struct dql *dql);

/* Initialize dql state */
int dql_init(struct dql *dql, unsigned hold_time);

#endif /* _KERNEL_ */

#endif /* _LINUX_DQL_H */
