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
 * Define shape of hierarchy based on NR_CPUS and CONFIG_RCU_FANOUT.
 * In theory, it should be possible to add more levels straightforwardly.
 * In practice, this has not been tested, so there is probably some
 * bug somewhere.
 */
#define MAX_RCU_LVLS 4
#define RCU_FANOUT	      (CONFIG_RCU_FANOUT)
#define RCU_FANOUT_SQ	      (RCU_FANOUT * RCU_FANOUT)
#define RCU_FANOUT_CUBE	      (RCU_FANOUT_SQ * RCU_FANOUT)
#define RCU_FANOUT_FOURTH     (RCU_FANOUT_CUBE * RCU_FANOUT)

#if NR_CPUS <= RCU_FANOUT
#  define NUM_RCU_LVLS	      1
#  define NUM_RCU_LVL_0	      1
#  define NUM_RCU_LVL_1	      (NR_CPUS)
#  define NUM_RCU_LVL_2	      0
#  define NUM_RCU_LVL_3	      0
#  define NUM_RCU_LVL_4	      0
#elif NR_CPUS <= RCU_FANOUT_SQ
#  define NUM_RCU_LVLS	      2
#  define NUM_RCU_LVL_0	      1
#  define NUM_RCU_LVL_1	      DIV_ROUND_UP(NR_CPUS, RCU_FANOUT)
#  define NUM_RCU_LVL_2	      (NR_CPUS)
#  define NUM_RCU_LVL_3	      0
#  define NUM_RCU_LVL_4	      0
#elif NR_CPUS <= RCU_FANOUT_CUBE
#  define NUM_RCU_LVLS	      3
#  define NUM_RCU_LVL_0	      1
#  define NUM_RCU_LVL_1	      DIV_ROUND_UP(NR_CPUS, RCU_FANOUT_SQ)
#  define NUM_RCU_LVL_2	      DIV_ROUND_UP(NR_CPUS, RCU_FANOUT)
#  define NUM_RCU_LVL_3	      NR_CPUS
#  define NUM_RCU_LVL_4	      0
#elif NR_CPUS <= RCU_FANOUT_FOURTH
#  define NUM_RCU_LVLS	      4
#  define NUM_RCU_LVL_0	      1
#  define NUM_RCU_LVL_1	      DIV_ROUND_UP(NR_CPUS, RCU_FANOUT_CUBE)
#  define NUM_RCU_LVL_2	      DIV_ROUND_UP(NR_CPUS, RCU_FANOUT_SQ)
#  define NUM_RCU_LVL_3	      DIV_ROUND_UP(NR_CPUS, RCU_FANOUT)
#  define NUM_RCU_LVL_4	      NR_CPUS
#else
# error "CONFIG_RCU_FANOUT insufficient for NR_CPUS"
#endif /* #if (NR_CPUS) <= RCU_FANOUT */

#define RCU_SUM (NUM_RCU_LVL_0 + NUM_RCU_LVL_1 + NUM_RCU_LVL_2 + NUM_RCU_LVL_3 + NUM_RCU_LVL_4)
#define NUM_RCU_NODES (RCU_SUM - NR_CPUS)

/*
 * Dynticks per-CPU state.
 */
struct rcu_dynticks {
	int dynticks_nesting;	/* Track nesting level, sort of. */
	int dynticks;		/* Even value for dynticks-idle, else odd. */
	int dynticks_nmi;	/* Even value for either dynticks-idle or */
				/*  not in nmi handler, else odd.  So this */
				/*  remains even for nmi from irq handler. */
};

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
	unsigned long expmask;	/* Groups that have ->blocked_tasks[] */
				/*  elements that need to drain to allow the */
				/*  current expedited grace period to */
				/*  complete (only for TREE_PREEMPT_RCU). */
	unsigned long qsmaskinit;
				/* Per-GP initial value for qsmask & expmask. */
	unsigned long grpmask;	/* Mask to apply to parent qsmask. */
				/*  Only one bit will be set in this mask. */
	int	grplo;		/* lowest-numbered CPU or group here. */
	int	grphi;		/* highest-numbered CPU or group here. */
	u8	grpnum;		/* CPU/group number for next level up. */
	u8	level;		/* root is at level 0. */
	struct rcu_node *parent;
	struct list_head blocked_tasks[4];
				/* Tasks blocked in RCU read-side critsect. */
				/*  Grace period number (->gpnum) x blocked */
				/*  by tasks on the (x & 0x1) element of the */
				/*  blocked_tasks[] array. */
} ____cacheline_internodealigned_in_smp;

/*
 * Do a full breadth-first scan of the rcu_node structures for the
 * specified rcu_state structure.
 */
#define rcu_for_each_node_breadth_first(rsp, rnp) \
	for ((rnp) = &(rsp)->node[0]; \
	     (rnp) < &(rsp)->node[NUM_RCU_NODES]; (rnp)++)

/*
 * Do a breadth-first scan of the non-leaf rcu_node structures for the
 * specified rcu_state structure.  Note that if there is a singleton
 * rcu_node tree with but one rcu_node structure, this loop is a no-op.
 */
#define rcu_for_each_nonleaf_node_breadth_first(rsp, rnp) \
	for ((rnp) = &(rsp)->node[0]; \
	     (rnp) < (rsp)->level[NUM_RCU_LVLS - 1]; (rnp)++)

/*
 * Scan the leaves of the rcu_node hierarchy for the specified rcu_state
 * structure.  Note that if there is a singleton rcu_node tree with but
 * one rcu_node structure, this loop -will- visit the rcu_node structure.
 * It is still a leaf node, even if it is also the root node.
 */
#define rcu_for_each_leaf_node(rsp, rnp) \
	for ((rnp) = (rsp)->level[NUM_RCU_LVLS - 1]; \
	     (rnp) < &(rsp)->node[NUM_RCU_NODES]; (rnp)++)

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
	unsigned long	passed_quiesc_completed;
					/* Value of completed at time of qs. */
	bool		passed_quiesc;	/* User-mode/idle loop etc. */
	bool		qs_pending;	/* Core waits for quiesc state. */
	bool		beenonline;	/* CPU online at least once. */
	bool		preemptable;	/* Preemptable RCU? */
	struct rcu_node *mynode;	/* This CPU's leaf of hierarchy */
	unsigned long grpmask;		/* Mask to apply to leaf qsmask. */

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
	long		qlen;		/* # of queued callbacks */
	long		qlen_last_fqs_check;
					/* qlen at last check for QS forcing */
	unsigned long	n_force_qs_snap;
					/* did other CPU force QS recently? */
	long		blimit;		/* Upper limit on a processed batch */

#ifdef CONFIG_NO_HZ
	/* 3) dynticks interface. */
	struct rcu_dynticks *dynticks;	/* Shared per-CPU dynticks state. */
	int dynticks_snap;		/* Per-GP tracking for dynticks. */
	int dynticks_nmi_snap;		/* Per-GP tracking for dynticks_nmi. */
#endif /* #ifdef CONFIG_NO_HZ */

	/* 4) reasons this CPU needed to be kicked by force_quiescent_state */
#ifdef CONFIG_NO_HZ
	unsigned long dynticks_fqs;	/* Kicked due to dynticks idle. */
#endif /* #ifdef CONFIG_NO_HZ */
	unsigned long offline_fqs;	/* Kicked due to being offline. */
	unsigned long resched_ipi;	/* Sent a resched IPI. */

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

	int cpu;
};

/* Values for signaled field in struct rcu_state. */
#define RCU_GP_IDLE		0	/* No grace period in progress. */
#define RCU_GP_INIT		1	/* Grace period being initialized. */
#define RCU_SAVE_DYNTICK	2	/* Need to scan dyntick state. */
#define RCU_FORCE_QS		3	/* Need to force quiescent state. */
#ifdef CONFIG_NO_HZ
#define RCU_SIGNAL_INIT		RCU_SAVE_DYNTICK
#else /* #ifdef CONFIG_NO_HZ */
#define RCU_SIGNAL_INIT		RCU_FORCE_QS
#endif /* #else #ifdef CONFIG_NO_HZ */

#define RCU_JIFFIES_TILL_FORCE_QS	 3	/* for rsp->jiffies_force_qs */
#ifdef CONFIG_RCU_CPU_STALL_DETECTOR

#ifdef CONFIG_PROVE_RCU
#define RCU_STALL_DELAY_DELTA	       (5 * HZ)
#else
#define RCU_STALL_DELAY_DELTA	       0
#endif

#define RCU_SECONDS_TILL_STALL_CHECK   (10 * HZ + RCU_STALL_DELAY_DELTA)
						/* for rsp->jiffies_stall */
#define RCU_SECONDS_TILL_STALL_RECHECK (30 * HZ + RCU_STALL_DELAY_DELTA)
						/* for rsp->jiffies_stall */
#define RCU_STALL_RAT_DELAY		2	/* Allow other CPUs time */
						/*  to take at least one */
						/*  scheduling clock irq */
						/*  before ratting on them. */

#endif /* #ifdef CONFIG_RCU_CPU_STALL_DETECTOR */

#define ULONG_CMP_GE(a, b)	(ULONG_MAX / 2 >= (a) - (b))
#define ULONG_CMP_LT(a, b)	(ULONG_MAX / 2 < (a) - (b))

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
	struct rcu_node *level[NUM_RCU_LVLS];	/* Hierarchy levels. */
	u32 levelcnt[MAX_RCU_LVLS + 1];		/* # nodes in each level. */
	u8 levelspread[NUM_RCU_LVLS];		/* kids/node in each level. */
	struct rcu_data __percpu *rda;		/* pointer of percu rcu_data. */

	/* The following fields are guarded by the root rcu_node's lock. */

	u8	signaled ____cacheline_internodealigned_in_smp;
						/* Force QS state. */
	u8	fqs_active;			/* force_quiescent_state() */
						/*  is running. */
	u8	fqs_need_gp;			/* A CPU was prevented from */
						/*  starting a new grace */
						/*  period because */
						/*  force_quiescent_state() */
						/*  was running. */
	unsigned long gpnum;			/* Current gp number. */
	unsigned long completed;		/* # of last completed gp. */

	/* End of fields guarded by root rcu_node's lock. */

	raw_spinlock_t onofflock;		/* exclude on/offline and */
						/*  starting new GP.  Also */
						/*  protects the following */
						/*  orphan_cbs fields. */
	struct rcu_head *orphan_cbs_list;	/* list of rcu_head structs */
						/*  orphaned by all CPUs in */
						/*  a given leaf rcu_node */
						/*  going offline. */
	struct rcu_head **orphan_cbs_tail;	/* And tail pointer. */
	long orphan_qlen;			/* Number of orphaned cbs. */
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
#ifdef CONFIG_RCU_CPU_STALL_DETECTOR
	unsigned long gp_start;			/* Time at which GP started, */
						/*  but in jiffies. */
	unsigned long jiffies_stall;		/* Time at which to check */
						/*  for CPU stalls. */
#endif /* #ifdef CONFIG_RCU_CPU_STALL_DETECTOR */
	char *name;				/* Name of structure. */
};

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

#ifndef RCU_TREE_NONCORE

/* Forward declarations for rcutree_plugin.h */
static void rcu_bootup_announce(void);
long rcu_batches_completed(void);
static void rcu_preempt_note_context_switch(int cpu);
static int rcu_preempted_readers(struct rcu_node *rnp);
#ifdef CONFIG_HOTPLUG_CPU
static void rcu_report_unblock_qs_rnp(struct rcu_node *rnp,
				      unsigned long flags);
#endif /* #ifdef CONFIG_HOTPLUG_CPU */
#ifdef CONFIG_RCU_CPU_STALL_DETECTOR
static void rcu_print_detail_task_stall(struct rcu_state *rsp);
static void rcu_print_task_stall(struct rcu_node *rnp);
#endif /* #ifdef CONFIG_RCU_CPU_STALL_DETECTOR */
static void rcu_preempt_check_blocked_tasks(struct rcu_node *rnp);
#ifdef CONFIG_HOTPLUG_CPU
static int rcu_preempt_offline_tasks(struct rcu_state *rsp,
				     struct rcu_node *rnp,
				     struct rcu_data *rdp);
static void rcu_preempt_offline_cpu(int cpu);
#endif /* #ifdef CONFIG_HOTPLUG_CPU */
static void rcu_preempt_check_callbacks(int cpu);
static void rcu_preempt_process_callbacks(void);
void call_rcu(struct rcu_head *head, void (*func)(struct rcu_head *rcu));
#if defined(CONFIG_HOTPLUG_CPU) || defined(CONFIG_TREE_PREEMPT_RCU)
static void rcu_report_exp_rnp(struct rcu_state *rsp, struct rcu_node *rnp);
#endif /* #if defined(CONFIG_HOTPLUG_CPU) || defined(CONFIG_TREE_PREEMPT_RCU) */
static int rcu_preempt_pending(int cpu);
static int rcu_preempt_needs_cpu(int cpu);
static void __cpuinit rcu_preempt_init_percpu_data(int cpu);
static void rcu_preempt_send_cbs_to_orphanage(void);
static void __init __rcu_init_preempt(void);
static void rcu_needs_cpu_flush(void);

#endif /* #ifndef RCU_TREE_NONCORE */
