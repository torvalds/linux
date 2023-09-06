/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Read-Copy Update mechanism for mutual exclusion (tree-based version)
 * Internal non-public definitions that provide either classic
 * or preemptible semantics.
 *
 * Copyright Red Hat, 2009
 * Copyright IBM Corporation, 2009
 *
 * Author: Ingo Molnar <mingo@elte.hu>
 *	   Paul E. McKenney <paulmck@linux.ibm.com>
 */

#include "../locking/rtmutex_common.h"

static bool rcu_rdp_is_offloaded(struct rcu_data *rdp)
{
	/*
	 * In order to read the offloaded state of an rdp is a safe
	 * and stable way and prevent from its value to be changed
	 * under us, we must either hold the barrier mutex, the cpu
	 * hotplug lock (read or write) or the nocb lock. Local
	 * non-preemptible reads are also safe. NOCB kthreads and
	 * timers have their own means of synchronization against the
	 * offloaded state updaters.
	 */
	RCU_LOCKDEP_WARN(
		!(lockdep_is_held(&rcu_state.barrier_mutex) ||
		  (IS_ENABLED(CONFIG_HOTPLUG_CPU) && lockdep_is_cpus_held()) ||
		  rcu_lockdep_is_held_nocb(rdp) ||
		  (rdp == this_cpu_ptr(&rcu_data) &&
		   !(IS_ENABLED(CONFIG_PREEMPT_COUNT) && preemptible())) ||
		  rcu_current_is_nocb_kthread(rdp)),
		"Unsafe read of RCU_NOCB offloaded state"
	);

	return rcu_segcblist_is_offloaded(&rdp->cblist);
}

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
		pr_info("\tCONFIG_RCU_FANOUT set to non-default value of %d.\n",
			RCU_FANOUT);
	if (rcu_fanout_exact)
		pr_info("\tHierarchical RCU autobalancing is disabled.\n");
	if (IS_ENABLED(CONFIG_RCU_FAST_NO_HZ))
		pr_info("\tRCU dyntick-idle grace-period acceleration is enabled.\n");
	if (IS_ENABLED(CONFIG_PROVE_RCU))
		pr_info("\tRCU lockdep checking is enabled.\n");
	if (IS_ENABLED(CONFIG_RCU_STRICT_GRACE_PERIOD))
		pr_info("\tRCU strict (and thus non-scalable) grace periods enabled.\n");
	if (RCU_NUM_LVLS >= 4)
		pr_info("\tFour(or more)-level hierarchy is enabled.\n");
	if (RCU_FANOUT_LEAF != 16)
		pr_info("\tBuild-time adjustment of leaf fanout to %d.\n",
			RCU_FANOUT_LEAF);
	if (rcu_fanout_leaf != RCU_FANOUT_LEAF)
		pr_info("\tBoot-time adjustment of leaf fanout to %d.\n",
			rcu_fanout_leaf);
	if (nr_cpu_ids != NR_CPUS)
		pr_info("\tRCU restricting CPUs from NR_CPUS=%d to nr_cpu_ids=%u.\n", NR_CPUS, nr_cpu_ids);
#ifdef CONFIG_RCU_BOOST
	pr_info("\tRCU priority boosting: priority %d delay %d ms.\n",
		kthread_prio, CONFIG_RCU_BOOST_DELAY);
#endif
	if (blimit != DEFAULT_RCU_BLIMIT)
		pr_info("\tBoot-time adjustment of callback invocation limit to %ld.\n", blimit);
	if (qhimark != DEFAULT_RCU_QHIMARK)
		pr_info("\tBoot-time adjustment of callback high-water mark to %ld.\n", qhimark);
	if (qlowmark != DEFAULT_RCU_QLOMARK)
		pr_info("\tBoot-time adjustment of callback low-water mark to %ld.\n", qlowmark);
	if (qovld != DEFAULT_RCU_QOVLD)
		pr_info("\tBoot-time adjustment of callback overload level to %ld.\n", qovld);
	if (jiffies_till_first_fqs != ULONG_MAX)
		pr_info("\tBoot-time adjustment of first FQS scan delay to %ld jiffies.\n", jiffies_till_first_fqs);
	if (jiffies_till_next_fqs != ULONG_MAX)
		pr_info("\tBoot-time adjustment of subsequent FQS scan delay to %ld jiffies.\n", jiffies_till_next_fqs);
	if (jiffies_till_sched_qs != ULONG_MAX)
		pr_info("\tBoot-time adjustment of scheduler-enlistment delay to %ld jiffies.\n", jiffies_till_sched_qs);
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
	if (!use_softirq)
		pr_info("\tRCU_SOFTIRQ processing moved to rcuc kthreads.\n");
	if (IS_ENABLED(CONFIG_RCU_EQS_DEBUG))
		pr_info("\tRCU debug extended QS entry/exit.\n");
	rcupdate_announce_bootup_oddness();
}

#ifdef CONFIG_PREEMPT_RCU

static void rcu_report_exp_rnp(struct rcu_node *rnp, bool wake);
static void rcu_read_unlock_special(struct task_struct *t);

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

	raw_lockdep_assert_held_rcu_node(rnp);
	WARN_ON_ONCE(rdp->mynode != rnp);
	WARN_ON_ONCE(!rcu_is_leaf_node(rnp));
	/* RCU better not be waiting on newly onlined CPUs! */
	WARN_ON_ONCE(rnp->qsmaskinitnext & ~rnp->qsmaskinit & rnp->qsmask &
		     rdp->grpmask);

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
	if (!rnp->gp_tasks && (blkd_state & RCU_GP_BLKD)) {
		WRITE_ONCE(rnp->gp_tasks, &t->rcu_node_entry);
		WARN_ON_ONCE(rnp->completedqs == rnp->gp_seq);
	}
	if (!rnp->exp_tasks && (blkd_state & RCU_EXP_BLKD))
		WRITE_ONCE(rnp->exp_tasks, &t->rcu_node_entry);
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
	if (blkd_state & RCU_EXP_BLKD && rdp->exp_deferred_qs)
		rcu_report_exp_rdp(rdp);
	else
		WARN_ON_ONCE(rdp->exp_deferred_qs);
}

/*
 * Record a preemptible-RCU quiescent state for the specified CPU.
 * Note that this does not necessarily mean that the task currently running
 * on the CPU is in a quiescent state:  Instead, it means that the current
 * grace period need not wait on any RCU read-side critical section that
 * starts later on this CPU.  It also means that if the current task is
 * in an RCU read-side critical section, it has already added itself to
 * some leaf rcu_node structure's ->blkd_tasks list.  In addition to the
 * current task, there might be any number of other tasks blocked while
 * in an RCU read-side critical section.
 *
 * Callers to this function must disable preemption.
 */
static void rcu_qs(void)
{
	RCU_LOCKDEP_WARN(preemptible(), "rcu_qs() invoked with preemption enabled!!!\n");
	if (__this_cpu_read(rcu_data.cpu_no_qs.s)) {
		trace_rcu_grace_period(TPS("rcu_preempt"),
				       __this_cpu_read(rcu_data.gp_seq),
				       TPS("cpuqs"));
		__this_cpu_write(rcu_data.cpu_no_qs.b.norm, false);
		barrier(); /* Coordinate with rcu_flavor_sched_clock_irq(). */
		WRITE_ONCE(current->rcu_read_unlock_special.b.need_qs, false);
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
void rcu_note_context_switch(bool preempt)
{
	struct task_struct *t = current;
	struct rcu_data *rdp = this_cpu_ptr(&rcu_data);
	struct rcu_node *rnp;

	trace_rcu_utilization(TPS("Start context switch"));
	lockdep_assert_irqs_disabled();
	WARN_ONCE(!preempt && rcu_preempt_depth() > 0, "Voluntary context switch within RCU read-side critical section!");
	if (rcu_preempt_depth() > 0 &&
	    !t->rcu_read_unlock_special.b.blocked) {

		/* Possibly blocking in an RCU read-side critical section. */
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
		trace_rcu_preempt_task(rcu_state.name,
				       t->pid,
				       (rnp->qsmask & rdp->grpmask)
				       ? rnp->gp_seq
				       : rcu_seq_snap(&rnp->gp_seq));
		rcu_preempt_ctxt_queue(rnp, rdp);
	} else {
		rcu_preempt_deferred_qs(t);
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
	rcu_qs();
	if (rdp->exp_deferred_qs)
		rcu_report_exp_rdp(rdp);
	rcu_tasks_qs(current, preempt);
	trace_rcu_utilization(TPS("End context switch"));
}
EXPORT_SYMBOL_GPL(rcu_note_context_switch);

/*
 * Check for preempted RCU readers blocking the current grace period
 * for the specified rcu_node structure.  If the caller needs a reliable
 * answer, it must hold the rcu_node's ->lock.
 */
static int rcu_preempt_blocked_readers_cgp(struct rcu_node *rnp)
{
	return READ_ONCE(rnp->gp_tasks) != NULL;
}

/* limit value for ->rcu_read_lock_nesting. */
#define RCU_NEST_PMAX (INT_MAX / 2)

static void rcu_preempt_read_enter(void)
{
	WRITE_ONCE(current->rcu_read_lock_nesting, READ_ONCE(current->rcu_read_lock_nesting) + 1);
}

static int rcu_preempt_read_exit(void)
{
	int ret = READ_ONCE(current->rcu_read_lock_nesting) - 1;

	WRITE_ONCE(current->rcu_read_lock_nesting, ret);
	return ret;
}

static void rcu_preempt_depth_set(int val)
{
	WRITE_ONCE(current->rcu_read_lock_nesting, val);
}

/*
 * Preemptible RCU implementation for rcu_read_lock().
 * Just increment ->rcu_read_lock_nesting, shared state will be updated
 * if we block.
 */
void __rcu_read_lock(void)
{
	rcu_preempt_read_enter();
	if (IS_ENABLED(CONFIG_PROVE_LOCKING))
		WARN_ON_ONCE(rcu_preempt_depth() > RCU_NEST_PMAX);
	if (IS_ENABLED(CONFIG_RCU_STRICT_GRACE_PERIOD) && rcu_state.gp_kthread)
		WRITE_ONCE(current->rcu_read_unlock_special.b.need_qs, true);
	barrier();  /* critical section after entry code. */
}
EXPORT_SYMBOL_GPL(__rcu_read_lock);

/*
 * Preemptible RCU implementation for rcu_read_unlock().
 * Decrement ->rcu_read_lock_nesting.  If the result is zero (outermost
 * rcu_read_unlock()) and ->rcu_read_unlock_special is non-zero, then
 * invoke rcu_read_unlock_special() to clean up after a context switch
 * in an RCU read-side critical section and other special cases.
 */
void __rcu_read_unlock(void)
{
	struct task_struct *t = current;

	barrier();  // critical section before exit code.
	if (rcu_preempt_read_exit() == 0) {
		barrier();  // critical-section exit before .s check.
		if (unlikely(READ_ONCE(t->rcu_read_unlock_special.s)))
			rcu_read_unlock_special(t);
	}
	if (IS_ENABLED(CONFIG_PROVE_LOCKING)) {
		int rrln = rcu_preempt_depth();

		WARN_ON_ONCE(rrln < 0 || rrln > RCU_NEST_PMAX);
	}
}
EXPORT_SYMBOL_GPL(__rcu_read_unlock);

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
 * Report deferred quiescent states.  The deferral time can
 * be quite short, for example, in the case of the call from
 * rcu_read_unlock_special().
 */
static void
rcu_preempt_deferred_qs_irqrestore(struct task_struct *t, unsigned long flags)
{
	bool empty_exp;
	bool empty_norm;
	bool empty_exp_now;
	struct list_head *np;
	bool drop_boost_mutex = false;
	struct rcu_data *rdp;
	struct rcu_node *rnp;
	union rcu_special special;

	/*
	 * If RCU core is waiting for this CPU to exit its critical section,
	 * report the fact that it has exited.  Because irqs are disabled,
	 * t->rcu_read_unlock_special cannot change.
	 */
	special = t->rcu_read_unlock_special;
	rdp = this_cpu_ptr(&rcu_data);
	if (!special.s && !rdp->exp_deferred_qs) {
		local_irq_restore(flags);
		return;
	}
	t->rcu_read_unlock_special.s = 0;
	if (special.b.need_qs) {
		if (IS_ENABLED(CONFIG_RCU_STRICT_GRACE_PERIOD)) {
			rcu_report_qs_rdp(rdp);
			udelay(rcu_unlock_delay);
		} else {
			rcu_qs();
		}
	}

	/*
	 * Respond to a request by an expedited grace period for a
	 * quiescent state from this CPU.  Note that requests from
	 * tasks are handled when removing the task from the
	 * blocked-tasks list below.
	 */
	if (rdp->exp_deferred_qs)
		rcu_report_exp_rdp(rdp);

	/* Clean up if blocked during RCU read-side critical section. */
	if (special.b.blocked) {

		/*
		 * Remove this task from the list it blocked on.  The task
		 * now remains queued on the rcu_node corresponding to the
		 * CPU it first blocked on, so there is no longer any need
		 * to loop.  Retain a WARN_ON_ONCE() out of sheer paranoia.
		 */
		rnp = t->rcu_blocked_node;
		raw_spin_lock_rcu_node(rnp); /* irqs already disabled. */
		WARN_ON_ONCE(rnp != t->rcu_blocked_node);
		WARN_ON_ONCE(!rcu_is_leaf_node(rnp));
		empty_norm = !rcu_preempt_blocked_readers_cgp(rnp);
		WARN_ON_ONCE(rnp->completedqs == rnp->gp_seq &&
			     (!empty_norm || rnp->qsmask));
		empty_exp = sync_rcu_exp_done(rnp);
		smp_mb(); /* ensure expedited fastpath sees end of RCU c-s. */
		np = rcu_next_node_entry(t, rnp);
		list_del_init(&t->rcu_node_entry);
		t->rcu_blocked_node = NULL;
		trace_rcu_unlock_preempted_task(TPS("rcu_preempt"),
						rnp->gp_seq, t->pid);
		if (&t->rcu_node_entry == rnp->gp_tasks)
			WRITE_ONCE(rnp->gp_tasks, np);
		if (&t->rcu_node_entry == rnp->exp_tasks)
			WRITE_ONCE(rnp->exp_tasks, np);
		if (IS_ENABLED(CONFIG_RCU_BOOST)) {
			/* Snapshot ->boost_mtx ownership w/rnp->lock held. */
			drop_boost_mutex = rt_mutex_owner(&rnp->boost_mtx.rtmutex) == t;
			if (&t->rcu_node_entry == rnp->boost_tasks)
				WRITE_ONCE(rnp->boost_tasks, np);
		}

		/*
		 * If this was the last task on the current list, and if
		 * we aren't waiting on any CPUs, report the quiescent state.
		 * Note that rcu_report_unblock_qs_rnp() releases rnp->lock,
		 * so we must take a snapshot of the expedited state.
		 */
		empty_exp_now = sync_rcu_exp_done(rnp);
		if (!empty_norm && !rcu_preempt_blocked_readers_cgp(rnp)) {
			trace_rcu_quiescent_state_report(TPS("preempt_rcu"),
							 rnp->gp_seq,
							 0, rnp->qsmask,
							 rnp->level,
							 rnp->grplo,
							 rnp->grphi,
							 !!rnp->gp_tasks);
			rcu_report_unblock_qs_rnp(rnp, flags);
		} else {
			raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
		}

		/*
		 * If this was the last task on the expedited lists,
		 * then we need to report up the rcu_node hierarchy.
		 */
		if (!empty_exp && empty_exp_now)
			rcu_report_exp_rnp(rnp, true);

		/* Unboost if we were boosted. */
		if (IS_ENABLED(CONFIG_RCU_BOOST) && drop_boost_mutex)
			rt_mutex_futex_unlock(&rnp->boost_mtx.rtmutex);
	} else {
		local_irq_restore(flags);
	}
}

/*
 * Is a deferred quiescent-state pending, and are we also not in
 * an RCU read-side critical section?  It is the caller's responsibility
 * to ensure it is otherwise safe to report any deferred quiescent
 * states.  The reason for this is that it is safe to report a
 * quiescent state during context switch even though preemption
 * is disabled.  This function cannot be expected to understand these
 * nuances, so the caller must handle them.
 */
static bool rcu_preempt_need_deferred_qs(struct task_struct *t)
{
	return (__this_cpu_read(rcu_data.exp_deferred_qs) ||
		READ_ONCE(t->rcu_read_unlock_special.s)) &&
	       rcu_preempt_depth() == 0;
}

/*
 * Report a deferred quiescent state if needed and safe to do so.
 * As with rcu_preempt_need_deferred_qs(), "safe" involves only
 * not being in an RCU read-side critical section.  The caller must
 * evaluate safety in terms of interrupt, softirq, and preemption
 * disabling.
 */
static void rcu_preempt_deferred_qs(struct task_struct *t)
{
	unsigned long flags;

	if (!rcu_preempt_need_deferred_qs(t))
		return;
	local_irq_save(flags);
	rcu_preempt_deferred_qs_irqrestore(t, flags);
}

/*
 * Minimal handler to give the scheduler a chance to re-evaluate.
 */
static void rcu_preempt_deferred_qs_handler(struct irq_work *iwp)
{
	struct rcu_data *rdp;

	rdp = container_of(iwp, struct rcu_data, defer_qs_iw);
	rdp->defer_qs_iw_pending = false;
}

/*
 * Handle special cases during rcu_read_unlock(), such as needing to
 * notify RCU core processing or task having blocked during the RCU
 * read-side critical section.
 */
static void rcu_read_unlock_special(struct task_struct *t)
{
	unsigned long flags;
	bool irqs_were_disabled;
	bool preempt_bh_were_disabled =
			!!(preempt_count() & (PREEMPT_MASK | SOFTIRQ_MASK));

	/* NMI handlers cannot block and cannot safely manipulate state. */
	if (in_nmi())
		return;

	local_irq_save(flags);
	irqs_were_disabled = irqs_disabled_flags(flags);
	if (preempt_bh_were_disabled || irqs_were_disabled) {
		bool expboost; // Expedited GP in flight or possible boosting.
		struct rcu_data *rdp = this_cpu_ptr(&rcu_data);
		struct rcu_node *rnp = rdp->mynode;

		expboost = (t->rcu_blocked_node && READ_ONCE(t->rcu_blocked_node->exp_tasks)) ||
			   (rdp->grpmask & READ_ONCE(rnp->expmask)) ||
			   (IS_ENABLED(CONFIG_RCU_STRICT_GRACE_PERIOD) &&
			   ((rdp->grpmask & READ_ONCE(rnp->qsmask)) || t->rcu_blocked_node)) ||
			   (IS_ENABLED(CONFIG_RCU_BOOST) && irqs_were_disabled &&
			    t->rcu_blocked_node);
		// Need to defer quiescent state until everything is enabled.
		if (use_softirq && (in_irq() || (expboost && !irqs_were_disabled))) {
			// Using softirq, safe to awaken, and either the
			// wakeup is free or there is either an expedited
			// GP in flight or a potential need to deboost.
			raise_softirq_irqoff(RCU_SOFTIRQ);
		} else {
			// Enabling BH or preempt does reschedule, so...
			// Also if no expediting and no possible deboosting,
			// slow is OK.  Plus nohz_full CPUs eventually get
			// tick enabled.
			set_tsk_need_resched(current);
			set_preempt_need_resched();
			if (IS_ENABLED(CONFIG_IRQ_WORK) && irqs_were_disabled &&
			    expboost && !rdp->defer_qs_iw_pending && cpu_online(rdp->cpu)) {
				// Get scheduler to re-evaluate and call hooks.
				// If !IRQ_WORK, FQS scan will eventually IPI.
				init_irq_work(&rdp->defer_qs_iw, rcu_preempt_deferred_qs_handler);
				rdp->defer_qs_iw_pending = true;
				irq_work_queue_on(&rdp->defer_qs_iw, rdp->cpu);
			}
		}
		local_irq_restore(flags);
		return;
	}
	rcu_preempt_deferred_qs_irqrestore(t, flags);
}

/*
 * Check that the list of blocked tasks for the newly completed grace
 * period is in fact empty.  It is a serious bug to complete a grace
 * period that still has RCU readers blocked!  This function must be
 * invoked -before- updating this rnp's ->gp_seq.
 *
 * Also, if there are blocked tasks on the list, they automatically
 * block the newly created grace period, so set up ->gp_tasks accordingly.
 */
static void rcu_preempt_check_blocked_tasks(struct rcu_node *rnp)
{
	struct task_struct *t;

	RCU_LOCKDEP_WARN(preemptible(), "rcu_preempt_check_blocked_tasks() invoked with preemption enabled!!!\n");
	raw_lockdep_assert_held_rcu_node(rnp);
	if (WARN_ON_ONCE(rcu_preempt_blocked_readers_cgp(rnp)))
		dump_blkd_tasks(rnp, 10);
	if (rcu_preempt_has_tasks(rnp) &&
	    (rnp->qsmaskinit || rnp->wait_blkd_tasks)) {
		WRITE_ONCE(rnp->gp_tasks, rnp->blkd_tasks.next);
		t = container_of(rnp->gp_tasks, struct task_struct,
				 rcu_node_entry);
		trace_rcu_unlock_preempted_task(TPS("rcu_preempt-GPS"),
						rnp->gp_seq, t->pid);
	}
	WARN_ON_ONCE(rnp->qsmask);
}

/*
 * Check for a quiescent state from the current CPU, including voluntary
 * context switches for Tasks RCU.  When a task blocks, the task is
 * recorded in the corresponding CPU's rcu_node structure, which is checked
 * elsewhere, hence this function need only check for quiescent states
 * related to the current CPU, not to those related to tasks.
 */
static void rcu_flavor_sched_clock_irq(int user)
{
	struct task_struct *t = current;

	lockdep_assert_irqs_disabled();
	if (user || rcu_is_cpu_rrupt_from_idle()) {
		rcu_note_voluntary_context_switch(current);
	}
	if (rcu_preempt_depth() > 0 ||
	    (preempt_count() & (PREEMPT_MASK | SOFTIRQ_MASK))) {
		/* No QS, force context switch if deferred. */
		if (rcu_preempt_need_deferred_qs(t)) {
			set_tsk_need_resched(t);
			set_preempt_need_resched();
		}
	} else if (rcu_preempt_need_deferred_qs(t)) {
		rcu_preempt_deferred_qs(t); /* Report deferred QS. */
		return;
	} else if (!WARN_ON_ONCE(rcu_preempt_depth())) {
		rcu_qs(); /* Report immediate QS. */
		return;
	}

	/* If GP is oldish, ask for help from rcu_read_unlock_special(). */
	if (rcu_preempt_depth() > 0 &&
	    __this_cpu_read(rcu_data.core_needs_qs) &&
	    __this_cpu_read(rcu_data.cpu_no_qs.b.norm) &&
	    !t->rcu_read_unlock_special.b.need_qs &&
	    time_after(jiffies, rcu_state.gp_start + HZ))
		t->rcu_read_unlock_special.b.need_qs = true;
}

/*
 * Check for a task exiting while in a preemptible-RCU read-side
 * critical section, clean up if so.  No need to issue warnings, as
 * debug_check_no_locks_held() already does this if lockdep is enabled.
 * Besides, if this function does anything other than just immediately
 * return, there was a bug of some sort.  Spewing warnings from this
 * function is like as not to simply obscure important prior warnings.
 */
void exit_rcu(void)
{
	struct task_struct *t = current;

	if (unlikely(!list_empty(&current->rcu_node_entry))) {
		rcu_preempt_depth_set(1);
		barrier();
		WRITE_ONCE(t->rcu_read_unlock_special.b.blocked, true);
	} else if (unlikely(rcu_preempt_depth())) {
		rcu_preempt_depth_set(1);
	} else {
		return;
	}
	__rcu_read_unlock();
	rcu_preempt_deferred_qs(current);
}

/*
 * Dump the blocked-tasks state, but limit the list dump to the
 * specified number of elements.
 */
static void
dump_blkd_tasks(struct rcu_node *rnp, int ncheck)
{
	int cpu;
	int i;
	struct list_head *lhp;
	bool onl;
	struct rcu_data *rdp;
	struct rcu_node *rnp1;

	raw_lockdep_assert_held_rcu_node(rnp);
	pr_info("%s: grp: %d-%d level: %d ->gp_seq %ld ->completedqs %ld\n",
		__func__, rnp->grplo, rnp->grphi, rnp->level,
		(long)READ_ONCE(rnp->gp_seq), (long)rnp->completedqs);
	for (rnp1 = rnp; rnp1; rnp1 = rnp1->parent)
		pr_info("%s: %d:%d ->qsmask %#lx ->qsmaskinit %#lx ->qsmaskinitnext %#lx\n",
			__func__, rnp1->grplo, rnp1->grphi, rnp1->qsmask, rnp1->qsmaskinit, rnp1->qsmaskinitnext);
	pr_info("%s: ->gp_tasks %p ->boost_tasks %p ->exp_tasks %p\n",
		__func__, READ_ONCE(rnp->gp_tasks), data_race(rnp->boost_tasks),
		READ_ONCE(rnp->exp_tasks));
	pr_info("%s: ->blkd_tasks", __func__);
	i = 0;
	list_for_each(lhp, &rnp->blkd_tasks) {
		pr_cont(" %p", lhp);
		if (++i >= ncheck)
			break;
	}
	pr_cont("\n");
	for (cpu = rnp->grplo; cpu <= rnp->grphi; cpu++) {
		rdp = per_cpu_ptr(&rcu_data, cpu);
		onl = !!(rdp->grpmask & rcu_rnp_online_cpus(rnp));
		pr_info("\t%d: %c online: %ld(%d) offline: %ld(%d)\n",
			cpu, ".o"[onl],
			(long)rdp->rcu_onl_gp_seq, rdp->rcu_onl_gp_flags,
			(long)rdp->rcu_ofl_gp_seq, rdp->rcu_ofl_gp_flags);
	}
}

#else /* #ifdef CONFIG_PREEMPT_RCU */

/*
 * If strict grace periods are enabled, and if the calling
 * __rcu_read_unlock() marks the beginning of a quiescent state, immediately
 * report that quiescent state and, if requested, spin for a bit.
 */
void rcu_read_unlock_strict(void)
{
	struct rcu_data *rdp;

	if (!IS_ENABLED(CONFIG_RCU_STRICT_GRACE_PERIOD) ||
	   irqs_disabled() || preempt_count() || !rcu_state.gp_kthread)
		return;
	rdp = this_cpu_ptr(&rcu_data);
	rcu_report_qs_rdp(rdp);
	udelay(rcu_unlock_delay);
}
EXPORT_SYMBOL_GPL(rcu_read_unlock_strict);

/*
 * Tell them what RCU they are running.
 */
static void __init rcu_bootup_announce(void)
{
	pr_info("Hierarchical RCU implementation.\n");
	rcu_bootup_announce_oddness();
}

/*
 * Note a quiescent state for PREEMPTION=n.  Because we do not need to know
 * how many quiescent states passed, just if there was at least one since
 * the start of the grace period, this just sets a flag.  The caller must
 * have disabled preemption.
 */
static void rcu_qs(void)
{
	RCU_LOCKDEP_WARN(preemptible(), "rcu_qs() invoked with preemption enabled!!!");
	if (!__this_cpu_read(rcu_data.cpu_no_qs.s))
		return;
	trace_rcu_grace_period(TPS("rcu_sched"),
			       __this_cpu_read(rcu_data.gp_seq), TPS("cpuqs"));
	__this_cpu_write(rcu_data.cpu_no_qs.b.norm, false);
	if (!__this_cpu_read(rcu_data.cpu_no_qs.b.exp))
		return;
	__this_cpu_write(rcu_data.cpu_no_qs.b.exp, false);
	rcu_report_exp_rdp(this_cpu_ptr(&rcu_data));
}

/*
 * Register an urgently needed quiescent state.  If there is an
 * emergency, invoke rcu_momentary_dyntick_idle() to do a heavy-weight
 * dyntick-idle quiescent state visible to other CPUs, which will in
 * some cases serve for expedited as well as normal grace periods.
 * Either way, register a lightweight quiescent state.
 */
void rcu_all_qs(void)
{
	unsigned long flags;

	if (!raw_cpu_read(rcu_data.rcu_urgent_qs))
		return;
	preempt_disable();
	/* Load rcu_urgent_qs before other flags. */
	if (!smp_load_acquire(this_cpu_ptr(&rcu_data.rcu_urgent_qs))) {
		preempt_enable();
		return;
	}
	this_cpu_write(rcu_data.rcu_urgent_qs, false);
	if (unlikely(raw_cpu_read(rcu_data.rcu_need_heavy_qs))) {
		local_irq_save(flags);
		rcu_momentary_dyntick_idle();
		local_irq_restore(flags);
	}
	rcu_qs();
	preempt_enable();
}
EXPORT_SYMBOL_GPL(rcu_all_qs);

/*
 * Note a PREEMPTION=n context switch. The caller must have disabled interrupts.
 */
void rcu_note_context_switch(bool preempt)
{
	trace_rcu_utilization(TPS("Start context switch"));
	rcu_qs();
	/* Load rcu_urgent_qs before other flags. */
	if (!smp_load_acquire(this_cpu_ptr(&rcu_data.rcu_urgent_qs)))
		goto out;
	this_cpu_write(rcu_data.rcu_urgent_qs, false);
	if (unlikely(raw_cpu_read(rcu_data.rcu_need_heavy_qs)))
		rcu_momentary_dyntick_idle();
	rcu_tasks_qs(current, preempt);
out:
	trace_rcu_utilization(TPS("End context switch"));
}
EXPORT_SYMBOL_GPL(rcu_note_context_switch);

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
 * Because there is no preemptible RCU, there can be no deferred quiescent
 * states.
 */
static bool rcu_preempt_need_deferred_qs(struct task_struct *t)
{
	return false;
}
static void rcu_preempt_deferred_qs(struct task_struct *t) { }

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
 * Check to see if this CPU is in a non-context-switch quiescent state,
 * namely user mode and idle loop.
 */
static void rcu_flavor_sched_clock_irq(int user)
{
	if (user || rcu_is_cpu_rrupt_from_idle()) {

		/*
		 * Get here if this CPU took its interrupt from user
		 * mode or from the idle loop, and if this is not a
		 * nested interrupt.  In this case, the CPU is in
		 * a quiescent state, so note it.
		 *
		 * No memory barrier is required here because rcu_qs()
		 * references only CPU-local variables that other CPUs
		 * neither access nor modify, at least not while the
		 * corresponding CPU is online.
		 */

		rcu_qs();
	}
}

/*
 * Because preemptible RCU does not exist, tasks cannot possibly exit
 * while in preemptible RCU read-side critical sections.
 */
void exit_rcu(void)
{
}

/*
 * Dump the guaranteed-empty blocked-tasks state.  Trust but verify.
 */
static void
dump_blkd_tasks(struct rcu_node *rnp, int ncheck)
{
	WARN_ON_ONCE(!list_empty(&rnp->blkd_tasks));
}

#endif /* #else #ifdef CONFIG_PREEMPT_RCU */

/*
 * If boosting, set rcuc kthreads to realtime priority.
 */
static void rcu_cpu_kthread_setup(unsigned int cpu)
{
#ifdef CONFIG_RCU_BOOST
	struct sched_param sp;

	sp.sched_priority = kthread_prio;
	sched_setscheduler_nocheck(current, SCHED_FIFO, &sp);
#endif /* #ifdef CONFIG_RCU_BOOST */
}

#ifdef CONFIG_RCU_BOOST

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
	if (rnp->exp_tasks != NULL)
		tb = rnp->exp_tasks;
	else
		tb = rnp->boost_tasks;

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
	rt_mutex_init_proxy_locked(&rnp->boost_mtx.rtmutex, t);
	raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
	/* Lock only for side effect: boosts task t's priority. */
	rt_mutex_lock(&rnp->boost_mtx);
	rt_mutex_unlock(&rnp->boost_mtx);  /* Then keep lockdep happy. */
	rnp->n_boosts++;

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
		WRITE_ONCE(rnp->boost_kthread_status, RCU_KTHREAD_WAITING);
		trace_rcu_utilization(TPS("End boost kthread@rcu_wait"));
		rcu_wait(READ_ONCE(rnp->boost_tasks) ||
			 READ_ONCE(rnp->exp_tasks));
		trace_rcu_utilization(TPS("Start boost kthread@rcu_wait"));
		WRITE_ONCE(rnp->boost_kthread_status, RCU_KTHREAD_RUNNING);
		more2boost = rcu_boost(rnp);
		if (more2boost)
			spincnt++;
		else
			spincnt = 0;
		if (spincnt > 10) {
			WRITE_ONCE(rnp->boost_kthread_status, RCU_KTHREAD_YIELDING);
			trace_rcu_utilization(TPS("End boost kthread@rcu_yield"));
			schedule_timeout_idle(2);
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
	raw_lockdep_assert_held_rcu_node(rnp);
	if (!rcu_preempt_blocked_readers_cgp(rnp) && rnp->exp_tasks == NULL) {
		raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
		return;
	}
	if (rnp->exp_tasks != NULL ||
	    (rnp->gp_tasks != NULL &&
	     rnp->boost_tasks == NULL &&
	     rnp->qsmask == 0 &&
	     (!time_after(rnp->boost_time, jiffies) || rcu_state.cbovld))) {
		if (rnp->exp_tasks == NULL)
			WRITE_ONCE(rnp->boost_tasks, rnp->gp_tasks);
		raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
		rcu_wake_cond(rnp->boost_kthread_task,
			      READ_ONCE(rnp->boost_kthread_status));
	} else {
		raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
	}
}

/*
 * Is the current CPU running the RCU-callbacks kthread?
 * Caller must have preemption disabled.
 */
static bool rcu_is_callbacks_kthread(void)
{
	return __this_cpu_read(rcu_data.rcu_cpu_kthread_task) == current;
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
static void rcu_spawn_one_boost_kthread(struct rcu_node *rnp)
{
	unsigned long flags;
	int rnp_index = rnp - rcu_get_root();
	struct sched_param sp;
	struct task_struct *t;

	if (rnp->boost_kthread_task || !rcu_scheduler_fully_active)
		return;

	rcu_state.boost = 1;

	t = kthread_create(rcu_boost_kthread, (void *)rnp,
			   "rcub/%d", rnp_index);
	if (WARN_ON_ONCE(IS_ERR(t)))
		return;

	raw_spin_lock_irqsave_rcu_node(rnp, flags);
	rnp->boost_kthread_task = t;
	raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
	sp.sched_priority = kthread_prio;
	sched_setscheduler_nocheck(t, SCHED_FIFO, &sp);
	wake_up_process(t); /* get to TASK_INTERRUPTIBLE quickly. */
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

/*
 * Spawn boost kthreads -- called as soon as the scheduler is running.
 */
static void __init rcu_spawn_boost_kthreads(void)
{
	struct rcu_node *rnp;

	rcu_for_each_leaf_node(rnp)
		if (rcu_rnp_online_cpus(rnp))
			rcu_spawn_one_boost_kthread(rnp);
}

#else /* #ifdef CONFIG_RCU_BOOST */

static void rcu_initiate_boost(struct rcu_node *rnp, unsigned long flags)
	__releases(rnp->lock)
{
	raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
}

static bool rcu_is_callbacks_kthread(void)
{
	return false;
}

static void rcu_preempt_boost_start_gp(struct rcu_node *rnp)
{
}

static void rcu_spawn_one_boost_kthread(struct rcu_node *rnp)
{
}

static void rcu_boost_kthread_setaffinity(struct rcu_node *rnp, int outgoingcpu)
{
}

static void __init rcu_spawn_boost_kthreads(void)
{
}

#endif /* #else #ifdef CONFIG_RCU_BOOST */

#if !defined(CONFIG_RCU_FAST_NO_HZ)

/*
 * Check to see if any future non-offloaded RCU-related work will need
 * to be done by the current CPU, even if none need be done immediately,
 * returning 1 if so.  This function is part of the RCU implementation;
 * it is -not- an exported member of the RCU API.
 *
 * Because we not have RCU_FAST_NO_HZ, just check whether or not this
 * CPU has RCU callbacks queued.
 */
int rcu_needs_cpu(u64 basemono, u64 *nextevt)
{
	*nextevt = KTIME_MAX;
	return !rcu_segcblist_empty(&this_cpu_ptr(&rcu_data)->cblist) &&
		!rcu_rdp_is_offloaded(this_cpu_ptr(&rcu_data));
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

#else /* #if !defined(CONFIG_RCU_FAST_NO_HZ) */

/*
 * This code is invoked when a CPU goes idle, at which point we want
 * to have the CPU do everything required for RCU so that it can enter
 * the energy-efficient dyntick-idle mode.
 *
 * The following preprocessor symbol controls this:
 *
 * RCU_IDLE_GP_DELAY gives the number of jiffies that a CPU is permitted
 *	to sleep in dyntick-idle mode with RCU callbacks pending.  This
 *	is sized to be roughly one RCU grace period.  Those energy-efficiency
 *	benchmarkers who might otherwise be tempted to set this to a large
 *	number, be warned: Setting RCU_IDLE_GP_DELAY too high can hang your
 *	system.  And if you are -that- concerned about energy efficiency,
 *	just power the system down and be done with it!
 *
 * The value below works well in practice.  If future workloads require
 * adjustment, they can be converted into kernel config parameters, though
 * making the state machine smarter might be a better option.
 */
#define RCU_IDLE_GP_DELAY 4		/* Roughly one grace period. */

static int rcu_idle_gp_delay = RCU_IDLE_GP_DELAY;
module_param(rcu_idle_gp_delay, int, 0644);

/*
 * Try to advance callbacks on the current CPU, but only if it has been
 * awhile since the last time we did so.  Afterwards, if there are any
 * callbacks ready for immediate invocation, return true.
 */
static bool __maybe_unused rcu_try_advance_all_cbs(void)
{
	bool cbs_ready = false;
	struct rcu_data *rdp = this_cpu_ptr(&rcu_data);
	struct rcu_node *rnp;

	/* Exit early if we advanced recently. */
	if (jiffies == rdp->last_advance_all)
		return false;
	rdp->last_advance_all = jiffies;

	rnp = rdp->mynode;

	/*
	 * Don't bother checking unless a grace period has
	 * completed since we last checked and there are
	 * callbacks not yet ready to invoke.
	 */
	if ((rcu_seq_completed_gp(rdp->gp_seq,
				  rcu_seq_current(&rnp->gp_seq)) ||
	     unlikely(READ_ONCE(rdp->gpwrap))) &&
	    rcu_segcblist_pend_cbs(&rdp->cblist))
		note_gp_changes(rdp);

	if (rcu_segcblist_ready_cbs(&rdp->cblist))
		cbs_ready = true;
	return cbs_ready;
}

/*
 * Allow the CPU to enter dyntick-idle mode unless it has callbacks ready
 * to invoke.  If the CPU has callbacks, try to advance them.  Tell the
 * caller about what to set the timeout.
 *
 * The caller must have disabled interrupts.
 */
int rcu_needs_cpu(u64 basemono, u64 *nextevt)
{
	struct rcu_data *rdp = this_cpu_ptr(&rcu_data);
	unsigned long dj;

	lockdep_assert_irqs_disabled();

	/* If no non-offloaded callbacks, RCU doesn't need the CPU. */
	if (rcu_segcblist_empty(&rdp->cblist) ||
	    rcu_rdp_is_offloaded(rdp)) {
		*nextevt = KTIME_MAX;
		return 0;
	}

	/* Attempt to advance callbacks. */
	if (rcu_try_advance_all_cbs()) {
		/* Some ready to invoke, so initiate later invocation. */
		invoke_rcu_core();
		return 1;
	}
	rdp->last_accelerate = jiffies;

	/* Request timer and round. */
	dj = round_up(rcu_idle_gp_delay + jiffies, rcu_idle_gp_delay) - jiffies;

	*nextevt = basemono + dj * TICK_NSEC;
	return 0;
}

/*
 * Prepare a CPU for idle from an RCU perspective.  The first major task is to
 * sense whether nohz mode has been enabled or disabled via sysfs.  The second
 * major task is to accelerate (that is, assign grace-period numbers to) any
 * recently arrived callbacks.
 *
 * The caller must have disabled interrupts.
 */
static void rcu_prepare_for_idle(void)
{
	bool needwake;
	struct rcu_data *rdp = this_cpu_ptr(&rcu_data);
	struct rcu_node *rnp;
	int tne;

	lockdep_assert_irqs_disabled();
	if (rcu_rdp_is_offloaded(rdp))
		return;

	/* Handle nohz enablement switches conservatively. */
	tne = READ_ONCE(tick_nohz_active);
	if (tne != rdp->tick_nohz_enabled_snap) {
		if (!rcu_segcblist_empty(&rdp->cblist))
			invoke_rcu_core(); /* force nohz to see update. */
		rdp->tick_nohz_enabled_snap = tne;
		return;
	}
	if (!tne)
		return;

	/*
	 * If we have not yet accelerated this jiffy, accelerate all
	 * callbacks on this CPU.
	 */
	if (rdp->last_accelerate == jiffies)
		return;
	rdp->last_accelerate = jiffies;
	if (rcu_segcblist_pend_cbs(&rdp->cblist)) {
		rnp = rdp->mynode;
		raw_spin_lock_rcu_node(rnp); /* irqs already disabled. */
		needwake = rcu_accelerate_cbs(rnp, rdp);
		raw_spin_unlock_rcu_node(rnp); /* irqs remain disabled. */
		if (needwake)
			rcu_gp_kthread_wake();
	}
}

/*
 * Clean up for exit from idle.  Attempt to advance callbacks based on
 * any grace periods that elapsed while the CPU was idle, and if any
 * callbacks are now ready to invoke, initiate invocation.
 */
static void rcu_cleanup_after_idle(void)
{
	struct rcu_data *rdp = this_cpu_ptr(&rcu_data);

	lockdep_assert_irqs_disabled();
	if (rcu_rdp_is_offloaded(rdp))
		return;
	if (rcu_try_advance_all_cbs())
		invoke_rcu_core();
}

#endif /* #else #if !defined(CONFIG_RCU_FAST_NO_HZ) */

/*
 * Is this CPU a NO_HZ_FULL CPU that should ignore RCU so that the
 * grace-period kthread will do force_quiescent_state() processing?
 * The idea is to avoid waking up RCU core processing on such a
 * CPU unless the grace period has extended for too long.
 *
 * This code relies on the fact that all NO_HZ_FULL CPUs are also
 * CONFIG_RCU_NOCB_CPU CPUs.
 */
static bool rcu_nohz_full_cpu(void)
{
#ifdef CONFIG_NO_HZ_FULL
	if (tick_nohz_full_cpu(smp_processor_id()) &&
	    (!rcu_gp_in_progress() ||
	     time_before(jiffies, READ_ONCE(rcu_state.gp_start) + HZ)))
		return true;
#endif /* #ifdef CONFIG_NO_HZ_FULL */
	return false;
}

/*
 * Bind the RCU grace-period kthreads to the housekeeping CPU.
 */
static void rcu_bind_gp_kthread(void)
{
	if (!tick_nohz_full_enabled())
		return;
	housekeeping_affine(current, HK_FLAG_RCU);
}

/* Record the current task on dyntick-idle entry. */
static __always_inline void rcu_dynticks_task_enter(void)
{
#if defined(CONFIG_TASKS_RCU) && defined(CONFIG_NO_HZ_FULL)
	WRITE_ONCE(current->rcu_tasks_idle_cpu, smp_processor_id());
#endif /* #if defined(CONFIG_TASKS_RCU) && defined(CONFIG_NO_HZ_FULL) */
}

/* Record no current task on dyntick-idle exit. */
static __always_inline void rcu_dynticks_task_exit(void)
{
#if defined(CONFIG_TASKS_RCU) && defined(CONFIG_NO_HZ_FULL)
	WRITE_ONCE(current->rcu_tasks_idle_cpu, -1);
#endif /* #if defined(CONFIG_TASKS_RCU) && defined(CONFIG_NO_HZ_FULL) */
}

/* Turn on heavyweight RCU tasks trace readers on idle/user entry. */
static __always_inline void rcu_dynticks_task_trace_enter(void)
{
#ifdef CONFIG_TASKS_TRACE_RCU
	if (IS_ENABLED(CONFIG_TASKS_TRACE_RCU_READ_MB))
		current->trc_reader_special.b.need_mb = true;
#endif /* #ifdef CONFIG_TASKS_TRACE_RCU */
}

/* Turn off heavyweight RCU tasks trace readers on idle/user exit. */
static __always_inline void rcu_dynticks_task_trace_exit(void)
{
#ifdef CONFIG_TASKS_TRACE_RCU
	if (IS_ENABLED(CONFIG_TASKS_TRACE_RCU_READ_MB))
		current->trc_reader_special.b.need_mb = false;
#endif /* #ifdef CONFIG_TASKS_TRACE_RCU */
}
