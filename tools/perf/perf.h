#ifndef _PERF_PERF_H
#define _PERF_PERF_H

#if defined(__x86_64__) || defined(__i386__)
#include "../../arch/x86/include/asm/unistd.h"
#define rmb()		asm volatile("lfence" ::: "memory")
#define cpu_relax()	asm volatile("rep; nop" ::: "memory");
#endif

#ifdef __powerpc__
#include "../../arch/powerpc/include/asm/unistd.h"
#define rmb()		asm volatile ("sync" ::: "memory")
#define cpu_relax()	asm volatile ("" ::: "memory");
#endif

#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>

#include "../../include/linux/perf_counter.h"
#include "types.h"

/*
 * prctl(PR_TASK_PERF_COUNTERS_DISABLE) will (cheaply) disable all
 * counters in the current task.
 */
#define PR_TASK_PERF_COUNTERS_DISABLE   31
#define PR_TASK_PERF_COUNTERS_ENABLE    32

#ifndef NSEC_PER_SEC
# define NSEC_PER_SEC			1000000000ULL
#endif

static inline unsigned long long rdclock(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/*
 * Pick up some kernel type conventions:
 */
#define __user
#define asmlinkage

#define unlikely(x)	__builtin_expect(!!(x), 0)
#define min(x, y) ({				\
	typeof(x) _min1 = (x);			\
	typeof(y) _min2 = (y);			\
	(void) (&_min1 == &_min2);		\
	_min1 < _min2 ? _min1 : _min2; })

static inline int
sys_perf_counter_open(struct perf_counter_attr *attr,
		      pid_t pid, int cpu, int group_fd,
		      unsigned long flags)
{
	attr->size = sizeof(*attr);
	return syscall(__NR_perf_counter_open, attr, pid, cpu,
		       group_fd, flags);
}

#define MAX_COUNTERS			256
#define MAX_NR_CPUS			256

struct perf_file_header {
	u64	version;
	u64	sample_type;
	u64	data_size;
};

#endif
