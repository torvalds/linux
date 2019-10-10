/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_STATS_H
#define __PERF_STATS_H

#include <linux/types.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/resource.h>
#include "rblist.h"

struct perf_cpu_map;
struct perf_stat_config;
struct timespec;

struct stats {
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
	PERF_STAT_EVSEL_ID__SMI_NUM,
	PERF_STAT_EVSEL_ID__APERF,
	PERF_STAT_EVSEL_ID__MAX,
};

struct perf_stat_evsel {
	struct stats		 res_stats[3];
	enum perf_stat_evsel_id	 id;
	u64			*group_data;
};

enum aggr_mode {
	AGGR_NONE,
	AGGR_GLOBAL,
	AGGR_SOCKET,
	AGGR_DIE,
	AGGR_CORE,
	AGGR_THREAD,
	AGGR_UNSET,
};

enum {
	CTX_BIT_USER	= 1 << 0,
	CTX_BIT_KERNEL	= 1 << 1,
	CTX_BIT_HV	= 1 << 2,
	CTX_BIT_HOST	= 1 << 3,
	CTX_BIT_IDLE	= 1 << 4,
	CTX_BIT_MAX	= 1 << 5,
};

#define NUM_CTX CTX_BIT_MAX

enum stat_type {
	STAT_NONE = 0,
	STAT_NSECS,
	STAT_CYCLES,
	STAT_STALLED_CYCLES_FRONT,
	STAT_STALLED_CYCLES_BACK,
	STAT_BRANCHES,
	STAT_CACHEREFS,
	STAT_L1_DCACHE,
	STAT_L1_ICACHE,
	STAT_LL_CACHE,
	STAT_ITLB_CACHE,
	STAT_DTLB_CACHE,
	STAT_CYCLES_IN_TX,
	STAT_TRANSACTION,
	STAT_ELISION,
	STAT_TOPDOWN_TOTAL_SLOTS,
	STAT_TOPDOWN_SLOTS_ISSUED,
	STAT_TOPDOWN_SLOTS_RETIRED,
	STAT_TOPDOWN_FETCH_BUBBLES,
	STAT_TOPDOWN_RECOVERY_BUBBLES,
	STAT_SMI_NUM,
	STAT_APERF,
	STAT_MAX
};

struct runtime_stat {
	struct rblist value_list;
};

typedef int (*aggr_get_id_t)(struct perf_stat_config *config,
			     struct perf_cpu_map *m, int cpu);

struct perf_stat_config {
	enum aggr_mode		 aggr_mode;
	bool			 scale;
	bool			 no_inherit;
	bool			 identifier;
	bool			 csv_output;
	bool			 interval_clear;
	bool			 metric_only;
	bool			 null_run;
	bool			 ru_display;
	bool			 big_num;
	bool			 no_merge;
	bool			 walltime_run_table;
	FILE			*output;
	unsigned int		 interval;
	unsigned int		 timeout;
	unsigned int		 initial_delay;
	unsigned int		 unit_width;
	unsigned int		 metric_only_len;
	int			 times;
	int			 run_count;
	int			 print_free_counters_hint;
	int			 print_mixed_hw_group_error;
	struct runtime_stat	*stats;
	int			 stats_num;
	const char		*csv_sep;
	struct stats		*walltime_nsecs_stats;
	struct rusage		 ru_data;
	struct perf_cpu_map		*aggr_map;
	aggr_get_id_t		 aggr_get_id;
	struct perf_cpu_map		*cpus_aggr_map;
	u64			*walltime_run;
	struct rblist		 metric_events;
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

struct evsel;
struct evlist;

struct perf_aggr_thread_value {
	struct evsel *counter;
	int id;
	double uval;
	u64 val;
	u64 run;
	u64 ena;
};

bool __perf_evsel_stat__is(struct evsel *evsel,
			   enum perf_stat_evsel_id id);

#define perf_stat_evsel__is(evsel, id) \
	__perf_evsel_stat__is(evsel, PERF_STAT_EVSEL_ID__ ## id)

extern struct runtime_stat rt_stat;
extern struct stats walltime_nsecs_stats;

typedef void (*print_metric_t)(struct perf_stat_config *config,
			       void *ctx, const char *color, const char *unit,
			       const char *fmt, double val);
typedef void (*new_line_t)(struct perf_stat_config *config, void *ctx);

void runtime_stat__init(struct runtime_stat *st);
void runtime_stat__exit(struct runtime_stat *st);
void perf_stat__init_shadow_stats(void);
void perf_stat__reset_shadow_stats(void);
void perf_stat__reset_shadow_per_stat(struct runtime_stat *st);
void perf_stat__update_shadow_stats(struct evsel *counter, u64 count,
				    int cpu, struct runtime_stat *st);
struct perf_stat_output_ctx {
	void *ctx;
	print_metric_t print_metric;
	new_line_t new_line;
	bool force_header;
};

void perf_stat__print_shadow_stats(struct perf_stat_config *config,
				   struct evsel *evsel,
				   double avg, int cpu,
				   struct perf_stat_output_ctx *out,
				   struct rblist *metric_events,
				   struct runtime_stat *st);
void perf_stat__collect_metric_expr(struct evlist *);

int perf_evlist__alloc_stats(struct evlist *evlist, bool alloc_raw);
void perf_evlist__free_stats(struct evlist *evlist);
void perf_evlist__reset_stats(struct evlist *evlist);
void perf_evlist__reset_prev_raw_counts(struct evlist *evlist);

int perf_stat_process_counter(struct perf_stat_config *config,
			      struct evsel *counter);
struct perf_tool;
union perf_event;
struct perf_session;
struct target;

int perf_event__process_stat_event(struct perf_session *session,
				   union perf_event *event);

size_t perf_event__fprintf_stat(union perf_event *event, FILE *fp);
size_t perf_event__fprintf_stat_round(union perf_event *event, FILE *fp);
size_t perf_event__fprintf_stat_config(union perf_event *event, FILE *fp);

int create_perf_stat_counter(struct evsel *evsel,
			     struct perf_stat_config *config,
			     struct target *target);
void
perf_evlist__print_counters(struct evlist *evlist,
			    struct perf_stat_config *config,
			    struct target *_target,
			    struct timespec *ts,
			    int argc, const char **argv);
#endif
