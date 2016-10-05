#ifndef WB_THROTTLE_H
#define WB_THROTTLE_H

#include <linux/atomic.h>
#include <linux/wait.h>
#include <linux/timer.h>
#include <linux/ktime.h>

enum wbt_flags {
	WBT_TRACKED		= 1,	/* write, tracked for throttling */
	WBT_READ		= 2,	/* read */
	WBT_KSWAPD		= 4,	/* write, from kswapd */

	WBT_NR_BITS		= 3,	/* number of bits */
};

enum {
	/*
	 * Set aside 3 bits for state, rest is a time stamp
	 */
	ISSUE_STAT_SHIFT	= 64 - WBT_NR_BITS,
	ISSUE_STAT_MASK 	= ~((1ULL << ISSUE_STAT_SHIFT) - 1),
	ISSUE_STAT_TIME_MASK	= ~ISSUE_STAT_MASK,

	WBT_NUM_RWQ		= 2,
};

struct wb_issue_stat {
	u64 time;
};

static inline void wbt_issue_stat_set_time(struct wb_issue_stat *stat)
{
	stat->time = (stat->time & ISSUE_STAT_MASK) |
			(ktime_to_ns(ktime_get()) & ISSUE_STAT_TIME_MASK);
}

static inline u64 wbt_issue_stat_get_time(struct wb_issue_stat *stat)
{
	return stat->time & ISSUE_STAT_TIME_MASK;
}

static inline void wbt_clear_state(struct wb_issue_stat *stat)
{
	stat->time &= ISSUE_STAT_TIME_MASK;
}

static inline enum wbt_flags wbt_stat_to_mask(struct wb_issue_stat *stat)
{
	return (stat->time & ISSUE_STAT_MASK) >> ISSUE_STAT_SHIFT;
}

static inline void wbt_track(struct wb_issue_stat *stat, enum wbt_flags wb_acct)
{
	stat->time |= ((u64) wb_acct) << ISSUE_STAT_SHIFT;
}

static inline bool wbt_is_tracked(struct wb_issue_stat *stat)
{
	return (stat->time >> ISSUE_STAT_SHIFT) & WBT_TRACKED;
}

static inline bool wbt_is_read(struct wb_issue_stat *stat)
{
	return (stat->time >> ISSUE_STAT_SHIFT) & WBT_READ;
}

struct wb_stat_ops {
	void (*get)(void *, struct blk_rq_stat *);
	bool (*is_current)(struct blk_rq_stat *);
	void (*clear)(void *);
};

struct rq_wait {
	wait_queue_head_t wait;
	atomic_t inflight;
};

struct rq_wb {
	/*
	 * Settings that govern how we throttle
	 */
	unsigned int wb_background;		/* background writeback */
	unsigned int wb_normal;			/* normal writeback */
	unsigned int wb_max;			/* max throughput writeback */
	int scale_step;
	bool scaled_max;

	/*
	 * Number of consecutive periods where we don't have enough
	 * information to make a firm scale up/down decision.
	 */
	unsigned int unknown_cnt;

	u64 win_nsec;				/* default window size */
	u64 cur_win_nsec;			/* current window size */

	struct timer_list window_timer;

	s64 sync_issue;
	void *sync_cookie;

	unsigned int wc;
	unsigned int queue_depth;

	unsigned long last_issue;		/* last non-throttled issue */
	unsigned long last_comp;		/* last non-throttled comp */
	unsigned long min_lat_nsec;
	struct backing_dev_info *bdi;
	struct rq_wait rq_wait[WBT_NUM_RWQ];

	struct wb_stat_ops *stat_ops;
	void *ops_data;
};

static inline unsigned int wbt_inflight(struct rq_wb *rwb)
{
	unsigned int i, ret = 0;

	for (i = 0; i < WBT_NUM_RWQ; i++)
		ret += atomic_read(&rwb->rq_wait[i].inflight);

	return ret;
}

struct backing_dev_info;

void __wbt_done(struct rq_wb *, enum wbt_flags);
void wbt_done(struct rq_wb *, struct wb_issue_stat *);
enum wbt_flags wbt_wait(struct rq_wb *, unsigned int, spinlock_t *);
struct rq_wb *wbt_init(struct backing_dev_info *, struct wb_stat_ops *, void *);
void wbt_exit(struct rq_wb *);
void wbt_update_limits(struct rq_wb *);
void wbt_requeue(struct rq_wb *, struct wb_issue_stat *);
void wbt_issue(struct rq_wb *, struct wb_issue_stat *);
void wbt_disable(struct rq_wb *);

void wbt_set_queue_depth(struct rq_wb *, unsigned int);
void wbt_set_write_cache(struct rq_wb *, bool);

#endif
