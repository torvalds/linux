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
 * Copyright IBM Corporation, 2001
 *
 * Authors: Dipankar Sarma <dipankar@in.ibm.com>
 *	    Manfred Spraul <manfred@colorfullife.com>
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
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/smp.h>
#include <linux/rcupdate.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <asm/atomic.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/completion.h>
#include <linux/moduleparam.h>
#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/mutex.h>
#include <linux/time.h>

#ifdef CONFIG_DEBUG_LOCK_ALLOC
static struct lock_class_key rcu_lock_key;
struct lockdep_map rcu_lock_map =
	STATIC_LOCKDEP_MAP_INIT("rcu_read_lock", &rcu_lock_key);
EXPORT_SYMBOL_GPL(rcu_lock_map);
#endif


/* Definition for rcupdate control block. */
static struct rcu_ctrlblk rcu_ctrlblk = {
	.cur = -300,
	.completed = -300,
	.pending = -300,
	.lock = __SPIN_LOCK_UNLOCKED(&rcu_ctrlblk.lock),
	.cpumask = CPU_BITS_NONE,
};

static struct rcu_ctrlblk rcu_bh_ctrlblk = {
	.cur = -300,
	.completed = -300,
	.pending = -300,
	.lock = __SPIN_LOCK_UNLOCKED(&rcu_bh_ctrlblk.lock),
	.cpumask = CPU_BITS_NONE,
};

static DEFINE_PER_CPU(struct rcu_data, rcu_data);
static DEFINE_PER_CPU(struct rcu_data, rcu_bh_data);

/*
 * Increment the quiescent state counter.
 * The counter is a bit degenerated: We do not need to know
 * how many quiescent states passed, just if there was at least
 * one since the start of the grace period. Thus just a flag.
 */
void rcu_qsctr_inc(int cpu)
{
	struct rcu_data *rdp = &per_cpu(rcu_data, cpu);
	rdp->passed_quiesc = 1;
}

void rcu_bh_qsctr_inc(int cpu)
{
	struct rcu_data *rdp = &per_cpu(rcu_bh_data, cpu);
	rdp->passed_quiesc = 1;
}

static int blimit = 10;
static int qhimark = 10000;
static int qlowmark = 100;

#ifdef CONFIG_SMP
static void force_quiescent_state(struct rcu_data *rdp,
			struct rcu_ctrlblk *rcp)
{
	int cpu;
	unsigned long flags;

	set_need_resched();
	spin_lock_irqsave(&rcp->lock, flags);
	if (unlikely(!rcp->signaled)) {
		rcp->signaled = 1;
		/*
		 * Don't send IPI to itself. With irqs disabled,
		 * rdp->cpu is the current cpu.
		 *
		 * cpu_online_mask is updated by the _cpu_down()
		 * using __stop_machine(). Since we're in irqs disabled
		 * section, __stop_machine() is not exectuting, hence
		 * the cpu_online_mask is stable.
		 *
		 * However,  a cpu might have been offlined _just_ before
		 * we disabled irqs while entering here.
		 * And rcu subsystem might not yet have handled the CPU_DEAD
		 * notification, leading to the offlined cpu's bit
		 * being set in the rcp->cpumask.
		 *
		 * Hence cpumask = (rcp->cpumask & cpu_online_mask) to prevent
		 * sending smp_reschedule() to an offlined CPU.
		 */
		for_each_cpu_and(cpu,
				  to_cpumask(rcp->cpumask), cpu_online_mask) {
			if (cpu != rdp->cpu)
				smp_send_reschedule(cpu);
		}
	}
	spin_unlock_irqrestore(&rcp->lock, flags);
}
#else
static inline void force_quiescent_state(struct rcu_data *rdp,
			struct rcu_ctrlblk *rcp)
{
	set_need_resched();
}
#endif

static void __call_rcu(struct rcu_head *head, struct rcu_ctrlblk *rcp,
		struct rcu_data *rdp)
{
	long batch;

	head->next = NULL;
	smp_mb(); /* Read of rcu->cur must happen after any change by caller. */

	/*
	 * Determine the batch number of this callback.
	 *
	 * Using ACCESS_ONCE to avoid the following error when gcc eliminates
	 * local variable "batch" and emits codes like this:
	 *	1) rdp->batch = rcp->cur + 1 # gets old value
	 *	......
	 *	2)rcu_batch_after(rcp->cur + 1, rdp->batch) # gets new value
	 * then [*nxttail[0], *nxttail[1]) may contain callbacks
	 * that batch# = rdp->batch, see the comment of struct rcu_data.
	 */
	batch = ACCESS_ONCE(rcp->cur) + 1;

	if (rdp->nxtlist && rcu_batch_after(batch, rdp->batch)) {
		/* process callbacks */
		rdp->nxttail[0] = rdp->nxttail[1];
		rdp->nxttail[1] = rdp->nxttail[2];
		if (rcu_batch_after(batch - 1, rdp->batch))
			rdp->nxttail[0] = rdp->nxttail[2];
	}

	rdp->batch = batch;
	*rdp->nxttail[2] = head;
	rdp->nxttail[2] = &head->next;

	if (unlikely(++rdp->qlen > qhimark)) {
		rdp->blimit = INT_MAX;
		force_quiescent_state(rdp, &rcu_ctrlblk);
	}
}

#ifdef CONFIG_RCU_CPU_STALL_DETECTOR

static void record_gp_stall_check_time(struct rcu_ctrlblk *rcp)
{
	rcp->gp_start = jiffies;
	rcp->jiffies_stall = jiffies + RCU_SECONDS_TILL_STALL_CHECK;
}

static void print_other_cpu_stall(struct rcu_ctrlblk *rcp)
{
	int cpu;
	long delta;
	unsigned long flags;

	/* Only let one CPU complain about others per time interval. */

	spin_lock_irqsave(&rcp->lock, flags);
	delta = jiffies - rcp->jiffies_stall;
	if (delta < 2 || rcp->cur != rcp->completed) {
		spin_unlock_irqrestore(&rcp->lock, flags);
		return;
	}
	rcp->jiffies_stall = jiffies + RCU_SECONDS_TILL_STALL_RECHECK;
	spin_unlock_irqrestore(&rcp->lock, flags);

	/* OK, time to rat on our buddy... */

	printk(KERN_ERR "INFO: RCU detected CPU stalls:");
	for_each_possible_cpu(cpu) {
		if (cpumask_test_cpu(cpu, to_cpumask(rcp->cpumask)))
			printk(" %d", cpu);
	}
	printk(" (detected by %d, t=%ld jiffies)\n",
	       smp_processor_id(), (long)(jiffies - rcp->gp_start));
}

static void print_cpu_stall(struct rcu_ctrlblk *rcp)
{
	unsigned long flags;

	printk(KERN_ERR "INFO: RCU detected CPU %d stall (t=%lu/%lu jiffies)\n",
			smp_processor_id(), jiffies,
			jiffies - rcp->gp_start);
	dump_stack();
	spin_lock_irqsave(&rcp->lock, flags);
	if ((long)(jiffies - rcp->jiffies_stall) >= 0)
		rcp->jiffies_stall =
			jiffies + RCU_SECONDS_TILL_STALL_RECHECK;
	spin_unlock_irqrestore(&rcp->lock, flags);
	set_need_resched();  /* kick ourselves to get things going. */
}

static void check_cpu_stall(struct rcu_ctrlblk *rcp)
{
	long delta;

	delta = jiffies - rcp->jiffies_stall;
	if (cpumask_test_cpu(smp_processor_id(), to_cpumask(rcp->cpumask)) &&
		delta >= 0) {

		/* We haven't checked in, so go dump stack. */
		print_cpu_stall(rcp);

	} else if (rcp->cur != rcp->completed && delta >= 2) {

		/* They had two seconds to dump stack, so complain. */
		print_other_cpu_stall(rcp);
	}
}

#else /* #ifdef CONFIG_RCU_CPU_STALL_DETECTOR */

static void record_gp_stall_check_time(struct rcu_ctrlblk *rcp)
{
}

static inline void check_cpu_stall(struct rcu_ctrlblk *rcp)
{
}

#endif /* #else #ifdef CONFIG_RCU_CPU_STALL_DETECTOR */

/**
 * call_rcu - Queue an RCU callback for invocation after a grace period.
 * @head: structure to be used for queueing the RCU updates.
 * @func: actual update function to be invoked after the grace period
 *
 * The update function will be invoked some time after a full grace
 * period elapses, in other words after all currently executing RCU
 * read-side critical sections have completed.  RCU read-side critical
 * sections are delimited by rcu_read_lock() and rcu_read_unlock(),
 * and may be nested.
 */
void call_rcu(struct rcu_head *head,
				void (*func)(struct rcu_head *rcu))
{
	unsigned long flags;

	head->func = func;
	local_irq_save(flags);
	__call_rcu(head, &rcu_ctrlblk, &__get_cpu_var(rcu_data));
	local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(call_rcu);

/**
 * call_rcu_bh - Queue an RCU for invocation after a quicker grace period.
 * @head: structure to be used for queueing the RCU updates.
 * @func: actual update function to be invoked after the grace period
 *
 * The update function will be invoked some time after a full grace
 * period elapses, in other words after all currently executing RCU
 * read-side critical sections have completed. call_rcu_bh() assumes
 * that the read-side critical sections end on completion of a softirq
 * handler. This means that read-side critical sections in process
 * context must not be interrupted by softirqs. This interface is to be
 * used when most of the read-side critical sections are in softirq context.
 * RCU read-side critical sections are delimited by rcu_read_lock() and
 * rcu_read_unlock(), * if in interrupt context or rcu_read_lock_bh()
 * and rcu_read_unlock_bh(), if in process context. These may be nested.
 */
void call_rcu_bh(struct rcu_head *head,
				void (*func)(struct rcu_head *rcu))
{
	unsigned long flags;

	head->func = func;
	local_irq_save(flags);
	__call_rcu(head, &rcu_bh_ctrlblk, &__get_cpu_var(rcu_bh_data));
	local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(call_rcu_bh);

/*
 * Return the number of RCU batches processed thus far.  Useful
 * for debug and statistics.
 */
long rcu_batches_completed(void)
{
	return rcu_ctrlblk.completed;
}
EXPORT_SYMBOL_GPL(rcu_batches_completed);

/*
 * Return the number of RCU batches processed thus far.  Useful
 * for debug and statistics.
 */
long rcu_batches_completed_bh(void)
{
	return rcu_bh_ctrlblk.completed;
}
EXPORT_SYMBOL_GPL(rcu_batches_completed_bh);

/* Raises the softirq for processing rcu_callbacks. */
static inline void raise_rcu_softirq(void)
{
	raise_softirq(RCU_SOFTIRQ);
}

/*
 * Invoke the completed RCU callbacks. They are expected to be in
 * a per-cpu list.
 */
static void rcu_do_batch(struct rcu_data *rdp)
{
	unsigned long flags;
	struct rcu_head *next, *list;
	int count = 0;

	list = rdp->donelist;
	while (list) {
		next = list->next;
		prefetch(next);
		list->func(list);
		list = next;
		if (++count >= rdp->blimit)
			break;
	}
	rdp->donelist = list;

	local_irq_save(flags);
	rdp->qlen -= count;
	local_irq_restore(flags);
	if (rdp->blimit == INT_MAX && rdp->qlen <= qlowmark)
		rdp->blimit = blimit;

	if (!rdp->donelist)
		rdp->donetail = &rdp->donelist;
	else
		raise_rcu_softirq();
}

/*
 * Grace period handling:
 * The grace period handling consists out of two steps:
 * - A new grace period is started.
 *   This is done by rcu_start_batch. The start is not broadcasted to
 *   all cpus, they must pick this up by comparing rcp->cur with
 *   rdp->quiescbatch. All cpus are recorded  in the
 *   rcu_ctrlblk.cpumask bitmap.
 * - All cpus must go through a quiescent state.
 *   Since the start of the grace period is not broadcasted, at least two
 *   calls to rcu_check_quiescent_state are required:
 *   The first call just notices that a new grace period is running. The
 *   following calls check if there was a quiescent state since the beginning
 *   of the grace period. If so, it updates rcu_ctrlblk.cpumask. If
 *   the bitmap is empty, then the grace period is completed.
 *   rcu_check_quiescent_state calls rcu_start_batch(0) to start the next grace
 *   period (if necessary).
 */

/*
 * Register a new batch of callbacks, and start it up if there is currently no
 * active batch and the batch to be registered has not already occurred.
 * Caller must hold rcu_ctrlblk.lock.
 */
static void rcu_start_batch(struct rcu_ctrlblk *rcp)
{
	if (rcp->cur != rcp->pending &&
			rcp->completed == rcp->cur) {
		rcp->cur++;
		record_gp_stall_check_time(rcp);

		/*
		 * Accessing nohz_cpu_mask before incrementing rcp->cur needs a
		 * Barrier  Otherwise it can cause tickless idle CPUs to be
		 * included in rcp->cpumask, which will extend graceperiods
		 * unnecessarily.
		 */
		smp_mb();
		cpumask_andnot(to_cpumask(rcp->cpumask),
			       cpu_online_mask, nohz_cpu_mask);

		rcp->signaled = 0;
	}
}

/*
 * cpu went through a quiescent state since the beginning of the grace period.
 * Clear it from the cpu mask and complete the grace period if it was the last
 * cpu. Start another grace period if someone has further entries pending
 */
static void cpu_quiet(int cpu, struct rcu_ctrlblk *rcp)
{
	cpumask_clear_cpu(cpu, to_cpumask(rcp->cpumask));
	if (cpumask_empty(to_cpumask(rcp->cpumask))) {
		/* batch completed ! */
		rcp->completed = rcp->cur;
		rcu_start_batch(rcp);
	}
}

/*
 * Check if the cpu has gone through a quiescent state (say context
 * switch). If so and if it already hasn't done so in this RCU
 * quiescent cycle, then indicate that it has done so.
 */
static void rcu_check_quiescent_state(struct rcu_ctrlblk *rcp,
					struct rcu_data *rdp)
{
	unsigned long flags;

	if (rdp->quiescbatch != rcp->cur) {
		/* start new grace period: */
		rdp->qs_pending = 1;
		rdp->passed_quiesc = 0;
		rdp->quiescbatch = rcp->cur;
		return;
	}

	/* Grace period already completed for this cpu?
	 * qs_pending is checked instead of the actual bitmap to avoid
	 * cacheline trashing.
	 */
	if (!rdp->qs_pending)
		return;

	/*
	 * Was there a quiescent state since the beginning of the grace
	 * period? If no, then exit and wait for the next call.
	 */
	if (!rdp->passed_quiesc)
		return;
	rdp->qs_pending = 0;

	spin_lock_irqsave(&rcp->lock, flags);
	/*
	 * rdp->quiescbatch/rcp->cur and the cpu bitmap can come out of sync
	 * during cpu startup. Ignore the quiescent state.
	 */
	if (likely(rdp->quiescbatch == rcp->cur))
		cpu_quiet(rdp->cpu, rcp);

	spin_unlock_irqrestore(&rcp->lock, flags);
}


#ifdef CONFIG_HOTPLUG_CPU

/* warning! helper for rcu_offline_cpu. do not use elsewhere without reviewing
 * locking requirements, the list it's pulling from has to belong to a cpu
 * which is dead and hence not processing interrupts.
 */
static void rcu_move_batch(struct rcu_data *this_rdp, struct rcu_head *list,
				struct rcu_head **tail, long batch)
{
	unsigned long flags;

	if (list) {
		local_irq_save(flags);
		this_rdp->batch = batch;
		*this_rdp->nxttail[2] = list;
		this_rdp->nxttail[2] = tail;
		local_irq_restore(flags);
	}
}

static void __rcu_offline_cpu(struct rcu_data *this_rdp,
				struct rcu_ctrlblk *rcp, struct rcu_data *rdp)
{
	unsigned long flags;

	/*
	 * if the cpu going offline owns the grace period
	 * we can block indefinitely waiting for it, so flush
	 * it here
	 */
	spin_lock_irqsave(&rcp->lock, flags);
	if (rcp->cur != rcp->completed)
		cpu_quiet(rdp->cpu, rcp);
	rcu_move_batch(this_rdp, rdp->donelist, rdp->donetail, rcp->cur + 1);
	rcu_move_batch(this_rdp, rdp->nxtlist, rdp->nxttail[2], rcp->cur + 1);
	spin_unlock(&rcp->lock);

	this_rdp->qlen += rdp->qlen;
	local_irq_restore(flags);
}

static void rcu_offline_cpu(int cpu)
{
	struct rcu_data *this_rdp = &get_cpu_var(rcu_data);
	struct rcu_data *this_bh_rdp = &get_cpu_var(rcu_bh_data);

	__rcu_offline_cpu(this_rdp, &rcu_ctrlblk,
					&per_cpu(rcu_data, cpu));
	__rcu_offline_cpu(this_bh_rdp, &rcu_bh_ctrlblk,
					&per_cpu(rcu_bh_data, cpu));
	put_cpu_var(rcu_data);
	put_cpu_var(rcu_bh_data);
}

#else

static void rcu_offline_cpu(int cpu)
{
}

#endif

/*
 * This does the RCU processing work from softirq context.
 */
static void __rcu_process_callbacks(struct rcu_ctrlblk *rcp,
					struct rcu_data *rdp)
{
	unsigned long flags;
	long completed_snap;

	if (rdp->nxtlist) {
		local_irq_save(flags);
		completed_snap = ACCESS_ONCE(rcp->completed);

		/*
		 * move the other grace-period-completed entries to
		 * [rdp->nxtlist, *rdp->nxttail[0]) temporarily
		 */
		if (!rcu_batch_before(completed_snap, rdp->batch))
			rdp->nxttail[0] = rdp->nxttail[1] = rdp->nxttail[2];
		else if (!rcu_batch_before(completed_snap, rdp->batch - 1))
			rdp->nxttail[0] = rdp->nxttail[1];

		/*
		 * the grace period for entries in
		 * [rdp->nxtlist, *rdp->nxttail[0]) has completed and
		 * move these entries to donelist
		 */
		if (rdp->nxttail[0] != &rdp->nxtlist) {
			*rdp->donetail = rdp->nxtlist;
			rdp->donetail = rdp->nxttail[0];
			rdp->nxtlist = *rdp->nxttail[0];
			*rdp->donetail = NULL;

			if (rdp->nxttail[1] == rdp->nxttail[0])
				rdp->nxttail[1] = &rdp->nxtlist;
			if (rdp->nxttail[2] == rdp->nxttail[0])
				rdp->nxttail[2] = &rdp->nxtlist;
			rdp->nxttail[0] = &rdp->nxtlist;
		}

		local_irq_restore(flags);

		if (rcu_batch_after(rdp->batch, rcp->pending)) {
			unsigned long flags2;

			/* and start it/schedule start if it's a new batch */
			spin_lock_irqsave(&rcp->lock, flags2);
			if (rcu_batch_after(rdp->batch, rcp->pending)) {
				rcp->pending = rdp->batch;
				rcu_start_batch(rcp);
			}
			spin_unlock_irqrestore(&rcp->lock, flags2);
		}
	}

	rcu_check_quiescent_state(rcp, rdp);
	if (rdp->donelist)
		rcu_do_batch(rdp);
}

static void rcu_process_callbacks(struct softirq_action *unused)
{
	/*
	 * Memory references from any prior RCU read-side critical sections
	 * executed by the interrupted code must be see before any RCU
	 * grace-period manupulations below.
	 */

	smp_mb(); /* See above block comment. */

	__rcu_process_callbacks(&rcu_ctrlblk, &__get_cpu_var(rcu_data));
	__rcu_process_callbacks(&rcu_bh_ctrlblk, &__get_cpu_var(rcu_bh_data));

	/*
	 * Memory references from any later RCU read-side critical sections
	 * executed by the interrupted code must be see after any RCU
	 * grace-period manupulations above.
	 */

	smp_mb(); /* See above block comment. */
}

static int __rcu_pending(struct rcu_ctrlblk *rcp, struct rcu_data *rdp)
{
	/* Check for CPU stalls, if enabled. */
	check_cpu_stall(rcp);

	if (rdp->nxtlist) {
		long completed_snap = ACCESS_ONCE(rcp->completed);

		/*
		 * This cpu has pending rcu entries and the grace period
		 * for them has completed.
		 */
		if (!rcu_batch_before(completed_snap, rdp->batch))
			return 1;
		if (!rcu_batch_before(completed_snap, rdp->batch - 1) &&
				rdp->nxttail[0] != rdp->nxttail[1])
			return 1;
		if (rdp->nxttail[0] != &rdp->nxtlist)
			return 1;

		/*
		 * This cpu has pending rcu entries and the new batch
		 * for then hasn't been started nor scheduled start
		 */
		if (rcu_batch_after(rdp->batch, rcp->pending))
			return 1;
	}

	/* This cpu has finished callbacks to invoke */
	if (rdp->donelist)
		return 1;

	/* The rcu core waits for a quiescent state from the cpu */
	if (rdp->quiescbatch != rcp->cur || rdp->qs_pending)
		return 1;

	/* nothing to do */
	return 0;
}

/*
 * Check to see if there is any immediate RCU-related work to be done
 * by the current CPU, returning 1 if so.  This function is part of the
 * RCU implementation; it is -not- an exported member of the RCU API.
 */
int rcu_pending(int cpu)
{
	return __rcu_pending(&rcu_ctrlblk, &per_cpu(rcu_data, cpu)) ||
		__rcu_pending(&rcu_bh_ctrlblk, &per_cpu(rcu_bh_data, cpu));
}

/*
 * Check to see if any future RCU-related work will need to be done
 * by the current CPU, even if none need be done immediately, returning
 * 1 if so.  This function is part of the RCU implementation; it is -not-
 * an exported member of the RCU API.
 */
int rcu_needs_cpu(int cpu)
{
	struct rcu_data *rdp = &per_cpu(rcu_data, cpu);
	struct rcu_data *rdp_bh = &per_cpu(rcu_bh_data, cpu);

	return !!rdp->nxtlist || !!rdp_bh->nxtlist || rcu_pending(cpu);
}

/*
 * Top-level function driving RCU grace-period detection, normally
 * invoked from the scheduler-clock interrupt.  This function simply
 * increments counters that are read only from softirq by this same
 * CPU, so there are no memory barriers required.
 */
void rcu_check_callbacks(int cpu, int user)
{
	if (user ||
	    (idle_cpu(cpu) && rcu_scheduler_active &&
	     !in_softirq() && hardirq_count() <= (1 << HARDIRQ_SHIFT))) {

		/*
		 * Get here if this CPU took its interrupt from user
		 * mode or from the idle loop, and if this is not a
		 * nested interrupt.  In this case, the CPU is in
		 * a quiescent state, so count it.
		 *
		 * Also do a memory barrier.  This is needed to handle
		 * the case where writes from a preempt-disable section
		 * of code get reordered into schedule() by this CPU's
		 * write buffer.  The memory barrier makes sure that
		 * the rcu_qsctr_inc() and rcu_bh_qsctr_inc() are see
		 * by other CPUs to happen after any such write.
		 */

		smp_mb();  /* See above block comment. */
		rcu_qsctr_inc(cpu);
		rcu_bh_qsctr_inc(cpu);

	} else if (!in_softirq()) {

		/*
		 * Get here if this CPU did not take its interrupt from
		 * softirq, in other words, if it is not interrupting
		 * a rcu_bh read-side critical section.  This is an _bh
		 * critical section, so count it.  The memory barrier
		 * is needed for the same reason as is the above one.
		 */

		smp_mb();  /* See above block comment. */
		rcu_bh_qsctr_inc(cpu);
	}
	raise_rcu_softirq();
}

static void __cpuinit rcu_init_percpu_data(int cpu, struct rcu_ctrlblk *rcp,
						struct rcu_data *rdp)
{
	unsigned long flags;

	spin_lock_irqsave(&rcp->lock, flags);
	memset(rdp, 0, sizeof(*rdp));
	rdp->nxttail[0] = rdp->nxttail[1] = rdp->nxttail[2] = &rdp->nxtlist;
	rdp->donetail = &rdp->donelist;
	rdp->quiescbatch = rcp->completed;
	rdp->qs_pending = 0;
	rdp->cpu = cpu;
	rdp->blimit = blimit;
	spin_unlock_irqrestore(&rcp->lock, flags);
}

static void __cpuinit rcu_online_cpu(int cpu)
{
	struct rcu_data *rdp = &per_cpu(rcu_data, cpu);
	struct rcu_data *bh_rdp = &per_cpu(rcu_bh_data, cpu);

	rcu_init_percpu_data(cpu, &rcu_ctrlblk, rdp);
	rcu_init_percpu_data(cpu, &rcu_bh_ctrlblk, bh_rdp);
	open_softirq(RCU_SOFTIRQ, rcu_process_callbacks);
}

static int __cpuinit rcu_cpu_notify(struct notifier_block *self,
				unsigned long action, void *hcpu)
{
	long cpu = (long)hcpu;

	switch (action) {
	case CPU_UP_PREPARE:
	case CPU_UP_PREPARE_FROZEN:
		rcu_online_cpu(cpu);
		break;
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		rcu_offline_cpu(cpu);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block __cpuinitdata rcu_nb = {
	.notifier_call	= rcu_cpu_notify,
};

/*
 * Initializes rcu mechanism.  Assumed to be called early.
 * That is before local timer(SMP) or jiffie timer (uniproc) is setup.
 * Note that rcu_qsctr and friends are implicitly
 * initialized due to the choice of ``0'' for RCU_CTR_INVALID.
 */
void __init __rcu_init(void)
{
#ifdef CONFIG_RCU_CPU_STALL_DETECTOR
	printk(KERN_INFO "RCU-based detection of stalled CPUs is enabled.\n");
#endif /* #ifdef CONFIG_RCU_CPU_STALL_DETECTOR */
	rcu_cpu_notify(&rcu_nb, CPU_UP_PREPARE,
			(void *)(long)smp_processor_id());
	/* Register notifier for non-boot CPUs */
	register_cpu_notifier(&rcu_nb);
}

module_param(blimit, int, 0);
module_param(qhimark, int, 0);
module_param(qlowmark, int, 0);
