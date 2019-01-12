// SPDX-License-Identifier: GPL-2.0+
/*
 * RCU CPU stall warnings for normal RCU grace periods
 *
 * Copyright IBM Corporation, 2019
 *
 * Author: Paul E. McKenney <paulmck@linux.ibm.com>
 */


/* panic() on RCU Stall sysctl. */
int sysctl_panic_on_rcu_stall __read_mostly;

#ifdef CONFIG_PROVE_RCU
#define RCU_STALL_DELAY_DELTA	       (5 * HZ)
#else
#define RCU_STALL_DELAY_DELTA	       0
#endif

int rcu_jiffies_till_stall_check(void)
{
	int till_stall_check = READ_ONCE(rcu_cpu_stall_timeout);

	/*
	 * Limit check must be consistent with the Kconfig limits
	 * for CONFIG_RCU_CPU_STALL_TIMEOUT.
	 */
	if (till_stall_check < 3) {
		WRITE_ONCE(rcu_cpu_stall_timeout, 3);
		till_stall_check = 3;
	} else if (till_stall_check > 300) {
		WRITE_ONCE(rcu_cpu_stall_timeout, 300);
		till_stall_check = 300;
	}
	return till_stall_check * HZ + RCU_STALL_DELAY_DELTA;
}
EXPORT_SYMBOL_GPL(rcu_jiffies_till_stall_check);

void rcu_sysrq_start(void)
{
	if (!rcu_cpu_stall_suppress)
		rcu_cpu_stall_suppress = 2;
}

void rcu_sysrq_end(void)
{
	if (rcu_cpu_stall_suppress == 2)
		rcu_cpu_stall_suppress = 0;
}

static int rcu_panic(struct notifier_block *this, unsigned long ev, void *ptr)
{
	rcu_cpu_stall_suppress = 1;
	return NOTIFY_DONE;
}

static struct notifier_block rcu_panic_block = {
	.notifier_call = rcu_panic,
};

static int __init check_cpu_stall_init(void)
{
	atomic_notifier_chain_register(&panic_notifier_list, &rcu_panic_block);
	return 0;
}
early_initcall(check_cpu_stall_init);

#ifdef CONFIG_PREEMPT

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
	list_for_each_entry_continue(t, &rnp->blkd_tasks, rcu_node_entry) {
		/*
		 * We could be printing a lot while holding a spinlock.
		 * Avoid triggering hard lockup.
		 */
		touch_nmi_watchdog();
		sched_show_task(t);
	}
	raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
}

/*
 * Dump detailed information for all tasks blocking the current RCU
 * grace period.
 */
static void rcu_print_detail_task_stall(void)
{
	struct rcu_node *rnp = rcu_get_root();

	rcu_print_detail_task_stall_rnp(rnp);
	rcu_for_each_leaf_node(rnp)
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

#else /* #ifdef CONFIG_PREEMPT */

/*
 * Because preemptible RCU does not exist, we never have to check for
 * tasks blocked within RCU read-side critical sections.
 */
static void rcu_print_detail_task_stall(void)
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
#endif /* #else #ifdef CONFIG_PREEMPT */

static void record_gp_stall_check_time(void)
{
	unsigned long j = jiffies;
	unsigned long j1;

	rcu_state.gp_start = j;
	j1 = rcu_jiffies_till_stall_check();
	/* Record ->gp_start before ->jiffies_stall. */
	smp_store_release(&rcu_state.jiffies_stall, j + j1); /* ^^^ */
	rcu_state.jiffies_resched = j + j1 / 2;
	rcu_state.n_force_qs_gpstart = READ_ONCE(rcu_state.n_force_qs);
}

/*
 * Complain about starvation of grace-period kthread.
 */
static void rcu_check_gp_kthread_starvation(void)
{
	struct task_struct *gpk = rcu_state.gp_kthread;
	unsigned long j;

	j = jiffies - READ_ONCE(rcu_state.gp_activity);
	if (j > 2 * HZ) {
		pr_err("%s kthread starved for %ld jiffies! g%ld f%#x %s(%d) ->state=%#lx ->cpu=%d\n",
		       rcu_state.name, j,
		       (long)rcu_seq_current(&rcu_state.gp_seq),
		       READ_ONCE(rcu_state.gp_flags),
		       gp_state_getname(rcu_state.gp_state), rcu_state.gp_state,
		       gpk ? gpk->state : ~0, gpk ? task_cpu(gpk) : -1);
		if (gpk) {
			pr_err("RCU grace-period kthread stack dump:\n");
			sched_show_task(gpk);
			wake_up_process(gpk);
		}
	}
}

/*
 * Dump stacks of all tasks running on stalled CPUs.  First try using
 * NMIs, but fall back to manual remote stack tracing on architectures
 * that don't support NMI-based stack dumps.  The NMI-triggered stack
 * traces are more accurate because they are printed by the target CPU.
 */
static void rcu_dump_cpu_stacks(void)
{
	int cpu;
	unsigned long flags;
	struct rcu_node *rnp;

	rcu_for_each_leaf_node(rnp) {
		raw_spin_lock_irqsave_rcu_node(rnp, flags);
		for_each_leaf_node_possible_cpu(rnp, cpu)
			if (rnp->qsmask & leaf_node_cpu_bit(rnp, cpu))
				if (!trigger_single_cpu_backtrace(cpu))
					dump_cpu_task(cpu);
		raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
	}
}

/*
 * If too much time has passed in the current grace period, and if
 * so configured, go kick the relevant kthreads.
 */
static void rcu_stall_kick_kthreads(void)
{
	unsigned long j;

	if (!rcu_kick_kthreads)
		return;
	j = READ_ONCE(rcu_state.jiffies_kick_kthreads);
	if (time_after(jiffies, j) && rcu_state.gp_kthread &&
	    (rcu_gp_in_progress() || READ_ONCE(rcu_state.gp_flags))) {
		WARN_ONCE(1, "Kicking %s grace-period kthread\n",
			  rcu_state.name);
		rcu_ftrace_dump(DUMP_ALL);
		wake_up_process(rcu_state.gp_kthread);
		WRITE_ONCE(rcu_state.jiffies_kick_kthreads, j + HZ);
	}
}

static void panic_on_rcu_stall(void)
{
	if (sysctl_panic_on_rcu_stall)
		panic("RCU Stall\n");
}

static void print_other_cpu_stall(unsigned long gp_seq)
{
	int cpu;
	unsigned long flags;
	unsigned long gpa;
	unsigned long j;
	int ndetected = 0;
	struct rcu_node *rnp = rcu_get_root();
	long totqlen = 0;

	/* Kick and suppress, if so configured. */
	rcu_stall_kick_kthreads();
	if (rcu_cpu_stall_suppress)
		return;

	/*
	 * OK, time to rat on our buddy...
	 * See Documentation/RCU/stallwarn.txt for info on how to debug
	 * RCU CPU stall warnings.
	 */
	pr_err("INFO: %s detected stalls on CPUs/tasks:", rcu_state.name);
	print_cpu_stall_info_begin();
	rcu_for_each_leaf_node(rnp) {
		raw_spin_lock_irqsave_rcu_node(rnp, flags);
		ndetected += rcu_print_task_stall(rnp);
		if (rnp->qsmask != 0) {
			for_each_leaf_node_possible_cpu(rnp, cpu)
				if (rnp->qsmask & leaf_node_cpu_bit(rnp, cpu)) {
					print_cpu_stall_info(cpu);
					ndetected++;
				}
		}
		raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
	}

	print_cpu_stall_info_end();
	for_each_possible_cpu(cpu)
		totqlen += rcu_get_n_cbs_cpu(cpu);
	pr_cont("(detected by %d, t=%ld jiffies, g=%ld, q=%lu)\n",
	       smp_processor_id(), (long)(jiffies - rcu_state.gp_start),
	       (long)rcu_seq_current(&rcu_state.gp_seq), totqlen);
	if (ndetected) {
		rcu_dump_cpu_stacks();

		/* Complain about tasks blocking the grace period. */
		rcu_print_detail_task_stall();
	} else {
		if (rcu_seq_current(&rcu_state.gp_seq) != gp_seq) {
			pr_err("INFO: Stall ended before state dump start\n");
		} else {
			j = jiffies;
			gpa = READ_ONCE(rcu_state.gp_activity);
			pr_err("All QSes seen, last %s kthread activity %ld (%ld-%ld), jiffies_till_next_fqs=%ld, root ->qsmask %#lx\n",
			       rcu_state.name, j - gpa, j, gpa,
			       READ_ONCE(jiffies_till_next_fqs),
			       rcu_get_root()->qsmask);
			/* In this case, the current CPU might be at fault. */
			sched_show_task(current);
		}
	}
	/* Rewrite if needed in case of slow consoles. */
	if (ULONG_CMP_GE(jiffies, READ_ONCE(rcu_state.jiffies_stall)))
		WRITE_ONCE(rcu_state.jiffies_stall,
			   jiffies + 3 * rcu_jiffies_till_stall_check() + 3);

	rcu_check_gp_kthread_starvation();

	panic_on_rcu_stall();

	rcu_force_quiescent_state();  /* Kick them all. */
}

static void print_cpu_stall(void)
{
	int cpu;
	unsigned long flags;
	struct rcu_data *rdp = this_cpu_ptr(&rcu_data);
	struct rcu_node *rnp = rcu_get_root();
	long totqlen = 0;

	/* Kick and suppress, if so configured. */
	rcu_stall_kick_kthreads();
	if (rcu_cpu_stall_suppress)
		return;

	/*
	 * OK, time to rat on ourselves...
	 * See Documentation/RCU/stallwarn.txt for info on how to debug
	 * RCU CPU stall warnings.
	 */
	pr_err("INFO: %s self-detected stall on CPU", rcu_state.name);
	print_cpu_stall_info_begin();
	raw_spin_lock_irqsave_rcu_node(rdp->mynode, flags);
	print_cpu_stall_info(smp_processor_id());
	raw_spin_unlock_irqrestore_rcu_node(rdp->mynode, flags);
	print_cpu_stall_info_end();
	for_each_possible_cpu(cpu)
		totqlen += rcu_get_n_cbs_cpu(cpu);
	pr_cont(" (t=%lu jiffies g=%ld q=%lu)\n",
		jiffies - rcu_state.gp_start,
		(long)rcu_seq_current(&rcu_state.gp_seq), totqlen);

	rcu_check_gp_kthread_starvation();

	rcu_dump_cpu_stacks();

	raw_spin_lock_irqsave_rcu_node(rnp, flags);
	/* Rewrite if needed in case of slow consoles. */
	if (ULONG_CMP_GE(jiffies, READ_ONCE(rcu_state.jiffies_stall)))
		WRITE_ONCE(rcu_state.jiffies_stall,
			   jiffies + 3 * rcu_jiffies_till_stall_check() + 3);
	raw_spin_unlock_irqrestore_rcu_node(rnp, flags);

	panic_on_rcu_stall();

	/*
	 * Attempt to revive the RCU machinery by forcing a context switch.
	 *
	 * A context switch would normally allow the RCU state machine to make
	 * progress and it could be we're stuck in kernel space without context
	 * switches for an entirely unreasonable amount of time.
	 */
	set_tsk_need_resched(current);
	set_preempt_need_resched();
}

static void check_cpu_stall(struct rcu_data *rdp)
{
	unsigned long gs1;
	unsigned long gs2;
	unsigned long gps;
	unsigned long j;
	unsigned long jn;
	unsigned long js;
	struct rcu_node *rnp;

	if ((rcu_cpu_stall_suppress && !rcu_kick_kthreads) ||
	    !rcu_gp_in_progress())
		return;
	rcu_stall_kick_kthreads();
	j = jiffies;

	/*
	 * Lots of memory barriers to reject false positives.
	 *
	 * The idea is to pick up rcu_state.gp_seq, then
	 * rcu_state.jiffies_stall, then rcu_state.gp_start, and finally
	 * another copy of rcu_state.gp_seq.  These values are updated in
	 * the opposite order with memory barriers (or equivalent) during
	 * grace-period initialization and cleanup.  Now, a false positive
	 * can occur if we get an new value of rcu_state.gp_start and a old
	 * value of rcu_state.jiffies_stall.  But given the memory barriers,
	 * the only way that this can happen is if one grace period ends
	 * and another starts between these two fetches.  This is detected
	 * by comparing the second fetch of rcu_state.gp_seq with the
	 * previous fetch from rcu_state.gp_seq.
	 *
	 * Given this check, comparisons of jiffies, rcu_state.jiffies_stall,
	 * and rcu_state.gp_start suffice to forestall false positives.
	 */
	gs1 = READ_ONCE(rcu_state.gp_seq);
	smp_rmb(); /* Pick up ->gp_seq first... */
	js = READ_ONCE(rcu_state.jiffies_stall);
	smp_rmb(); /* ...then ->jiffies_stall before the rest... */
	gps = READ_ONCE(rcu_state.gp_start);
	smp_rmb(); /* ...and finally ->gp_start before ->gp_seq again. */
	gs2 = READ_ONCE(rcu_state.gp_seq);
	if (gs1 != gs2 ||
	    ULONG_CMP_LT(j, js) ||
	    ULONG_CMP_GE(gps, js))
		return; /* No stall or GP completed since entering function. */
	rnp = rdp->mynode;
	jn = jiffies + 3 * rcu_jiffies_till_stall_check() + 3;
	if (rcu_gp_in_progress() &&
	    (READ_ONCE(rnp->qsmask) & rdp->grpmask) &&
	    cmpxchg(&rcu_state.jiffies_stall, js, jn) == js) {

		/* We haven't checked in, so go dump stack. */
		print_cpu_stall();

	} else if (rcu_gp_in_progress() &&
		   ULONG_CMP_GE(j, js + RCU_STALL_RAT_DELAY) &&
		   cmpxchg(&rcu_state.jiffies_stall, js, jn) == js) {

		/* They had a few time units to dump stack, so complain. */
		print_other_cpu_stall(gs2);
	}
}

/**
 * rcu_cpu_stall_reset - prevent further stall warnings in current grace period
 *
 * Set the stall-warning timeout way off into the future, thus preventing
 * any RCU CPU stall-warning messages from appearing in the current set of
 * RCU grace periods.
 *
 * The caller must disable hard irqs.
 */
void rcu_cpu_stall_reset(void)
{
	WRITE_ONCE(rcu_state.jiffies_stall, jiffies + ULONG_MAX / 2);
}
