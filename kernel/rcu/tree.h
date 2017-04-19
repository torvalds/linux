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
#include <linux/rtmutex.h>
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <linux/seqlock.h>
#include <linux/swait.h>
#include <linux/stop_machine.h>

/*
 * Define shape of hierarchy based on NR_CPUS, CONFIG_RCU_FANOUT, and
 * CONFIG_RCU_FANOUT_LEAF.
 * In theory, it should be possible to add more levels straightforwardly.
 * In practice, this did work well going from three levels to four.
 * Of course, your mileage may vary.
 */

#ifdef CONFIG_RCU_FANOUT
#define RCU_FANOUT CONFIG_RCU_FANOUT
#else /* #ifdef CONFIG_RCU_FANOUT */
# ifdef CONFIG_64BIT
# define RCU_FANOUT 64
# else
# define RCU_FANOUT 32
# endif
#endif /* #else #ifdef CONFIG_RCU_FANOUT */

#ifdef CONFIG_RCU_FANOUT_LEAF
#define RCU_FANOUT_LEAF CONFIG_RCU_FANOUT_LEAF
#else /* #ifdef CONFIG_RCU_FANOUT_LEAF */
# ifdef CONFIG_64BIT
# define RCU_FANOUT_LEAF 64
# else
# define RCU_FANOUT_LEAF 32
# endif
#endif /* #else #ifdef CONFIG_RCU_FANOUT_LEAF */

#define RCU_FANOUT_1	      (RCU_FANOUT_LEAF)
#define RCU_FANOUT_2	      (RCU_FANOUT_1 * RCU_FANOUT)
#define RCU_FANOUT_3	      (RCU_FANOUT_2 * RCU_FANOUT)
#define RCU_FANOUT_4	      (RCU_FANOUT_3 * RCU_FANOUT)

#if NR_CPUS <= RCU_FANOUT_1
#  define RCU_NUM_LVLS	      1
#  define NUM_RCU_LVL_0	      1
#  define NUM_RCU_NODES	      NUM_RCU_LVL_0
#  define NUM_RCU_LVL_INIT    { NUM_RCU_LVL_0 }
#  define RCU_NODE_NAME_INIT  { "rcu_node_0" }
#  define RCU_FQS_NAME_INIT   { "rcu_node_fqs_0" }
#elif NR_CPUS <= RCU_FANOUT_2
#  define RCU_NUM_LVLS	      2
#  define NUM_RCU_LVL_0	      1
#  define NUM_RCU_LVL_1	      DIV_ROUND_UP(NR_CPUS, RCU_FANOUT_1)
#  define NUM_RCU_NODES	      (NUM_RCU_LVL_0 + NUM_RCU_LVL_1)
#  define NUM_RCU_LVL_INIT    { NUM_RCU_LVL_0, NUM_RCU_LVL_1 }
#  define RCU_NODE_NAME_INIT  { "rcu_node_0", "rcu_node_1" }
#  define RCU_FQS_NAME_INIT   { "rcu_node_fqs_0", "rcu_node_fqs_1" }
#elif NR_CPUS <= RCU_FANOUT_3
#  define RCU_NUM_LVLS	      3
#  define NUM_RCU_LVL_0	      1
#  define NUM_RCU_LVL_1	      DIV_ROUND_UP(NR_CPUS, RCU_FANOUT_2)
#  define NUM_RCU_LVL_2	      DIV_ROUND_UP(NR_CPUS, RCU_FANOUT_1)
#  define NUM_RCU_NODES	      (NUM_RCU_LVL_0 + NUM_RCU_LVL_1 + NUM_RCU_LVL_2)
#  define NUM_RCU_LVL_INIT    { NUM_RCU_LVL_0, NUM_RCU_LVL_1, NUM_RCU_LVL_2 }
#  define RCU_NODE_NAME_INIT  { "rcu_node_0", "rcu_node_1", "rcu_node_2" }
#  define RCU_FQS_NAME_INIT   { "rcu_node_fqs_0", "rcu_node_fqs_1", "rcu_node_fqs_2" }
#elif NR_CPUS <= RCU_FANOUT_4
#  define RCU_NUM_LVLS	      4
#  define NUM_RCU_LVL_0	      1
#  define NUM_RCU_LVL_1	      DIV_ROUND_UP(NR_CPUS, RCU_FANOUT_3)
#  define NUM_RCU_LVL_2	      DIV_ROUND_UP(NR_CPUS, RCU_FANOUT_2)
#  define NUM_RCU_LVL_3	      DIV_ROUND_UP(NR_CPUS, RCU_FANOUT_1)
#  define NUM_RCU_NODES	      (NUM_RCU_LVL_0 + NUM_RCU_LVL_1 + NUM_RCU_LVL_2 + NUM_RCU_LVL_3)
#  define NUM_RCU_LVL_INIT    { NUM_RCU_LVL_0, NUM_RCU_LVL_1, NUM_RCU_LVL_2, NUM_RCU_LVL_3 }
#  define RCU_NODE_NAME_INIT  { "rcu_node_0", "rcu_node_1", "rcu_node_2", "rcu_node_3" }
#  define RCU_FQS_NAME_INIT   { "rcu_node_fqs_0", "rcu_node_fqs_1", "rcu_node_fqs_2", "rcu_node_fqs_3" }
#else
# error "CONFIG_RCU_FANOUT insufficient for NR_CPUS"
#endif /* #if (NR_CPUS) <= RCU_FANOUT_1 */

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
	raw_spinlock_t __private lock;	/* Root rcu_node's lock protects */
					/*  some rcu_state fields as well as */
					/*  following. */
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
	unsigned long qsmaskinit;
				/* Per-GP initial value for qsmask. */
				/*  Initialized from ->qsmaskinitnext at the */
				/*  beginning of each grace period. */
	unsigned long qsmaskinitnext;
				/* Online CPUs for next grace period. */
	unsigned long expmask;	/* CPUs or groups that need to check in */
				/*  to allow the current expedited GP */
				/*  to complete. */
	unsigned long expmaskinit;
				/* Per-GP initial values for expmask. */
				/*  Initialized from ->expmaskinitnext at the */
				/*  beginning of each expedited GP. */
	unsigned long expmaskinitnext;
				/* Online CPUs for next expedited GP. */
				/*  Any CPU that has ever been online will */
				/*  have its bit set. */
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
#ifdef CONFIG_RCU_NOCB_CPU
	struct swait_queue_head nocb_gp_wq[2];
				/* Place for rcu_nocb_kthread() to wait GP. */
#endif /* #ifdef CONFIG_RCU_NOCB_CPU */
	int need_future_gp[2];
				/* Counts of upcoming no-CB GP requests. */
	raw_spinlock_t fqslock ____cacheline_internodealigned_in_smp;

	spinlock_t exp_lock ____cacheline_internodealigned_in_smp;
	unsigned long exp_seq_rq;
	wait_queue_head_t exp_wq[4];
} ____cacheline_internodealigned_in_smp;

/*
 * Bitmasks in an rcu_node cover the interval [grplo, grphi] of CPU IDs, and
 * are indexed relative to this interval rather than the global CPU ID space.
 * This generates the bit for a CPU in node-local masks.
 */
#define leaf_node_cpu_bit(rnp, cpu) (1UL << ((cpu) - (rnp)->grplo))

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

/*
 * Iterate over all possible CPUs in a leaf RCU node.
 */
#define for_each_leaf_node_possible_cpu(rnp, cpu) \
	for ((cpu) = cpumask_next(rnp->grplo - 1, cpu_possible_mask); \
	     cpu <= rnp->grphi; \
	     cpu = cpumask_next((cpu), cpu_possible_mask))

/*
 * Union to allow "aggregate OR" operation on the need for a quiescent
 * state by the normal and expedited grace periods.
 */
union rcu_noqs {
	struct {
		u8 norm;
		u8 exp;
	} b; /* Bits. */
	u16 s; /* Set of bits, aggregate OR here. */
};

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
	union rcu_noqs	cpu_no_qs;	/* No QSes yet for this CPU. */
	bool		core_needs_qs;	/* Core waits for quiesc state. */
	bool		beenonline;	/* CPU online at least once. */
	bool		gpwrap;		/* Possible gpnum/completed wrap. */
	struct rcu_node *mynode;	/* This CPU's leaf of hierarchy */
	unsigned long grpmask;		/* Mask to apply to leaf qsmask. */
	unsigned long	ticks_this_gp;	/* The number of scheduling-clock */
					/*  ticks this CPU has handled */
					/*  during and after the last grace */
					/* period it is aware of. */

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
	unsigned long n_rp_core_needs_qs;
	unsigned long n_rp_report_qs;
	unsigned long n_rp_cb_ready;
	unsigned long n_rp_cpu_needs_gp;
	unsigned long n_rp_gp_completed;
	unsigned long n_rp_gp_started;
	unsigned long n_rp_nocb_defer_wakeup;
	unsigned long n_rp_need_nothing;

	/* 6) _rcu_barrier(), OOM callbacks, and expediting. */
	struct rcu_head barrier_head;
#ifdef CONFIG_RCU_FAST_NO_HZ
	struct rcu_head oom_head;
#endif /* #ifdef CONFIG_RCU_FAST_NO_HZ */
	atomic_long_t exp_workdone0;	/* # done by workqueue. */
	atomic_long_t exp_workdone1;	/* # done by others #1. */
	atomic_long_t exp_workdone2;	/* # done by others #2. */
	atomic_long_t exp_workdone3;	/* # done by others #3. */
	int exp_dynticks_snap;		/* Double-check need for IPI. */

	/* 7) Callback offloading. */
#ifdef CONFIG_RCU_NOCB_CPU
	struct rcu_head *nocb_head;	/* CBs waiting for kthread. */
	struct rcu_head **nocb_tail;
	atomic_long_t nocb_q_count;	/* # CBs waiting for nocb */
	atomic_long_t nocb_q_count_lazy; /*  invocation (all stages). */
	struct rcu_head *nocb_follower_head; /* CBs ready to invoke. */
	struct rcu_head **nocb_follower_tail;
	struct swait_queue_head nocb_wq; /* For nocb kthreads to sleep on. */
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
	unsigned int softirq_snap;	/* Snapshot of softirq activity. */

	int cpu;
	struct rcu_state *rsp;
};

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
	struct rcu_node *level[RCU_NUM_LVLS + 1];
						/* Hierarchy levels (+1 to */
						/*  shut bogus gcc warning) */
	u8 flavor_mask;				/* bit in flavor mask. */
	struct rcu_data __percpu *rda;		/* pointer of percu rcu_data. */
	call_rcu_func_t call;			/* call_rcu() flavor. */
	int ncpus;				/* # CPUs seen so far. */

	/* The following fields are guarded by the root rcu_node's lock. */

	u8	boost ____cacheline_internodealigned_in_smp;
						/* Subject to priority boost. */
	unsigned long gpnum;			/* Current gp number. */
	unsigned long completed;		/* # of last completed gp. */
	struct task_struct *gp_kthread;		/* Task for grace periods. */
	struct swait_queue_head gp_wq;		/* Where GP task waits. */
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

	struct mutex barrier_mutex;		/* Guards barrier fields. */
	atomic_t barrier_cpu_count;		/* # CPUs waiting on. */
	struct completion barrier_completion;	/* Wake at barrier end. */
	unsigned long barrier_sequence;		/* ++ at start and end of */
						/*  _rcu_barrier(). */
	/* End of fields guarded by barrier_mutex. */

	struct mutex exp_mutex;			/* Serialize expedited GP. */
	struct mutex exp_wake_mutex;		/* Serialize wakeup. */
	unsigned long expedited_sequence;	/* Take a ticket. */
	atomic_t expedited_need_qs;		/* # CPUs left to check in. */
	struct swait_queue_head expedited_wq;	/* Wait for check-ins. */
	int ncpus_snap;				/* # CPUs seen last time. */

	unsigned long jiffies_force_qs;		/* Time at which to invoke */
						/*  force_quiescent_state(). */
	unsigned long jiffies_kick_kthreads;	/* Time at which to kick */
						/*  kthreads, if configured. */
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

/* Values for rcu_state structure's gp_state field. */
#define RCU_GP_IDLE	 0	/* Initial state and no GP in progress. */
#define RCU_GP_WAIT_GPS  1	/* Wait for grace-period start. */
#define RCU_GP_DONE_GPS  2	/* Wait done for grace-period start. */
#define RCU_GP_WAIT_FQS  3	/* Wait for force-quiescent-state time. */
#define RCU_GP_DOING_FQS 4	/* Wait done for force-quiescent-state time. */
#define RCU_GP_CLEANUP   5	/* Grace-period cleanup started. */
#define RCU_GP_CLEANED   6	/* Grace-period cleanup complete. */

#ifndef RCU_TREE_NONCORE
static const char * const gp_state_names[] = {
	"RCU_GP_IDLE",
	"RCU_GP_WAIT_GPS",
	"RCU_GP_DONE_GPS",
	"RCU_GP_WAIT_FQS",
	"RCU_GP_DOING_FQS",
	"RCU_GP_CLEANUP",
	"RCU_GP_CLEANED",
};
#endif /* #ifndef RCU_TREE_NONCORE */

extern struct list_head rcu_struct_flavors;

/* Sequence through rcu_state structures for each RCU flavor. */
#define for_each_rcu_flavor(rsp) \
	list_for_each_entry((rsp), &rcu_struct_flavors, flavors)

/*
 * RCU implementation internal declarations:
 */
extern struct rcu_state rcu_sched_state;

extern struct rcu_state rcu_bh_state;

#ifdef CONFIG_PREEMPT_RCU
extern struct rcu_state rcu_preempt_state;
#endif /* #ifdef CONFIG_PREEMPT_RCU */

int rcu_dynticks_snap(struct rcu_dynticks *rdtp);

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
static int rcu_print_task_exp_stall(struct rcu_node *rnp);
static void rcu_preempt_check_blocked_tasks(struct rcu_node *rnp);
static void rcu_preempt_check_callbacks(void);
void call_rcu(struct rcu_head *head, rcu_callback_t func);
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
static struct swait_queue_head *rcu_nocb_gp_get(struct rcu_node *rnp);
static void rcu_nocb_gp_cleanup(struct swait_queue_head *sq);
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

/*
 * Wrappers for the rcu_node::lock acquire and release.
 *
 * Because the rcu_nodes form a tree, the tree traversal locking will observe
 * different lock values, this in turn means that an UNLOCK of one level
 * followed by a LOCK of another level does not imply a full memory barrier;
 * and most importantly transitivity is lost.
 *
 * In order to restore full ordering between tree levels, augment the regular
 * lock acquire functions with smp_mb__after_unlock_lock().
 *
 * As ->lock of struct rcu_node is a __private field, therefore one should use
 * these wrappers rather than directly call raw_spin_{lock,unlock}* on ->lock.
 */
static inline void raw_spin_lock_rcu_node(struct rcu_node *rnp)
{
	raw_spin_lock(&ACCESS_PRIVATE(rnp, lock));
	smp_mb__after_unlock_lock();
}

static inline void raw_spin_unlock_rcu_node(struct rcu_node *rnp)
{
	raw_spin_unlock(&ACCESS_PRIVATE(rnp, lock));
}

static inline void raw_spin_lock_irq_rcu_node(struct rcu_node *rnp)
{
	raw_spin_lock_irq(&ACCESS_PRIVATE(rnp, lock));
	smp_mb__after_unlock_lock();
}

static inline void raw_spin_unlock_irq_rcu_node(struct rcu_node *rnp)
{
	raw_spin_unlock_irq(&ACCESS_PRIVATE(rnp, lock));
}

#define raw_spin_lock_irqsave_rcu_node(rnp, flags)			\
do {									\
	typecheck(unsigned long, flags);				\
	raw_spin_lock_irqsave(&ACCESS_PRIVATE(rnp, lock), flags);	\
	smp_mb__after_unlock_lock();					\
} while (0)

#define raw_spin_unlock_irqrestore_rcu_node(rnp, flags)			\
do {									\
	typecheck(unsigned long, flags);				\
	raw_spin_unlock_irqrestore(&ACCESS_PRIVATE(rnp, lock), flags);	\
} while (0)

static inline bool raw_spin_trylock_rcu_node(struct rcu_node *rnp)
{
	bool locked = raw_spin_trylock(&ACCESS_PRIVATE(rnp, lock));

	if (locked)
		smp_mb__after_unlock_lock();
	return locked;
}
