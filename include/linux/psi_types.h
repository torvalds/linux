/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PSI_TYPES_H
#define _LINUX_PSI_TYPES_H

#include <linux/kthread.h>
#include <linux/seqlock.h>
#include <linux/types.h>
#include <linux/kref.h>
#include <linux/wait.h>

#ifdef CONFIG_PSI

/* Tracked task states */
enum psi_task_count {
	NR_IOWAIT,
	NR_MEMSTALL,
	NR_RUNNING,
	/*
	 * For IO and CPU stalls the presence of running/oncpu tasks
	 * in the domain means a partial rather than a full stall.
	 * For memory it's not so simple because of page reclaimers:
	 * they are running/oncpu while representing a stall. To tell
	 * whether a domain has productivity left or not, we need to
	 * distinguish between regular running (i.e. productive)
	 * threads and memstall ones.
	 */
	NR_MEMSTALL_RUNNING,
	NR_PSI_TASK_COUNTS = 4,
};

/* Task state bitmasks */
#define TSK_IOWAIT	(1 << NR_IOWAIT)
#define TSK_MEMSTALL	(1 << NR_MEMSTALL)
#define TSK_RUNNING	(1 << NR_RUNNING)
#define TSK_MEMSTALL_RUNNING	(1 << NR_MEMSTALL_RUNNING)

/* Only one task can be scheduled, no corresponding task count */
#define TSK_ONCPU	(1 << NR_PSI_TASK_COUNTS)

/* Resources that workloads could be stalled on */
enum psi_res {
	PSI_IO,
	PSI_MEM,
	PSI_CPU,
#ifdef CONFIG_IRQ_TIME_ACCOUNTING
	PSI_IRQ,
#endif
	NR_PSI_RESOURCES,
};

/*
 * Pressure states for each resource:
 *
 * SOME: Stalled tasks & working tasks
 * FULL: Stalled tasks & no working tasks
 */
enum psi_states {
	PSI_IO_SOME,
	PSI_IO_FULL,
	PSI_MEM_SOME,
	PSI_MEM_FULL,
	PSI_CPU_SOME,
	PSI_CPU_FULL,
#ifdef CONFIG_IRQ_TIME_ACCOUNTING
	PSI_IRQ_FULL,
#endif
	/* Only per-CPU, to weigh the CPU in the global average: */
	PSI_NONIDLE,
	NR_PSI_STATES,
};

/* Use one bit in the state mask to track TSK_ONCPU */
#define PSI_ONCPU	(1 << NR_PSI_STATES)

/* Flag whether to re-arm avgs_work, see details in get_recent_times() */
#define PSI_STATE_RESCHEDULE	(1 << (NR_PSI_STATES + 1))

enum psi_aggregators {
	PSI_AVGS = 0,
	PSI_POLL,
	NR_PSI_AGGREGATORS,
};

struct psi_group_cpu {
	/* 1st cacheline updated by the scheduler */

	/* Aggregator needs to know of concurrent changes */
	seqcount_t seq ____cacheline_aligned_in_smp;

	/* States of the tasks belonging to this group */
	unsigned int tasks[NR_PSI_TASK_COUNTS];

	/* Aggregate pressure state derived from the tasks */
	u32 state_mask;

	/* Period time sampling buckets for each state of interest (ns) */
	u32 times[NR_PSI_STATES];

	/* Time of last task change in this group (rq_clock) */
	u64 state_start;

	/* 2nd cacheline updated by the aggregator */

	/* Delta detection against the sampling buckets */
	u32 times_prev[NR_PSI_AGGREGATORS][NR_PSI_STATES]
			____cacheline_aligned_in_smp;
};

/* PSI growth tracking window */
struct psi_window {
	/* Window size in ns */
	u64 size;

	/* Start time of the current window in ns */
	u64 start_time;

	/* Value at the start of the window */
	u64 start_value;

	/* Value growth in the previous window */
	u64 prev_growth;
};

struct psi_trigger {
	/* PSI state being monitored by the trigger */
	enum psi_states state;

	/* User-spacified threshold in ns */
	u64 threshold;

	/* List node inside triggers list */
	struct list_head node;

	/* Backpointer needed during trigger destruction */
	struct psi_group *group;

	/* Wait queue for polling */
	wait_queue_head_t event_wait;

	/* Kernfs file for cgroup triggers */
	struct kernfs_open_file *of;

	/* Pending event flag */
	int event;

	/* Tracking window */
	struct psi_window win;

	/*
	 * Time last event was generated. Used for rate-limiting
	 * events to one per window
	 */
	u64 last_event_time;

	/* Deferred event(s) from previous ratelimit window */
	bool pending_event;

	/* Trigger type - PSI_AVGS for unprivileged, PSI_POLL for RT */
	enum psi_aggregators aggregator;
};

struct psi_group {
	struct psi_group *parent;
	bool enabled;

	/* Protects data used by the aggregator */
	struct mutex avgs_lock;

	/* Per-cpu task state & time tracking */
	struct psi_group_cpu __percpu *pcpu;

	/* Running pressure averages */
	u64 avg_total[NR_PSI_STATES - 1];
	u64 avg_last_update;
	u64 avg_next_update;

	/* Aggregator work control */
	struct delayed_work avgs_work;

	/* Unprivileged triggers against N*PSI_FREQ windows */
	struct list_head avg_triggers;
	u32 avg_nr_triggers[NR_PSI_STATES - 1];

	/* Total stall times and sampled pressure averages */
	u64 total[NR_PSI_AGGREGATORS][NR_PSI_STATES - 1];
	unsigned long avg[NR_PSI_STATES - 1][3];

	/* Monitor RT polling work control */
	struct task_struct __rcu *rtpoll_task;
	struct timer_list rtpoll_timer;
	wait_queue_head_t rtpoll_wait;
	atomic_t rtpoll_wakeup;
	atomic_t rtpoll_scheduled;

	/* Protects data used by the monitor */
	struct mutex rtpoll_trigger_lock;

	/* Configured RT polling triggers */
	struct list_head rtpoll_triggers;
	u32 rtpoll_nr_triggers[NR_PSI_STATES - 1];
	u32 rtpoll_states;
	u64 rtpoll_min_period;

	/* Total stall times at the start of RT polling monitor activation */
	u64 rtpoll_total[NR_PSI_STATES - 1];
	u64 rtpoll_next_update;
	u64 rtpoll_until;
};

#else /* CONFIG_PSI */

#define NR_PSI_RESOURCES	0

struct psi_group { };

#endif /* CONFIG_PSI */

#endif /* _LINUX_PSI_TYPES_H */
