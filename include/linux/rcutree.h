/*
 * Read-Copy Update mechanism for mutual exclusion (tree-based version)
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
 * Author: Dipankar Sarma <dipankar@in.ibm.com>
 *	   Paul E. McKenney <paulmck@linux.vnet.ibm.com> Hierarchical algorithm
 *
 * Based on the original work by Paul McKenney <paulmck@us.ibm.com>
 * and inputs from Rusty Russell, Andrea Arcangeli and Andi Kleen.
 *
 * For detailed explanation of Read-Copy Update mechanism see -
 * 	Documentation/RCU
 */

#ifndef __LINUX_RCUTREE_H
#define __LINUX_RCUTREE_H

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
#define MAX_RCU_LVLS 3
#define RCU_FANOUT	      (CONFIG_RCU_FANOUT)
#define RCU_FANOUT_SQ	      (RCU_FANOUT * RCU_FANOUT)
#define RCU_FANOUT_CUBE	      (RCU_FANOUT_SQ * RCU_FANOUT)

#if NR_CPUS <= RCU_FANOUT
#  define NUM_RCU_LVLS	      1
#  define NUM_RCU_LVL_0	      1
#  define NUM_RCU_LVL_1	      (NR_CPUS)
#  define NUM_RCU_LVL_2	      0
#  define NUM_RCU_LVL_3	      0
#elif NR_CPUS <= RCU_FANOUT_SQ
#  define NUM_RCU_LVLS	      2
#  define NUM_RCU_LVL_0	      1
#  define NUM_RCU_LVL_1	      (((NR_CPUS) + RCU_FANOUT - 1) / RCU_FANOUT)
#  define NUM_RCU_LVL_2	      (NR_CPUS)
#  define NUM_RCU_LVL_3	      0
#elif NR_CPUS <= RCU_FANOUT_CUBE
#  define NUM_RCU_LVLS	      3
#  define NUM_RCU_LVL_0	      1
#  define NUM_RCU_LVL_1	      (((NR_CPUS) + RCU_FANOUT_SQ - 1) / RCU_FANOUT_SQ)
#  define NUM_RCU_LVL_2	      (((NR_CPUS) + (RCU_FANOUT) - 1) / (RCU_FANOUT))
#  define NUM_RCU_LVL_3	      NR_CPUS
#else
# error "CONFIG_RCU_FANOUT insufficient for NR_CPUS"
#endif /* #if (NR_CPUS) <= RCU_FANOUT */

#define RCU_SUM (NUM_RCU_LVL_0 + NUM_RCU_LVL_1 + NUM_RCU_LVL_2 + NUM_RCU_LVL_3)
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
	spinlock_t lock;
	unsigned long qsmask;	/* CPUs or groups that need to switch in */
				/*  order for current grace period to proceed.*/
	unsigned long qsmaskinit;
				/* Per-GP initialization for qsmask. */
	unsigned long grpmask;	/* Mask to apply to parent qsmask. */
	int	grplo;		/* lowest-numbered CPU or group here. */
	int	grphi;		/* highest-numbered CPU or group here. */
	u8	grpnum;		/* CPU/group number for next level up. */
	u8	level;		/* root is at level 0. */
	struct rcu_node *parent;
} ____cacheline_internodealigned_in_smp;

/* Index values for nxttail array in struct rcu_data. */
#define RCU_DONE_TAIL		0	/* Also RCU_WAIT head. */
#define RCU_WAIT_TAIL		1	/* Also RCU_NEXT_READY head. */
#define RCU_NEXT_READY_TAIL	2	/* Also RCU_NEXT head. */
#define RCU_NEXT_TAIL		3
#define RCU_NEXT_SIZE		4

/* Per-CPU data for read-copy update. */
struct rcu_data {
	/* 1) quiescent-state and grace-period handling : */
	long		completed;	/* Track rsp->completed gp number */
					/*  in order to detect GP end. */
	long		gpnum;		/* Highest gp number that this CPU */
					/*  is aware of having started. */
	long		passed_quiesc_completed;
					/* Value of completed at time of qs. */
	bool		passed_quiesc;	/* User-mode/idle loop etc. */
	bool		qs_pending;	/* Core waits for quiesc state. */
	bool		beenonline;	/* CPU online at least once. */
	struct rcu_node *mynode;	/* This CPU's leaf of hierarchy */
	unsigned long grpmask;		/* Mask to apply to leaf qsmask. */

	/* 2) batch handling */
	/*
	 * If nxtlist is not NULL, it is partitioned as follows.
	 * Any of the partitions might be empty, in which case the
	 * pointer to that partition will be equal to the pointer for
	 * the following partition.  When the list is empty, all of
	 * the nxttail elements point to nxtlist, which is NULL.
	 *
	 * [*nxttail[RCU_NEXT_READY_TAIL], NULL = *nxttail[RCU_NEXT_TAIL]):
	 *	Entries that might have arrived after current GP ended
	 * [*nxttail[RCU_WAIT_TAIL], *nxttail[RCU_NEXT_READY_TAIL]):
	 *	Entries known to have arrived before current GP ended
	 * [*nxttail[RCU_DONE_TAIL], *nxttail[RCU_WAIT_TAIL]):
	 *	Entries that batch # <= ->completed - 1: waiting for current GP
	 * [nxtlist, *nxttail[RCU_DONE_TAIL]):
	 *	Entries that batch # <= ->completed
	 *	The grace period for these entries has completed, and
	 *	the other grace-period-completed entries may be moved
	 *	here temporarily in rcu_process_callbacks().
	 */
	struct rcu_head *nxtlist;
	struct rcu_head **nxttail[RCU_NEXT_SIZE];
	long		qlen; 	 	/* # of queued callbacks */
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

	/* 5) For future __rcu_pending statistics. */
	long n_rcu_pending;		/* rcu_pending() calls since boot. */

	int cpu;
};

/* Values for signaled field in struct rcu_state. */
#define RCU_GP_INIT		0	/* Grace period being initialized. */
#define RCU_SAVE_DYNTICK	1	/* Need to scan dyntick state. */
#define RCU_FORCE_QS		2	/* Need to force quiescent state. */
#ifdef CONFIG_NO_HZ
#define RCU_SIGNAL_INIT		RCU_SAVE_DYNTICK
#else /* #ifdef CONFIG_NO_HZ */
#define RCU_SIGNAL_INIT		RCU_FORCE_QS
#endif /* #else #ifdef CONFIG_NO_HZ */

#define RCU_JIFFIES_TILL_FORCE_QS	 3	/* for rsp->jiffies_force_qs */
#ifdef CONFIG_RCU_CPU_STALL_DETECTOR
#define RCU_SECONDS_TILL_STALL_CHECK   (10 * HZ)  /* for rsp->jiffies_stall */
#define RCU_SECONDS_TILL_STALL_RECHECK (30 * HZ)  /* for rsp->jiffies_stall */
#define RCU_STALL_RAT_DELAY		2	  /* Allow other CPUs time */
						  /*  to take at least one */
						  /*  scheduling clock irq */
						  /*  before ratting on them. */

#endif /* #ifdef CONFIG_RCU_CPU_STALL_DETECTOR */

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
	struct rcu_data *rda[NR_CPUS];		/* array of rdp pointers. */

	/* The following fields are guarded by the root rcu_node's lock. */

	u8	signaled ____cacheline_internodealigned_in_smp;
						/* Force QS state. */
	long	gpnum;				/* Current gp number. */
	long	completed;			/* # of last completed gp. */
	spinlock_t onofflock;			/* exclude on/offline and */
						/*  starting new GP. */
	spinlock_t fqslock;			/* Only one task forcing */
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
#ifdef CONFIG_NO_HZ
	long dynticks_completed;		/* Value of completed @ snap. */
#endif /* #ifdef CONFIG_NO_HZ */
};

extern void rcu_qsctr_inc(int cpu);
extern void rcu_bh_qsctr_inc(int cpu);

extern int rcu_pending(int cpu);
extern int rcu_needs_cpu(int cpu);

#ifdef CONFIG_DEBUG_LOCK_ALLOC
extern struct lockdep_map rcu_lock_map;
# define rcu_read_acquire()	\
			lock_acquire(&rcu_lock_map, 0, 0, 2, 1, NULL, _THIS_IP_)
# define rcu_read_release()	lock_release(&rcu_lock_map, 1, _THIS_IP_)
#else
# define rcu_read_acquire()	do { } while (0)
# define rcu_read_release()	do { } while (0)
#endif

static inline void __rcu_read_lock(void)
{
	preempt_disable();
	__acquire(RCU);
	rcu_read_acquire();
}
static inline void __rcu_read_unlock(void)
{
	rcu_read_release();
	__release(RCU);
	preempt_enable();
}
static inline void __rcu_read_lock_bh(void)
{
	local_bh_disable();
	__acquire(RCU_BH);
	rcu_read_acquire();
}
static inline void __rcu_read_unlock_bh(void)
{
	rcu_read_release();
	__release(RCU_BH);
	local_bh_enable();
}

#define __synchronize_sched() synchronize_rcu()

#define call_rcu_sched(head, func) call_rcu(head, func)

static inline void rcu_init_sched(void)
{
}

extern void __rcu_init(void);
extern void rcu_check_callbacks(int cpu, int user);
extern void rcu_restart_cpu(int cpu);

extern long rcu_batches_completed(void);
extern long rcu_batches_completed_bh(void);

#ifdef CONFIG_NO_HZ
void rcu_enter_nohz(void);
void rcu_exit_nohz(void);
#else /* CONFIG_NO_HZ */
static inline void rcu_enter_nohz(void)
{
}
static inline void rcu_exit_nohz(void)
{
}
#endif /* CONFIG_NO_HZ */

/* A context switch is a grace period for rcutree. */
static inline int rcu_blocking_is_gp(void)
{
	return num_online_cpus() == 1;
}

#endif /* __LINUX_RCUTREE_H */
