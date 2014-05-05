#ifndef _PERF_PERF_H
#define _PERF_PERF_H

#include "perf-sys.h"

#define ACCESS_ONCE(x) (*(volatile typeof(x) *)&(x))

#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>

#include <linux/types.h>
#include <linux/perf_event.h>

#ifndef NSEC_PER_SEC
# define NSEC_PER_SEC			1000000000ULL
#endif
#ifndef NSEC_PER_USEC
# define NSEC_PER_USEC			1000ULL
#endif

static inline unsigned long long rdclock(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

extern bool test_attr__enabled;
void test_attr__init(void);
void test_attr__open(struct perf_event_attr *attr, pid_t pid, int cpu,
		     int fd, int group_fd, unsigned long flags);

static inline int
sys_perf_event_open(struct perf_event_attr *attr,
		      pid_t pid, int cpu, int group_fd,
		      unsigned long flags)
{
	int fd;

	fd = syscall(__NR_perf_event_open, attr, pid, cpu,
		     group_fd, flags);

	if (unlikely(test_attr__enabled))
		test_attr__open(attr, pid, cpu, fd, group_fd, flags);

	return fd;
}

#define MAX_NR_CPUS			256

extern const char *input_name;
extern bool perf_host, perf_guest;
extern const char perf_version_string[];

void pthread__unblock_sigwinch(void);

#include "util/target.h"

struct record_opts {
	struct target target;
	int	     call_graph;
	bool         call_graph_enabled;
	bool	     group;
	bool	     inherit_stat;
	bool	     no_buffering;
	bool	     no_inherit;
	bool	     no_inherit_set;
	bool	     no_samples;
	bool	     raw_samples;
	bool	     sample_address;
	bool	     sample_weight;
	bool	     sample_time;
	bool	     period;
	unsigned int freq;
	unsigned int mmap_pages;
	unsigned int user_freq;
	u64          branch_stack;
	u64	     default_interval;
	u64	     user_interval;
	u16	     stack_dump_size;
	bool	     sample_transaction;
	unsigned     initial_delay;
};

#endif
