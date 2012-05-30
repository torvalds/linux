/*
 * Dynamic byte queue limits.  See include/linux/dynamic_queue_limits.h
 *
 * Copyright (c) 2011, Tom Herbert <therbert@google.com>
 */
#include <linux/module.h>
#include <linux/types.h>
#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/dynamic_queue_limits.h>

#define POSDIFF(A, B) ((int)((A) - (B)) > 0 ? (A) - (B) : 0)
#define AFTER_EQ(A, B) ((int)((A) - (B)) >= 0)

/* Records completed count and recalculates the queue limit */
void dql_completed(struct dql *dql, unsigned int count)
{
	unsigned int inprogress, prev_inprogress, limit;
	unsigned int ovlimit, completed;
	bool all_prev_completed;

	/* Can't complete more than what's in queue */
	BUG_ON(count > dql->num_queued - dql->num_completed);

	completed = dql->num_completed + count;
	limit = dql->limit;
	ovlimit = POSDIFF(dql->num_queued - dql->num_completed, limit);
	inprogress = dql->num_queued - completed;
	prev_inprogress = dql->prev_num_queued - dql->num_completed;
	all_prev_completed = AFTER_EQ(completed, dql->prev_num_queued);

	if ((ovlimit && !inprogress) ||
	    (dql->prev_ovlimit && all_prev_completed)) {
		/*
		 * Queue considered starved if:
		 *   - The queue was over-limit in the last interval,
		 *     and there is no more data in the queue.
		 *  OR
		 *   - The queue was over-limit in the previous interval and
		 *     when enqueuing it was possible that all queued data
		 *     had been consumed.  This covers the case when queue
		 *     may have becomes starved between completion processing
		 *     running and next time enqueue was scheduled.
		 *
		 *     When queue is starved increase the limit by the amount
		 *     of bytes both sent and completed in the last interval,
		 *     plus any previous over-limit.
		 */
		limit += POSDIFF(completed, dql->prev_num_queued) +
		     dql->prev_ovlimit;
		dql->slack_start_time = jiffies;
		dql->lowest_slack = UINT_MAX;
	} else if (inprogress && prev_inprogress && !all_prev_completed) {
		/*
		 * Queue was not starved, check if the limit can be decreased.
		 * A decrease is only considered if the queue has been busy in
		 * the whole interval (the check above).
		 *
		 * If there is slack, the amount of execess data queued above
		 * the the amount needed to prevent starvation, the queue limit
		 * can be decreased.  To avoid hysteresis we consider the
		 * minimum amount of slack found over several iterations of the
		 * completion routine.
		 */
		unsigned int slack, slack_last_objs;

		/*
		 * Slack is the maximum of
		 *   - The queue limit plus previous over-limit minus twice
		 *     the number of objects completed.  Note that two times
		 *     number of completed bytes is a basis for an upper bound
		 *     of the limit.
		 *   - Portion of objects in the last queuing operation that
		 *     was not part of non-zero previous over-limit.  That is
		 *     "round down" by non-overlimit portion of the last
		 *     queueing operation.
		 */
		slack = POSDIFF(limit + dql->prev_ovlimit,
		    2 * (completed - dql->num_completed));
		slack_last_objs = dql->prev_ovlimit ?
		    POSDIFF(dql->prev_last_obj_cnt, dql->prev_ovlimit) : 0;

		slack = max(slack, slack_last_objs);

		if (slack < dql->lowest_slack)
			dql->lowest_slack = slack;

		if (time_after(jiffies,
			       dql->slack_start_time + dql->slack_hold_time)) {
			limit = POSDIFF(limit, dql->lowest_slack);
			dql->slack_start_time = jiffies;
			dql->lowest_slack = UINT_MAX;
		}
	}

	/* Enforce bounds on limit */
	limit = clamp(limit, dql->min_limit, dql->max_limit);

	if (limit != dql->limit) {
		dql->limit = limit;
		ovlimit = 0;
	}

	dql->adj_limit = limit + completed;
	dql->prev_ovlimit = ovlimit;
	dql->prev_last_obj_cnt = dql->last_obj_cnt;
	dql->num_completed = completed;
	dql->prev_num_queued = dql->num_queued;
}
EXPORT_SYMBOL(dql_completed);

void dql_reset(struct dql *dql)
{
	/* Reset all dynamic values */
	dql->limit = 0;
	dql->num_queued = 0;
	dql->num_completed = 0;
	dql->last_obj_cnt = 0;
	dql->prev_num_queued = 0;
	dql->prev_last_obj_cnt = 0;
	dql->prev_ovlimit = 0;
	dql->lowest_slack = UINT_MAX;
	dql->slack_start_time = jiffies;
}
EXPORT_SYMBOL(dql_reset);

int dql_init(struct dql *dql, unsigned hold_time)
{
	dql->max_limit = DQL_MAX_LIMIT;
	dql->min_limit = 0;
	dql->slack_hold_time = hold_time;
	dql_reset(dql);
	return 0;
}
EXPORT_SYMBOL(dql_init);
