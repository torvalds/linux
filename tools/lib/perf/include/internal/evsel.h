/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBPERF_INTERNAL_EVSEL_H
#define __LIBPERF_INTERNAL_EVSEL_H

#include <linux/types.h>
#include <linux/perf_event.h>
#include <stdbool.h>
#include <sys/types.h>
#include <internal/cpumap.h>

struct perf_thread_map;
struct xyarray;

/*
 * Per fd, to map back from PERF_SAMPLE_ID to evsel, only used when there are
 * more than one entry in the evlist.
 */
struct perf_sample_id {
	struct hlist_node	 node;
	u64			 id;
	struct perf_evsel	*evsel;
       /*
	* 'idx' will be used for AUX area sampling. A sample will have AUX area
	* data that will be queued for decoding, where there are separate
	* queues for each CPU (per-cpu tracing) or task (per-thread tracing).
	* The sample ID can be used to lookup 'idx' which is effectively the
	* queue number.
	*/
	int			 idx;
	struct perf_cpu		 cpu;
	pid_t			 tid;

	/* Holds total ID period value for PERF_SAMPLE_READ processing. */
	u64			 period;
};

struct perf_evsel {
	struct list_head	 node;
	struct perf_event_attr	 attr;
	struct perf_cpu_map	*cpus;
	struct perf_cpu_map	*own_cpus;
	struct perf_thread_map	*threads;
	struct xyarray		*fd;
	struct xyarray		*mmap;
	struct xyarray		*sample_id;
	u64			*id;
	u32			 ids;
	struct perf_evsel	*leader;

	/* parse modifier helper */
	int			 nr_members;
	/*
	 * system_wide is for events that need to be on every CPU, irrespective
	 * of user requested CPUs or threads. Map propagation will set cpus to
	 * this event's own_cpus, whereby they will contribute to evlist
	 * all_cpus.
	 */
	bool			 system_wide;
	/*
	 * Some events, for example uncore events, require a CPU.
	 * i.e. it cannot be the 'any CPU' value of -1.
	 */
	bool			 requires_cpu;
	int			 idx;
};

void perf_evsel__init(struct perf_evsel *evsel, struct perf_event_attr *attr,
		      int idx);
int perf_evsel__alloc_fd(struct perf_evsel *evsel, int ncpus, int nthreads);
void perf_evsel__close_fd(struct perf_evsel *evsel);
void perf_evsel__free_fd(struct perf_evsel *evsel);
int perf_evsel__read_size(struct perf_evsel *evsel);
int perf_evsel__apply_filter(struct perf_evsel *evsel, const char *filter);

int perf_evsel__alloc_id(struct perf_evsel *evsel, int ncpus, int nthreads);
void perf_evsel__free_id(struct perf_evsel *evsel);

#endif /* __LIBPERF_INTERNAL_EVSEL_H */
