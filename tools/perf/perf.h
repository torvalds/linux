#ifndef _PERF_PERF_H
#define _PERF_PERF_H

#if defined(__i386__)
#include "../../arch/x86/include/asm/unistd.h"
#define rmb()		asm volatile("lock; addl $0,0(%%esp)" ::: "memory")
#define cpu_relax()	asm volatile("rep; nop" ::: "memory");
#endif

#if defined(__x86_64__)
#include "../../arch/x86/include/asm/unistd.h"
#define rmb()		asm volatile("lfence" ::: "memory")
#define cpu_relax()	asm volatile("rep; nop" ::: "memory");
#endif

#ifdef __powerpc__
#include "../../arch/powerpc/include/asm/unistd.h"
#define rmb()		asm volatile ("sync" ::: "memory")
#define cpu_relax()	asm volatile ("" ::: "memory");
#endif

#ifdef __s390__
#include "../../arch/s390/include/asm/unistd.h"
#define rmb()		asm volatile("bcr 15,0" ::: "memory")
#define cpu_relax()	asm volatile("" ::: "memory");
#endif

#ifdef __sh__
#include "../../arch/sh/include/asm/unistd.h"
#if defined(__SH4A__) || defined(__SH5__)
# define rmb()		asm volatile("synco" ::: "memory")
#else
# define rmb()		asm volatile("" ::: "memory")
#endif
#define cpu_relax()	asm volatile("" ::: "memory")
#endif

#ifdef __hppa__
#include "../../arch/parisc/include/asm/unistd.h"
#define rmb()		asm volatile("" ::: "memory")
#define cpu_relax()	asm volatile("" ::: "memory");
#endif

#ifdef __sparc__
#include "../../arch/sparc/include/asm/unistd.h"
#define rmb()		asm volatile("":::"memory")
#define cpu_relax()	asm volatile("":::"memory")
#endif

#ifdef __alpha__
#include "../../arch/alpha/include/asm/unistd.h"
#define rmb()		asm volatile("mb" ::: "memory")
#define cpu_relax()	asm volatile("" ::: "memory")
#endif

#ifdef __ia64__
#include "../../arch/ia64/include/asm/unistd.h"
#define rmb()		asm volatile ("mf" ::: "memory")
#define cpu_relax()	asm volatile ("hint @pause" ::: "memory")
#endif

#ifdef __arm__
#include "../../arch/arm/include/asm/unistd.h"
/*
 * Use the __kuser_memory_barrier helper in the CPU helper page. See
 * arch/arm/kernel/entry-armv.S in the kernel source for details.
 */
#define rmb()		asm volatile("mov r0, #0xffff0fff; mov lr, pc;" \
				     "sub pc, r0, #95" ::: "r0", "lr", "cc", \
				     "memory")
#define cpu_relax()	asm volatile("":::"memory")
#endif

#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>

#include "../../include/linux/perf_event.h"
#include "util/types.h"

/*
 * prctl(PR_TASK_PERF_EVENTS_DISABLE) will (cheaply) disable all
 * counters in the current task.
 */
#define PR_TASK_PERF_EVENTS_DISABLE   31
#define PR_TASK_PERF_EVENTS_ENABLE    32

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

#define __used		__attribute__((__unused__))

#define unlikely(x)	__builtin_expect(!!(x), 0)
#define min(x, y) ({				\
	typeof(x) _min1 = (x);			\
	typeof(y) _min2 = (y);			\
	(void) (&_min1 == &_min2);		\
	_min1 < _min2 ? _min1 : _min2; })

static inline int
sys_perf_event_open(struct perf_event_attr *attr,
		      pid_t pid, int cpu, int group_fd,
		      unsigned long flags)
{
	attr->size = sizeof(*attr);
	return syscall(__NR_perf_event_open, attr, pid, cpu,
		       group_fd, flags);
}

#define MAX_COUNTERS			256
#define MAX_NR_CPUS			256

struct ip_callchain {
	u64 nr;
	u64 ips[0];
};

#endif
