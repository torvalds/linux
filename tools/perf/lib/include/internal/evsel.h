/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBPERF_INTERNAL_EVSEL_H
#define __LIBPERF_INTERNAL_EVSEL_H

#include <linux/types.h>
#include <linux/perf_event.h>

struct perf_cpu_map;
struct perf_thread_map;

struct perf_evsel {
	struct list_head	 node;
	struct perf_event_attr	 attr;
	struct perf_cpu_map	*cpus;
	struct perf_cpu_map	*own_cpus;
	struct perf_thread_map	*threads;
	struct xyarray		*fd;

	/* parse modifier helper */
	int			 nr_members;
};

int perf_evsel__alloc_fd(struct perf_evsel *evsel, int ncpus, int nthreads);
void perf_evsel__close_fd(struct perf_evsel *evsel);
void perf_evsel__free_fd(struct perf_evsel *evsel);
int perf_evsel__read_size(struct perf_evsel *evsel);
int perf_evsel__apply_filter(struct perf_evsel *evsel, const char *filter);

#endif /* __LIBPERF_INTERNAL_EVSEL_H */
