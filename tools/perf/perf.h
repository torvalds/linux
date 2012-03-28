#ifndef _PERF_PERF_H
#define _PERF_PERF_H

struct winsize;

void get_term_dimensions(struct winsize *ws);

#if defined(__i386__)
#include "../../arch/x86/include/asm/unistd.h"
#define rmb()		asm volatile("lock; addl $0,0(%%esp)" ::: "memory")
#define cpu_relax()	asm volatile("rep; nop" ::: "memory");
#define CPUINFO_PROC	"model name"
#ifndef __NR_perf_event_open
# define __NR_perf_event_open 336
#endif
#endif

#if defined(__x86_64__)
#include "../../arch/x86/include/asm/unistd.h"
#define rmb()		asm volatile("lfence" ::: "memory")
#define cpu_relax()	asm volatile("rep; nop" ::: "memory");
#define CPUINFO_PROC	"model name"
#ifndef __NR_perf_event_open
# define __NR_perf_event_open 298
#endif
#endif

#ifdef __powerpc__
#include "../../arch/powerpc/include/asm/unistd.h"
#define rmb()		asm volatile ("sync" ::: "memory")
#define cpu_relax()	asm volatile ("" ::: "memory");
#define CPUINFO_PROC	"cpu"
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
#define CPUINFO_PROC	"cpu type"
#endif

#ifdef __hppa__
#include "../../arch/parisc/include/asm/unistd.h"
#define rmb()		asm volatile("" ::: "memory")
#define cpu_relax()	asm volatile("" ::: "memory");
#define CPUINFO_PROC	"cpu"
#endif

#ifdef __sparc__
#include "../../arch/sparc/include/asm/unistd.h"
#define rmb()		asm volatile("":::"memory")
#define cpu_relax()	asm volatile("":::"memory")
#define CPUINFO_PROC	"cpu"
#endif

#ifdef __alpha__
#include "../../arch/alpha/include/asm/unistd.h"
#define rmb()		asm volatile("mb" ::: "memory")
#define cpu_relax()	asm volatile("" ::: "memory")
#define CPUINFO_PROC	"cpu model"
#endif

#ifdef __ia64__
#include "../../arch/ia64/include/asm/unistd.h"
#define rmb()		asm volatile ("mf" ::: "memory")
#define cpu_relax()	asm volatile ("hint @pause" ::: "memory")
#define CPUINFO_PROC	"model name"
#endif

#ifdef __arm__
#include "../../arch/arm/include/asm/unistd.h"
/*
 * Use the __kuser_memory_barrier helper in the CPU helper page. See
 * arch/arm/kernel/entry-armv.S in the kernel source for details.
 */
#define rmb()		((void(*)(void))0xffff0fa0)()
#define cpu_relax()	asm volatile("":::"memory")
#define CPUINFO_PROC	"Processor"
#endif

#ifdef __mips__
#include "../../arch/mips/include/asm/unistd.h"
#define rmb()		asm volatile(					\
				".set	mips2\n\t"			\
				"sync\n\t"				\
				".set	mips0"				\
				: /* no output */			\
				: /* no input */			\
				: "memory")
#define cpu_relax()	asm volatile("" ::: "memory")
#define CPUINFO_PROC	"cpu model"
#endif

#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>

#include "../../include/linux/perf_event.h"
#include "util/types.h"
#include <stdbool.h>

struct perf_mmap {
	void			*base;
	int			mask;
	unsigned int		prev;
};

static inline unsigned int perf_mmap__read_head(struct perf_mmap *mm)
{
	struct perf_event_mmap_page *pc = mm->base;
	int head = pc->data_head;
	rmb();
	return head;
}

static inline void perf_mmap__write_tail(struct perf_mmap *md,
					 unsigned long tail)
{
	struct perf_event_mmap_page *pc = md->base;

	/*
	 * ensure all reads are done before we write the tail out.
	 */
	/* mb(); */
	pc->data_tail = tail;
}

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
	return syscall(__NR_perf_event_open, attr, pid, cpu,
		       group_fd, flags);
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
	u64 reserved:62;
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

extern bool perf_host, perf_guest;
extern const char perf_version_string[];

void pthread__unblock_sigwinch(void);

struct perf_record_opts {
	const char   *target_pid;
	const char   *target_tid;
	uid_t	     uid;
	bool	     call_graph;
	bool	     group;
	bool	     inherit_stat;
	bool	     no_delay;
	bool	     no_inherit;
	bool	     no_samples;
	bool	     pipe_output;
	bool	     raw_samples;
	bool	     sample_address;
	bool	     sample_time;
	bool	     sample_id_all_missing;
	bool	     exclude_guest_missing;
	bool	     system_wide;
	bool	     period;
	unsigned int freq;
	unsigned int mmap_pages;
	unsigned int user_freq;
	int	     branch_stack;
	u64	     default_interval;
	u64	     user_interval;
	const char   *cpu_list;
};

#endif
