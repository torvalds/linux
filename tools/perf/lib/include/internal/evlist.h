/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBPERF_INTERNAL_EVLIST_H
#define __LIBPERF_INTERNAL_EVLIST_H

struct perf_cpu_map;
struct perf_thread_map;

struct perf_evlist {
	struct list_head	 entries;
	int			 nr_entries;
	bool			 has_user_cpus;
	struct perf_cpu_map	*cpus;
	struct perf_thread_map	*threads;
};

#endif /* __LIBPERF_INTERNAL_EVLIST_H */
