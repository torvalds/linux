#ifndef __PERF_EVSEL_H
#define __PERF_EVSEL_H 1

#include <linux/list.h>
#include <stdbool.h>
#include "../../../include/linux/perf_event.h"
#include "types.h"
#include "xyarray.h"
 
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

struct perf_evsel {
	struct list_head	node;
	struct perf_event_attr	attr;
	char			*filter;
	struct xyarray		*fd;
	struct perf_counts	*counts;
	int			idx;
	void			*priv;
};

struct cpu_map;
struct thread_map;

struct perf_evsel *perf_evsel__new(struct perf_event_attr *attr, int idx);
void perf_evsel__delete(struct perf_evsel *evsel);

int perf_evsel__alloc_fd(struct perf_evsel *evsel, int ncpus, int nthreads);
int perf_evsel__alloc_counts(struct perf_evsel *evsel, int ncpus);
void perf_evsel__free_fd(struct perf_evsel *evsel);
void perf_evsel__close_fd(struct perf_evsel *evsel, int ncpus, int nthreads);

int perf_evsel__open_per_cpu(struct perf_evsel *evsel, struct cpu_map *cpus);
int perf_evsel__open_per_thread(struct perf_evsel *evsel, struct thread_map *threads);
int perf_evsel__open(struct perf_evsel *evsel, 
		     struct cpu_map *cpus, struct thread_map *threads);

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

#endif /* __PERF_EVSEL_H */
