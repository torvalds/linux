/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_EVSEL_H
#define __PERF_EVSEL_H 1

#include <linux/list.h>
#include <stdbool.h>
#include <sys/types.h>
#include <linux/perf_event.h>
#include <linux/types.h>
#include <internal/evsel.h>
#include <perf/evsel.h>
#include "symbol_conf.h"
#include <internal/cpumap.h>

struct bpf_object;
struct cgroup;
struct perf_counts;
struct perf_stat_evsel;
union perf_event;

typedef int (perf_evsel__sb_cb_t)(union perf_event *event, void *data);

enum perf_tool_event {
	PERF_TOOL_NONE		= 0,
	PERF_TOOL_DURATION_TIME = 1,
};

/** struct evsel - event selector
 *
 * @evlist - evlist this evsel is in, if it is in one.
 * @core - libperf evsel object
 * @name - Can be set to retain the original event name passed by the user,
 *         so that when showing results in tools such as 'perf stat', we
 *         show the name used, not some alias.
 * @id_pos: the position of the event id (PERF_SAMPLE_ID or
 *          PERF_SAMPLE_IDENTIFIER) in a sample event i.e. in the array of
 *          struct perf_record_sample
 * @is_pos: the position (counting backwards) of the event id (PERF_SAMPLE_ID or
 *          PERF_SAMPLE_IDENTIFIER) in a non-sample event i.e. if sample_id_all
 *          is used there is an id sample appended to non-sample events
 * @priv:   And what is in its containing unnamed union are tool specific
 */
struct evsel {
	struct perf_evsel	core;
	struct evlist	*evlist;
	char			*filter;
	struct perf_counts	*counts;
	struct perf_counts	*prev_raw_counts;
	int			idx;
	unsigned long		max_events;
	unsigned long		nr_events_printed;
	char			*name;
	double			scale;
	const char		*unit;
	struct tep_event	*tp_format;
	off_t			id_offset;
	struct perf_stat_evsel  *stats;
	void			*priv;
	u64			db_id;
	struct cgroup		*cgrp;
	void			*handler;
	unsigned int		sample_size;
	int			id_pos;
	int			is_pos;
	enum perf_tool_event	tool_event;
	bool			uniquified_name;
	bool			snapshot;
	bool 			supported;
	bool 			needs_swap;
	bool 			disabled;
	bool			no_aux_samples;
	bool			immediate;
	bool			tracking;
	bool			per_pkg;
	bool			precise_max;
	bool			ignore_missing_thread;
	bool			forced_leader;
	bool			use_uncore_alias;
	/* parse modifier helper */
	int			exclude_GH;
	int			sample_read;
	unsigned long		*per_pkg_mask;
	struct evsel		*leader;
	char			*group_name;
	bool			cmdline_group_boundary;
	struct list_head	config_terms;
	struct bpf_object	*bpf_obj;
	int			bpf_fd;
	int			err;
	bool			auto_merge_stats;
	bool			merged_stat;
	const char *		metric_expr;
	const char *		metric_name;
	struct evsel		**metric_events;
	struct evsel		*metric_leader;
	bool			collect_stat;
	bool			weak_group;
	bool			reset_group;
	bool			errored;
	bool			percore;
	int			cpu_iter;
	const char		*pmu_name;
	struct {
		perf_evsel__sb_cb_t	*cb;
		void			*data;
	} side_band;
};

struct perf_missing_features {
	bool sample_id_all;
	bool exclude_guest;
	bool mmap2;
	bool cloexec;
	bool clockid;
	bool clockid_wrong;
	bool lbr_flags;
	bool write_backward;
	bool group_read;
	bool ksymbol;
	bool bpf;
	bool aux_output;
};

extern struct perf_missing_features perf_missing_features;

struct perf_cpu_map;
struct target;
struct thread_map;
struct record_opts;

static inline struct perf_cpu_map *evsel__cpus(struct evsel *evsel)
{
	return perf_evsel__cpus(&evsel->core);
}

static inline int perf_evsel__nr_cpus(struct evsel *evsel)
{
	return evsel__cpus(evsel)->nr;
}

void perf_counts_values__scale(struct perf_counts_values *count,
			       bool scale, s8 *pscaled);

void perf_evsel__compute_deltas(struct evsel *evsel, int cpu, int thread,
				struct perf_counts_values *count);

int perf_evsel__object_config(size_t object_size,
			      int (*init)(struct evsel *evsel),
			      void (*fini)(struct evsel *evsel));

struct evsel *perf_evsel__new_idx(struct perf_event_attr *attr, int idx);

static inline struct evsel *evsel__new(struct perf_event_attr *attr)
{
	return perf_evsel__new_idx(attr, 0);
}

struct evsel *perf_evsel__newtp_idx(const char *sys, const char *name, int idx);

/*
 * Returns pointer with encoded error via <linux/err.h> interface.
 */
static inline struct evsel *perf_evsel__newtp(const char *sys, const char *name)
{
	return perf_evsel__newtp_idx(sys, name, 0);
}

struct evsel *perf_evsel__new_cycles(bool precise);

struct tep_event *event_format__new(const char *sys, const char *name);

void evsel__init(struct evsel *evsel, struct perf_event_attr *attr, int idx);
void perf_evsel__exit(struct evsel *evsel);
void evsel__delete(struct evsel *evsel);

struct callchain_param;

void perf_evsel__config(struct evsel *evsel,
			struct record_opts *opts,
			struct callchain_param *callchain);
void perf_evsel__config_callchain(struct evsel *evsel,
				  struct record_opts *opts,
				  struct callchain_param *callchain);

int __perf_evsel__sample_size(u64 sample_type);
void perf_evsel__calc_id_pos(struct evsel *evsel);

bool perf_evsel__is_cache_op_valid(u8 type, u8 op);

#define PERF_EVSEL__MAX_ALIASES 8

extern const char *perf_evsel__hw_cache[PERF_COUNT_HW_CACHE_MAX]
				       [PERF_EVSEL__MAX_ALIASES];
extern const char *perf_evsel__hw_cache_op[PERF_COUNT_HW_CACHE_OP_MAX]
					  [PERF_EVSEL__MAX_ALIASES];
extern const char *perf_evsel__hw_cache_result[PERF_COUNT_HW_CACHE_RESULT_MAX]
					      [PERF_EVSEL__MAX_ALIASES];
extern const char *perf_evsel__hw_names[PERF_COUNT_HW_MAX];
extern const char *perf_evsel__sw_names[PERF_COUNT_SW_MAX];
int __perf_evsel__hw_cache_type_op_res_name(u8 type, u8 op, u8 result,
					    char *bf, size_t size);
const char *perf_evsel__name(struct evsel *evsel);

const char *perf_evsel__group_name(struct evsel *evsel);
int perf_evsel__group_desc(struct evsel *evsel, char *buf, size_t size);

void __perf_evsel__set_sample_bit(struct evsel *evsel,
				  enum perf_event_sample_format bit);
void __perf_evsel__reset_sample_bit(struct evsel *evsel,
				    enum perf_event_sample_format bit);

#define perf_evsel__set_sample_bit(evsel, bit) \
	__perf_evsel__set_sample_bit(evsel, PERF_SAMPLE_##bit)

#define perf_evsel__reset_sample_bit(evsel, bit) \
	__perf_evsel__reset_sample_bit(evsel, PERF_SAMPLE_##bit)

void perf_evsel__set_sample_id(struct evsel *evsel,
			       bool use_sample_identifier);

int perf_evsel__set_filter(struct evsel *evsel, const char *filter);
int perf_evsel__append_tp_filter(struct evsel *evsel, const char *filter);
int perf_evsel__append_addr_filter(struct evsel *evsel,
				   const char *filter);
int evsel__enable_cpu(struct evsel *evsel, int cpu);
int evsel__enable(struct evsel *evsel);
int evsel__disable(struct evsel *evsel);
int evsel__disable_cpu(struct evsel *evsel, int cpu);

int perf_evsel__open_per_cpu(struct evsel *evsel,
			     struct perf_cpu_map *cpus,
			     int cpu);
int perf_evsel__open_per_thread(struct evsel *evsel,
				struct perf_thread_map *threads);
int evsel__open(struct evsel *evsel, struct perf_cpu_map *cpus,
		struct perf_thread_map *threads);
void evsel__close(struct evsel *evsel);

struct perf_sample;

void *perf_evsel__rawptr(struct evsel *evsel, struct perf_sample *sample,
			 const char *name);
u64 perf_evsel__intval(struct evsel *evsel, struct perf_sample *sample,
		       const char *name);

static inline char *perf_evsel__strval(struct evsel *evsel,
				       struct perf_sample *sample,
				       const char *name)
{
	return perf_evsel__rawptr(evsel, sample, name);
}

struct tep_format_field;

u64 format_field__intval(struct tep_format_field *field, struct perf_sample *sample, bool needs_swap);

struct tep_format_field *perf_evsel__field(struct evsel *evsel, const char *name);

#define perf_evsel__match(evsel, t, c)		\
	(evsel->core.attr.type == PERF_TYPE_##t &&	\
	 evsel->core.attr.config == PERF_COUNT_##c)

static inline bool perf_evsel__match2(struct evsel *e1,
				      struct evsel *e2)
{
	return (e1->core.attr.type == e2->core.attr.type) &&
	       (e1->core.attr.config == e2->core.attr.config);
}

#define perf_evsel__cmp(a, b)			\
	((a) &&					\
	 (b) &&					\
	 (a)->core.attr.type == (b)->core.attr.type &&	\
	 (a)->core.attr.config == (b)->core.attr.config)

int perf_evsel__read_counter(struct evsel *evsel, int cpu, int thread);

int __perf_evsel__read_on_cpu(struct evsel *evsel,
			      int cpu, int thread, bool scale);

/**
 * perf_evsel__read_on_cpu - Read out the results on a CPU and thread
 *
 * @evsel - event selector to read value
 * @cpu - CPU of interest
 * @thread - thread of interest
 */
static inline int perf_evsel__read_on_cpu(struct evsel *evsel,
					  int cpu, int thread)
{
	return __perf_evsel__read_on_cpu(evsel, cpu, thread, false);
}

/**
 * perf_evsel__read_on_cpu_scaled - Read out the results on a CPU and thread, scaled
 *
 * @evsel - event selector to read value
 * @cpu - CPU of interest
 * @thread - thread of interest
 */
static inline int perf_evsel__read_on_cpu_scaled(struct evsel *evsel,
						 int cpu, int thread)
{
	return __perf_evsel__read_on_cpu(evsel, cpu, thread, true);
}

int perf_evsel__parse_sample(struct evsel *evsel, union perf_event *event,
			     struct perf_sample *sample);

int perf_evsel__parse_sample_timestamp(struct evsel *evsel,
				       union perf_event *event,
				       u64 *timestamp);

static inline struct evsel *perf_evsel__next(struct evsel *evsel)
{
	return list_entry(evsel->core.node.next, struct evsel, core.node);
}

static inline struct evsel *perf_evsel__prev(struct evsel *evsel)
{
	return list_entry(evsel->core.node.prev, struct evsel, core.node);
}

/**
 * perf_evsel__is_group_leader - Return whether given evsel is a leader event
 *
 * @evsel - evsel selector to be tested
 *
 * Return %true if @evsel is a group leader or a stand-alone event
 */
static inline bool perf_evsel__is_group_leader(const struct evsel *evsel)
{
	return evsel->leader == evsel;
}

/**
 * perf_evsel__is_group_event - Return whether given evsel is a group event
 *
 * @evsel - evsel selector to be tested
 *
 * Return %true iff event group view is enabled and @evsel is a actual group
 * leader which has other members in the group
 */
static inline bool perf_evsel__is_group_event(struct evsel *evsel)
{
	if (!symbol_conf.event_group)
		return false;

	return perf_evsel__is_group_leader(evsel) && evsel->core.nr_members > 1;
}

bool perf_evsel__is_function_event(struct evsel *evsel);

static inline bool perf_evsel__is_bpf_output(struct evsel *evsel)
{
	return perf_evsel__match(evsel, SOFTWARE, SW_BPF_OUTPUT);
}

static inline bool perf_evsel__is_clock(struct evsel *evsel)
{
	return perf_evsel__match(evsel, SOFTWARE, SW_CPU_CLOCK) ||
	       perf_evsel__match(evsel, SOFTWARE, SW_TASK_CLOCK);
}

bool perf_evsel__fallback(struct evsel *evsel, int err,
			  char *msg, size_t msgsize);
int perf_evsel__open_strerror(struct evsel *evsel, struct target *target,
			      int err, char *msg, size_t size);

static inline int perf_evsel__group_idx(struct evsel *evsel)
{
	return evsel->idx - evsel->leader->idx;
}

/* Iterates group WITHOUT the leader. */
#define for_each_group_member(_evsel, _leader) 					\
for ((_evsel) = list_entry((_leader)->core.node.next, struct evsel, core.node); \
     (_evsel) && (_evsel)->leader == (_leader);					\
     (_evsel) = list_entry((_evsel)->core.node.next, struct evsel, core.node))

/* Iterates group WITH the leader. */
#define for_each_group_evsel(_evsel, _leader) 					\
for ((_evsel) = _leader; 							\
     (_evsel) && (_evsel)->leader == (_leader);					\
     (_evsel) = list_entry((_evsel)->core.node.next, struct evsel, core.node))

static inline bool perf_evsel__has_branch_callstack(const struct evsel *evsel)
{
	return evsel->core.attr.branch_sample_type & PERF_SAMPLE_BRANCH_CALL_STACK;
}

static inline bool evsel__has_callchain(const struct evsel *evsel)
{
	return (evsel->core.attr.sample_type & PERF_SAMPLE_CALLCHAIN) != 0;
}

struct perf_env *perf_evsel__env(struct evsel *evsel);

int perf_evsel__store_ids(struct evsel *evsel, struct evlist *evlist);
#endif /* __PERF_EVSEL_H */
