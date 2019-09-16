// SPDX-License-Identifier: GPL-2.0+
/*
 * RCU CPU stall warnings for normal RCU grace periods
 *
 * Copyright IBM Corporation, 2019
 *
 * Author: Paul E. McKenney <paulmck@linux.ibm.com>
 */

//////////////////////////////////////////////////////////////////////////////
//
// Controlling CPU stall warnings, including delay calculation.

/* panic() on RCU Stall sysctl. */
int sysctl_panic_on_rcu_stall __read_mostly;

#ifdef CONFIG_PROVE_RCU
#define RCU_STALL_DELAY_DELTA	       (5 * HZ)
#else
#define RCU_STALL_DELAY_DELTA	       0
#endif

/* Limit-check stall timeouts specified at boottime and runtime. */
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

/* Don't do RCU CPU stall warnings during long sysrq printouts. */
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

/* Don't print RCU CPU stall warnings during a kernel panic. */
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

/* If so specified via sysctl, panic, yielding cleaner stall-warning output. */
static void panic_on_rcu_stall(void)
{
	if (sysctl_panic_on_rcu_stall)
		panic("RCU Stall\n");
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

//////////////////////////////////////////////////////////////////////////////
//
// Interaction with RCU grace periods

/* Start of new grace period, so record stall time (and forcing times). */
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

/* Zero ->ticks_this_gp and snapshot the number of RCU softirq handlers. */
static void zero_cpu_stall_ticks(struct rcu_data *rdp)
{
	rdp->ticks_this_gp = 0;
	rdp->softirq_snap = kstat_softirqs_cpu(RCU_SOFTIRQ, smp_processor_id());
	WRITE_ONCE(rdp->last_fqs_resched, jiffies);
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

/*
 * Handler for the irq_work request posted about halfway into the RCU CPU
 * stall timeout, and used to detect excessive irq disabling.  Set state
 * appropriately, but just complain if there is unexpected state on entry.
 */
static void rcu_iw_handler(struct irq_work *iwp)
{
	struct rcu_data *rdp;
	struct rcu_node *rnp;

	rdp = container_of(iwp, struct rcu_data, rcu_iw);
	rnp = rdp->mynode;
	raw_spin_lock_rcu_node(rnp);
	if (!WARN_ON_ONCE(!rdp->rcu_iw_pending)) {
		rdp->rcu_iw_gp_seq = rnp->gp_seq;
		rdp->rcu_iw_pending = false;
	}
	raw_spin_unlock_rcu_node(rnp);
}

//////////////////////////////////////////////////////////////////////////////
//
// Printing RCU CPU stall warnings

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
 * Scan the current list of tasks blocked within RCU read-side critical
 * sections, printing out the tid of each.
 */
static int rcu_print_task_stall(struct rcu_node *rnp)
{
	struct task_struct *t;
	int ndetected = 0;

	if (!rcu_preempt_blocked_readers_cgp(rnp))
		return 0;
	pr_err("\tTasks blocked on level-%d rcu_node (CPUs %d-%d):",
	       rnp->level, rnp->grplo, rnp->grphi);
	t = list_entry(rnp->gp_tasks->prev,
		       struct task_struct, rcu_node_entry);
	list_for_each_entry_continue(t, &rnp->blkd_tasks, rcu_node_entry) {
		pr_cont(" P%d", t->pid);
		ndetected++;
	}
	pr_cont("\n");
	return ndetected;
}

#else /* #ifdef CONFIG_PREEMPT */

/*
 * Because preemptible RCU does not exist, we never have to check for
 * tasks blocked within RCU read-side critical sections.
 */
static void rcu_print_detail_task_stall_rnp(struct rcu_node *rnp)
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

#ifdef CONFIG_RCU_FAST_NO_HZ

static void print_cpu_stall_fast_no_hz(char *cp, int cpu)
{
	struct rcu_data *rdp = &per_cpu(rcu_data, cpu);

	sprintf(cp, "last_accelerate: %04lx/%04lx, Nonlazy posted: %c%c%c",
		rdp->last_accelerate & 0xffff, jiffies & 0xffff,
		".l"[rdp->all_lazy],
		".L"[!rcu_segcblist_n_nonlazy_cbs(&rdp->cblist)],
		".D"[!!rdp->tick_nohz_enabled_snap]);
}

#else /* #ifdef CONFIG_RCU_FAST_NO_HZ */

static void print_cpu_stall_fast_no_hz(char *cp, int cpu)
{
	*cp = '\0';
}

#endif /* #else #ifdef CONFIG_RCU_FAST_NO_HZ */

/*
 * Print out diagnostic information for the specified stalled CPU.
 *
 * If the specified CPU is aware of the current RCU grace period, then
 * print the number of scheduling clock interrupts the CPU has taken
 * during the time that it has been aware.  Otherwise, print the number
 * of RCU grace periods that this CPU is ignorant of, for example, "1"
 * if the CPU was aware of the previous grace period.
 *
 * Also print out idle and (if CONFIG_RCU_FAST_NO_HZ) idle-entry info.
 */
static void print_cpu_stall_info(int cpu)
{
	unsigned long delta;
	char fast_no_hz[72];
	struct rcu_data *rdp = per_cpu_ptr(&rcu_data, cpu);
	char *ticks_title;
	unsigned long ticks_value;

	/*
	 * We could be printing a lot while holding a spinlock.  Avoid
	 * triggering hard lockup.
	 */
	touch_nmi_watchdog();

	ticks_value = rcu_seq_ctr(rcu_state.gp_seq - rdp->gp_seq);
	if (ticks_value) {
		ticks_title = "GPs behind";
	} else {
		ticks_title = "ticks this GP";
		ticks_value = rdp->ticks_this_gp;
	}
	print_cpu_stall_fast_no_hz(fast_no_hz, cpu);
	delta = rcu_seq_ctr(rdp->mynode->gp_seq - rdp->rcu_iw_gp_seq);
	pr_err("\t%d-%c%c%c%c: (%lu %s) idle=%03x/%ld/%#lx softirq=%u/%u fqs=%ld %s\n",
	       cpu,
	       "O."[!!cpu_online(cpu)],
	       "o."[!!(rdp->grpmask & rdp->mynode->qsmaskinit)],
	       "N."[!!(rdp->grpmask & rdp->mynode->qsmaskinitnext)],
	       !IS_ENABLED(CONFIG_IRQ_WORK) ? '?' :
			rdp->rcu_iw_pending ? (int)min(delta, 9UL) + '0' :
				"!."[!delta],
	       ticks_value, ticks_title,
	       rcu_dynticks_snap(rdp) & 0xfff,
	       rdp->dynticks_nesting, rdp->dynticks_nmi_nesting,
	       rdp->softirq_snap, kstat_softirqs_cpu(RCU_SOFTIRQ, cpu),
	       READ_ONCE(rcu_state.n_force_qs) - rcu_state.n_force_qs_gpstart,
	       fast_no_hz);
}

/* Complain about starvation of grace-period kthread.  */
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

static void print_other_cpu_stall(unsigned long gp_seq)
{
	int cpu;
	unsigned long flags;
	unsigned long gpa;
	unsigned long j;
	int ndetected = 0;
	struct rcu_node *rnp;
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
	pr_err("INFO: %s detected stalls on CPUs/tasks:\n", rcu_state.name);
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

	for_each_possible_cpu(cpu)
		totqlen += rcu_get_n_cbs_cpu(cpu);
	pr_cont("\t(detected by %d, t=%ld jiffies, g=%ld, q=%lu)\n",
	       smp_processor_id(), (long)(jiffies - rcu_state.gp_start),
	       (long)rcu_seq_current(&rcu_state.gp_seq), totqlen);
	if (ndetected) {
		rcu_dump_cpu_stacks();

		/* Complain about tasks blocking the grace period. */
		rcu_for_each_leaf_node(rnp)
			rcu_print_detail_task_stall_rnp(rnp);
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
	pr_err("INFO: %s self-detected stall on CPU\n", rcu_state.name);
	raw_spin_lock_irqsave_rcu_node(rdp->mynode, flags);
	print_cpu_stall_info(smp_processor_id());
	raw_spin_unlock_irqrestore_rcu_node(rdp->mynode, flags);
	for_each_possible_cpu(cpu)
		totqlen += rcu_get_n_cbs_cpu(cpu);
	pr_cont("\t(t=%lu jiffies g=%ld q=%lu)\n",
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
		if (rcu_cpu_stall_ftrace_dump)
			rcu_ftrace_dump(DUMP_ALL);

	} else if (rcu_gp_in_progress() &&
		   ULONG_CMP_GE(j, js + RCU_STALL_RAT_DELAY) &&
		   cmpxchg(&rcu_state.jiffies_stall, js, jn) == js) {

		/* They had a few time units to dump stack, so complain. */
		print_other_cpu_stall(gs2);
		if (rcu_cpu_stall_ftrace_dump)
			rcu_ftrace_dump(DUMP_ALL);
	}
}

//////////////////////////////////////////////////////////////////////////////
//
// RCU forward-progress mechanisms, including of callback invocation.


/*
 * Show the state of the grace-period kthreads.
 */
void show_rcu_gp_kthreads(void)
{
	int cpu;
	unsigned long j;
	unsigned long ja;
	unsigned long jr;
	unsigned long jw;
	struct rcu_data *rdp;
	struct rcu_node *rnp;

	j = jiffies;
	ja = j - READ_ONCE(rcu_state.gp_activity);
	jr = j - READ_ONCE(rcu_state.gp_req_activity);
	jw = j - READ_ONCE(rcu_state.gp_wake_time);
	pr_info("%s: wait state: %s(%d) ->state: %#lx delta ->gp_activity %lu ->gp_req_activity %lu ->gp_wake_time %lu ->gp_wake_seq %ld ->gp_seq %ld ->gp_seq_needed %ld ->gp_flags %#x\n",
		rcu_state.name, gp_state_getname(rcu_state.gp_state),
		rcu_state.gp_state,
		rcu_state.gp_kthread ? rcu_state.gp_kthread->state : 0x1ffffL,
		ja, jr, jw, (long)READ_ONCE(rcu_state.gp_wake_seq),
		(long)READ_ONCE(rcu_state.gp_seq),
		(long)READ_ONCE(rcu_get_root()->gp_seq_needed),
		READ_ONCE(rcu_state.gp_flags));
	rcu_for_each_node_breadth_first(rnp) {
		if (ULONG_CMP_GE(rcu_state.gp_seq, rnp->gp_seq_needed))
			continue;
		pr_info("\trcu_node %d:%d ->gp_seq %ld ->gp_seq_needed %ld\n",
			rnp->grplo, rnp->grphi, (long)rnp->gp_seq,
			(long)rnp->gp_seq_needed);
		if (!rcu_is_leaf_node(rnp))
			continue;
		for_each_leaf_node_possible_cpu(rnp, cpu) {
			rdp = per_cpu_ptr(&rcu_data, cpu);
			if (rdp->gpwrap ||
			    ULONG_CMP_GE(rcu_state.gp_seq,
					 rdp->gp_seq_needed))
				continue;
			pr_info("\tcpu %d ->gp_seq_needed %ld\n",
				cpu, (long)rdp->gp_seq_needed);
		}
	}
	for_each_possible_cpu(cpu) {
		rdp = per_cpu_ptr(&rcu_data, cpu);
		if (rcu_segcblist_is_offloaded(&rdp->cblist))
			show_rcu_nocb_state(rdp);
	}
	/* sched_show_task(rcu_state.gp_kthread); */
}
EXPORT_SYMBOL_GPL(show_rcu_gp_kthreads);

/*
 * This function checks for grace-period requests that fail to motivate
 * RCU to come out of its idle mode.
 */
static void rcu_check_gp_start_stall(struct rcu_node *rnp, struct rcu_data *rdp,
				     const unsigned long gpssdelay)
{
	unsigned long flags;
	unsigned long j;
	struct rcu_node *rnp_root = rcu_get_root();
	static atomic_t warned = ATOMIC_INIT(0);

	if (!IS_ENABLED(CONFIG_PROVE_RCU) || rcu_gp_in_progress() ||
	    ULONG_CMP_GE(rnp_root->gp_seq, rnp_root->gp_seq_needed))
		return;
	j = jiffies; /* Expensive access, and in common case don't get here. */
	if (time_before(j, READ_ONCE(rcu_state.gp_req_activity) + gpssdelay) ||
	    time_before(j, READ_ONCE(rcu_state.gp_activity) + gpssdelay) ||
	    atomic_read(&warned))
		return;

	raw_spin_lock_irqsave_rcu_node(rnp, flags);
	j = jiffies;
	if (rcu_gp_in_progress() ||
	    ULONG_CMP_GE(rnp_root->gp_seq, rnp_root->gp_seq_needed) ||
	    time_before(j, READ_ONCE(rcu_state.gp_req_activity) + gpssdelay) ||
	    time_before(j, READ_ONCE(rcu_state.gp_activity) + gpssdelay) ||
	    atomic_read(&warned)) {
		raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
		return;
	}
	/* Hold onto the leaf lock to make others see warned==1. */

	if (rnp_root != rnp)
		raw_spin_lock_rcu_node(rnp_root); /* irqs already disabled. */
	j = jiffies;
	if (rcu_gp_in_progress() ||
	    ULONG_CMP_GE(rnp_root->gp_seq, rnp_root->gp_seq_needed) ||
	    time_before(j, rcu_state.gp_req_activity + gpssdelay) ||
	    time_before(j, rcu_state.gp_activity + gpssdelay) ||
	    atomic_xchg(&warned, 1)) {
		if (rnp_root != rnp)
			/* irqs remain disabled. */
			raw_spin_unlock_rcu_node(rnp_root);
		raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
		return;
	}
	WARN_ON(1);
	if (rnp_root != rnp)
		raw_spin_unlock_rcu_node(rnp_root);
	raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
	show_rcu_gp_kthreads();
}

/*
 * Do a forward-progress check for rcutorture.  This is normally invoked
 * due to an OOM event.  The argument "j" gives the time period during
 * which rcutorture would like progress to have been made.
 */
void rcu_fwd_progress_check(unsigned long j)
{
	unsigned long cbs;
	int cpu;
	unsigned long max_cbs = 0;
	int max_cpu = -1;
	struct rcu_data *rdp;

	if (rcu_gp_in_progress()) {
		pr_info("%s: GP age %lu jiffies\n",
			__func__, jiffies - rcu_state.gp_start);
		show_rcu_gp_kthreads();
	} else {
		pr_info("%s: Last GP end %lu jiffies ago\n",
			__func__, jiffies - rcu_state.gp_end);
		preempt_disable();
		rdp = this_cpu_ptr(&rcu_data);
		rcu_check_gp_start_stall(rdp->mynode, rdp, j);
		preempt_enable();
	}
	for_each_possible_cpu(cpu) {
		cbs = rcu_get_n_cbs_cpu(cpu);
		if (!cbs)
			continue;
		if (max_cpu < 0)
			pr_info("%s: callbacks", __func__);
		pr_cont(" %d: %lu", cpu, cbs);
		if (cbs <= max_cbs)
			continue;
		max_cbs = cbs;
		max_cpu = cpu;
	}
	if (max_cpu >= 0)
		pr_cont("\n");
}
EXPORT_SYMBOL_GPL(rcu_fwd_progress_check);

/* Commandeer a sysrq key to dump RCU's tree. */
static bool sysrq_rcu;
module_param(sysrq_rcu, bool, 0444);

/* Dump grace-period-request information due to commandeered sysrq. */
static void sysrq_show_rcu(int key)
{
	show_rcu_gp_kthreads();
}

static struct sysrq_key_op sysrq_rcudump_op = {
	.handler = sysrq_show_rcu,
	.help_msg = "show-rcu(y)",
	.action_msg = "Show RCU tree",
	.enable_mask = SYSRQ_ENABLE_DUMP,
};

static int __init rcu_sysrq_init(void)
{
	if (sysrq_rcu)
		return register_sysrq_key('y', &sysrq_rcudump_op);
	return 0;
}
early_initcall(rcu_sysrq_init);
