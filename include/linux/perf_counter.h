/*
 *  Performance counters:
 *
 *   Copyright(C) 2008, Thomas Gleixner <tglx@linutronix.de>
 *   Copyright(C) 2008, Red Hat, Inc., Ingo Molnar
 *
 *  Data type definitions, declarations, prototypes.
 *
 *  Started by: Thomas Gleixner and Ingo Molnar
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
 * hw_event.type
 */
enum perf_event_types {
	PERF_TYPE_HARDWARE		= 0,
	PERF_TYPE_SOFTWARE		= 1,
	PERF_TYPE_TRACEPOINT		= 2,

	/*
	 * available TYPE space, raw is the max value.
	 */

	PERF_TYPE_RAW			= 128,
};

/*
 * Generalized performance counter event types, used by the hw_event.event_id
 * parameter of the sys_perf_counter_open() syscall:
 */
enum hw_event_ids {
	/*
	 * Common hardware events, generalized by the kernel:
	 */
	PERF_COUNT_CPU_CYCLES		= 0,
	PERF_COUNT_INSTRUCTIONS		= 1,
	PERF_COUNT_CACHE_REFERENCES	= 2,
	PERF_COUNT_CACHE_MISSES		= 3,
	PERF_COUNT_BRANCH_INSTRUCTIONS	= 4,
	PERF_COUNT_BRANCH_MISSES	= 5,
	PERF_COUNT_BUS_CYCLES		= 6,

	PERF_HW_EVENTS_MAX		= 7,
};

/*
 * Special "software" counters provided by the kernel, even if the hardware
 * does not support performance counters. These counters measure various
 * physical and sw events of the kernel (and allow the profiling of them as
 * well):
 */
enum sw_event_ids {
	PERF_COUNT_CPU_CLOCK		= 0,
	PERF_COUNT_TASK_CLOCK		= 1,
	PERF_COUNT_PAGE_FAULTS		= 2,
	PERF_COUNT_CONTEXT_SWITCHES	= 3,
	PERF_COUNT_CPU_MIGRATIONS	= 4,
	PERF_COUNT_PAGE_FAULTS_MIN	= 5,
	PERF_COUNT_PAGE_FAULTS_MAJ	= 6,

	PERF_SW_EVENTS_MAX		= 7,
};

#define __PERF_COUNTER_MASK(name) 			\
	(((1ULL << PERF_COUNTER_##name##_BITS) - 1) <<	\
	 PERF_COUNTER_##name##_SHIFT)

#define PERF_COUNTER_RAW_BITS		1
#define PERF_COUNTER_RAW_SHIFT		63
#define PERF_COUNTER_RAW_MASK		__PERF_COUNTER_MASK(RAW)

#define PERF_COUNTER_CONFIG_BITS	63
#define PERF_COUNTER_CONFIG_SHIFT	0
#define PERF_COUNTER_CONFIG_MASK	__PERF_COUNTER_MASK(CONFIG)

#define PERF_COUNTER_TYPE_BITS		7
#define PERF_COUNTER_TYPE_SHIFT		56
#define PERF_COUNTER_TYPE_MASK		__PERF_COUNTER_MASK(TYPE)

#define PERF_COUNTER_EVENT_BITS		56
#define PERF_COUNTER_EVENT_SHIFT	0
#define PERF_COUNTER_EVENT_MASK		__PERF_COUNTER_MASK(EVENT)

/*
 * Bits that can be set in hw_event.record_type to request information
 * in the overflow packets.
 */
enum perf_counter_record_format {
	PERF_RECORD_IP		= 1U << 0,
	PERF_RECORD_TID		= 1U << 1,
	PERF_RECORD_TIME	= 1U << 2,
	PERF_RECORD_ADDR	= 1U << 3,
	PERF_RECORD_GROUP	= 1U << 4,
	PERF_RECORD_CALLCHAIN	= 1U << 5,
};

/*
 * Bits that can be set in hw_event.read_format to request that
 * reads on the counter should return the indicated quantities,
 * in increasing order of bit value, after the counter value.
 */
enum perf_counter_read_format {
	PERF_FORMAT_TOTAL_TIME_ENABLED	=  1,
	PERF_FORMAT_TOTAL_TIME_RUNNING	=  2,
};

/*
 * Hardware event to monitor via a performance monitoring counter:
 */
struct perf_counter_hw_event {
	/*
	 * The MSB of the config word signifies if the rest contains cpu
	 * specific (raw) counter configuration data, if unset, the next
	 * 7 bits are an event type and the rest of the bits are the event
	 * identifier.
	 */
	__u64			config;

	__u64			irq_period;
	__u32			record_type;
	__u32			read_format;

	__u64			disabled       :  1, /* off by default        */
				nmi	       :  1, /* NMI sampling          */
				inherit	       :  1, /* children inherit it   */
				pinned	       :  1, /* must always be on PMU */
				exclusive      :  1, /* only group on PMU     */
				exclude_user   :  1, /* don't count user      */
				exclude_kernel :  1, /* ditto kernel          */
				exclude_hv     :  1, /* ditto hypervisor      */
				exclude_idle   :  1, /* don't count when idle */
				mmap           :  1, /* include mmap data     */
				munmap         :  1, /* include munmap data   */
				comm	       :  1, /* include comm data     */

				__reserved_1   : 52;

	__u32			extra_config_len;
	__u32			wakeup_events;	/* wakeup every n events */

	__u64			__reserved_2;
	__u64			__reserved_3;
};

/*
 * Ioctls that can be done on a perf counter fd:
 */
#define PERF_COUNTER_IOC_ENABLE		_IO ('$', 0)
#define PERF_COUNTER_IOC_DISABLE	_IO ('$', 1)
#define PERF_COUNTER_IOC_REFRESH	_IOW('$', 2, u32)

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
	 * User-space reading this value should issue an rmb(), on SMP capable
	 * platforms, after reading this value -- see perf_counter_wakeup().
	 */
	__u32   data_head;		/* head in the data section */
};

#define PERF_EVENT_MISC_KERNEL		(1 << 0)
#define PERF_EVENT_MISC_USER		(1 << 1)
#define PERF_EVENT_MISC_OVERFLOW	(1 << 2)

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
	 * 	struct perf_event_header	header;
	 *
	 * 	u32				pid, tid;
	 * 	u64				addr;
	 * 	u64				len;
	 * 	u64				pgoff;
	 * 	char				filename[];
	 * };
	 */
	PERF_EVENT_MMAP			= 1,
	PERF_EVENT_MUNMAP		= 2,

	/*
	 * struct {
	 * 	struct perf_event_header	header;
	 *
	 * 	u32				pid, tid;
	 * 	char				comm[];
	 * };
	 */
	PERF_EVENT_COMM			= 3,

	/*
	 * When header.misc & PERF_EVENT_MISC_OVERFLOW the event_type field
	 * will be PERF_RECORD_*
	 *
	 * struct {
	 * 	struct perf_event_header	header;
	 *
	 * 	{ u64			ip;	  } && PERF_RECORD_IP
	 * 	{ u32			pid, tid; } && PERF_RECORD_TID
	 * 	{ u64			time;     } && PERF_RECORD_TIME
	 * 	{ u64			addr;     } && PERF_RECORD_ADDR
	 *
	 * 	{ u64			nr;
	 * 	  { u64 event, val; } 	cnt[nr];  } && PERF_RECORD_GROUP
	 *
	 * 	{ u16			nr,
	 * 				hv,
	 * 				kernel,
	 * 				user;
	 * 	  u64			ips[nr];  } && PERF_RECORD_CALLCHAIN
	 * };
	 */
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
#include <asm/atomic.h>

struct task_struct;

static inline u64 perf_event_raw(struct perf_counter_hw_event *hw_event)
{
	return hw_event->config & PERF_COUNTER_RAW_MASK;
}

static inline u64 perf_event_config(struct perf_counter_hw_event *hw_event)
{
	return hw_event->config & PERF_COUNTER_CONFIG_MASK;
}

static inline u64 perf_event_type(struct perf_counter_hw_event *hw_event)
{
	return (hw_event->config & PERF_COUNTER_TYPE_MASK) >>
		PERF_COUNTER_TYPE_SHIFT;
}

static inline u64 perf_event_id(struct perf_counter_hw_event *hw_event)
{
	return hw_event->config & PERF_COUNTER_EVENT_MASK;
}

/**
 * struct hw_perf_counter - performance counter hardware details:
 */
struct hw_perf_counter {
#ifdef CONFIG_PERF_COUNTERS
	union {
		struct { /* hardware */
			u64				config;
			unsigned long			config_base;
			unsigned long			counter_base;
			int				nmi;
			unsigned int			idx;
		};
		union { /* software */
			atomic64_t			count;
			struct hrtimer			hrtimer;
		};
	};
	atomic64_t			prev_count;
	u64				irq_period;
	atomic64_t			period_left;
#endif
};

struct perf_counter;

/**
 * struct hw_perf_counter_ops - performance counter hw ops
 */
struct hw_perf_counter_ops {
	int (*enable)			(struct perf_counter *counter);
	void (*disable)			(struct perf_counter *counter);
	void (*read)			(struct perf_counter *counter);
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

	atomic_t			wakeup;		/* POLL_ for wakeups */
	atomic_t			head;		/* write position    */
	atomic_t			events;		/* event limit       */

	struct perf_counter_mmap_page   *user_page;
	void 				*data_pages[0];
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
	int 				nr_siblings;
	struct perf_counter		*group_leader;
	const struct hw_perf_counter_ops *hw_ops;

	enum perf_counter_active_state	state;
	enum perf_counter_active_state	prev_state;
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

	struct perf_counter_hw_event	hw_event;
	struct hw_perf_counter		hw;

	struct perf_counter_context	*ctx;
	struct task_struct		*task;
	struct file			*filp;

	struct perf_counter		*parent;
	struct list_head		child_list;

	/*
	 * These accumulate total time (in nanoseconds) that children
	 * counters have been enabled and running, respectively.
	 */
	atomic64_t			child_total_time_enabled;
	atomic64_t			child_total_time_running;

	/*
	 * Protect attach/detach and child_list:
	 */
	struct mutex			mutex;

	int				oncpu;
	int				cpu;

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
#endif
};

/**
 * struct perf_counter_context - counter context structure
 *
 * Used as a container for task counters and CPU counters as well:
 */
struct perf_counter_context {
#ifdef CONFIG_PERF_COUNTERS
	/*
	 * Protect the states of the counters in the list,
	 * nr_active, and the list:
	 */
	spinlock_t		lock;
	/*
	 * Protect the list of counters.  Locking either mutex or lock
	 * is sufficient to ensure the list doesn't change; to change
	 * the list you need to lock both the mutex and the spinlock.
	 */
	struct mutex		mutex;

	struct list_head	counter_list;
	struct list_head	event_list;
	int			nr_counters;
	int			nr_active;
	int			is_active;
	struct task_struct	*task;

	/*
	 * Context clock, runs when context enabled.
	 */
	u64			time;
	u64			timestamp;
#endif
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
	int			recursion[4];
};

/*
 * Set by architecture code:
 */
extern int perf_max_counters;

#ifdef CONFIG_PERF_COUNTERS
extern const struct hw_perf_counter_ops *
hw_perf_counter_init(struct perf_counter *counter);

extern void perf_counter_task_sched_in(struct task_struct *task, int cpu);
extern void perf_counter_task_sched_out(struct task_struct *task, int cpu);
extern void perf_counter_task_tick(struct task_struct *task, int cpu);
extern void perf_counter_init_task(struct task_struct *child);
extern void perf_counter_exit_task(struct task_struct *child);
extern void perf_counter_do_pending(void);
extern void perf_counter_print_debug(void);
extern void perf_counter_unthrottle(void);
extern u64 hw_perf_save_disable(void);
extern void hw_perf_restore(u64 ctrl);
extern int perf_counter_task_disable(void);
extern int perf_counter_task_enable(void);
extern int hw_perf_group_sched_in(struct perf_counter *group_leader,
	       struct perf_cpu_context *cpuctx,
	       struct perf_counter_context *ctx, int cpu);
extern void perf_counter_update_userpage(struct perf_counter *counter);

extern int perf_counter_overflow(struct perf_counter *counter,
				 int nmi, struct pt_regs *regs, u64 addr);
/*
 * Return 1 for a software counter, 0 for a hardware counter
 */
static inline int is_software_counter(struct perf_counter *counter)
{
	return !perf_event_raw(&counter->hw_event) &&
		perf_event_type(&counter->hw_event) != PERF_TYPE_HARDWARE;
}

extern void perf_swcounter_event(u32, u64, int, struct pt_regs *, u64);

extern void perf_counter_mmap(unsigned long addr, unsigned long len,
			      unsigned long pgoff, struct file *file);

extern void perf_counter_munmap(unsigned long addr, unsigned long len,
				unsigned long pgoff, struct file *file);

extern void perf_counter_comm(struct task_struct *tsk);

#define MAX_STACK_DEPTH		255

struct perf_callchain_entry {
	u16	nr, hv, kernel, user;
	u64	ip[MAX_STACK_DEPTH];
};

extern struct perf_callchain_entry *perf_callchain(struct pt_regs *regs);

#else
static inline void
perf_counter_task_sched_in(struct task_struct *task, int cpu)		{ }
static inline void
perf_counter_task_sched_out(struct task_struct *task, int cpu)		{ }
static inline void
perf_counter_task_tick(struct task_struct *task, int cpu)		{ }
static inline void perf_counter_init_task(struct task_struct *child)	{ }
static inline void perf_counter_exit_task(struct task_struct *child)	{ }
static inline void perf_counter_do_pending(void)			{ }
static inline void perf_counter_print_debug(void)			{ }
static inline void perf_counter_unthrottle(void)			{ }
static inline void hw_perf_restore(u64 ctrl)				{ }
static inline u64 hw_perf_save_disable(void)		      { return 0; }
static inline int perf_counter_task_disable(void)	{ return -EINVAL; }
static inline int perf_counter_task_enable(void)	{ return -EINVAL; }

static inline void
perf_swcounter_event(u32 event, u64 nr, int nmi,
		     struct pt_regs *regs, u64 addr)			{ }

static inline void
perf_counter_mmap(unsigned long addr, unsigned long len,
		  unsigned long pgoff, struct file *file)		{ }

static inline void
perf_counter_munmap(unsigned long addr, unsigned long len,
		    unsigned long pgoff, struct file *file) 		{ }

static inline void perf_counter_comm(struct task_struct *tsk)		{ }
#endif

#endif /* __KERNEL__ */
#endif /* _LINUX_PERF_COUNTER_H */
