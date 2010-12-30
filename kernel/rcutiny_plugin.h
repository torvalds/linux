/*
 * Read-Copy Update mechanism for mutual exclusion, the Bloatwatch edition
 * Internal non-public definitions that provide either classic
 * or preemptible semantics.
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
 * Copyright (c) 2010 Linaro
 *
 * Author: Paul E. McKenney <paulmck@linux.vnet.ibm.com>
 */

#ifdef CONFIG_TINY_PREEMPT_RCU

#include <linux/delay.h>

/* Global control variables for preemptible RCU. */
struct rcu_preempt_ctrlblk {
	struct rcu_ctrlblk rcb;	/* curtail: ->next ptr of last CB for GP. */
	struct rcu_head **nexttail;
				/* Tasks blocked in a preemptible RCU */
				/*  read-side critical section while an */
				/*  preemptible-RCU grace period is in */
				/*  progress must wait for a later grace */
				/*  period.  This pointer points to the */
				/*  ->next pointer of the last task that */
				/*  must wait for a later grace period, or */
				/*  to &->rcb.rcucblist if there is no */
				/*  such task. */
	struct list_head blkd_tasks;
				/* Tasks blocked in RCU read-side critical */
				/*  section.  Tasks are placed at the head */
				/*  of this list and age towards the tail. */
	struct list_head *gp_tasks;
				/* Pointer to the first task blocking the */
				/*  current grace period, or NULL if there */
				/*  is not such task. */
	struct list_head *exp_tasks;
				/* Pointer to first task blocking the */
				/*  current expedited grace period, or NULL */
				/*  if there is no such task.  If there */
				/*  is no current expedited grace period, */
				/*  then there cannot be any such task. */
	u8 gpnum;		/* Current grace period. */
	u8 gpcpu;		/* Last grace period blocked by the CPU. */
	u8 completed;		/* Last grace period completed. */
				/*  If all three are equal, RCU is idle. */
};

static struct rcu_preempt_ctrlblk rcu_preempt_ctrlblk = {
	.rcb.donetail = &rcu_preempt_ctrlblk.rcb.rcucblist,
	.rcb.curtail = &rcu_preempt_ctrlblk.rcb.rcucblist,
	.nexttail = &rcu_preempt_ctrlblk.rcb.rcucblist,
	.blkd_tasks = LIST_HEAD_INIT(rcu_preempt_ctrlblk.blkd_tasks),
};

static int rcu_preempted_readers_exp(void);
static void rcu_report_exp_done(void);

/*
 * Return true if the CPU has not yet responded to the current grace period.
 */
static int rcu_cpu_blocking_cur_gp(void)
{
	return rcu_preempt_ctrlblk.gpcpu != rcu_preempt_ctrlblk.gpnum;
}

/*
 * Check for a running RCU reader.  Because there is only one CPU,
 * there can be but one running RCU reader at a time.  ;-)
 */
static int rcu_preempt_running_reader(void)
{
	return current->rcu_read_lock_nesting;
}

/*
 * Check for preempted RCU readers blocking any grace period.
 * If the caller needs a reliable answer, it must disable hard irqs.
 */
static int rcu_preempt_blocked_readers_any(void)
{
	return !list_empty(&rcu_preempt_ctrlblk.blkd_tasks);
}

/*
 * Check for preempted RCU readers blocking the current grace period.
 * If the caller needs a reliable answer, it must disable hard irqs.
 */
static int rcu_preempt_blocked_readers_cgp(void)
{
	return rcu_preempt_ctrlblk.gp_tasks != NULL;
}

/*
 * Return true if another preemptible-RCU grace period is needed.
 */
static int rcu_preempt_needs_another_gp(void)
{
	return *rcu_preempt_ctrlblk.rcb.curtail != NULL;
}

/*
 * Return true if a preemptible-RCU grace period is in progress.
 * The caller must disable hardirqs.
 */
static int rcu_preempt_gp_in_progress(void)
{
	return rcu_preempt_ctrlblk.completed != rcu_preempt_ctrlblk.gpnum;
}

/*
 * Record a preemptible-RCU quiescent state for the specified CPU.  Note
 * that this just means that the task currently running on the CPU is
 * in a quiescent state.  There might be any number of tasks blocked
 * while in an RCU read-side critical section.
 *
 * Unlike the other rcu_*_qs() functions, callers to this function
 * must disable irqs in order to protect the assignment to
 * ->rcu_read_unlock_special.
 *
 * Because this is a single-CPU implementation, the only way a grace
 * period can end is if the CPU is in a quiescent state.  The reason is
 * that a blocked preemptible-RCU reader can exit its critical section
 * only if the CPU is running it at the time.  Therefore, when the
 * last task blocking the current grace period exits its RCU read-side
 * critical section, neither the CPU nor blocked tasks will be stopping
 * the current grace period.  (In contrast, SMP implementations
 * might have CPUs running in RCU read-side critical sections that
 * block later grace periods -- but this is not possible given only
 * one CPU.)
 */
static void rcu_preempt_cpu_qs(void)
{
	/* Record both CPU and task as having responded to current GP. */
	rcu_preempt_ctrlblk.gpcpu = rcu_preempt_ctrlblk.gpnum;
	current->rcu_read_unlock_special &= ~RCU_READ_UNLOCK_NEED_QS;

	/*
	 * If there is no GP, or if blocked readers are still blocking GP,
	 * then there is nothing more to do.
	 */
	if (!rcu_preempt_gp_in_progress() || rcu_preempt_blocked_readers_cgp())
		return;

	/* Advance callbacks. */
	rcu_preempt_ctrlblk.completed = rcu_preempt_ctrlblk.gpnum;
	rcu_preempt_ctrlblk.rcb.donetail = rcu_preempt_ctrlblk.rcb.curtail;
	rcu_preempt_ctrlblk.rcb.curtail = rcu_preempt_ctrlblk.nexttail;

	/* If there are no blocked readers, next GP is done instantly. */
	if (!rcu_preempt_blocked_readers_any())
		rcu_preempt_ctrlblk.rcb.donetail = rcu_preempt_ctrlblk.nexttail;

	/* If there are done callbacks, make RCU_SOFTIRQ process them. */
	if (*rcu_preempt_ctrlblk.rcb.donetail != NULL)
		raise_softirq(RCU_SOFTIRQ);
}

/*
 * Start a new RCU grace period if warranted.  Hard irqs must be disabled.
 */
static void rcu_preempt_start_gp(void)
{
	if (!rcu_preempt_gp_in_progress() && rcu_preempt_needs_another_gp()) {

		/* Official start of GP. */
		rcu_preempt_ctrlblk.gpnum++;

		/* Any blocked RCU readers block new GP. */
		if (rcu_preempt_blocked_readers_any())
			rcu_preempt_ctrlblk.gp_tasks =
				rcu_preempt_ctrlblk.blkd_tasks.next;

		/* If there is no running reader, CPU is done with GP. */
		if (!rcu_preempt_running_reader())
			rcu_preempt_cpu_qs();
	}
}

/*
 * We have entered the scheduler, and the current task might soon be
 * context-switched away from.  If this task is in an RCU read-side
 * critical section, we will no longer be able to rely on the CPU to
 * record that fact, so we enqueue the task on the blkd_tasks list.
 * If the task started after the current grace period began, as recorded
 * by ->gpcpu, we enqueue at the beginning of the list.  Otherwise
 * before the element referenced by ->gp_tasks (or at the tail if
 * ->gp_tasks is NULL) and point ->gp_tasks at the newly added element.
 * The task will dequeue itself when it exits the outermost enclosing
 * RCU read-side critical section.  Therefore, the current grace period
 * cannot be permitted to complete until the ->gp_tasks pointer becomes
 * NULL.
 *
 * Caller must disable preemption.
 */
void rcu_preempt_note_context_switch(void)
{
	struct task_struct *t = current;
	unsigned long flags;

	local_irq_save(flags); /* must exclude scheduler_tick(). */
	if (rcu_preempt_running_reader() &&
	    (t->rcu_read_unlock_special & RCU_READ_UNLOCK_BLOCKED) == 0) {

		/* Possibly blocking in an RCU read-side critical section. */
		t->rcu_read_unlock_special |= RCU_READ_UNLOCK_BLOCKED;

		/*
		 * If this CPU has already checked in, then this task
		 * will hold up the next grace period rather than the
		 * current grace period.  Queue the task accordingly.
		 * If the task is queued for the current grace period
		 * (i.e., this CPU has not yet passed through a quiescent
		 * state for the current grace period), then as long
		 * as that task remains queued, the current grace period
		 * cannot end.
		 */
		list_add(&t->rcu_node_entry, &rcu_preempt_ctrlblk.blkd_tasks);
		if (rcu_cpu_blocking_cur_gp())
			rcu_preempt_ctrlblk.gp_tasks = &t->rcu_node_entry;
	}

	/*
	 * Either we were not in an RCU read-side critical section to
	 * begin with, or we have now recorded that critical section
	 * globally.  Either way, we can now note a quiescent state
	 * for this CPU.  Again, if we were in an RCU read-side critical
	 * section, and if that critical section was blocking the current
	 * grace period, then the fact that the task has been enqueued
	 * means that current grace period continues to be blocked.
	 */
	rcu_preempt_cpu_qs();
	local_irq_restore(flags);
}

/*
 * Tiny-preemptible RCU implementation for rcu_read_lock().
 * Just increment ->rcu_read_lock_nesting, shared state will be updated
 * if we block.
 */
void __rcu_read_lock(void)
{
	current->rcu_read_lock_nesting++;
	barrier();  /* needed if we ever invoke rcu_read_lock in rcutiny.c */
}
EXPORT_SYMBOL_GPL(__rcu_read_lock);

/*
 * Handle special cases during rcu_read_unlock(), such as needing to
 * notify RCU core processing or task having blocked during the RCU
 * read-side critical section.
 */
static void rcu_read_unlock_special(struct task_struct *t)
{
	int empty;
	int empty_exp;
	unsigned long flags;
	struct list_head *np;
	int special;

	/*
	 * NMI handlers cannot block and cannot safely manipulate state.
	 * They therefore cannot possibly be special, so just leave.
	 */
	if (in_nmi())
		return;

	local_irq_save(flags);

	/*
	 * If RCU core is waiting for this CPU to exit critical section,
	 * let it know that we have done so.
	 */
	special = t->rcu_read_unlock_special;
	if (special & RCU_READ_UNLOCK_NEED_QS)
		rcu_preempt_cpu_qs();

	/* Hardware IRQ handlers cannot block. */
	if (in_irq()) {
		local_irq_restore(flags);
		return;
	}

	/* Clean up if blocked during RCU read-side critical section. */
	if (special & RCU_READ_UNLOCK_BLOCKED) {
		t->rcu_read_unlock_special &= ~RCU_READ_UNLOCK_BLOCKED;

		/*
		 * Remove this task from the ->blkd_tasks list and adjust
		 * any pointers that might have been referencing it.
		 */
		empty = !rcu_preempt_blocked_readers_cgp();
		empty_exp = rcu_preempt_ctrlblk.exp_tasks == NULL;
		np = t->rcu_node_entry.next;
		if (np == &rcu_preempt_ctrlblk.blkd_tasks)
			np = NULL;
		list_del(&t->rcu_node_entry);
		if (&t->rcu_node_entry == rcu_preempt_ctrlblk.gp_tasks)
			rcu_preempt_ctrlblk.gp_tasks = np;
		if (&t->rcu_node_entry == rcu_preempt_ctrlblk.exp_tasks)
			rcu_preempt_ctrlblk.exp_tasks = np;
		INIT_LIST_HEAD(&t->rcu_node_entry);

		/*
		 * If this was the last task on the current list, and if
		 * we aren't waiting on the CPU, report the quiescent state
		 * and start a new grace period if needed.
		 */
		if (!empty && !rcu_preempt_blocked_readers_cgp()) {
			rcu_preempt_cpu_qs();
			rcu_preempt_start_gp();
		}

		/*
		 * If this was the last task on the expedited lists,
		 * then we need wake up the waiting task.
		 */
		if (!empty_exp && rcu_preempt_ctrlblk.exp_tasks == NULL)
			rcu_report_exp_done();
	}
	local_irq_restore(flags);
}

/*
 * Tiny-preemptible RCU implementation for rcu_read_unlock().
 * Decrement ->rcu_read_lock_nesting.  If the result is zero (outermost
 * rcu_read_unlock()) and ->rcu_read_unlock_special is non-zero, then
 * invoke rcu_read_unlock_special() to clean up after a context switch
 * in an RCU read-side critical section and other special cases.
 */
void __rcu_read_unlock(void)
{
	struct task_struct *t = current;

	barrier();  /* needed if we ever invoke rcu_read_unlock in rcutiny.c */
	--t->rcu_read_lock_nesting;
	barrier();  /* decrement before load of ->rcu_read_unlock_special */
	if (t->rcu_read_lock_nesting == 0 &&
	    unlikely(ACCESS_ONCE(t->rcu_read_unlock_special)))
		rcu_read_unlock_special(t);
#ifdef CONFIG_PROVE_LOCKING
	WARN_ON_ONCE(t->rcu_read_lock_nesting < 0);
#endif /* #ifdef CONFIG_PROVE_LOCKING */
}
EXPORT_SYMBOL_GPL(__rcu_read_unlock);

/*
 * Check for a quiescent state from the current CPU.  When a task blocks,
 * the task is recorded in the rcu_preempt_ctrlblk structure, which is
 * checked elsewhere.  This is called from the scheduling-clock interrupt.
 *
 * Caller must disable hard irqs.
 */
static void rcu_preempt_check_callbacks(void)
{
	struct task_struct *t = current;

	if (rcu_preempt_gp_in_progress() &&
	    (!rcu_preempt_running_reader() ||
	     !rcu_cpu_blocking_cur_gp()))
		rcu_preempt_cpu_qs();
	if (&rcu_preempt_ctrlblk.rcb.rcucblist !=
	    rcu_preempt_ctrlblk.rcb.donetail)
		raise_softirq(RCU_SOFTIRQ);
	if (rcu_preempt_gp_in_progress() &&
	    rcu_cpu_blocking_cur_gp() &&
	    rcu_preempt_running_reader())
		t->rcu_read_unlock_special |= RCU_READ_UNLOCK_NEED_QS;
}

/*
 * TINY_PREEMPT_RCU has an extra callback-list tail pointer to
 * update, so this is invoked from __rcu_process_callbacks() to
 * handle that case.  Of course, it is invoked for all flavors of
 * RCU, but RCU callbacks can appear only on one of the lists, and
 * neither ->nexttail nor ->donetail can possibly be NULL, so there
 * is no need for an explicit check.
 */
static void rcu_preempt_remove_callbacks(struct rcu_ctrlblk *rcp)
{
	if (rcu_preempt_ctrlblk.nexttail == rcp->donetail)
		rcu_preempt_ctrlblk.nexttail = &rcp->rcucblist;
}

/*
 * Process callbacks for preemptible RCU.
 */
static void rcu_preempt_process_callbacks(void)
{
	__rcu_process_callbacks(&rcu_preempt_ctrlblk.rcb);
}

/*
 * Queue a preemptible -RCU callback for invocation after a grace period.
 */
void call_rcu(struct rcu_head *head, void (*func)(struct rcu_head *rcu))
{
	unsigned long flags;

	debug_rcu_head_queue(head);
	head->func = func;
	head->next = NULL;

	local_irq_save(flags);
	*rcu_preempt_ctrlblk.nexttail = head;
	rcu_preempt_ctrlblk.nexttail = &head->next;
	rcu_preempt_start_gp();  /* checks to see if GP needed. */
	local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(call_rcu);

void rcu_barrier(void)
{
	struct rcu_synchronize rcu;

	init_rcu_head_on_stack(&rcu.head);
	init_completion(&rcu.completion);
	/* Will wake me after RCU finished. */
	call_rcu(&rcu.head, wakeme_after_rcu);
	/* Wait for it. */
	wait_for_completion(&rcu.completion);
	destroy_rcu_head_on_stack(&rcu.head);
}
EXPORT_SYMBOL_GPL(rcu_barrier);

/*
 * synchronize_rcu - wait until a grace period has elapsed.
 *
 * Control will return to the caller some time after a full grace
 * period has elapsed, in other words after all currently executing RCU
 * read-side critical sections have completed.  RCU read-side critical
 * sections are delimited by rcu_read_lock() and rcu_read_unlock(),
 * and may be nested.
 */
void synchronize_rcu(void)
{
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	if (!rcu_scheduler_active)
		return;
#endif /* #ifdef CONFIG_DEBUG_LOCK_ALLOC */

	WARN_ON_ONCE(rcu_preempt_running_reader());
	if (!rcu_preempt_blocked_readers_any())
		return;

	/* Once we get past the fastpath checks, same code as rcu_barrier(). */
	rcu_barrier();
}
EXPORT_SYMBOL_GPL(synchronize_rcu);

static DECLARE_WAIT_QUEUE_HEAD(sync_rcu_preempt_exp_wq);
static unsigned long sync_rcu_preempt_exp_count;
static DEFINE_MUTEX(sync_rcu_preempt_exp_mutex);

/*
 * Return non-zero if there are any tasks in RCU read-side critical
 * sections blocking the current preemptible-RCU expedited grace period.
 * If there is no preemptible-RCU expedited grace period currently in
 * progress, returns zero unconditionally.
 */
static int rcu_preempted_readers_exp(void)
{
	return rcu_preempt_ctrlblk.exp_tasks != NULL;
}

/*
 * Report the exit from RCU read-side critical section for the last task
 * that queued itself during or before the current expedited preemptible-RCU
 * grace period.
 */
static void rcu_report_exp_done(void)
{
	wake_up(&sync_rcu_preempt_exp_wq);
}

/*
 * Wait for an rcu-preempt grace period, but expedite it.  The basic idea
 * is to rely in the fact that there is but one CPU, and that it is
 * illegal for a task to invoke synchronize_rcu_expedited() while in a
 * preemptible-RCU read-side critical section.  Therefore, any such
 * critical sections must correspond to blocked tasks, which must therefore
 * be on the ->blkd_tasks list.  So just record the current head of the
 * list in the ->exp_tasks pointer, and wait for all tasks including and
 * after the task pointed to by ->exp_tasks to drain.
 */
void synchronize_rcu_expedited(void)
{
	unsigned long flags;
	struct rcu_preempt_ctrlblk *rpcp = &rcu_preempt_ctrlblk;
	unsigned long snap;

	barrier(); /* ensure prior action seen before grace period. */

	WARN_ON_ONCE(rcu_preempt_running_reader());

	/*
	 * Acquire lock so that there is only one preemptible RCU grace
	 * period in flight.  Of course, if someone does the expedited
	 * grace period for us while we are acquiring the lock, just leave.
	 */
	snap = sync_rcu_preempt_exp_count + 1;
	mutex_lock(&sync_rcu_preempt_exp_mutex);
	if (ULONG_CMP_LT(snap, sync_rcu_preempt_exp_count))
		goto unlock_mb_ret; /* Others did our work for us. */

	local_irq_save(flags);

	/*
	 * All RCU readers have to already be on blkd_tasks because
	 * we cannot legally be executing in an RCU read-side critical
	 * section.
	 */

	/* Snapshot current head of ->blkd_tasks list. */
	rpcp->exp_tasks = rpcp->blkd_tasks.next;
	if (rpcp->exp_tasks == &rpcp->blkd_tasks)
		rpcp->exp_tasks = NULL;
	local_irq_restore(flags);

	/* Wait for tail of ->blkd_tasks list to drain. */
	if (rcu_preempted_readers_exp())
		wait_event(sync_rcu_preempt_exp_wq,
			   !rcu_preempted_readers_exp());

	/* Clean up and exit. */
	barrier(); /* ensure expedited GP seen before counter increment. */
	sync_rcu_preempt_exp_count++;
unlock_mb_ret:
	mutex_unlock(&sync_rcu_preempt_exp_mutex);
	barrier(); /* ensure subsequent action seen after grace period. */
}
EXPORT_SYMBOL_GPL(synchronize_rcu_expedited);

/*
 * Does preemptible RCU need the CPU to stay out of dynticks mode?
 */
int rcu_preempt_needs_cpu(void)
{
	if (!rcu_preempt_running_reader())
		rcu_preempt_cpu_qs();
	return rcu_preempt_ctrlblk.rcb.rcucblist != NULL;
}

/*
 * Check for a task exiting while in a preemptible -RCU read-side
 * critical section, clean up if so.  No need to issue warnings,
 * as debug_check_no_locks_held() already does this if lockdep
 * is enabled.
 */
void exit_rcu(void)
{
	struct task_struct *t = current;

	if (t->rcu_read_lock_nesting == 0)
		return;
	t->rcu_read_lock_nesting = 1;
	rcu_read_unlock();
}

#else /* #ifdef CONFIG_TINY_PREEMPT_RCU */

/*
 * Because preemptible RCU does not exist, it never has any callbacks
 * to check.
 */
static void rcu_preempt_check_callbacks(void)
{
}

/*
 * Because preemptible RCU does not exist, it never has any callbacks
 * to remove.
 */
static void rcu_preempt_remove_callbacks(struct rcu_ctrlblk *rcp)
{
}

/*
 * Because preemptible RCU does not exist, it never has any callbacks
 * to process.
 */
static void rcu_preempt_process_callbacks(void)
{
}

#endif /* #else #ifdef CONFIG_TINY_PREEMPT_RCU */

#ifdef CONFIG_DEBUG_LOCK_ALLOC

#include <linux/kernel_stat.h>

/*
 * During boot, we forgive RCU lockdep issues.  After this function is
 * invoked, we start taking RCU lockdep issues seriously.
 */
void rcu_scheduler_starting(void)
{
	WARN_ON(nr_context_switches() > 0);
	rcu_scheduler_active = 1;
}

#endif /* #ifdef CONFIG_DEBUG_LOCK_ALLOC */
