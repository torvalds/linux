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
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * Copyright Red Hat, 2009
 * Copyright IBM Corporation, 2009
 *
 * Author: Ingo Molnar <mingo@elte.hu>
 *	   Paul E. McKenney <paulmck@linux.vnet.ibm.com>
 */

#include <linux/delay.h>
#include <linux/gfp.h>
#include <linux/oom.h>
#include <linux/smpboot.h>
#include "../time/tick-internal.h"

#define RCU_KTHREAD_PRIO 1

#ifdef CONFIG_RCU_BOOST
#define RCU_BOOST_PRIO CONFIG_RCU_BOOST_PRIO
#else
#define RCU_BOOST_PRIO RCU_KTHREAD_PRIO
#endif

#ifdef CONFIG_RCU_NOCB_CPU
static cpumask_var_t rcu_nocb_mask; /* CPUs to have callbacks offloaded. */
static bool have_rcu_nocb_mask;	    /* Was rcu_nocb_mask allocated? */
static bool __read_mostly rcu_nocb_poll;    /* Offload kthread are to poll. */
static char __initdata nocb_buf[NR_CPUS * 5];
#endif /* #ifdef CONFIG_RCU_NOCB_CPU */

/*
 * Check the RCU kernel configuration parameters and print informative
 * messages about anything out of the ordinary.  If you like #ifdef, you
 * will love this function.
 */
static void __init rcu_bootup_announce_oddness(void)
{
#ifdef CONFIG_RCU_TRACE
	pr_info("\tRCU debugfs-based tracing is enabled.\n");
#endif
#if (defined(CONFIG_64BIT) && CONFIG_RCU_FANOUT != 64) || (!defined(CONFIG_64BIT) && CONFIG_RCU_FANOUT != 32)
	pr_info("\tCONFIG_RCU_FANOUT set to non-default value of %d\n",
	       CONFIG_RCU_FANOUT);
#endif
#ifdef CONFIG_RCU_FANOUT_EXACT
	pr_info("\tHierarchical RCU autobalancing is disabled.\n");
#endif
#ifdef CONFIG_RCU_FAST_NO_HZ
	pr_info("\tRCU dyntick-idle grace-period acceleration is enabled.\n");
#endif
#ifdef CONFIG_PROVE_RCU
	pr_info("\tRCU lockdep checking is enabled.\n");
#endif
#ifdef CONFIG_RCU_TORTURE_TEST_RUNNABLE
	pr_info("\tRCU torture testing starts during boot.\n");
#endif
#if defined(CONFIG_TREE_PREEMPT_RCU) && !defined(CONFIG_RCU_CPU_STALL_VERBOSE)
	pr_info("\tDump stacks of tasks blocking RCU-preempt GP.\n");
#endif
#if defined(CONFIG_RCU_CPU_STALL_INFO)
	pr_info("\tAdditional per-CPU info printed with stalls.\n");
#endif
#if NUM_RCU_LVL_4 != 0
	pr_info("\tFour-level hierarchy is enabled.\n");
#endif
	if (rcu_fanout_leaf != CONFIG_RCU_FANOUT_LEAF)
		pr_info("\tBoot-time adjustment of leaf fanout to %d.\n", rcu_fanout_leaf);
	if (nr_cpu_ids != NR_CPUS)
		pr_info("\tRCU restricting CPUs from NR_CPUS=%d to nr_cpu_ids=%d.\n", NR_CPUS, nr_cpu_ids);
#ifdef CONFIG_RCU_NOCB_CPU
#ifndef CONFIG_RCU_NOCB_CPU_NONE
	if (!have_rcu_nocb_mask) {
		zalloc_cpumask_var(&rcu_nocb_mask, GFP_KERNEL);
		have_rcu_nocb_mask = true;
	}
#ifdef CONFIG_RCU_NOCB_CPU_ZERO
	pr_info("\tOffload RCU callbacks from CPU 0\n");
	cpumask_set_cpu(0, rcu_nocb_mask);
#endif /* #ifdef CONFIG_RCU_NOCB_CPU_ZERO */
#ifdef CONFIG_RCU_NOCB_CPU_ALL
	pr_info("\tOffload RCU callbacks from all CPUs\n");
	cpumask_copy(rcu_nocb_mask, cpu_possible_mask);
#endif /* #ifdef CONFIG_RCU_NOCB_CPU_ALL */
#endif /* #ifndef CONFIG_RCU_NOCB_CPU_NONE */
	if (have_rcu_nocb_mask) {
		if (!cpumask_subset(rcu_nocb_mask, cpu_possible_mask)) {
			pr_info("\tNote: kernel parameter 'rcu_nocbs=' contains nonexistent CPUs.\n");
			cpumask_and(rcu_nocb_mask, cpu_possible_mask,
				    rcu_nocb_mask);
		}
		cpulist_scnprintf(nocb_buf, sizeof(nocb_buf), rcu_nocb_mask);
		pr_info("\tOffload RCU callbacks from CPUs: %s.\n", nocb_buf);
		if (rcu_nocb_poll)
			pr_info("\tPoll for callbacks from no-CBs CPUs.\n");
	}
#endif /* #ifdef CONFIG_RCU_NOCB_CPU */
}

#ifdef CONFIG_TREE_PREEMPT_RCU

RCU_STATE_INITIALIZER(rcu_preempt, 'p', call_rcu);
static struct rcu_state *rcu_state_p = &rcu_preempt_state;

static int rcu_preempted_readers_exp(struct rcu_node *rnp);

/*
 * Tell them what RCU they are running.
 */
static void __init rcu_bootup_announce(void)
{
	pr_info("Preemptible hierarchical RCU implementation.\n");
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

	if (rdp->passed_quiesce == 0)
		trace_rcu_grace_period(TPS("rcu_preempt"), rdp->gpnum, TPS("cpuqs"));
	rdp->passed_quiesce = 1;
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

	if (t->rcu_read_lock_nesting > 0 &&
	    (t->rcu_read_unlock_special & RCU_READ_UNLOCK_BLOCKED) == 0) {

		/* Possibly blocking in an RCU read-side critical section. */
		rdp = per_cpu_ptr(rcu_preempt_state.rda, cpu);
		rnp = rdp->mynode;
		raw_spin_lock_irqsave(&rnp->lock, flags);
		smp_mb__after_unlock_lock();
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
		trace_rcu_preempt_task(rdp->rsp->name,
				       t->pid,
				       (rnp->qsmask & rdp->grpmask)
				       ? rnp->gpnum
				       : rnp->gpnum + 1);
		raw_spin_unlock_irqrestore(&rnp->lock, flags);
	} else if (t->rcu_read_lock_nesting < 0 &&
		   t->rcu_read_unlock_special) {

		/*
		 * Complete exit from RCU read-side critical section on
		 * behalf of preempted instance of __rcu_read_unlock().
		 */
		rcu_read_unlock_special(t);
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
	smp_mb__after_unlock_lock();
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
void rcu_read_unlock_special(struct task_struct *t)
{
	int empty;
	int empty_exp;
	int empty_exp_now;
	unsigned long flags;
	struct list_head *np;
#ifdef CONFIG_RCU_BOOST
	struct rt_mutex *rbmp = NULL;
#endif /* #ifdef CONFIG_RCU_BOOST */
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
		if (!t->rcu_read_unlock_special) {
			local_irq_restore(flags);
			return;
		}
	}

	/* Hardware IRQ handlers cannot block, complain if they get here. */
	if (WARN_ON_ONCE(in_irq() || in_serving_softirq())) {
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
			smp_mb__after_unlock_lock();
			if (rnp == t->rcu_blocked_node)
				break;
			raw_spin_unlock(&rnp->lock); /* irqs remain disabled. */
		}
		empty = !rcu_preempt_blocked_readers_cgp(rnp);
		empty_exp = !rcu_preempted_readers_exp(rnp);
		smp_mb(); /* ensure expedited fastpath sees end of RCU c-s. */
		np = rcu_next_node_entry(t, rnp);
		list_del_init(&t->rcu_node_entry);
		t->rcu_blocked_node = NULL;
		trace_rcu_unlock_preempted_task(TPS("rcu_preempt"),
						rnp->gpnum, t->pid);
		if (&t->rcu_node_entry == rnp->gp_tasks)
			rnp->gp_tasks = np;
		if (&t->rcu_node_entry == rnp->exp_tasks)
			rnp->exp_tasks = np;
#ifdef CONFIG_RCU_BOOST
		if (&t->rcu_node_entry == rnp->boost_tasks)
			rnp->boost_tasks = np;
		/* Snapshot/clear ->rcu_boost_mutex with rcu_node lock held. */
		if (t->rcu_boost_mutex) {
			rbmp = t->rcu_boost_mutex;
			t->rcu_boost_mutex = NULL;
		}
#endif /* #ifdef CONFIG_RCU_BOOST */

		/*
		 * If this was the last task on the current list, and if
		 * we aren't waiting on any CPUs, report the quiescent state.
		 * Note that rcu_report_unblock_qs_rnp() releases rnp->lock,
		 * so we must take a snapshot of the expedited state.
		 */
		empty_exp_now = !rcu_preempted_readers_exp(rnp);
		if (!empty && !rcu_preempt_blocked_readers_cgp(rnp)) {
			trace_rcu_quiescent_state_report(TPS("preempt_rcu"),
							 rnp->gpnum,
							 0, rnp->qsmask,
							 rnp->level,
							 rnp->grplo,
							 rnp->grphi,
							 !!rnp->gp_tasks);
			rcu_report_unblock_qs_rnp(rnp, flags);
		} else {
			raw_spin_unlock_irqrestore(&rnp->lock, flags);
		}

#ifdef CONFIG_RCU_BOOST
		/* Unboost if we were boosted. */
		if (rbmp)
			rt_mutex_unlock(rbmp);
#endif /* #ifdef CONFIG_RCU_BOOST */

		/*
		 * If this was the last task on the expedited lists,
		 * then we need to report up the rcu_node hierarchy.
		 */
		if (!empty_exp && empty_exp_now)
			rcu_report_exp_rnp(&rcu_preempt_state, rnp, true);
	} else {
		local_irq_restore(flags);
	}
}

#ifdef CONFIG_RCU_CPU_STALL_VERBOSE

/*
 * Dump detailed information for all tasks blocking the current RCU
 * grace period on the specified rcu_node structure.
 */
static void rcu_print_detail_task_stall_rnp(struct rcu_node *rnp)
{
	unsigned long flags;
	struct task_struct *t;

	raw_spin_lock_irqsave(&rnp->lock, flags);
	if (!rcu_preempt_blocked_readers_cgp(rnp)) {
		raw_spin_unlock_irqrestore(&rnp->lock, flags);
		return;
	}
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

#ifdef CONFIG_RCU_CPU_STALL_INFO

static void rcu_print_task_stall_begin(struct rcu_node *rnp)
{
	pr_err("\tTasks blocked on level-%d rcu_node (CPUs %d-%d):",
	       rnp->level, rnp->grplo, rnp->grphi);
}

static void rcu_print_task_stall_end(void)
{
	pr_cont("\n");
}

#else /* #ifdef CONFIG_RCU_CPU_STALL_INFO */

static void rcu_print_task_stall_begin(struct rcu_node *rnp)
{
}

static void rcu_print_task_stall_end(void)
{
}

#endif /* #else #ifdef CONFIG_RCU_CPU_STALL_INFO */

/*
 * Scan the current list of tasks blocked within RCU read-side critical
 * sections, printing out the tid of each.
 */
static int rcu_print_task_stall(struct rcu_node *rnp)
{
	struct task_struct *t;
	int ndetected = 0;

	if (!rcu_preempt_blocked_readers_cgp(rnp))
		return 0;
	rcu_print_task_stall_begin(rnp);
	t = list_entry(rnp->gp_tasks,
		       struct task_struct, rcu_node_entry);
	list_for_each_entry_continue(t, &rnp->blkd_tasks, rcu_node_entry) {
		pr_cont(" P%d", t->pid);
		ndetected++;
	}
	rcu_print_task_stall_end();
	return ndetected;
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
	if (rcu_preempt_blocked_readers_cgp(rnp) && rnp->qsmask == 0)
		retval |= RCU_OFL_TASKS_NORM_GP;
	if (rcu_preempted_readers_exp(rnp))
		retval |= RCU_OFL_TASKS_EXP_GP;
	lp = &rnp->blkd_tasks;
	lp_root = &rnp_root->blkd_tasks;
	while (!list_empty(lp)) {
		t = list_entry(lp->next, typeof(*t), rcu_node_entry);
		raw_spin_lock(&rnp_root->lock); /* irqs already disabled */
		smp_mb__after_unlock_lock();
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

	rnp->gp_tasks = NULL;
	rnp->exp_tasks = NULL;
#ifdef CONFIG_RCU_BOOST
	rnp->boost_tasks = NULL;
	/*
	 * In case root is being boosted and leaf was not.  Make sure
	 * that we boost the tasks blocking the current grace period
	 * in this case.
	 */
	raw_spin_lock(&rnp_root->lock); /* irqs already disabled */
	smp_mb__after_unlock_lock();
	if (rnp_root->boost_tasks != NULL &&
	    rnp_root->boost_tasks != rnp_root->gp_tasks &&
	    rnp_root->boost_tasks != rnp_root->exp_tasks)
		rnp_root->boost_tasks = rnp_root->gp_tasks;
	raw_spin_unlock(&rnp_root->lock); /* irqs still disabled */
#endif /* #ifdef CONFIG_RCU_BOOST */

	return retval;
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
	if (t->rcu_read_lock_nesting > 0 &&
	    per_cpu(rcu_preempt_data, cpu).qs_pending)
		t->rcu_read_unlock_special |= RCU_READ_UNLOCK_NEED_QS;
}

#ifdef CONFIG_RCU_BOOST

static void rcu_preempt_do_callbacks(void)
{
	rcu_do_batch(&rcu_preempt_state, this_cpu_ptr(&rcu_preempt_data));
}

#endif /* #ifdef CONFIG_RCU_BOOST */

/*
 * Queue a preemptible-RCU callback for invocation after a grace period.
 */
void call_rcu(struct rcu_head *head, void (*func)(struct rcu_head *rcu))
{
	__call_rcu(head, func, &rcu_preempt_state, -1, 0);
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
 *
 * See the description of synchronize_sched() for more detailed information
 * on memory ordering guarantees.
 */
void synchronize_rcu(void)
{
	rcu_lockdep_assert(!lock_is_held(&rcu_bh_lock_map) &&
			   !lock_is_held(&rcu_lock_map) &&
			   !lock_is_held(&rcu_sched_lock_map),
			   "Illegal synchronize_rcu() in RCU read-side critical section");
	if (!rcu_scheduler_active)
		return;
	if (rcu_expedited)
		synchronize_rcu_expedited();
	else
		wait_rcu_gp(call_rcu);
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
 * Most callers will set the "wake" flag, but the task initiating the
 * expedited grace period need not wake itself.
 *
 * Caller must hold sync_rcu_preempt_exp_mutex.
 */
static void rcu_report_exp_rnp(struct rcu_state *rsp, struct rcu_node *rnp,
			       bool wake)
{
	unsigned long flags;
	unsigned long mask;

	raw_spin_lock_irqsave(&rnp->lock, flags);
	smp_mb__after_unlock_lock();
	for (;;) {
		if (!sync_rcu_preempt_exp_done(rnp)) {
			raw_spin_unlock_irqrestore(&rnp->lock, flags);
			break;
		}
		if (rnp->parent == NULL) {
			raw_spin_unlock_irqrestore(&rnp->lock, flags);
			if (wake) {
				smp_mb(); /* EGP done before wake_up(). */
				wake_up(&sync_rcu_preempt_exp_wq);
			}
			break;
		}
		mask = rnp->grpmask;
		raw_spin_unlock(&rnp->lock); /* irqs remain disabled */
		rnp = rnp->parent;
		raw_spin_lock(&rnp->lock); /* irqs already disabled */
		smp_mb__after_unlock_lock();
		rnp->expmask &= ~mask;
	}
}

/*
 * Snapshot the tasks blocking the newly started preemptible-RCU expedited
 * grace period for the specified rcu_node structure.  If there are no such
 * tasks, report it up the rcu_node hierarchy.
 *
 * Caller must hold sync_rcu_preempt_exp_mutex and must exclude
 * CPU hotplug operations.
 */
static void
sync_rcu_preempt_exp_init(struct rcu_state *rsp, struct rcu_node *rnp)
{
	unsigned long flags;
	int must_wait = 0;

	raw_spin_lock_irqsave(&rnp->lock, flags);
	smp_mb__after_unlock_lock();
	if (list_empty(&rnp->blkd_tasks)) {
		raw_spin_unlock_irqrestore(&rnp->lock, flags);
	} else {
		rnp->exp_tasks = rnp->blkd_tasks.next;
		rcu_initiate_boost(rnp, flags);  /* releases rnp->lock */
		must_wait = 1;
	}
	if (!must_wait)
		rcu_report_exp_rnp(rsp, rnp, false); /* Don't wake self. */
}

/**
 * synchronize_rcu_expedited - Brute-force RCU grace period
 *
 * Wait for an RCU-preempt grace period, but expedite it.  The basic
 * idea is to invoke synchronize_sched_expedited() to push all the tasks to
 * the ->blkd_tasks lists and wait for this list to drain.  This consumes
 * significant time on all CPUs and is unfriendly to real-time workloads,
 * so is thus not recommended for any sort of common-case code.
 * In fact, if you are using synchronize_rcu_expedited() in a loop,
 * please restructure your code to batch your updates, and then Use a
 * single synchronize_rcu() instead.
 *
 * Note that it is illegal to call this function while holding any lock
 * that is acquired by a CPU-hotplug notifier.  And yes, it is also illegal
 * to call this function from a CPU-hotplug notifier.  Failing to observe
 * these restriction will result in deadlock.
 */
void synchronize_rcu_expedited(void)
{
	unsigned long flags;
	struct rcu_node *rnp;
	struct rcu_state *rsp = &rcu_preempt_state;
	unsigned long snap;
	int trycount = 0;

	smp_mb(); /* Caller's modifications seen first by other CPUs. */
	snap = ACCESS_ONCE(sync_rcu_preempt_exp_count) + 1;
	smp_mb(); /* Above access cannot bleed into critical section. */

	/*
	 * Block CPU-hotplug operations.  This means that any CPU-hotplug
	 * operation that finds an rcu_node structure with tasks in the
	 * process of being boosted will know that all tasks blocking
	 * this expedited grace period will already be in the process of
	 * being boosted.  This simplifies the process of moving tasks
	 * from leaf to root rcu_node structures.
	 */
	get_online_cpus();

	/*
	 * Acquire lock, falling back to synchronize_rcu() if too many
	 * lock-acquisition failures.  Of course, if someone does the
	 * expedited grace period for us, just leave.
	 */
	while (!mutex_trylock(&sync_rcu_preempt_exp_mutex)) {
		if (ULONG_CMP_LT(snap,
		    ACCESS_ONCE(sync_rcu_preempt_exp_count))) {
			put_online_cpus();
			goto mb_ret; /* Others did our work for us. */
		}
		if (trycount++ < 10) {
			udelay(trycount * num_online_cpus());
		} else {
			put_online_cpus();
			wait_rcu_gp(call_rcu);
			return;
		}
	}
	if (ULONG_CMP_LT(snap, ACCESS_ONCE(sync_rcu_preempt_exp_count))) {
		put_online_cpus();
		goto unlock_mb_ret; /* Others did our work for us. */
	}

	/* force all RCU readers onto ->blkd_tasks lists. */
	synchronize_sched_expedited();

	/* Initialize ->expmask for all non-leaf rcu_node structures. */
	rcu_for_each_nonleaf_node_breadth_first(rsp, rnp) {
		raw_spin_lock_irqsave(&rnp->lock, flags);
		smp_mb__after_unlock_lock();
		rnp->expmask = rnp->qsmaskinit;
		raw_spin_unlock_irqrestore(&rnp->lock, flags);
	}

	/* Snapshot current state of ->blkd_tasks lists. */
	rcu_for_each_leaf_node(rsp, rnp)
		sync_rcu_preempt_exp_init(rsp, rnp);
	if (NUM_RCU_NODES > 1)
		sync_rcu_preempt_exp_init(rsp, rcu_get_root(rsp));

	put_online_cpus();

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

/**
 * rcu_barrier - Wait until all in-flight call_rcu() callbacks complete.
 *
 * Note that this primitive does not necessarily wait for an RCU grace period
 * to complete.  For example, if there are no RCU callbacks queued anywhere
 * in the system, then rcu_barrier() is within its rights to return
 * immediately, without waiting for anything, much less an RCU grace period.
 */
void rcu_barrier(void)
{
	_rcu_barrier(&rcu_preempt_state);
}
EXPORT_SYMBOL_GPL(rcu_barrier);

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

	if (likely(list_empty(&current->rcu_node_entry)))
		return;
	t->rcu_read_lock_nesting = 1;
	barrier();
	t->rcu_read_unlock_special = RCU_READ_UNLOCK_BLOCKED;
	__rcu_read_unlock();
}

#else /* #ifdef CONFIG_TREE_PREEMPT_RCU */

static struct rcu_state *rcu_state_p = &rcu_sched_state;

/*
 * Tell them what RCU they are running.
 */
static void __init rcu_bootup_announce(void)
{
	pr_info("Hierarchical RCU implementation.\n");
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
static int rcu_print_task_stall(struct rcu_node *rnp)
{
	return 0;
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

#endif /* #ifdef CONFIG_HOTPLUG_CPU */

/*
 * Because preemptible RCU does not exist, it never has any callbacks
 * to check.
 */
static void rcu_preempt_check_callbacks(int cpu)
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
static void rcu_report_exp_rnp(struct rcu_state *rsp, struct rcu_node *rnp,
			       bool wake)
{
}

#endif /* #ifdef CONFIG_HOTPLUG_CPU */

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
 * Because preemptible RCU does not exist, it need not be initialized.
 */
static void __init __rcu_init_preempt(void)
{
}

/*
 * Because preemptible RCU does not exist, tasks cannot possibly exit
 * while in preemptible RCU read-side critical sections.
 */
void exit_rcu(void)
{
}

#endif /* #else #ifdef CONFIG_TREE_PREEMPT_RCU */

#ifdef CONFIG_RCU_BOOST

#include "../locking/rtmutex_common.h"

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

static void rcu_wake_cond(struct task_struct *t, int status)
{
	/*
	 * If the thread is yielding, only wake it when this
	 * is invoked from idle
	 */
	if (status != RCU_KTHREAD_YIELDING || is_idle_task(current))
		wake_up_process(t);
}

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
	smp_mb__after_unlock_lock();

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
	raw_spin_unlock_irqrestore(&rnp->lock, flags);
	rt_mutex_lock(&mtx);  /* Side effect: boosts task t's priority. */
	rt_mutex_unlock(&mtx);  /* Keep lockdep happy. */

	return ACCESS_ONCE(rnp->exp_tasks) != NULL ||
	       ACCESS_ONCE(rnp->boost_tasks) != NULL;
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

	trace_rcu_utilization(TPS("Start boost kthread@init"));
	for (;;) {
		rnp->boost_kthread_status = RCU_KTHREAD_WAITING;
		trace_rcu_utilization(TPS("End boost kthread@rcu_wait"));
		rcu_wait(rnp->boost_tasks || rnp->exp_tasks);
		trace_rcu_utilization(TPS("Start boost kthread@rcu_wait"));
		rnp->boost_kthread_status = RCU_KTHREAD_RUNNING;
		more2boost = rcu_boost(rnp);
		if (more2boost)
			spincnt++;
		else
			spincnt = 0;
		if (spincnt > 10) {
			rnp->boost_kthread_status = RCU_KTHREAD_YIELDING;
			trace_rcu_utilization(TPS("End boost kthread@rcu_yield"));
			schedule_timeout_interruptible(2);
			trace_rcu_utilization(TPS("Start boost kthread@rcu_yield"));
			spincnt = 0;
		}
	}
	/* NOTREACHED */
	trace_rcu_utilization(TPS("End boost kthread@notreached"));
	return 0;
}

/*
 * Check to see if it is time to start boosting RCU readers that are
 * blocking the current grace period, and, if so, tell the per-rcu_node
 * kthread to start boosting them.  If there is an expedited grace
 * period in progress, it is always time to boost.
 *
 * The caller must hold rnp->lock, which this function releases.
 * The ->boost_kthread_task is immortal, so we don't need to worry
 * about it going away.
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
		if (t)
			rcu_wake_cond(t, rnp->boost_kthread_status);
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
	if (__this_cpu_read(rcu_cpu_kthread_task) != NULL &&
	    current != __this_cpu_read(rcu_cpu_kthread_task)) {
		rcu_wake_cond(__this_cpu_read(rcu_cpu_kthread_task),
			      __this_cpu_read(rcu_cpu_kthread_status));
	}
	local_irq_restore(flags);
}

/*
 * Is the current CPU running the RCU-callbacks kthread?
 * Caller must have preemption disabled.
 */
static bool rcu_is_callbacks_kthread(void)
{
	return __this_cpu_read(rcu_cpu_kthread_task) == current;
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
static int rcu_spawn_one_boost_kthread(struct rcu_state *rsp,
						 struct rcu_node *rnp)
{
	int rnp_index = rnp - &rsp->node[0];
	unsigned long flags;
	struct sched_param sp;
	struct task_struct *t;

	if (&rcu_preempt_state != rsp)
		return 0;

	if (!rcu_scheduler_fully_active || rnp->qsmaskinit == 0)
		return 0;

	rsp->boost = 1;
	if (rnp->boost_kthread_task != NULL)
		return 0;
	t = kthread_create(rcu_boost_kthread, (void *)rnp,
			   "rcub/%d", rnp_index);
	if (IS_ERR(t))
		return PTR_ERR(t);
	raw_spin_lock_irqsave(&rnp->lock, flags);
	smp_mb__after_unlock_lock();
	rnp->boost_kthread_task = t;
	raw_spin_unlock_irqrestore(&rnp->lock, flags);
	sp.sched_priority = RCU_BOOST_PRIO;
	sched_setscheduler_nocheck(t, SCHED_FIFO, &sp);
	wake_up_process(t); /* get to TASK_INTERRUPTIBLE quickly. */
	return 0;
}

static void rcu_kthread_do_work(void)
{
	rcu_do_batch(&rcu_sched_state, this_cpu_ptr(&rcu_sched_data));
	rcu_do_batch(&rcu_bh_state, this_cpu_ptr(&rcu_bh_data));
	rcu_preempt_do_callbacks();
}

static void rcu_cpu_kthread_setup(unsigned int cpu)
{
	struct sched_param sp;

	sp.sched_priority = RCU_KTHREAD_PRIO;
	sched_setscheduler_nocheck(current, SCHED_FIFO, &sp);
}

static void rcu_cpu_kthread_park(unsigned int cpu)
{
	per_cpu(rcu_cpu_kthread_status, cpu) = RCU_KTHREAD_OFFCPU;
}

static int rcu_cpu_kthread_should_run(unsigned int cpu)
{
	return __this_cpu_read(rcu_cpu_has_work);
}

/*
 * Per-CPU kernel thread that invokes RCU callbacks.  This replaces the
 * RCU softirq used in flavors and configurations of RCU that do not
 * support RCU priority boosting.
 */
static void rcu_cpu_kthread(unsigned int cpu)
{
	unsigned int *statusp = this_cpu_ptr(&rcu_cpu_kthread_status);
	char work, *workp = this_cpu_ptr(&rcu_cpu_has_work);
	int spincnt;

	for (spincnt = 0; spincnt < 10; spincnt++) {
		trace_rcu_utilization(TPS("Start CPU kthread@rcu_wait"));
		local_bh_disable();
		*statusp = RCU_KTHREAD_RUNNING;
		this_cpu_inc(rcu_cpu_kthread_loops);
		local_irq_disable();
		work = *workp;
		*workp = 0;
		local_irq_enable();
		if (work)
			rcu_kthread_do_work();
		local_bh_enable();
		if (*workp == 0) {
			trace_rcu_utilization(TPS("End CPU kthread@rcu_wait"));
			*statusp = RCU_KTHREAD_WAITING;
			return;
		}
	}
	*statusp = RCU_KTHREAD_YIELDING;
	trace_rcu_utilization(TPS("Start CPU kthread@rcu_yield"));
	schedule_timeout_interruptible(2);
	trace_rcu_utilization(TPS("End CPU kthread@rcu_yield"));
	*statusp = RCU_KTHREAD_WAITING;
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
static void rcu_boost_kthread_setaffinity(struct rcu_node *rnp, int outgoingcpu)
{
	struct task_struct *t = rnp->boost_kthread_task;
	unsigned long mask = rnp->qsmaskinit;
	cpumask_var_t cm;
	int cpu;

	if (!t)
		return;
	if (!zalloc_cpumask_var(&cm, GFP_KERNEL))
		return;
	for (cpu = rnp->grplo; cpu <= rnp->grphi; cpu++, mask >>= 1)
		if ((mask & 0x1) && cpu != outgoingcpu)
			cpumask_set_cpu(cpu, cm);
	if (cpumask_weight(cm) == 0) {
		cpumask_setall(cm);
		for (cpu = rnp->grplo; cpu <= rnp->grphi; cpu++)
			cpumask_clear_cpu(cpu, cm);
		WARN_ON_ONCE(cpumask_weight(cm) == 0);
	}
	set_cpus_allowed_ptr(t, cm);
	free_cpumask_var(cm);
}

static struct smp_hotplug_thread rcu_cpu_thread_spec = {
	.store			= &rcu_cpu_kthread_task,
	.thread_should_run	= rcu_cpu_kthread_should_run,
	.thread_fn		= rcu_cpu_kthread,
	.thread_comm		= "rcuc/%u",
	.setup			= rcu_cpu_kthread_setup,
	.park			= rcu_cpu_kthread_park,
};

/*
 * Spawn all kthreads -- called as soon as the scheduler is running.
 */
static int __init rcu_spawn_kthreads(void)
{
	struct rcu_node *rnp;
	int cpu;

	rcu_scheduler_fully_active = 1;
	for_each_possible_cpu(cpu)
		per_cpu(rcu_cpu_has_work, cpu) = 0;
	BUG_ON(smpboot_register_percpu_thread(&rcu_cpu_thread_spec));
	rnp = rcu_get_root(rcu_state_p);
	(void)rcu_spawn_one_boost_kthread(rcu_state_p, rnp);
	if (NUM_RCU_NODES > 1) {
		rcu_for_each_leaf_node(rcu_state_p, rnp)
			(void)rcu_spawn_one_boost_kthread(rcu_state_p, rnp);
	}
	return 0;
}
early_initcall(rcu_spawn_kthreads);

static void rcu_prepare_kthreads(int cpu)
{
	struct rcu_data *rdp = per_cpu_ptr(rcu_state_p->rda, cpu);
	struct rcu_node *rnp = rdp->mynode;

	/* Fire up the incoming CPU's kthread and leaf rcu_node kthread. */
	if (rcu_scheduler_fully_active)
		(void)rcu_spawn_one_boost_kthread(rcu_state_p, rnp);
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

static bool rcu_is_callbacks_kthread(void)
{
	return false;
}

static void rcu_preempt_boost_start_gp(struct rcu_node *rnp)
{
}

static void rcu_boost_kthread_setaffinity(struct rcu_node *rnp, int outgoingcpu)
{
}

static int __init rcu_scheduler_really_started(void)
{
	rcu_scheduler_fully_active = 1;
	return 0;
}
early_initcall(rcu_scheduler_really_started);

static void rcu_prepare_kthreads(int cpu)
{
}

#endif /* #else #ifdef CONFIG_RCU_BOOST */

#if !defined(CONFIG_RCU_FAST_NO_HZ)

/*
 * Check to see if any future RCU-related work will need to be done
 * by the current CPU, even if none need be done immediately, returning
 * 1 if so.  This function is part of the RCU implementation; it is -not-
 * an exported member of the RCU API.
 *
 * Because we not have RCU_FAST_NO_HZ, just check whether this CPU needs
 * any flavor of RCU.
 */
#ifndef CONFIG_RCU_NOCB_CPU_ALL
int rcu_needs_cpu(int cpu, unsigned long *delta_jiffies)
{
	*delta_jiffies = ULONG_MAX;
	return rcu_cpu_has_callbacks(cpu, NULL);
}
#endif /* #ifndef CONFIG_RCU_NOCB_CPU_ALL */

/*
 * Because we do not have RCU_FAST_NO_HZ, don't bother cleaning up
 * after it.
 */
static void rcu_cleanup_after_idle(int cpu)
{
}

/*
 * Do the idle-entry grace-period work, which, because CONFIG_RCU_FAST_NO_HZ=n,
 * is nothing.
 */
static void rcu_prepare_for_idle(int cpu)
{
}

/*
 * Don't bother keeping a running count of the number of RCU callbacks
 * posted because CONFIG_RCU_FAST_NO_HZ=n.
 */
static void rcu_idle_count_callbacks_posted(void)
{
}

#else /* #if !defined(CONFIG_RCU_FAST_NO_HZ) */

/*
 * This code is invoked when a CPU goes idle, at which point we want
 * to have the CPU do everything required for RCU so that it can enter
 * the energy-efficient dyntick-idle mode.  This is handled by a
 * state machine implemented by rcu_prepare_for_idle() below.
 *
 * The following three proprocessor symbols control this state machine:
 *
 * RCU_IDLE_GP_DELAY gives the number of jiffies that a CPU is permitted
 *	to sleep in dyntick-idle mode with RCU callbacks pending.  This
 *	is sized to be roughly one RCU grace period.  Those energy-efficiency
 *	benchmarkers who might otherwise be tempted to set this to a large
 *	number, be warned: Setting RCU_IDLE_GP_DELAY too high can hang your
 *	system.  And if you are -that- concerned about energy efficiency,
 *	just power the system down and be done with it!
 * RCU_IDLE_LAZY_GP_DELAY gives the number of jiffies that a CPU is
 *	permitted to sleep in dyntick-idle mode with only lazy RCU
 *	callbacks pending.  Setting this too high can OOM your system.
 *
 * The values below work well in practice.  If future workloads require
 * adjustment, they can be converted into kernel config parameters, though
 * making the state machine smarter might be a better option.
 */
#define RCU_IDLE_GP_DELAY 4		/* Roughly one grace period. */
#define RCU_IDLE_LAZY_GP_DELAY (6 * HZ)	/* Roughly six seconds. */

static int rcu_idle_gp_delay = RCU_IDLE_GP_DELAY;
module_param(rcu_idle_gp_delay, int, 0644);
static int rcu_idle_lazy_gp_delay = RCU_IDLE_LAZY_GP_DELAY;
module_param(rcu_idle_lazy_gp_delay, int, 0644);

extern int tick_nohz_active;

/*
 * Try to advance callbacks for all flavors of RCU on the current CPU, but
 * only if it has been awhile since the last time we did so.  Afterwards,
 * if there are any callbacks ready for immediate invocation, return true.
 */
static bool __maybe_unused rcu_try_advance_all_cbs(void)
{
	bool cbs_ready = false;
	struct rcu_data *rdp;
	struct rcu_dynticks *rdtp = this_cpu_ptr(&rcu_dynticks);
	struct rcu_node *rnp;
	struct rcu_state *rsp;

	/* Exit early if we advanced recently. */
	if (jiffies == rdtp->last_advance_all)
		return 0;
	rdtp->last_advance_all = jiffies;

	for_each_rcu_flavor(rsp) {
		rdp = this_cpu_ptr(rsp->rda);
		rnp = rdp->mynode;

		/*
		 * Don't bother checking unless a grace period has
		 * completed since we last checked and there are
		 * callbacks not yet ready to invoke.
		 */
		if (rdp->completed != rnp->completed &&
		    rdp->nxttail[RCU_DONE_TAIL] != rdp->nxttail[RCU_NEXT_TAIL])
			note_gp_changes(rsp, rdp);

		if (cpu_has_callbacks_ready_to_invoke(rdp))
			cbs_ready = true;
	}
	return cbs_ready;
}

/*
 * Allow the CPU to enter dyntick-idle mode unless it has callbacks ready
 * to invoke.  If the CPU has callbacks, try to advance them.  Tell the
 * caller to set the timeout based on whether or not there are non-lazy
 * callbacks.
 *
 * The caller must have disabled interrupts.
 */
#ifndef CONFIG_RCU_NOCB_CPU_ALL
int rcu_needs_cpu(int cpu, unsigned long *dj)
{
	struct rcu_dynticks *rdtp = &per_cpu(rcu_dynticks, cpu);

	/* Snapshot to detect later posting of non-lazy callback. */
	rdtp->nonlazy_posted_snap = rdtp->nonlazy_posted;

	/* If no callbacks, RCU doesn't need the CPU. */
	if (!rcu_cpu_has_callbacks(cpu, &rdtp->all_lazy)) {
		*dj = ULONG_MAX;
		return 0;
	}

	/* Attempt to advance callbacks. */
	if (rcu_try_advance_all_cbs()) {
		/* Some ready to invoke, so initiate later invocation. */
		invoke_rcu_core();
		return 1;
	}
	rdtp->last_accelerate = jiffies;

	/* Request timer delay depending on laziness, and round. */
	if (!rdtp->all_lazy) {
		*dj = round_up(rcu_idle_gp_delay + jiffies,
			       rcu_idle_gp_delay) - jiffies;
	} else {
		*dj = round_jiffies(rcu_idle_lazy_gp_delay + jiffies) - jiffies;
	}
	return 0;
}
#endif /* #ifndef CONFIG_RCU_NOCB_CPU_ALL */

/*
 * Prepare a CPU for idle from an RCU perspective.  The first major task
 * is to sense whether nohz mode has been enabled or disabled via sysfs.
 * The second major task is to check to see if a non-lazy callback has
 * arrived at a CPU that previously had only lazy callbacks.  The third
 * major task is to accelerate (that is, assign grace-period numbers to)
 * any recently arrived callbacks.
 *
 * The caller must have disabled interrupts.
 */
static void rcu_prepare_for_idle(int cpu)
{
#ifndef CONFIG_RCU_NOCB_CPU_ALL
	bool needwake;
	struct rcu_data *rdp;
	struct rcu_dynticks *rdtp = &per_cpu(rcu_dynticks, cpu);
	struct rcu_node *rnp;
	struct rcu_state *rsp;
	int tne;

	/* Handle nohz enablement switches conservatively. */
	tne = ACCESS_ONCE(tick_nohz_active);
	if (tne != rdtp->tick_nohz_enabled_snap) {
		if (rcu_cpu_has_callbacks(cpu, NULL))
			invoke_rcu_core(); /* force nohz to see update. */
		rdtp->tick_nohz_enabled_snap = tne;
		return;
	}
	if (!tne)
		return;

	/* If this is a no-CBs CPU, no callbacks, just return. */
	if (rcu_is_nocb_cpu(cpu))
		return;

	/*
	 * If a non-lazy callback arrived at a CPU having only lazy
	 * callbacks, invoke RCU core for the side-effect of recalculating
	 * idle duration on re-entry to idle.
	 */
	if (rdtp->all_lazy &&
	    rdtp->nonlazy_posted != rdtp->nonlazy_posted_snap) {
		rdtp->all_lazy = false;
		rdtp->nonlazy_posted_snap = rdtp->nonlazy_posted;
		invoke_rcu_core();
		return;
	}

	/*
	 * If we have not yet accelerated this jiffy, accelerate all
	 * callbacks on this CPU.
	 */
	if (rdtp->last_accelerate == jiffies)
		return;
	rdtp->last_accelerate = jiffies;
	for_each_rcu_flavor(rsp) {
		rdp = per_cpu_ptr(rsp->rda, cpu);
		if (!*rdp->nxttail[RCU_DONE_TAIL])
			continue;
		rnp = rdp->mynode;
		raw_spin_lock(&rnp->lock); /* irqs already disabled. */
		smp_mb__after_unlock_lock();
		needwake = rcu_accelerate_cbs(rsp, rnp, rdp);
		raw_spin_unlock(&rnp->lock); /* irqs remain disabled. */
		if (needwake)
			rcu_gp_kthread_wake(rsp);
	}
#endif /* #ifndef CONFIG_RCU_NOCB_CPU_ALL */
}

/*
 * Clean up for exit from idle.  Attempt to advance callbacks based on
 * any grace periods that elapsed while the CPU was idle, and if any
 * callbacks are now ready to invoke, initiate invocation.
 */
static void rcu_cleanup_after_idle(int cpu)
{
#ifndef CONFIG_RCU_NOCB_CPU_ALL
	if (rcu_is_nocb_cpu(cpu))
		return;
	if (rcu_try_advance_all_cbs())
		invoke_rcu_core();
#endif /* #ifndef CONFIG_RCU_NOCB_CPU_ALL */
}

/*
 * Keep a running count of the number of non-lazy callbacks posted
 * on this CPU.  This running counter (which is never decremented) allows
 * rcu_prepare_for_idle() to detect when something out of the idle loop
 * posts a callback, even if an equal number of callbacks are invoked.
 * Of course, callbacks should only be posted from within a trace event
 * designed to be called from idle or from within RCU_NONIDLE().
 */
static void rcu_idle_count_callbacks_posted(void)
{
	__this_cpu_add(rcu_dynticks.nonlazy_posted, 1);
}

/*
 * Data for flushing lazy RCU callbacks at OOM time.
 */
static atomic_t oom_callback_count;
static DECLARE_WAIT_QUEUE_HEAD(oom_callback_wq);

/*
 * RCU OOM callback -- decrement the outstanding count and deliver the
 * wake-up if we are the last one.
 */
static void rcu_oom_callback(struct rcu_head *rhp)
{
	if (atomic_dec_and_test(&oom_callback_count))
		wake_up(&oom_callback_wq);
}

/*
 * Post an rcu_oom_notify callback on the current CPU if it has at
 * least one lazy callback.  This will unnecessarily post callbacks
 * to CPUs that already have a non-lazy callback at the end of their
 * callback list, but this is an infrequent operation, so accept some
 * extra overhead to keep things simple.
 */
static void rcu_oom_notify_cpu(void *unused)
{
	struct rcu_state *rsp;
	struct rcu_data *rdp;

	for_each_rcu_flavor(rsp) {
		rdp = raw_cpu_ptr(rsp->rda);
		if (rdp->qlen_lazy != 0) {
			atomic_inc(&oom_callback_count);
			rsp->call(&rdp->oom_head, rcu_oom_callback);
		}
	}
}

/*
 * If low on memory, ensure that each CPU has a non-lazy callback.
 * This will wake up CPUs that have only lazy callbacks, in turn
 * ensuring that they free up the corresponding memory in a timely manner.
 * Because an uncertain amount of memory will be freed in some uncertain
 * timeframe, we do not claim to have freed anything.
 */
static int rcu_oom_notify(struct notifier_block *self,
			  unsigned long notused, void *nfreed)
{
	int cpu;

	/* Wait for callbacks from earlier instance to complete. */
	wait_event(oom_callback_wq, atomic_read(&oom_callback_count) == 0);
	smp_mb(); /* Ensure callback reuse happens after callback invocation. */

	/*
	 * Prevent premature wakeup: ensure that all increments happen
	 * before there is a chance of the counter reaching zero.
	 */
	atomic_set(&oom_callback_count, 1);

	get_online_cpus();
	for_each_online_cpu(cpu) {
		smp_call_function_single(cpu, rcu_oom_notify_cpu, NULL, 1);
		cond_resched();
	}
	put_online_cpus();

	/* Unconditionally decrement: no need to wake ourselves up. */
	atomic_dec(&oom_callback_count);

	return NOTIFY_OK;
}

static struct notifier_block rcu_oom_nb = {
	.notifier_call = rcu_oom_notify
};

static int __init rcu_register_oom_notifier(void)
{
	register_oom_notifier(&rcu_oom_nb);
	return 0;
}
early_initcall(rcu_register_oom_notifier);

#endif /* #else #if !defined(CONFIG_RCU_FAST_NO_HZ) */

#ifdef CONFIG_RCU_CPU_STALL_INFO

#ifdef CONFIG_RCU_FAST_NO_HZ

static void print_cpu_stall_fast_no_hz(char *cp, int cpu)
{
	struct rcu_dynticks *rdtp = &per_cpu(rcu_dynticks, cpu);
	unsigned long nlpd = rdtp->nonlazy_posted - rdtp->nonlazy_posted_snap;

	sprintf(cp, "last_accelerate: %04lx/%04lx, nonlazy_posted: %ld, %c%c",
		rdtp->last_accelerate & 0xffff, jiffies & 0xffff,
		ulong2long(nlpd),
		rdtp->all_lazy ? 'L' : '.',
		rdtp->tick_nohz_enabled_snap ? '.' : 'D');
}

#else /* #ifdef CONFIG_RCU_FAST_NO_HZ */

static void print_cpu_stall_fast_no_hz(char *cp, int cpu)
{
	*cp = '\0';
}

#endif /* #else #ifdef CONFIG_RCU_FAST_NO_HZ */

/* Initiate the stall-info list. */
static void print_cpu_stall_info_begin(void)
{
	pr_cont("\n");
}

/*
 * Print out diagnostic information for the specified stalled CPU.
 *
 * If the specified CPU is aware of the current RCU grace period
 * (flavor specified by rsp), then print the number of scheduling
 * clock interrupts the CPU has taken during the time that it has
 * been aware.  Otherwise, print the number of RCU grace periods
 * that this CPU is ignorant of, for example, "1" if the CPU was
 * aware of the previous grace period.
 *
 * Also print out idle and (if CONFIG_RCU_FAST_NO_HZ) idle-entry info.
 */
static void print_cpu_stall_info(struct rcu_state *rsp, int cpu)
{
	char fast_no_hz[72];
	struct rcu_data *rdp = per_cpu_ptr(rsp->rda, cpu);
	struct rcu_dynticks *rdtp = rdp->dynticks;
	char *ticks_title;
	unsigned long ticks_value;

	if (rsp->gpnum == rdp->gpnum) {
		ticks_title = "ticks this GP";
		ticks_value = rdp->ticks_this_gp;
	} else {
		ticks_title = "GPs behind";
		ticks_value = rsp->gpnum - rdp->gpnum;
	}
	print_cpu_stall_fast_no_hz(fast_no_hz, cpu);
	pr_err("\t%d: (%lu %s) idle=%03x/%llx/%d softirq=%u/%u %s\n",
	       cpu, ticks_value, ticks_title,
	       atomic_read(&rdtp->dynticks) & 0xfff,
	       rdtp->dynticks_nesting, rdtp->dynticks_nmi_nesting,
	       rdp->softirq_snap, kstat_softirqs_cpu(RCU_SOFTIRQ, cpu),
	       fast_no_hz);
}

/* Terminate the stall-info list. */
static void print_cpu_stall_info_end(void)
{
	pr_err("\t");
}

/* Zero ->ticks_this_gp for all flavors of RCU. */
static void zero_cpu_stall_ticks(struct rcu_data *rdp)
{
	rdp->ticks_this_gp = 0;
	rdp->softirq_snap = kstat_softirqs_cpu(RCU_SOFTIRQ, smp_processor_id());
}

/* Increment ->ticks_this_gp for all flavors of RCU. */
static void increment_cpu_stall_ticks(void)
{
	struct rcu_state *rsp;

	for_each_rcu_flavor(rsp)
		raw_cpu_inc(rsp->rda->ticks_this_gp);
}

#else /* #ifdef CONFIG_RCU_CPU_STALL_INFO */

static void print_cpu_stall_info_begin(void)
{
	pr_cont(" {");
}

static void print_cpu_stall_info(struct rcu_state *rsp, int cpu)
{
	pr_cont(" %d", cpu);
}

static void print_cpu_stall_info_end(void)
{
	pr_cont("} ");
}

static void zero_cpu_stall_ticks(struct rcu_data *rdp)
{
}

static void increment_cpu_stall_ticks(void)
{
}

#endif /* #else #ifdef CONFIG_RCU_CPU_STALL_INFO */

#ifdef CONFIG_RCU_NOCB_CPU

/*
 * Offload callback processing from the boot-time-specified set of CPUs
 * specified by rcu_nocb_mask.  For each CPU in the set, there is a
 * kthread created that pulls the callbacks from the corresponding CPU,
 * waits for a grace period to elapse, and invokes the callbacks.
 * The no-CBs CPUs do a wake_up() on their kthread when they insert
 * a callback into any empty list, unless the rcu_nocb_poll boot parameter
 * has been specified, in which case each kthread actively polls its
 * CPU.  (Which isn't so great for energy efficiency, but which does
 * reduce RCU's overhead on that CPU.)
 *
 * This is intended to be used in conjunction with Frederic Weisbecker's
 * adaptive-idle work, which would seriously reduce OS jitter on CPUs
 * running CPU-bound user-mode computations.
 *
 * Offloading of callback processing could also in theory be used as
 * an energy-efficiency measure because CPUs with no RCU callbacks
 * queued are more aggressive about entering dyntick-idle mode.
 */


/* Parse the boot-time rcu_nocb_mask CPU list from the kernel parameters. */
static int __init rcu_nocb_setup(char *str)
{
	alloc_bootmem_cpumask_var(&rcu_nocb_mask);
	have_rcu_nocb_mask = true;
	cpulist_parse(str, rcu_nocb_mask);
	return 1;
}
__setup("rcu_nocbs=", rcu_nocb_setup);

static int __init parse_rcu_nocb_poll(char *arg)
{
	rcu_nocb_poll = 1;
	return 0;
}
early_param("rcu_nocb_poll", parse_rcu_nocb_poll);

/*
 * Wake up any no-CBs CPUs' kthreads that were waiting on the just-ended
 * grace period.
 */
static void rcu_nocb_gp_cleanup(struct rcu_state *rsp, struct rcu_node *rnp)
{
	wake_up_all(&rnp->nocb_gp_wq[rnp->completed & 0x1]);
}

/*
 * Set the root rcu_node structure's ->need_future_gp field
 * based on the sum of those of all rcu_node structures.  This does
 * double-count the root rcu_node structure's requests, but this
 * is necessary to handle the possibility of a rcu_nocb_kthread()
 * having awakened during the time that the rcu_node structures
 * were being updated for the end of the previous grace period.
 */
static void rcu_nocb_gp_set(struct rcu_node *rnp, int nrq)
{
	rnp->need_future_gp[(rnp->completed + 1) & 0x1] += nrq;
}

static void rcu_init_one_nocb(struct rcu_node *rnp)
{
	init_waitqueue_head(&rnp->nocb_gp_wq[0]);
	init_waitqueue_head(&rnp->nocb_gp_wq[1]);
}

#ifndef CONFIG_RCU_NOCB_CPU_ALL
/* Is the specified CPU a no-CBs CPU? */
bool rcu_is_nocb_cpu(int cpu)
{
	if (have_rcu_nocb_mask)
		return cpumask_test_cpu(cpu, rcu_nocb_mask);
	return false;
}
#endif /* #ifndef CONFIG_RCU_NOCB_CPU_ALL */

/*
 * Kick the leader kthread for this NOCB group.
 */
static void wake_nocb_leader(struct rcu_data *rdp, bool force)
{
	struct rcu_data *rdp_leader = rdp->nocb_leader;

	if (!ACCESS_ONCE(rdp_leader->nocb_kthread))
		return;
	if (!ACCESS_ONCE(rdp_leader->nocb_leader_wake) || force) {
		/* Prior xchg orders against prior callback enqueue. */
		ACCESS_ONCE(rdp_leader->nocb_leader_wake) = true;
		wake_up(&rdp_leader->nocb_wq);
	}
}

/*
 * Enqueue the specified string of rcu_head structures onto the specified
 * CPU's no-CBs lists.  The CPU is specified by rdp, the head of the
 * string by rhp, and the tail of the string by rhtp.  The non-lazy/lazy
 * counts are supplied by rhcount and rhcount_lazy.
 *
 * If warranted, also wake up the kthread servicing this CPUs queues.
 */
static void __call_rcu_nocb_enqueue(struct rcu_data *rdp,
				    struct rcu_head *rhp,
				    struct rcu_head **rhtp,
				    int rhcount, int rhcount_lazy,
				    unsigned long flags)
{
	int len;
	struct rcu_head **old_rhpp;
	struct task_struct *t;

	/* Enqueue the callback on the nocb list and update counts. */
	old_rhpp = xchg(&rdp->nocb_tail, rhtp);
	ACCESS_ONCE(*old_rhpp) = rhp;
	atomic_long_add(rhcount, &rdp->nocb_q_count);
	atomic_long_add(rhcount_lazy, &rdp->nocb_q_count_lazy);

	/* If we are not being polled and there is a kthread, awaken it ... */
	t = ACCESS_ONCE(rdp->nocb_kthread);
	if (rcu_nocb_poll || !t) {
		trace_rcu_nocb_wake(rdp->rsp->name, rdp->cpu,
				    TPS("WakeNotPoll"));
		return;
	}
	len = atomic_long_read(&rdp->nocb_q_count);
	if (old_rhpp == &rdp->nocb_head) {
		if (!irqs_disabled_flags(flags)) {
			/* ... if queue was empty ... */
			wake_nocb_leader(rdp, false);
			trace_rcu_nocb_wake(rdp->rsp->name, rdp->cpu,
					    TPS("WakeEmpty"));
		} else {
			rdp->nocb_defer_wakeup = true;
			trace_rcu_nocb_wake(rdp->rsp->name, rdp->cpu,
					    TPS("WakeEmptyIsDeferred"));
		}
		rdp->qlen_last_fqs_check = 0;
	} else if (len > rdp->qlen_last_fqs_check + qhimark) {
		/* ... or if many callbacks queued. */
		wake_nocb_leader(rdp, true);
		rdp->qlen_last_fqs_check = LONG_MAX / 2;
		trace_rcu_nocb_wake(rdp->rsp->name, rdp->cpu, TPS("WakeOvf"));
	} else {
		trace_rcu_nocb_wake(rdp->rsp->name, rdp->cpu, TPS("WakeNot"));
	}
	return;
}

/*
 * This is a helper for __call_rcu(), which invokes this when the normal
 * callback queue is inoperable.  If this is not a no-CBs CPU, this
 * function returns failure back to __call_rcu(), which can complain
 * appropriately.
 *
 * Otherwise, this function queues the callback where the corresponding
 * "rcuo" kthread can find it.
 */
static bool __call_rcu_nocb(struct rcu_data *rdp, struct rcu_head *rhp,
			    bool lazy, unsigned long flags)
{

	if (!rcu_is_nocb_cpu(rdp->cpu))
		return 0;
	__call_rcu_nocb_enqueue(rdp, rhp, &rhp->next, 1, lazy, flags);
	if (__is_kfree_rcu_offset((unsigned long)rhp->func))
		trace_rcu_kfree_callback(rdp->rsp->name, rhp,
					 (unsigned long)rhp->func,
					 -atomic_long_read(&rdp->nocb_q_count_lazy),
					 -atomic_long_read(&rdp->nocb_q_count));
	else
		trace_rcu_callback(rdp->rsp->name, rhp,
				   -atomic_long_read(&rdp->nocb_q_count_lazy),
				   -atomic_long_read(&rdp->nocb_q_count));
	return 1;
}

/*
 * Adopt orphaned callbacks on a no-CBs CPU, or return 0 if this is
 * not a no-CBs CPU.
 */
static bool __maybe_unused rcu_nocb_adopt_orphan_cbs(struct rcu_state *rsp,
						     struct rcu_data *rdp,
						     unsigned long flags)
{
	long ql = rsp->qlen;
	long qll = rsp->qlen_lazy;

	/* If this is not a no-CBs CPU, tell the caller to do it the old way. */
	if (!rcu_is_nocb_cpu(smp_processor_id()))
		return 0;
	rsp->qlen = 0;
	rsp->qlen_lazy = 0;

	/* First, enqueue the donelist, if any.  This preserves CB ordering. */
	if (rsp->orphan_donelist != NULL) {
		__call_rcu_nocb_enqueue(rdp, rsp->orphan_donelist,
					rsp->orphan_donetail, ql, qll, flags);
		ql = qll = 0;
		rsp->orphan_donelist = NULL;
		rsp->orphan_donetail = &rsp->orphan_donelist;
	}
	if (rsp->orphan_nxtlist != NULL) {
		__call_rcu_nocb_enqueue(rdp, rsp->orphan_nxtlist,
					rsp->orphan_nxttail, ql, qll, flags);
		ql = qll = 0;
		rsp->orphan_nxtlist = NULL;
		rsp->orphan_nxttail = &rsp->orphan_nxtlist;
	}
	return 1;
}

/*
 * If necessary, kick off a new grace period, and either way wait
 * for a subsequent grace period to complete.
 */
static void rcu_nocb_wait_gp(struct rcu_data *rdp)
{
	unsigned long c;
	bool d;
	unsigned long flags;
	bool needwake;
	struct rcu_node *rnp = rdp->mynode;

	raw_spin_lock_irqsave(&rnp->lock, flags);
	smp_mb__after_unlock_lock();
	needwake = rcu_start_future_gp(rnp, rdp, &c);
	raw_spin_unlock_irqrestore(&rnp->lock, flags);
	if (needwake)
		rcu_gp_kthread_wake(rdp->rsp);

	/*
	 * Wait for the grace period.  Do so interruptibly to avoid messing
	 * up the load average.
	 */
	trace_rcu_future_gp(rnp, rdp, c, TPS("StartWait"));
	for (;;) {
		wait_event_interruptible(
			rnp->nocb_gp_wq[c & 0x1],
			(d = ULONG_CMP_GE(ACCESS_ONCE(rnp->completed), c)));
		if (likely(d))
			break;
		flush_signals(current);
		trace_rcu_future_gp(rnp, rdp, c, TPS("ResumeWait"));
	}
	trace_rcu_future_gp(rnp, rdp, c, TPS("EndWait"));
	smp_mb(); /* Ensure that CB invocation happens after GP end. */
}

/*
 * Leaders come here to wait for additional callbacks to show up.
 * This function does not return until callbacks appear.
 */
static void nocb_leader_wait(struct rcu_data *my_rdp)
{
	bool firsttime = true;
	bool gotcbs;
	struct rcu_data *rdp;
	struct rcu_head **tail;

wait_again:

	/* Wait for callbacks to appear. */
	if (!rcu_nocb_poll) {
		trace_rcu_nocb_wake(my_rdp->rsp->name, my_rdp->cpu, "Sleep");
		wait_event_interruptible(my_rdp->nocb_wq,
					 ACCESS_ONCE(my_rdp->nocb_leader_wake));
		/* Memory barrier handled by smp_mb() calls below and repoll. */
	} else if (firsttime) {
		firsttime = false; /* Don't drown trace log with "Poll"! */
		trace_rcu_nocb_wake(my_rdp->rsp->name, my_rdp->cpu, "Poll");
	}

	/*
	 * Each pass through the following loop checks a follower for CBs.
	 * We are our own first follower.  Any CBs found are moved to
	 * nocb_gp_head, where they await a grace period.
	 */
	gotcbs = false;
	for (rdp = my_rdp; rdp; rdp = rdp->nocb_next_follower) {
		rdp->nocb_gp_head = ACCESS_ONCE(rdp->nocb_head);
		if (!rdp->nocb_gp_head)
			continue;  /* No CBs here, try next follower. */

		/* Move callbacks to wait-for-GP list, which is empty. */
		ACCESS_ONCE(rdp->nocb_head) = NULL;
		rdp->nocb_gp_tail = xchg(&rdp->nocb_tail, &rdp->nocb_head);
		rdp->nocb_gp_count = atomic_long_xchg(&rdp->nocb_q_count, 0);
		rdp->nocb_gp_count_lazy =
			atomic_long_xchg(&rdp->nocb_q_count_lazy, 0);
		gotcbs = true;
	}

	/*
	 * If there were no callbacks, sleep a bit, rescan after a
	 * memory barrier, and go retry.
	 */
	if (unlikely(!gotcbs)) {
		if (!rcu_nocb_poll)
			trace_rcu_nocb_wake(my_rdp->rsp->name, my_rdp->cpu,
					    "WokeEmpty");
		flush_signals(current);
		schedule_timeout_interruptible(1);

		/* Rescan in case we were a victim of memory ordering. */
		my_rdp->nocb_leader_wake = false;
		smp_mb();  /* Ensure _wake false before scan. */
		for (rdp = my_rdp; rdp; rdp = rdp->nocb_next_follower)
			if (ACCESS_ONCE(rdp->nocb_head)) {
				/* Found CB, so short-circuit next wait. */
				my_rdp->nocb_leader_wake = true;
				break;
			}
		goto wait_again;
	}

	/* Wait for one grace period. */
	rcu_nocb_wait_gp(my_rdp);

	/*
	 * We left ->nocb_leader_wake set to reduce cache thrashing.
	 * We clear it now, but recheck for new callbacks while
	 * traversing our follower list.
	 */
	my_rdp->nocb_leader_wake = false;
	smp_mb(); /* Ensure _wake false before scan of ->nocb_head. */

	/* Each pass through the following loop wakes a follower, if needed. */
	for (rdp = my_rdp; rdp; rdp = rdp->nocb_next_follower) {
		if (ACCESS_ONCE(rdp->nocb_head))
			my_rdp->nocb_leader_wake = true; /* No need to wait. */
		if (!rdp->nocb_gp_head)
			continue; /* No CBs, so no need to wake follower. */

		/* Append callbacks to follower's "done" list. */
		tail = xchg(&rdp->nocb_follower_tail, rdp->nocb_gp_tail);
		*tail = rdp->nocb_gp_head;
		atomic_long_add(rdp->nocb_gp_count, &rdp->nocb_follower_count);
		atomic_long_add(rdp->nocb_gp_count_lazy,
				&rdp->nocb_follower_count_lazy);
		if (rdp != my_rdp && tail == &rdp->nocb_follower_head) {
			/*
			 * List was empty, wake up the follower.
			 * Memory barriers supplied by atomic_long_add().
			 */
			wake_up(&rdp->nocb_wq);
		}
	}

	/* If we (the leader) don't have CBs, go wait some more. */
	if (!my_rdp->nocb_follower_head)
		goto wait_again;
}

/*
 * Followers come here to wait for additional callbacks to show up.
 * This function does not return until callbacks appear.
 */
static void nocb_follower_wait(struct rcu_data *rdp)
{
	bool firsttime = true;

	for (;;) {
		if (!rcu_nocb_poll) {
			trace_rcu_nocb_wake(rdp->rsp->name, rdp->cpu,
					    "FollowerSleep");
			wait_event_interruptible(rdp->nocb_wq,
						 ACCESS_ONCE(rdp->nocb_follower_head));
		} else if (firsttime) {
			/* Don't drown trace log with "Poll"! */
			firsttime = false;
			trace_rcu_nocb_wake(rdp->rsp->name, rdp->cpu, "Poll");
		}
		if (smp_load_acquire(&rdp->nocb_follower_head)) {
			/* ^^^ Ensure CB invocation follows _head test. */
			return;
		}
		if (!rcu_nocb_poll)
			trace_rcu_nocb_wake(rdp->rsp->name, rdp->cpu,
					    "WokeEmpty");
		flush_signals(current);
		schedule_timeout_interruptible(1);
	}
}

/*
 * Per-rcu_data kthread, but only for no-CBs CPUs.  Each kthread invokes
 * callbacks queued by the corresponding no-CBs CPU, however, there is
 * an optional leader-follower relationship so that the grace-period
 * kthreads don't have to do quite so many wakeups.
 */
static int rcu_nocb_kthread(void *arg)
{
	int c, cl;
	struct rcu_head *list;
	struct rcu_head *next;
	struct rcu_head **tail;
	struct rcu_data *rdp = arg;

	/* Each pass through this loop invokes one batch of callbacks */
	for (;;) {
		/* Wait for callbacks. */
		if (rdp->nocb_leader == rdp)
			nocb_leader_wait(rdp);
		else
			nocb_follower_wait(rdp);

		/* Pull the ready-to-invoke callbacks onto local list. */
		list = ACCESS_ONCE(rdp->nocb_follower_head);
		BUG_ON(!list);
		trace_rcu_nocb_wake(rdp->rsp->name, rdp->cpu, "WokeNonEmpty");
		ACCESS_ONCE(rdp->nocb_follower_head) = NULL;
		tail = xchg(&rdp->nocb_follower_tail, &rdp->nocb_follower_head);
		c = atomic_long_xchg(&rdp->nocb_follower_count, 0);
		cl = atomic_long_xchg(&rdp->nocb_follower_count_lazy, 0);
		rdp->nocb_p_count += c;
		rdp->nocb_p_count_lazy += cl;

		/* Each pass through the following loop invokes a callback. */
		trace_rcu_batch_start(rdp->rsp->name, cl, c, -1);
		c = cl = 0;
		while (list) {
			next = list->next;
			/* Wait for enqueuing to complete, if needed. */
			while (next == NULL && &list->next != tail) {
				trace_rcu_nocb_wake(rdp->rsp->name, rdp->cpu,
						    TPS("WaitQueue"));
				schedule_timeout_interruptible(1);
				trace_rcu_nocb_wake(rdp->rsp->name, rdp->cpu,
						    TPS("WokeQueue"));
				next = list->next;
			}
			debug_rcu_head_unqueue(list);
			local_bh_disable();
			if (__rcu_reclaim(rdp->rsp->name, list))
				cl++;
			c++;
			local_bh_enable();
			list = next;
		}
		trace_rcu_batch_end(rdp->rsp->name, c, !!list, 0, 0, 1);
		ACCESS_ONCE(rdp->nocb_p_count) -= c;
		ACCESS_ONCE(rdp->nocb_p_count_lazy) -= cl;
		rdp->n_nocbs_invoked += c;
	}
	return 0;
}

/* Is a deferred wakeup of rcu_nocb_kthread() required? */
static bool rcu_nocb_need_deferred_wakeup(struct rcu_data *rdp)
{
	return ACCESS_ONCE(rdp->nocb_defer_wakeup);
}

/* Do a deferred wakeup of rcu_nocb_kthread(). */
static void do_nocb_deferred_wakeup(struct rcu_data *rdp)
{
	if (!rcu_nocb_need_deferred_wakeup(rdp))
		return;
	ACCESS_ONCE(rdp->nocb_defer_wakeup) = false;
	wake_nocb_leader(rdp, false);
	trace_rcu_nocb_wake(rdp->rsp->name, rdp->cpu, TPS("DeferredWakeEmpty"));
}

/* Initialize per-rcu_data variables for no-CBs CPUs. */
static void __init rcu_boot_init_nocb_percpu_data(struct rcu_data *rdp)
{
	rdp->nocb_tail = &rdp->nocb_head;
	init_waitqueue_head(&rdp->nocb_wq);
	rdp->nocb_follower_tail = &rdp->nocb_follower_head;
}

/* How many follower CPU IDs per leader?  Default of -1 for sqrt(nr_cpu_ids). */
static int rcu_nocb_leader_stride = -1;
module_param(rcu_nocb_leader_stride, int, 0444);

/*
 * Create a kthread for each RCU flavor for each no-CBs CPU.
 * Also initialize leader-follower relationships.
 */
static void __init rcu_spawn_nocb_kthreads(struct rcu_state *rsp)
{
	int cpu;
	int ls = rcu_nocb_leader_stride;
	int nl = 0;  /* Next leader. */
	struct rcu_data *rdp;
	struct rcu_data *rdp_leader = NULL;  /* Suppress misguided gcc warn. */
	struct rcu_data *rdp_prev = NULL;
	struct task_struct *t;

	if (rcu_nocb_mask == NULL)
		return;
#ifdef CONFIG_NO_HZ_FULL
	cpumask_or(rcu_nocb_mask, rcu_nocb_mask, tick_nohz_full_mask);
#endif /* #ifdef CONFIG_NO_HZ_FULL */
	if (ls == -1) {
		ls = int_sqrt(nr_cpu_ids);
		rcu_nocb_leader_stride = ls;
	}

	/*
	 * Each pass through this loop sets up one rcu_data structure and
	 * spawns one rcu_nocb_kthread().
	 */
	for_each_cpu(cpu, rcu_nocb_mask) {
		rdp = per_cpu_ptr(rsp->rda, cpu);
		if (rdp->cpu >= nl) {
			/* New leader, set up for followers & next leader. */
			nl = DIV_ROUND_UP(rdp->cpu + 1, ls) * ls;
			rdp->nocb_leader = rdp;
			rdp_leader = rdp;
		} else {
			/* Another follower, link to previous leader. */
			rdp->nocb_leader = rdp_leader;
			rdp_prev->nocb_next_follower = rdp;
		}
		rdp_prev = rdp;

		/* Spawn the kthread for this CPU. */
		t = kthread_run(rcu_nocb_kthread, rdp,
				"rcuo%c/%d", rsp->abbr, cpu);
		BUG_ON(IS_ERR(t));
		ACCESS_ONCE(rdp->nocb_kthread) = t;
	}
}

/* Prevent __call_rcu() from enqueuing callbacks on no-CBs CPUs */
static bool init_nocb_callback_list(struct rcu_data *rdp)
{
	if (rcu_nocb_mask == NULL ||
	    !cpumask_test_cpu(rdp->cpu, rcu_nocb_mask))
		return false;
	rdp->nxttail[RCU_NEXT_TAIL] = NULL;
	return true;
}

#else /* #ifdef CONFIG_RCU_NOCB_CPU */

static void rcu_nocb_gp_cleanup(struct rcu_state *rsp, struct rcu_node *rnp)
{
}

static void rcu_nocb_gp_set(struct rcu_node *rnp, int nrq)
{
}

static void rcu_init_one_nocb(struct rcu_node *rnp)
{
}

static bool __call_rcu_nocb(struct rcu_data *rdp, struct rcu_head *rhp,
			    bool lazy, unsigned long flags)
{
	return 0;
}

static bool __maybe_unused rcu_nocb_adopt_orphan_cbs(struct rcu_state *rsp,
						     struct rcu_data *rdp,
						     unsigned long flags)
{
	return 0;
}

static void __init rcu_boot_init_nocb_percpu_data(struct rcu_data *rdp)
{
}

static bool rcu_nocb_need_deferred_wakeup(struct rcu_data *rdp)
{
	return false;
}

static void do_nocb_deferred_wakeup(struct rcu_data *rdp)
{
}

static void __init rcu_spawn_nocb_kthreads(struct rcu_state *rsp)
{
}

static bool init_nocb_callback_list(struct rcu_data *rdp)
{
	return false;
}

#endif /* #else #ifdef CONFIG_RCU_NOCB_CPU */

/*
 * An adaptive-ticks CPU can potentially execute in kernel mode for an
 * arbitrarily long period of time with the scheduling-clock tick turned
 * off.  RCU will be paying attention to this CPU because it is in the
 * kernel, but the CPU cannot be guaranteed to be executing the RCU state
 * machine because the scheduling-clock tick has been disabled.  Therefore,
 * if an adaptive-ticks CPU is failing to respond to the current grace
 * period and has not be idle from an RCU perspective, kick it.
 */
static void __maybe_unused rcu_kick_nohz_cpu(int cpu)
{
#ifdef CONFIG_NO_HZ_FULL
	if (tick_nohz_full_cpu(cpu))
		smp_send_reschedule(cpu);
#endif /* #ifdef CONFIG_NO_HZ_FULL */
}


#ifdef CONFIG_NO_HZ_FULL_SYSIDLE

/*
 * Define RCU flavor that holds sysidle state.  This needs to be the
 * most active flavor of RCU.
 */
#ifdef CONFIG_PREEMPT_RCU
static struct rcu_state *rcu_sysidle_state = &rcu_preempt_state;
#else /* #ifdef CONFIG_PREEMPT_RCU */
static struct rcu_state *rcu_sysidle_state = &rcu_sched_state;
#endif /* #else #ifdef CONFIG_PREEMPT_RCU */

static int full_sysidle_state;		/* Current system-idle state. */
#define RCU_SYSIDLE_NOT		0	/* Some CPU is not idle. */
#define RCU_SYSIDLE_SHORT	1	/* All CPUs idle for brief period. */
#define RCU_SYSIDLE_LONG	2	/* All CPUs idle for long enough. */
#define RCU_SYSIDLE_FULL	3	/* All CPUs idle, ready for sysidle. */
#define RCU_SYSIDLE_FULL_NOTED	4	/* Actually entered sysidle state. */

/*
 * Invoked to note exit from irq or task transition to idle.  Note that
 * usermode execution does -not- count as idle here!  After all, we want
 * to detect full-system idle states, not RCU quiescent states and grace
 * periods.  The caller must have disabled interrupts.
 */
static void rcu_sysidle_enter(struct rcu_dynticks *rdtp, int irq)
{
	unsigned long j;

	/* Adjust nesting, check for fully idle. */
	if (irq) {
		rdtp->dynticks_idle_nesting--;
		WARN_ON_ONCE(rdtp->dynticks_idle_nesting < 0);
		if (rdtp->dynticks_idle_nesting != 0)
			return;  /* Still not fully idle. */
	} else {
		if ((rdtp->dynticks_idle_nesting & DYNTICK_TASK_NEST_MASK) ==
		    DYNTICK_TASK_NEST_VALUE) {
			rdtp->dynticks_idle_nesting = 0;
		} else {
			rdtp->dynticks_idle_nesting -= DYNTICK_TASK_NEST_VALUE;
			WARN_ON_ONCE(rdtp->dynticks_idle_nesting < 0);
			return;  /* Still not fully idle. */
		}
	}

	/* Record start of fully idle period. */
	j = jiffies;
	ACCESS_ONCE(rdtp->dynticks_idle_jiffies) = j;
	smp_mb__before_atomic();
	atomic_inc(&rdtp->dynticks_idle);
	smp_mb__after_atomic();
	WARN_ON_ONCE(atomic_read(&rdtp->dynticks_idle) & 0x1);
}

/*
 * Unconditionally force exit from full system-idle state.  This is
 * invoked when a normal CPU exits idle, but must be called separately
 * for the timekeeping CPU (tick_do_timer_cpu).  The reason for this
 * is that the timekeeping CPU is permitted to take scheduling-clock
 * interrupts while the system is in system-idle state, and of course
 * rcu_sysidle_exit() has no way of distinguishing a scheduling-clock
 * interrupt from any other type of interrupt.
 */
void rcu_sysidle_force_exit(void)
{
	int oldstate = ACCESS_ONCE(full_sysidle_state);
	int newoldstate;

	/*
	 * Each pass through the following loop attempts to exit full
	 * system-idle state.  If contention proves to be a problem,
	 * a trylock-based contention tree could be used here.
	 */
	while (oldstate > RCU_SYSIDLE_SHORT) {
		newoldstate = cmpxchg(&full_sysidle_state,
				      oldstate, RCU_SYSIDLE_NOT);
		if (oldstate == newoldstate &&
		    oldstate == RCU_SYSIDLE_FULL_NOTED) {
			rcu_kick_nohz_cpu(tick_do_timer_cpu);
			return; /* We cleared it, done! */
		}
		oldstate = newoldstate;
	}
	smp_mb(); /* Order initial oldstate fetch vs. later non-idle work. */
}

/*
 * Invoked to note entry to irq or task transition from idle.  Note that
 * usermode execution does -not- count as idle here!  The caller must
 * have disabled interrupts.
 */
static void rcu_sysidle_exit(struct rcu_dynticks *rdtp, int irq)
{
	/* Adjust nesting, check for already non-idle. */
	if (irq) {
		rdtp->dynticks_idle_nesting++;
		WARN_ON_ONCE(rdtp->dynticks_idle_nesting <= 0);
		if (rdtp->dynticks_idle_nesting != 1)
			return; /* Already non-idle. */
	} else {
		/*
		 * Allow for irq misnesting.  Yes, it really is possible
		 * to enter an irq handler then never leave it, and maybe
		 * also vice versa.  Handle both possibilities.
		 */
		if (rdtp->dynticks_idle_nesting & DYNTICK_TASK_NEST_MASK) {
			rdtp->dynticks_idle_nesting += DYNTICK_TASK_NEST_VALUE;
			WARN_ON_ONCE(rdtp->dynticks_idle_nesting <= 0);
			return; /* Already non-idle. */
		} else {
			rdtp->dynticks_idle_nesting = DYNTICK_TASK_EXIT_IDLE;
		}
	}

	/* Record end of idle period. */
	smp_mb__before_atomic();
	atomic_inc(&rdtp->dynticks_idle);
	smp_mb__after_atomic();
	WARN_ON_ONCE(!(atomic_read(&rdtp->dynticks_idle) & 0x1));

	/*
	 * If we are the timekeeping CPU, we are permitted to be non-idle
	 * during a system-idle state.  This must be the case, because
	 * the timekeeping CPU has to take scheduling-clock interrupts
	 * during the time that the system is transitioning to full
	 * system-idle state.  This means that the timekeeping CPU must
	 * invoke rcu_sysidle_force_exit() directly if it does anything
	 * more than take a scheduling-clock interrupt.
	 */
	if (smp_processor_id() == tick_do_timer_cpu)
		return;

	/* Update system-idle state: We are clearly no longer fully idle! */
	rcu_sysidle_force_exit();
}

/*
 * Check to see if the current CPU is idle.  Note that usermode execution
 * does not count as idle.  The caller must have disabled interrupts.
 */
static void rcu_sysidle_check_cpu(struct rcu_data *rdp, bool *isidle,
				  unsigned long *maxj)
{
	int cur;
	unsigned long j;
	struct rcu_dynticks *rdtp = rdp->dynticks;

	/*
	 * If some other CPU has already reported non-idle, if this is
	 * not the flavor of RCU that tracks sysidle state, or if this
	 * is an offline or the timekeeping CPU, nothing to do.
	 */
	if (!*isidle || rdp->rsp != rcu_sysidle_state ||
	    cpu_is_offline(rdp->cpu) || rdp->cpu == tick_do_timer_cpu)
		return;
	if (rcu_gp_in_progress(rdp->rsp))
		WARN_ON_ONCE(smp_processor_id() != tick_do_timer_cpu);

	/* Pick up current idle and NMI-nesting counter and check. */
	cur = atomic_read(&rdtp->dynticks_idle);
	if (cur & 0x1) {
		*isidle = false; /* We are not idle! */
		return;
	}
	smp_mb(); /* Read counters before timestamps. */

	/* Pick up timestamps. */
	j = ACCESS_ONCE(rdtp->dynticks_idle_jiffies);
	/* If this CPU entered idle more recently, update maxj timestamp. */
	if (ULONG_CMP_LT(*maxj, j))
		*maxj = j;
}

/*
 * Is this the flavor of RCU that is handling full-system idle?
 */
static bool is_sysidle_rcu_state(struct rcu_state *rsp)
{
	return rsp == rcu_sysidle_state;
}

/*
 * Return a delay in jiffies based on the number of CPUs, rcu_node
 * leaf fanout, and jiffies tick rate.  The idea is to allow larger
 * systems more time to transition to full-idle state in order to
 * avoid the cache thrashing that otherwise occur on the state variable.
 * Really small systems (less than a couple of tens of CPUs) should
 * instead use a single global atomically incremented counter, and later
 * versions of this will automatically reconfigure themselves accordingly.
 */
static unsigned long rcu_sysidle_delay(void)
{
	if (nr_cpu_ids <= CONFIG_NO_HZ_FULL_SYSIDLE_SMALL)
		return 0;
	return DIV_ROUND_UP(nr_cpu_ids * HZ, rcu_fanout_leaf * 1000);
}

/*
 * Advance the full-system-idle state.  This is invoked when all of
 * the non-timekeeping CPUs are idle.
 */
static void rcu_sysidle(unsigned long j)
{
	/* Check the current state. */
	switch (ACCESS_ONCE(full_sysidle_state)) {
	case RCU_SYSIDLE_NOT:

		/* First time all are idle, so note a short idle period. */
		ACCESS_ONCE(full_sysidle_state) = RCU_SYSIDLE_SHORT;
		break;

	case RCU_SYSIDLE_SHORT:

		/*
		 * Idle for a bit, time to advance to next state?
		 * cmpxchg failure means race with non-idle, let them win.
		 */
		if (ULONG_CMP_GE(jiffies, j + rcu_sysidle_delay()))
			(void)cmpxchg(&full_sysidle_state,
				      RCU_SYSIDLE_SHORT, RCU_SYSIDLE_LONG);
		break;

	case RCU_SYSIDLE_LONG:

		/*
		 * Do an additional check pass before advancing to full.
		 * cmpxchg failure means race with non-idle, let them win.
		 */
		if (ULONG_CMP_GE(jiffies, j + rcu_sysidle_delay()))
			(void)cmpxchg(&full_sysidle_state,
				      RCU_SYSIDLE_LONG, RCU_SYSIDLE_FULL);
		break;

	default:
		break;
	}
}

/*
 * Found a non-idle non-timekeeping CPU, so kick the system-idle state
 * back to the beginning.
 */
static void rcu_sysidle_cancel(void)
{
	smp_mb();
	if (full_sysidle_state > RCU_SYSIDLE_SHORT)
		ACCESS_ONCE(full_sysidle_state) = RCU_SYSIDLE_NOT;
}

/*
 * Update the sysidle state based on the results of a force-quiescent-state
 * scan of the CPUs' dyntick-idle state.
 */
static void rcu_sysidle_report(struct rcu_state *rsp, int isidle,
			       unsigned long maxj, bool gpkt)
{
	if (rsp != rcu_sysidle_state)
		return;  /* Wrong flavor, ignore. */
	if (gpkt && nr_cpu_ids <= CONFIG_NO_HZ_FULL_SYSIDLE_SMALL)
		return;  /* Running state machine from timekeeping CPU. */
	if (isidle)
		rcu_sysidle(maxj);    /* More idle! */
	else
		rcu_sysidle_cancel(); /* Idle is over. */
}

/*
 * Wrapper for rcu_sysidle_report() when called from the grace-period
 * kthread's context.
 */
static void rcu_sysidle_report_gp(struct rcu_state *rsp, int isidle,
				  unsigned long maxj)
{
	rcu_sysidle_report(rsp, isidle, maxj, true);
}

/* Callback and function for forcing an RCU grace period. */
struct rcu_sysidle_head {
	struct rcu_head rh;
	int inuse;
};

static void rcu_sysidle_cb(struct rcu_head *rhp)
{
	struct rcu_sysidle_head *rshp;

	/*
	 * The following memory barrier is needed to replace the
	 * memory barriers that would normally be in the memory
	 * allocator.
	 */
	smp_mb();  /* grace period precedes setting inuse. */

	rshp = container_of(rhp, struct rcu_sysidle_head, rh);
	ACCESS_ONCE(rshp->inuse) = 0;
}

/*
 * Check to see if the system is fully idle, other than the timekeeping CPU.
 * The caller must have disabled interrupts.
 */
bool rcu_sys_is_idle(void)
{
	static struct rcu_sysidle_head rsh;
	int rss = ACCESS_ONCE(full_sysidle_state);

	if (WARN_ON_ONCE(smp_processor_id() != tick_do_timer_cpu))
		return false;

	/* Handle small-system case by doing a full scan of CPUs. */
	if (nr_cpu_ids <= CONFIG_NO_HZ_FULL_SYSIDLE_SMALL) {
		int oldrss = rss - 1;

		/*
		 * One pass to advance to each state up to _FULL.
		 * Give up if any pass fails to advance the state.
		 */
		while (rss < RCU_SYSIDLE_FULL && oldrss < rss) {
			int cpu;
			bool isidle = true;
			unsigned long maxj = jiffies - ULONG_MAX / 4;
			struct rcu_data *rdp;

			/* Scan all the CPUs looking for nonidle CPUs. */
			for_each_possible_cpu(cpu) {
				rdp = per_cpu_ptr(rcu_sysidle_state->rda, cpu);
				rcu_sysidle_check_cpu(rdp, &isidle, &maxj);
				if (!isidle)
					break;
			}
			rcu_sysidle_report(rcu_sysidle_state,
					   isidle, maxj, false);
			oldrss = rss;
			rss = ACCESS_ONCE(full_sysidle_state);
		}
	}

	/* If this is the first observation of an idle period, record it. */
	if (rss == RCU_SYSIDLE_FULL) {
		rss = cmpxchg(&full_sysidle_state,
			      RCU_SYSIDLE_FULL, RCU_SYSIDLE_FULL_NOTED);
		return rss == RCU_SYSIDLE_FULL;
	}

	smp_mb(); /* ensure rss load happens before later caller actions. */

	/* If already fully idle, tell the caller (in case of races). */
	if (rss == RCU_SYSIDLE_FULL_NOTED)
		return true;

	/*
	 * If we aren't there yet, and a grace period is not in flight,
	 * initiate a grace period.  Either way, tell the caller that
	 * we are not there yet.  We use an xchg() rather than an assignment
	 * to make up for the memory barriers that would otherwise be
	 * provided by the memory allocator.
	 */
	if (nr_cpu_ids > CONFIG_NO_HZ_FULL_SYSIDLE_SMALL &&
	    !rcu_gp_in_progress(rcu_sysidle_state) &&
	    !rsh.inuse && xchg(&rsh.inuse, 1) == 0)
		call_rcu(&rsh.rh, rcu_sysidle_cb);
	return false;
}

/*
 * Initialize dynticks sysidle state for CPUs coming online.
 */
static void rcu_sysidle_init_percpu_data(struct rcu_dynticks *rdtp)
{
	rdtp->dynticks_idle_nesting = DYNTICK_TASK_NEST_VALUE;
}

#else /* #ifdef CONFIG_NO_HZ_FULL_SYSIDLE */

static void rcu_sysidle_enter(struct rcu_dynticks *rdtp, int irq)
{
}

static void rcu_sysidle_exit(struct rcu_dynticks *rdtp, int irq)
{
}

static void rcu_sysidle_check_cpu(struct rcu_data *rdp, bool *isidle,
				  unsigned long *maxj)
{
}

static bool is_sysidle_rcu_state(struct rcu_state *rsp)
{
	return false;
}

static void rcu_sysidle_report_gp(struct rcu_state *rsp, int isidle,
				  unsigned long maxj)
{
}

static void rcu_sysidle_init_percpu_data(struct rcu_dynticks *rdtp)
{
}

#endif /* #else #ifdef CONFIG_NO_HZ_FULL_SYSIDLE */

/*
 * Is this CPU a NO_HZ_FULL CPU that should ignore RCU so that the
 * grace-period kthread will do force_quiescent_state() processing?
 * The idea is to avoid waking up RCU core processing on such a
 * CPU unless the grace period has extended for too long.
 *
 * This code relies on the fact that all NO_HZ_FULL CPUs are also
 * CONFIG_RCU_NOCB_CPU CPUs.
 */
static bool rcu_nohz_full_cpu(struct rcu_state *rsp)
{
#ifdef CONFIG_NO_HZ_FULL
	if (tick_nohz_full_cpu(smp_processor_id()) &&
	    (!rcu_gp_in_progress(rsp) ||
	     ULONG_CMP_LT(jiffies, ACCESS_ONCE(rsp->gp_start) + HZ)))
		return 1;
#endif /* #ifdef CONFIG_NO_HZ_FULL */
	return 0;
}

/*
 * Bind the grace-period kthread for the sysidle flavor of RCU to the
 * timekeeping CPU.
 */
static void rcu_bind_gp_kthread(void)
{
#ifdef CONFIG_NO_HZ_FULL
	int cpu = ACCESS_ONCE(tick_do_timer_cpu);

	if (cpu < 0 || cpu >= nr_cpu_ids)
		return;
	if (raw_smp_processor_id() != cpu)
		set_cpus_allowed_ptr(current, cpumask_of(cpu));
#endif /* #ifdef CONFIG_NO_HZ_FULL */
}
