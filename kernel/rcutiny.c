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

/* Global control variables for rcupdate callback mechanism. */
struct rcu_ctrlblk {
	struct rcu_head *rcucblist;	/* List of pending callbacks (CBs). */
	struct rcu_head **donetail;	/* ->next pointer of last "done" CB. */
	struct rcu_head **curtail;	/* ->next pointer of last CB. */
};

/* Definition for rcupdate control block. */
static struct rcu_ctrlblk rcu_sched_ctrlblk = {
	.donetail	= &rcu_sched_ctrlblk.rcucblist,
	.curtail	= &rcu_sched_ctrlblk.rcucblist,
};

static struct rcu_ctrlblk rcu_bh_ctrlblk = {
	.donetail	= &rcu_bh_ctrlblk.rcucblist,
	.curtail	= &rcu_bh_ctrlblk.rcucblist,
};

#ifdef CONFIG_DEBUG_LOCK_ALLOC
int rcu_scheduler_active __read_mostly;
EXPORT_SYMBOL_GPL(rcu_scheduler_active);
#endif /* #ifdef CONFIG_DEBUG_LOCK_ALLOC */

/* Forward declarations for rcutiny_plugin.h. */
static void __rcu_process_callbacks(struct rcu_ctrlblk *rcp);
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
 * Helper function for rcu_qsctr_inc() and rcu_bh_qsctr_inc().
 * Also disable irqs to avoid confusion due to interrupt handlers
 * invoking call_rcu().
 */
static int rcu_qsctr_help(struct rcu_ctrlblk *rcp)
{
	unsigned long flags;

	local_irq_save(flags);
	if (rcp->rcucblist != NULL &&
	    rcp->donetail != rcp->curtail) {
		rcp->donetail = rcp->curtail;
		local_irq_restore(flags);
		return 1;
	}
	local_irq_restore(flags);

	return 0;
}

/*
 * Record an rcu quiescent state.  And an rcu_bh quiescent state while we
 * are at it, given that any rcu quiescent state is also an rcu_bh
 * quiescent state.  Use "+" instead of "||" to defeat short circuiting.
 */
void rcu_sched_qs(int cpu)
{
	if (rcu_qsctr_help(&rcu_sched_ctrlblk) +
	    rcu_qsctr_help(&rcu_bh_ctrlblk))
		raise_softirq(RCU_SOFTIRQ);
}

/*
 * Record an rcu_bh quiescent state.
 */
void rcu_bh_qs(int cpu)
{
	if (rcu_qsctr_help(&rcu_bh_ctrlblk))
		raise_softirq(RCU_SOFTIRQ);
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
 * Helper function for rcu_process_callbacks() that operates on the
 * specified rcu_ctrlkblk structure.
 */
static void __rcu_process_callbacks(struct rcu_ctrlblk *rcp)
{
	struct rcu_head *next, *list;
	unsigned long flags;

	/* If no RCU callbacks ready to invoke, just return. */
	if (&rcp->rcucblist == rcp->donetail)
		return;

	/* Move the ready-to-invoke callbacks to a local list. */
	local_irq_save(flags);
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
		list->func(list);
		list = next;
	}
}

/*
 * Invoke any callbacks whose grace period has completed.
 */
static void rcu_process_callbacks(struct softirq_action *unused)
{
	__rcu_process_callbacks(&rcu_sched_ctrlblk);
	__rcu_process_callbacks(&rcu_bh_ctrlblk);
	rcu_preempt_process_callbacks();
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

void rcu_barrier_bh(void)
{
	struct rcu_synchronize rcu;

	init_rcu_head_on_stack(&rcu.head);
	init_completion(&rcu.completion);
	/* Will wake me after RCU finished. */
	call_rcu_bh(&rcu.head, wakeme_after_rcu);
	/* Wait for it. */
	wait_for_completion(&rcu.completion);
	destroy_rcu_head_on_stack(&rcu.head);
}
EXPORT_SYMBOL_GPL(rcu_barrier_bh);

void rcu_barrier_sched(void)
{
	struct rcu_synchronize rcu;

	init_rcu_head_on_stack(&rcu.head);
	init_completion(&rcu.completion);
	/* Will wake me after RCU finished. */
	call_rcu_sched(&rcu.head, wakeme_after_rcu);
	/* Wait for it. */
	wait_for_completion(&rcu.completion);
	destroy_rcu_head_on_stack(&rcu.head);
}
EXPORT_SYMBOL_GPL(rcu_barrier_sched);

void __init rcu_init(void)
{
	open_softirq(RCU_SOFTIRQ, rcu_process_callbacks);
}
