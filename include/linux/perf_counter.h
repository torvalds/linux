/*
 *  Performance counters:
 *
 *    Copyright (C) 2008-2009, Thomas Gleixner <tglx@linutronix.de>
 *    Copyright (C) 2008-2009, Red Hat, Inc., Ingo Molnar
 *    Copyright (C) 2008-2009, Red Hat, Inc., Peter Zijlstra
 *
 *  Data type definitions, declarations, prototypes.
 *
 *    Started by: Thomas Gleixner and Ingo Molnar
 *
 *  For licencing details see kernel-base/COPYING
 */
#ifndef _LINUX_PERF_COUNTER_H
#define _LINUX_PERF_COUNTER_H

#include <linux/types.h>
#include <linux/ioctl.h>
#include <asm/byteorder.h>

/*
 * User-space ABI bits:
 */

/*
 * attr.type
 */
enum perf_type_id {
	PERF_TYPE_HARDWARE			= 0,
	PERF_TYPE_SOFTWARE			= 1,
	PERF_TYPE_TRACEPOINT			= 2,
	PERF_TYPE_HW_CACHE			= 3,
	PERF_TYPE_RAW				= 4,

	PERF_TYPE_MAX,				/* non-ABI */
};

/*
 * Generalized performance counter event types, used by the
 * attr.event_id parameter of the sys_perf_counter_open()
 * syscall:
 */
enum perf_hw_id {
	/*
	 * Common hardware events, generalized by the kernel:
	 */
	PERF_COUNT_HW_CPU_CYCLES		= 0,
	PERF_COUNT_HW_INSTRUCTIONS		= 1,
	PERF_COUNT_HW_CACHE_REFERENCES		= 2,
	PERF_COUNT_HW_CACHE_MISSES		= 3,
	PERF_COUNT_HW_BRANCH_INSTRUCTIONS	= 4,
	PERF_COUNT_HW_BRANCH_MISSES		= 5,
	PERF_COUNT_HW_BUS_CYCLES		= 6,

	PERF_COUNT_HW_MAX,			/* non-ABI */
};

/*
 * Generalized hardware cache counters:
 *
 *       { L1-D, L1-I, LLC, ITLB, DTLB, BPU } x
 *       { read, write, prefetch } x
 *       { accesses, misses }
 */
enum perf_hw_cache_id {
	PERF_COUNT_HW_CACHE_L1D			= 0,
	PERF_COUNT_HW_CACHE_L1I			= 1,
	PERF_COUNT_HW_CACHE_LL			= 2,
	PERF_COUNT_HW_CACHE_DTLB		= 3,
	PERF_COUNT_HW_CACHE_ITLB		= 4,
	PERF_COUNT_HW_CACHE_BPU			= 5,

	PERF_COUNT_HW_CACHE_MAX,		/* non-ABI */
};

enum perf_hw_cache_op_id {
	PERF_COUNT_HW_CACHE_OP_READ		= 0,
	PERF_COUNT_HW_CACHE_OP_WRITE		= 1,
	PERF_COUNT_HW_CACHE_OP_PREFETCH		= 2,

	PERF_COUNT_HW_CACHE_OP_MAX,		/* non-ABI */
};

enum perf_hw_cache_op_result_id {
	PERF_COUNT_HW_CACHE_RESULT_ACCESS	= 0,
	PERF_COUNT_HW_CACHE_RESULT_MISS		= 1,

	PERF_COUNT_HW_CACHE_RESULT_MAX,		/* non-ABI */
};

/*
 * Special "software" counters provided by the kernel, even if the hardware
 * does not support performance counters. These counters measure various
 * physical and sw events of the kernel (and allow the profiling of them as
 * well):
 */
enum perf_sw_ids {
	PERF_COUNT_SW_CPU_CLOCK			= 0,
	PERF_COUNT_SW_TASK_CLOCK		= 1,
	PERF_COUNT_SW_PAGE_FAULTS		= 2,
	PERF_COUNT_SW_CONTEXT_SWITCHES		= 3,
	PERF_COUNT_SW_CPU_MIGRATIONS		= 4,
	PERF_COUNT_SW_PAGE_FAULTS_MIN		= 5,
	PERF_COUNT_SW_PAGE_FAULTS_MAJ		= 6,

	PERF_COUNT_SW_MAX,			/* non-ABI */
};

/*
 * Bits that can be set in attr.sample_type to request information
 * in the overflow packets.
 */
enum perf_counter_sample_format {
	PERF_SAMPLE_IP				= 1U << 0,
	PERF_SAMPLE_TID				= 1U << 1,
	PERF_SAMPLE_TIME			= 1U << 2,
	PERF_SAMPLE_ADDR			= 1U << 3,
	PERF_SAMPLE_GROUP			= 1U << 4,
	PERF_SAMPLE_CALLCHAIN			= 1U << 5,
	PERF_SAMPLE_ID				= 1U << 6,
	PERF_SAMPLE_CPU				= 1U << 7,
	PERF_SAMPLE_PERIOD			= 1U << 8,

	PERF_SAMPLE_MAX = 1U << 9,		/* non-ABI */
};

/*
 * Bits that can be set in attr.read_format to request that
 * reads on the counter should return the indicated quantities,
 * in increasing order of bit value, after the counter value.
 */
enum perf_counter_read_format {
	PERF_FORMAT_TOTAL_TIME_ENABLED		= 1U << 0,
	PERF_FORMAT_TOTAL_TIME_RUNNING		= 1U << 1,
	PERF_FORMAT_ID				= 1U << 2,

	PERF_FORMAT_MAX = 1U << 3, 		/* non-ABI */
};

#define PERF_ATTR_SIZE_VER0	64	/* sizeof first published struct */

/*
 * Hardware event to monitor via a performance monitoring counter:
 */
struct perf_counter_attr {

	/*
	 * Major type: hardware/software/tracepoint/etc.
	 */
	__u32			type;

	/*
	 * Size of the attr structure, for fwd/bwd compat.
	 */
	__u32			size;

	/*
	 * Type specific configuration information.
	 */
	__u64			config;

	union {
		__u64		sample_period;
		__u64		sample_freq;
	};

	__u64			sample_type;
	__u64			read_format;

	__u64			disabled       :  1, /* off by default        */
				inherit	       :  1, /* children inherit it   */
				pinned	       :  1, /* must always be on PMU */
				exclusive      :  1, /* only group on PMU     */
				exclude_user   :  1, /* don't count user      */
				exclude_kernel :  1, /* ditto kernel          */
				exclude_hv     :  1, /* ditto hypervisor      */
				exclude_idle   :  1, /* don't count when idle */
				mmap           :  1, /* include mmap data     */
				comm	       :  1, /* include comm data     */
				freq           :  1, /* use freq, not period  */

				__reserved_1   : 53;

	__u32			wakeup_events;	/* wakeup every n events */
	__u32			__reserved_2;

	__u64			__reserved_3;
};

/*
 * Ioctls that can be done on a perf counter fd:
 */
#define PERF_COUNTER_IOC_ENABLE		_IO ('$', 0)
#define PERF_COUNTER_IOC_DISABLE	_IO ('$', 1)
#define PERF_COUNTER_IOC_REFRESH	_IO ('$', 2)
#define PERF_COUNTER_IOC_RESET		_IO ('$', 3)
#define PERF_COUNTER_IOC_PERIOD		_IOW('$', 4, u64)

enum perf_counter_ioc_flags {
	PERF_IOC_FLAG_GROUP		= 1U << 0,
};

/*
 * Structure of the page that can be mapped via mmap
 */
struct perf_counter_mmap_page {
	__u32	version;		/* version number of this structure */
	__u32	compat_version;		/* lowest version this is compat with */

	/*
	 * Bits needed to read the hw counters in user-space.
	 *
	 *   u32 seq;
	 *   s64 count;
	 *
	 *   do {
	 *     seq = pc->lock;
	 *
	 *     barrier()
	 *     if (pc->index) {
	 *       count = pmc_read(pc->index - 1);
	 *       count += pc->offset;
	 *     } else
	 *       goto regular_read;
	 *
	 *     barrier();
	 *   } while (pc->lock != seq);
	 *
	 * NOTE: for obvious reason this only works on self-monitoring
	 *       processes.
	 */
	__u32	lock;			/* seqlock for synchronization */
	__u32	index;			/* hardware counter identifier */
	__s64	offset;			/* add to hardware counter value */

	/*
	 * Control data for the mmap() data buffer.
	 *
	 * User-space reading the @data_head value should issue an rmb(), on
	 * SMP capable platforms, after reading this value -- see
	 * perf_counter_wakeup().
	 *
	 * When the mapping is PROT_WRITE the @data_tail value should be
	 * written by userspace to reflect the last read data. In this case
	 * the kernel will not over-write unread data.
	 */
	__u64   data_head;		/* head in the data section */
	__u64	data_tail;		/* user-space written tail */
};

#define PERF_EVENT_MISC_CPUMODE_MASK		(3 << 0)
#define PERF_EVENT_MISC_CPUMODE_UNKNOWN		(0 << 0)
#define PERF_EVENT_MISC_KERNEL			(1 << 0)
#define PERF_EVENT_MISC_USER			(2 << 0)
#define PERF_EVENT_MISC_HYPERVISOR		(3 << 0)
#define PERF_EVENT_MISC_OVERFLOW		(1 << 2)

struct perf_event_header {
	__u32	type;
	__u16	misc;
	__u16	size;
};

enum perf_event_type {

	/*
	 * The MMAP events record the PROT_EXEC mappings so that we can
	 * correlate userspace IPs to code. They have the following structure:
	 *
	 * struct {
	 *	struct perf_event_header	header;
	 *
	 *	u32				pid, tid;
	 *	u64				addr;
	 *	u64				len;
	 *	u64				pgoff;
	 *	char				filename[];
	 * };
	 */
	PERF_EVENT_MMAP			= 1,

	/*
	 * struct {
	 * 	struct perf_event_header	header;
	 * 	u64				id;
	 * 	u64				lost;
	 * };
	 */
	PERF_EVENT_LOST			= 2,

	/*
	 * struct {
	 *	struct perf_event_header	header;
	 *
	 *	u32				pid, tid;
	 *	char				comm[];
	 * };
	 */
	PERF_EVENT_COMM			= 3,

	/*
	 * struct {
	 *	struct perf_event_header	header;
	 *	u64				time;
	 *	u64				id;
	 *	u64				sample_period;
	 * };
	 */
	PERF_EVENT_PERIOD		= 4,

	/*
	 * struct {
	 *	struct perf_event_header	header;
	 *	u64				time;
	 *	u64				id;
	 * };
	 */
	PERF_EVENT_THROTTLE		= 5,
	PERF_EVENT_UNTHROTTLE		= 6,

	/*
	 * struct {
	 *	struct perf_event_header	header;
	 *	u32				pid, ppid;
	 * };
	 */
	PERF_EVENT_FORK			= 7,

	/*
	 * When header.misc & PERF_EVENT_MISC_OVERFLOW the event_type field
	 * will be PERF_SAMPLE_*
	 *
	 * struct {
	 *	struct perf_event_header	header;
	 *
	 *	{ u64			ip;	  } && PERF_SAMPLE_IP
	 *	{ u32			pid, tid; } && PERF_SAMPLE_TID
	 *	{ u64			time;     } && PERF_SAMPLE_TIME
	 *	{ u64			addr;     } && PERF_SAMPLE_ADDR
	 *	{ u64			config;   } && PERF_SAMPLE_CONFIG
	 *	{ u32			cpu, res; } && PERF_SAMPLE_CPU
	 *
	 *	{ u64			nr;
	 *	  { u64 id, val; }	cnt[nr];  } && PERF_SAMPLE_GROUP
	 *
	 *	{ u64			nr,
	 *	  u64			ips[nr];  } && PERF_SAMPLE_CALLCHAIN
	 * };
	 */
};

enum perf_callchain_context {
	PERF_CONTEXT_HV			= (__u64)-32,
	PERF_CONTEXT_KERNEL		= (__u64)-128,
	PERF_CONTEXT_USER		= (__u64)-512,

	PERF_CONTEXT_GUEST		= (__u64)-2048,
	PERF_CONTEXT_GUEST_KERNEL	= (__u64)-2176,
	PERF_CONTEXT_GUEST_USER		= (__u64)-2560,

	PERF_CONTEXT_MAX		= (__u64)-4095,
};

#ifdef __KERNEL__
/*
 * Kernel-internal data types and definitions:
 */

#ifdef CONFIG_PERF_COUNTERS
# include <asm/perf_counter.h>
#endif

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/rculist.h>
#include <linux/rcupdate.h>
#include <linux/spinlock.h>
#include <linux/hrtimer.h>
#include <linux/fs.h>
#include <linux/pid_namespace.h>
#include <asm/atomic.h>

#define PERF_MAX_STACK_DEPTH		255

struct perf_callchain_entry {
	__u64				nr;
	__u64				ip[PERF_MAX_STACK_DEPTH];
};

struct task_struct;

/**
 * struct hw_perf_counter - performance counter hardware details:
 */
struct hw_perf_counter {
#ifdef CONFIG_PERF_COUNTERS
	union {
		struct { /* hardware */
			u64		config;
			unsigned long	config_base;
			unsigned long	counter_base;
			int		idx;
		};
		union { /* software */
			atomic64_t	count;
			struct hrtimer	hrtimer;
		};
	};
	atomic64_t			prev_count;
	u64				sample_period;
	u64				last_period;
	atomic64_t			period_left;
	u64				interrupts;

	u64				freq_count;
	u64				freq_interrupts;
	u64				freq_stamp;
#endif
};

struct perf_counter;

/**
 * struct pmu - generic performance monitoring unit
 */
struct pmu {
	int (*enable)			(struct perf_counter *counter);
	void (*disable)			(struct perf_counter *counter);
	void (*read)			(struct perf_counter *counter);
	void (*unthrottle)		(struct perf_counter *counter);
};

/**
 * enum perf_counter_active_state - the states of a counter
 */
enum perf_counter_active_state {
	PERF_COUNTER_STATE_ERROR	= -2,
	PERF_COUNTER_STATE_OFF		= -1,
	PERF_COUNTER_STATE_INACTIVE	=  0,
	PERF_COUNTER_STATE_ACTIVE	=  1,
};

struct file;

struct perf_mmap_data {
	struct rcu_head			rcu_head;
	int				nr_pages;	/* nr of data pages  */
	int				writable;	/* are we writable   */
	int				nr_locked;	/* nr pages mlocked  */

	atomic_t			poll;		/* POLL_ for wakeups */
	atomic_t			events;		/* event limit       */

	atomic_long_t			head;		/* write position    */
	atomic_long_t			done_head;	/* completed head    */

	atomic_t			lock;		/* concurrent writes */
	atomic_t			wakeup;		/* needs a wakeup    */
	atomic_t			lost;		/* nr records lost   */

	struct perf_counter_mmap_page   *user_page;
	void				*data_pages[0];
};

struct perf_pending_entry {
	struct perf_pending_entry *next;
	void (*func)(struct perf_pending_entry *);
};

/**
 * struct perf_counter - performance counter kernel representation:
 */
struct perf_counter {
#ifdef CONFIG_PERF_COUNTERS
	struct list_head		list_entry;
	struct list_head		event_entry;
	struct list_head		sibling_list;
	int				nr_siblings;
	struct perf_counter		*group_leader;
	const struct pmu		*pmu;

	enum perf_counter_active_state	state;
	atomic64_t			count;

	/*
	 * These are the total time in nanoseconds that the counter
	 * has been enabled (i.e. eligible to run, and the task has
	 * been scheduled in, if this is a per-task counter)
	 * and running (scheduled onto the CPU), respectively.
	 *
	 * They are computed from tstamp_enabled, tstamp_running and
	 * tstamp_stopped when the counter is in INACTIVE or ACTIVE state.
	 */
	u64				total_time_enabled;
	u64				total_time_running;

	/*
	 * These are timestamps used for computing total_time_enabled
	 * and total_time_running when the counter is in INACTIVE or
	 * ACTIVE state, measured in nanoseconds from an arbitrary point
	 * in time.
	 * tstamp_enabled: the notional time when the counter was enabled
	 * tstamp_running: the notional time when the counter was scheduled on
	 * tstamp_stopped: in INACTIVE state, the notional time when the
	 *	counter was scheduled off.
	 */
	u64				tstamp_enabled;
	u64				tstamp_running;
	u64				tstamp_stopped;

	struct perf_counter_attr	attr;
	struct hw_perf_counter		hw;

	struct perf_counter_context	*ctx;
	struct file			*filp;

	/*
	 * These accumulate total time (in nanoseconds) that children
	 * counters have been enabled and running, respectively.
	 */
	atomic64_t			child_total_time_enabled;
	atomic64_t			child_total_time_running;

	/*
	 * Protect attach/detach and child_list:
	 */
	struct mutex			child_mutex;
	struct list_head		child_list;
	struct perf_counter		*parent;

	int				oncpu;
	int				cpu;

	struct list_head		owner_entry;
	struct task_struct		*owner;

	/* mmap bits */
	struct mutex			mmap_mutex;
	atomic_t			mmap_count;
	struct perf_mmap_data		*data;

	/* poll related */
	wait_queue_head_t		waitq;
	struct fasync_struct		*fasync;

	/* delayed work for NMIs and such */
	int				pending_wakeup;
	int				pending_kill;
	int				pending_disable;
	struct perf_pending_entry	pending;

	atomic_t			event_limit;

	void (*destroy)(struct perf_counter *);
	struct rcu_head			rcu_head;

	struct pid_namespace		*ns;
	u64				id;
#endif
};

/**
 * struct perf_counter_context - counter context structure
 *
 * Used as a container for task counters and CPU counters as well:
 */
struct perf_counter_context {
	/*
	 * Protect the states of the counters in the list,
	 * nr_active, and the list:
	 */
	spinlock_t			lock;
	/*
	 * Protect the list of counters.  Locking either mutex or lock
	 * is sufficient to ensure the list doesn't change; to change
	 * the list you need to lock both the mutex and the spinlock.
	 */
	struct mutex			mutex;

	struct list_head		counter_list;
	struct list_head		event_list;
	int				nr_counters;
	int				nr_active;
	int				is_active;
	atomic_t			refcount;
	struct task_struct		*task;

	/*
	 * Context clock, runs when context enabled.
	 */
	u64				time;
	u64				timestamp;

	/*
	 * These fields let us detect when two contexts have both
	 * been cloned (inherited) from a common ancestor.
	 */
	struct perf_counter_context	*parent_ctx;
	u64				parent_gen;
	u64				generation;
	int				pin_count;
	struct rcu_head			rcu_head;
};

/**
 * struct perf_counter_cpu_context - per cpu counter context structure
 */
struct perf_cpu_context {
	struct perf_counter_context	ctx;
	struct perf_counter_context	*task_ctx;
	int				active_oncpu;
	int				max_pertask;
	int				exclusive;

	/*
	 * Recursion avoidance:
	 *
	 * task, softirq, irq, nmi context
	 */
	int				recursion[4];
};

#ifdef CONFIG_PERF_COUNTERS

/*
 * Set by architecture code:
 */
extern int perf_max_counters;

extern const struct pmu *hw_perf_counter_init(struct perf_counter *counter);

extern void perf_counter_task_sched_in(struct task_struct *task, int cpu);
extern void perf_counter_task_sched_out(struct task_struct *task,
					struct task_struct *next, int cpu);
extern void perf_counter_task_tick(struct task_struct *task, int cpu);
extern int perf_counter_init_task(struct task_struct *child);
extern void perf_counter_exit_task(struct task_struct *child);
extern void perf_counter_free_task(struct task_struct *task);
extern void set_perf_counter_pending(void);
extern void perf_counter_do_pending(void);
extern void perf_counter_print_debug(void);
extern void __perf_disable(void);
extern bool __perf_enable(void);
extern void perf_disable(void);
extern void perf_enable(void);
extern int perf_counter_task_disable(void);
extern int perf_counter_task_enable(void);
extern int hw_perf_group_sched_in(struct perf_counter *group_leader,
	       struct perf_cpu_context *cpuctx,
	       struct perf_counter_context *ctx, int cpu);
extern void perf_counter_update_userpage(struct perf_counter *counter);

struct perf_sample_data {
	struct pt_regs			*regs;
	u64				addr;
	u64				period;
};

extern int perf_counter_overflow(struct perf_counter *counter, int nmi,
				 struct perf_sample_data *data);

/*
 * Return 1 for a software counter, 0 for a hardware counter
 */
static inline int is_software_counter(struct perf_counter *counter)
{
	return (counter->attr.type != PERF_TYPE_RAW) &&
		(counter->attr.type != PERF_TYPE_HARDWARE) &&
		(counter->attr.type != PERF_TYPE_HW_CACHE);
}

extern void perf_swcounter_event(u32, u64, int, struct pt_regs *, u64);

extern void __perf_counter_mmap(struct vm_area_struct *vma);

static inline void perf_counter_mmap(struct vm_area_struct *vma)
{
	if (vma->vm_flags & VM_EXEC)
		__perf_counter_mmap(vma);
}

extern void perf_counter_comm(struct task_struct *tsk);
extern void perf_counter_fork(struct task_struct *tsk);

extern struct perf_callchain_entry *perf_callchain(struct pt_regs *regs);

extern int sysctl_perf_counter_paranoid;
extern int sysctl_perf_counter_mlock;
extern int sysctl_perf_counter_sample_rate;

extern void perf_counter_init(void);

#ifndef perf_misc_flags
#define perf_misc_flags(regs)	(user_mode(regs) ? PERF_EVENT_MISC_USER : \
				 PERF_EVENT_MISC_KERNEL)
#define perf_instruction_pointer(regs)	instruction_pointer(regs)
#endif

#else
static inline void
perf_counter_task_sched_in(struct task_struct *task, int cpu)		{ }
static inline void
perf_counter_task_sched_out(struct task_struct *task,
			    struct task_struct *next, int cpu)		{ }
static inline void
perf_counter_task_tick(struct task_struct *task, int cpu)		{ }
static inline int perf_counter_init_task(struct task_struct *child)	{ return 0; }
static inline void perf_counter_exit_task(struct task_struct *child)	{ }
static inline void perf_counter_free_task(struct task_struct *task)	{ }
static inline void perf_counter_do_pending(void)			{ }
static inline void perf_counter_print_debug(void)			{ }
static inline void perf_disable(void)					{ }
static inline void perf_enable(void)					{ }
static inline int perf_counter_task_disable(void)	{ return -EINVAL; }
static inline int perf_counter_task_enable(void)	{ return -EINVAL; }

static inline void
perf_swcounter_event(u32 event, u64 nr, int nmi,
		     struct pt_regs *regs, u64 addr)			{ }

static inline void perf_counter_mmap(struct vm_area_struct *vma)	{ }
static inline void perf_counter_comm(struct task_struct *tsk)		{ }
static inline void perf_counter_fork(struct task_struct *tsk)		{ }
static inline void perf_counter_init(void)				{ }
#endif

#endif /* __KERNEL__ */
#endif /* _LINUX_PERF_COUNTER_H */
