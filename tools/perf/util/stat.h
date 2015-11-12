#ifndef __PERF_STATS_H
#define __PERF_STATS_H

#include <linux/types.h>
#include <stdio.h>
#include "xyarray.h"

struct stats
{
	double n, mean, M2;
	u64 max, min;
};

enum perf_stat_evsel_id {
	PERF_STAT_EVSEL_ID__NONE = 0,
	PERF_STAT_EVSEL_ID__CYCLES_IN_TX,
	PERF_STAT_EVSEL_ID__TRANSACTION_START,
	PERF_STAT_EVSEL_ID__ELISION_START,
	PERF_STAT_EVSEL_ID__CYCLES_IN_TX_CP,
	PERF_STAT_EVSEL_ID__MAX,
};

struct perf_stat_evsel {
	struct stats		res_stats[3];
	enum perf_stat_evsel_id	id;
};

enum aggr_mode {
	AGGR_NONE,
	AGGR_GLOBAL,
	AGGR_SOCKET,
	AGGR_CORE,
	AGGR_THREAD,
	AGGR_UNSET,
};

struct perf_stat_config {
	enum aggr_mode	aggr_mode;
	bool		scale;
	FILE		*output;
	unsigned int	interval;
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

struct perf_evsel;
struct perf_evlist;

bool __perf_evsel_stat__is(struct perf_evsel *evsel,
			   enum perf_stat_evsel_id id);

#define perf_stat_evsel__is(evsel, id) \
	__perf_evsel_stat__is(evsel, PERF_STAT_EVSEL_ID__ ## id)

void perf_stat_evsel_id_init(struct perf_evsel *evsel);

extern struct stats walltime_nsecs_stats;

void perf_stat__reset_shadow_stats(void);
void perf_stat__update_shadow_stats(struct perf_evsel *counter, u64 *count,
				    int cpu);
void perf_stat__print_shadow_stats(FILE *out, struct perf_evsel *evsel,
				   double avg, int cpu, enum aggr_mode aggr);

void perf_evsel__reset_stat_priv(struct perf_evsel *evsel);
int perf_evsel__alloc_stat_priv(struct perf_evsel *evsel);
void perf_evsel__free_stat_priv(struct perf_evsel *evsel);

int perf_evsel__alloc_prev_raw_counts(struct perf_evsel *evsel,
				      int ncpus, int nthreads);
void perf_evsel__free_prev_raw_counts(struct perf_evsel *evsel);

int perf_evsel__alloc_stats(struct perf_evsel *evsel, bool alloc_raw);

int perf_evlist__alloc_stats(struct perf_evlist *evlist, bool alloc_raw);
void perf_evlist__free_stats(struct perf_evlist *evlist);
void perf_evlist__reset_stats(struct perf_evlist *evlist);

int perf_stat_process_counter(struct perf_stat_config *config,
			      struct perf_evsel *counter);
#endif
