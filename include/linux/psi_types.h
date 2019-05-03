#ifndef _LINUX_PSI_TYPES_H
#define _LINUX_PSI_TYPES_H

#include <linux/seqlock.h>
#include <linux/types.h>

#ifdef CONFIG_PSI

/* Tracked task states */
enum psi_task_count {
	NR_IOWAIT,
	NR_MEMSTALL,
	NR_RUNNING,
	NR_PSI_TASK_COUNTS,
};

/* Task state bitmasks */
#define TSK_IOWAIT	(1 << NR_IOWAIT)
#define TSK_MEMSTALL	(1 << NR_MEMSTALL)
#define TSK_RUNNING	(1 << NR_RUNNING)

/* Resources that workloads could be stalled on */
enum psi_res {
	PSI_IO,
	PSI_MEM,
	PSI_CPU,
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
	/* Only per-CPU, to weigh the CPU in the global average: */
	PSI_NONIDLE,
	NR_PSI_STATES,
};

struct psi_group_cpu {
	/* 1st cacheline updated by the scheduler */

	/* Aggregator needs to know of concurrent changes */
	seqcount_t seq ____cacheline_aligned_in_smp;

	/* States of the tasks belonging to this group */
	unsigned int tasks[NR_PSI_TASK_COUNTS];

	/* Period time sampling buckets for each state of interest (ns) */
	u32 times[NR_PSI_STATES];

	/* Time of last task change in this group (rq_clock) */
	u64 state_start;

	/* 2nd cacheline updated by the aggregator */

	/* Delta detection against the sampling buckets */
	u32 times_prev[NR_PSI_STATES] ____cacheline_aligned_in_smp;
};

struct psi_group {
	/* Protects data updated during an aggregation */
	struct mutex stat_lock;

	/* Per-cpu task state & time tracking */
	struct psi_group_cpu __percpu *pcpu;

	/* Periodic aggregation state */
	u64 total_prev[NR_PSI_STATES - 1];
	u64 last_update;
	u64 next_update;
	struct delayed_work clock_work;

	/* Total stall times and sampled pressure averages */
	u64 total[NR_PSI_STATES - 1];
	unsigned long avg[NR_PSI_STATES - 1][3];
};

#else /* CONFIG_PSI */

struct psi_group { };

#endif /* CONFIG_PSI */

#endif /* _LINUX_PSI_TYPES_H */
