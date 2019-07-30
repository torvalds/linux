/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBPERF_INTERNAL_THREADMAP_H
#define __LIBPERF_INTERNAL_THREADMAP_H

#include <linux/refcount.h>
#include <sys/types.h>
#include <unistd.h>

struct thread_map_data {
	pid_t	 pid;
	char	*comm;
};

struct perf_thread_map {
	refcount_t	refcnt;
	int		nr;
	int		err_thread;
	struct thread_map_data map[];
};

struct perf_thread_map *perf_thread_map__realloc(struct perf_thread_map *map, int nr);

#endif /* __LIBPERF_INTERNAL_THREADMAP_H */
