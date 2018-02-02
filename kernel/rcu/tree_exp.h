/*
 * RCU expedited grace periods
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
 * Copyright IBM Corporation, 2016
 *
 * Authors: Paul E. McKenney <paulmck@linux.vnet.ibm.com>
 */

/*
 * Record the start of an expedited grace period.
 */
static void rcu_exp_gp_seq_start(struct rcu_state *rsp)
{
	rcu_seq_start(&rsp->expedited_sequence);
}

/*
 * Return then value that expedited-grace-period counter will have
 * at the end of the current grace period.
 */
static __maybe_unused unsigned long rcu_exp_gp_seq_endval(struct rcu_state *rsp)
{
	return rcu_seq_endval(&rsp->expedited_sequence);
}

/*
 * Record the end of an expedited grace period.
 */
static void rcu_exp_gp_seq_end(struct rcu_state *rsp)
{
	rcu_seq_end(&rsp->expedited_sequence);
	smp_mb(); /* Ensure that consecutive grace periods serialize. */
}

/*
 * Take a snapshot of the expedited-grace-period counter.
 */
static unsigned long rcu_exp_gp_seq_snap(struct rcu_state *rsp)
{
	unsigned long s;

	smp_mb(); /* Caller's modifications seen first by other CPUs. */
	s = rcu_seq_snap(&rsp->expedited_sequence);
	trace_rcu_exp_grace_period(rsp->name, s, TPS("snap"));
	return s;
}

/*
 * Given a counter snapshot from rcu_exp_gp_seq_snap(), return true
 * if a full expedited grace period has elapsed since that snapshot
 * was taken.
 */
static bool rcu_exp_gp_seq_done(struct rcu_state *rsp, unsigned long s)
{
	return rcu_seq_done(&rsp->expedited_sequence, s);
}

/*
 * Reset the ->expmaskinit values in the rcu_node tree to reflect any
 * recent CPU-online activity.  Note that these masks are not cleared
 * when CPUs go offline, so they reflect the union of all CPUs that have
 * ever been online.  This means that this function normally takes its
 * no-work-to-do fastpath.
 */
static void sync_exp_reset_tree_hotplug(struct rcu_state *rsp)
{
	bool done;
	unsigned long flags;
	unsigned long mask;
	unsigned long oldmask;
	int ncpus = smp_load_acquire(&rsp->ncpus); /* Order against locking. */
	struct rcu_node *rnp;
	struct rcu_node *rnp_up;

	/* If no new CPUs onlined since last time, nothing to do. */
	if (likely(ncpus == rsp->ncpus_snap))
		return;
	rsp->ncpus_snap = ncpus;

	/*
	 * Each pass through the following loop propagates newly onlined
	 * CPUs for the current rcu_node structure up the rcu_node tree.
	 */
	rcu_for_each_leaf_node(rsp, rnp) {
		raw_spin_lock_irqsave_rcu_node(rnp, flags);
		if (rnp->expmaskinit == rnp->expmaskinitnext) {
			raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
			continue;  /* No new CPUs, nothing to do. */
		}

		/* Update this node's mask, track old value for propagation. */
		oldmask = rnp->expmaskinit;
		rnp->expmaskinit = rnp->expmaskinitnext;
		raw_spin_unlock_irqrestore_rcu_node(rnp, flags);

		/* If was already nonzero, nothing to propagate. */
		if (oldmask)
			continue;

		/* Propagate the new CPU up the tree. */
		mask = rnp->grpmask;
		rnp_up = rnp->parent;
		done = false;
		while (rnp_up) {
			raw_spin_lock_irqsave_rcu_node(rnp_up, flags);
			if (rnp_up->expmaskinit)
				done = true;
			rnp_up->expmaskinit |= mask;
			raw_spin_unlock_irqrestore_rcu_node(rnp_up, flags);
			if (done)
				break;
			mask = rnp_up->grpmask;
			rnp_up = rnp_up->parent;
		}
	}
}

/*
 * Reset the ->expmask values in the rcu_node tree in preparation for
 * a new expedited grace period.
 */
static void __maybe_unused sync_exp_reset_tree(struct rcu_state *rsp)
{
	unsigned long flags;
	struct rcu_node *rnp;

	sync_exp_reset_tree_hotplug(rsp);
	rcu_for_each_node_breadth_first(rsp, rnp) {
		raw_spin_lock_irqsave_rcu_node(rnp, flags);
		WARN_ON_ONCE(rnp->expmask);
		rnp->expmask = rnp->expmaskinit;
		raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
	}
}

/*
 * Return non-zero if there is no RCU expedited grace period in progress
 * for the specified rcu_node structure, in other words, if all CPUs and
 * tasks covered by the specified rcu_node structure have done their bit
 * for the current expedited grace period.  Works only for preemptible
 * RCU -- other RCU implementation use other means.
 *
 * Caller must hold the rcu_state's exp_mutex.
 */
static bool sync_rcu_preempt_exp_done(struct rcu_node *rnp)
{
	return rnp->exp_tasks == NULL &&
	       READ_ONCE(rnp->expmask) == 0;
}

/*
 * Report the exit from RCU read-side critical section for the last task
 * that queued itself during or before the current expedited preemptible-RCU
 * grace period.  This event is reported either to the rcu_node structure on
 * which the task was queued or to one of that rcu_node structure's ancestors,
 * recursively up the tree.  (Calm down, calm down, we do the recursion
 * iteratively!)
 *
 * Caller must hold the rcu_state's exp_mutex and the specified rcu_node
 * structure's ->lock.
 */
static void __rcu_report_exp_rnp(struct rcu_state *rsp, struct rcu_node *rnp,
				 bool wake, unsigned long flags)
	__releases(rnp->lock)
{
	unsigned long mask;

	for (;;) {
		if (!sync_rcu_preempt_exp_done(rnp)) {
			if (!rnp->expmask)
				rcu_initiate_boost(rnp, flags);
			else
				raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
			break;
		}
		if (rnp->parent == NULL) {
			raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
			if (wake) {
				smp_mb(); /* EGP done before wake_up(). */
				swake_up(&rsp->expedited_wq);
			}
			break;
		}
		mask = rnp->grpmask;
		raw_spin_unlock_rcu_node(rnp); /* irqs remain disabled */
		rnp = rnp->parent;
		raw_spin_lock_rcu_node(rnp); /* irqs already disabled */
		WARN_ON_ONCE(!(rnp->expmask & mask));
		rnp->expmask &= ~mask;
	}
}

/*
 * Report expedited quiescent state for specified node.  This is a
 * lock-acquisition wrapper function for __rcu_report_exp_rnp().
 *
 * Caller must hold the rcu_state's exp_mutex.
 */
static void __maybe_unused rcu_report_exp_rnp(struct rcu_state *rsp,
					      struct rcu_node *rnp, bool wake)
{
	unsigned long flags;

	raw_spin_lock_irqsave_rcu_node(rnp, flags);
	__rcu_report_exp_rnp(rsp, rnp, wake, flags);
}

/*
 * Report expedited quiescent state for multiple CPUs, all covered by the
 * specified leaf rcu_node structure.  Caller must hold the rcu_state's
 * exp_mutex.
 */
static void rcu_report_exp_cpu_mult(struct rcu_state *rsp, struct rcu_node *rnp,
				    unsigned long mask, bool wake)
{
	unsigned long flags;

	raw_spin_lock_irqsave_rcu_node(rnp, flags);
	if (!(rnp->expmask & mask)) {
		raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
		return;
	}
	rnp->expmask &= ~mask;
	__rcu_report_exp_rnp(rsp, rnp, wake, flags); /* Releases rnp->lock. */
}

/*
 * Report expedited quiescent state for specified rcu_data (CPU).
 */
static void rcu_report_exp_rdp(struct rcu_state *rsp, struct rcu_data *rdp,
			       bool wake)
{
	rcu_report_exp_cpu_mult(rsp, rdp->mynode, rdp->grpmask, wake);
}

/* Common code for synchronize_{rcu,sched}_expedited() work-done checking. */
static bool sync_exp_work_done(struct rcu_state *rsp, atomic_long_t *stat,
			       unsigned long s)
{
	if (rcu_exp_gp_seq_done(rsp, s)) {
		trace_rcu_exp_grace_period(rsp->name, s, TPS("done"));
		/* Ensure test happens before caller kfree(). */
		smp_mb__before_atomic(); /* ^^^ */
		atomic_long_inc(stat);
		return true;
	}
	return false;
}

/*
 * Funnel-lock acquisition for expedited grace periods.  Returns true
 * if some other task completed an expedited grace period that this task
 * can piggy-back on, and with no mutex held.  Otherwise, returns false
 * with the mutex held, indicating that the caller must actually do the
 * expedited grace period.
 */
static bool exp_funnel_lock(struct rcu_state *rsp, unsigned long s)
{
	struct rcu_data *rdp = per_cpu_ptr(rsp->rda, raw_smp_processor_id());
	struct rcu_node *rnp = rdp->mynode;
	struct rcu_node *rnp_root = rcu_get_root(rsp);

	/* Low-contention fastpath. */
	if (ULONG_CMP_LT(READ_ONCE(rnp->exp_seq_rq), s) &&
	    (rnp == rnp_root ||
	     ULONG_CMP_LT(READ_ONCE(rnp_root->exp_seq_rq), s)) &&
	    mutex_trylock(&rsp->exp_mutex))
		goto fastpath;

	/*
	 * Each pass through the following loop works its way up
	 * the rcu_node tree, returning if others have done the work or
	 * otherwise falls through to acquire rsp->exp_mutex.  The mapping
	 * from CPU to rcu_node structure can be inexact, as it is just
	 * promoting locality and is not strictly needed for correctness.
	 */
	for (; rnp != NULL; rnp = rnp->parent) {
		if (sync_exp_work_done(rsp, &rdp->exp_workdone1, s))
			return true;

		/* Work not done, either wait here or go up. */
		spin_lock(&rnp->exp_lock);
		if (ULONG_CMP_GE(rnp->exp_seq_rq, s)) {

			/* Someone else doing GP, so wait for them. */
			spin_unlock(&rnp->exp_lock);
			trace_rcu_exp_funnel_lock(rsp->name, rnp->level,
						  rnp->grplo, rnp->grphi,
						  TPS("wait"));
			wait_event(rnp->exp_wq[rcu_seq_ctr(s) & 0x3],
				   sync_exp_work_done(rsp,
						      &rdp->exp_workdone2, s));
			return true;
		}
		rnp->exp_seq_rq = s; /* Followers can wait on us. */
		spin_unlock(&rnp->exp_lock);
		trace_rcu_exp_funnel_lock(rsp->name, rnp->level, rnp->grplo,
					  rnp->grphi, TPS("nxtlvl"));
	}
	mutex_lock(&rsp->exp_mutex);
fastpath:
	if (sync_exp_work_done(rsp, &rdp->exp_workdone3, s)) {
		mutex_unlock(&rsp->exp_mutex);
		return true;
	}
	rcu_exp_gp_seq_start(rsp);
	trace_rcu_exp_grace_period(rsp->name, s, TPS("start"));
	return false;
}

/* Invoked on each online non-idle CPU for expedited quiescent state. */
static void sync_sched_exp_handler(void *data)
{
	struct rcu_data *rdp;
	struct rcu_node *rnp;
	struct rcu_state *rsp = data;

	rdp = this_cpu_ptr(rsp->rda);
	rnp = rdp->mynode;
	if (!(READ_ONCE(rnp->expmask) & rdp->grpmask) ||
	    __this_cpu_read(rcu_sched_data.cpu_no_qs.b.exp))
		return;
	if (rcu_is_cpu_rrupt_from_idle()) {
		rcu_report_exp_rdp(&rcu_sched_state,
				   this_cpu_ptr(&rcu_sched_data), true);
		return;
	}
	__this_cpu_write(rcu_sched_data.cpu_no_qs.b.exp, true);
	/* Store .exp before .rcu_urgent_qs. */
	smp_store_release(this_cpu_ptr(&rcu_dynticks.rcu_urgent_qs), true);
	resched_cpu(smp_processor_id());
}

/* Send IPI for expedited cleanup if needed at end of CPU-hotplug operation. */
static void sync_sched_exp_online_cleanup(int cpu)
{
	struct rcu_data *rdp;
	int ret;
	struct rcu_node *rnp;
	struct rcu_state *rsp = &rcu_sched_state;

	rdp = per_cpu_ptr(rsp->rda, cpu);
	rnp = rdp->mynode;
	if (!(READ_ONCE(rnp->expmask) & rdp->grpmask))
		return;
	ret = smp_call_function_single(cpu, sync_sched_exp_handler, rsp, 0);
	WARN_ON_ONCE(ret);
}

/*
 * Select the CPUs within the specified rcu_node that the upcoming
 * expedited grace period needs to wait for.
 */
static void sync_rcu_exp_select_node_cpus(struct work_struct *wp)
{
	int cpu;
	unsigned long flags;
	smp_call_func_t func;
	unsigned long mask_ofl_test;
	unsigned long mask_ofl_ipi;
	int ret;
	struct rcu_exp_work *rewp =
		container_of(wp, struct rcu_exp_work, rew_work);
	struct rcu_node *rnp = container_of(rewp, struct rcu_node, rew);
	struct rcu_state *rsp = rewp->rew_rsp;

	func = rewp->rew_func;
	raw_spin_lock_irqsave_rcu_node(rnp, flags);

	/* Each pass checks a CPU for identity, offline, and idle. */
	mask_ofl_test = 0;
	for_each_leaf_node_cpu_mask(rnp, cpu, rnp->expmask) {
		unsigned long mask = leaf_node_cpu_bit(rnp, cpu);
		struct rcu_data *rdp = per_cpu_ptr(rsp->rda, cpu);
		struct rcu_dynticks *rdtp = per_cpu_ptr(&rcu_dynticks, cpu);
		int snap;

		if (raw_smp_processor_id() == cpu ||
		    !(rnp->qsmaskinitnext & mask)) {
			mask_ofl_test |= mask;
		} else {
			snap = rcu_dynticks_snap(rdtp);
			if (rcu_dynticks_in_eqs(snap))
				mask_ofl_test |= mask;
			else
				rdp->exp_dynticks_snap = snap;
		}
	}
	mask_ofl_ipi = rnp->expmask & ~mask_ofl_test;

	/*
	 * Need to wait for any blocked tasks as well.	Note that
	 * additional blocking tasks will also block the expedited GP
	 * until such time as the ->expmask bits are cleared.
	 */
	if (rcu_preempt_has_tasks(rnp))
		rnp->exp_tasks = rnp->blkd_tasks.next;
	raw_spin_unlock_irqrestore_rcu_node(rnp, flags);

	/* IPI the remaining CPUs for expedited quiescent state. */
	for_each_leaf_node_cpu_mask(rnp, cpu, rnp->expmask) {
		unsigned long mask = leaf_node_cpu_bit(rnp, cpu);
		struct rcu_data *rdp = per_cpu_ptr(rsp->rda, cpu);

		if (!(mask_ofl_ipi & mask))
			continue;
retry_ipi:
		if (rcu_dynticks_in_eqs_since(rdp->dynticks,
					      rdp->exp_dynticks_snap)) {
			mask_ofl_test |= mask;
			continue;
		}
		ret = smp_call_function_single(cpu, func, rsp, 0);
		if (!ret) {
			mask_ofl_ipi &= ~mask;
			continue;
		}
		/* Failed, raced with CPU hotplug operation. */
		raw_spin_lock_irqsave_rcu_node(rnp, flags);
		if ((rnp->qsmaskinitnext & mask) &&
		    (rnp->expmask & mask)) {
			/* Online, so delay for a bit and try again. */
			raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
			trace_rcu_exp_grace_period(rsp->name, rcu_exp_gp_seq_endval(rsp), TPS("selectofl"));
			schedule_timeout_uninterruptible(1);
			goto retry_ipi;
		}
		/* CPU really is offline, so we can ignore it. */
		if (!(rnp->expmask & mask))
			mask_ofl_ipi &= ~mask;
		raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
	}
	/* Report quiescent states for those that went offline. */
	mask_ofl_test |= mask_ofl_ipi;
	if (mask_ofl_test)
		rcu_report_exp_cpu_mult(rsp, rnp, mask_ofl_test, false);
}

/*
 * Select the nodes that the upcoming expedited grace period needs
 * to wait for.
 */
static void sync_rcu_exp_select_cpus(struct rcu_state *rsp,
				     smp_call_func_t func)
{
	struct rcu_node *rnp;

	trace_rcu_exp_grace_period(rsp->name, rcu_exp_gp_seq_endval(rsp), TPS("reset"));
	sync_exp_reset_tree(rsp);
	trace_rcu_exp_grace_period(rsp->name, rcu_exp_gp_seq_endval(rsp), TPS("select"));

	/* Schedule work for each leaf rcu_node structure. */
	rcu_for_each_leaf_node(rsp, rnp) {
		rnp->exp_need_flush = false;
		if (!READ_ONCE(rnp->expmask))
			continue; /* Avoid early boot non-existent wq. */
		rnp->rew.rew_func = func;
		rnp->rew.rew_rsp = rsp;
		if (!READ_ONCE(rcu_par_gp_wq) ||
		    rcu_scheduler_active != RCU_SCHEDULER_RUNNING) {
			/* No workqueues yet. */
			sync_rcu_exp_select_node_cpus(&rnp->rew.rew_work);
			continue;
		}
		INIT_WORK(&rnp->rew.rew_work, sync_rcu_exp_select_node_cpus);
		queue_work_on(rnp->grplo, rcu_par_gp_wq, &rnp->rew.rew_work);
		rnp->exp_need_flush = true;
	}

	/* Wait for workqueue jobs (if any) to complete. */
	rcu_for_each_leaf_node(rsp, rnp)
		if (rnp->exp_need_flush)
			flush_work(&rnp->rew.rew_work);
}

static void synchronize_sched_expedited_wait(struct rcu_state *rsp)
{
	int cpu;
	unsigned long jiffies_stall;
	unsigned long jiffies_start;
	unsigned long mask;
	int ndetected;
	struct rcu_node *rnp;
	struct rcu_node *rnp_root = rcu_get_root(rsp);
	int ret;

	trace_rcu_exp_grace_period(rsp->name, rcu_exp_gp_seq_endval(rsp), TPS("startwait"));
	jiffies_stall = rcu_jiffies_till_stall_check();
	jiffies_start = jiffies;

	for (;;) {
		ret = swait_event_timeout(
				rsp->expedited_wq,
				sync_rcu_preempt_exp_done(rnp_root),
				jiffies_stall);
		if (ret > 0 || sync_rcu_preempt_exp_done(rnp_root))
			return;
		WARN_ON(ret < 0);  /* workqueues should not be signaled. */
		if (rcu_cpu_stall_suppress)
			continue;
		panic_on_rcu_stall();
		pr_err("INFO: %s detected expedited stalls on CPUs/tasks: {",
		       rsp->name);
		ndetected = 0;
		rcu_for_each_leaf_node(rsp, rnp) {
			ndetected += rcu_print_task_exp_stall(rnp);
			for_each_leaf_node_possible_cpu(rnp, cpu) {
				struct rcu_data *rdp;

				mask = leaf_node_cpu_bit(rnp, cpu);
				if (!(rnp->expmask & mask))
					continue;
				ndetected++;
				rdp = per_cpu_ptr(rsp->rda, cpu);
				pr_cont(" %d-%c%c%c", cpu,
					"O."[!!cpu_online(cpu)],
					"o."[!!(rdp->grpmask & rnp->expmaskinit)],
					"N."[!!(rdp->grpmask & rnp->expmaskinitnext)]);
			}
		}
		pr_cont(" } %lu jiffies s: %lu root: %#lx/%c\n",
			jiffies - jiffies_start, rsp->expedited_sequence,
			rnp_root->expmask, ".T"[!!rnp_root->exp_tasks]);
		if (ndetected) {
			pr_err("blocking rcu_node structures:");
			rcu_for_each_node_breadth_first(rsp, rnp) {
				if (rnp == rnp_root)
					continue; /* printed unconditionally */
				if (sync_rcu_preempt_exp_done(rnp))
					continue;
				pr_cont(" l=%u:%d-%d:%#lx/%c",
					rnp->level, rnp->grplo, rnp->grphi,
					rnp->expmask,
					".T"[!!rnp->exp_tasks]);
			}
			pr_cont("\n");
		}
		rcu_for_each_leaf_node(rsp, rnp) {
			for_each_leaf_node_possible_cpu(rnp, cpu) {
				mask = leaf_node_cpu_bit(rnp, cpu);
				if (!(rnp->expmask & mask))
					continue;
				dump_cpu_task(cpu);
			}
		}
		jiffies_stall = 3 * rcu_jiffies_till_stall_check() + 3;
	}
}

/*
 * Wait for the current expedited grace period to complete, and then
 * wake up everyone who piggybacked on the just-completed expedited
 * grace period.  Also update all the ->exp_seq_rq counters as needed
 * in order to avoid counter-wrap problems.
 */
static void rcu_exp_wait_wake(struct rcu_state *rsp, unsigned long s)
{
	struct rcu_node *rnp;

	synchronize_sched_expedited_wait(rsp);
	rcu_exp_gp_seq_end(rsp);
	trace_rcu_exp_grace_period(rsp->name, s, TPS("end"));

	/*
	 * Switch over to wakeup mode, allowing the next GP, but -only- the
	 * next GP, to proceed.
	 */
	mutex_lock(&rsp->exp_wake_mutex);

	rcu_for_each_node_breadth_first(rsp, rnp) {
		if (ULONG_CMP_LT(READ_ONCE(rnp->exp_seq_rq), s)) {
			spin_lock(&rnp->exp_lock);
			/* Recheck, avoid hang in case someone just arrived. */
			if (ULONG_CMP_LT(rnp->exp_seq_rq, s))
				rnp->exp_seq_rq = s;
			spin_unlock(&rnp->exp_lock);
		}
		smp_mb(); /* All above changes before wakeup. */
		wake_up_all(&rnp->exp_wq[rcu_seq_ctr(rsp->expedited_sequence) & 0x3]);
	}
	trace_rcu_exp_grace_period(rsp->name, s, TPS("endwake"));
	mutex_unlock(&rsp->exp_wake_mutex);
}

/*
 * Common code to drive an expedited grace period forward, used by
 * workqueues and mid-boot-time tasks.
 */
static void rcu_exp_sel_wait_wake(struct rcu_state *rsp,
				  smp_call_func_t func, unsigned long s)
{
	/* Initialize the rcu_node tree in preparation for the wait. */
	sync_rcu_exp_select_cpus(rsp, func);

	/* Wait and clean up, including waking everyone. */
	rcu_exp_wait_wake(rsp, s);
}

/*
 * Work-queue handler to drive an expedited grace period forward.
 */
static void wait_rcu_exp_gp(struct work_struct *wp)
{
	struct rcu_exp_work *rewp;

	rewp = container_of(wp, struct rcu_exp_work, rew_work);
	rcu_exp_sel_wait_wake(rewp->rew_rsp, rewp->rew_func, rewp->rew_s);
}

/*
 * Given an rcu_state pointer and a smp_call_function() handler, kick
 * off the specified flavor of expedited grace period.
 */
static void _synchronize_rcu_expedited(struct rcu_state *rsp,
				       smp_call_func_t func)
{
	struct rcu_data *rdp;
	struct rcu_exp_work rew;
	struct rcu_node *rnp;
	unsigned long s;

	/* If expedited grace periods are prohibited, fall back to normal. */
	if (rcu_gp_is_normal()) {
		wait_rcu_gp(rsp->call);
		return;
	}

	/* Take a snapshot of the sequence number.  */
	s = rcu_exp_gp_seq_snap(rsp);
	if (exp_funnel_lock(rsp, s))
		return;  /* Someone else did our work for us. */

	/* Ensure that load happens before action based on it. */
	if (unlikely(rcu_scheduler_active == RCU_SCHEDULER_INIT)) {
		/* Direct call during scheduler init and early_initcalls(). */
		rcu_exp_sel_wait_wake(rsp, func, s);
	} else {
		/* Marshall arguments & schedule the expedited grace period. */
		rew.rew_func = func;
		rew.rew_rsp = rsp;
		rew.rew_s = s;
		INIT_WORK_ONSTACK(&rew.rew_work, wait_rcu_exp_gp);
		queue_work(rcu_gp_wq, &rew.rew_work);
	}

	/* Wait for expedited grace period to complete. */
	rdp = per_cpu_ptr(rsp->rda, raw_smp_processor_id());
	rnp = rcu_get_root(rsp);
	wait_event(rnp->exp_wq[rcu_seq_ctr(s) & 0x3],
		   sync_exp_work_done(rsp, &rdp->exp_workdone0, s));
	smp_mb(); /* Workqueue actions happen before return. */

	/* Let the next expedited grace period start. */
	mutex_unlock(&rsp->exp_mutex);
}

/**
 * synchronize_sched_expedited - Brute-force RCU-sched grace period
 *
 * Wait for an RCU-sched grace period to elapse, but use a "big hammer"
 * approach to force the grace period to end quickly.  This consumes
 * significant time on all CPUs and is unfriendly to real-time workloads,
 * so is thus not recommended for any sort of common-case code.  In fact,
 * if you are using synchronize_sched_expedited() in a loop, please
 * restructure your code to batch your updates, and then use a single
 * synchronize_sched() instead.
 *
 * This implementation can be thought of as an application of sequence
 * locking to expedited grace periods, but using the sequence counter to
 * determine when someone else has already done the work instead of for
 * retrying readers.
 */
void synchronize_sched_expedited(void)
{
	struct rcu_state *rsp = &rcu_sched_state;

	RCU_LOCKDEP_WARN(lock_is_held(&rcu_bh_lock_map) ||
			 lock_is_held(&rcu_lock_map) ||
			 lock_is_held(&rcu_sched_lock_map),
			 "Illegal synchronize_sched_expedited() in RCU read-side critical section");

	/* If only one CPU, this is automatically a grace period. */
	if (rcu_blocking_is_gp())
		return;

	_synchronize_rcu_expedited(rsp, sync_sched_exp_handler);
}
EXPORT_SYMBOL_GPL(synchronize_sched_expedited);

#ifdef CONFIG_PREEMPT_RCU

/*
 * Remote handler for smp_call_function_single().  If there is an
 * RCU read-side critical section in effect, request that the
 * next rcu_read_unlock() record the quiescent state up the
 * ->expmask fields in the rcu_node tree.  Otherwise, immediately
 * report the quiescent state.
 */
static void sync_rcu_exp_handler(void *info)
{
	struct rcu_data *rdp;
	struct rcu_state *rsp = info;
	struct task_struct *t = current;

	/*
	 * Within an RCU read-side critical section, request that the next
	 * rcu_read_unlock() report.  Unless this RCU read-side critical
	 * section has already blocked, in which case it is already set
	 * up for the expedited grace period to wait on it.
	 */
	if (t->rcu_read_lock_nesting > 0 &&
	    !t->rcu_read_unlock_special.b.blocked) {
		t->rcu_read_unlock_special.b.exp_need_qs = true;
		return;
	}

	/*
	 * We are either exiting an RCU read-side critical section (negative
	 * values of t->rcu_read_lock_nesting) or are not in one at all
	 * (zero value of t->rcu_read_lock_nesting).  Or we are in an RCU
	 * read-side critical section that blocked before this expedited
	 * grace period started.  Either way, we can immediately report
	 * the quiescent state.
	 */
	rdp = this_cpu_ptr(rsp->rda);
	rcu_report_exp_rdp(rsp, rdp, true);
}

/**
 * synchronize_rcu_expedited - Brute-force RCU grace period
 *
 * Wait for an RCU-preempt grace period, but expedite it.  The basic
 * idea is to IPI all non-idle non-nohz online CPUs.  The IPI handler
 * checks whether the CPU is in an RCU-preempt critical section, and
 * if so, it sets a flag that causes the outermost rcu_read_unlock()
 * to report the quiescent state.  On the other hand, if the CPU is
 * not in an RCU read-side critical section, the IPI handler reports
 * the quiescent state immediately.
 *
 * Although this is a greate improvement over previous expedited
 * implementations, it is still unfriendly to real-time workloads, so is
 * thus not recommended for any sort of common-case code.  In fact, if
 * you are using synchronize_rcu_expedited() in a loop, please restructure
 * your code to batch your updates, and then Use a single synchronize_rcu()
 * instead.
 */
void synchronize_rcu_expedited(void)
{
	struct rcu_state *rsp = rcu_state_p;

	RCU_LOCKDEP_WARN(lock_is_held(&rcu_bh_lock_map) ||
			 lock_is_held(&rcu_lock_map) ||
			 lock_is_held(&rcu_sched_lock_map),
			 "Illegal synchronize_rcu_expedited() in RCU read-side critical section");

	if (rcu_scheduler_active == RCU_SCHEDULER_INACTIVE)
		return;
	_synchronize_rcu_expedited(rsp, sync_rcu_exp_handler);
}
EXPORT_SYMBOL_GPL(synchronize_rcu_expedited);

#else /* #ifdef CONFIG_PREEMPT_RCU */

/*
 * Wait for an rcu-preempt grace period, but make it happen quickly.
 * But because preemptible RCU does not exist, map to rcu-sched.
 */
void synchronize_rcu_expedited(void)
{
	synchronize_sched_expedited();
}
EXPORT_SYMBOL_GPL(synchronize_rcu_expedited);

#endif /* #else #ifdef CONFIG_PREEMPT_RCU */
