// SPDX-License-Identifier: GPL-2.0+
/*
 * Read-Copy Update mechanism for mutual exclusion, the Bloatwatch edition.
 *
 * Copyright IBM Corporation, 2008
 *
 * Author: Paul E. McKenney <paulmck@linux.ibm.com>
 *
 * For detailed explanation of Read-Copy Update mechanism see -
 *		Documentation/RCU
 */
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/rcupdate_wait.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/cpu.h>
#include <linux/prefetch.h>
#include <linux/slab.h>

#include "rcu.h"

/* Global control variables for rcupdate callback mechanism. */
struct rcu_ctrlblk {
	struct rcu_head *rcucblist;	/* List of pending callbacks (CBs). */
	struct rcu_head **donetail;	/* ->next pointer of last "done" CB. */
	struct rcu_head **curtail;	/* ->next pointer of last CB. */
};

/* Definition for rcupdate control block. */
static struct rcu_ctrlblk rcu_ctrlblk = {
	.donetail	= &rcu_ctrlblk.rcucblist,
	.curtail	= &rcu_ctrlblk.rcucblist,
};

void rcu_barrier(void)
{
	wait_rcu_gp(call_rcu);
}
EXPORT_SYMBOL(rcu_barrier);

/* Record an rcu quiescent state.  */
void rcu_qs(void)
{
	unsigned long flags;

	local_irq_save(flags);
	if (rcu_ctrlblk.donetail != rcu_ctrlblk.curtail) {
		rcu_ctrlblk.donetail = rcu_ctrlblk.curtail;
		raise_softirq_irqoff(RCU_SOFTIRQ);
	}
	local_irq_restore(flags);
}

/*
 * Check to see if the scheduling-clock interrupt came from an extended
 * quiescent state, and, if so, tell RCU about it.  This function must
 * be called from hardirq context.  It is normally called from the
 * scheduling-clock interrupt.
 */
void rcu_sched_clock_irq(int user)
{
	if (user) {
		rcu_qs();
	} else if (rcu_ctrlblk.donetail != rcu_ctrlblk.curtail) {
		set_tsk_need_resched(current);
		set_preempt_need_resched();
	}
}

/*
 * Reclaim the specified callback, either by invoking it for non-kfree cases or
 * freeing it directly (for kfree). Return true if kfreeing, false otherwise.
 */
static inline bool rcu_reclaim_tiny(struct rcu_head *head)
{
	rcu_callback_t f;
	unsigned long offset = (unsigned long)head->func;

	rcu_lock_acquire(&rcu_callback_map);
	if (__is_kfree_rcu_offset(offset)) {
		trace_rcu_invoke_kfree_callback("", head, offset);
		kfree((void *)head - offset);
		rcu_lock_release(&rcu_callback_map);
		return true;
	}

	trace_rcu_invoke_callback("", head);
	f = head->func;
	WRITE_ONCE(head->func, (rcu_callback_t)0L);
	f(head);
	rcu_lock_release(&rcu_callback_map);
	return false;
}

/* Invoke the RCU callbacks whose grace period has elapsed.  */
static __latent_entropy void rcu_process_callbacks(struct softirq_action *unused)
{
	struct rcu_head *next, *list;
	unsigned long flags;

	/* Move the ready-to-invoke callbacks to a local list. */
	local_irq_save(flags);
	if (rcu_ctrlblk.donetail == &rcu_ctrlblk.rcucblist) {
		/* No callbacks ready, so just leave. */
		local_irq_restore(flags);
		return;
	}
	list = rcu_ctrlblk.rcucblist;
	rcu_ctrlblk.rcucblist = *rcu_ctrlblk.donetail;
	*rcu_ctrlblk.donetail = NULL;
	if (rcu_ctrlblk.curtail == rcu_ctrlblk.donetail)
		rcu_ctrlblk.curtail = &rcu_ctrlblk.rcucblist;
	rcu_ctrlblk.donetail = &rcu_ctrlblk.rcucblist;
	local_irq_restore(flags);

	/* Invoke the callbacks on the local list. */
	while (list) {
		next = list->next;
		prefetch(next);
		debug_rcu_head_unqueue(list);
		local_bh_disable();
		rcu_reclaim_tiny(list);
		local_bh_enable();
		list = next;
	}
}

/*
 * Wait for a grace period to elapse.  But it is illegal to invoke
 * synchronize_rcu() from within an RCU read-side critical section.
 * Therefore, any legal call to synchronize_rcu() is a quiescent
 * state, and so on a UP system, synchronize_rcu() need do nothing.
 * (But Lai Jiangshan points out the benefits of doing might_sleep()
 * to reduce latency.)
 *
 * Cool, huh?  (Due to Josh Triplett.)
 */
void synchronize_rcu(void)
{
	RCU_LOCKDEP_WARN(lock_is_held(&rcu_bh_lock_map) ||
			 lock_is_held(&rcu_lock_map) ||
			 lock_is_held(&rcu_sched_lock_map),
			 "Illegal synchronize_rcu() in RCU read-side critical section");
}
EXPORT_SYMBOL_GPL(synchronize_rcu);

/*
 * Post an RCU callback to be invoked after the end of an RCU grace
 * period.  But since we have but one CPU, that would be after any
 * quiescent state.
 */
void call_rcu(struct rcu_head *head, rcu_callback_t func)
{
	unsigned long flags;

	debug_rcu_head_queue(head);
	head->func = func;
	head->next = NULL;

	local_irq_save(flags);
	*rcu_ctrlblk.curtail = head;
	rcu_ctrlblk.curtail = &head->next;
	local_irq_restore(flags);

	if (unlikely(is_idle_task(current))) {
		/* force scheduling for rcu_qs() */
		resched_cpu(0);
	}
}
EXPORT_SYMBOL_GPL(call_rcu);

void __init rcu_init(void)
{
	open_softirq(RCU_SOFTIRQ, rcu_process_callbacks);
	rcu_early_boot_tests();
	srcu_init();
}
