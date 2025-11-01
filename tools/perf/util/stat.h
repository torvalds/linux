/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_STATS_H
#define __PERF_STATS_H

#include <linux/types.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/resource.h>
#include "cpumap.h"
#include "counts.h"

struct perf_cpu_map;
struct perf_stat_config;
struct timespec;

struct stats {
	double n, mean, M2;
	u64 max, min;
};

/* hold aggregated event info */
struct perf_stat_aggr {
	/* aggregated values */
	struct perf_counts_values	counts;
	/* number of entries (CPUs) aggregated */
	int				nr;
	/* whether any entry has failed to read/process event */
	bool				failed;
	/* to mark this data is processed already */
	bool				used;
};

/* per-evsel event stats */
struct perf_stat_evsel {
	/* used for repeated runs */
	struct stats		 res_stats;
	/* number of allocated 'aggr' */
	int			 nr_aggr;
	/* aggregated event values */
	struct perf_stat_aggr	*aggr;
	/* used for group read */
	u64			*group_data;
};

enum aggr_mode {
	AGGR_NONE,
	AGGR_GLOBAL,
	AGGR_SOCKET,
	AGGR_DIE,
	AGGR_CLUSTER,
	AGGR_CACHE,
	AGGR_CORE,
	AGGR_THREAD,
	AGGR_UNSET,
	AGGR_NODE,
	AGGR_MAX
};

struct rusage_stats {
	struct stats ru_utime_usec_stat;
	struct stats ru_stime_usec_stat;
};

typedef struct aggr_cpu_id (*aggr_get_id_t)(struct perf_stat_config *config, struct perf_cpu cpu);

struct perf_stat_config {
	enum aggr_mode		 aggr_mode;
	u32			 aggr_level;
	bool			 scale;
	bool			 no_inherit;
	bool			 identifier;
	bool			 csv_output;
	bool			 json_output;
	bool			 interval_clear;
	bool			 metric_only;
	bool			 null_run;
	bool			 ru_display;
	bool			 big_num;
	bool			 hybrid_merge;
	bool			 walltime_run_table;
	bool			 all_kernel;
	bool			 all_user;
	bool			 percore_show_thread;
	bool			 summary;
	bool			 no_csv_summary;
	bool			 metric_no_group;
	bool			 metric_no_merge;
	bool			 metric_no_threshold;
	bool			 hardware_aware_grouping;
	bool			 stop_read_counter;
	bool			 iostat_run;
	char			 *user_requested_cpu_list;
	bool			 system_wide;
	FILE			*output;
	unsigned int		 interval;
	unsigned int		 timeout;
	unsigned int		 unit_width;
	unsigned int		 metric_only_len;
	int			 times;
	int			 run_count;
	int			 print_free_counters_hint;
	const char		*csv_sep;
	struct stats		*walltime_nsecs_stats;
	struct rusage		 ru_data;
	struct rusage_stats		 *ru_stats;
	struct cpu_aggr_map	*aggr_map;
	aggr_get_id_t		 aggr_get_id;
	struct cpu_aggr_map	*cpus_aggr_map;
	u64			*walltime_run;
	int			 ctl_fd;
	int			 ctl_fd_ack;
	bool			 ctl_fd_close;
	const char		*cgroup_list;
	unsigned int		topdown_level;
};

extern struct perf_stat_config stat_config;

void perf_stat__set_big_num(int set);

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

static inline void init_rusage_stats(struct rusage_stats *ru_stats) {
	init_stats(&ru_stats->ru_utime_usec_stat);
	init_stats(&ru_stats->ru_stime_usec_stat);
}

static inline void update_rusage_stats(struct rusage_stats *ru_stats, struct rusage* rusage) {
	const u64 us_to_ns = 1000;
	const u64 s_to_ns = 1000000000;
	update_stats(&ru_stats->ru_utime_usec_stat,
	             (rusage->ru_utime.tv_usec * us_to_ns + rusage->ru_utime.tv_sec * s_to_ns));
	update_stats(&ru_stats->ru_stime_usec_stat,
	             (rusage->ru_stime.tv_usec * us_to_ns + rusage->ru_stime.tv_sec * s_to_ns));
}

struct evsel;
struct evlist;

extern struct stats walltime_nsecs_stats;
extern struct rusage_stats ru_stats;

enum metric_threshold_classify {
	METRIC_THRESHOLD_UNKNOWN,
	METRIC_THRESHOLD_BAD,
	METRIC_THRESHOLD_NEARLY_BAD,
	METRIC_THRESHOLD_LESS_GOOD,
	METRIC_THRESHOLD_GOOD,
};
const char *metric_threshold_classify__color(enum metric_threshold_classify thresh);

typedef void (*print_metric_t)(struct perf_stat_config *config,
			       void *ctx,
			       enum metric_threshold_classify thresh,
			       const char *fmt,
			       const char *unit,
			       double val);
typedef void (*new_line_t)(struct perf_stat_config *config, void *ctx);

/* Used to print the display name of the Default metricgroup for now. */
typedef void (*print_metricgroup_header_t)(struct perf_stat_config *config,
					   void *ctx, const char *metricgroup_name);

void perf_stat__reset_shadow_stats(void);
struct perf_stat_output_ctx {
	void *ctx;
	print_metric_t print_metric;
	new_line_t new_line;
	print_metricgroup_header_t print_metricgroup_header;
	bool force_header;
};

void perf_stat__print_shadow_stats(struct perf_stat_config *config,
				   struct evsel *evsel,
				   double avg, int aggr_idx,
				   struct perf_stat_output_ctx *out);
bool perf_stat__skip_metric_event(struct evsel *evsel, u64 ena, u64 run);
void *perf_stat__print_shadow_stats_metricgroup(struct perf_stat_config *config,
						struct evsel *evsel,
						int aggr_idx,
						int *num,
						void *from,
						struct perf_stat_output_ctx *out);

int evlist__alloc_stats(struct perf_stat_config *config,
			struct evlist *evlist, bool alloc_raw);
void evlist__free_stats(struct evlist *evlist);
void evlist__reset_stats(struct evlist *evlist);
void evlist__reset_prev_raw_counts(struct evlist *evlist);
void evlist__copy_prev_raw_counts(struct evlist *evlist);
void evlist__save_aggr_prev_raw_counts(struct evlist *evlist);

int evlist__alloc_aggr_stats(struct evlist *evlist, int nr_aggr);
void evlist__reset_aggr_stats(struct evlist *evlist);
void evlist__copy_res_stats(struct perf_stat_config *config, struct evlist *evlist);

int perf_stat_process_counter(struct perf_stat_config *config,
			      struct evsel *counter);
void perf_stat_merge_counters(struct perf_stat_config *config, struct evlist *evlist);
void perf_stat_process_percore(struct perf_stat_config *config, struct evlist *evlist);

struct perf_tool;
union perf_event;
struct perf_session;
struct target;

int perf_event__process_stat_event(struct perf_session *session,
				   union perf_event *event);

size_t perf_event__fprintf_stat(union perf_event *event, FILE *fp);
size_t perf_event__fprintf_stat_round(union perf_event *event, FILE *fp);
size_t perf_event__fprintf_stat_config(union perf_event *event, FILE *fp);

void evlist__print_counters(struct evlist *evlist, struct perf_stat_config *config,
			    struct target *_target, struct timespec *ts, int argc, const char **argv);

struct metric_expr;
double test_generic_metric(struct metric_expr *mexp, int aggr_idx);
#endif
