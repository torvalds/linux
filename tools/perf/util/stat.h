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
	PERF_STAT_EVSEL_ID__TOPDOWN_TOTAL_SLOTS,
	PERF_STAT_EVSEL_ID__TOPDOWN_SLOTS_ISSUED,
	PERF_STAT_EVSEL_ID__TOPDOWN_SLOTS_RETIRED,
	PERF_STAT_EVSEL_ID__TOPDOWN_FETCH_BUBBLES,
	PERF_STAT_EVSEL_ID__TOPDOWN_RECOVERY_BUBBLES,
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

typedef void (*print_metric_t)(void *ctx, const char *color, const char *unit,
			       const char *fmt, double val);
typedef void (*new_line_t )(void *ctx);

void perf_stat__init_shadow_stats(void);
void perf_stat__reset_shadow_stats(void);
void perf_stat__update_shadow_stats(struct perf_evsel *counter, u64 *count,
				    int cpu);
struct perf_stat_output_ctx {
	void *ctx;
	print_metric_t print_metric;
	new_line_t new_line;
};

void perf_stat__print_shadow_stats(struct perf_evsel *evsel,
				   double avg, int cpu,
				   struct perf_stat_output_ctx *out);

int perf_evlist__alloc_stats(struct perf_evlist *evlist, bool alloc_raw);
void perf_evlist__free_stats(struct perf_evlist *evlist);
void perf_evlist__reset_stats(struct perf_evlist *evlist);

int perf_stat_process_counter(struct perf_stat_config *config,
			      struct perf_evsel *counter);
struct perf_tool;
union perf_event;
struct perf_session;
int perf_event__process_stat_event(struct perf_tool *tool,
				   union perf_event *event,
				   struct perf_session *session);

size_t perf_event__fprintf_stat(union perf_event *event, FILE *fp);
size_t perf_event__fprintf_stat_round(union perf_event *event, FILE *fp);
size_t perf_event__fprintf_stat_config(union perf_event *event, FILE *fp);
#endif
