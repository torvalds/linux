/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PERF_PERF_H
#define _PERF_PERF_H

#include <time.h>
#include <stdbool.h>
#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/perf_event.h>

extern bool test_attr__enabled;
void test_attr__ready(void);
void test_attr__init(void);
void test_attr__open(struct perf_event_attr *attr, pid_t pid, int cpu,
		     int fd, int group_fd, unsigned long flags);

#define HAVE_ATTR_TEST
#include "perf-sys.h"

static inline unsigned long long rdclock(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

#ifndef MAX_NR_CPUS
#define MAX_NR_CPUS			2048
#endif

extern const char *input_name;
extern bool perf_host, perf_guest;
extern const char perf_version_string[];

void pthread__unblock_sigwinch(void);

enum perf_affinity {
	PERF_AFFINITY_SYS = 0,
	PERF_AFFINITY_NODE,
	PERF_AFFINITY_CPU,
	PERF_AFFINITY_MAX
};

extern int version_verbose;
#endif
