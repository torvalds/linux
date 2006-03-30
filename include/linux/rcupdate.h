/*
 * Read-Copy Update mechanism for mutual exclusion 
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
 * Copyright (C) IBM Corporation, 2001
 *
 * Author: Dipankar Sarma <dipankar@in.ibm.com>
 * 
 * Based on the original work by Paul McKenney <paul.mckenney@us.ibm.com>
 * and inputs from Rusty Russell, Andrea Arcangeli and Andi Kleen.
 * Papers:
 * http://www.rdrop.com/users/paulmck/paper/rclockpdcsproof.pdf
 * http://lse.sourceforge.net/locking/rclock_OLS.2001.05.01c.sc.pdf (OLS2001)
 *
 * For detailed explanation of Read-Copy Update mechanism see -
 * 		http://lse.sourceforge.net/locking/rcupdate.html
 *
 */

#ifndef __LINUX_RCUPDATE_H
#define __LINUX_RCUPDATE_H

#ifdef __KERNEL__

#include <linux/cache.h>
#include <linux/spinlock.h>
#include <linux/threads.h>
#include <linux/percpu.h>
#include <linux/cpumask.h>
#include <linux/seqlock.h>

/**
 * struct rcu_head - callback structure for use with RCU
 * @next: next update requests in a list
 * @func: actual update function to call after the grace period.
 */
struct rcu_head {
	struct rcu_head *next;
	void (*func)(struct rcu_head *head);
};

#define RCU_HEAD_INIT 	{ .next = NULL, .func = NULL }
#define RCU_HEAD(head) struct rcu_head head = RCU_HEAD_INIT
#define INIT_RCU_HEAD(ptr) do { \
       (ptr)->next = NULL; (ptr)->func = NULL; \
} while (0)



/* Global control variables for rcupdate callback mechanism. */
struct rcu_ctrlblk {
	long	cur;		/* Current batch number.                      */
	long	completed;	/* Number of the last completed batch         */
	int	next_pending;	/* Is the next batch already waiting?         */

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
#ifdef CONFIG_SMP
	long		last_rs_qlen;	 /* qlen during the last resched */
#endif
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

/**
 * rcu_read_lock - mark the beginning of an RCU read-side critical section.
 *
 * When synchronize_rcu() is invoked on one CPU while other CPUs
 * are within RCU read-side critical sections, then the
 * synchronize_rcu() is guaranteed to block until after all the other
 * CPUs exit their critical sections.  Similarly, if call_rcu() is invoked
 * on one CPU while other CPUs are within RCU read-side critical
 * sections, invocation of the corresponding RCU callback is deferred
 * until after the all the other CPUs exit their critical sections.
 *
 * Note, however, that RCU callbacks are permitted to run concurrently
 * with RCU read-side critical sections.  One way that this can happen
 * is via the following sequence of events: (1) CPU 0 enters an RCU
 * read-side critical section, (2) CPU 1 invokes call_rcu() to register
 * an RCU callback, (3) CPU 0 exits the RCU read-side critical section,
 * (4) CPU 2 enters a RCU read-side critical section, (5) the RCU
 * callback is invoked.  This is legal, because the RCU read-side critical
 * section that was running concurrently with the call_rcu() (and which
 * therefore might be referencing something that the corresponding RCU
 * callback would free up) has completed before the corresponding
 * RCU callback is invoked.
 *
 * RCU read-side critical sections may be nested.  Any deferred actions
 * will be deferred until the outermost RCU read-side critical section
 * completes.
 *
 * It is illegal to block while in an RCU read-side critical section.
 */
#define rcu_read_lock()		preempt_disable()

/**
 * rcu_read_unlock - marks the end of an RCU read-side critical section.
 *
 * See rcu_read_lock() for more information.
 */
#define rcu_read_unlock()	preempt_enable()

/*
 * So where is rcu_write_lock()?  It does not exist, as there is no
 * way for writers to lock out RCU readers.  This is a feature, not
 * a bug -- this property is what provides RCU's performance benefits.
 * Of course, writers must coordinate with each other.  The normal
 * spinlock primitives work well for this, but any other technique may be
 * used as well.  RCU does not care how the writers keep out of each
 * others' way, as long as they do so.
 */

/**
 * rcu_read_lock_bh - mark the beginning of a softirq-only RCU critical section
 *
 * This is equivalent of rcu_read_lock(), but to be used when updates
 * are being done using call_rcu_bh(). Since call_rcu_bh() callbacks
 * consider completion of a softirq handler to be a quiescent state,
 * a process in RCU read-side critical section must be protected by
 * disabling softirqs. Read-side critical sections in interrupt context
 * can use just rcu_read_lock().
 *
 */
#define rcu_read_lock_bh()	local_bh_disable()

/*
 * rcu_read_unlock_bh - marks the end of a softirq-only RCU critical section
 *
 * See rcu_read_lock_bh() for more information.
 */
#define rcu_read_unlock_bh()	local_bh_enable()

/**
 * rcu_dereference - fetch an RCU-protected pointer in an
 * RCU read-side critical section.  This pointer may later
 * be safely dereferenced.
 *
 * Inserts memory barriers on architectures that require them
 * (currently only the Alpha), and, more importantly, documents
 * exactly which pointers are protected by RCU.
 */

#define rcu_dereference(p)     ({ \
				typeof(p) _________p1 = p; \
				smp_read_barrier_depends(); \
				(_________p1); \
				})

/**
 * rcu_assign_pointer - assign (publicize) a pointer to a newly
 * initialized structure that will be dereferenced by RCU read-side
 * critical sections.  Returns the value assigned.
 *
 * Inserts memory barriers on architectures that require them
 * (pretty much all of them other than x86), and also prevents
 * the compiler from reordering the code that initializes the
 * structure after the pointer assignment.  More importantly, this
 * call documents which pointers will be dereferenced by RCU read-side
 * code.
 */

#define rcu_assign_pointer(p, v)	({ \
						smp_wmb(); \
						(p) = (v); \
					})

/**
 * synchronize_sched - block until all CPUs have exited any non-preemptive
 * kernel code sequences.
 *
 * This means that all preempt_disable code sequences, including NMI and
 * hardware-interrupt handlers, in progress on entry will have completed
 * before this primitive returns.  However, this does not guarantee that
 * softirq handlers will have completed, since in some kernels, these
 * handlers can run in process context, and can block.
 *
 * This primitive provides the guarantees made by the (deprecated)
 * synchronize_kernel() API.  In contrast, synchronize_rcu() only
 * guarantees that rcu_read_lock() sections will have completed.
 * In "classic RCU", these two guarantees happen to be one and
 * the same, but can differ in realtime RCU implementations.
 */
#define synchronize_sched() synchronize_rcu()

extern void rcu_init(void);
extern void rcu_check_callbacks(int cpu, int user);
extern void rcu_restart_cpu(int cpu);
extern long rcu_batches_completed(void);

/* Exported interfaces */
extern void FASTCALL(call_rcu(struct rcu_head *head, 
				void (*func)(struct rcu_head *head)));
extern void FASTCALL(call_rcu_bh(struct rcu_head *head,
				void (*func)(struct rcu_head *head)));
extern __deprecated_for_modules void synchronize_kernel(void);
extern void synchronize_rcu(void);
void synchronize_idle(void);
extern void rcu_barrier(void);

#endif /* __KERNEL__ */
#endif /* __LINUX_RCUPDATE_H */
