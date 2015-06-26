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

struct perf_stat {
	struct stats		res_stats[3];
	enum perf_stat_evsel_id	id;
};

enum aggr_mode {
	AGGR_NONE,
	AGGR_GLOBAL,
	AGGR_SOCKET,
	AGGR_CORE,
};

struct perf_counts_values {
	union {
		struct {
			u64 val;
			u64 ena;
			u64 run;
		};
		u64 values[3];
	};
};

struct perf_counts {
	s8			  scaled;
	struct perf_counts_values aggr;
	struct xyarray		  *cpu;
};

static inline struct perf_counts_values*
perf_counts(struct perf_counts *counts, int cpu)
{
	return xyarray__entry(counts->cpu, cpu, 0);
}

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

struct perf_counts *perf_counts__new(int ncpus);
void perf_counts__delete(struct perf_counts *counts);

void perf_evsel__reset_counts(struct perf_evsel *evsel);
int perf_evsel__alloc_counts(struct perf_evsel *evsel, int ncpus);
void perf_evsel__free_counts(struct perf_evsel *evsel);
#endif
