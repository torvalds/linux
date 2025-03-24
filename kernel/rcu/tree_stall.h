// SPDX-License-Identifier: GPL-2.0+
/*
 * RCU CPU stall warnings for normal RCU grace periods
 *
 * Copyright IBM Corporation, 2019
 *
 * Author: Paul E. McKenney <paulmck@linux.ibm.com>
 */

#include <linux/console.h>
#include <linux/kvm_para.h>
#include <linux/rcu_notifier.h>
#include <linux/smp.h>

//////////////////////////////////////////////////////////////////////////////
//
// Controlling CPU stall warnings, including delay calculation.

/* panic() on RCU Stall sysctl. */
int sysctl_panic_on_rcu_stall __read_mostly;
int sysctl_max_rcu_stall_to_panic __read_mostly;

#ifdef CONFIG_PROVE_RCU
#define RCU_STALL_DELAY_DELTA		(5 * HZ)
#else
#define RCU_STALL_DELAY_DELTA		0
#endif
#define RCU_STALL_MIGHT_DIV		8
#define RCU_STALL_MIGHT_MIN		(2 * HZ)

int rcu_exp_jiffies_till_stall_check(void)
{
	int cpu_stall_timeout = READ_ONCE(rcu_exp_cpu_stall_timeout);
	int exp_stall_delay_delta = 0;
	int till_stall_check;

	// Zero says to use rcu_cpu_stall_timeout, but in milliseconds.
	if (!cpu_stall_timeout)
		cpu_stall_timeout = jiffies_to_msecs(rcu_jiffies_till_stall_check());

	// Limit check must be consistent with the Kconfig limits for
	// CONFIG_RCU_EXP_CPU_STALL_TIMEOUT, so check the allowed range.
	// The minimum clamped value is "2UL", because at least one full
	// tick has to be guaranteed.
	till_stall_check = clamp(msecs_to_jiffies(cpu_stall_timeout), 2UL, 300UL * HZ);

	if (cpu_stall_timeout && jiffies_to_msecs(till_stall_check) != cpu_stall_timeout)
		WRITE_ONCE(rcu_exp_cpu_stall_timeout, jiffies_to_msecs(till_stall_check));

#ifdef CONFIG_PROVE_RCU
	/* Add extra ~25% out of till_stall_check. */
	exp_stall_delay_delta = ((till_stall_check * 25) / 100) + 1;
#endif

	return till_stall_check + exp_stall_delay_delta;
}
EXPORT_SYMBOL_GPL(rcu_exp_jiffies_till_stall_check);

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
	static int cpu_stall;

	if (++cpu_stall < sysctl_max_rcu_stall_to_panic)
		return;

	if (sysctl_panic_on_rcu_stall)
		panic("RCU Stall\n");
}

/**
 * rcu_cpu_stall_reset - restart stall-warning timeout for current grace period
 *
 * To perform the reset request from the caller, disable stall detection until
 * 3 fqs loops have passed. This is required to ensure a fresh jiffies is
 * loaded.  It should be safe to do from the fqs loop as enough timer
 * interrupts and context switches should have passed.
 *
 * The caller must disable hard irqs.
 */
void rcu_cpu_stall_reset(void)
{
	WRITE_ONCE(rcu_state.nr_fqs_jiffies_stall, 3);
	WRITE_ONCE(rcu_state.jiffies_stall, ULONG_MAX);
}

//////////////////////////////////////////////////////////////////////////////
//
// Interaction with RCU grace periods

/* Start of new grace period, so record stall time (and forcing times). */
static void record_gp_stall_check_time(void)
{
	unsigned long j = jiffies;
	unsigned long j1;

	WRITE_ONCE(rcu_state.gp_start, j);
	j1 = rcu_jiffies_till_stall_check();
	smp_mb(); // ->gp_start before ->jiffies_stall and caller's ->gp_seq.
	WRITE_ONCE(rcu_state.nr_fqs_jiffies_stall, 0);
	WRITE_ONCE(rcu_state.jiffies_stall, j + j1);
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

	if (!READ_ONCE(rcu_kick_kthreads))
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

#ifdef CONFIG_PREEMPT_RCU

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

// Communicate task state back to the RCU CPU stall warning request.
struct rcu_stall_chk_rdr {
	int nesting;
	union rcu_special rs;
	bool on_blkd_list;
};

/*
 * Report out the state of a not-running task that is stalling the
 * current RCU grace period.
 */
static int check_slow_task(struct task_struct *t, void *arg)
{
	struct rcu_stall_chk_rdr *rscrp = arg;

	if (task_curr(t))
		return -EBUSY; // It is running, so decline to inspect it.
	rscrp->nesting = t->rcu_read_lock_nesting;
	rscrp->rs = t->rcu_read_unlock_special;
	rscrp->on_blkd_list = !list_empty(&t->rcu_node_entry);
	return 0;
}

/*
 * Scan the current list of tasks blocked within RCU read-side critical
 * sections, printing out the tid of each of the first few of them.
 */
static int rcu_print_task_stall(struct rcu_node *rnp, unsigned long flags)
	__releases(rnp->lock)
{
	int i = 0;
	int ndetected = 0;
	struct rcu_stall_chk_rdr rscr;
	struct task_struct *t;
	struct task_struct *ts[8];

	lockdep_assert_irqs_disabled();
	if (!rcu_preempt_blocked_readers_cgp(rnp)) {
		raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
		return 0;
	}
	pr_err("\tTasks blocked on level-%d rcu_node (CPUs %d-%d):",
	       rnp->level, rnp->grplo, rnp->grphi);
	t = list_entry(rnp->gp_tasks->prev,
		       struct task_struct, rcu_node_entry);
	list_for_each_entry_continue(t, &rnp->blkd_tasks, rcu_node_entry) {
		get_task_struct(t);
		ts[i++] = t;
		if (i >= ARRAY_SIZE(ts))
			break;
	}
	raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
	while (i) {
		t = ts[--i];
		if (task_call_func(t, check_slow_task, &rscr))
			pr_cont(" P%d", t->pid);
		else
			pr_cont(" P%d/%d:%c%c%c%c",
				t->pid, rscr.nesting,
				".b"[rscr.rs.b.blocked],
				".q"[rscr.rs.b.need_qs],
				".e"[rscr.rs.b.exp_hint],
				".l"[rscr.on_blkd_list]);
		lockdep_assert_irqs_disabled();
		put_task_struct(t);
		ndetected++;
	}
	pr_cont("\n");
	return ndetected;
}

#else /* #ifdef CONFIG_PREEMPT_RCU */

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
static int rcu_print_task_stall(struct rcu_node *rnp, unsigned long flags)
	__releases(rnp->lock)
{
	raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
	return 0;
}
#endif /* #else #ifdef CONFIG_PREEMPT_RCU */

/*
 * Dump stacks of all tasks running on stalled CPUs.  First try using
 * NMIs, but fall back to manual remote stack tracing on architectures
 * that don't support NMI-based stack dumps.  The NMI-triggered stack
 * traces are more accurate because they are printed by the target CPU.
 */
static void rcu_dump_cpu_stacks(unsigned long gp_seq)
{
	int cpu;
	unsigned long flags;
	struct rcu_node *rnp;

	rcu_for_each_leaf_node(rnp) {
		printk_deferred_enter();
		for_each_leaf_node_possible_cpu(rnp, cpu) {
			if (gp_seq != data_race(rcu_state.gp_seq)) {
				printk_deferred_exit();
				pr_err("INFO: Stall ended during stack backtracing.\n");
				return;
			}
			if (!(data_race(rnp->qsmask) & leaf_node_cpu_bit(rnp, cpu)))
				continue;
			raw_spin_lock_irqsave_rcu_node(rnp, flags);
			if (rnp->qsmask & leaf_node_cpu_bit(rnp, cpu)) {
				if (cpu_is_offline(cpu))
					pr_err("Offline CPU %d blocking current GP.\n", cpu);
				else
					dump_cpu_task(cpu);
			}
			raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
		}
		printk_deferred_exit();
	}
}

static const char * const gp_state_names[] = {
	[RCU_GP_IDLE] = "RCU_GP_IDLE",
	[RCU_GP_WAIT_GPS] = "RCU_GP_WAIT_GPS",
	[RCU_GP_DONE_GPS] = "RCU_GP_DONE_GPS",
	[RCU_GP_ONOFF] = "RCU_GP_ONOFF",
	[RCU_GP_INIT] = "RCU_GP_INIT",
	[RCU_GP_WAIT_FQS] = "RCU_GP_WAIT_FQS",
	[RCU_GP_DOING_FQS] = "RCU_GP_DOING_FQS",
	[RCU_GP_CLEANUP] = "RCU_GP_CLEANUP",
	[RCU_GP_CLEANED] = "RCU_GP_CLEANED",
};

/*
 * Convert a ->gp_state value to a character string.
 */
static const char *gp_state_getname(short gs)
{
	if (gs < 0 || gs >= ARRAY_SIZE(gp_state_names))
		return "???";
	return gp_state_names[gs];
}

/* Is the RCU grace-period kthread being starved of CPU time? */
static bool rcu_is_gp_kthread_starving(unsigned long *jp)
{
	unsigned long j = jiffies - READ_ONCE(rcu_state.gp_activity);

	if (jp)
		*jp = j;
	return j > 2 * HZ;
}

static bool rcu_is_rcuc_kthread_starving(struct rcu_data *rdp, unsigned long *jp)
{
	int cpu;
	struct task_struct *rcuc;
	unsigned long j;

	rcuc = rdp->rcu_cpu_kthread_task;
	if (!rcuc)
		return false;

	cpu = task_cpu(rcuc);
	if (cpu_is_offline(cpu) || idle_cpu(cpu))
		return false;

	j = jiffies - READ_ONCE(rdp->rcuc_activity);

	if (jp)
		*jp = j;
	return j > 2 * HZ;
}

static void print_cpu_stat_info(int cpu)
{
	struct rcu_snap_record rsr, *rsrp;
	struct rcu_data *rdp = per_cpu_ptr(&rcu_data, cpu);
	struct kernel_cpustat *kcsp = &kcpustat_cpu(cpu);

	if (!rcu_cpu_stall_cputime)
		return;

	rsrp = &rdp->snap_record;
	if (rsrp->gp_seq != rdp->gp_seq)
		return;

	rsr.cputime_irq     = kcpustat_field(kcsp, CPUTIME_IRQ, cpu);
	rsr.cputime_softirq = kcpustat_field(kcsp, CPUTIME_SOFTIRQ, cpu);
	rsr.cputime_system  = kcpustat_field(kcsp, CPUTIME_SYSTEM, cpu);

	pr_err("\t         hardirqs   softirqs   csw/system\n");
	pr_err("\t number: %8ld %10d %12lld\n",
		kstat_cpu_irqs_sum(cpu) - rsrp->nr_hardirqs,
		kstat_cpu_softirqs_sum(cpu) - rsrp->nr_softirqs,
		nr_context_switches_cpu(cpu) - rsrp->nr_csw);
	pr_err("\tcputime: %8lld %10lld %12lld   ==> %d(ms)\n",
		div_u64(rsr.cputime_irq - rsrp->cputime_irq, NSEC_PER_MSEC),
		div_u64(rsr.cputime_softirq - rsrp->cputime_softirq, NSEC_PER_MSEC),
		div_u64(rsr.cputime_system - rsrp->cputime_system, NSEC_PER_MSEC),
		jiffies_to_msecs(jiffies - rsrp->jiffies));
}

/*
 * Print out diagnostic information for the specified stalled CPU.
 *
 * If the specified CPU is aware of the current RCU grace period, then
 * print the number of scheduling clock interrupts the CPU has taken
 * during the time that it has been aware.  Otherwise, print the number
 * of RCU grace periods that this CPU is ignorant of, for example, "1"
 * if the CPU was aware of the previous grace period.
 *
 * Also print out idle info.
 */
static void print_cpu_stall_info(int cpu)
{
	unsigned long delta;
	bool falsepositive;
	struct rcu_data *rdp = per_cpu_ptr(&rcu_data, cpu);
	char *ticks_title;
	unsigned long ticks_value;
	bool rcuc_starved;
	unsigned long j;
	char buf[32];

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
	delta = rcu_seq_ctr(rdp->mynode->gp_seq - rdp->rcu_iw_gp_seq);
	falsepositive = rcu_is_gp_kthread_starving(NULL) &&
			rcu_watching_snap_in_eqs(ct_rcu_watching_cpu(cpu));
	rcuc_starved = rcu_is_rcuc_kthread_starving(rdp, &j);
	if (rcuc_starved)
		// Print signed value, as negative values indicate a probable bug.
		snprintf(buf, sizeof(buf), " rcuc=%ld jiffies(starved)", j);
	pr_err("\t%d-%c%c%c%c: (%lu %s) idle=%04x/%ld/%#lx softirq=%u/%u fqs=%ld%s%s\n",
	       cpu,
	       "O."[!!cpu_online(cpu)],
	       "o."[!!(rdp->grpmask & rdp->mynode->qsmaskinit)],
	       "N."[!!(rdp->grpmask & rdp->mynode->qsmaskinitnext)],
	       !IS_ENABLED(CONFIG_IRQ_WORK) ? '?' :
			rdp->rcu_iw_pending ? (int)min(delta, 9UL) + '0' :
				"!."[!delta],
	       ticks_value, ticks_title,
	       ct_rcu_watching_cpu(cpu) & 0xffff,
	       ct_nesting_cpu(cpu), ct_nmi_nesting_cpu(cpu),
	       rdp->softirq_snap, kstat_softirqs_cpu(RCU_SOFTIRQ, cpu),
	       data_race(rcu_state.n_force_qs) - rcu_state.n_force_qs_gpstart,
	       rcuc_starved ? buf : "",
	       falsepositive ? " (false positive?)" : "");

	print_cpu_stat_info(cpu);
}

/* Complain about starvation of grace-period kthread.  */
static void rcu_check_gp_kthread_starvation(void)
{
	int cpu;
	struct task_struct *gpk = rcu_state.gp_kthread;
	unsigned long j;

	if (rcu_is_gp_kthread_starving(&j)) {
		cpu = gpk ? task_cpu(gpk) : -1;
		pr_err("%s kthread starved for %ld jiffies! g%ld f%#x %s(%d) ->state=%#x ->cpu=%d\n",
		       rcu_state.name, j,
		       (long)rcu_seq_current(&rcu_state.gp_seq),
		       data_race(READ_ONCE(rcu_state.gp_flags)),
		       gp_state_getname(rcu_state.gp_state),
		       data_race(READ_ONCE(rcu_state.gp_state)),
		       gpk ? data_race(READ_ONCE(gpk->__state)) : ~0, cpu);
		if (gpk) {
			struct rcu_data *rdp = per_cpu_ptr(&rcu_data, cpu);

			pr_err("\tUnless %s kthread gets sufficient CPU time, OOM is now expected behavior.\n", rcu_state.name);
			pr_err("RCU grace-period kthread stack dump:\n");
			sched_show_task(gpk);
			if (cpu_is_offline(cpu)) {
				pr_err("RCU GP kthread last ran on offline CPU %d.\n", cpu);
			} else if (!(data_race(READ_ONCE(rdp->mynode->qsmask)) & rdp->grpmask)) {
				pr_err("Stack dump where RCU GP kthread last ran:\n");
				dump_cpu_task(cpu);
			}
			wake_up_process(gpk);
		}
	}
}

/* Complain about missing wakeups from expired fqs wait timer */
static void rcu_check_gp_kthread_expired_fqs_timer(void)
{
	struct task_struct *gpk = rcu_state.gp_kthread;
	short gp_state;
	unsigned long jiffies_fqs;
	int cpu;

	/*
	 * Order reads of .gp_state and .jiffies_force_qs.
	 * Matching smp_wmb() is present in rcu_gp_fqs_loop().
	 */
	gp_state = smp_load_acquire(&rcu_state.gp_state);
	jiffies_fqs = READ_ONCE(rcu_state.jiffies_force_qs);

	if (gp_state == RCU_GP_WAIT_FQS &&
	    time_after(jiffies, jiffies_fqs + RCU_STALL_MIGHT_MIN) &&
	    gpk && !READ_ONCE(gpk->on_rq)) {
		cpu = task_cpu(gpk);
		pr_err("%s kthread timer wakeup didn't happen for %ld jiffies! g%ld f%#x %s(%d) ->state=%#x\n",
		       rcu_state.name, (jiffies - jiffies_fqs),
		       (long)rcu_seq_current(&rcu_state.gp_seq),
		       data_race(READ_ONCE(rcu_state.gp_flags)), // Diagnostic read
		       gp_state_getname(RCU_GP_WAIT_FQS), RCU_GP_WAIT_FQS,
		       data_race(READ_ONCE(gpk->__state)));
		pr_err("\tPossible timer handling issue on cpu=%d timer-softirq=%u\n",
		       cpu, kstat_softirqs_cpu(TIMER_SOFTIRQ, cpu));
	}
}

static void print_other_cpu_stall(unsigned long gp_seq, unsigned long gps)
{
	int cpu;
	unsigned long flags;
	unsigned long gpa;
	unsigned long j;
	int ndetected = 0;
	struct rcu_node *rnp;
	long totqlen = 0;

	lockdep_assert_irqs_disabled();

	/* Kick and suppress, if so configured. */
	rcu_stall_kick_kthreads();
	if (rcu_stall_is_suppressed())
		return;

	nbcon_cpu_emergency_enter();

	/*
	 * OK, time to rat on our buddy...
	 * See Documentation/RCU/stallwarn.rst for info on how to debug
	 * RCU CPU stall warnings.
	 */
	trace_rcu_stall_warning(rcu_state.name, TPS("StallDetected"));
	pr_err("INFO: %s detected stalls on CPUs/tasks:\n", rcu_state.name);
	rcu_for_each_leaf_node(rnp) {
		raw_spin_lock_irqsave_rcu_node(rnp, flags);
		if (rnp->qsmask != 0) {
			for_each_leaf_node_possible_cpu(rnp, cpu)
				if (rnp->qsmask & leaf_node_cpu_bit(rnp, cpu)) {
					print_cpu_stall_info(cpu);
					ndetected++;
				}
		}
		ndetected += rcu_print_task_stall(rnp, flags); // Releases rnp->lock.
		lockdep_assert_irqs_disabled();
	}

	for_each_possible_cpu(cpu)
		totqlen += rcu_get_n_cbs_cpu(cpu);
	pr_err("\t(detected by %d, t=%ld jiffies, g=%ld, q=%lu ncpus=%d)\n",
	       smp_processor_id(), (long)(jiffies - gps),
	       (long)rcu_seq_current(&rcu_state.gp_seq), totqlen,
	       data_race(rcu_state.n_online_cpus)); // Diagnostic read
	if (ndetected) {
		rcu_dump_cpu_stacks(gp_seq);

		/* Complain about tasks blocking the grace period. */
		rcu_for_each_leaf_node(rnp)
			rcu_print_detail_task_stall_rnp(rnp);
	} else {
		if (rcu_seq_current(&rcu_state.gp_seq) != gp_seq) {
			pr_err("INFO: Stall ended before state dump start\n");
		} else {
			j = jiffies;
			gpa = data_race(READ_ONCE(rcu_state.gp_activity));
			pr_err("All QSes seen, last %s kthread activity %ld (%ld-%ld), jiffies_till_next_fqs=%ld, root ->qsmask %#lx\n",
			       rcu_state.name, j - gpa, j, gpa,
			       data_race(READ_ONCE(jiffies_till_next_fqs)),
			       data_race(READ_ONCE(rcu_get_root()->qsmask)));
		}
	}
	/* Rewrite if needed in case of slow consoles. */
	if (ULONG_CMP_GE(jiffies, READ_ONCE(rcu_state.jiffies_stall)))
		WRITE_ONCE(rcu_state.jiffies_stall,
			   jiffies + 3 * rcu_jiffies_till_stall_check() + 3);

	rcu_check_gp_kthread_expired_fqs_timer();
	rcu_check_gp_kthread_starvation();

	nbcon_cpu_emergency_exit();

	panic_on_rcu_stall();

	rcu_force_quiescent_state();  /* Kick them all. */
}

static void print_cpu_stall(unsigned long gp_seq, unsigned long gps)
{
	int cpu;
	unsigned long flags;
	struct rcu_data *rdp = this_cpu_ptr(&rcu_data);
	struct rcu_node *rnp = rcu_get_root();
	long totqlen = 0;

	lockdep_assert_irqs_disabled();

	/* Kick and suppress, if so configured. */
	rcu_stall_kick_kthreads();
	if (rcu_stall_is_suppressed())
		return;

	nbcon_cpu_emergency_enter();

	/*
	 * OK, time to rat on ourselves...
	 * See Documentation/RCU/stallwarn.rst for info on how to debug
	 * RCU CPU stall warnings.
	 */
	trace_rcu_stall_warning(rcu_state.name, TPS("SelfDetected"));
	pr_err("INFO: %s self-detected stall on CPU\n", rcu_state.name);
	raw_spin_lock_irqsave_rcu_node(rdp->mynode, flags);
	print_cpu_stall_info(smp_processor_id());
	raw_spin_unlock_irqrestore_rcu_node(rdp->mynode, flags);
	for_each_possible_cpu(cpu)
		totqlen += rcu_get_n_cbs_cpu(cpu);
	pr_err("\t(t=%lu jiffies g=%ld q=%lu ncpus=%d)\n",
		jiffies - gps,
		(long)rcu_seq_current(&rcu_state.gp_seq), totqlen,
		data_race(rcu_state.n_online_cpus)); // Diagnostic read

	rcu_check_gp_kthread_expired_fqs_timer();
	rcu_check_gp_kthread_starvation();

	rcu_dump_cpu_stacks(gp_seq);

	raw_spin_lock_irqsave_rcu_node(rnp, flags);
	/* Rewrite if needed in case of slow consoles. */
	if (ULONG_CMP_GE(jiffies, READ_ONCE(rcu_state.jiffies_stall)))
		WRITE_ONCE(rcu_state.jiffies_stall,
			   jiffies + 3 * rcu_jiffies_till_stall_check() + 3);
	raw_spin_unlock_irqrestore_rcu_node(rnp, flags);

	nbcon_cpu_emergency_exit();

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

static bool csd_lock_suppress_rcu_stall;
module_param(csd_lock_suppress_rcu_stall, bool, 0644);

static void check_cpu_stall(struct rcu_data *rdp)
{
	bool self_detected;
	unsigned long gs1;
	unsigned long gs2;
	unsigned long gps;
	unsigned long j;
	unsigned long jn;
	unsigned long js;
	struct rcu_node *rnp;

	lockdep_assert_irqs_disabled();
	if ((rcu_stall_is_suppressed() && !READ_ONCE(rcu_kick_kthreads)) ||
	    !rcu_gp_in_progress())
		return;
	rcu_stall_kick_kthreads();

	/*
	 * Check if it was requested (via rcu_cpu_stall_reset()) that the FQS
	 * loop has to set jiffies to ensure a non-stale jiffies value. This
	 * is required to have good jiffies value after coming out of long
	 * breaks of jiffies updates. Not doing so can cause false positives.
	 */
	if (READ_ONCE(rcu_state.nr_fqs_jiffies_stall) > 0)
		return;

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
	    ULONG_CMP_GE(gps, js) ||
	    !rcu_seq_state(gs2))
		return; /* No stall or GP completed since entering function. */
	rnp = rdp->mynode;
	jn = jiffies + ULONG_MAX / 2;
	self_detected = READ_ONCE(rnp->qsmask) & rdp->grpmask;
	if (rcu_gp_in_progress() &&
	    (self_detected || ULONG_CMP_GE(j, js + RCU_STALL_RAT_DELAY)) &&
	    cmpxchg(&rcu_state.jiffies_stall, js, jn) == js) {
		/*
		 * If a virtual machine is stopped by the host it can look to
		 * the watchdog like an RCU stall. Check to see if the host
		 * stopped the vm.
		 */
		if (kvm_check_and_clear_guest_paused())
			return;

		rcu_stall_notifier_call_chain(RCU_STALL_NOTIFY_NORM, (void *)j - gps);
		if (READ_ONCE(csd_lock_suppress_rcu_stall) && csd_lock_is_stuck()) {
			pr_err("INFO: %s detected stall, but suppressed full report due to a stuck CSD-lock.\n", rcu_state.name);
		} else if (self_detected) {
			/* We haven't checked in, so go dump stack. */
			print_cpu_stall(gs2, gps);
		} else {
			/* They had a few time units to dump stack, so complain. */
			print_other_cpu_stall(gs2, gps);
		}

		if (READ_ONCE(rcu_cpu_stall_ftrace_dump))
			rcu_ftrace_dump(DUMP_ALL);

		if (READ_ONCE(rcu_state.jiffies_stall) == jn) {
			jn = jiffies + 3 * rcu_jiffies_till_stall_check() + 3;
			WRITE_ONCE(rcu_state.jiffies_stall, jn);
		}
	}
}

//////////////////////////////////////////////////////////////////////////////
//
// RCU forward-progress mechanisms, including for callback invocation.


/*
 * Check to see if a failure to end RCU priority inversion was due to
 * a CPU not passing through a quiescent state.  When this happens, there
 * is nothing that RCU priority boosting can do to help, so we shouldn't
 * count this as an RCU priority boosting failure.  A return of true says
 * RCU priority boosting is to blame, and false says otherwise.  If false
 * is returned, the first of the CPUs to blame is stored through cpup.
 * If there was no CPU blocking the current grace period, but also nothing
 * in need of being boosted, *cpup is set to -1.  This can happen in case
 * of vCPU preemption while the last CPU is reporting its quiscent state,
 * for example.
 *
 * If cpup is NULL, then a lockless quick check is carried out, suitable
 * for high-rate usage.  On the other hand, if cpup is non-NULL, each
 * rcu_node structure's ->lock is acquired, ruling out high-rate usage.
 */
bool rcu_check_boost_fail(unsigned long gp_state, int *cpup)
{
	bool atb = false;
	int cpu;
	unsigned long flags;
	struct rcu_node *rnp;

	rcu_for_each_leaf_node(rnp) {
		if (!cpup) {
			if (data_race(READ_ONCE(rnp->qsmask))) {
				return false;
			} else {
				if (READ_ONCE(rnp->gp_tasks))
					atb = true;
				continue;
			}
		}
		*cpup = -1;
		raw_spin_lock_irqsave_rcu_node(rnp, flags);
		if (rnp->gp_tasks)
			atb = true;
		if (!rnp->qsmask) {
			// No CPUs without quiescent states for this rnp.
			raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
			continue;
		}
		// Find the first holdout CPU.
		for_each_leaf_node_possible_cpu(rnp, cpu) {
			if (rnp->qsmask & (1UL << (cpu - rnp->grplo))) {
				raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
				*cpup = cpu;
				return false;
			}
		}
		raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
	}
	// Can't blame CPUs, so must blame RCU priority boosting.
	return atb;
}
EXPORT_SYMBOL_GPL(rcu_check_boost_fail);

/*
 * Show the state of the grace-period kthreads.
 */
void show_rcu_gp_kthreads(void)
{
	unsigned long cbs = 0;
	int cpu;
	unsigned long j;
	unsigned long ja;
	unsigned long jr;
	unsigned long js;
	unsigned long jw;
	struct rcu_data *rdp;
	struct rcu_node *rnp;
	struct task_struct *t = READ_ONCE(rcu_state.gp_kthread);

	j = jiffies;
	ja = j - data_race(READ_ONCE(rcu_state.gp_activity));
	jr = j - data_race(READ_ONCE(rcu_state.gp_req_activity));
	js = j - data_race(READ_ONCE(rcu_state.gp_start));
	jw = j - data_race(READ_ONCE(rcu_state.gp_wake_time));
	pr_info("%s: wait state: %s(%d) ->state: %#x ->rt_priority %u delta ->gp_start %lu ->gp_activity %lu ->gp_req_activity %lu ->gp_wake_time %lu ->gp_wake_seq %ld ->gp_seq %ld ->gp_seq_needed %ld ->gp_max %lu ->gp_flags %#x\n",
		rcu_state.name, gp_state_getname(rcu_state.gp_state),
		data_race(READ_ONCE(rcu_state.gp_state)),
		t ? data_race(READ_ONCE(t->__state)) : 0x1ffff, t ? t->rt_priority : 0xffU,
		js, ja, jr, jw, (long)data_race(READ_ONCE(rcu_state.gp_wake_seq)),
		(long)data_race(READ_ONCE(rcu_state.gp_seq)),
		(long)data_race(READ_ONCE(rcu_get_root()->gp_seq_needed)),
		data_race(READ_ONCE(rcu_state.gp_max)),
		data_race(READ_ONCE(rcu_state.gp_flags)));
	rcu_for_each_node_breadth_first(rnp) {
		if (ULONG_CMP_GE(READ_ONCE(rcu_state.gp_seq), READ_ONCE(rnp->gp_seq_needed)) &&
		    !data_race(READ_ONCE(rnp->qsmask)) && !data_race(READ_ONCE(rnp->boost_tasks)) &&
		    !data_race(READ_ONCE(rnp->exp_tasks)) && !data_race(READ_ONCE(rnp->gp_tasks)))
			continue;
		pr_info("\trcu_node %d:%d ->gp_seq %ld ->gp_seq_needed %ld ->qsmask %#lx %c%c%c%c ->n_boosts %ld\n",
			rnp->grplo, rnp->grphi,
			(long)data_race(READ_ONCE(rnp->gp_seq)),
			(long)data_race(READ_ONCE(rnp->gp_seq_needed)),
			data_race(READ_ONCE(rnp->qsmask)),
			".b"[!!data_race(READ_ONCE(rnp->boost_kthread_task))],
			".B"[!!data_race(READ_ONCE(rnp->boost_tasks))],
			".E"[!!data_race(READ_ONCE(rnp->exp_tasks))],
			".G"[!!data_race(READ_ONCE(rnp->gp_tasks))],
			data_race(READ_ONCE(rnp->n_boosts)));
		if (!rcu_is_leaf_node(rnp))
			continue;
		for_each_leaf_node_possible_cpu(rnp, cpu) {
			rdp = per_cpu_ptr(&rcu_data, cpu);
			if (READ_ONCE(rdp->gpwrap) ||
			    ULONG_CMP_GE(READ_ONCE(rcu_state.gp_seq),
					 READ_ONCE(rdp->gp_seq_needed)))
				continue;
			pr_info("\tcpu %d ->gp_seq_needed %ld\n",
				cpu, (long)data_race(READ_ONCE(rdp->gp_seq_needed)));
		}
	}
	for_each_possible_cpu(cpu) {
		rdp = per_cpu_ptr(&rcu_data, cpu);
		cbs += data_race(READ_ONCE(rdp->n_cbs_invoked));
		if (rcu_segcblist_is_offloaded(&rdp->cblist))
			show_rcu_nocb_state(rdp);
	}
	pr_info("RCU callbacks invoked since boot: %lu\n", cbs);
	show_rcu_tasks_gp_kthreads();
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
	    ULONG_CMP_GE(READ_ONCE(rnp_root->gp_seq),
			 READ_ONCE(rnp_root->gp_seq_needed)) ||
	    !smp_load_acquire(&rcu_state.gp_kthread)) // Get stable kthread.
		return;
	j = jiffies; /* Expensive access, and in common case don't get here. */
	if (time_before(j, READ_ONCE(rcu_state.gp_req_activity) + gpssdelay) ||
	    time_before(j, READ_ONCE(rcu_state.gp_activity) + gpssdelay) ||
	    atomic_read(&warned))
		return;

	raw_spin_lock_irqsave_rcu_node(rnp, flags);
	j = jiffies;
	if (rcu_gp_in_progress() ||
	    ULONG_CMP_GE(READ_ONCE(rnp_root->gp_seq),
			 READ_ONCE(rnp_root->gp_seq_needed)) ||
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
	    ULONG_CMP_GE(READ_ONCE(rnp_root->gp_seq),
			 READ_ONCE(rnp_root->gp_seq_needed)) ||
	    time_before(j, READ_ONCE(rcu_state.gp_req_activity) + gpssdelay) ||
	    time_before(j, READ_ONCE(rcu_state.gp_activity) + gpssdelay) ||
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
			__func__, jiffies - data_race(READ_ONCE(rcu_state.gp_start)));
		show_rcu_gp_kthreads();
	} else {
		pr_info("%s: Last GP end %lu jiffies ago\n",
			__func__, jiffies - data_race(READ_ONCE(rcu_state.gp_end)));
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
static void sysrq_show_rcu(u8 key)
{
	show_rcu_gp_kthreads();
}

static const struct sysrq_key_op sysrq_rcudump_op = {
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

#ifdef CONFIG_RCU_CPU_STALL_NOTIFIER

//////////////////////////////////////////////////////////////////////////////
//
// RCU CPU stall-warning notifiers

static ATOMIC_NOTIFIER_HEAD(rcu_cpu_stall_notifier_list);

/**
 * rcu_stall_chain_notifier_register - Add an RCU CPU stall notifier
 * @n: Entry to add.
 *
 * Adds an RCU CPU stall notifier to an atomic notifier chain.
 * The @action passed to a notifier will be @RCU_STALL_NOTIFY_NORM or
 * friends.  The @data will be the duration of the stalled grace period,
 * in jiffies, coerced to a void* pointer.
 *
 * Returns 0 on success, %-EEXIST on error.
 */
int rcu_stall_chain_notifier_register(struct notifier_block *n)
{
	int rcsn = rcu_cpu_stall_notifiers;

	WARN(1, "Adding %pS() to RCU stall notifier list (%s).\n", n->notifier_call,
	     rcsn ? "possibly suppressing RCU CPU stall warnings" : "failed, so all is well");
	if (rcsn)
		return atomic_notifier_chain_register(&rcu_cpu_stall_notifier_list, n);
	return -EEXIST;
}
EXPORT_SYMBOL_GPL(rcu_stall_chain_notifier_register);

/**
 * rcu_stall_chain_notifier_unregister - Remove an RCU CPU stall notifier
 * @n: Entry to add.
 *
 * Removes an RCU CPU stall notifier from an atomic notifier chain.
 *
 * Returns zero on success, %-ENOENT on failure.
 */
int rcu_stall_chain_notifier_unregister(struct notifier_block *n)
{
	return atomic_notifier_chain_unregister(&rcu_cpu_stall_notifier_list, n);
}
EXPORT_SYMBOL_GPL(rcu_stall_chain_notifier_unregister);

/*
 * rcu_stall_notifier_call_chain - Call functions in an RCU CPU stall notifier chain
 * @val: Value passed unmodified to notifier function
 * @v: Pointer passed unmodified to notifier function
 *
 * Calls each function in the RCU CPU stall notifier chain in turn, which
 * is an atomic call chain.  See atomic_notifier_call_chain() for more
 * information.
 *
 * This is for use within RCU, hence the omission of the extra asterisk
 * to indicate a non-kerneldoc format header comment.
 */
int rcu_stall_notifier_call_chain(unsigned long val, void *v)
{
	return atomic_notifier_call_chain(&rcu_cpu_stall_notifier_list, val, v);
}

#endif // #ifdef CONFIG_RCU_CPU_STALL_NOTIFIER
