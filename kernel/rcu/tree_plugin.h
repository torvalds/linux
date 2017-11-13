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
#include <linux/sched/debug.h>
#include <linux/smpboot.h>
#include <uapi/linux/sched/types.h>
#include "../time/tick-internal.h"

#ifdef CONFIG_RCU_BOOST

#include "../locking/rtmutex_common.h"

/*
 * Control variables for per-CPU and per-rcu_node kthreads.  These
 * handle all flavors of RCU.
 */
static DEFINE_PER_CPU(struct task_struct *, rcu_cpu_kthread_task);
DEFINE_PER_CPU(unsigned int, rcu_cpu_kthread_status);
DEFINE_PER_CPU(unsigned int, rcu_cpu_kthread_loops);
DEFINE_PER_CPU(char, rcu_cpu_has_work);

#else /* #ifdef CONFIG_RCU_BOOST */

/*
 * Some architectures do not define rt_mutexes, but if !CONFIG_RCU_BOOST,
 * all uses are in dead code.  Provide a definition to keep the compiler
 * happy, but add WARN_ON_ONCE() to complain if used in the wrong place.
 * This probably needs to be excluded from -rt builds.
 */
#define rt_mutex_owner(a) ({ WARN_ON_ONCE(1); NULL; })

#endif /* #else #ifdef CONFIG_RCU_BOOST */

#ifdef CONFIG_RCU_NOCB_CPU
static cpumask_var_t rcu_nocb_mask; /* CPUs to have callbacks offloaded. */
static bool have_rcu_nocb_mask;	    /* Was rcu_nocb_mask allocated? */
static bool __read_mostly rcu_nocb_poll;    /* Offload kthread are to poll. */
#endif /* #ifdef CONFIG_RCU_NOCB_CPU */

/*
 * Check the RCU kernel configuration parameters and print informative
 * messages about anything out of the ordinary.
 */
static void __init rcu_bootup_announce_oddness(void)
{
	if (IS_ENABLED(CONFIG_RCU_TRACE))
		pr_info("\tRCU event tracing is enabled.\n");
	if ((IS_ENABLED(CONFIG_64BIT) && RCU_FANOUT != 64) ||
	    (!IS_ENABLED(CONFIG_64BIT) && RCU_FANOUT != 32))
		pr_info("\tCONFIG_RCU_FANOUT set to non-default value of %d\n",
		       RCU_FANOUT);
	if (rcu_fanout_exact)
		pr_info("\tHierarchical RCU autobalancing is disabled.\n");
	if (IS_ENABLED(CONFIG_RCU_FAST_NO_HZ))
		pr_info("\tRCU dyntick-idle grace-period acceleration is enabled.\n");
	if (IS_ENABLED(CONFIG_PROVE_RCU))
		pr_info("\tRCU lockdep checking is enabled.\n");
	if (RCU_NUM_LVLS >= 4)
		pr_info("\tFour(or more)-level hierarchy is enabled.\n");
	if (RCU_FANOUT_LEAF != 16)
		pr_info("\tBuild-time adjustment of leaf fanout to %d.\n",
			RCU_FANOUT_LEAF);
	if (rcu_fanout_leaf != RCU_FANOUT_LEAF)
		pr_info("\tBoot-time adjustment of leaf fanout to %d.\n", rcu_fanout_leaf);
	if (nr_cpu_ids != NR_CPUS)
		pr_info("\tRCU restricting CPUs from NR_CPUS=%d to nr_cpu_ids=%u.\n", NR_CPUS, nr_cpu_ids);
#ifdef CONFIG_RCU_BOOST
	pr_info("\tRCU priority boosting: priority %d delay %d ms.\n", kthread_prio, CONFIG_RCU_BOOST_DELAY);
#endif
	if (blimit != DEFAULT_RCU_BLIMIT)
		pr_info("\tBoot-time adjustment of callback invocation limit to %ld.\n", blimit);
	if (qhimark != DEFAULT_RCU_QHIMARK)
		pr_info("\tBoot-time adjustment of callback high-water mark to %ld.\n", qhimark);
	if (qlowmark != DEFAULT_RCU_QLOMARK)
		pr_info("\tBoot-time adjustment of callback low-water mark to %ld.\n", qlowmark);
	if (jiffies_till_first_fqs != ULONG_MAX)
		pr_info("\tBoot-time adjustment of first FQS scan delay to %ld jiffies.\n", jiffies_till_first_fqs);
	if (jiffies_till_next_fqs != ULONG_MAX)
		pr_info("\tBoot-time adjustment of subsequent FQS scan delay to %ld jiffies.\n", jiffies_till_next_fqs);
	if (rcu_kick_kthreads)
		pr_info("\tKick kthreads if too-long grace period.\n");
	if (IS_ENABLED(CONFIG_DEBUG_OBJECTS_RCU_HEAD))
		pr_info("\tRCU callback double-/use-after-free debug enabled.\n");
	if (gp_preinit_delay)
		pr_info("\tRCU debug GP pre-init slowdown %d jiffies.\n", gp_preinit_delay);
	if (gp_init_delay)
		pr_info("\tRCU debug GP init slowdown %d jiffies.\n", gp_init_delay);
	if (gp_cleanup_delay)
		pr_info("\tRCU debug GP init slowdown %d jiffies.\n", gp_cleanup_delay);
	if (IS_ENABLED(CONFIG_RCU_EQS_DEBUG))
		pr_info("\tRCU debug extended QS entry/exit.\n");
	rcupdate_announce_bootup_oddness();
}

#ifdef CONFIG_PREEMPT_RCU

RCU_STATE_INITIALIZER(rcu_preempt, 'p', call_rcu);
static struct rcu_state *const rcu_state_p = &rcu_preempt_state;
static struct rcu_data __percpu *const rcu_data_p = &rcu_preempt_data;

static void rcu_report_exp_rnp(struct rcu_state *rsp, struct rcu_node *rnp,
			       bool wake);

/*
 * Tell them what RCU they are running.
 */
static void __init rcu_bootup_announce(void)
{
	pr_info("Preemptible hierarchical RCU implementation.\n");
	rcu_bootup_announce_oddness();
}

/* Flags for rcu_preempt_ctxt_queue() decision table. */
#define RCU_GP_TASKS	0x8
#define RCU_EXP_TASKS	0x4
#define RCU_GP_BLKD	0x2
#define RCU_EXP_BLKD	0x1

/*
 * Queues a task preempted within an RCU-preempt read-side critical
 * section into the appropriate location within the ->blkd_tasks list,
 * depending on the states of any ongoing normal and expedited grace
 * periods.  The ->gp_tasks pointer indicates which element the normal
 * grace period is waiting on (NULL if none), and the ->exp_tasks pointer
 * indicates which element the expedited grace period is waiting on (again,
 * NULL if none).  If a grace period is waiting on a given element in the
 * ->blkd_tasks list, it also waits on all subsequent elements.  Thus,
 * adding a task to the tail of the list blocks any grace period that is
 * already waiting on one of the elements.  In contrast, adding a task
 * to the head of the list won't block any grace period that is already
 * waiting on one of the elements.
 *
 * This queuing is imprecise, and can sometimes make an ongoing grace
 * period wait for a task that is not strictly speaking blocking it.
 * Given the choice, we needlessly block a normal grace period rather than
 * blocking an expedited grace period.
 *
 * Note that an endless sequence of expedited grace periods still cannot
 * indefinitely postpone a normal grace period.  Eventually, all of the
 * fixed number of preempted tasks blocking the normal grace period that are
 * not also blocking the expedited grace period will resume and complete
 * their RCU read-side critical sections.  At that point, the ->gp_tasks
 * pointer will equal the ->exp_tasks pointer, at which point the end of
 * the corresponding expedited grace period will also be the end of the
 * normal grace period.
 */
static void rcu_preempt_ctxt_queue(struct rcu_node *rnp, struct rcu_data *rdp)
	__releases(rnp->lock) /* But leaves rrupts disabled. */
{
	int blkd_state = (rnp->gp_tasks ? RCU_GP_TASKS : 0) +
			 (rnp->exp_tasks ? RCU_EXP_TASKS : 0) +
			 (rnp->qsmask & rdp->grpmask ? RCU_GP_BLKD : 0) +
			 (rnp->expmask & rdp->grpmask ? RCU_EXP_BLKD : 0);
	struct task_struct *t = current;

	lockdep_assert_held(&rnp->lock);
	WARN_ON_ONCE(rdp->mynode != rnp);
	WARN_ON_ONCE(rnp->level != rcu_num_lvls - 1);

	/*
	 * Decide where to queue the newly blocked task.  In theory,
	 * this could be an if-statement.  In practice, when I tried
	 * that, it was quite messy.
	 */
	switch (blkd_state) {
	case 0:
	case                RCU_EXP_TASKS:
	case                RCU_EXP_TASKS + RCU_GP_BLKD:
	case RCU_GP_TASKS:
	case RCU_GP_TASKS + RCU_EXP_TASKS:

		/*
		 * Blocking neither GP, or first task blocking the normal
		 * GP but not blocking the already-waiting expedited GP.
		 * Queue at the head of the list to avoid unnecessarily
		 * blocking the already-waiting GPs.
		 */
		list_add(&t->rcu_node_entry, &rnp->blkd_tasks);
		break;

	case                                              RCU_EXP_BLKD:
	case                                RCU_GP_BLKD:
	case                                RCU_GP_BLKD + RCU_EXP_BLKD:
	case RCU_GP_TASKS +                               RCU_EXP_BLKD:
	case RCU_GP_TASKS +                 RCU_GP_BLKD + RCU_EXP_BLKD:
	case RCU_GP_TASKS + RCU_EXP_TASKS + RCU_GP_BLKD + RCU_EXP_BLKD:

		/*
		 * First task arriving that blocks either GP, or first task
		 * arriving that blocks the expedited GP (with the normal
		 * GP already waiting), or a task arriving that blocks
		 * both GPs with both GPs already waiting.  Queue at the
		 * tail of the list to avoid any GP waiting on any of the
		 * already queued tasks that are not blocking it.
		 */
		list_add_tail(&t->rcu_node_entry, &rnp->blkd_tasks);
		break;

	case                RCU_EXP_TASKS +               RCU_EXP_BLKD:
	case                RCU_EXP_TASKS + RCU_GP_BLKD + RCU_EXP_BLKD:
	case RCU_GP_TASKS + RCU_EXP_TASKS +               RCU_EXP_BLKD:

		/*
		 * Second or subsequent task blocking the expedited GP.
		 * The task either does not block the normal GP, or is the
		 * first task blocking the normal GP.  Queue just after
		 * the first task blocking the expedited GP.
		 */
		list_add(&t->rcu_node_entry, rnp->exp_tasks);
		break;

	case RCU_GP_TASKS +                 RCU_GP_BLKD:
	case RCU_GP_TASKS + RCU_EXP_TASKS + RCU_GP_BLKD:

		/*
		 * Second or subsequent task blocking the normal GP.
		 * The task does not block the expedited GP. Queue just
		 * after the first task blocking the normal GP.
		 */
		list_add(&t->rcu_node_entry, rnp->gp_tasks);
		break;

	default:

		/* Yet another exercise in excessive paranoia. */
		WARN_ON_ONCE(1);
		break;
	}

	/*
	 * We have now queued the task.  If it was the first one to
	 * block either grace period, update the ->gp_tasks and/or
	 * ->exp_tasks pointers, respectively, to reference the newly
	 * blocked tasks.
	 */
	if (!rnp->gp_tasks && (blkd_state & RCU_GP_BLKD))
		rnp->gp_tasks = &t->rcu_node_entry;
	if (!rnp->exp_tasks && (blkd_state & RCU_EXP_BLKD))
		rnp->exp_tasks = &t->rcu_node_entry;
	WARN_ON_ONCE(!(blkd_state & RCU_GP_BLKD) !=
		     !(rnp->qsmask & rdp->grpmask));
	WARN_ON_ONCE(!(blkd_state & RCU_EXP_BLKD) !=
		     !(rnp->expmask & rdp->grpmask));
	raw_spin_unlock_rcu_node(rnp); /* interrupts remain disabled. */

	/*
	 * Report the quiescent state for the expedited GP.  This expedited
	 * GP should not be able to end until we report, so there should be
	 * no need to check for a subsequent expedited GP.  (Though we are
	 * still in a quiescent state in any case.)
	 */
	if (blkd_state & RCU_EXP_BLKD &&
	    t->rcu_read_unlock_special.b.exp_need_qs) {
		t->rcu_read_unlock_special.b.exp_need_qs = false;
		rcu_report_exp_rdp(rdp->rsp, rdp, true);
	} else {
		WARN_ON_ONCE(t->rcu_read_unlock_special.b.exp_need_qs);
	}
}

/*
 * Record a preemptible-RCU quiescent state for the specified CPU.  Note
 * that this just means that the task currently running on the CPU is
 * not in a quiescent state.  There might be any number of tasks blocked
 * while in an RCU read-side critical section.
 *
 * As with the other rcu_*_qs() functions, callers to this function
 * must disable preemption.
 */
static void rcu_preempt_qs(void)
{
	RCU_LOCKDEP_WARN(preemptible(), "rcu_preempt_qs() invoked with preemption enabled!!!\n");
	if (__this_cpu_read(rcu_data_p->cpu_no_qs.s)) {
		trace_rcu_grace_period(TPS("rcu_preempt"),
				       __this_cpu_read(rcu_data_p->gpnum),
				       TPS("cpuqs"));
		__this_cpu_write(rcu_data_p->cpu_no_qs.b.norm, false);
		barrier(); /* Coordinate with rcu_preempt_check_callbacks(). */
		current->rcu_read_unlock_special.b.need_qs = false;
	}
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
 * Caller must disable interrupts.
 */
static void rcu_preempt_note_context_switch(bool preempt)
{
	struct task_struct *t = current;
	struct rcu_data *rdp;
	struct rcu_node *rnp;

	RCU_LOCKDEP_WARN(!irqs_disabled(), "rcu_preempt_note_context_switch() invoked with interrupts enabled!!!\n");
	WARN_ON_ONCE(!preempt && t->rcu_read_lock_nesting > 0);
	if (t->rcu_read_lock_nesting > 0 &&
	    !t->rcu_read_unlock_special.b.blocked) {

		/* Possibly blocking in an RCU read-side critical section. */
		rdp = this_cpu_ptr(rcu_state_p->rda);
		rnp = rdp->mynode;
		raw_spin_lock_rcu_node(rnp);
		t->rcu_read_unlock_special.b.blocked = true;
		t->rcu_blocked_node = rnp;

		/*
		 * Verify the CPU's sanity, trace the preemption, and
		 * then queue the task as required based on the states
		 * of any ongoing and expedited grace periods.
		 */
		WARN_ON_ONCE((rdp->grpmask & rcu_rnp_online_cpus(rnp)) == 0);
		WARN_ON_ONCE(!list_empty(&t->rcu_node_entry));
		trace_rcu_preempt_task(rdp->rsp->name,
				       t->pid,
				       (rnp->qsmask & rdp->grpmask)
				       ? rnp->gpnum
				       : rnp->gpnum + 1);
		rcu_preempt_ctxt_queue(rnp, rdp);
	} else if (t->rcu_read_lock_nesting < 0 &&
		   t->rcu_read_unlock_special.s) {

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
	rcu_preempt_qs();
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
 * Return true if the specified rcu_node structure has tasks that were
 * preempted within an RCU read-side critical section.
 */
static bool rcu_preempt_has_tasks(struct rcu_node *rnp)
{
	return !list_empty(&rnp->blkd_tasks);
}

/*
 * Handle special cases during rcu_read_unlock(), such as needing to
 * notify RCU core processing or task having blocked during the RCU
 * read-side critical section.
 */
void rcu_read_unlock_special(struct task_struct *t)
{
	bool empty_exp;
	bool empty_norm;
	bool empty_exp_now;
	unsigned long flags;
	struct list_head *np;
	bool drop_boost_mutex = false;
	struct rcu_data *rdp;
	struct rcu_node *rnp;
	union rcu_special special;

	/* NMI handlers cannot block and cannot safely manipulate state. */
	if (in_nmi())
		return;

	local_irq_save(flags);

	/*
	 * If RCU core is waiting for this CPU to exit its critical section,
	 * report the fact that it has exited.  Because irqs are disabled,
	 * t->rcu_read_unlock_special cannot change.
	 */
	special = t->rcu_read_unlock_special;
	if (special.b.need_qs) {
		rcu_preempt_qs();
		t->rcu_read_unlock_special.b.need_qs = false;
		if (!t->rcu_read_unlock_special.s) {
			local_irq_restore(flags);
			return;
		}
	}

	/*
	 * Respond to a request for an expedited grace period, but only if
	 * we were not preempted, meaning that we were running on the same
	 * CPU throughout.  If we were preempted, the exp_need_qs flag
	 * would have been cleared at the time of the first preemption,
	 * and the quiescent state would be reported when we were dequeued.
	 */
	if (special.b.exp_need_qs) {
		WARN_ON_ONCE(special.b.blocked);
		t->rcu_read_unlock_special.b.exp_need_qs = false;
		rdp = this_cpu_ptr(rcu_state_p->rda);
		rcu_report_exp_rdp(rcu_state_p, rdp, true);
		if (!t->rcu_read_unlock_special.s) {
			local_irq_restore(flags);
			return;
		}
	}

	/* Hardware IRQ handlers cannot block, complain if they get here. */
	if (in_irq() || in_serving_softirq()) {
		lockdep_rcu_suspicious(__FILE__, __LINE__,
				       "rcu_read_unlock() from irq or softirq with blocking in critical section!!!\n");
		pr_alert("->rcu_read_unlock_special: %#x (b: %d, enq: %d nq: %d)\n",
			 t->rcu_read_unlock_special.s,
			 t->rcu_read_unlock_special.b.blocked,
			 t->rcu_read_unlock_special.b.exp_need_qs,
			 t->rcu_read_unlock_special.b.need_qs);
		local_irq_restore(flags);
		return;
	}

	/* Clean up if blocked during RCU read-side critical section. */
	if (special.b.blocked) {
		t->rcu_read_unlock_special.b.blocked = false;

		/*
		 * Remove this task from the list it blocked on.  The task
		 * now remains queued on the rcu_node corresponding to the
		 * CPU it first blocked on, so there is no longer any need
		 * to loop.  Retain a WARN_ON_ONCE() out of sheer paranoia.
		 */
		rnp = t->rcu_blocked_node;
		raw_spin_lock_rcu_node(rnp); /* irqs already disabled. */
		WARN_ON_ONCE(rnp != t->rcu_blocked_node);
		WARN_ON_ONCE(rnp->level != rcu_num_lvls - 1);
		empty_norm = !rcu_preempt_blocked_readers_cgp(rnp);
		empty_exp = sync_rcu_preempt_exp_done(rnp);
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
		if (IS_ENABLED(CONFIG_RCU_BOOST)) {
			/* Snapshot ->boost_mtx ownership w/rnp->lock held. */
			drop_boost_mutex = rt_mutex_owner(&rnp->boost_mtx) == t;
			if (&t->rcu_node_entry == rnp->boost_tasks)
				rnp->boost_tasks = np;
		}

		/*
		 * If this was the last task on the current list, and if
		 * we aren't waiting on any CPUs, report the quiescent state.
		 * Note that rcu_report_unblock_qs_rnp() releases rnp->lock,
		 * so we must take a snapshot of the expedited state.
		 */
		empty_exp_now = sync_rcu_preempt_exp_done(rnp);
		if (!empty_norm && !rcu_preempt_blocked_readers_cgp(rnp)) {
			trace_rcu_quiescent_state_report(TPS("preempt_rcu"),
							 rnp->gpnum,
							 0, rnp->qsmask,
							 rnp->level,
							 rnp->grplo,
							 rnp->grphi,
							 !!rnp->gp_tasks);
			rcu_report_unblock_qs_rnp(rcu_state_p, rnp, flags);
		} else {
			raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
		}

		/* Unboost if we were boosted. */
		if (IS_ENABLED(CONFIG_RCU_BOOST) && drop_boost_mutex)
			rt_mutex_unlock(&rnp->boost_mtx);

		/*
		 * If this was the last task on the expedited lists,
		 * then we need to report up the rcu_node hierarchy.
		 */
		if (!empty_exp && empty_exp_now)
			rcu_report_exp_rnp(rcu_state_p, rnp, true);
	} else {
		local_irq_restore(flags);
	}
}

/*
 * Dump detailed information for all tasks blocking the current RCU
 * grace period on the specified rcu_node structure.
 */
static void rcu_print_detail_task_stall_rnp(struct rcu_node *rnp)
{
	unsigned long flags;
	struct task_struct *t;

	raw_spin_lock_irqsave_rcu_node(rnp, flags);
	if (!rcu_preempt_blocked_readers_cgp(rnp)) {
		raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
		return;
	}
	t = list_entry(rnp->gp_tasks->prev,
		       struct task_struct, rcu_node_entry);
	list_for_each_entry_continue(t, &rnp->blkd_tasks, rcu_node_entry)
		sched_show_task(t);
	raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
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

static void rcu_print_task_stall_begin(struct rcu_node *rnp)
{
	pr_err("\tTasks blocked on level-%d rcu_node (CPUs %d-%d):",
	       rnp->level, rnp->grplo, rnp->grphi);
}

static void rcu_print_task_stall_end(void)
{
	pr_cont("\n");
}

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
	t = list_entry(rnp->gp_tasks->prev,
		       struct task_struct, rcu_node_entry);
	list_for_each_entry_continue(t, &rnp->blkd_tasks, rcu_node_entry) {
		pr_cont(" P%d", t->pid);
		ndetected++;
	}
	rcu_print_task_stall_end();
	return ndetected;
}

/*
 * Scan the current list of tasks blocked within RCU read-side critical
 * sections, printing out the tid of each that is blocking the current
 * expedited grace period.
 */
static int rcu_print_task_exp_stall(struct rcu_node *rnp)
{
	struct task_struct *t;
	int ndetected = 0;

	if (!rnp->exp_tasks)
		return 0;
	t = list_entry(rnp->exp_tasks->prev,
		       struct task_struct, rcu_node_entry);
	list_for_each_entry_continue(t, &rnp->blkd_tasks, rcu_node_entry) {
		pr_cont(" P%d", t->pid);
		ndetected++;
	}
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
	struct task_struct *t;

	RCU_LOCKDEP_WARN(preemptible(), "rcu_preempt_check_blocked_tasks() invoked with preemption enabled!!!\n");
	WARN_ON_ONCE(rcu_preempt_blocked_readers_cgp(rnp));
	if (rcu_preempt_has_tasks(rnp)) {
		rnp->gp_tasks = rnp->blkd_tasks.next;
		t = container_of(rnp->gp_tasks, struct task_struct,
				 rcu_node_entry);
		trace_rcu_unlock_preempted_task(TPS("rcu_preempt-GPS"),
						rnp->gpnum, t->pid);
	}
	WARN_ON_ONCE(rnp->qsmask);
}

/*
 * Check for a quiescent state from the current CPU.  When a task blocks,
 * the task is recorded in the corresponding CPU's rcu_node structure,
 * which is checked elsewhere.
 *
 * Caller must disable hard irqs.
 */
static void rcu_preempt_check_callbacks(void)
{
	struct task_struct *t = current;

	if (t->rcu_read_lock_nesting == 0) {
		rcu_preempt_qs();
		return;
	}
	if (t->rcu_read_lock_nesting > 0 &&
	    __this_cpu_read(rcu_data_p->core_needs_qs) &&
	    __this_cpu_read(rcu_data_p->cpu_no_qs.b.norm))
		t->rcu_read_unlock_special.b.need_qs = true;
}

#ifdef CONFIG_RCU_BOOST

static void rcu_preempt_do_callbacks(void)
{
	rcu_do_batch(rcu_state_p, this_cpu_ptr(rcu_data_p));
}

#endif /* #ifdef CONFIG_RCU_BOOST */

/**
 * call_rcu() - Queue an RCU callback for invocation after a grace period.
 * @head: structure to be used for queueing the RCU updates.
 * @func: actual callback function to be invoked after the grace period
 *
 * The callback function will be invoked some time after a full grace
 * period elapses, in other words after all pre-existing RCU read-side
 * critical sections have completed.  However, the callback function
 * might well execute concurrently with RCU read-side critical sections
 * that started after call_rcu() was invoked.  RCU read-side critical
 * sections are delimited by rcu_read_lock() and rcu_read_unlock(),
 * and may be nested.
 *
 * Note that all CPUs must agree that the grace period extended beyond
 * all pre-existing RCU read-side critical section.  On systems with more
 * than one CPU, this means that when "func()" is invoked, each CPU is
 * guaranteed to have executed a full memory barrier since the end of its
 * last RCU read-side critical section whose beginning preceded the call
 * to call_rcu().  It also means that each CPU executing an RCU read-side
 * critical section that continues beyond the start of "func()" must have
 * executed a memory barrier after the call_rcu() but before the beginning
 * of that RCU read-side critical section.  Note that these guarantees
 * include CPUs that are offline, idle, or executing in user mode, as
 * well as CPUs that are executing in the kernel.
 *
 * Furthermore, if CPU A invoked call_rcu() and CPU B invoked the
 * resulting RCU callback function "func()", then both CPU A and CPU B are
 * guaranteed to execute a full memory barrier during the time interval
 * between the call to call_rcu() and the invocation of "func()" -- even
 * if CPU A and CPU B are the same CPU (but again only if the system has
 * more than one CPU).
 */
void call_rcu(struct rcu_head *head, rcu_callback_t func)
{
	__call_rcu(head, func, rcu_state_p, -1, 0);
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
 * See the description of synchronize_sched() for more detailed
 * information on memory-ordering guarantees.  However, please note
 * that -only- the memory-ordering guarantees apply.  For example,
 * synchronize_rcu() is -not- guaranteed to wait on things like code
 * protected by preempt_disable(), instead, synchronize_rcu() is -only-
 * guaranteed to wait on RCU read-side critical sections, that is, sections
 * of code protected by rcu_read_lock().
 */
void synchronize_rcu(void)
{
	RCU_LOCKDEP_WARN(lock_is_held(&rcu_bh_lock_map) ||
			 lock_is_held(&rcu_lock_map) ||
			 lock_is_held(&rcu_sched_lock_map),
			 "Illegal synchronize_rcu() in RCU read-side critical section");
	if (rcu_scheduler_active == RCU_SCHEDULER_INACTIVE)
		return;
	if (rcu_gp_is_expedited())
		synchronize_rcu_expedited();
	else
		wait_rcu_gp(call_rcu);
}
EXPORT_SYMBOL_GPL(synchronize_rcu);

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
	_rcu_barrier(rcu_state_p);
}
EXPORT_SYMBOL_GPL(rcu_barrier);

/*
 * Initialize preemptible RCU's state structures.
 */
static void __init __rcu_init_preempt(void)
{
	rcu_init_one(rcu_state_p);
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
	t->rcu_read_unlock_special.b.blocked = true;
	__rcu_read_unlock();
}

#else /* #ifdef CONFIG_PREEMPT_RCU */

static struct rcu_state *const rcu_state_p = &rcu_sched_state;

/*
 * Tell them what RCU they are running.
 */
static void __init rcu_bootup_announce(void)
{
	pr_info("Hierarchical RCU implementation.\n");
	rcu_bootup_announce_oddness();
}

/*
 * Because preemptible RCU does not exist, we never have to check for
 * CPUs being in quiescent states.
 */
static void rcu_preempt_note_context_switch(bool preempt)
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

/*
 * Because there is no preemptible RCU, there can be no readers blocked.
 */
static bool rcu_preempt_has_tasks(struct rcu_node *rnp)
{
	return false;
}

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
 * Because preemptible RCU does not exist, we never have to check for
 * tasks blocked within RCU read-side critical sections that are
 * blocking the current expedited grace period.
 */
static int rcu_print_task_exp_stall(struct rcu_node *rnp)
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

/*
 * Because preemptible RCU does not exist, it never has any callbacks
 * to check.
 */
static void rcu_preempt_check_callbacks(void)
{
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

#endif /* #else #ifdef CONFIG_PREEMPT_RCU */

#ifdef CONFIG_RCU_BOOST

#include "../locking/rtmutex_common.h"

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
	struct task_struct *t;
	struct list_head *tb;

	if (READ_ONCE(rnp->exp_tasks) == NULL &&
	    READ_ONCE(rnp->boost_tasks) == NULL)
		return 0;  /* Nothing left to boost. */

	raw_spin_lock_irqsave_rcu_node(rnp, flags);

	/*
	 * Recheck under the lock: all tasks in need of boosting
	 * might exit their RCU read-side critical sections on their own.
	 */
	if (rnp->exp_tasks == NULL && rnp->boost_tasks == NULL) {
		raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
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
	rt_mutex_init_proxy_locked(&rnp->boost_mtx, t);
	raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
	/* Lock only for side effect: boosts task t's priority. */
	rt_mutex_lock(&rnp->boost_mtx);
	rt_mutex_unlock(&rnp->boost_mtx);  /* Then keep lockdep happy. */

	return READ_ONCE(rnp->exp_tasks) != NULL ||
	       READ_ONCE(rnp->boost_tasks) != NULL;
}

/*
 * Priority-boosting kthread, one per leaf rcu_node.
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
	__releases(rnp->lock)
{
	struct task_struct *t;

	lockdep_assert_held(&rnp->lock);
	if (!rcu_preempt_blocked_readers_cgp(rnp) && rnp->exp_tasks == NULL) {
		raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
		return;
	}
	if (rnp->exp_tasks != NULL ||
	    (rnp->gp_tasks != NULL &&
	     rnp->boost_tasks == NULL &&
	     rnp->qsmask == 0 &&
	     ULONG_CMP_GE(jiffies, rnp->boost_time))) {
		if (rnp->exp_tasks == NULL)
			rnp->boost_tasks = rnp->gp_tasks;
		raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
		t = rnp->boost_kthread_task;
		if (t)
			rcu_wake_cond(t, rnp->boost_kthread_status);
	} else {
		raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
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

	if (rcu_state_p != rsp)
		return 0;

	if (!rcu_scheduler_fully_active || rcu_rnp_online_cpus(rnp) == 0)
		return 0;

	rsp->boost = 1;
	if (rnp->boost_kthread_task != NULL)
		return 0;
	t = kthread_create(rcu_boost_kthread, (void *)rnp,
			   "rcub/%d", rnp_index);
	if (IS_ERR(t))
		return PTR_ERR(t);
	raw_spin_lock_irqsave_rcu_node(rnp, flags);
	rnp->boost_kthread_task = t;
	raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
	sp.sched_priority = kthread_prio;
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

	sp.sched_priority = kthread_prio;
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
	unsigned long mask = rcu_rnp_online_cpus(rnp);
	cpumask_var_t cm;
	int cpu;

	if (!t)
		return;
	if (!zalloc_cpumask_var(&cm, GFP_KERNEL))
		return;
	for_each_leaf_node_possible_cpu(rnp, cpu)
		if ((mask & leaf_node_cpu_bit(rnp, cpu)) &&
		    cpu != outgoingcpu)
			cpumask_set_cpu(cpu, cm);
	if (cpumask_weight(cm) == 0)
		cpumask_setall(cm);
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
 * Spawn boost kthreads -- called as soon as the scheduler is running.
 */
static void __init rcu_spawn_boost_kthreads(void)
{
	struct rcu_node *rnp;
	int cpu;

	for_each_possible_cpu(cpu)
		per_cpu(rcu_cpu_has_work, cpu) = 0;
	BUG_ON(smpboot_register_percpu_thread(&rcu_cpu_thread_spec));
	rcu_for_each_leaf_node(rcu_state_p, rnp)
		(void)rcu_spawn_one_boost_kthread(rcu_state_p, rnp);
}

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
	__releases(rnp->lock)
{
	raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
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

static void __init rcu_spawn_boost_kthreads(void)
{
}

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
int rcu_needs_cpu(u64 basemono, u64 *nextevt)
{
	*nextevt = KTIME_MAX;
	return rcu_cpu_has_callbacks(NULL);
}

/*
 * Because we do not have RCU_FAST_NO_HZ, don't bother cleaning up
 * after it.
 */
static void rcu_cleanup_after_idle(void)
{
}

/*
 * Do the idle-entry grace-period work, which, because CONFIG_RCU_FAST_NO_HZ=n,
 * is nothing.
 */
static void rcu_prepare_for_idle(void)
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
		return false;
	rdtp->last_advance_all = jiffies;

	for_each_rcu_flavor(rsp) {
		rdp = this_cpu_ptr(rsp->rda);
		rnp = rdp->mynode;

		/*
		 * Don't bother checking unless a grace period has
		 * completed since we last checked and there are
		 * callbacks not yet ready to invoke.
		 */
		if ((rdp->completed != rnp->completed ||
		     unlikely(READ_ONCE(rdp->gpwrap))) &&
		    rcu_segcblist_pend_cbs(&rdp->cblist))
			note_gp_changes(rsp, rdp);

		if (rcu_segcblist_ready_cbs(&rdp->cblist))
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
int rcu_needs_cpu(u64 basemono, u64 *nextevt)
{
	struct rcu_dynticks *rdtp = this_cpu_ptr(&rcu_dynticks);
	unsigned long dj;

	RCU_LOCKDEP_WARN(!irqs_disabled(), "rcu_needs_cpu() invoked with irqs enabled!!!");

	/* Snapshot to detect later posting of non-lazy callback. */
	rdtp->nonlazy_posted_snap = rdtp->nonlazy_posted;

	/* If no callbacks, RCU doesn't need the CPU. */
	if (!rcu_cpu_has_callbacks(&rdtp->all_lazy)) {
		*nextevt = KTIME_MAX;
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
		dj = round_up(rcu_idle_gp_delay + jiffies,
			       rcu_idle_gp_delay) - jiffies;
	} else {
		dj = round_jiffies(rcu_idle_lazy_gp_delay + jiffies) - jiffies;
	}
	*nextevt = basemono + dj * TICK_NSEC;
	return 0;
}

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
static void rcu_prepare_for_idle(void)
{
	bool needwake;
	struct rcu_data *rdp;
	struct rcu_dynticks *rdtp = this_cpu_ptr(&rcu_dynticks);
	struct rcu_node *rnp;
	struct rcu_state *rsp;
	int tne;

	RCU_LOCKDEP_WARN(!irqs_disabled(), "rcu_prepare_for_idle() invoked with irqs enabled!!!");
	if (rcu_is_nocb_cpu(smp_processor_id()))
		return;

	/* Handle nohz enablement switches conservatively. */
	tne = READ_ONCE(tick_nohz_active);
	if (tne != rdtp->tick_nohz_enabled_snap) {
		if (rcu_cpu_has_callbacks(NULL))
			invoke_rcu_core(); /* force nohz to see update. */
		rdtp->tick_nohz_enabled_snap = tne;
		return;
	}
	if (!tne)
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
		rdp = this_cpu_ptr(rsp->rda);
		if (rcu_segcblist_pend_cbs(&rdp->cblist))
			continue;
		rnp = rdp->mynode;
		raw_spin_lock_rcu_node(rnp); /* irqs already disabled. */
		needwake = rcu_accelerate_cbs(rsp, rnp, rdp);
		raw_spin_unlock_rcu_node(rnp); /* irqs remain disabled. */
		if (needwake)
			rcu_gp_kthread_wake(rsp);
	}
}

/*
 * Clean up for exit from idle.  Attempt to advance callbacks based on
 * any grace periods that elapsed while the CPU was idle, and if any
 * callbacks are now ready to invoke, initiate invocation.
 */
static void rcu_cleanup_after_idle(void)
{
	RCU_LOCKDEP_WARN(!irqs_disabled(), "rcu_cleanup_after_idle() invoked with irqs enabled!!!");
	if (rcu_is_nocb_cpu(smp_processor_id()))
		return;
	if (rcu_try_advance_all_cbs())
		invoke_rcu_core();
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
		if (rcu_segcblist_n_lazy_cbs(&rdp->cblist)) {
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

	for_each_online_cpu(cpu) {
		smp_call_function_single(cpu, rcu_oom_notify_cpu, NULL, 1);
		cond_resched_rcu_qs();
	}

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
	pr_err("\t%d-%c%c%c: (%lu %s) idle=%03x/%llx/%d softirq=%u/%u fqs=%ld %s\n",
	       cpu,
	       "O."[!!cpu_online(cpu)],
	       "o."[!!(rdp->grpmask & rdp->mynode->qsmaskinit)],
	       "N."[!!(rdp->grpmask & rdp->mynode->qsmaskinitnext)],
	       ticks_value, ticks_title,
	       rcu_dynticks_snap(rdtp) & 0xfff,
	       rdtp->dynticks_nesting, rdtp->dynticks_nmi_nesting,
	       rdp->softirq_snap, kstat_softirqs_cpu(RCU_SOFTIRQ, cpu),
	       READ_ONCE(rsp->n_force_qs) - rsp->n_force_qs_gpstart,
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
	rcu_nocb_poll = true;
	return 0;
}
early_param("rcu_nocb_poll", parse_rcu_nocb_poll);

/*
 * Wake up any no-CBs CPUs' kthreads that were waiting on the just-ended
 * grace period.
 */
static void rcu_nocb_gp_cleanup(struct swait_queue_head *sq)
{
	swake_up_all(sq);
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

static struct swait_queue_head *rcu_nocb_gp_get(struct rcu_node *rnp)
{
	return &rnp->nocb_gp_wq[rnp->completed & 0x1];
}

static void rcu_init_one_nocb(struct rcu_node *rnp)
{
	init_swait_queue_head(&rnp->nocb_gp_wq[0]);
	init_swait_queue_head(&rnp->nocb_gp_wq[1]);
}

/* Is the specified CPU a no-CBs CPU? */
bool rcu_is_nocb_cpu(int cpu)
{
	if (have_rcu_nocb_mask)
		return cpumask_test_cpu(cpu, rcu_nocb_mask);
	return false;
}

/*
 * Kick the leader kthread for this NOCB group.  Caller holds ->nocb_lock
 * and this function releases it.
 */
static void __wake_nocb_leader(struct rcu_data *rdp, bool force,
			       unsigned long flags)
	__releases(rdp->nocb_lock)
{
	struct rcu_data *rdp_leader = rdp->nocb_leader;

	lockdep_assert_held(&rdp->nocb_lock);
	if (!READ_ONCE(rdp_leader->nocb_kthread)) {
		raw_spin_unlock_irqrestore(&rdp->nocb_lock, flags);
		return;
	}
	if (rdp_leader->nocb_leader_sleep || force) {
		/* Prior smp_mb__after_atomic() orders against prior enqueue. */
		WRITE_ONCE(rdp_leader->nocb_leader_sleep, false);
		del_timer(&rdp->nocb_timer);
		raw_spin_unlock_irqrestore(&rdp->nocb_lock, flags);
		smp_mb(); /* ->nocb_leader_sleep before swake_up(). */
		swake_up(&rdp_leader->nocb_wq);
	} else {
		raw_spin_unlock_irqrestore(&rdp->nocb_lock, flags);
	}
}

/*
 * Kick the leader kthread for this NOCB group, but caller has not
 * acquired locks.
 */
static void wake_nocb_leader(struct rcu_data *rdp, bool force)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&rdp->nocb_lock, flags);
	__wake_nocb_leader(rdp, force, flags);
}

/*
 * Arrange to wake the leader kthread for this NOCB group at some
 * future time when it is safe to do so.
 */
static void wake_nocb_leader_defer(struct rcu_data *rdp, int waketype,
				   const char *reason)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&rdp->nocb_lock, flags);
	if (rdp->nocb_defer_wakeup == RCU_NOCB_WAKE_NOT)
		mod_timer(&rdp->nocb_timer, jiffies + 1);
	WRITE_ONCE(rdp->nocb_defer_wakeup, waketype);
	trace_rcu_nocb_wake(rdp->rsp->name, rdp->cpu, reason);
	raw_spin_unlock_irqrestore(&rdp->nocb_lock, flags);
}

/*
 * Does the specified CPU need an RCU callback for the specified flavor
 * of rcu_barrier()?
 */
static bool rcu_nocb_cpu_needs_barrier(struct rcu_state *rsp, int cpu)
{
	struct rcu_data *rdp = per_cpu_ptr(rsp->rda, cpu);
	unsigned long ret;
#ifdef CONFIG_PROVE_RCU
	struct rcu_head *rhp;
#endif /* #ifdef CONFIG_PROVE_RCU */

	/*
	 * Check count of all no-CBs callbacks awaiting invocation.
	 * There needs to be a barrier before this function is called,
	 * but associated with a prior determination that no more
	 * callbacks would be posted.  In the worst case, the first
	 * barrier in _rcu_barrier() suffices (but the caller cannot
	 * necessarily rely on this, not a substitute for the caller
	 * getting the concurrency design right!).  There must also be
	 * a barrier between the following load an posting of a callback
	 * (if a callback is in fact needed).  This is associated with an
	 * atomic_inc() in the caller.
	 */
	ret = atomic_long_read(&rdp->nocb_q_count);

#ifdef CONFIG_PROVE_RCU
	rhp = READ_ONCE(rdp->nocb_head);
	if (!rhp)
		rhp = READ_ONCE(rdp->nocb_gp_head);
	if (!rhp)
		rhp = READ_ONCE(rdp->nocb_follower_head);

	/* Having no rcuo kthread but CBs after scheduler starts is bad! */
	if (!READ_ONCE(rdp->nocb_kthread) && rhp &&
	    rcu_scheduler_fully_active) {
		/* RCU callback enqueued before CPU first came online??? */
		pr_err("RCU: Never-onlined no-CBs CPU %d has CB %p\n",
		       cpu, rhp->func);
		WARN_ON_ONCE(1);
	}
#endif /* #ifdef CONFIG_PROVE_RCU */

	return !!ret;
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
	atomic_long_add(rhcount, &rdp->nocb_q_count);
	/* rcu_barrier() relies on ->nocb_q_count add before xchg. */
	old_rhpp = xchg(&rdp->nocb_tail, rhtp);
	WRITE_ONCE(*old_rhpp, rhp);
	atomic_long_add(rhcount_lazy, &rdp->nocb_q_count_lazy);
	smp_mb__after_atomic(); /* Store *old_rhpp before _wake test. */

	/* If we are not being polled and there is a kthread, awaken it ... */
	t = READ_ONCE(rdp->nocb_kthread);
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
			wake_nocb_leader_defer(rdp, RCU_NOCB_WAKE,
					       TPS("WakeEmptyIsDeferred"));
		}
		rdp->qlen_last_fqs_check = 0;
	} else if (len > rdp->qlen_last_fqs_check + qhimark) {
		/* ... or if many callbacks queued. */
		if (!irqs_disabled_flags(flags)) {
			wake_nocb_leader(rdp, true);
			trace_rcu_nocb_wake(rdp->rsp->name, rdp->cpu,
					    TPS("WakeOvf"));
		} else {
			wake_nocb_leader_defer(rdp, RCU_NOCB_WAKE,
					       TPS("WakeOvfIsDeferred"));
		}
		rdp->qlen_last_fqs_check = LONG_MAX / 2;
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
		return false;
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

	/*
	 * If called from an extended quiescent state with interrupts
	 * disabled, invoke the RCU core in order to allow the idle-entry
	 * deferred-wakeup check to function.
	 */
	if (irqs_disabled_flags(flags) &&
	    !rcu_is_watching() &&
	    cpu_online(smp_processor_id()))
		invoke_rcu_core();

	return true;
}

/*
 * Adopt orphaned callbacks on a no-CBs CPU, or return 0 if this is
 * not a no-CBs CPU.
 */
static bool __maybe_unused rcu_nocb_adopt_orphan_cbs(struct rcu_data *my_rdp,
						     struct rcu_data *rdp,
						     unsigned long flags)
{
	RCU_LOCKDEP_WARN(!irqs_disabled(), "rcu_nocb_adopt_orphan_cbs() invoked with irqs enabled!!!");
	if (!rcu_is_nocb_cpu(smp_processor_id()))
		return false; /* Not NOCBs CPU, caller must migrate CBs. */
	__call_rcu_nocb_enqueue(my_rdp, rcu_segcblist_head(&rdp->cblist),
				rcu_segcblist_tail(&rdp->cblist),
				rcu_segcblist_n_cbs(&rdp->cblist),
				rcu_segcblist_n_lazy_cbs(&rdp->cblist), flags);
	rcu_segcblist_init(&rdp->cblist);
	rcu_segcblist_disable(&rdp->cblist);
	return true;
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

	raw_spin_lock_irqsave_rcu_node(rnp, flags);
	needwake = rcu_start_future_gp(rnp, rdp, &c);
	raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
	if (needwake)
		rcu_gp_kthread_wake(rdp->rsp);

	/*
	 * Wait for the grace period.  Do so interruptibly to avoid messing
	 * up the load average.
	 */
	trace_rcu_future_gp(rnp, rdp, c, TPS("StartWait"));
	for (;;) {
		swait_event_interruptible(
			rnp->nocb_gp_wq[c & 0x1],
			(d = ULONG_CMP_GE(READ_ONCE(rnp->completed), c)));
		if (likely(d))
			break;
		WARN_ON(signal_pending(current));
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
	unsigned long flags;
	bool gotcbs;
	struct rcu_data *rdp;
	struct rcu_head **tail;

wait_again:

	/* Wait for callbacks to appear. */
	if (!rcu_nocb_poll) {
		trace_rcu_nocb_wake(my_rdp->rsp->name, my_rdp->cpu, TPS("Sleep"));
		swait_event_interruptible(my_rdp->nocb_wq,
				!READ_ONCE(my_rdp->nocb_leader_sleep));
		raw_spin_lock_irqsave(&my_rdp->nocb_lock, flags);
		my_rdp->nocb_leader_sleep = true;
		WRITE_ONCE(my_rdp->nocb_defer_wakeup, RCU_NOCB_WAKE_NOT);
		del_timer(&my_rdp->nocb_timer);
		raw_spin_unlock_irqrestore(&my_rdp->nocb_lock, flags);
	} else if (firsttime) {
		firsttime = false; /* Don't drown trace log with "Poll"! */
		trace_rcu_nocb_wake(my_rdp->rsp->name, my_rdp->cpu, TPS("Poll"));
	}

	/*
	 * Each pass through the following loop checks a follower for CBs.
	 * We are our own first follower.  Any CBs found are moved to
	 * nocb_gp_head, where they await a grace period.
	 */
	gotcbs = false;
	smp_mb(); /* wakeup and _sleep before ->nocb_head reads. */
	for (rdp = my_rdp; rdp; rdp = rdp->nocb_next_follower) {
		rdp->nocb_gp_head = READ_ONCE(rdp->nocb_head);
		if (!rdp->nocb_gp_head)
			continue;  /* No CBs here, try next follower. */

		/* Move callbacks to wait-for-GP list, which is empty. */
		WRITE_ONCE(rdp->nocb_head, NULL);
		rdp->nocb_gp_tail = xchg(&rdp->nocb_tail, &rdp->nocb_head);
		gotcbs = true;
	}

	/* No callbacks?  Sleep a bit if polling, and go retry.  */
	if (unlikely(!gotcbs)) {
		WARN_ON(signal_pending(current));
		if (rcu_nocb_poll) {
			schedule_timeout_interruptible(1);
		} else {
			trace_rcu_nocb_wake(my_rdp->rsp->name, my_rdp->cpu,
					    TPS("WokeEmpty"));
		}
		goto wait_again;
	}

	/* Wait for one grace period. */
	rcu_nocb_wait_gp(my_rdp);

	/* Each pass through the following loop wakes a follower, if needed. */
	for (rdp = my_rdp; rdp; rdp = rdp->nocb_next_follower) {
		if (!rcu_nocb_poll &&
		    READ_ONCE(rdp->nocb_head) &&
		    READ_ONCE(my_rdp->nocb_leader_sleep)) {
			raw_spin_lock_irqsave(&my_rdp->nocb_lock, flags);
			my_rdp->nocb_leader_sleep = false;/* No need to sleep.*/
			raw_spin_unlock_irqrestore(&my_rdp->nocb_lock, flags);
		}
		if (!rdp->nocb_gp_head)
			continue; /* No CBs, so no need to wake follower. */

		/* Append callbacks to follower's "done" list. */
		raw_spin_lock_irqsave(&rdp->nocb_lock, flags);
		tail = rdp->nocb_follower_tail;
		rdp->nocb_follower_tail = rdp->nocb_gp_tail;
		*tail = rdp->nocb_gp_head;
		raw_spin_unlock_irqrestore(&rdp->nocb_lock, flags);
		if (rdp != my_rdp && tail == &rdp->nocb_follower_head) {
			/* List was empty, so wake up the follower.  */
			swake_up(&rdp->nocb_wq);
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
	for (;;) {
		trace_rcu_nocb_wake(rdp->rsp->name, rdp->cpu, TPS("FollowerSleep"));
		swait_event_interruptible(rdp->nocb_wq,
					 READ_ONCE(rdp->nocb_follower_head));
		if (smp_load_acquire(&rdp->nocb_follower_head)) {
			/* ^^^ Ensure CB invocation follows _head test. */
			return;
		}
		WARN_ON(signal_pending(current));
		trace_rcu_nocb_wake(rdp->rsp->name, rdp->cpu, TPS("WokeEmpty"));
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
	unsigned long flags;
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
		raw_spin_lock_irqsave(&rdp->nocb_lock, flags);
		list = rdp->nocb_follower_head;
		rdp->nocb_follower_head = NULL;
		tail = rdp->nocb_follower_tail;
		rdp->nocb_follower_tail = &rdp->nocb_follower_head;
		raw_spin_unlock_irqrestore(&rdp->nocb_lock, flags);
		BUG_ON(!list);
		trace_rcu_nocb_wake(rdp->rsp->name, rdp->cpu, TPS("WokeNonEmpty"));

		/* Each pass through the following loop invokes a callback. */
		trace_rcu_batch_start(rdp->rsp->name,
				      atomic_long_read(&rdp->nocb_q_count_lazy),
				      atomic_long_read(&rdp->nocb_q_count), -1);
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
			cond_resched_rcu_qs();
			list = next;
		}
		trace_rcu_batch_end(rdp->rsp->name, c, !!list, 0, 0, 1);
		smp_mb__before_atomic();  /* _add after CB invocation. */
		atomic_long_add(-c, &rdp->nocb_q_count);
		atomic_long_add(-cl, &rdp->nocb_q_count_lazy);
		rdp->n_nocbs_invoked += c;
	}
	return 0;
}

/* Is a deferred wakeup of rcu_nocb_kthread() required? */
static int rcu_nocb_need_deferred_wakeup(struct rcu_data *rdp)
{
	return READ_ONCE(rdp->nocb_defer_wakeup);
}

/* Do a deferred wakeup of rcu_nocb_kthread(). */
static void do_nocb_deferred_wakeup_common(struct rcu_data *rdp)
{
	unsigned long flags;
	int ndw;

	raw_spin_lock_irqsave(&rdp->nocb_lock, flags);
	if (!rcu_nocb_need_deferred_wakeup(rdp)) {
		raw_spin_unlock_irqrestore(&rdp->nocb_lock, flags);
		return;
	}
	ndw = READ_ONCE(rdp->nocb_defer_wakeup);
	WRITE_ONCE(rdp->nocb_defer_wakeup, RCU_NOCB_WAKE_NOT);
	__wake_nocb_leader(rdp, ndw == RCU_NOCB_WAKE_FORCE, flags);
	trace_rcu_nocb_wake(rdp->rsp->name, rdp->cpu, TPS("DeferredWake"));
}

/* Do a deferred wakeup of rcu_nocb_kthread() from a timer handler. */
static void do_nocb_deferred_wakeup_timer(unsigned long x)
{
	do_nocb_deferred_wakeup_common((struct rcu_data *)x);
}

/*
 * Do a deferred wakeup of rcu_nocb_kthread() from fastpath.
 * This means we do an inexact common-case check.  Note that if
 * we miss, ->nocb_timer will eventually clean things up.
 */
static void do_nocb_deferred_wakeup(struct rcu_data *rdp)
{
	if (rcu_nocb_need_deferred_wakeup(rdp))
		do_nocb_deferred_wakeup_common(rdp);
}

void __init rcu_init_nohz(void)
{
	int cpu;
	bool need_rcu_nocb_mask = true;
	struct rcu_state *rsp;

#if defined(CONFIG_NO_HZ_FULL)
	if (tick_nohz_full_running && cpumask_weight(tick_nohz_full_mask))
		need_rcu_nocb_mask = true;
#endif /* #if defined(CONFIG_NO_HZ_FULL) */

	if (!have_rcu_nocb_mask && need_rcu_nocb_mask) {
		if (!zalloc_cpumask_var(&rcu_nocb_mask, GFP_KERNEL)) {
			pr_info("rcu_nocb_mask allocation failed, callback offloading disabled.\n");
			return;
		}
		have_rcu_nocb_mask = true;
	}
	if (!have_rcu_nocb_mask)
		return;

#if defined(CONFIG_NO_HZ_FULL)
	if (tick_nohz_full_running)
		cpumask_or(rcu_nocb_mask, rcu_nocb_mask, tick_nohz_full_mask);
#endif /* #if defined(CONFIG_NO_HZ_FULL) */

	if (!cpumask_subset(rcu_nocb_mask, cpu_possible_mask)) {
		pr_info("\tNote: kernel parameter 'rcu_nocbs=' contains nonexistent CPUs.\n");
		cpumask_and(rcu_nocb_mask, cpu_possible_mask,
			    rcu_nocb_mask);
	}
	pr_info("\tOffload RCU callbacks from CPUs: %*pbl.\n",
		cpumask_pr_args(rcu_nocb_mask));
	if (rcu_nocb_poll)
		pr_info("\tPoll for callbacks from no-CBs CPUs.\n");

	for_each_rcu_flavor(rsp) {
		for_each_cpu(cpu, rcu_nocb_mask)
			init_nocb_callback_list(per_cpu_ptr(rsp->rda, cpu));
		rcu_organize_nocb_kthreads(rsp);
	}
}

/* Initialize per-rcu_data variables for no-CBs CPUs. */
static void __init rcu_boot_init_nocb_percpu_data(struct rcu_data *rdp)
{
	rdp->nocb_tail = &rdp->nocb_head;
	init_swait_queue_head(&rdp->nocb_wq);
	rdp->nocb_follower_tail = &rdp->nocb_follower_head;
	raw_spin_lock_init(&rdp->nocb_lock);
	setup_timer(&rdp->nocb_timer, do_nocb_deferred_wakeup_timer,
		    (unsigned long)rdp);
}

/*
 * If the specified CPU is a no-CBs CPU that does not already have its
 * rcuo kthread for the specified RCU flavor, spawn it.  If the CPUs are
 * brought online out of order, this can require re-organizing the
 * leader-follower relationships.
 */
static void rcu_spawn_one_nocb_kthread(struct rcu_state *rsp, int cpu)
{
	struct rcu_data *rdp;
	struct rcu_data *rdp_last;
	struct rcu_data *rdp_old_leader;
	struct rcu_data *rdp_spawn = per_cpu_ptr(rsp->rda, cpu);
	struct task_struct *t;

	/*
	 * If this isn't a no-CBs CPU or if it already has an rcuo kthread,
	 * then nothing to do.
	 */
	if (!rcu_is_nocb_cpu(cpu) || rdp_spawn->nocb_kthread)
		return;

	/* If we didn't spawn the leader first, reorganize! */
	rdp_old_leader = rdp_spawn->nocb_leader;
	if (rdp_old_leader != rdp_spawn && !rdp_old_leader->nocb_kthread) {
		rdp_last = NULL;
		rdp = rdp_old_leader;
		do {
			rdp->nocb_leader = rdp_spawn;
			if (rdp_last && rdp != rdp_spawn)
				rdp_last->nocb_next_follower = rdp;
			if (rdp == rdp_spawn) {
				rdp = rdp->nocb_next_follower;
			} else {
				rdp_last = rdp;
				rdp = rdp->nocb_next_follower;
				rdp_last->nocb_next_follower = NULL;
			}
		} while (rdp);
		rdp_spawn->nocb_next_follower = rdp_old_leader;
	}

	/* Spawn the kthread for this CPU and RCU flavor. */
	t = kthread_run(rcu_nocb_kthread, rdp_spawn,
			"rcuo%c/%d", rsp->abbr, cpu);
	BUG_ON(IS_ERR(t));
	WRITE_ONCE(rdp_spawn->nocb_kthread, t);
}

/*
 * If the specified CPU is a no-CBs CPU that does not already have its
 * rcuo kthreads, spawn them.
 */
static void rcu_spawn_all_nocb_kthreads(int cpu)
{
	struct rcu_state *rsp;

	if (rcu_scheduler_fully_active)
		for_each_rcu_flavor(rsp)
			rcu_spawn_one_nocb_kthread(rsp, cpu);
}

/*
 * Once the scheduler is running, spawn rcuo kthreads for all online
 * no-CBs CPUs.  This assumes that the early_initcall()s happen before
 * non-boot CPUs come online -- if this changes, we will need to add
 * some mutual exclusion.
 */
static void __init rcu_spawn_nocb_kthreads(void)
{
	int cpu;

	for_each_online_cpu(cpu)
		rcu_spawn_all_nocb_kthreads(cpu);
}

/* How many follower CPU IDs per leader?  Default of -1 for sqrt(nr_cpu_ids). */
static int rcu_nocb_leader_stride = -1;
module_param(rcu_nocb_leader_stride, int, 0444);

/*
 * Initialize leader-follower relationships for all no-CBs CPU.
 */
static void __init rcu_organize_nocb_kthreads(struct rcu_state *rsp)
{
	int cpu;
	int ls = rcu_nocb_leader_stride;
	int nl = 0;  /* Next leader. */
	struct rcu_data *rdp;
	struct rcu_data *rdp_leader = NULL;  /* Suppress misguided gcc warn. */
	struct rcu_data *rdp_prev = NULL;

	if (!have_rcu_nocb_mask)
		return;
	if (ls == -1) {
		ls = int_sqrt(nr_cpu_ids);
		rcu_nocb_leader_stride = ls;
	}

	/*
	 * Each pass through this loop sets up one rcu_data structure.
	 * Should the corresponding CPU come online in the future, then
	 * we will spawn the needed set of rcu_nocb_kthread() kthreads.
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
	}
}

/* Prevent __call_rcu() from enqueuing callbacks on no-CBs CPUs */
static bool init_nocb_callback_list(struct rcu_data *rdp)
{
	if (!rcu_is_nocb_cpu(rdp->cpu))
		return false;

	/* If there are early-boot callbacks, move them to nocb lists. */
	if (!rcu_segcblist_empty(&rdp->cblist)) {
		rdp->nocb_head = rcu_segcblist_head(&rdp->cblist);
		rdp->nocb_tail = rcu_segcblist_tail(&rdp->cblist);
		atomic_long_set(&rdp->nocb_q_count,
				rcu_segcblist_n_cbs(&rdp->cblist));
		atomic_long_set(&rdp->nocb_q_count_lazy,
				rcu_segcblist_n_lazy_cbs(&rdp->cblist));
		rcu_segcblist_init(&rdp->cblist);
	}
	rcu_segcblist_disable(&rdp->cblist);
	return true;
}

#else /* #ifdef CONFIG_RCU_NOCB_CPU */

static bool rcu_nocb_cpu_needs_barrier(struct rcu_state *rsp, int cpu)
{
	WARN_ON_ONCE(1); /* Should be dead code. */
	return false;
}

static void rcu_nocb_gp_cleanup(struct swait_queue_head *sq)
{
}

static void rcu_nocb_gp_set(struct rcu_node *rnp, int nrq)
{
}

static struct swait_queue_head *rcu_nocb_gp_get(struct rcu_node *rnp)
{
	return NULL;
}

static void rcu_init_one_nocb(struct rcu_node *rnp)
{
}

static bool __call_rcu_nocb(struct rcu_data *rdp, struct rcu_head *rhp,
			    bool lazy, unsigned long flags)
{
	return false;
}

static bool __maybe_unused rcu_nocb_adopt_orphan_cbs(struct rcu_data *my_rdp,
						     struct rcu_data *rdp,
						     unsigned long flags)
{
	return false;
}

static void __init rcu_boot_init_nocb_percpu_data(struct rcu_data *rdp)
{
}

static int rcu_nocb_need_deferred_wakeup(struct rcu_data *rdp)
{
	return false;
}

static void do_nocb_deferred_wakeup(struct rcu_data *rdp)
{
}

static void rcu_spawn_all_nocb_kthreads(int cpu)
{
}

static void __init rcu_spawn_nocb_kthreads(void)
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
	     ULONG_CMP_LT(jiffies, READ_ONCE(rsp->gp_start) + HZ)))
		return true;
#endif /* #ifdef CONFIG_NO_HZ_FULL */
	return false;
}

/*
 * Bind the grace-period kthread for the sysidle flavor of RCU to the
 * timekeeping CPU.
 */
static void rcu_bind_gp_kthread(void)
{
	int __maybe_unused cpu;

	if (!tick_nohz_full_enabled())
		return;
	housekeeping_affine(current);
}

/* Record the current task on dyntick-idle entry. */
static void rcu_dynticks_task_enter(void)
{
#if defined(CONFIG_TASKS_RCU) && defined(CONFIG_NO_HZ_FULL)
	WRITE_ONCE(current->rcu_tasks_idle_cpu, smp_processor_id());
#endif /* #if defined(CONFIG_TASKS_RCU) && defined(CONFIG_NO_HZ_FULL) */
}

/* Record no current task on dyntick-idle exit. */
static void rcu_dynticks_task_exit(void)
{
#if defined(CONFIG_TASKS_RCU) && defined(CONFIG_NO_HZ_FULL)
	WRITE_ONCE(current->rcu_tasks_idle_cpu, -1);
#endif /* #if defined(CONFIG_TASKS_RCU) && defined(CONFIG_NO_HZ_FULL) */
}
