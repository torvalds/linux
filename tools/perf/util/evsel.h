#ifndef __PERF_EVSEL_H
#define __PERF_EVSEL_H 1

#include <linux/list.h>
#include <stdbool.h>
#include "../../../include/linux/perf_event.h"
#include "types.h"
#include "xyarray.h"
#include "cgroup.h"
#include "hist.h"
 
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
	s8		   	  scaled;
	struct perf_counts_values aggr;
	struct perf_counts_values cpu[];
};

struct perf_evsel;

/*
 * Per fd, to map back from PERF_SAMPLE_ID to evsel, only used when there are
 * more than one entry in the evlist.
 */
struct perf_sample_id {
	struct hlist_node 	node;
	u64		 	id;
	struct perf_evsel	*evsel;
};

/** struct perf_evsel - event selector
 *
 * @name - Can be set to retain the original event name passed by the user,
 *         so that when showing results in tools such as 'perf stat', we
 *         show the name used, not some alias.
 */
struct perf_evsel {
	struct list_head	node;
	struct perf_event_attr	attr;
	char			*filter;
	struct xyarray		*fd;
	struct xyarray		*sample_id;
	u64			*id;
	struct perf_counts	*counts;
	int			idx;
	int			ids;
	struct hists		hists;
	char			*name;
	struct event_format	*tp_format;
	union {
		void		*priv;
		off_t		id_offset;
	};
	struct cgroup_sel	*cgrp;
	struct {
		void		*func;
		void		*data;
	} handler;
	unsigned int		sample_size;
	bool 			supported;
};

struct cpu_map;
struct thread_map;
struct perf_evlist;
struct perf_record_opts;

struct perf_evsel *perf_evsel__new(struct perf_event_attr *attr, int idx);
void perf_evsel__init(struct perf_evsel *evsel,
		      struct perf_event_attr *attr, int idx);
void perf_evsel__exit(struct perf_evsel *evsel);
void perf_evsel__delete(struct perf_evsel *evsel);

void perf_evsel__config(struct perf_evsel *evsel,
			struct perf_record_opts *opts,
			struct perf_evsel *first);

bool perf_evsel__is_cache_op_valid(u8 type, u8 op);

#define PERF_EVSEL__MAX_ALIASES 8

extern const char *perf_evsel__hw_cache[PERF_COUNT_HW_CACHE_MAX]
				       [PERF_EVSEL__MAX_ALIASES];
extern const char *perf_evsel__hw_cache_op[PERF_COUNT_HW_CACHE_OP_MAX]
					  [PERF_EVSEL__MAX_ALIASES];
const char *perf_evsel__hw_cache_result[PERF_COUNT_HW_CACHE_RESULT_MAX]
				       [PERF_EVSEL__MAX_ALIASES];
int __perf_evsel__hw_cache_type_op_res_name(u8 type, u8 op, u8 result,
					    char *bf, size_t size);
const char *perf_evsel__name(struct perf_evsel *evsel);

int perf_evsel__alloc_fd(struct perf_evsel *evsel, int ncpus, int nthreads);
int perf_evsel__alloc_id(struct perf_evsel *evsel, int ncpus, int nthreads);
int perf_evsel__alloc_counts(struct perf_evsel *evsel, int ncpus);
void perf_evsel__free_fd(struct perf_evsel *evsel);
void perf_evsel__free_id(struct perf_evsel *evsel);
void perf_evsel__close_fd(struct perf_evsel *evsel, int ncpus, int nthreads);

int perf_evsel__open_per_cpu(struct perf_evsel *evsel,
			     struct cpu_map *cpus, bool group,
			     struct xyarray *group_fds);
int perf_evsel__open_per_thread(struct perf_evsel *evsel,
				struct thread_map *threads, bool group,
				struct xyarray *group_fds);
int perf_evsel__open(struct perf_evsel *evsel, struct cpu_map *cpus,
		     struct thread_map *threads, bool group,
		     struct xyarray *group_fds);
void perf_evsel__close(struct perf_evsel *evsel, int ncpus, int nthreads);

#define perf_evsel__match(evsel, t, c)		\
	(evsel->attr.type == PERF_TYPE_##t &&	\
	 evsel->attr.config == PERF_COUNT_##c)

int __perf_evsel__read_on_cpu(struct perf_evsel *evsel,
			      int cpu, int thread, bool scale);

/**
 * perf_evsel__read_on_cpu - Read out the results on a CPU and thread
 *
 * @evsel - event selector to read value
 * @cpu - CPU of interest
 * @thread - thread of interest
 */
static inline int perf_evsel__read_on_cpu(struct perf_evsel *evsel,
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
static inline int perf_evsel__read_on_cpu_scaled(struct perf_evsel *evsel,
						 int cpu, int thread)
{
	return __perf_evsel__read_on_cpu(evsel, cpu, thread, true);
}

int __perf_evsel__read(struct perf_evsel *evsel, int ncpus, int nthreads,
		       bool scale);

/**
 * perf_evsel__read - Read the aggregate results on all CPUs
 *
 * @evsel - event selector to read value
 * @ncpus - Number of cpus affected, from zero
 * @nthreads - Number of threads affected, from zero
 */
static inline int perf_evsel__read(struct perf_evsel *evsel,
				    int ncpus, int nthreads)
{
	return __perf_evsel__read(evsel, ncpus, nthreads, false);
}

/**
 * perf_evsel__read_scaled - Read the aggregate results on all CPUs, scaled
 *
 * @evsel - event selector to read value
 * @ncpus - Number of cpus affected, from zero
 * @nthreads - Number of threads affected, from zero
 */
static inline int perf_evsel__read_scaled(struct perf_evsel *evsel,
					  int ncpus, int nthreads)
{
	return __perf_evsel__read(evsel, ncpus, nthreads, true);
}

void hists__init(struct hists *hists);

int perf_evsel__parse_sample(struct perf_evsel *evsel, union perf_event *event,
			     struct perf_sample *sample, bool swapped);
#endif /* __PERF_EVSEL_H */
