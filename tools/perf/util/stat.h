#ifndef __PERF_STATS_H
#define __PERF_STATS_H

#include <linux/types.h>

struct stats
{
	double n, mean, M2;
	u64 max, min;
};

void update_stats(struct stats *stats, u64 val);
double avg_stats(struct stats *stats);
double stddev_stats(struct stats *stats);
double rel_stddev_stats(double stddev, double avg);

static inline void init_stats(struct stats *stats)
{
	stats->n    = 0.0;
	stats->mean = 0.0;
	stats->M2   = 0.0;
	stats->min  = (u64) -1;
	stats->max  = 0;
}
#endif
