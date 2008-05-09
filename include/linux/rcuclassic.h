/*
 * Read-Copy Update mechanism for mutual exclusion (classic version)
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
 * Copyright IBM Corporation, 2001
 *
 * Author: Dipankar Sarma <dipankar@in.ibm.com>
 *
 * Based on the original work by Paul McKenney <paulmck@us.ibm.com>
 * and inputs from Rusty Russell, Andrea Arcangeli and Andi Kleen.
 * Papers:
 * http://www.rdrop.com/users/paulmck/paper/rclockpdcsproof.pdf
 * http://lse.sourceforge.net/locking/rclock_OLS.2001.05.01c.sc.pdf (OLS2001)
 *
 * For detailed explanation of Read-Copy Update mechanism see -
 * 		Documentation/RCU
 *
 */

#ifndef __LINUX_RCUCLASSIC_H
#define __LINUX_RCUCLASSIC_H

#include <linux/cache.h>
#include <linux/spinlock.h>
#include <linux/threads.h>
#include <linux/percpu.h>
#include <linux/cpumask.h>
#include <linux/seqlock.h>


/* Global control variables for rcupdate callback mechanism. */
struct rcu_ctrlblk {
	long	cur;		/* Current batch number.                      */
	long	completed;	/* Number of the last completed batch         */
	int	next_pending;	/* Is the next batch already waiting?         */

	int	signaled;

	spinlock_t	lock	____cacheline_internodealigned_in_smp;
	cpumask_t	cpumask; /* CPUs that need to switch in order    */
				 /* for current batch to proceed.        */
} ____cacheline_internodealigned_in_smp;

/* Is batch a before batch b ? */
static inline int rcu_batch_before(long a, long b)
{
	return (a - b) < 0;
}

/* Is batch a after batch b ? */
static inline int rcu_batch_after(long a, long b)
{
	return (a - b) > 0;
}

/*
 * Per-CPU data for Read-Copy UPdate.
 * nxtlist - new callbacks are added here
 * curlist - current batch for which quiescent cycle started if any
 */
struct rcu_data {
	/* 1) quiescent state handling : */
	long		quiescbatch;     /* Batch # for grace period */
	int		passed_quiesc;	 /* User-mode/idle loop etc. */
	int		qs_pending;	 /* core waits for quiesc state */

	/* 2) batch handling */
	long  	       	batch;           /* Batch # for current RCU batch */
	struct rcu_head *nxtlist;
	struct rcu_head **nxttail;
	long            qlen; 	 	 /* # of queued callbacks */
	struct rcu_head *curlist;
	struct rcu_head **curtail;
	struct rcu_head *donelist;
	struct rcu_head **donetail;
	long		blimit;		 /* Upper limit on a processed batch */
	int cpu;
	struct rcu_head barrier;
};

DECLARE_PER_CPU(struct rcu_data, rcu_data);
DECLARE_PER_CPU(struct rcu_data, rcu_bh_data);

/*
 * Increment the quiescent state counter.
 * The counter is a bit degenerated: We do not need to know
 * how many quiescent states passed, just if there was at least
 * one since the start of the grace period. Thus just a flag.
 */
static inline void rcu_qsctr_inc(int cpu)
{
	struct rcu_data *rdp = &per_cpu(rcu_data, cpu);
	rdp->passed_quiesc = 1;
}
static inline void rcu_bh_qsctr_inc(int cpu)
{
	struct rcu_data *rdp = &per_cpu(rcu_bh_data, cpu);
	rdp->passed_quiesc = 1;
}

extern int rcu_pending(int cpu);
extern int rcu_needs_cpu(int cpu);

#ifdef CONFIG_DEBUG_LOCK_ALLOC
extern struct lockdep_map rcu_lock_map;
# define rcu_read_acquire()	\
			lock_acquire(&rcu_lock_map, 0, 0, 2, 1, _THIS_IP_)
# define rcu_read_release()	lock_release(&rcu_lock_map, 1, _THIS_IP_)
#else
# define rcu_read_acquire()	do { } while (0)
# define rcu_read_release()	do { } while (0)
#endif

#define __rcu_read_lock() \
	do { \
		preempt_disable(); \
		__acquire(RCU); \
		rcu_read_acquire(); \
	} while (0)
#define __rcu_read_unlock() \
	do { \
		rcu_read_release(); \
		__release(RCU); \
		preempt_enable(); \
	} while (0)
#define __rcu_read_lock_bh() \
	do { \
		local_bh_disable(); \
		__acquire(RCU_BH); \
		rcu_read_acquire(); \
	} while (0)
#define __rcu_read_unlock_bh() \
	do { \
		rcu_read_release(); \
		__release(RCU_BH); \
		local_bh_enable(); \
	} while (0)

#define __synchronize_sched() synchronize_rcu()

extern void __rcu_init(void);
extern void rcu_check_callbacks(int cpu, int user);
extern void rcu_restart_cpu(int cpu);

extern long rcu_batches_completed(void);
extern long rcu_batches_completed_bh(void);

#define rcu_enter_nohz()	do { } while (0)
#define rcu_exit_nohz()		do { } while (0)

#endif /* __LINUX_RCUCLASSIC_H */
