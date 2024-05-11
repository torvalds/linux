// SPDX-License-Identifier: GPL-2.0
/*
 * Dynamic byte queue limits.  See include/linux/dynamic_queue_limits.h
 *
 * Copyright (c) 2011, Tom Herbert <therbert@google.com>
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/dynamic_queue_limits.h>
#include <linux/compiler.h>
#include <linux/export.h>
#include <trace/events/napi.h>

#define POSDIFF(A, B) ((int)((A) - (B)) > 0 ? (A) - (B) : 0)
#define AFTER_EQ(A, B) ((int)((A) - (B)) >= 0)

static void dql_check_stall(struct dql *dql, unsigned short stall_thrs)
{
	unsigned long now;

	if (!stall_thrs)
		return;

	now = jiffies;
	/* Check for a potential stall */
	if (time_after_eq(now, dql->last_reap + stall_thrs)) {
		unsigned long hist_head, t, start, end;

		/* We are trying to detect a period of at least @stall_thrs
		 * jiffies without any Tx completions, but during first half
		 * of which some Tx was posted.
		 */
dqs_again:
		hist_head = READ_ONCE(dql->history_head);
		/* pairs with smp_wmb() in dql_queued() */
		smp_rmb();

		/* Get the previous entry in the ring buffer, which is the
		 * oldest sample.
		 */
		start = (hist_head - DQL_HIST_LEN + 1) * BITS_PER_LONG;

		/* Advance start to continue from the last reap time */
		if (time_before(start, dql->last_reap + 1))
			start = dql->last_reap + 1;

		/* Newest sample we should have already seen a completion for */
		end = hist_head * BITS_PER_LONG + (BITS_PER_LONG - 1);

		/* Shrink the search space to [start, (now - start_thrs/2)] if
		 * `end` is beyond the stall zone
		 */
		if (time_before(now, end + stall_thrs / 2))
			end = now - stall_thrs / 2;

		/* Search for the queued time in [t, end] */
		for (t = start; time_before_eq(t, end); t++)
			if (test_bit(t % (DQL_HIST_LEN * BITS_PER_LONG),
				     dql->history))
				break;

		/* Variable t contains the time of the queue */
		if (!time_before_eq(t, end))
			goto no_stall;

		/* The ring buffer was modified in the meantime, retry */
		if (hist_head != READ_ONCE(dql->history_head))
			goto dqs_again;

		dql->stall_cnt++;
		dql->stall_max = max_t(unsigned short, dql->stall_max, now - t);

		trace_dql_stall_detected(dql->stall_thrs, now - t,
					 dql->last_reap, dql->history_head,
					 now, dql->history);
	}
no_stall:
	dql->last_reap = now;
}

/* Records completed count and recalculates the queue limit */
void dql_completed(struct dql *dql, unsigned int count)
{
	unsigned int inprogress, prev_inprogress, limit;
	unsigned int ovlimit, completed, num_queued;
	unsigned short stall_thrs;
	bool all_prev_completed;

	num_queued = READ_ONCE(dql->num_queued);
	/* Read stall_thrs in advance since it belongs to the same (first)
	 * cache line as ->num_queued. This way, dql_check_stall() does not
	 * need to touch the first cache line again later, reducing the window
	 * of possible false sharing.
	 */
	stall_thrs = READ_ONCE(dql->stall_thrs);

	/* Can't complete more than what's in queue */
	BUG_ON(count > num_queued - dql->num_completed);

	completed = dql->num_completed + count;
	limit = dql->limit;
	ovlimit = POSDIFF(num_queued - dql->num_completed, limit);
	inprogress = num_queued - completed;
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
		 * If there is slack, the amount of excess data queued above
		 * the amount needed to prevent starvation, the queue limit
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
	dql->prev_num_queued = num_queued;

	dql_check_stall(dql, stall_thrs);
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

	dql->last_reap = jiffies;
	dql->history_head = jiffies / BITS_PER_LONG;
	memset(dql->history, 0, sizeof(dql->history));
}
EXPORT_SYMBOL(dql_reset);

void dql_init(struct dql *dql, unsigned int hold_time)
{
	dql->max_limit = DQL_MAX_LIMIT;
	dql->min_limit = 0;
	dql->slack_hold_time = hold_time;
	dql->stall_thrs = 0;
	dql_reset(dql);
}
EXPORT_SYMBOL(dql_init);
