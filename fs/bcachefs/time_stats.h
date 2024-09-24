/* SPDX-License-Identifier: GPL-2.0 */
/*
 * bch2_time_stats - collect statistics on events that have a duration, with nicely
 * formatted textual output on demand
 *
 * - percpu buffering of event collection: cheap enough to shotgun
 *   everywhere without worrying about overhead
 *
 * tracks:
 *  - number of events
 *  - maximum event duration ever seen
 *  - sum of all event durations
 *  - average event duration, standard and weighted
 *  - standard deviation of event durations, standard and weighted
 * and analagous statistics for the frequency of events
 *
 * We provide both mean and weighted mean (exponentially weighted), and standard
 * deviation and weighted standard deviation, to give an efficient-to-compute
 * view of current behaviour versus. average behaviour - "did this event source
 * just become wonky, or is this typical?".
 *
 * Particularly useful for tracking down latency issues.
 */
#ifndef _BCACHEFS_TIME_STATS_H
#define _BCACHEFS_TIME_STATS_H

#include <linux/sched/clock.h>
#include <linux/spinlock_types.h>
#include <linux/string.h>

#include "mean_and_variance.h"

struct time_unit {
	const char	*name;
	u64		nsecs;
};

/*
 * given a nanosecond value, pick the preferred time units for printing:
 */
const struct time_unit *bch2_pick_time_units(u64 ns);

/*
 * quantiles - do not use:
 *
 * Only enabled if bch2_time_stats->quantiles_enabled has been manually set - don't
 * use in new code.
 */

#define NR_QUANTILES	15
#define QUANTILE_IDX(i)	inorder_to_eytzinger0(i, NR_QUANTILES)
#define QUANTILE_FIRST	eytzinger0_first(NR_QUANTILES)
#define QUANTILE_LAST	eytzinger0_last(NR_QUANTILES)

struct quantiles {
	struct quantile_entry {
		u64	m;
		u64	step;
	}		entries[NR_QUANTILES];
};

struct time_stat_buffer {
	unsigned	nr;
	struct time_stat_buffer_entry {
		u64	start;
		u64	end;
	}		entries[31];
};

struct bch2_time_stats {
	spinlock_t	lock;
	bool		have_quantiles;
	struct time_stat_buffer __percpu *buffer;
	/* all fields are in nanoseconds */
	u64             min_duration;
	u64		max_duration;
	u64		total_duration;
	u64             max_freq;
	u64             min_freq;
	u64		last_event;
	u64		last_event_start;

	struct mean_and_variance	  duration_stats;
	struct mean_and_variance	  freq_stats;

/* default weight for weighted mean and variance calculations */
#define TIME_STATS_MV_WEIGHT	8

	struct mean_and_variance_weighted duration_stats_weighted;
	struct mean_and_variance_weighted freq_stats_weighted;
};

struct bch2_time_stats_quantiles {
	struct bch2_time_stats	stats;
	struct quantiles	quantiles;
};

static inline struct quantiles *time_stats_to_quantiles(struct bch2_time_stats *stats)
{
	return stats->have_quantiles
		? &container_of(stats, struct bch2_time_stats_quantiles, stats)->quantiles
		: NULL;
}

void __bch2_time_stats_clear_buffer(struct bch2_time_stats *, struct time_stat_buffer *);
void __bch2_time_stats_update(struct bch2_time_stats *stats, u64, u64);

/**
 * time_stats_update - collect a new event being tracked
 *
 * @stats	- bch2_time_stats to update
 * @start	- start time of event, recorded with local_clock()
 *
 * The end duration of the event will be the current time
 */
static inline void bch2_time_stats_update(struct bch2_time_stats *stats, u64 start)
{
	__bch2_time_stats_update(stats, start, local_clock());
}

/**
 * track_event_change - track state change events
 *
 * @stats	- bch2_time_stats to update
 * @v		- new state, true or false
 *
 * Use this when tracking time stats for state changes, i.e. resource X becoming
 * blocked/unblocked.
 */
static inline bool track_event_change(struct bch2_time_stats *stats, bool v)
{
	if (v != !!stats->last_event_start) {
		if (!v) {
			bch2_time_stats_update(stats, stats->last_event_start);
			stats->last_event_start = 0;
		} else {
			stats->last_event_start = local_clock() ?: 1;
			return true;
		}
	}

	return false;
}

void bch2_time_stats_reset(struct bch2_time_stats *);
void bch2_time_stats_exit(struct bch2_time_stats *);
void bch2_time_stats_init(struct bch2_time_stats *);

static inline void bch2_time_stats_quantiles_exit(struct bch2_time_stats_quantiles *statq)
{
	bch2_time_stats_exit(&statq->stats);
}
static inline void bch2_time_stats_quantiles_init(struct bch2_time_stats_quantiles *statq)
{
	bch2_time_stats_init(&statq->stats);
	statq->stats.have_quantiles = true;
	memset(&statq->quantiles, 0, sizeof(statq->quantiles));
}

#endif /* _BCACHEFS_TIME_STATS_H */
