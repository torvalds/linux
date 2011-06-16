/*
 * Read-Copy Update mechanism for mutual exclusion (tree-based version)
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
 * Copyright Red Hat, 2009
 * Copyright IBM Corporation, 2009
 *
 * Author: Ingo Molnar <mingo@elte.hu>
 *	   Paul E. McKenney <paulmck@linux.vnet.ibm.com>
 */

#include <linux/delay.h>
#include <linux/stop_machine.h>

/*
 * Check the RCU kernel configuration parameters and print informative
 * messages about anything out of the ordinary.  If you like #ifdef, you
 * will love this function.
 */
static void __init rcu_bootup_announce_oddness(void)
{
#ifdef CONFIG_RCU_TRACE
	printk(KERN_INFO "\tRCU debugfs-based tracing is enabled.\n");
#endif
#if (defined(CONFIG_64BIT) && CONFIG_RCU_FANOUT != 64) || (!defined(CONFIG_64BIT) && CONFIG_RCU_FANOUT != 32)
	printk(KERN_INFO "\tCONFIG_RCU_FANOUT set to non-default value of %d\n",
	       CONFIG_RCU_FANOUT);
#endif
#ifdef CONFIG_RCU_FANOUT_EXACT
	printk(KERN_INFO "\tHierarchical RCU autobalancing is disabled.\n");
#endif
#ifdef CONFIG_RCU_FAST_NO_HZ
	printk(KERN_INFO
	       "\tRCU dyntick-idle grace-period acceleration is enabled.\n");
#endif
#ifdef CONFIG_PROVE_RCU
	printk(KERN_INFO "\tRCU lockdep checking is enabled.\n");
#endif
#ifdef CONFIG_RCU_TORTURE_TEST_RUNNABLE
	printk(KERN_INFO "\tRCU torture testing starts during boot.\n");
#endif
#if defined(CONFIG_TREE_PREEMPT_RCU) && !defined(CONFIG_RCU_CPU_STALL_VERBOSE)
	printk(KERN_INFO "\tVerbose stalled-CPUs detection is disabled.\n");
#endif
#if NUM_RCU_LVL_4 != 0
	printk(KERN_INFO "\tExperimental four-level hierarchy is enabled.\n");
#endif
}

#ifdef CONFIG_TREE_PREEMPT_RCU

struct rcu_state rcu_preempt_state = RCU_STATE_INITIALIZER(rcu_preempt_state);
DEFINE_PER_CPU(struct rcu_data, rcu_preempt_data);
static struct rcu_state *rcu_state = &rcu_preempt_state;

static int rcu_preempted_readers_exp(struct rcu_node *rnp);

/*
 * Tell them what RCU they are running.
 */
static void __init rcu_bootup_announce(void)
{
	printk(KERN_INFO "Preemptible hierarchical RCU implementation.\n");
	rcu_bootup_announce_oddness();
}

/*
 * Return the number of RCU-preempt batches processed thus far
 * for debug and statistics.
 */
long rcu_batches_completed_preempt(void)
{
	return rcu_preempt_state.completed;
}
EXPORT_SYMBOL_GPL(rcu_batches_completed_preempt);

/*
 * Return the number of RCU batches processed thus far for debug & stats.
 */
long rcu_batches_completed(void)
{
	return rcu_batches_completed_preempt();
}
EXPORT_SYMBOL_GPL(rcu_batches_completed);

/*
 * Force a quiescent state for preemptible RCU.
 */
void rcu_force_quiescent_state(void)
{
	force_quiescent_state(&rcu_preempt_state, 0);
}
EXPORT_SYMBOL_GPL(rcu_force_quiescent_state);

/*
 * Record a preemptible-RCU quiescent state for the specified CPU.  Note
 * that this just means that the task currently running on the CPU is
 * not in a quiescent state.  There might be any number of tasks blocked
 * while in an RCU read-side critical section.
 *
 * Unlike the other rcu_*_qs() functions, callers to this function
 * must disable irqs in order to protect the assignment to
 * ->rcu_read_unlock_special.
 */
static void rcu_preempt_qs(int cpu)
{
	struct rcu_data *rdp = &per_cpu(rcu_preempt_data, cpu);

	rdp->passed_quiesc_completed = rdp->gpnum - 1;
	barrier();
	rdp->passed_quiesc = 1;
	current->rcu_read_unlock_special &= ~RCU_READ_UNLOCK_NEED_QS;
}

/*
 * We have entered the scheduler, and the current task might soon be
 * context-switched away from.  If this task is in an RCU read-side
 * critical section, we will no longer be able to rely on the CPU to
 * record that fact, so we enqueue the task on the blkd_tasks list.
 * The task will dequeue itself when it exits the outermost enclosing
 * RCU read-side critical section.  Therefore, the current grace period
 * cannot be permitted to complete until the blkd_tasks list entries
 * predating the current grace period drain, in other words, until
 * rnp->gp_tasks becomes NULL.
 *
 * Caller must disable preemption.
 */
static void rcu_preempt_note_context_switch(int cpu)
{
	struct task_struct *t = current;
	unsigned long flags;
	struct rcu_data *rdp;
	struct rcu_node *rnp;

	if (t->rcu_read_lock_nesting &&
	    (t->rcu_read_unlock_special & RCU_READ_UNLOCK_BLOCKED) == 0) {

		/* Possibly blocking in an RCU read-side critical section. */
		rdp = per_cpu_ptr(rcu_preempt_state.rda, cpu);
		rnp = rdp->mynode;
		raw_spin_lock_irqsave(&rnp->lock, flags);
		t->rcu_read_unlock_special |= RCU_READ_UNLOCK_BLOCKED;
		t->rcu_blocked_node = rnp;

		/*
		 * If this CPU has already checked in, then this task
		 * will hold up the next grace period rather than the
		 * current grace period.  Queue the task accordingly.
		 * If the task is queued for the current grace period
		 * (i.e., this CPU has not yet passed through a quiescent
		 * state for the current grace period), then as long
		 * as that task remains queued, the current grace period
		 * cannot end.  Note that there is some uncertainty as
		 * to exactly when the current grace period started.
		 * We take a conservative approach, which can result
		 * in unnecessarily waiting on tasks that started very
		 * slightly after the current grace period began.  C'est
		 * la vie!!!
		 *
		 * But first, note that the current CPU must still be
		 * on line!
		 */
		WARN_ON_ONCE((rdp->grpmask & rnp->qsmaskinit) == 0);
		WARN_ON_ONCE(!list_empty(&t->rcu_node_entry));
		if ((rnp->qsmask & rdp->grpmask) && rnp->gp_tasks != NULL) {
			list_add(&t->rcu_node_entry, rnp->gp_tasks->prev);
			rnp->gp_tasks = &t->rcu_node_entry;
#ifdef CONFIG_RCU_BOOST
			if (rnp->boost_tasks != NULL)
				rnp->boost_tasks = rnp->gp_tasks;
#endif /* #ifdef CONFIG_RCU_BOOST */
		} else {
			list_add(&t->rcu_node_entry, &rnp->blkd_tasks);
			if (rnp->qsmask & rdp->grpmask)
				rnp->gp_tasks = &t->rcu_node_entry;
		}
		raw_spin_unlock_irqrestore(&rnp->lock, flags);
	}

	/*
	 * Either we were not in an RCU read-side critical section to
	 * begin with, or we have now recorded that critical section
	 * globally.  Either way, we can now note a quiescent state
	 * for this CPU.  Again, if we were in an RCU read-side critical
	 * section, and if that critical section was blocking the current
	 * grace period, then the fact that the task has been enqueued
	 * means that we continue to block the current grace period.
	 */
	local_irq_save(flags);
	rcu_preempt_qs(cpu);
	local_irq_restore(flags);
}

/*
 * Tree-preemptible RCU implementation for rcu_read_lock().
 * Just increment ->rcu_read_lock_nesting, shared state will be updated
 * if we block.
 */
void __rcu_read_lock(void)
{
	current->rcu_read_lock_nesting++;
	barrier();  /* needed if we ever invoke rcu_read_lock in rcutree.c */
}
EXPORT_SYMBOL_GPL(__rcu_read_lock);

/*
 * Check for preempted RCU readers blocking the current grace period
 * for the specified rcu_node structure.  If the caller needs a reliable
 * answer, it must hold the rcu_node's ->lock.
 */
static int rcu_preempt_blocked_readers_cgp(struct rcu_node *rnp)
{
	return rnp->gp_tasks != NULL;
}

/*
 * Record a quiescent state for all tasks that were previously queued
 * on the specified rcu_node structure and that were blocking the current
 * RCU grace period.  The caller must hold the specified rnp->lock with
 * irqs disabled, and this lock is released upon return, but irqs remain
 * disabled.
 */
static void rcu_report_unblock_qs_rnp(struct rcu_node *rnp, unsigned long flags)
	__releases(rnp->lock)
{
	unsigned long mask;
	struct rcu_node *rnp_p;

	if (rnp->qsmask != 0 || rcu_preempt_blocked_readers_cgp(rnp)) {
		raw_spin_unlock_irqrestore(&rnp->lock, flags);
		return;  /* Still need more quiescent states! */
	}

	rnp_p = rnp->parent;
	if (rnp_p == NULL) {
		/*
		 * Either there is only one rcu_node in the tree,
		 * or tasks were kicked up to root rcu_node due to
		 * CPUs going offline.
		 */
		rcu_report_qs_rsp(&rcu_preempt_state, flags);
		return;
	}

	/* Report up the rest of the hierarchy. */
	mask = rnp->grpmask;
	raw_spin_unlock(&rnp->lock);	/* irqs remain disabled. */
	raw_spin_lock(&rnp_p->lock);	/* irqs already disabled. */
	rcu_report_qs_rnp(mask, &rcu_preempt_state, rnp_p, flags);
}

/*
 * Advance a ->blkd_tasks-list pointer to the next entry, instead
 * returning NULL if at the end of the list.
 */
static struct list_head *rcu_next_node_entry(struct task_struct *t,
					     struct rcu_node *rnp)
{
	struct list_head *np;

	np = t->rcu_node_entry.next;
	if (np == &rnp->blkd_tasks)
		np = NULL;
	return np;
}

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
	struct rcu_node *rnp;
	int special;

	/* NMI handlers cannot block and cannot safely manipulate state. */
	if (in_nmi())
		return;

	local_irq_save(flags);

	/*
	 * If RCU core is waiting for this CPU to exit critical section,
	 * let it know that we have done so.
	 */
	special = t->rcu_read_unlock_special;
	if (special & RCU_READ_UNLOCK_NEED_QS) {
		rcu_preempt_qs(smp_processor_id());
	}

	/* Hardware IRQ handlers cannot block. */
	if (in_irq()) {
		local_irq_restore(flags);
		return;
	}

	/* Clean up if blocked during RCU read-side critical section. */
	if (special & RCU_READ_UNLOCK_BLOCKED) {
		t->rcu_read_unlock_special &= ~RCU_READ_UNLOCK_BLOCKED;

		/*
		 * Remove this task from the list it blocked on.  The
		 * task can migrate while we acquire the lock, but at
		 * most one time.  So at most two passes through loop.
		 */
		for (;;) {
			rnp = t->rcu_blocked_node;
			raw_spin_lock(&rnp->lock);  /* irqs already disabled. */
			if (rnp == t->rcu_blocked_node)
				break;
			raw_spin_unlock(&rnp->lock); /* irqs remain disabled. */
		}
		empty = !rcu_preempt_blocked_readers_cgp(rnp);
		empty_exp = !rcu_preempted_readers_exp(rnp);
		smp_mb(); /* ensure expedited fastpath sees end of RCU c-s. */
		np = rcu_next_node_entry(t, rnp);
		list_del_init(&t->rcu_node_entry);
		if (&t->rcu_node_entry == rnp->gp_tasks)
			rnp->gp_tasks = np;
		if (&t->rcu_node_entry == rnp->exp_tasks)
			rnp->exp_tasks = np;
#ifdef CONFIG_RCU_BOOST
		if (&t->rcu_node_entry == rnp->boost_tasks)
			rnp->boost_tasks = np;
#endif /* #ifdef CONFIG_RCU_BOOST */
		t->rcu_blocked_node = NULL;

		/*
		 * If this was the last task on the current list, and if
		 * we aren't waiting on any CPUs, report the quiescent state.
		 * Note that rcu_report_unblock_qs_rnp() releases rnp->lock.
		 */
		if (empty)
			raw_spin_unlock_irqrestore(&rnp->lock, flags);
		else
			rcu_report_unblock_qs_rnp(rnp, flags);

#ifdef CONFIG_RCU_BOOST
		/* Unboost if we were boosted. */
		if (special & RCU_READ_UNLOCK_BOOSTED) {
			t->rcu_read_unlock_special &= ~RCU_READ_UNLOCK_BOOSTED;
			rt_mutex_unlock(t->rcu_boost_mutex);
			t->rcu_boost_mutex = NULL;
		}
#endif /* #ifdef CONFIG_RCU_BOOST */

		/*
		 * If this was the last task on the expedited lists,
		 * then we need to report up the rcu_node hierarchy.
		 */
		if (!empty_exp && !rcu_preempted_readers_exp(rnp))
			rcu_report_exp_rnp(&rcu_preempt_state, rnp);
	} else {
		local_irq_restore(flags);
	}
}

/*
 * Tree-preemptible RCU implementation for rcu_read_unlock().
 * Decrement ->rcu_read_lock_nesting.  If the result is zero (outermost
 * rcu_read_unlock()) and ->rcu_read_unlock_special is non-zero, then
 * invoke rcu_read_unlock_special() to clean up after a context switch
 * in an RCU read-side critical section and other special cases.
 */
void __rcu_read_unlock(void)
{
	struct task_struct *t = current;

	barrier();  /* needed if we ever invoke rcu_read_unlock in rcutree.c */
	--t->rcu_read_lock_nesting;
	barrier();  /* decrement before load of ->rcu_read_unlock_special */
	if (t->rcu_read_lock_nesting == 0 &&
	    unlikely(ACCESS_ONCE(t->rcu_read_unlock_special)))
		rcu_read_unlock_special(t);
#ifdef CONFIG_PROVE_LOCKING
	WARN_ON_ONCE(ACCESS_ONCE(t->rcu_read_lock_nesting) < 0);
#endif /* #ifdef CONFIG_PROVE_LOCKING */
}
EXPORT_SYMBOL_GPL(__rcu_read_unlock);

#ifdef CONFIG_RCU_CPU_STALL_VERBOSE

/*
 * Dump detailed information for all tasks blocking the current RCU
 * grace period on the specified rcu_node structure.
 */
static void rcu_print_detail_task_stall_rnp(struct rcu_node *rnp)
{
	unsigned long flags;
	struct task_struct *t;

	if (!rcu_preempt_blocked_readers_cgp(rnp))
		return;
	raw_spin_lock_irqsave(&rnp->lock, flags);
	t = list_entry(rnp->gp_tasks,
		       struct task_struct, rcu_node_entry);
	list_for_each_entry_continue(t, &rnp->blkd_tasks, rcu_node_entry)
		sched_show_task(t);
	raw_spin_unlock_irqrestore(&rnp->lock, flags);
}

/*
 * Dump detailed information for all tasks blocking the current RCU
 * grace period.
 */
static void rcu_print_detail_task_stall(struct rcu_state *rsp)
{
	struct rcu_node *rnp = rcu_get_root(rsp);

	rcu_print_detail_task_stall_rnp(rnp);
	rcu_for_each_leaf_node(rsp, rnp)
		rcu_print_detail_task_stall_rnp(rnp);
}

#else /* #ifdef CONFIG_RCU_CPU_STALL_VERBOSE */

static void rcu_print_detail_task_stall(struct rcu_state *rsp)
{
}

#endif /* #else #ifdef CONFIG_RCU_CPU_STALL_VERBOSE */

/*
 * Scan the current list of tasks blocked within RCU read-side critical
 * sections, printing out the tid of each.
 */
static void rcu_print_task_stall(struct rcu_node *rnp)
{
	struct task_struct *t;

	if (!rcu_preempt_blocked_readers_cgp(rnp))
		return;
	t = list_entry(rnp->gp_tasks,
		       struct task_struct, rcu_node_entry);
	list_for_each_entry_continue(t, &rnp->blkd_tasks, rcu_node_entry)
		printk(" P%d", t->pid);
}

/*
 * Suppress preemptible RCU's CPU stall warnings by pushing the
 * time of the next stall-warning message comfortably far into the
 * future.
 */
static void rcu_preempt_stall_reset(void)
{
	rcu_preempt_state.jiffies_stall = jiffies + ULONG_MAX / 2;
}

/*
 * Check that the list of blocked tasks for the newly completed grace
 * period is in fact empty.  It is a serious bug to complete a grace
 * period that still has RCU readers blocked!  This function must be
 * invoked -before- updating this rnp's ->gpnum, and the rnp's ->lock
 * must be held by the caller.
 *
 * Also, if there are blocked tasks on the list, they automatically
 * block the newly created grace period, so set up ->gp_tasks accordingly.
 */
static void rcu_preempt_check_blocked_tasks(struct rcu_node *rnp)
{
	WARN_ON_ONCE(rcu_preempt_blocked_readers_cgp(rnp));
	if (!list_empty(&rnp->blkd_tasks))
		rnp->gp_tasks = rnp->blkd_tasks.next;
	WARN_ON_ONCE(rnp->qsmask);
}

#ifdef CONFIG_HOTPLUG_CPU

/*
 * Handle tasklist migration for case in which all CPUs covered by the
 * specified rcu_node have gone offline.  Move them up to the root
 * rcu_node.  The reason for not just moving them to the immediate
 * parent is to remove the need for rcu_read_unlock_special() to
 * make more than two attempts to acquire the target rcu_node's lock.
 * Returns true if there were tasks blocking the current RCU grace
 * period.
 *
 * Returns 1 if there was previously a task blocking the current grace
 * period on the specified rcu_node structure.
 *
 * The caller must hold rnp->lock with irqs disabled.
 */
static int rcu_preempt_offline_tasks(struct rcu_state *rsp,
				     struct rcu_node *rnp,
				     struct rcu_data *rdp)
{
	struct list_head *lp;
	struct list_head *lp_root;
	int retval = 0;
	struct rcu_node *rnp_root = rcu_get_root(rsp);
	struct task_struct *t;

	if (rnp == rnp_root) {
		WARN_ONCE(1, "Last CPU thought to be offlined?");
		return 0;  /* Shouldn't happen: at least one CPU online. */
	}

	/* If we are on an internal node, complain bitterly. */
	WARN_ON_ONCE(rnp != rdp->mynode);

	/*
	 * Move tasks up to root rcu_node.  Don't try to get fancy for
	 * this corner-case operation -- just put this node's tasks
	 * at the head of the root node's list, and update the root node's
	 * ->gp_tasks and ->exp_tasks pointers to those of this node's,
	 * if non-NULL.  This might result in waiting for more tasks than
	 * absolutely necessary, but this is a good performance/complexity
	 * tradeoff.
	 */
	if (rcu_preempt_blocked_readers_cgp(rnp))
		retval |= RCU_OFL_TASKS_NORM_GP;
	if (rcu_preempted_readers_exp(rnp))
		retval |= RCU_OFL_TASKS_EXP_GP;
	lp = &rnp->blkd_tasks;
	lp_root = &rnp_root->blkd_tasks;
	while (!list_empty(lp)) {
		t = list_entry(lp->next, typeof(*t), rcu_node_entry);
		raw_spin_lock(&rnp_root->lock); /* irqs already disabled */
		list_del(&t->rcu_node_entry);
		t->rcu_blocked_node = rnp_root;
		list_add(&t->rcu_node_entry, lp_root);
		if (&t->rcu_node_entry == rnp->gp_tasks)
			rnp_root->gp_tasks = rnp->gp_tasks;
		if (&t->rcu_node_entry == rnp->exp_tasks)
			rnp_root->exp_tasks = rnp->exp_tasks;
#ifdef CONFIG_RCU_BOOST
		if (&t->rcu_node_entry == rnp->boost_tasks)
			rnp_root->boost_tasks = rnp->boost_tasks;
#endif /* #ifdef CONFIG_RCU_BOOST */
		raw_spin_unlock(&rnp_root->lock); /* irqs still disabled */
	}

#ifdef CONFIG_RCU_BOOST
	/* In case root is being boosted and leaf is not. */
	raw_spin_lock(&rnp_root->lock); /* irqs already disabled */
	if (rnp_root->boost_tasks != NULL &&
	    rnp_root->boost_tasks != rnp_root->gp_tasks)
		rnp_root->boost_tasks = rnp_root->gp_tasks;
	raw_spin_unlock(&rnp_root->lock); /* irqs still disabled */
#endif /* #ifdef CONFIG_RCU_BOOST */

	rnp->gp_tasks = NULL;
	rnp->exp_tasks = NULL;
	return retval;
}

/*
 * Do CPU-offline processing for preemptible RCU.
 */
static void rcu_preempt_offline_cpu(int cpu)
{
	__rcu_offline_cpu(cpu, &rcu_preempt_state);
}

#endif /* #ifdef CONFIG_HOTPLUG_CPU */

/*
 * Check for a quiescent state from the current CPU.  When a task blocks,
 * the task is recorded in the corresponding CPU's rcu_node structure,
 * which is checked elsewhere.
 *
 * Caller must disable hard irqs.
 */
static void rcu_preempt_check_callbacks(int cpu)
{
	struct task_struct *t = current;

	if (t->rcu_read_lock_nesting == 0) {
		rcu_preempt_qs(cpu);
		return;
	}
	if (per_cpu(rcu_preempt_data, cpu).qs_pending)
		t->rcu_read_unlock_special |= RCU_READ_UNLOCK_NEED_QS;
}

/*
 * Process callbacks for preemptible RCU.
 */
static void rcu_preempt_process_callbacks(void)
{
	__rcu_process_callbacks(&rcu_preempt_state,
				&__get_cpu_var(rcu_preempt_data));
}

#ifdef CONFIG_RCU_BOOST

static void rcu_preempt_do_callbacks(void)
{
	rcu_do_batch(&rcu_preempt_state, &__get_cpu_var(rcu_preempt_data));
}

#endif /* #ifdef CONFIG_RCU_BOOST */

/*
 * Queue a preemptible-RCU callback for invocation after a grace period.
 */
void call_rcu(struct rcu_head *head, void (*func)(struct rcu_head *rcu))
{
	__call_rcu(head, func, &rcu_preempt_state);
}
EXPORT_SYMBOL_GPL(call_rcu);

/**
 * synchronize_rcu - wait until a grace period has elapsed.
 *
 * Control will return to the caller some time after a full grace
 * period has elapsed, in other words after all currently executing RCU
 * read-side critical sections have completed.  Note, however, that
 * upon return from synchronize_rcu(), the caller might well be executing
 * concurrently with new RCU read-side critical sections that began while
 * synchronize_rcu() was waiting.  RCU read-side critical sections are
 * delimited by rcu_read_lock() and rcu_read_unlock(), and may be nested.
 */
void synchronize_rcu(void)
{
	struct rcu_synchronize rcu;

	if (!rcu_scheduler_active)
		return;

	init_rcu_head_on_stack(&rcu.head);
	init_completion(&rcu.completion);
	/* Will wake me after RCU finished. */
	call_rcu(&rcu.head, wakeme_after_rcu);
	/* Wait for it. */
	wait_for_completion(&rcu.completion);
	destroy_rcu_head_on_stack(&rcu.head);
}
EXPORT_SYMBOL_GPL(synchronize_rcu);

static DECLARE_WAIT_QUEUE_HEAD(sync_rcu_preempt_exp_wq);
static long sync_rcu_preempt_exp_count;
static DEFINE_MUTEX(sync_rcu_preempt_exp_mutex);

/*
 * Return non-zero if there are any tasks in RCU read-side critical
 * sections blocking the current preemptible-RCU expedited grace period.
 * If there is no preemptible-RCU expedited grace period currently in
 * progress, returns zero unconditionally.
 */
static int rcu_preempted_readers_exp(struct rcu_node *rnp)
{
	return rnp->exp_tasks != NULL;
}

/*
 * return non-zero if there is no RCU expedited grace period in progress
 * for the specified rcu_node structure, in other words, if all CPUs and
 * tasks covered by the specified rcu_node structure have done their bit
 * for the current expedited grace period.  Works only for preemptible
 * RCU -- other RCU implementation use other means.
 *
 * Caller must hold sync_rcu_preempt_exp_mutex.
 */
static int sync_rcu_preempt_exp_done(struct rcu_node *rnp)
{
	return !rcu_preempted_readers_exp(rnp) &&
	       ACCESS_ONCE(rnp->expmask) == 0;
}

/*
 * Report the exit from RCU read-side critical section for the last task
 * that queued itself during or before the current expedited preemptible-RCU
 * grace period.  This event is reported either to the rcu_node structure on
 * which the task was queued or to one of that rcu_node structure's ancestors,
 * recursively up the tree.  (Calm down, calm down, we do the recursion
 * iteratively!)
 *
 * Caller must hold sync_rcu_preempt_exp_mutex.
 */
static void rcu_report_exp_rnp(struct rcu_state *rsp, struct rcu_node *rnp)
{
	unsigned long flags;
	unsigned long mask;

	raw_spin_lock_irqsave(&rnp->lock, flags);
	for (;;) {
		if (!sync_rcu_preempt_exp_done(rnp))
			break;
		if (rnp->parent == NULL) {
			wake_up(&sync_rcu_preempt_exp_wq);
			break;
		}
		mask = rnp->grpmask;
		raw_spin_unlock(&rnp->lock); /* irqs remain disabled */
		rnp = rnp->parent;
		raw_spin_lock(&rnp->lock); /* irqs already disabled */
		rnp->expmask &= ~mask;
	}
	raw_spin_unlock_irqrestore(&rnp->lock, flags);
}

/*
 * Snapshot the tasks blocking the newly started preemptible-RCU expedited
 * grace period for the specified rcu_node structure.  If there are no such
 * tasks, report it up the rcu_node hierarchy.
 *
 * Caller must hold sync_rcu_preempt_exp_mutex and rsp->onofflock.
 */
static void
sync_rcu_preempt_exp_init(struct rcu_state *rsp, struct rcu_node *rnp)
{
	unsigned long flags;
	int must_wait = 0;

	raw_spin_lock_irqsave(&rnp->lock, flags);
	if (list_empty(&rnp->blkd_tasks))
		raw_spin_unlock_irqrestore(&rnp->lock, flags);
	else {
		rnp->exp_tasks = rnp->blkd_tasks.next;
		rcu_initiate_boost(rnp, flags);  /* releases rnp->lock */
		must_wait = 1;
	}
	if (!must_wait)
		rcu_report_exp_rnp(rsp, rnp);
}

/*
 * Wait for an rcu-preempt grace period, but expedite it.  The basic idea
 * is to invoke synchronize_sched_expedited() to push all the tasks to
 * the ->blkd_tasks lists and wait for this list to drain.
 */
void synchronize_rcu_expedited(void)
{
	unsigned long flags;
	struct rcu_node *rnp;
	struct rcu_state *rsp = &rcu_preempt_state;
	long snap;
	int trycount = 0;

	smp_mb(); /* Caller's modifications seen first by other CPUs. */
	snap = ACCESS_ONCE(sync_rcu_preempt_exp_count) + 1;
	smp_mb(); /* Above access cannot bleed into critical section. */

	/*
	 * Acquire lock, falling back to synchronize_rcu() if too many
	 * lock-acquisition failures.  Of course, if someone does the
	 * expedited grace period for us, just leave.
	 */
	while (!mutex_trylock(&sync_rcu_preempt_exp_mutex)) {
		if (trycount++ < 10)
			udelay(trycount * num_online_cpus());
		else {
			synchronize_rcu();
			return;
		}
		if ((ACCESS_ONCE(sync_rcu_preempt_exp_count) - snap) > 0)
			goto mb_ret; /* Others did our work for us. */
	}
	if ((ACCESS_ONCE(sync_rcu_preempt_exp_count) - snap) > 0)
		goto unlock_mb_ret; /* Others did our work for us. */

	/* force all RCU readers onto ->blkd_tasks lists. */
	synchronize_sched_expedited();

	raw_spin_lock_irqsave(&rsp->onofflock, flags);

	/* Initialize ->expmask for all non-leaf rcu_node structures. */
	rcu_for_each_nonleaf_node_breadth_first(rsp, rnp) {
		raw_spin_lock(&rnp->lock); /* irqs already disabled. */
		rnp->expmask = rnp->qsmaskinit;
		raw_spin_unlock(&rnp->lock); /* irqs remain disabled. */
	}

	/* Snapshot current state of ->blkd_tasks lists. */
	rcu_for_each_leaf_node(rsp, rnp)
		sync_rcu_preempt_exp_init(rsp, rnp);
	if (NUM_RCU_NODES > 1)
		sync_rcu_preempt_exp_init(rsp, rcu_get_root(rsp));

	raw_spin_unlock_irqrestore(&rsp->onofflock, flags);

	/* Wait for snapshotted ->blkd_tasks lists to drain. */
	rnp = rcu_get_root(rsp);
	wait_event(sync_rcu_preempt_exp_wq,
		   sync_rcu_preempt_exp_done(rnp));

	/* Clean up and exit. */
	smp_mb(); /* ensure expedited GP seen before counter increment. */
	ACCESS_ONCE(sync_rcu_preempt_exp_count)++;
unlock_mb_ret:
	mutex_unlock(&sync_rcu_preempt_exp_mutex);
mb_ret:
	smp_mb(); /* ensure subsequent action seen after grace period. */
}
EXPORT_SYMBOL_GPL(synchronize_rcu_expedited);

/*
 * Check to see if there is any immediate preemptible-RCU-related work
 * to be done.
 */
static int rcu_preempt_pending(int cpu)
{
	return __rcu_pending(&rcu_preempt_state,
			     &per_cpu(rcu_preempt_data, cpu));
}

/*
 * Does preemptible RCU need the CPU to stay out of dynticks mode?
 */
static int rcu_preempt_needs_cpu(int cpu)
{
	return !!per_cpu(rcu_preempt_data, cpu).nxtlist;
}

/**
 * rcu_barrier - Wait until all in-flight call_rcu() callbacks complete.
 */
void rcu_barrier(void)
{
	_rcu_barrier(&rcu_preempt_state, call_rcu);
}
EXPORT_SYMBOL_GPL(rcu_barrier);

/*
 * Initialize preemptible RCU's per-CPU data.
 */
static void __cpuinit rcu_preempt_init_percpu_data(int cpu)
{
	rcu_init_percpu_data(cpu, &rcu_preempt_state, 1);
}

/*
 * Move preemptible RCU's callbacks from dying CPU to other online CPU.
 */
static void rcu_preempt_send_cbs_to_online(void)
{
	rcu_send_cbs_to_online(&rcu_preempt_state);
}

/*
 * Initialize preemptible RCU's state structures.
 */
static void __init __rcu_init_preempt(void)
{
	rcu_init_one(&rcu_preempt_state, &rcu_preempt_data);
}

/*
 * Check for a task exiting while in a preemptible-RCU read-side
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
	__rcu_read_unlock();
}

#else /* #ifdef CONFIG_TREE_PREEMPT_RCU */

static struct rcu_state *rcu_state = &rcu_sched_state;

/*
 * Tell them what RCU they are running.
 */
static void __init rcu_bootup_announce(void)
{
	printk(KERN_INFO "Hierarchical RCU implementation.\n");
	rcu_bootup_announce_oddness();
}

/*
 * Return the number of RCU batches processed thus far for debug & stats.
 */
long rcu_batches_completed(void)
{
	return rcu_batches_completed_sched();
}
EXPORT_SYMBOL_GPL(rcu_batches_completed);

/*
 * Force a quiescent state for RCU, which, because there is no preemptible
 * RCU, becomes the same as rcu-sched.
 */
void rcu_force_quiescent_state(void)
{
	rcu_sched_force_quiescent_state();
}
EXPORT_SYMBOL_GPL(rcu_force_quiescent_state);

/*
 * Because preemptible RCU does not exist, we never have to check for
 * CPUs being in quiescent states.
 */
static void rcu_preempt_note_context_switch(int cpu)
{
}

/*
 * Because preemptible RCU does not exist, there are never any preempted
 * RCU readers.
 */
static int rcu_preempt_blocked_readers_cgp(struct rcu_node *rnp)
{
	return 0;
}

#ifdef CONFIG_HOTPLUG_CPU

/* Because preemptible RCU does not exist, no quieting of tasks. */
static void rcu_report_unblock_qs_rnp(struct rcu_node *rnp, unsigned long flags)
{
	raw_spin_unlock_irqrestore(&rnp->lock, flags);
}

#endif /* #ifdef CONFIG_HOTPLUG_CPU */

/*
 * Because preemptible RCU does not exist, we never have to check for
 * tasks blocked within RCU read-side critical sections.
 */
static void rcu_print_detail_task_stall(struct rcu_state *rsp)
{
}

/*
 * Because preemptible RCU does not exist, we never have to check for
 * tasks blocked within RCU read-side critical sections.
 */
static void rcu_print_task_stall(struct rcu_node *rnp)
{
}

/*
 * Because preemptible RCU does not exist, there is no need to suppress
 * its CPU stall warnings.
 */
static void rcu_preempt_stall_reset(void)
{
}

/*
 * Because there is no preemptible RCU, there can be no readers blocked,
 * so there is no need to check for blocked tasks.  So check only for
 * bogus qsmask values.
 */
static void rcu_preempt_check_blocked_tasks(struct rcu_node *rnp)
{
	WARN_ON_ONCE(rnp->qsmask);
}

#ifdef CONFIG_HOTPLUG_CPU

/*
 * Because preemptible RCU does not exist, it never needs to migrate
 * tasks that were blocked within RCU read-side critical sections, and
 * such non-existent tasks cannot possibly have been blocking the current
 * grace period.
 */
static int rcu_preempt_offline_tasks(struct rcu_state *rsp,
				     struct rcu_node *rnp,
				     struct rcu_data *rdp)
{
	return 0;
}

/*
 * Because preemptible RCU does not exist, it never needs CPU-offline
 * processing.
 */
static void rcu_preempt_offline_cpu(int cpu)
{
}

#endif /* #ifdef CONFIG_HOTPLUG_CPU */

/*
 * Because preemptible RCU does not exist, it never has any callbacks
 * to check.
 */
static void rcu_preempt_check_callbacks(int cpu)
{
}

/*
 * Because preemptible RCU does not exist, it never has any callbacks
 * to process.
 */
static void rcu_preempt_process_callbacks(void)
{
}

/*
 * Wait for an rcu-preempt grace period, but make it happen quickly.
 * But because preemptible RCU does not exist, map to rcu-sched.
 */
void synchronize_rcu_expedited(void)
{
	synchronize_sched_expedited();
}
EXPORT_SYMBOL_GPL(synchronize_rcu_expedited);

#ifdef CONFIG_HOTPLUG_CPU

/*
 * Because preemptible RCU does not exist, there is never any need to
 * report on tasks preempted in RCU read-side critical sections during
 * expedited RCU grace periods.
 */
static void rcu_report_exp_rnp(struct rcu_state *rsp, struct rcu_node *rnp)
{
	return;
}

#endif /* #ifdef CONFIG_HOTPLUG_CPU */

/*
 * Because preemptible RCU does not exist, it never has any work to do.
 */
static int rcu_preempt_pending(int cpu)
{
	return 0;
}

/*
 * Because preemptible RCU does not exist, it never needs any CPU.
 */
static int rcu_preempt_needs_cpu(int cpu)
{
	return 0;
}

/*
 * Because preemptible RCU does not exist, rcu_barrier() is just
 * another name for rcu_barrier_sched().
 */
void rcu_barrier(void)
{
	rcu_barrier_sched();
}
EXPORT_SYMBOL_GPL(rcu_barrier);

/*
 * Because preemptible RCU does not exist, there is no per-CPU
 * data to initialize.
 */
static void __cpuinit rcu_preempt_init_percpu_data(int cpu)
{
}

/*
 * Because there is no preemptible RCU, there are no callbacks to move.
 */
static void rcu_preempt_send_cbs_to_online(void)
{
}

/*
 * Because preemptible RCU does not exist, it need not be initialized.
 */
static void __init __rcu_init_preempt(void)
{
}

#endif /* #else #ifdef CONFIG_TREE_PREEMPT_RCU */

#ifdef CONFIG_RCU_BOOST

#include "rtmutex_common.h"

#ifdef CONFIG_RCU_TRACE

static void rcu_initiate_boost_trace(struct rcu_node *rnp)
{
	if (list_empty(&rnp->blkd_tasks))
		rnp->n_balk_blkd_tasks++;
	else if (rnp->exp_tasks == NULL && rnp->gp_tasks == NULL)
		rnp->n_balk_exp_gp_tasks++;
	else if (rnp->gp_tasks != NULL && rnp->boost_tasks != NULL)
		rnp->n_balk_boost_tasks++;
	else if (rnp->gp_tasks != NULL && rnp->qsmask != 0)
		rnp->n_balk_notblocked++;
	else if (rnp->gp_tasks != NULL &&
		 ULONG_CMP_LT(jiffies, rnp->boost_time))
		rnp->n_balk_notyet++;
	else
		rnp->n_balk_nos++;
}

#else /* #ifdef CONFIG_RCU_TRACE */

static void rcu_initiate_boost_trace(struct rcu_node *rnp)
{
}

#endif /* #else #ifdef CONFIG_RCU_TRACE */

/*
 * Carry out RCU priority boosting on the task indicated by ->exp_tasks
 * or ->boost_tasks, advancing the pointer to the next task in the
 * ->blkd_tasks list.
 *
 * Note that irqs must be enabled: boosting the task can block.
 * Returns 1 if there are more tasks needing to be boosted.
 */
static int rcu_boost(struct rcu_node *rnp)
{
	unsigned long flags;
	struct rt_mutex mtx;
	struct task_struct *t;
	struct list_head *tb;

	if (rnp->exp_tasks == NULL && rnp->boost_tasks == NULL)
		return 0;  /* Nothing left to boost. */

	raw_spin_lock_irqsave(&rnp->lock, flags);

	/*
	 * Recheck under the lock: all tasks in need of boosting
	 * might exit their RCU read-side critical sections on their own.
	 */
	if (rnp->exp_tasks == NULL && rnp->boost_tasks == NULL) {
		raw_spin_unlock_irqrestore(&rnp->lock, flags);
		return 0;
	}

	/*
	 * Preferentially boost tasks blocking expedited grace periods.
	 * This cannot starve the normal grace periods because a second
	 * expedited grace period must boost all blocked tasks, including
	 * those blocking the pre-existing normal grace period.
	 */
	if (rnp->exp_tasks != NULL) {
		tb = rnp->exp_tasks;
		rnp->n_exp_boosts++;
	} else {
		tb = rnp->boost_tasks;
		rnp->n_normal_boosts++;
	}
	rnp->n_tasks_boosted++;

	/*
	 * We boost task t by manufacturing an rt_mutex that appears to
	 * be held by task t.  We leave a pointer to that rt_mutex where
	 * task t can find it, and task t will release the mutex when it
	 * exits its outermost RCU read-side critical section.  Then
	 * simply acquiring this artificial rt_mutex will boost task
	 * t's priority.  (Thanks to tglx for suggesting this approach!)
	 *
	 * Note that task t must acquire rnp->lock to remove itself from
	 * the ->blkd_tasks list, which it will do from exit() if from
	 * nowhere else.  We therefore are guaranteed that task t will
	 * stay around at least until we drop rnp->lock.  Note that
	 * rnp->lock also resolves races between our priority boosting
	 * and task t's exiting its outermost RCU read-side critical
	 * section.
	 */
	t = container_of(tb, struct task_struct, rcu_node_entry);
	rt_mutex_init_proxy_locked(&mtx, t);
	t->rcu_boost_mutex = &mtx;
	t->rcu_read_unlock_special |= RCU_READ_UNLOCK_BOOSTED;
	raw_spin_unlock_irqrestore(&rnp->lock, flags);
	rt_mutex_lock(&mtx);  /* Side effect: boosts task t's priority. */
	rt_mutex_unlock(&mtx);  /* Keep lockdep happy. */

	return rnp->exp_tasks != NULL || rnp->boost_tasks != NULL;
}

/*
 * Timer handler to initiate waking up of boost kthreads that
 * have yielded the CPU due to excessive numbers of tasks to
 * boost.  We wake up the per-rcu_node kthread, which in turn
 * will wake up the booster kthread.
 */
static void rcu_boost_kthread_timer(unsigned long arg)
{
	invoke_rcu_node_kthread((struct rcu_node *)arg);
}

/*
 * Priority-boosting kthread.  One per leaf rcu_node and one for the
 * root rcu_node.
 */
static int rcu_boost_kthread(void *arg)
{
	struct rcu_node *rnp = (struct rcu_node *)arg;
	int spincnt = 0;
	int more2boost;

	for (;;) {
		rnp->boost_kthread_status = RCU_KTHREAD_WAITING;
		rcu_wait(rnp->boost_tasks || rnp->exp_tasks);
		rnp->boost_kthread_status = RCU_KTHREAD_RUNNING;
		more2boost = rcu_boost(rnp);
		if (more2boost)
			spincnt++;
		else
			spincnt = 0;
		if (spincnt > 10) {
			rcu_yield(rcu_boost_kthread_timer, (unsigned long)rnp);
			spincnt = 0;
		}
	}
	/* NOTREACHED */
	return 0;
}

/*
 * Check to see if it is time to start boosting RCU readers that are
 * blocking the current grace period, and, if so, tell the per-rcu_node
 * kthread to start boosting them.  If there is an expedited grace
 * period in progress, it is always time to boost.
 *
 * The caller must hold rnp->lock, which this function releases,
 * but irqs remain disabled.  The ->boost_kthread_task is immortal,
 * so we don't need to worry about it going away.
 */
static void rcu_initiate_boost(struct rcu_node *rnp, unsigned long flags)
{
	struct task_struct *t;

	if (!rcu_preempt_blocked_readers_cgp(rnp) && rnp->exp_tasks == NULL) {
		rnp->n_balk_exp_gp_tasks++;
		raw_spin_unlock_irqrestore(&rnp->lock, flags);
		return;
	}
	if (rnp->exp_tasks != NULL ||
	    (rnp->gp_tasks != NULL &&
	     rnp->boost_tasks == NULL &&
	     rnp->qsmask == 0 &&
	     ULONG_CMP_GE(jiffies, rnp->boost_time))) {
		if (rnp->exp_tasks == NULL)
			rnp->boost_tasks = rnp->gp_tasks;
		raw_spin_unlock_irqrestore(&rnp->lock, flags);
		t = rnp->boost_kthread_task;
		if (t != NULL)
			wake_up_process(t);
	} else {
		rcu_initiate_boost_trace(rnp);
		raw_spin_unlock_irqrestore(&rnp->lock, flags);
	}
}

/*
 * Wake up the per-CPU kthread to invoke RCU callbacks.
 */
static void invoke_rcu_callbacks_kthread(void)
{
	unsigned long flags;

	local_irq_save(flags);
	__this_cpu_write(rcu_cpu_has_work, 1);
	if (__this_cpu_read(rcu_cpu_kthread_task) == NULL) {
		local_irq_restore(flags);
		return;
	}
	wake_up_process(__this_cpu_read(rcu_cpu_kthread_task));
	local_irq_restore(flags);
}

/*
 * Set the affinity of the boost kthread.  The CPU-hotplug locks are
 * held, so no one should be messing with the existence of the boost
 * kthread.
 */
static void rcu_boost_kthread_setaffinity(struct rcu_node *rnp,
					  cpumask_var_t cm)
{
	struct task_struct *t;

	t = rnp->boost_kthread_task;
	if (t != NULL)
		set_cpus_allowed_ptr(rnp->boost_kthread_task, cm);
}

#define RCU_BOOST_DELAY_JIFFIES DIV_ROUND_UP(CONFIG_RCU_BOOST_DELAY * HZ, 1000)

/*
 * Do priority-boost accounting for the start of a new grace period.
 */
static void rcu_preempt_boost_start_gp(struct rcu_node *rnp)
{
	rnp->boost_time = jiffies + RCU_BOOST_DELAY_JIFFIES;
}

/*
 * Create an RCU-boost kthread for the specified node if one does not
 * already exist.  We only create this kthread for preemptible RCU.
 * Returns zero if all is well, a negated errno otherwise.
 */
static int __cpuinit rcu_spawn_one_boost_kthread(struct rcu_state *rsp,
						 struct rcu_node *rnp,
						 int rnp_index)
{
	unsigned long flags;
	struct sched_param sp;
	struct task_struct *t;

	if (&rcu_preempt_state != rsp)
		return 0;
	rsp->boost = 1;
	if (rnp->boost_kthread_task != NULL)
		return 0;
	t = kthread_create(rcu_boost_kthread, (void *)rnp,
			   "rcub%d", rnp_index);
	if (IS_ERR(t))
		return PTR_ERR(t);
	raw_spin_lock_irqsave(&rnp->lock, flags);
	rnp->boost_kthread_task = t;
	raw_spin_unlock_irqrestore(&rnp->lock, flags);
	sp.sched_priority = RCU_KTHREAD_PRIO;
	sched_setscheduler_nocheck(t, SCHED_FIFO, &sp);
	wake_up_process(t); /* get to TASK_INTERRUPTIBLE quickly. */
	return 0;
}

#ifdef CONFIG_HOTPLUG_CPU

/*
 * Stop the RCU's per-CPU kthread when its CPU goes offline,.
 */
static void rcu_stop_cpu_kthread(int cpu)
{
	struct task_struct *t;

	/* Stop the CPU's kthread. */
	t = per_cpu(rcu_cpu_kthread_task, cpu);
	if (t != NULL) {
		per_cpu(rcu_cpu_kthread_task, cpu) = NULL;
		kthread_stop(t);
	}
}

#endif /* #ifdef CONFIG_HOTPLUG_CPU */

static void rcu_kthread_do_work(void)
{
	rcu_do_batch(&rcu_sched_state, &__get_cpu_var(rcu_sched_data));
	rcu_do_batch(&rcu_bh_state, &__get_cpu_var(rcu_bh_data));
	rcu_preempt_do_callbacks();
}

/*
 * Wake up the specified per-rcu_node-structure kthread.
 * Because the per-rcu_node kthreads are immortal, we don't need
 * to do anything to keep them alive.
 */
static void invoke_rcu_node_kthread(struct rcu_node *rnp)
{
	struct task_struct *t;

	t = rnp->node_kthread_task;
	if (t != NULL)
		wake_up_process(t);
}

/*
 * Set the specified CPU's kthread to run RT or not, as specified by
 * the to_rt argument.  The CPU-hotplug locks are held, so the task
 * is not going away.
 */
static void rcu_cpu_kthread_setrt(int cpu, int to_rt)
{
	int policy;
	struct sched_param sp;
	struct task_struct *t;

	t = per_cpu(rcu_cpu_kthread_task, cpu);
	if (t == NULL)
		return;
	if (to_rt) {
		policy = SCHED_FIFO;
		sp.sched_priority = RCU_KTHREAD_PRIO;
	} else {
		policy = SCHED_NORMAL;
		sp.sched_priority = 0;
	}
	sched_setscheduler_nocheck(t, policy, &sp);
}

/*
 * Timer handler to initiate the waking up of per-CPU kthreads that
 * have yielded the CPU due to excess numbers of RCU callbacks.
 * We wake up the per-rcu_node kthread, which in turn will wake up
 * the booster kthread.
 */
static void rcu_cpu_kthread_timer(unsigned long arg)
{
	struct rcu_data *rdp = per_cpu_ptr(rcu_state->rda, arg);
	struct rcu_node *rnp = rdp->mynode;

	atomic_or(rdp->grpmask, &rnp->wakemask);
	invoke_rcu_node_kthread(rnp);
}

/*
 * Drop to non-real-time priority and yield, but only after posting a
 * timer that will cause us to regain our real-time priority if we
 * remain preempted.  Either way, we restore our real-time priority
 * before returning.
 */
static void rcu_yield(void (*f)(unsigned long), unsigned long arg)
{
	struct sched_param sp;
	struct timer_list yield_timer;

	setup_timer_on_stack(&yield_timer, f, arg);
	mod_timer(&yield_timer, jiffies + 2);
	sp.sched_priority = 0;
	sched_setscheduler_nocheck(current, SCHED_NORMAL, &sp);
	set_user_nice(current, 19);
	schedule();
	sp.sched_priority = RCU_KTHREAD_PRIO;
	sched_setscheduler_nocheck(current, SCHED_FIFO, &sp);
	del_timer(&yield_timer);
}

/*
 * Handle cases where the rcu_cpu_kthread() ends up on the wrong CPU.
 * This can happen while the corresponding CPU is either coming online
 * or going offline.  We cannot wait until the CPU is fully online
 * before starting the kthread, because the various notifier functions
 * can wait for RCU grace periods.  So we park rcu_cpu_kthread() until
 * the corresponding CPU is online.
 *
 * Return 1 if the kthread needs to stop, 0 otherwise.
 *
 * Caller must disable bh.  This function can momentarily enable it.
 */
static int rcu_cpu_kthread_should_stop(int cpu)
{
	while (cpu_is_offline(cpu) ||
	       !cpumask_equal(&current->cpus_allowed, cpumask_of(cpu)) ||
	       smp_processor_id() != cpu) {
		if (kthread_should_stop())
			return 1;
		per_cpu(rcu_cpu_kthread_status, cpu) = RCU_KTHREAD_OFFCPU;
		per_cpu(rcu_cpu_kthread_cpu, cpu) = raw_smp_processor_id();
		local_bh_enable();
		schedule_timeout_uninterruptible(1);
		if (!cpumask_equal(&current->cpus_allowed, cpumask_of(cpu)))
			set_cpus_allowed_ptr(current, cpumask_of(cpu));
		local_bh_disable();
	}
	per_cpu(rcu_cpu_kthread_cpu, cpu) = cpu;
	return 0;
}

/*
 * Per-CPU kernel thread that invokes RCU callbacks.  This replaces the
 * earlier RCU softirq.
 */
static int rcu_cpu_kthread(void *arg)
{
	int cpu = (int)(long)arg;
	unsigned long flags;
	int spincnt = 0;
	unsigned int *statusp = &per_cpu(rcu_cpu_kthread_status, cpu);
	char work;
	char *workp = &per_cpu(rcu_cpu_has_work, cpu);

	for (;;) {
		*statusp = RCU_KTHREAD_WAITING;
		rcu_wait(*workp != 0 || kthread_should_stop());
		local_bh_disable();
		if (rcu_cpu_kthread_should_stop(cpu)) {
			local_bh_enable();
			break;
		}
		*statusp = RCU_KTHREAD_RUNNING;
		per_cpu(rcu_cpu_kthread_loops, cpu)++;
		local_irq_save(flags);
		work = *workp;
		*workp = 0;
		local_irq_restore(flags);
		if (work)
			rcu_kthread_do_work();
		local_bh_enable();
		if (*workp != 0)
			spincnt++;
		else
			spincnt = 0;
		if (spincnt > 10) {
			*statusp = RCU_KTHREAD_YIELDING;
			rcu_yield(rcu_cpu_kthread_timer, (unsigned long)cpu);
			spincnt = 0;
		}
	}
	*statusp = RCU_KTHREAD_STOPPED;
	return 0;
}

/*
 * Spawn a per-CPU kthread, setting up affinity and priority.
 * Because the CPU hotplug lock is held, no other CPU will be attempting
 * to manipulate rcu_cpu_kthread_task.  There might be another CPU
 * attempting to access it during boot, but the locking in kthread_bind()
 * will enforce sufficient ordering.
 *
 * Please note that we cannot simply refuse to wake up the per-CPU
 * kthread because kthreads are created in TASK_UNINTERRUPTIBLE state,
 * which can result in softlockup complaints if the task ends up being
 * idle for more than a couple of minutes.
 *
 * However, please note also that we cannot bind the per-CPU kthread to its
 * CPU until that CPU is fully online.  We also cannot wait until the
 * CPU is fully online before we create its per-CPU kthread, as this would
 * deadlock the system when CPU notifiers tried waiting for grace
 * periods.  So we bind the per-CPU kthread to its CPU only if the CPU
 * is online.  If its CPU is not yet fully online, then the code in
 * rcu_cpu_kthread() will wait until it is fully online, and then do
 * the binding.
 */
static int __cpuinit rcu_spawn_one_cpu_kthread(int cpu)
{
	struct sched_param sp;
	struct task_struct *t;

	if (!rcu_kthreads_spawnable ||
	    per_cpu(rcu_cpu_kthread_task, cpu) != NULL)
		return 0;
	t = kthread_create(rcu_cpu_kthread, (void *)(long)cpu, "rcuc%d", cpu);
	if (IS_ERR(t))
		return PTR_ERR(t);
	if (cpu_online(cpu))
		kthread_bind(t, cpu);
	per_cpu(rcu_cpu_kthread_cpu, cpu) = cpu;
	WARN_ON_ONCE(per_cpu(rcu_cpu_kthread_task, cpu) != NULL);
	sp.sched_priority = RCU_KTHREAD_PRIO;
	sched_setscheduler_nocheck(t, SCHED_FIFO, &sp);
	per_cpu(rcu_cpu_kthread_task, cpu) = t;
	wake_up_process(t); /* Get to TASK_INTERRUPTIBLE quickly. */
	return 0;
}

/*
 * Per-rcu_node kthread, which is in charge of waking up the per-CPU
 * kthreads when needed.  We ignore requests to wake up kthreads
 * for offline CPUs, which is OK because force_quiescent_state()
 * takes care of this case.
 */
static int rcu_node_kthread(void *arg)
{
	int cpu;
	unsigned long flags;
	unsigned long mask;
	struct rcu_node *rnp = (struct rcu_node *)arg;
	struct sched_param sp;
	struct task_struct *t;

	for (;;) {
		rnp->node_kthread_status = RCU_KTHREAD_WAITING;
		rcu_wait(atomic_read(&rnp->wakemask) != 0);
		rnp->node_kthread_status = RCU_KTHREAD_RUNNING;
		raw_spin_lock_irqsave(&rnp->lock, flags);
		mask = atomic_xchg(&rnp->wakemask, 0);
		rcu_initiate_boost(rnp, flags); /* releases rnp->lock. */
		for (cpu = rnp->grplo; cpu <= rnp->grphi; cpu++, mask >>= 1) {
			if ((mask & 0x1) == 0)
				continue;
			preempt_disable();
			t = per_cpu(rcu_cpu_kthread_task, cpu);
			if (!cpu_online(cpu) || t == NULL) {
				preempt_enable();
				continue;
			}
			per_cpu(rcu_cpu_has_work, cpu) = 1;
			sp.sched_priority = RCU_KTHREAD_PRIO;
			sched_setscheduler_nocheck(t, SCHED_FIFO, &sp);
			preempt_enable();
		}
	}
	/* NOTREACHED */
	rnp->node_kthread_status = RCU_KTHREAD_STOPPED;
	return 0;
}

/*
 * Set the per-rcu_node kthread's affinity to cover all CPUs that are
 * served by the rcu_node in question.  The CPU hotplug lock is still
 * held, so the value of rnp->qsmaskinit will be stable.
 *
 * We don't include outgoingcpu in the affinity set, use -1 if there is
 * no outgoing CPU.  If there are no CPUs left in the affinity set,
 * this function allows the kthread to execute on any CPU.
 */
static void rcu_node_kthread_setaffinity(struct rcu_node *rnp, int outgoingcpu)
{
	cpumask_var_t cm;
	int cpu;
	unsigned long mask = rnp->qsmaskinit;

	if (rnp->node_kthread_task == NULL)
		return;
	if (!alloc_cpumask_var(&cm, GFP_KERNEL))
		return;
	cpumask_clear(cm);
	for (cpu = rnp->grplo; cpu <= rnp->grphi; cpu++, mask >>= 1)
		if ((mask & 0x1) && cpu != outgoingcpu)
			cpumask_set_cpu(cpu, cm);
	if (cpumask_weight(cm) == 0) {
		cpumask_setall(cm);
		for (cpu = rnp->grplo; cpu <= rnp->grphi; cpu++)
			cpumask_clear_cpu(cpu, cm);
		WARN_ON_ONCE(cpumask_weight(cm) == 0);
	}
	set_cpus_allowed_ptr(rnp->node_kthread_task, cm);
	rcu_boost_kthread_setaffinity(rnp, cm);
	free_cpumask_var(cm);
}

/*
 * Spawn a per-rcu_node kthread, setting priority and affinity.
 * Called during boot before online/offline can happen, or, if
 * during runtime, with the main CPU-hotplug locks held.  So only
 * one of these can be executing at a time.
 */
static int __cpuinit rcu_spawn_one_node_kthread(struct rcu_state *rsp,
						struct rcu_node *rnp)
{
	unsigned long flags;
	int rnp_index = rnp - &rsp->node[0];
	struct sched_param sp;
	struct task_struct *t;

	if (!rcu_kthreads_spawnable ||
	    rnp->qsmaskinit == 0)
		return 0;
	if (rnp->node_kthread_task == NULL) {
		t = kthread_create(rcu_node_kthread, (void *)rnp,
				   "rcun%d", rnp_index);
		if (IS_ERR(t))
			return PTR_ERR(t);
		raw_spin_lock_irqsave(&rnp->lock, flags);
		rnp->node_kthread_task = t;
		raw_spin_unlock_irqrestore(&rnp->lock, flags);
		sp.sched_priority = 99;
		sched_setscheduler_nocheck(t, SCHED_FIFO, &sp);
		wake_up_process(t); /* get to TASK_INTERRUPTIBLE quickly. */
	}
	return rcu_spawn_one_boost_kthread(rsp, rnp, rnp_index);
}

/*
 * Spawn all kthreads -- called as soon as the scheduler is running.
 */
static int __init rcu_spawn_kthreads(void)
{
	int cpu;
	struct rcu_node *rnp;

	rcu_kthreads_spawnable = 1;
	for_each_possible_cpu(cpu) {
		per_cpu(rcu_cpu_has_work, cpu) = 0;
		if (cpu_online(cpu))
			(void)rcu_spawn_one_cpu_kthread(cpu);
	}
	rnp = rcu_get_root(rcu_state);
	(void)rcu_spawn_one_node_kthread(rcu_state, rnp);
	if (NUM_RCU_NODES > 1) {
		rcu_for_each_leaf_node(rcu_state, rnp)
			(void)rcu_spawn_one_node_kthread(rcu_state, rnp);
	}
	return 0;
}
early_initcall(rcu_spawn_kthreads);

static void __cpuinit rcu_prepare_kthreads(int cpu)
{
	struct rcu_data *rdp = per_cpu_ptr(rcu_state->rda, cpu);
	struct rcu_node *rnp = rdp->mynode;

	/* Fire up the incoming CPU's kthread and leaf rcu_node kthread. */
	if (rcu_kthreads_spawnable) {
		(void)rcu_spawn_one_cpu_kthread(cpu);
		if (rnp->node_kthread_task == NULL)
			(void)rcu_spawn_one_node_kthread(rcu_state, rnp);
	}
}

#else /* #ifdef CONFIG_RCU_BOOST */

static void rcu_initiate_boost(struct rcu_node *rnp, unsigned long flags)
{
	raw_spin_unlock_irqrestore(&rnp->lock, flags);
}

static void invoke_rcu_callbacks_kthread(void)
{
	WARN_ON_ONCE(1);
}

static void rcu_preempt_boost_start_gp(struct rcu_node *rnp)
{
}

#ifdef CONFIG_HOTPLUG_CPU

static void rcu_stop_cpu_kthread(int cpu)
{
}

#endif /* #ifdef CONFIG_HOTPLUG_CPU */

static void rcu_node_kthread_setaffinity(struct rcu_node *rnp, int outgoingcpu)
{
}

static void rcu_cpu_kthread_setrt(int cpu, int to_rt)
{
}

static void __cpuinit rcu_prepare_kthreads(int cpu)
{
}

#endif /* #else #ifdef CONFIG_RCU_BOOST */

#ifndef CONFIG_SMP

void synchronize_sched_expedited(void)
{
	cond_resched();
}
EXPORT_SYMBOL_GPL(synchronize_sched_expedited);

#else /* #ifndef CONFIG_SMP */

static atomic_t sync_sched_expedited_started = ATOMIC_INIT(0);
static atomic_t sync_sched_expedited_done = ATOMIC_INIT(0);

static int synchronize_sched_expedited_cpu_stop(void *data)
{
	/*
	 * There must be a full memory barrier on each affected CPU
	 * between the time that try_stop_cpus() is called and the
	 * time that it returns.
	 *
	 * In the current initial implementation of cpu_stop, the
	 * above condition is already met when the control reaches
	 * this point and the following smp_mb() is not strictly
	 * necessary.  Do smp_mb() anyway for documentation and
	 * robustness against future implementation changes.
	 */
	smp_mb(); /* See above comment block. */
	return 0;
}

/*
 * Wait for an rcu-sched grace period to elapse, but use "big hammer"
 * approach to force grace period to end quickly.  This consumes
 * significant time on all CPUs, and is thus not recommended for
 * any sort of common-case code.
 *
 * Note that it is illegal to call this function while holding any
 * lock that is acquired by a CPU-hotplug notifier.  Failing to
 * observe this restriction will result in deadlock.
 *
 * This implementation can be thought of as an application of ticket
 * locking to RCU, with sync_sched_expedited_started and
 * sync_sched_expedited_done taking on the roles of the halves
 * of the ticket-lock word.  Each task atomically increments
 * sync_sched_expedited_started upon entry, snapshotting the old value,
 * then attempts to stop all the CPUs.  If this succeeds, then each
 * CPU will have executed a context switch, resulting in an RCU-sched
 * grace period.  We are then done, so we use atomic_cmpxchg() to
 * update sync_sched_expedited_done to match our snapshot -- but
 * only if someone else has not already advanced past our snapshot.
 *
 * On the other hand, if try_stop_cpus() fails, we check the value
 * of sync_sched_expedited_done.  If it has advanced past our
 * initial snapshot, then someone else must have forced a grace period
 * some time after we took our snapshot.  In this case, our work is
 * done for us, and we can simply return.  Otherwise, we try again,
 * but keep our initial snapshot for purposes of checking for someone
 * doing our work for us.
 *
 * If we fail too many times in a row, we fall back to synchronize_sched().
 */
void synchronize_sched_expedited(void)
{
	int firstsnap, s, snap, trycount = 0;

	/* Note that atomic_inc_return() implies full memory barrier. */
	firstsnap = snap = atomic_inc_return(&sync_sched_expedited_started);
	get_online_cpus();

	/*
	 * Each pass through the following loop attempts to force a
	 * context switch on each CPU.
	 */
	while (try_stop_cpus(cpu_online_mask,
			     synchronize_sched_expedited_cpu_stop,
			     NULL) == -EAGAIN) {
		put_online_cpus();

		/* No joy, try again later.  Or just synchronize_sched(). */
		if (trycount++ < 10)
			udelay(trycount * num_online_cpus());
		else {
			synchronize_sched();
			return;
		}

		/* Check to see if someone else did our work for us. */
		s = atomic_read(&sync_sched_expedited_done);
		if (UINT_CMP_GE((unsigned)s, (unsigned)firstsnap)) {
			smp_mb(); /* ensure test happens before caller kfree */
			return;
		}

		/*
		 * Refetching sync_sched_expedited_started allows later
		 * callers to piggyback on our grace period.  We subtract
		 * 1 to get the same token that the last incrementer got.
		 * We retry after they started, so our grace period works
		 * for them, and they started after our first try, so their
		 * grace period works for us.
		 */
		get_online_cpus();
		snap = atomic_read(&sync_sched_expedited_started) - 1;
		smp_mb(); /* ensure read is before try_stop_cpus(). */
	}

	/*
	 * Everyone up to our most recent fetch is covered by our grace
	 * period.  Update the counter, but only if our work is still
	 * relevant -- which it won't be if someone who started later
	 * than we did beat us to the punch.
	 */
	do {
		s = atomic_read(&sync_sched_expedited_done);
		if (UINT_CMP_GE((unsigned)s, (unsigned)snap)) {
			smp_mb(); /* ensure test happens before caller kfree */
			break;
		}
	} while (atomic_cmpxchg(&sync_sched_expedited_done, s, snap) != s);

	put_online_cpus();
}
EXPORT_SYMBOL_GPL(synchronize_sched_expedited);

#endif /* #else #ifndef CONFIG_SMP */

#if !defined(CONFIG_RCU_FAST_NO_HZ)

/*
 * Check to see if any future RCU-related work will need to be done
 * by the current CPU, even if none need be done immediately, returning
 * 1 if so.  This function is part of the RCU implementation; it is -not-
 * an exported member of the RCU API.
 *
 * Because we have preemptible RCU, just check whether this CPU needs
 * any flavor of RCU.  Do not chew up lots of CPU cycles with preemption
 * disabled in a most-likely vain attempt to cause RCU not to need this CPU.
 */
int rcu_needs_cpu(int cpu)
{
	return rcu_needs_cpu_quick_check(cpu);
}

/*
 * Check to see if we need to continue a callback-flush operations to
 * allow the last CPU to enter dyntick-idle mode.  But fast dyntick-idle
 * entry is not configured, so we never do need to.
 */
static void rcu_needs_cpu_flush(void)
{
}

#else /* #if !defined(CONFIG_RCU_FAST_NO_HZ) */

#define RCU_NEEDS_CPU_FLUSHES 5
static DEFINE_PER_CPU(int, rcu_dyntick_drain);
static DEFINE_PER_CPU(unsigned long, rcu_dyntick_holdoff);

/*
 * Check to see if any future RCU-related work will need to be done
 * by the current CPU, even if none need be done immediately, returning
 * 1 if so.  This function is part of the RCU implementation; it is -not-
 * an exported member of the RCU API.
 *
 * Because we are not supporting preemptible RCU, attempt to accelerate
 * any current grace periods so that RCU no longer needs this CPU, but
 * only if all other CPUs are already in dynticks-idle mode.  This will
 * allow the CPU cores to be powered down immediately, as opposed to after
 * waiting many milliseconds for grace periods to elapse.
 *
 * Because it is not legal to invoke rcu_process_callbacks() with irqs
 * disabled, we do one pass of force_quiescent_state(), then do a
 * invoke_rcu_core() to cause rcu_process_callbacks() to be invoked
 * later.  The per-cpu rcu_dyntick_drain variable controls the sequencing.
 */
int rcu_needs_cpu(int cpu)
{
	int c = 0;
	int snap;
	int thatcpu;

	/* Check for being in the holdoff period. */
	if (per_cpu(rcu_dyntick_holdoff, cpu) == jiffies)
		return rcu_needs_cpu_quick_check(cpu);

	/* Don't bother unless we are the last non-dyntick-idle CPU. */
	for_each_online_cpu(thatcpu) {
		if (thatcpu == cpu)
			continue;
		snap = atomic_add_return(0, &per_cpu(rcu_dynticks,
						     thatcpu).dynticks);
		smp_mb(); /* Order sampling of snap with end of grace period. */
		if ((snap & 0x1) != 0) {
			per_cpu(rcu_dyntick_drain, cpu) = 0;
			per_cpu(rcu_dyntick_holdoff, cpu) = jiffies - 1;
			return rcu_needs_cpu_quick_check(cpu);
		}
	}

	/* Check and update the rcu_dyntick_drain sequencing. */
	if (per_cpu(rcu_dyntick_drain, cpu) <= 0) {
		/* First time through, initialize the counter. */
		per_cpu(rcu_dyntick_drain, cpu) = RCU_NEEDS_CPU_FLUSHES;
	} else if (--per_cpu(rcu_dyntick_drain, cpu) <= 0) {
		/* We have hit the limit, so time to give up. */
		per_cpu(rcu_dyntick_holdoff, cpu) = jiffies;
		return rcu_needs_cpu_quick_check(cpu);
	}

	/* Do one step pushing remaining RCU callbacks through. */
	if (per_cpu(rcu_sched_data, cpu).nxtlist) {
		rcu_sched_qs(cpu);
		force_quiescent_state(&rcu_sched_state, 0);
		c = c || per_cpu(rcu_sched_data, cpu).nxtlist;
	}
	if (per_cpu(rcu_bh_data, cpu).nxtlist) {
		rcu_bh_qs(cpu);
		force_quiescent_state(&rcu_bh_state, 0);
		c = c || per_cpu(rcu_bh_data, cpu).nxtlist;
	}

	/* If RCU callbacks are still pending, RCU still needs this CPU. */
	if (c)
		invoke_rcu_core();
	return c;
}

/*
 * Check to see if we need to continue a callback-flush operations to
 * allow the last CPU to enter dyntick-idle mode.
 */
static void rcu_needs_cpu_flush(void)
{
	int cpu = smp_processor_id();
	unsigned long flags;

	if (per_cpu(rcu_dyntick_drain, cpu) <= 0)
		return;
	local_irq_save(flags);
	(void)rcu_needs_cpu(cpu);
	local_irq_restore(flags);
}

#endif /* #else #if !defined(CONFIG_RCU_FAST_NO_HZ) */
