/*
 * Read-Copy Update mechanism for mutual exclusion, the Bloatwatch edition.
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
 * Author: Paul E. McKenney <paulmck@linux.vnet.ibm.com>
 *
 * For detailed explanation of Read-Copy Update mechanism see -
 *		Documentation/RCU
 */
#include <linux/moduleparam.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/rcupdate.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/cpu.h>
#include <linux/prefetch.h>

#ifdef CONFIG_RCU_TRACE

#include <trace/events/rcu.h>

#else /* #ifdef CONFIG_RCU_TRACE */

/* No by-default tracing in TINY_RCU: Keep TINY_RCU tiny! */
static void trace_rcu_invoke_kfree_callback(struct rcu_head *rhp,
					    unsigned long offset)
{
}
static void trace_rcu_invoke_callback(struct rcu_head *head)
{
}

#endif /* #else #ifdef CONFIG_RCU_TRACE */

#include "rcu.h"

/* Controls for rcu_kthread() kthread, replacing RCU_SOFTIRQ used previously. */
static struct task_struct *rcu_kthread_task;
static DECLARE_WAIT_QUEUE_HEAD(rcu_kthread_wq);
static unsigned long have_rcu_kthread_work;

/* Forward declarations for rcutiny_plugin.h. */
struct rcu_ctrlblk;
static void invoke_rcu_kthread(void);
static void rcu_process_callbacks(struct rcu_ctrlblk *rcp);
static int rcu_kthread(void *arg);
static void __call_rcu(struct rcu_head *head,
		       void (*func)(struct rcu_head *rcu),
		       struct rcu_ctrlblk *rcp);

#include "rcutiny_plugin.h"

#ifdef CONFIG_NO_HZ

static long rcu_dynticks_nesting = 1;

/*
 * Enter dynticks-idle mode, which is an extended quiescent state
 * if we have fully entered that mode (i.e., if the new value of
 * dynticks_nesting is zero).
 */
void rcu_enter_nohz(void)
{
	if (--rcu_dynticks_nesting == 0)
		rcu_sched_qs(0); /* implies rcu_bh_qsctr_inc(0) */
}

/*
 * Exit dynticks-idle mode, so that we are no longer in an extended
 * quiescent state.
 */
void rcu_exit_nohz(void)
{
	rcu_dynticks_nesting++;
}

#endif /* #ifdef CONFIG_NO_HZ */

/*
 * Helper function for rcu_sched_qs() and rcu_bh_qs().
 * Also irqs are disabled to avoid confusion due to interrupt handlers
 * invoking call_rcu().
 */
static int rcu_qsctr_help(struct rcu_ctrlblk *rcp)
{
	if (rcp->rcucblist != NULL &&
	    rcp->donetail != rcp->curtail) {
		rcp->donetail = rcp->curtail;
		return 1;
	}

	return 0;
}

/*
 * Wake up rcu_kthread() to process callbacks now eligible for invocation
 * or to boost readers.
 */
static void invoke_rcu_kthread(void)
{
	have_rcu_kthread_work = 1;
	wake_up(&rcu_kthread_wq);
}

/*
 * Record an rcu quiescent state.  And an rcu_bh quiescent state while we
 * are at it, given that any rcu quiescent state is also an rcu_bh
 * quiescent state.  Use "+" instead of "||" to defeat short circuiting.
 */
void rcu_sched_qs(int cpu)
{
	unsigned long flags;

	local_irq_save(flags);
	if (rcu_qsctr_help(&rcu_sched_ctrlblk) +
	    rcu_qsctr_help(&rcu_bh_ctrlblk))
		invoke_rcu_kthread();
	local_irq_restore(flags);
}

/*
 * Record an rcu_bh quiescent state.
 */
void rcu_bh_qs(int cpu)
{
	unsigned long flags;

	local_irq_save(flags);
	if (rcu_qsctr_help(&rcu_bh_ctrlblk))
		invoke_rcu_kthread();
	local_irq_restore(flags);
}

/*
 * Check to see if the scheduling-clock interrupt came from an extended
 * quiescent state, and, if so, tell RCU about it.
 */
void rcu_check_callbacks(int cpu, int user)
{
	if (user ||
	    (idle_cpu(cpu) &&
	     !in_softirq() &&
	     hardirq_count() <= (1 << HARDIRQ_SHIFT)))
		rcu_sched_qs(cpu);
	else if (!in_softirq())
		rcu_bh_qs(cpu);
	rcu_preempt_check_callbacks();
}

/*
 * Invoke the RCU callbacks on the specified rcu_ctrlkblk structure
 * whose grace period has elapsed.
 */
static void rcu_process_callbacks(struct rcu_ctrlblk *rcp)
{
	struct rcu_head *next, *list;
	unsigned long flags;
	RCU_TRACE(int cb_count = 0);

	/* If no RCU callbacks ready to invoke, just return. */
	if (&rcp->rcucblist == rcp->donetail) {
		RCU_TRACE(trace_rcu_batch_start(0, -1));
		RCU_TRACE(trace_rcu_batch_end(0));
		return;
	}

	/* Move the ready-to-invoke callbacks to a local list. */
	local_irq_save(flags);
	RCU_TRACE(trace_rcu_batch_start(0, -1));
	list = rcp->rcucblist;
	rcp->rcucblist = *rcp->donetail;
	*rcp->donetail = NULL;
	if (rcp->curtail == rcp->donetail)
		rcp->curtail = &rcp->rcucblist;
	rcu_preempt_remove_callbacks(rcp);
	rcp->donetail = &rcp->rcucblist;
	local_irq_restore(flags);

	/* Invoke the callbacks on the local list. */
	while (list) {
		next = list->next;
		prefetch(next);
		debug_rcu_head_unqueue(list);
		local_bh_disable();
		__rcu_reclaim(list);
		local_bh_enable();
		list = next;
		RCU_TRACE(cb_count++);
	}
	RCU_TRACE(rcu_trace_sub_qlen(rcp, cb_count));
	RCU_TRACE(trace_rcu_batch_end(cb_count));
}

/*
 * This kthread invokes RCU callbacks whose grace periods have
 * elapsed.  It is awakened as needed, and takes the place of the
 * RCU_SOFTIRQ that was used previously for this purpose.
 * This is a kthread, but it is never stopped, at least not until
 * the system goes down.
 */
static int rcu_kthread(void *arg)
{
	unsigned long work;
	unsigned long morework;
	unsigned long flags;

	for (;;) {
		wait_event_interruptible(rcu_kthread_wq,
					 have_rcu_kthread_work != 0);
		morework = rcu_boost();
		local_irq_save(flags);
		work = have_rcu_kthread_work;
		have_rcu_kthread_work = morework;
		local_irq_restore(flags);
		if (work) {
			rcu_process_callbacks(&rcu_sched_ctrlblk);
			rcu_process_callbacks(&rcu_bh_ctrlblk);
			rcu_preempt_process_callbacks();
		}
		schedule_timeout_interruptible(1); /* Leave CPU for others. */
	}

	return 0;  /* Not reached, but needed to shut gcc up. */
}

/*
 * Wait for a grace period to elapse.  But it is illegal to invoke
 * synchronize_sched() from within an RCU read-side critical section.
 * Therefore, any legal call to synchronize_sched() is a quiescent
 * state, and so on a UP system, synchronize_sched() need do nothing.
 * Ditto for synchronize_rcu_bh().  (But Lai Jiangshan points out the
 * benefits of doing might_sleep() to reduce latency.)
 *
 * Cool, huh?  (Due to Josh Triplett.)
 *
 * But we want to make this a static inline later.  The cond_resched()
 * currently makes this problematic.
 */
void synchronize_sched(void)
{
	cond_resched();
}
EXPORT_SYMBOL_GPL(synchronize_sched);

/*
 * Helper function for call_rcu() and call_rcu_bh().
 */
static void __call_rcu(struct rcu_head *head,
		       void (*func)(struct rcu_head *rcu),
		       struct rcu_ctrlblk *rcp)
{
	unsigned long flags;

	debug_rcu_head_queue(head);
	head->func = func;
	head->next = NULL;

	local_irq_save(flags);
	*rcp->curtail = head;
	rcp->curtail = &head->next;
	RCU_TRACE(rcp->qlen++);
	local_irq_restore(flags);
}

/*
 * Post an RCU callback to be invoked after the end of an RCU-sched grace
 * period.  But since we have but one CPU, that would be after any
 * quiescent state.
 */
void call_rcu_sched(struct rcu_head *head, void (*func)(struct rcu_head *rcu))
{
	__call_rcu(head, func, &rcu_sched_ctrlblk);
}
EXPORT_SYMBOL_GPL(call_rcu_sched);

/*
 * Post an RCU bottom-half callback to be invoked after any subsequent
 * quiescent state.
 */
void call_rcu_bh(struct rcu_head *head, void (*func)(struct rcu_head *rcu))
{
	__call_rcu(head, func, &rcu_bh_ctrlblk);
}
EXPORT_SYMBOL_GPL(call_rcu_bh);

/*
 * Spawn the kthread that invokes RCU callbacks.
 */
static int __init rcu_spawn_kthreads(void)
{
	struct sched_param sp;

	rcu_kthread_task = kthread_run(rcu_kthread, NULL, "rcu_kthread");
	sp.sched_priority = RCU_BOOST_PRIO;
	sched_setscheduler_nocheck(rcu_kthread_task, SCHED_FIFO, &sp);
	return 0;
}
early_initcall(rcu_spawn_kthreads);
