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

#include <asm/atomic.h>

#ifdef CONFIG_PERF_COUNTERS
# include <asm/perf_counter.h>
#endif

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/rculist.h>
#include <linux/rcupdate.h>
#include <linux/spinlock.h>

struct task_struct;

/*
 * User-space ABI bits:
 */

/*
 * Generalized performance counter event types, used by the hw_event.type
 * parameter of the sys_perf_counter_open() syscall:
 */
enum hw_event_types {
	/*
	 * Common hardware events, generalized by the kernel:
	 */
	PERF_COUNT_CPU_CYCLES		=  0,
	PERF_COUNT_INSTRUCTIONS		=  1,
	PERF_COUNT_CACHE_REFERENCES	=  2,
	PERF_COUNT_CACHE_MISSES		=  3,
	PERF_COUNT_BRANCH_INSTRUCTIONS	=  4,
	PERF_COUNT_BRANCH_MISSES	=  5,
	PERF_COUNT_BUS_CYCLES		=  6,

	PERF_HW_EVENTS_MAX		=  7,

	/*
	 * Special "software" counters provided by the kernel, even if
	 * the hardware does not support performance counters. These
	 * counters measure various physical and sw events of the
	 * kernel (and allow the profiling of them as well):
	 */
	PERF_COUNT_CPU_CLOCK		= -1,
	PERF_COUNT_TASK_CLOCK		= -2,
	PERF_COUNT_PAGE_FAULTS		= -3,
	PERF_COUNT_CONTEXT_SWITCHES	= -4,
	PERF_COUNT_CPU_MIGRATIONS	= -5,

	PERF_SW_EVENTS_MIN		= -6,
};

/*
 * IRQ-notification data record type:
 */
enum perf_counter_record_type {
	PERF_RECORD_SIMPLE		=  0,
	PERF_RECORD_IRQ			=  1,
	PERF_RECORD_GROUP		=  2,
};

/*
 * Hardware event to monitor via a performance monitoring counter:
 */
struct perf_counter_hw_event {
	s64			type;

	u64			irq_period;
	u32			record_type;

	u32			disabled     :  1, /* off by default      */
				nmi	     :  1, /* NMI sampling        */
				raw	     :  1, /* raw event type      */
				inherit	     :  1, /* children inherit it */
				__reserved_1 : 28;

	u64			__reserved_2;
};

/*
 * Kernel-internal data types:
 */

/**
 * struct hw_perf_counter - performance counter hardware details:
 */
struct hw_perf_counter {
#ifdef CONFIG_PERF_COUNTERS
	u64				config;
	unsigned long			config_base;
	unsigned long			counter_base;
	int				nmi;
	unsigned int			idx;
	atomic64_t			prev_count;
	u64				irq_period;
	atomic64_t			period_left;
#endif
};

/*
 * Hardcoded buffer length limit for now, for IRQ-fed events:
 */
#define PERF_DATA_BUFLEN		2048

/**
 * struct perf_data - performance counter IRQ data sampling ...
 */
struct perf_data {
	int				len;
	int				rd_idx;
	int				overrun;
	u8				data[PERF_DATA_BUFLEN];
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
	PERF_COUNTER_STATE_OFF		= -1,
	PERF_COUNTER_STATE_INACTIVE	=  0,
	PERF_COUNTER_STATE_ACTIVE	=  1,
};

struct file;

/**
 * struct perf_counter - performance counter kernel representation:
 */
struct perf_counter {
#ifdef CONFIG_PERF_COUNTERS
	struct list_head		list_entry;
	struct list_head		sibling_list;
	struct perf_counter		*group_leader;
	const struct hw_perf_counter_ops *hw_ops;

	enum perf_counter_active_state	state;
	atomic64_t			count;

	struct perf_counter_hw_event	hw_event;
	struct hw_perf_counter		hw;

	struct perf_counter_context	*ctx;
	struct task_struct		*task;
	struct file			*filp;

	struct perf_counter		*parent;
	/*
	 * Protect attach/detach:
	 */
	struct mutex			mutex;

	int				oncpu;
	int				cpu;

	/* read() / irq related data */
	wait_queue_head_t		waitq;
	/* optional: for NMIs */
	int				wakeup_pending;
	struct perf_data		*irqdata;
	struct perf_data		*usrdata;
	struct perf_data		data[2];
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
	 * Protect the list of counters:
	 */
	spinlock_t		lock;

	struct list_head	counter_list;
	int			nr_counters;
	int			nr_active;
	struct task_struct	*task;
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
extern void perf_counter_notify(struct pt_regs *regs);
extern void perf_counter_print_debug(void);
extern u64 hw_perf_save_disable(void);
extern void hw_perf_restore(u64 ctrl);
extern int perf_counter_task_disable(void);
extern int perf_counter_task_enable(void);

#else
static inline void
perf_counter_task_sched_in(struct task_struct *task, int cpu)		{ }
static inline void
perf_counter_task_sched_out(struct task_struct *task, int cpu)		{ }
static inline void
perf_counter_task_tick(struct task_struct *task, int cpu)		{ }
static inline void perf_counter_init_task(struct task_struct *child)	{ }
static inline void perf_counter_exit_task(struct task_struct *child)	{ }
static inline void perf_counter_notify(struct pt_regs *regs)		{ }
static inline void perf_counter_print_debug(void)			{ }
static inline void hw_perf_restore(u64 ctrl)			{ }
static inline u64 hw_perf_save_disable(void)		      { return 0; }
static inline int perf_counter_task_disable(void)	{ return -EINVAL; }
static inline int perf_counter_task_enable(void)	{ return -EINVAL; }
#endif

#endif /* _LINUX_PERF_COUNTER_H */
