/*
 * Read-Copy Update mechanism for mutual exclusion (tree-based version)
 * Internal non-public definitions.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * Copyright IBM Corporation, 2008
 *
 * Author: Ingo Molnar <mingo@elte.hu>
 *	   Paul E. McKenney <paulmck@linux.vnet.ibm.com>
 */

#include <linux/cache.h>
#include <linux/spinlock.h>
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <linux/seqlock.h>

/*
 * Define shape of hierarchy based on NR_CPUS, CONFIG_RCU_FANOUT, and
 * CONFIG_RCU_FANOUT_LEAF.
 * In theory, it should be possible to add more levels straightforwardly.
 * In practice, this did work well going from three levels to four.
 * Of course, your mileage may vary.
 */
#define MAX_RCU_LVLS 4
#define RCU_FANOUT_1	      (CONFIG_RCU_FANOUT_LEAF)
#define RCU_FANOUT_2	      (RCU_FANOUT_1 * CONFIG_RCU_FANOUT)
#define RCU_FANOUT_3	      (RCU_FANOUT_2 * CONFIG_RCU_FANOUT)
#define RCU_FANOUT_4	      (RCU_FANOUT_3 * CONFIG_RCU_FANOUT)

#if NR_CPUS <= RCU_FANOUT_1
#  define RCU_NUM_LVLS	      1
#  define NUM_RCU_LVL_0	      1
#  define NUM_RCU_LVL_1	      (NR_CPUS)
#  define NUM_RCU_LVL_2	      0
#  define NUM_RCU_LVL_3	      0
#  define NUM_RCU_LVL_4	      0
#elif NR_CPUS <= RCU_FANOUT_2
#  define RCU_NUM_LVLS	      2
#  define NUM_RCU_LVL_0	      1
#  define NUM_RCU_LVL_1	      DIV_ROUND_UP(NR_CPUS, RCU_FANOUT_1)
#  define NUM_RCU_LVL_2	      (NR_CPUS)
#  define NUM_RCU_LVL_3	      0
#  define NUM_RCU_LVL_4	      0
#elif NR_CPUS <= RCU_FANOUT_3
#  define RCU_NUM_LVLS	      3
#  define NUM_RCU_LVL_0	      1
#  define NUM_RCU_LVL_1	      DIV_ROUND_UP(NR_CPUS, RCU_FANOUT_2)
#  define NUM_RCU_LVL_2	      DIV_ROUND_UP(NR_CPUS, RCU_FANOUT_1)
#  define NUM_RCU_LVL_3	      (NR_CPUS)
#  define NUM_RCU_LVL_4	      0
#elif NR_CPUS <= RCU_FANOUT_4
#  define RCU_NUM_LVLS	      4
#  define NUM_RCU_LVL_0	      1
#  define NUM_RCU_LVL_1	      DIV_ROUND_UP(NR_CPUS, RCU_FANOUT_3)
#  define NUM_RCU_LVL_2	      DIV_ROUND_UP(NR_CPUS, RCU_FANOUT_2)
#  define NUM_RCU_LVL_3	      DIV_ROUND_UP(NR_CPUS, RCU_FANOUT_1)
#  define NUM_RCU_LVL_4	      (NR_CPUS)
#else
# error "CONFIG_RCU_FANOUT insufficient for NR_CPUS"
#endif /* #if (NR_CPUS) <= RCU_FANOUT_1 */

#define RCU_SUM (NUM_RCU_LVL_0 + NUM_RCU_LVL_1 + NUM_RCU_LVL_2 + NUM_RCU_LVL_3 + NUM_RCU_LVL_4)
#define NUM_RCU_NODES (RCU_SUM - NR_CPUS)

extern int rcu_num_lvls;
extern int rcu_num_nodes;

/*
 * Dynticks per-CPU state.
 */
struct rcu_dynticks {
	long long dynticks_nesting; /* Track irq/process nesting level. */
				    /* Process level is worth LLONG_MAX/2. */
	int dynticks_nmi_nesting;   /* Track NMI nesting level. */
	atomic_t dynticks;	    /* Even value for idle, else odd. */
#ifdef CONFIG_NO_HZ_FULL_SYSIDLE
	long long dynticks_idle_nesting;
				    /* irq/process nesting level from idle. */
	atomic_t dynticks_idle;	    /* Even value for idle, else odd. */
				    /*  "Idle" excludes userspace execution. */
	unsigned long dynticks_idle_jiffies;
				    /* End of last non-NMI non-idle period. */
#endif /* #ifdef CONFIG_NO_HZ_FULL_SYSIDLE */
#ifdef CONFIG_RCU_FAST_NO_HZ
	bool all_lazy;		    /* Are all CPU's CBs lazy? */
	unsigned long nonlazy_posted;
				    /* # times non-lazy CBs posted to CPU. */
	unsigned long nonlazy_posted_snap;
				    /* idle-period nonlazy_posted snapshot. */
	unsigned long last_accelerate;
				    /* Last jiffy CBs were accelerated. */
	unsigned long last_advance_all;
				    /* Last jiffy CBs were all advanced. */
	int tick_nohz_enabled_snap; /* Previously seen value from sysfs. */
#endif /* #ifdef CONFIG_RCU_FAST_NO_HZ */
};

/* RCU's kthread states for tracing. */
#define RCU_KTHREAD_STOPPED  0
#define RCU_KTHREAD_RUNNING  1
#define RCU_KTHREAD_WAITING  2
#define RCU_KTHREAD_OFFCPU   3
#define RCU_KTHREAD_YIELDING 4
#define RCU_KTHREAD_MAX      4

/*
 * Definition for node within the RCU grace-period-detection hierarchy.
 */
struct rcu_node {
	raw_spinlock_t lock;	/* Root rcu_node's lock protects some */
				/*  rcu_state fields as well as following. */
	unsigned long gpnum;	/* Current grace period for this node. */
				/*  This will either be equal to or one */
				/*  behind the root rcu_node's gpnum. */
	unsigned long completed; /* Last GP completed for this node. */
				/*  This will either be equal to or one */
				/*  behind the root rcu_node's gpnum. */
	unsigned long qsmask;	/* CPUs or groups that need to switch in */
				/*  order for current grace period to proceed.*/
				/*  In leaf rcu_node, each bit corresponds to */
				/*  an rcu_data structure, otherwise, each */
				/*  bit corresponds to a child rcu_node */
				/*  structure. */
	unsigned long expmask;	/* Groups that have ->blkd_tasks */
				/*  elements that need to drain to allow the */
				/*  current expedited grace period to */
				/*  complete (only for PREEMPT_RCU). */
	unsigned long qsmaskinit;
				/* Per-GP initial value for qsmask & expmask. */
				/*  Initialized from ->qsmaskinitnext at the */
				/*  beginning of each grace period. */
	unsigned long qsmaskinitnext;
				/* Online CPUs for next grace period. */
	unsigned long grpmask;	/* Mask to apply to parent qsmask. */
				/*  Only one bit will be set in this mask. */
	int	grplo;		/* lowest-numbered CPU or group here. */
	int	grphi;		/* highest-numbered CPU or group here. */
	u8	grpnum;		/* CPU/group number for next level up. */
	u8	level;		/* root is at level 0. */
	bool	wait_blkd_tasks;/* Necessary to wait for blocked tasks to */
				/*  exit RCU read-side critical sections */
				/*  before propagating offline up the */
				/*  rcu_node tree? */
	struct rcu_node *parent;
	struct list_head blkd_tasks;
				/* Tasks blocked in RCU read-side critical */
				/*  section.  Tasks are placed at the head */
				/*  of this list and age towards the tail. */
	struct list_head *gp_tasks;
				/* Pointer to the first task blocking the */
				/*  current grace period, or NULL if there */
				/*  is no such task. */
	struct list_head *exp_tasks;
				/* Pointer to the first task blocking the */
				/*  current expedited grace period, or NULL */
				/*  if there is no such task.  If there */
				/*  is no current expedited grace period, */
				/*  then there can cannot be any such task. */
#ifdef CONFIG_RCU_BOOST
	struct list_head *boost_tasks;
				/* Pointer to first task that needs to be */
				/*  priority boosted, or NULL if no priority */
				/*  boosting is needed for this rcu_node */
				/*  structure.  If there are no tasks */
				/*  queued on this rcu_node structure that */
				/*  are blocking the current grace period, */
				/*  there can be no such task. */
	struct rt_mutex boost_mtx;
				/* Used only for the priority-boosting */
				/*  side effect, not as a lock. */
	unsigned long boost_time;
				/* When to start boosting (jiffies). */
	struct task_struct *boost_kthread_task;
				/* kthread that takes care of priority */
				/*  boosting for this rcu_node structure. */
	unsigned int boost_kthread_status;
				/* State of boost_kthread_task for tracing. */
	unsigned long n_tasks_boosted;
				/* Total number of tasks boosted. */
	unsigned long n_exp_boosts;
				/* Number of tasks boosted for expedited GP. */
	unsigned long n_normal_boosts;
				/* Number of tasks boosted for normal GP. */
	unsigned long n_balk_blkd_tasks;
				/* Refused to boost: no blocked tasks. */
	unsigned long n_balk_exp_gp_tasks;
				/* Refused to boost: nothing blocking GP. */
	unsigned long n_balk_boost_tasks;
				/* Refused to boost: already boosting. */
	unsigned long n_balk_notblocked;
				/* Refused to boost: RCU RS CS still running. */
	unsigned long n_balk_notyet;
				/* Refused to boost: not yet time. */
	unsigned long n_balk_nos;
				/* Refused to boost: not sure why, though. */
				/*  This can happen due to race conditions. */
#endif /* #ifdef CONFIG_RCU_BOOST */
#ifdef CONFIG_RCU_NOCB_CPU
	wait_queue_head_t nocb_gp_wq[2];
				/* Place for rcu_nocb_kthread() to wait GP. */
#endif /* #ifdef CONFIG_RCU_NOCB_CPU */
	int need_future_gp[2];
				/* Counts of upcoming no-CB GP requests. */
	raw_spinlock_t fqslock ____cacheline_internodealigned_in_smp;
} ____cacheline_internodealigned_in_smp;

/*
 * Do a full breadth-first scan of the rcu_node structures for the
 * specified rcu_state structure.
 */
#define rcu_for_each_node_breadth_first(rsp, rnp) \
	for ((rnp) = &(rsp)->node[0]; \
	     (rnp) < &(rsp)->node[rcu_num_nodes]; (rnp)++)

/*
 * Do a breadth-first scan of the non-leaf rcu_node structures for the
 * specified rcu_state structure.  Note that if there is a singleton
 * rcu_node tree with but one rcu_node structure, this loop is a no-op.
 */
#define rcu_for_each_nonleaf_node_breadth_first(rsp, rnp) \
	for ((rnp) = &(rsp)->node[0]; \
	     (rnp) < (rsp)->level[rcu_num_lvls - 1]; (rnp)++)

/*
 * Scan the leaves of the rcu_node hierarchy for the specified rcu_state
 * structure.  Note that if there is a singleton rcu_node tree with but
 * one rcu_node structure, this loop -will- visit the rcu_node structure.
 * It is still a leaf node, even if it is also the root node.
 */
#define rcu_for_each_leaf_node(rsp, rnp) \
	for ((rnp) = (rsp)->level[rcu_num_lvls - 1]; \
	     (rnp) < &(rsp)->node[rcu_num_nodes]; (rnp)++)

/* Index values for nxttail array in struct rcu_data. */
#define RCU_DONE_TAIL		0	/* Also RCU_WAIT head. */
#define RCU_WAIT_TAIL		1	/* Also RCU_NEXT_READY head. */
#define RCU_NEXT_READY_TAIL	2	/* Also RCU_NEXT head. */
#define RCU_NEXT_TAIL		3
#define RCU_NEXT_SIZE		4

/* Per-CPU data for read-copy update. */
struct rcu_data {
	/* 1) quiescent-state and grace-period handling : */
	unsigned long	completed;	/* Track rsp->completed gp number */
					/*  in order to detect GP end. */
	unsigned long	gpnum;		/* Highest gp number that this CPU */
					/*  is aware of having started. */
	unsigned long	rcu_qs_ctr_snap;/* Snapshot of rcu_qs_ctr to check */
					/*  for rcu_all_qs() invocations. */
	bool		passed_quiesce;	/* User-mode/idle loop etc. */
	bool		qs_pending;	/* Core waits for quiesc state. */
	bool		beenonline;	/* CPU online at least once. */
	bool		gpwrap;		/* Possible gpnum/completed wrap. */
	struct rcu_node *mynode;	/* This CPU's leaf of hierarchy */
	unsigned long grpmask;		/* Mask to apply to leaf qsmask. */
#ifdef CONFIG_RCU_CPU_STALL_INFO
	unsigned long	ticks_this_gp;	/* The number of scheduling-clock */
					/*  ticks this CPU has handled */
					/*  during and after the last grace */
					/* period it is aware of. */
#endif /* #ifdef CONFIG_RCU_CPU_STALL_INFO */

	/* 2) batch handling */
	/*
	 * If nxtlist is not NULL, it is partitioned as follows.
	 * Any of the partitions might be empty, in which case the
	 * pointer to that partition will be equal to the pointer for
	 * the following partition.  When the list is empty, all of
	 * the nxttail elements point to the ->nxtlist pointer itself,
	 * which in that case is NULL.
	 *
	 * [nxtlist, *nxttail[RCU_DONE_TAIL]):
	 *	Entries that batch # <= ->completed
	 *	The grace period for these entries has completed, and
	 *	the other grace-period-completed entries may be moved
	 *	here temporarily in rcu_process_callbacks().
	 * [*nxttail[RCU_DONE_TAIL], *nxttail[RCU_WAIT_TAIL]):
	 *	Entries that batch # <= ->completed - 1: waiting for current GP
	 * [*nxttail[RCU_WAIT_TAIL], *nxttail[RCU_NEXT_READY_TAIL]):
	 *	Entries known to have arrived before current GP ended
	 * [*nxttail[RCU_NEXT_READY_TAIL], *nxttail[RCU_NEXT_TAIL]):
	 *	Entries that might have arrived after current GP ended
	 *	Note that the value of *nxttail[RCU_NEXT_TAIL] will
	 *	always be NULL, as this is the end of the list.
	 */
	struct rcu_head *nxtlist;
	struct rcu_head **nxttail[RCU_NEXT_SIZE];
	unsigned long	nxtcompleted[RCU_NEXT_SIZE];
					/* grace periods for sublists. */
	long		qlen_lazy;	/* # of lazy queued callbacks */
	long		qlen;		/* # of queued callbacks, incl lazy */
	long		qlen_last_fqs_check;
					/* qlen at last check for QS forcing */
	unsigned long	n_cbs_invoked;	/* count of RCU cbs invoked. */
	unsigned long	n_nocbs_invoked; /* count of no-CBs RCU cbs invoked. */
	unsigned long   n_cbs_orphaned; /* RCU cbs orphaned by dying CPU */
	unsigned long   n_cbs_adopted;  /* RCU cbs adopted from dying CPU */
	unsigned long	n_force_qs_snap;
					/* did other CPU force QS recently? */
	long		blimit;		/* Upper limit on a processed batch */

	/* 3) dynticks interface. */
	struct rcu_dynticks *dynticks;	/* Shared per-CPU dynticks state. */
	int dynticks_snap;		/* Per-GP tracking for dynticks. */

	/* 4) reasons this CPU needed to be kicked by force_quiescent_state */
	unsigned long dynticks_fqs;	/* Kicked due to dynticks idle. */
	unsigned long offline_fqs;	/* Kicked due to being offline. */
	unsigned long cond_resched_completed;
					/* Grace period that needs help */
					/*  from cond_resched(). */

	/* 5) __rcu_pending() statistics. */
	unsigned long n_rcu_pending;	/* rcu_pending() calls since boot. */
	unsigned long n_rp_qs_pending;
	unsigned long n_rp_report_qs;
	unsigned long n_rp_cb_ready;
	unsigned long n_rp_cpu_needs_gp;
	unsigned long n_rp_gp_completed;
	unsigned long n_rp_gp_started;
	unsigned long n_rp_nocb_defer_wakeup;
	unsigned long n_rp_need_nothing;

	/* 6) _rcu_barrier() and OOM callbacks. */
	struct rcu_head barrier_head;
#ifdef CONFIG_RCU_FAST_NO_HZ
	struct rcu_head oom_head;
#endif /* #ifdef CONFIG_RCU_FAST_NO_HZ */

	/* 7) Callback offloading. */
#ifdef CONFIG_RCU_NOCB_CPU
	struct rcu_head *nocb_head;	/* CBs waiting for kthread. */
	struct rcu_head **nocb_tail;
	atomic_long_t nocb_q_count;	/* # CBs waiting for nocb */
	atomic_long_t nocb_q_count_lazy; /*  invocation (all stages). */
	struct rcu_head *nocb_follower_head; /* CBs ready to invoke. */
	struct rcu_head **nocb_follower_tail;
	wait_queue_head_t nocb_wq;	/* For nocb kthreads to sleep on. */
	struct task_struct *nocb_kthread;
	int nocb_defer_wakeup;		/* Defer wakeup of nocb_kthread. */

	/* The following fields are used by the leader, hence own cacheline. */
	struct rcu_head *nocb_gp_head ____cacheline_internodealigned_in_smp;
					/* CBs waiting for GP. */
	struct rcu_head **nocb_gp_tail;
	bool nocb_leader_sleep;		/* Is the nocb leader thread asleep? */
	struct rcu_data *nocb_next_follower;
					/* Next follower in wakeup chain. */

	/* The following fields are used by the follower, hence new cachline. */
	struct rcu_data *nocb_leader ____cacheline_internodealigned_in_smp;
					/* Leader CPU takes GP-end wakeups. */
#endif /* #ifdef CONFIG_RCU_NOCB_CPU */

	/* 8) RCU CPU stall data. */
#ifdef CONFIG_RCU_CPU_STALL_INFO
	unsigned int softirq_snap;	/* Snapshot of softirq activity. */
#endif /* #ifdef CONFIG_RCU_CPU_STALL_INFO */

	int cpu;
	struct rcu_state *rsp;
};

/* Values for fqs_state field in struct rcu_state. */
#define RCU_GP_IDLE		0	/* No grace period in progress. */
#define RCU_GP_INIT		1	/* Grace period being initialized. */
#define RCU_SAVE_DYNTICK	2	/* Need to scan dyntick state. */
#define RCU_FORCE_QS		3	/* Need to force quiescent state. */
#define RCU_SIGNAL_INIT		RCU_SAVE_DYNTICK

/* Values for nocb_defer_wakeup field in struct rcu_data. */
#define RCU_NOGP_WAKE_NOT	0
#define RCU_NOGP_WAKE		1
#define RCU_NOGP_WAKE_FORCE	2

#define RCU_JIFFIES_TILL_FORCE_QS (1 + (HZ > 250) + (HZ > 500))
					/* For jiffies_till_first_fqs and */
					/*  and jiffies_till_next_fqs. */

#define RCU_JIFFIES_FQS_DIV	256	/* Very large systems need more */
					/*  delay between bouts of */
					/*  quiescent-state forcing. */

#define RCU_STALL_RAT_DELAY	2	/* Allow other CPUs time to take */
					/*  at least one scheduling clock */
					/*  irq before ratting on them. */

#define rcu_wait(cond)							\
do {									\
	for (;;) {							\
		set_current_state(TASK_INTERRUPTIBLE);			\
		if (cond)						\
			break;						\
		schedule();						\
	}								\
	__set_current_state(TASK_RUNNING);				\
} while (0)

/*
 * RCU global state, including node hierarchy.  This hierarchy is
 * represented in "heap" form in a dense array.  The root (first level)
 * of the hierarchy is in ->node[0] (referenced by ->level[0]), the second
 * level in ->node[1] through ->node[m] (->node[1] referenced by ->level[1]),
 * and the third level in ->node[m+1] and following (->node[m+1] referenced
 * by ->level[2]).  The number of levels is determined by the number of
 * CPUs and by CONFIG_RCU_FANOUT.  Small systems will have a "hierarchy"
 * consisting of a single rcu_node.
 */
struct rcu_state {
	struct rcu_node node[NUM_RCU_NODES];	/* Hierarchy. */
	struct rcu_node *level[RCU_NUM_LVLS];	/* Hierarchy levels. */
	u32 levelcnt[MAX_RCU_LVLS + 1];		/* # nodes in each level. */
	u8 levelspread[RCU_NUM_LVLS];		/* kids/node in each level. */
	u8 flavor_mask;				/* bit in flavor mask. */
	struct rcu_data __percpu *rda;		/* pointer of percu rcu_data. */
	void (*call)(struct rcu_head *head,	/* call_rcu() flavor. */
		     void (*func)(struct rcu_head *head));

	/* The following fields are guarded by the root rcu_node's lock. */

	u8	fqs_state ____cacheline_internodealigned_in_smp;
						/* Force QS state. */
	u8	boost;				/* Subject to priority boost. */
	unsigned long gpnum;			/* Current gp number. */
	unsigned long completed;		/* # of last completed gp. */
	struct task_struct *gp_kthread;		/* Task for grace periods. */
	wait_queue_head_t gp_wq;		/* Where GP task waits. */
	short gp_flags;				/* Commands for GP task. */
	short gp_state;				/* GP kthread sleep state. */

	/* End of fields guarded by root rcu_node's lock. */

	raw_spinlock_t orphan_lock ____cacheline_internodealigned_in_smp;
						/* Protect following fields. */
	struct rcu_head *orphan_nxtlist;	/* Orphaned callbacks that */
						/*  need a grace period. */
	struct rcu_head **orphan_nxttail;	/* Tail of above. */
	struct rcu_head *orphan_donelist;	/* Orphaned callbacks that */
						/*  are ready to invoke. */
	struct rcu_head **orphan_donetail;	/* Tail of above. */
	long qlen_lazy;				/* Number of lazy callbacks. */
	long qlen;				/* Total number of callbacks. */
	/* End of fields guarded by orphan_lock. */

	struct mutex onoff_mutex;		/* Coordinate hotplug & GPs. */

	struct mutex barrier_mutex;		/* Guards barrier fields. */
	atomic_t barrier_cpu_count;		/* # CPUs waiting on. */
	struct completion barrier_completion;	/* Wake at barrier end. */
	unsigned long n_barrier_done;		/* ++ at start and end of */
						/*  _rcu_barrier(). */
	/* End of fields guarded by barrier_mutex. */

	atomic_long_t expedited_start;		/* Starting ticket. */
	atomic_long_t expedited_done;		/* Done ticket. */
	atomic_long_t expedited_wrap;		/* # near-wrap incidents. */
	atomic_long_t expedited_tryfail;	/* # acquisition failures. */
	atomic_long_t expedited_workdone1;	/* # done by others #1. */
	atomic_long_t expedited_workdone2;	/* # done by others #2. */
	atomic_long_t expedited_normal;		/* # fallbacks to normal. */
	atomic_long_t expedited_stoppedcpus;	/* # successful stop_cpus. */
	atomic_long_t expedited_done_tries;	/* # tries to update _done. */
	atomic_long_t expedited_done_lost;	/* # times beaten to _done. */
	atomic_long_t expedited_done_exit;	/* # times exited _done loop. */

	unsigned long jiffies_force_qs;		/* Time at which to invoke */
						/*  force_quiescent_state(). */
	unsigned long n_force_qs;		/* Number of calls to */
						/*  force_quiescent_state(). */
	unsigned long n_force_qs_lh;		/* ~Number of calls leaving */
						/*  due to lock unavailable. */
	unsigned long n_force_qs_ngp;		/* Number of calls leaving */
						/*  due to no GP active. */
	unsigned long gp_start;			/* Time at which GP started, */
						/*  but in jiffies. */
	unsigned long gp_activity;		/* Time of last GP kthread */
						/*  activity in jiffies. */
	unsigned long jiffies_stall;		/* Time at which to check */
						/*  for CPU stalls. */
	unsigned long jiffies_resched;		/* Time at which to resched */
						/*  a reluctant CPU. */
	unsigned long n_force_qs_gpstart;	/* Snapshot of n_force_qs at */
						/*  GP start. */
	unsigned long gp_max;			/* Maximum GP duration in */
						/*  jiffies. */
	const char *name;			/* Name of structure. */
	char abbr;				/* Abbreviated name. */
	struct list_head flavors;		/* List of RCU flavors. */
};

/* Values for rcu_state structure's gp_flags field. */
#define RCU_GP_FLAG_INIT 0x1	/* Need grace-period initialization. */
#define RCU_GP_FLAG_FQS  0x2	/* Need grace-period quiescent-state forcing. */

/* Values for rcu_state structure's gp_flags field. */
#define RCU_GP_WAIT_INIT 0	/* Initial state. */
#define RCU_GP_WAIT_GPS  1	/* Wait for grace-period start. */
#define RCU_GP_WAIT_FQS  2	/* Wait for force-quiescent-state time. */

extern struct list_head rcu_struct_flavors;

/* Sequence through rcu_state structures for each RCU flavor. */
#define for_each_rcu_flavor(rsp) \
	list_for_each_entry((rsp), &rcu_struct_flavors, flavors)

/*
 * RCU implementation internal declarations:
 */
extern struct rcu_state rcu_sched_state;
DECLARE_PER_CPU(struct rcu_data, rcu_sched_data);

extern struct rcu_state rcu_bh_state;
DECLARE_PER_CPU(struct rcu_data, rcu_bh_data);

#ifdef CONFIG_PREEMPT_RCU
extern struct rcu_state rcu_preempt_state;
DECLARE_PER_CPU(struct rcu_data, rcu_preempt_data);
#endif /* #ifdef CONFIG_PREEMPT_RCU */

#ifdef CONFIG_RCU_BOOST
DECLARE_PER_CPU(unsigned int, rcu_cpu_kthread_status);
DECLARE_PER_CPU(int, rcu_cpu_kthread_cpu);
DECLARE_PER_CPU(unsigned int, rcu_cpu_kthread_loops);
DECLARE_PER_CPU(char, rcu_cpu_has_work);
#endif /* #ifdef CONFIG_RCU_BOOST */

#ifndef RCU_TREE_NONCORE

/* Forward declarations for rcutree_plugin.h */
static void rcu_bootup_announce(void);
static void rcu_preempt_note_context_switch(void);
static int rcu_preempt_blocked_readers_cgp(struct rcu_node *rnp);
#ifdef CONFIG_HOTPLUG_CPU
static bool rcu_preempt_has_tasks(struct rcu_node *rnp);
#endif /* #ifdef CONFIG_HOTPLUG_CPU */
static void rcu_print_detail_task_stall(struct rcu_state *rsp);
static int rcu_print_task_stall(struct rcu_node *rnp);
static void rcu_preempt_check_blocked_tasks(struct rcu_node *rnp);
static void rcu_preempt_check_callbacks(void);
void call_rcu(struct rcu_head *head, void (*func)(struct rcu_head *rcu));
static void __init __rcu_init_preempt(void);
static void rcu_initiate_boost(struct rcu_node *rnp, unsigned long flags);
static void rcu_preempt_boost_start_gp(struct rcu_node *rnp);
static void invoke_rcu_callbacks_kthread(void);
static bool rcu_is_callbacks_kthread(void);
#ifdef CONFIG_RCU_BOOST
static void rcu_preempt_do_callbacks(void);
static int rcu_spawn_one_boost_kthread(struct rcu_state *rsp,
						 struct rcu_node *rnp);
#endif /* #ifdef CONFIG_RCU_BOOST */
static void __init rcu_spawn_boost_kthreads(void);
static void rcu_prepare_kthreads(int cpu);
static void rcu_cleanup_after_idle(void);
static void rcu_prepare_for_idle(void);
static void rcu_idle_count_callbacks_posted(void);
static bool rcu_preempt_has_tasks(struct rcu_node *rnp);
static void print_cpu_stall_info_begin(void);
static void print_cpu_stall_info(struct rcu_state *rsp, int cpu);
static void print_cpu_stall_info_end(void);
static void zero_cpu_stall_ticks(struct rcu_data *rdp);
static void increment_cpu_stall_ticks(void);
static bool rcu_nocb_cpu_needs_barrier(struct rcu_state *rsp, int cpu);
static void rcu_nocb_gp_set(struct rcu_node *rnp, int nrq);
static void rcu_nocb_gp_cleanup(struct rcu_state *rsp, struct rcu_node *rnp);
static void rcu_init_one_nocb(struct rcu_node *rnp);
static bool __call_rcu_nocb(struct rcu_data *rdp, struct rcu_head *rhp,
			    bool lazy, unsigned long flags);
static bool rcu_nocb_adopt_orphan_cbs(struct rcu_state *rsp,
				      struct rcu_data *rdp,
				      unsigned long flags);
static int rcu_nocb_need_deferred_wakeup(struct rcu_data *rdp);
static void do_nocb_deferred_wakeup(struct rcu_data *rdp);
static void rcu_boot_init_nocb_percpu_data(struct rcu_data *rdp);
static void rcu_spawn_all_nocb_kthreads(int cpu);
static void __init rcu_spawn_nocb_kthreads(void);
#ifdef CONFIG_RCU_NOCB_CPU
static void __init rcu_organize_nocb_kthreads(struct rcu_state *rsp);
#endif /* #ifdef CONFIG_RCU_NOCB_CPU */
static void __maybe_unused rcu_kick_nohz_cpu(int cpu);
static bool init_nocb_callback_list(struct rcu_data *rdp);
static void rcu_sysidle_enter(int irq);
static void rcu_sysidle_exit(int irq);
static void rcu_sysidle_check_cpu(struct rcu_data *rdp, bool *isidle,
				  unsigned long *maxj);
static bool is_sysidle_rcu_state(struct rcu_state *rsp);
static void rcu_sysidle_report_gp(struct rcu_state *rsp, int isidle,
				  unsigned long maxj);
static void rcu_bind_gp_kthread(void);
static void rcu_sysidle_init_percpu_data(struct rcu_dynticks *rdtp);
static bool rcu_nohz_full_cpu(struct rcu_state *rsp);
static void rcu_dynticks_task_enter(void);
static void rcu_dynticks_task_exit(void);

#endif /* #ifndef RCU_TREE_NONCORE */

#ifdef CONFIG_RCU_TRACE
/* Read out queue lengths for tracing. */
static inline void rcu_nocb_q_lengths(struct rcu_data *rdp, long *ql, long *qll)
{
#ifdef CONFIG_RCU_NOCB_CPU
	*ql = atomic_long_read(&rdp->nocb_q_count);
	*qll = atomic_long_read(&rdp->nocb_q_count_lazy);
#else /* #ifdef CONFIG_RCU_NOCB_CPU */
	*ql = 0;
	*qll = 0;
#endif /* #else #ifdef CONFIG_RCU_NOCB_CPU */
}
#endif /* #ifdef CONFIG_RCU_TRACE */
