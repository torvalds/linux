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

#define MAX_NR_CPUS			1024

extern const char *input_name;
extern bool perf_host, perf_guest;
extern const char perf_version_string[];

void pthread__unblock_sigwinch(void);

#include "util/target.h"

struct record_opts {
	struct target target;
	bool	     group;
	bool	     inherit_stat;
	bool	     no_buffering;
	bool	     no_inherit;
	bool	     no_inherit_set;
	bool	     no_samples;
	bool	     raw_samples;
	bool	     sample_address;
	bool	     sample_phys_addr;
	bool	     sample_weight;
	bool	     sample_time;
	bool	     sample_time_set;
	bool	     sample_cpu;
	bool	     period;
	bool	     period_set;
	bool	     running_time;
	bool	     full_auxtrace;
	bool	     auxtrace_snapshot_mode;
	bool	     record_namespaces;
	bool	     record_switch_events;
	bool	     all_kernel;
	bool	     all_user;
	bool	     tail_synthesize;
	bool	     overwrite;
	bool	     ignore_missing_thread;
	bool	     strict_freq;
	bool	     sample_id;
	unsigned int freq;
	unsigned int mmap_pages;
	unsigned int auxtrace_mmap_pages;
	unsigned int user_freq;
	u64          branch_stack;
	u64	     sample_intr_regs;
	u64	     sample_user_regs;
	u64	     default_interval;
	u64	     user_interval;
	size_t	     auxtrace_snapshot_size;
	const char   *auxtrace_snapshot_opts;
	bool	     sample_transaction;
	unsigned     initial_delay;
	bool         use_clockid;
	clockid_t    clockid;
	unsigned int proc_map_timeout;
};

struct option;
extern const char * const *record_usage;
extern struct option *record_options;
extern int version_verbose;

int record__parse_freq(const struct option *opt, const char *str, int unset);
#endif
