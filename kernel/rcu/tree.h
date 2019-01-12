/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Read-Copy Update mechanism for mutual exclusion (tree-based version)
 * Internal non-public definitions.
 *
 * Copyright IBM Corporation, 2008
 *
 * Author: Ingo Molnar <mingo@elte.hu>
 *	   Paul E. McKenney <paulmck@linux.ibm.com>
 */

#include <linux/cache.h>
#include <linux/spinlock.h>
#include <linux/rtmutex.h>
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <linux/seqlock.h>
#include <linux/swait.h>
#include <linux/stop_machine.h>
#include <linux/rcu_node_tree.h>

#include "rcu_segcblist.h"

/* Communicate arguments to a workqueue handler. */
struct rcu_exp_work {
	unsigned long rew_s;
	struct work_struct rew_work;
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
	unsigned long gp_seq;	/* Track rsp->rcu_gp_seq. */
	unsigned long gp_seq_needed; /* Track furthest future GP request. */
	unsigned long completedqs; /* All QSes done for this node. */
	unsigned long qsmask;	/* CPUs or groups that need to switch in */
				/*  order for current grace period to proceed.*/
				/*  In leaf rcu_node, each bit corresponds to */
				/*  an rcu_data structure, otherwise, each */
				/*  bit corresponds to a child rcu_node */
				/*  structure. */
	unsigned long rcu_gp_init_mask;	/* Mask of offline CPUs at GP init. */
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
	unsigned long ffmask;	/* Fully functional CPUs. */
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
#ifdef CONFIG_RCU_NOCB_CPU
	struct swait_queue_head nocb_gp_wq[2];
				/* Place for rcu_nocb_kthread() to wait GP. */
#endif /* #ifdef CONFIG_RCU_NOCB_CPU */
	raw_spinlock_t fqslock ____cacheline_internodealigned_in_smp;

	spinlock_t exp_lock ____cacheline_internodealigned_in_smp;
	unsigned long exp_seq_rq;
	wait_queue_head_t exp_wq[4];
	struct rcu_exp_work rew;
	bool exp_need_flush;	/* Need to flush workitem? */
} ____cacheline_internodealigned_in_smp;

/*
 * Bitmasks in an rcu_node cover the interval [grplo, grphi] of CPU IDs, and
 * are indexed relative to this interval rather than the global CPU ID space.
 * This generates the bit for a CPU in node-local masks.
 */
#define leaf_node_cpu_bit(rnp, cpu) (BIT((cpu) - (rnp)->grplo))

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

/* Per-CPU data for read-copy update. */
struct rcu_data {
	/* 1) quiescent-state and grace-period handling : */
	unsigned long	gp_seq;		/* Track rsp->rcu_gp_seq counter. */
	unsigned long	gp_seq_needed;	/* Track furthest future GP request. */
	union rcu_noqs	cpu_no_qs;	/* No QSes yet for this CPU. */
	bool		core_needs_qs;	/* Core waits for quiesc state. */
	bool		beenonline;	/* CPU online at least once. */
	bool		gpwrap;		/* Possible ->gp_seq wrap. */
	bool		deferred_qs;	/* This CPU awaiting a deferred QS? */
	struct rcu_node *mynode;	/* This CPU's leaf of hierarchy */
	unsigned long grpmask;		/* Mask to apply to leaf qsmask. */
	unsigned long	ticks_this_gp;	/* The number of scheduling-clock */
					/*  ticks this CPU has handled */
					/*  during and after the last grace */
					/* period it is aware of. */

	/* 2) batch handling */
	struct rcu_segcblist cblist;	/* Segmented callback list, with */
					/* different callbacks waiting for */
					/* different grace periods. */
	long		qlen_last_fqs_check;
					/* qlen at last check for QS forcing */
	unsigned long	n_force_qs_snap;
					/* did other CPU force QS recently? */
	long		blimit;		/* Upper limit on a processed batch */

	/* 3) dynticks interface. */
	int dynticks_snap;		/* Per-GP tracking for dynticks. */
	long dynticks_nesting;		/* Track process nesting level. */
	long dynticks_nmi_nesting;	/* Track irq/NMI nesting level. */
	atomic_t dynticks;		/* Even value for idle, else odd. */
	bool rcu_need_heavy_qs;		/* GP old, so heavy quiescent state! */
	bool rcu_urgent_qs;		/* GP old need light quiescent state. */
#ifdef CONFIG_RCU_FAST_NO_HZ
	bool all_lazy;			/* All CPU's CBs lazy at idle start? */
	unsigned long last_accelerate;	/* Last jiffy CBs were accelerated. */
	unsigned long last_advance_all;	/* Last jiffy CBs were all advanced. */
	int tick_nohz_enabled_snap;	/* Previously seen value from sysfs. */
#endif /* #ifdef CONFIG_RCU_FAST_NO_HZ */

	/* 4) rcu_barrier(), OOM callbacks, and expediting. */
	struct rcu_head barrier_head;
	int exp_dynticks_snap;		/* Double-check need for IPI. */

	/* 5) Callback offloading. */
#ifdef CONFIG_RCU_NOCB_CPU
	struct rcu_head *nocb_head;	/* CBs waiting for kthread. */
	struct rcu_head **nocb_tail;
	atomic_long_t nocb_q_count;	/* # CBs waiting for nocb */
	atomic_long_t nocb_q_count_lazy; /*  invocation (all stages). */
	struct rcu_head *nocb_follower_head; /* CBs ready to invoke. */
	struct rcu_head **nocb_follower_tail;
	struct swait_queue_head nocb_wq; /* For nocb kthreads to sleep on. */
	struct task_struct *nocb_kthread;
	raw_spinlock_t nocb_lock;	/* Guard following pair of fields. */
	int nocb_defer_wakeup;		/* Defer wakeup of nocb_kthread. */
	struct timer_list nocb_timer;	/* Enforce finite deferral. */

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

	/* 6) RCU priority boosting. */
	struct task_struct *rcu_cpu_kthread_task;
					/* rcuc per-CPU kthread or NULL. */
	unsigned int rcu_cpu_kthread_status;
	char rcu_cpu_has_work;

	/* 7) Diagnostic data, including RCU CPU stall warnings. */
	unsigned int softirq_snap;	/* Snapshot of softirq activity. */
	/* ->rcu_iw* fields protected by leaf rcu_node ->lock. */
	struct irq_work rcu_iw;		/* Check for non-irq activity. */
	bool rcu_iw_pending;		/* Is ->rcu_iw pending? */
	unsigned long rcu_iw_gp_seq;	/* ->gp_seq associated with ->rcu_iw. */
	unsigned long rcu_ofl_gp_seq;	/* ->gp_seq at last offline. */
	short rcu_ofl_gp_flags;		/* ->gp_flags at last offline. */
	unsigned long rcu_onl_gp_seq;	/* ->gp_seq at last online. */
	short rcu_onl_gp_flags;		/* ->gp_flags at last online. */
	unsigned long last_fqs_resched;	/* Time of last rcu_resched(). */

	int cpu;
};

/* Values for nocb_defer_wakeup field in struct rcu_data. */
#define RCU_NOCB_WAKE_NOT	0
#define RCU_NOCB_WAKE		1
#define RCU_NOCB_WAKE_FORCE	2

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
	int ncpus;				/* # CPUs seen so far. */

	/* The following fields are guarded by the root rcu_node's lock. */

	u8	boost ____cacheline_internodealigned_in_smp;
						/* Subject to priority boost. */
	unsigned long gp_seq;			/* Grace-period sequence #. */
	struct task_struct *gp_kthread;		/* Task for grace periods. */
	struct swait_queue_head gp_wq;		/* Where GP task waits. */
	short gp_flags;				/* Commands for GP task. */
	short gp_state;				/* GP kthread sleep state. */
	unsigned long gp_wake_time;		/* Last GP kthread wake. */
	unsigned long gp_wake_seq;		/* ->gp_seq at ^^^. */

	/* End of fields guarded by root rcu_node's lock. */

	struct mutex barrier_mutex;		/* Guards barrier fields. */
	atomic_t barrier_cpu_count;		/* # CPUs waiting on. */
	struct completion barrier_completion;	/* Wake at barrier end. */
	unsigned long barrier_sequence;		/* ++ at start and end of */
						/*  rcu_barrier(). */
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
	unsigned long gp_start;			/* Time at which GP started, */
						/*  but in jiffies. */
	unsigned long gp_end;			/* Time last GP ended, again */
						/*  in jiffies. */
	unsigned long gp_activity;		/* Time of last GP kthread */
						/*  activity in jiffies. */
	unsigned long gp_req_activity;		/* Time of last GP request */
						/*  in jiffies. */
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

	raw_spinlock_t ofl_lock ____cacheline_internodealigned_in_smp;
						/* Synchronize offline with */
						/*  GP pre-initialization. */
};

/* Values for rcu_state structure's gp_flags field. */
#define RCU_GP_FLAG_INIT 0x1	/* Need grace-period initialization. */
#define RCU_GP_FLAG_FQS  0x2	/* Need grace-period quiescent-state forcing. */

/* Values for rcu_state structure's gp_state field. */
#define RCU_GP_IDLE	 0	/* Initial state and no GP in progress. */
#define RCU_GP_WAIT_GPS  1	/* Wait for grace-period start. */
#define RCU_GP_DONE_GPS  2	/* Wait done for grace-period start. */
#define RCU_GP_ONOFF     3	/* Grace-period initialization hotplug. */
#define RCU_GP_INIT      4	/* Grace-period initialization. */
#define RCU_GP_WAIT_FQS  5	/* Wait for force-quiescent-state time. */
#define RCU_GP_DOING_FQS 6	/* Wait done for force-quiescent-state time. */
#define RCU_GP_CLEANUP   7	/* Grace-period cleanup started. */
#define RCU_GP_CLEANED   8	/* Grace-period cleanup complete. */

static const char * const gp_state_names[] = {
	"RCU_GP_IDLE",
	"RCU_GP_WAIT_GPS",
	"RCU_GP_DONE_GPS",
	"RCU_GP_ONOFF",
	"RCU_GP_INIT",
	"RCU_GP_WAIT_FQS",
	"RCU_GP_DOING_FQS",
	"RCU_GP_CLEANUP",
	"RCU_GP_CLEANED",
};

/*
 * In order to export the rcu_state name to the tracing tools, it
 * needs to be added in the __tracepoint_string section.
 * This requires defining a separate variable tp_<sname>_varname
 * that points to the string being used, and this will allow
 * the tracing userspace tools to be able to decipher the string
 * address to the matching string.
 */
#ifdef CONFIG_PREEMPT_RCU
#define RCU_ABBR 'p'
#define RCU_NAME_RAW "rcu_preempt"
#else /* #ifdef CONFIG_PREEMPT_RCU */
#define RCU_ABBR 's'
#define RCU_NAME_RAW "rcu_sched"
#endif /* #else #ifdef CONFIG_PREEMPT_RCU */
#ifndef CONFIG_TRACING
#define RCU_NAME RCU_NAME_RAW
#else /* #ifdef CONFIG_TRACING */
static char rcu_name[] = RCU_NAME_RAW;
static const char *tp_rcu_varname __used __tracepoint_string = rcu_name;
#define RCU_NAME rcu_name
#endif /* #else #ifdef CONFIG_TRACING */

int rcu_dynticks_snap(struct rcu_data *rdp);

/* Forward declarations for tree_plugin.h */
static void rcu_bootup_announce(void);
static void rcu_qs(void);
static int rcu_preempt_blocked_readers_cgp(struct rcu_node *rnp);
#ifdef CONFIG_HOTPLUG_CPU
static bool rcu_preempt_has_tasks(struct rcu_node *rnp);
#endif /* #ifdef CONFIG_HOTPLUG_CPU */
static int rcu_print_task_exp_stall(struct rcu_node *rnp);
static void rcu_preempt_check_blocked_tasks(struct rcu_node *rnp);
static void rcu_flavor_sched_clock_irq(int user);
void call_rcu(struct rcu_head *head, rcu_callback_t func);
static void dump_blkd_tasks(struct rcu_node *rnp, int ncheck);
static void rcu_initiate_boost(struct rcu_node *rnp, unsigned long flags);
static void rcu_preempt_boost_start_gp(struct rcu_node *rnp);
static void invoke_rcu_callbacks_kthread(void);
static bool rcu_is_callbacks_kthread(void);
static void __init rcu_spawn_boost_kthreads(void);
static void rcu_prepare_kthreads(int cpu);
static void rcu_cleanup_after_idle(void);
static void rcu_prepare_for_idle(void);
static bool rcu_preempt_has_tasks(struct rcu_node *rnp);
static bool rcu_preempt_need_deferred_qs(struct task_struct *t);
static void rcu_preempt_deferred_qs(struct task_struct *t);
static void zero_cpu_stall_ticks(struct rcu_data *rdp);
static bool rcu_nocb_cpu_needs_barrier(int cpu);
static struct swait_queue_head *rcu_nocb_gp_get(struct rcu_node *rnp);
static void rcu_nocb_gp_cleanup(struct swait_queue_head *sq);
static void rcu_init_one_nocb(struct rcu_node *rnp);
static bool __call_rcu_nocb(struct rcu_data *rdp, struct rcu_head *rhp,
			    bool lazy, unsigned long flags);
static bool rcu_nocb_adopt_orphan_cbs(struct rcu_data *my_rdp,
				      struct rcu_data *rdp,
				      unsigned long flags);
static int rcu_nocb_need_deferred_wakeup(struct rcu_data *rdp);
static void do_nocb_deferred_wakeup(struct rcu_data *rdp);
static void rcu_boot_init_nocb_percpu_data(struct rcu_data *rdp);
static void rcu_spawn_cpu_nocb_kthread(int cpu);
static void __init rcu_spawn_nocb_kthreads(void);
#ifdef CONFIG_RCU_NOCB_CPU
static void __init rcu_organize_nocb_kthreads(void);
#endif /* #ifdef CONFIG_RCU_NOCB_CPU */
static bool init_nocb_callback_list(struct rcu_data *rdp);
static unsigned long rcu_get_n_cbs_nocb_cpu(struct rcu_data *rdp);
static void rcu_bind_gp_kthread(void);
static bool rcu_nohz_full_cpu(void);
static void rcu_dynticks_task_enter(void);
static void rcu_dynticks_task_exit(void);

/* Forward declarations for tree_stall.h */
static void record_gp_stall_check_time(void);
static void check_cpu_stall(struct rcu_data *rdp);
