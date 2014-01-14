#ifndef _PERF_PERF_H
#define _PERF_PERF_H

#include <asm/unistd.h>

#if defined(__i386__)
#define mb()		asm volatile("lock; addl $0,0(%%esp)" ::: "memory")
#define wmb()		asm volatile("lock; addl $0,0(%%esp)" ::: "memory")
#define rmb()		asm volatile("lock; addl $0,0(%%esp)" ::: "memory")
#define cpu_relax()	asm volatile("rep; nop" ::: "memory");
#define CPUINFO_PROC	"model name"
#ifndef __NR_perf_event_open
# define __NR_perf_event_open 336
#endif
#endif

#if defined(__x86_64__)
#define mb()		asm volatile("mfence" ::: "memory")
#define wmb()		asm volatile("sfence" ::: "memory")
#define rmb()		asm volatile("lfence" ::: "memory")
#define cpu_relax()	asm volatile("rep; nop" ::: "memory");
#define CPUINFO_PROC	"model name"
#ifndef __NR_perf_event_open
# define __NR_perf_event_open 298
#endif
#endif

#ifdef __powerpc__
#include "../../arch/powerpc/include/uapi/asm/unistd.h"
#define mb()		asm volatile ("sync" ::: "memory")
#define wmb()		asm volatile ("sync" ::: "memory")
#define rmb()		asm volatile ("sync" ::: "memory")
#define CPUINFO_PROC	"cpu"
#endif

#ifdef __s390__
#define mb()		asm volatile("bcr 15,0" ::: "memory")
#define wmb()		asm volatile("bcr 15,0" ::: "memory")
#define rmb()		asm volatile("bcr 15,0" ::: "memory")
#endif

#ifdef __sh__
#if defined(__SH4A__) || defined(__SH5__)
# define mb()		asm volatile("synco" ::: "memory")
# define wmb()		asm volatile("synco" ::: "memory")
# define rmb()		asm volatile("synco" ::: "memory")
#else
# define mb()		asm volatile("" ::: "memory")
# define wmb()		asm volatile("" ::: "memory")
# define rmb()		asm volatile("" ::: "memory")
#endif
#define CPUINFO_PROC	"cpu type"
#endif

#ifdef __hppa__
#define mb()		asm volatile("" ::: "memory")
#define wmb()		asm volatile("" ::: "memory")
#define rmb()		asm volatile("" ::: "memory")
#define CPUINFO_PROC	"cpu"
#endif

#ifdef __sparc__
#ifdef __LP64__
#define mb()		asm volatile("ba,pt %%xcc, 1f\n"	\
				     "membar #StoreLoad\n"	\
				     "1:\n":::"memory")
#else
#define mb()		asm volatile("":::"memory")
#endif
#define wmb()		asm volatile("":::"memory")
#define rmb()		asm volatile("":::"memory")
#define CPUINFO_PROC	"cpu"
#endif

#ifdef __alpha__
#define mb()		asm volatile("mb" ::: "memory")
#define wmb()		asm volatile("wmb" ::: "memory")
#define rmb()		asm volatile("mb" ::: "memory")
#define CPUINFO_PROC	"cpu model"
#endif

#ifdef __ia64__
#define mb()		asm volatile ("mf" ::: "memory")
#define wmb()		asm volatile ("mf" ::: "memory")
#define rmb()		asm volatile ("mf" ::: "memory")
#define cpu_relax()	asm volatile ("hint @pause" ::: "memory")
#define CPUINFO_PROC	"model name"
#endif

#ifdef __arm__
/*
 * Use the __kuser_memory_barrier helper in the CPU helper page. See
 * arch/arm/kernel/entry-armv.S in the kernel source for details.
 */
#define mb()		((void(*)(void))0xffff0fa0)()
#define wmb()		((void(*)(void))0xffff0fa0)()
#define rmb()		((void(*)(void))0xffff0fa0)()
#define CPUINFO_PROC	"Processor"
#endif

#ifdef __aarch64__
#define mb()		asm volatile("dmb ish" ::: "memory")
#define wmb()		asm volatile("dmb ishld" ::: "memory")
#define rmb()		asm volatile("dmb ishst" ::: "memory")
#define cpu_relax()	asm volatile("yield" ::: "memory")
#endif

#ifdef __mips__
#define mb()		asm volatile(					\
				".set	mips2\n\t"			\
				"sync\n\t"				\
				".set	mips0"				\
				: /* no output */			\
				: /* no input */			\
				: "memory")
#define wmb()	mb()
#define rmb()	mb()
#define CPUINFO_PROC	"cpu model"
#endif

#ifdef __arc__
#define mb()		asm volatile("" ::: "memory")
#define wmb()		asm volatile("" ::: "memory")
#define rmb()		asm volatile("" ::: "memory")
#define CPUINFO_PROC	"Processor"
#endif

#ifdef __metag__
#define mb()		asm volatile("" ::: "memory")
#define wmb()		asm volatile("" ::: "memory")
#define rmb()		asm volatile("" ::: "memory")
#define CPUINFO_PROC	"CPU"
#endif

#define barrier() asm volatile ("" ::: "memory")

#ifndef cpu_relax
#define cpu_relax() barrier()
#endif

#define ACCESS_ONCE(x) (*(volatile typeof(x) *)&(x))


#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>

#include <linux/perf_event.h>
#include "util/types.h"
#include <stdbool.h>

/*
 * prctl(PR_TASK_PERF_EVENTS_DISABLE) will (cheaply) disable all
 * counters in the current task.
 */
#define PR_TASK_PERF_EVENTS_DISABLE   31
#define PR_TASK_PERF_EVENTS_ENABLE    32

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

#define MAX_COUNTERS			256
#define MAX_NR_CPUS			256

struct ip_callchain {
	u64 nr;
	u64 ips[0];
};

struct branch_flags {
	u64 mispred:1;
	u64 predicted:1;
	u64 in_tx:1;
	u64 abort:1;
	u64 reserved:60;
};

struct branch_entry {
	u64				from;
	u64				to;
	struct branch_flags flags;
};

struct branch_stack {
	u64				nr;
	struct branch_entry	entries[0];
};

extern const char *input_name;
extern bool perf_host, perf_guest;
extern const char perf_version_string[];

void pthread__unblock_sigwinch(void);

#include "util/target.h"

enum perf_call_graph_mode {
	CALLCHAIN_NONE,
	CALLCHAIN_FP,
	CALLCHAIN_DWARF
};

struct record_opts {
	struct target target;
	int	     call_graph;
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
