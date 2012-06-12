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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
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
#ifdef CONFIG_RCU_FAST_NO_HZ
	int dyntick_drain;	    /* Prepare-for-idle state variable. */
	unsigned long dyntick_holdoff;
				    /* No retries for the jiffy of failure. */
	struct timer_list idle_gp_timer;
				    /* Wake up CPU sleeping with callbacks. */
	unsigned long idle_gp_timer_expires;
				    /* When to wake up CPU (for repost). */
	bool idle_first_pass;	    /* First pass of attempt to go idle? */
	unsigned long nonlazy_posted;
				    /* # times non-lazy CBs posted to CPU. */
	unsigned long nonlazy_posted_snap;
				    /* idle-period nonlazy_posted snapshot. */
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
				/*  complete (only for TREE_PREEMPT_RCU). */
	atomic_t wakemask;	/* CPUs whose kthread needs to be awakened. */
				/*  Since this has meaning only for leaf */
				/*  rcu_node structures, 32 bits suffices. */
	unsigned long qsmaskinit;
				/* Per-GP initial value for qsmask & expmask. */
	unsigned long grpmask;	/* Mask to apply to parent qsmask. */
				/*  Only one bit will be set in this mask. */
	int	grplo;		/* lowest-numbered CPU or group here. */
	int	grphi;		/* highest-numbered CPU or group here. */
	u8	grpnum;		/* CPU/group number for next level up. */
	u8	level;		/* root is at level 0. */
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
	struct task_struct *node_kthread_task;
				/* kthread that takes care of this rcu_node */
				/*  structure, for example, awakening the */
				/*  per-CPU kthreads as needed. */
	unsigned int node_kthread_status;
				/* State of node_kthread_task for tracing. */
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
	unsigned long	passed_quiesce_gpnum;
					/* gpnum at time of quiescent state. */
	bool		passed_quiesce;	/* User-mode/idle loop etc. */
	bool		qs_pending;	/* Core waits for quiesc state. */
	bool		beenonline;	/* CPU online at least once. */
	bool		preemptible;	/* Preemptible RCU? */
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
	long		qlen_lazy;	/* # of lazy queued callbacks */
	long		qlen;		/* # of queued callbacks, incl lazy */
	long		qlen_last_fqs_check;
					/* qlen at last check for QS forcing */
	unsigned long	n_cbs_invoked;	/* count of RCU cbs invoked. */
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

	/* 5) __rcu_pending() statistics. */
	unsigned long n_rcu_pending;	/* rcu_pending() calls since boot. */
	unsigned long n_rp_qs_pending;
	unsigned long n_rp_report_qs;
	unsigned long n_rp_cb_ready;
	unsigned long n_rp_cpu_needs_gp;
	unsigned long n_rp_gp_completed;
	unsigned long n_rp_gp_started;
	unsigned long n_rp_need_fqs;
	unsigned long n_rp_need_nothing;

	/* 6) _rcu_barrier() callback. */
	struct rcu_head barrier_head;

	int cpu;
	struct rcu_state *rsp;
};

/* Values for fqs_state field in struct rcu_state. */
#define RCU_GP_IDLE		0	/* No grace period in progress. */
#define RCU_GP_INIT		1	/* Grace period being initialized. */
#define RCU_SAVE_DYNTICK	2	/* Need to scan dyntick state. */
#define RCU_FORCE_QS		3	/* Need to force quiescent state. */
#define RCU_SIGNAL_INIT		RCU_SAVE_DYNTICK

#define RCU_JIFFIES_TILL_FORCE_QS	 3	/* for rsp->jiffies_force_qs */

#ifdef CONFIG_PROVE_RCU
#define RCU_STALL_DELAY_DELTA	       (5 * HZ)
#else
#define RCU_STALL_DELAY_DELTA	       0
#endif
#define RCU_STALL_RAT_DELAY		2	/* Allow other CPUs time */
						/*  to take at least one */
						/*  scheduling clock irq */
						/*  before ratting on them. */

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
	struct rcu_data __percpu *rda;		/* pointer of percu rcu_data. */
	void (*call)(struct rcu_head *head,	/* call_rcu() flavor. */
		     void (*func)(struct rcu_head *head));

	/* The following fields are guarded by the root rcu_node's lock. */

	u8	fqs_state ____cacheline_internodealigned_in_smp;
						/* Force QS state. */
	u8	fqs_active;			/* force_quiescent_state() */
						/*  is running. */
	u8	fqs_need_gp;			/* A CPU was prevented from */
						/*  starting a new grace */
						/*  period because */
						/*  force_quiescent_state() */
						/*  was running. */
	u8	boost;				/* Subject to priority boost. */
	unsigned long gpnum;			/* Current gp number. */
	unsigned long completed;		/* # of last completed gp. */

	/* End of fields guarded by root rcu_node's lock. */

	raw_spinlock_t onofflock;		/* exclude on/offline and */
						/*  starting new GP. */
	struct rcu_head *orphan_nxtlist;	/* Orphaned callbacks that */
						/*  need a grace period. */
	struct rcu_head **orphan_nxttail;	/* Tail of above. */
	struct rcu_head *orphan_donelist;	/* Orphaned callbacks that */
						/*  are ready to invoke. */
	struct rcu_head **orphan_donetail;	/* Tail of above. */
	long qlen_lazy;				/* Number of lazy callbacks. */
	long qlen;				/* Total number of callbacks. */
	struct task_struct *rcu_barrier_in_progress;
						/* Task doing rcu_barrier(), */
						/*  or NULL if no barrier. */
	struct mutex barrier_mutex;		/* Guards barrier fields. */
	atomic_t barrier_cpu_count;		/* # CPUs waiting on. */
	struct completion barrier_completion;	/* Wake at barrier end. */
	unsigned long n_barrier_done;		/* ++ at start and end of */
						/*  _rcu_barrier(). */
	raw_spinlock_t fqslock;			/* Only one task forcing */
						/*  quiescent states. */
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
	unsigned long jiffies_stall;		/* Time at which to check */
						/*  for CPU stalls. */
	unsigned long gp_max;			/* Maximum GP duration in */
						/*  jiffies. */
	char *name;				/* Name of structure. */
	struct list_head flavors;		/* List of RCU flavors. */
};

extern struct list_head rcu_struct_flavors;
#define for_each_rcu_flavor(rsp) \
	list_for_each_entry((rsp), &rcu_struct_flavors, flavors)

/* Return values for rcu_preempt_offline_tasks(). */

#define RCU_OFL_TASKS_NORM_GP	0x1		/* Tasks blocking normal */
						/*  GP were moved to root. */
#define RCU_OFL_TASKS_EXP_GP	0x2		/* Tasks blocking expedited */
						/*  GP were moved to root. */

/*
 * RCU implementation internal declarations:
 */
extern struct rcu_state rcu_sched_state;
DECLARE_PER_CPU(struct rcu_data, rcu_sched_data);

extern struct rcu_state rcu_bh_state;
DECLARE_PER_CPU(struct rcu_data, rcu_bh_data);

#ifdef CONFIG_TREE_PREEMPT_RCU
extern struct rcu_state rcu_preempt_state;
DECLARE_PER_CPU(struct rcu_data, rcu_preempt_data);
#endif /* #ifdef CONFIG_TREE_PREEMPT_RCU */

#ifdef CONFIG_RCU_BOOST
DECLARE_PER_CPU(unsigned int, rcu_cpu_kthread_status);
DECLARE_PER_CPU(int, rcu_cpu_kthread_cpu);
DECLARE_PER_CPU(unsigned int, rcu_cpu_kthread_loops);
DECLARE_PER_CPU(char, rcu_cpu_has_work);
#endif /* #ifdef CONFIG_RCU_BOOST */

#ifndef RCU_TREE_NONCORE

/* Forward declarations for rcutree_plugin.h */
static void rcu_bootup_announce(void);
long rcu_batches_completed(void);
static void rcu_preempt_note_context_switch(int cpu);
static int rcu_preempt_blocked_readers_cgp(struct rcu_node *rnp);
#ifdef CONFIG_HOTPLUG_CPU
static void rcu_report_unblock_qs_rnp(struct rcu_node *rnp,
				      unsigned long flags);
static void rcu_stop_cpu_kthread(int cpu);
#endif /* #ifdef CONFIG_HOTPLUG_CPU */
static void rcu_print_detail_task_stall(struct rcu_state *rsp);
static int rcu_print_task_stall(struct rcu_node *rnp);
static void rcu_preempt_check_blocked_tasks(struct rcu_node *rnp);
#ifdef CONFIG_HOTPLUG_CPU
static int rcu_preempt_offline_tasks(struct rcu_state *rsp,
				     struct rcu_node *rnp,
				     struct rcu_data *rdp);
#endif /* #ifdef CONFIG_HOTPLUG_CPU */
static void rcu_preempt_check_callbacks(int cpu);
void call_rcu(struct rcu_head *head, void (*func)(struct rcu_head *rcu));
#if defined(CONFIG_HOTPLUG_CPU) || defined(CONFIG_TREE_PREEMPT_RCU)
static void rcu_report_exp_rnp(struct rcu_state *rsp, struct rcu_node *rnp,
			       bool wake);
#endif /* #if defined(CONFIG_HOTPLUG_CPU) || defined(CONFIG_TREE_PREEMPT_RCU) */
static void __init __rcu_init_preempt(void);
static void rcu_initiate_boost(struct rcu_node *rnp, unsigned long flags);
static void rcu_preempt_boost_start_gp(struct rcu_node *rnp);
static void invoke_rcu_callbacks_kthread(void);
static bool rcu_is_callbacks_kthread(void);
#ifdef CONFIG_RCU_BOOST
static void rcu_preempt_do_callbacks(void);
static void rcu_boost_kthread_setaffinity(struct rcu_node *rnp,
					  cpumask_var_t cm);
static int __cpuinit rcu_spawn_one_boost_kthread(struct rcu_state *rsp,
						 struct rcu_node *rnp,
						 int rnp_index);
static void invoke_rcu_node_kthread(struct rcu_node *rnp);
static void rcu_yield(void (*f)(unsigned long), unsigned long arg);
#endif /* #ifdef CONFIG_RCU_BOOST */
static void rcu_cpu_kthread_setrt(int cpu, int to_rt);
static void __cpuinit rcu_prepare_kthreads(int cpu);
static void rcu_prepare_for_idle_init(int cpu);
static void rcu_cleanup_after_idle(int cpu);
static void rcu_prepare_for_idle(int cpu);
static void rcu_idle_count_callbacks_posted(void);
static void print_cpu_stall_info_begin(void);
static void print_cpu_stall_info(struct rcu_state *rsp, int cpu);
static void print_cpu_stall_info_end(void);
static void zero_cpu_stall_ticks(struct rcu_data *rdp);
static void increment_cpu_stall_ticks(void);

#endif /* #ifndef RCU_TREE_NONCORE */
