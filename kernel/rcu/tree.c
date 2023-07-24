// SPDX-License-Identifier: GPL-2.0+
/*
 * Read-Copy Update mechanism for mutual exclusion (tree-based version)
 *
 * Copyright IBM Corporation, 2008
 *
 * Authors: Dipankar Sarma <dipankar@in.ibm.com>
 *	    Manfred Spraul <manfred@colorfullife.com>
 *	    Paul E. McKenney <paulmck@linux.ibm.com>
 *
 * Based on the original work by Paul McKenney <paulmck@linux.ibm.com>
 * and inputs from Rusty Russell, Andrea Arcangeli and Andi Kleen.
 *
 * For detailed explanation of Read-Copy Update mechanism see -
 *	Documentation/RCU
 */

#define pr_fmt(fmt) "rcu: " fmt

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/smp.h>
#include <linux/rcupdate_wait.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/nmi.h>
#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/export.h>
#include <linux/completion.h>
#include <linux/moduleparam.h>
#include <linux/panic.h>
#include <linux/panic_notifier.h>
#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/mutex.h>
#include <linux/time.h>
#include <linux/kernel_stat.h>
#include <linux/wait.h>
#include <linux/kthread.h>
#include <uapi/linux/sched/types.h>
#include <linux/prefetch.h>
#include <linux/delay.h>
#include <linux/random.h>
#include <linux/trace_events.h>
#include <linux/suspend.h>
#include <linux/ftrace.h>
#include <linux/tick.h>
#include <linux/sysrq.h>
#include <linux/kprobes.h>
#include <linux/gfp.h>
#include <linux/oom.h>
#include <linux/smpboot.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/sched/isolation.h>
#include <linux/sched/clock.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/kasan.h>
#include <linux/context_tracking.h>
#include "../time/tick-internal.h"

#include "tree.h"
#include "rcu.h"

#ifdef MODULE_PARAM_PREFIX
#undef MODULE_PARAM_PREFIX
#endif
#define MODULE_PARAM_PREFIX "rcutree."

/* Data structures. */

static DEFINE_PER_CPU_SHARED_ALIGNED(struct rcu_data, rcu_data) = {
	.gpwrap = true,
#ifdef CONFIG_RCU_NOCB_CPU
	.cblist.flags = SEGCBLIST_RCU_CORE,
#endif
};
static struct rcu_state rcu_state = {
	.level = { &rcu_state.node[0] },
	.gp_state = RCU_GP_IDLE,
	.gp_seq = (0UL - 300UL) << RCU_SEQ_CTR_SHIFT,
	.barrier_mutex = __MUTEX_INITIALIZER(rcu_state.barrier_mutex),
	.barrier_lock = __RAW_SPIN_LOCK_UNLOCKED(rcu_state.barrier_lock),
	.name = RCU_NAME,
	.abbr = RCU_ABBR,
	.exp_mutex = __MUTEX_INITIALIZER(rcu_state.exp_mutex),
	.exp_wake_mutex = __MUTEX_INITIALIZER(rcu_state.exp_wake_mutex),
	.ofl_lock = __ARCH_SPIN_LOCK_UNLOCKED,
};

/* Dump rcu_node combining tree at boot to verify correct setup. */
static bool dump_tree;
module_param(dump_tree, bool, 0444);
/* By default, use RCU_SOFTIRQ instead of rcuc kthreads. */
static bool use_softirq = !IS_ENABLED(CONFIG_PREEMPT_RT);
#ifndef CONFIG_PREEMPT_RT
module_param(use_softirq, bool, 0444);
#endif
/* Control rcu_node-tree auto-balancing at boot time. */
static bool rcu_fanout_exact;
module_param(rcu_fanout_exact, bool, 0444);
/* Increase (but not decrease) the RCU_FANOUT_LEAF at boot time. */
static int rcu_fanout_leaf = RCU_FANOUT_LEAF;
module_param(rcu_fanout_leaf, int, 0444);
int rcu_num_lvls __read_mostly = RCU_NUM_LVLS;
/* Number of rcu_nodes at specified level. */
int num_rcu_lvl[] = NUM_RCU_LVL_INIT;
int rcu_num_nodes __read_mostly = NUM_RCU_NODES; /* Total # rcu_nodes in use. */

/*
 * The rcu_scheduler_active variable is initialized to the value
 * RCU_SCHEDULER_INACTIVE and transitions RCU_SCHEDULER_INIT just before the
 * first task is spawned.  So when this variable is RCU_SCHEDULER_INACTIVE,
 * RCU can assume that there is but one task, allowing RCU to (for example)
 * optimize synchronize_rcu() to a simple barrier().  When this variable
 * is RCU_SCHEDULER_INIT, RCU must actually do all the hard work required
 * to detect real grace periods.  This variable is also used to suppress
 * boot-time false positives from lockdep-RCU error checking.  Finally, it
 * transitions from RCU_SCHEDULER_INIT to RCU_SCHEDULER_RUNNING after RCU
 * is fully initialized, including all of its kthreads having been spawned.
 */
int rcu_scheduler_active __read_mostly;
EXPORT_SYMBOL_GPL(rcu_scheduler_active);

/*
 * The rcu_scheduler_fully_active variable transitions from zero to one
 * during the early_initcall() processing, which is after the scheduler
 * is capable of creating new tasks.  So RCU processing (for example,
 * creating tasks for RCU priority boosting) must be delayed until after
 * rcu_scheduler_fully_active transitions from zero to one.  We also
 * currently delay invocation of any RCU callbacks until after this point.
 *
 * It might later prove better for people registering RCU callbacks during
 * early boot to take responsibility for these callbacks, but one step at
 * a time.
 */
static int rcu_scheduler_fully_active __read_mostly;

static void rcu_report_qs_rnp(unsigned long mask, struct rcu_node *rnp,
			      unsigned long gps, unsigned long flags);
static void rcu_boost_kthread_setaffinity(struct rcu_node *rnp, int outgoingcpu);
static void invoke_rcu_core(void);
static void rcu_report_exp_rdp(struct rcu_data *rdp);
static void sync_sched_exp_online_cleanup(int cpu);
static void check_cb_ovld_locked(struct rcu_data *rdp, struct rcu_node *rnp);
static bool rcu_rdp_is_offloaded(struct rcu_data *rdp);
static bool rcu_rdp_cpu_online(struct rcu_data *rdp);
static bool rcu_init_invoked(void);
static void rcu_cleanup_dead_rnp(struct rcu_node *rnp_leaf);
static void rcu_init_new_rnp(struct rcu_node *rnp_leaf);

/*
 * rcuc/rcub/rcuop kthread realtime priority. The "rcuop"
 * real-time priority(enabling/disabling) is controlled by
 * the extra CONFIG_RCU_NOCB_CPU_CB_BOOST configuration.
 */
static int kthread_prio = IS_ENABLED(CONFIG_RCU_BOOST) ? 1 : 0;
module_param(kthread_prio, int, 0444);

/* Delay in jiffies for grace-period initialization delays, debug only. */

static int gp_preinit_delay;
module_param(gp_preinit_delay, int, 0444);
static int gp_init_delay;
module_param(gp_init_delay, int, 0444);
static int gp_cleanup_delay;
module_param(gp_cleanup_delay, int, 0444);

// Add delay to rcu_read_unlock() for strict grace periods.
static int rcu_unlock_delay;
#ifdef CONFIG_RCU_STRICT_GRACE_PERIOD
module_param(rcu_unlock_delay, int, 0444);
#endif

/*
 * This rcu parameter is runtime-read-only. It reflects
 * a minimum allowed number of objects which can be cached
 * per-CPU. Object size is equal to one page. This value
 * can be changed at boot time.
 */
static int rcu_min_cached_objs = 5;
module_param(rcu_min_cached_objs, int, 0444);

// A page shrinker can ask for pages to be freed to make them
// available for other parts of the system. This usually happens
// under low memory conditions, and in that case we should also
// defer page-cache filling for a short time period.
//
// The default value is 5 seconds, which is long enough to reduce
// interference with the shrinker while it asks other systems to
// drain their caches.
static int rcu_delay_page_cache_fill_msec = 5000;
module_param(rcu_delay_page_cache_fill_msec, int, 0444);

/* Retrieve RCU kthreads priority for rcutorture */
int rcu_get_gp_kthreads_prio(void)
{
	return kthread_prio;
}
EXPORT_SYMBOL_GPL(rcu_get_gp_kthreads_prio);

/*
 * Number of grace periods between delays, normalized by the duration of
 * the delay.  The longer the delay, the more the grace periods between
 * each delay.  The reason for this normalization is that it means that,
 * for non-zero delays, the overall slowdown of grace periods is constant
 * regardless of the duration of the delay.  This arrangement balances
 * the need for long delays to increase some race probabilities with the
 * need for fast grace periods to increase other race probabilities.
 */
#define PER_RCU_NODE_PERIOD 3	/* Number of grace periods between delays for debugging. */

/*
 * Return true if an RCU grace period is in progress.  The READ_ONCE()s
 * permit this function to be invoked without holding the root rcu_node
 * structure's ->lock, but of course results can be subject to change.
 */
static int rcu_gp_in_progress(void)
{
	return rcu_seq_state(rcu_seq_current(&rcu_state.gp_seq));
}

/*
 * Return the number of callbacks queued on the specified CPU.
 * Handles both the nocbs and normal cases.
 */
static long rcu_get_n_cbs_cpu(int cpu)
{
	struct rcu_data *rdp = per_cpu_ptr(&rcu_data, cpu);

	if (rcu_segcblist_is_enabled(&rdp->cblist))
		return rcu_segcblist_n_cbs(&rdp->cblist);
	return 0;
}

void rcu_softirq_qs(void)
{
	rcu_qs();
	rcu_preempt_deferred_qs(current);
	rcu_tasks_qs(current, false);
}

/*
 * Reset the current CPU's ->dynticks counter to indicate that the
 * newly onlined CPU is no longer in an extended quiescent state.
 * This will either leave the counter unchanged, or increment it
 * to the next non-quiescent value.
 *
 * The non-atomic test/increment sequence works because the upper bits
 * of the ->dynticks counter are manipulated only by the corresponding CPU,
 * or when the corresponding CPU is offline.
 */
static void rcu_dynticks_eqs_online(void)
{
	if (ct_dynticks() & RCU_DYNTICKS_IDX)
		return;
	ct_state_inc(RCU_DYNTICKS_IDX);
}

/*
 * Snapshot the ->dynticks counter with full ordering so as to allow
 * stable comparison of this counter with past and future snapshots.
 */
static int rcu_dynticks_snap(int cpu)
{
	smp_mb();  // Fundamental RCU ordering guarantee.
	return ct_dynticks_cpu_acquire(cpu);
}

/*
 * Return true if the snapshot returned from rcu_dynticks_snap()
 * indicates that RCU is in an extended quiescent state.
 */
static bool rcu_dynticks_in_eqs(int snap)
{
	return !(snap & RCU_DYNTICKS_IDX);
}

/*
 * Return true if the CPU corresponding to the specified rcu_data
 * structure has spent some time in an extended quiescent state since
 * rcu_dynticks_snap() returned the specified snapshot.
 */
static bool rcu_dynticks_in_eqs_since(struct rcu_data *rdp, int snap)
{
	return snap != rcu_dynticks_snap(rdp->cpu);
}

/*
 * Return true if the referenced integer is zero while the specified
 * CPU remains within a single extended quiescent state.
 */
bool rcu_dynticks_zero_in_eqs(int cpu, int *vp)
{
	int snap;

	// If not quiescent, force back to earlier extended quiescent state.
	snap = ct_dynticks_cpu(cpu) & ~RCU_DYNTICKS_IDX;
	smp_rmb(); // Order ->dynticks and *vp reads.
	if (READ_ONCE(*vp))
		return false;  // Non-zero, so report failure;
	smp_rmb(); // Order *vp read and ->dynticks re-read.

	// If still in the same extended quiescent state, we are good!
	return snap == ct_dynticks_cpu(cpu);
}

/*
 * Let the RCU core know that this CPU has gone through the scheduler,
 * which is a quiescent state.  This is called when the need for a
 * quiescent state is urgent, so we burn an atomic operation and full
 * memory barriers to let the RCU core know about it, regardless of what
 * this CPU might (or might not) do in the near future.
 *
 * We inform the RCU core by emulating a zero-duration dyntick-idle period.
 *
 * The caller must have disabled interrupts and must not be idle.
 */
notrace void rcu_momentary_dyntick_idle(void)
{
	int seq;

	raw_cpu_write(rcu_data.rcu_need_heavy_qs, false);
	seq = ct_state_inc(2 * RCU_DYNTICKS_IDX);
	/* It is illegal to call this from idle state. */
	WARN_ON_ONCE(!(seq & RCU_DYNTICKS_IDX));
	rcu_preempt_deferred_qs(current);
}
EXPORT_SYMBOL_GPL(rcu_momentary_dyntick_idle);

/**
 * rcu_is_cpu_rrupt_from_idle - see if 'interrupted' from idle
 *
 * If the current CPU is idle and running at a first-level (not nested)
 * interrupt, or directly, from idle, return true.
 *
 * The caller must have at least disabled IRQs.
 */
static int rcu_is_cpu_rrupt_from_idle(void)
{
	long nesting;

	/*
	 * Usually called from the tick; but also used from smp_function_call()
	 * for expedited grace periods. This latter can result in running from
	 * the idle task, instead of an actual IPI.
	 */
	lockdep_assert_irqs_disabled();

	/* Check for counter underflows */
	RCU_LOCKDEP_WARN(ct_dynticks_nesting() < 0,
			 "RCU dynticks_nesting counter underflow!");
	RCU_LOCKDEP_WARN(ct_dynticks_nmi_nesting() <= 0,
			 "RCU dynticks_nmi_nesting counter underflow/zero!");

	/* Are we at first interrupt nesting level? */
	nesting = ct_dynticks_nmi_nesting();
	if (nesting > 1)
		return false;

	/*
	 * If we're not in an interrupt, we must be in the idle task!
	 */
	WARN_ON_ONCE(!nesting && !is_idle_task(current));

	/* Does CPU appear to be idle from an RCU standpoint? */
	return ct_dynticks_nesting() == 0;
}

#define DEFAULT_RCU_BLIMIT (IS_ENABLED(CONFIG_RCU_STRICT_GRACE_PERIOD) ? 1000 : 10)
				// Maximum callbacks per rcu_do_batch ...
#define DEFAULT_MAX_RCU_BLIMIT 10000 // ... even during callback flood.
static long blimit = DEFAULT_RCU_BLIMIT;
#define DEFAULT_RCU_QHIMARK 10000 // If this many pending, ignore blimit.
static long qhimark = DEFAULT_RCU_QHIMARK;
#define DEFAULT_RCU_QLOMARK 100   // Once only this many pending, use blimit.
static long qlowmark = DEFAULT_RCU_QLOMARK;
#define DEFAULT_RCU_QOVLD_MULT 2
#define DEFAULT_RCU_QOVLD (DEFAULT_RCU_QOVLD_MULT * DEFAULT_RCU_QHIMARK)
static long qovld = DEFAULT_RCU_QOVLD; // If this many pending, hammer QS.
static long qovld_calc = -1;	  // No pre-initialization lock acquisitions!

module_param(blimit, long, 0444);
module_param(qhimark, long, 0444);
module_param(qlowmark, long, 0444);
module_param(qovld, long, 0444);

static ulong jiffies_till_first_fqs = IS_ENABLED(CONFIG_RCU_STRICT_GRACE_PERIOD) ? 0 : ULONG_MAX;
static ulong jiffies_till_next_fqs = ULONG_MAX;
static bool rcu_kick_kthreads;
static int rcu_divisor = 7;
module_param(rcu_divisor, int, 0644);

/* Force an exit from rcu_do_batch() after 3 milliseconds. */
static long rcu_resched_ns = 3 * NSEC_PER_MSEC;
module_param(rcu_resched_ns, long, 0644);

/*
 * How long the grace period must be before we start recruiting
 * quiescent-state help from rcu_note_context_switch().
 */
static ulong jiffies_till_sched_qs = ULONG_MAX;
module_param(jiffies_till_sched_qs, ulong, 0444);
static ulong jiffies_to_sched_qs; /* See adjust_jiffies_till_sched_qs(). */
module_param(jiffies_to_sched_qs, ulong, 0444); /* Display only! */

/*
 * Make sure that we give the grace-period kthread time to detect any
 * idle CPUs before taking active measures to force quiescent states.
 * However, don't go below 100 milliseconds, adjusted upwards for really
 * large systems.
 */
static void adjust_jiffies_till_sched_qs(void)
{
	unsigned long j;

	/* If jiffies_till_sched_qs was specified, respect the request. */
	if (jiffies_till_sched_qs != ULONG_MAX) {
		WRITE_ONCE(jiffies_to_sched_qs, jiffies_till_sched_qs);
		return;
	}
	/* Otherwise, set to third fqs scan, but bound below on large system. */
	j = READ_ONCE(jiffies_till_first_fqs) +
		      2 * READ_ONCE(jiffies_till_next_fqs);
	if (j < HZ / 10 + nr_cpu_ids / RCU_JIFFIES_FQS_DIV)
		j = HZ / 10 + nr_cpu_ids / RCU_JIFFIES_FQS_DIV;
	pr_info("RCU calculated value of scheduler-enlistment delay is %ld jiffies.\n", j);
	WRITE_ONCE(jiffies_to_sched_qs, j);
}

static int param_set_first_fqs_jiffies(const char *val, const struct kernel_param *kp)
{
	ulong j;
	int ret = kstrtoul(val, 0, &j);

	if (!ret) {
		WRITE_ONCE(*(ulong *)kp->arg, (j > HZ) ? HZ : j);
		adjust_jiffies_till_sched_qs();
	}
	return ret;
}

static int param_set_next_fqs_jiffies(const char *val, const struct kernel_param *kp)
{
	ulong j;
	int ret = kstrtoul(val, 0, &j);

	if (!ret) {
		WRITE_ONCE(*(ulong *)kp->arg, (j > HZ) ? HZ : (j ?: 1));
		adjust_jiffies_till_sched_qs();
	}
	return ret;
}

static const struct kernel_param_ops first_fqs_jiffies_ops = {
	.set = param_set_first_fqs_jiffies,
	.get = param_get_ulong,
};

static const struct kernel_param_ops next_fqs_jiffies_ops = {
	.set = param_set_next_fqs_jiffies,
	.get = param_get_ulong,
};

module_param_cb(jiffies_till_first_fqs, &first_fqs_jiffies_ops, &jiffies_till_first_fqs, 0644);
module_param_cb(jiffies_till_next_fqs, &next_fqs_jiffies_ops, &jiffies_till_next_fqs, 0644);
module_param(rcu_kick_kthreads, bool, 0644);

static void force_qs_rnp(int (*f)(struct rcu_data *rdp));
static int rcu_pending(int user);

/*
 * Return the number of RCU GPs completed thus far for debug & stats.
 */
unsigned long rcu_get_gp_seq(void)
{
	return READ_ONCE(rcu_state.gp_seq);
}
EXPORT_SYMBOL_GPL(rcu_get_gp_seq);

/*
 * Return the number of RCU expedited batches completed thus far for
 * debug & stats.  Odd numbers mean that a batch is in progress, even
 * numbers mean idle.  The value returned will thus be roughly double
 * the cumulative batches since boot.
 */
unsigned long rcu_exp_batches_completed(void)
{
	return rcu_state.expedited_sequence;
}
EXPORT_SYMBOL_GPL(rcu_exp_batches_completed);

/*
 * Return the root node of the rcu_state structure.
 */
static struct rcu_node *rcu_get_root(void)
{
	return &rcu_state.node[0];
}

/*
 * Send along grace-period-related data for rcutorture diagnostics.
 */
void rcutorture_get_gp_data(enum rcutorture_type test_type, int *flags,
			    unsigned long *gp_seq)
{
	switch (test_type) {
	case RCU_FLAVOR:
		*flags = READ_ONCE(rcu_state.gp_flags);
		*gp_seq = rcu_seq_current(&rcu_state.gp_seq);
		break;
	default:
		break;
	}
}
EXPORT_SYMBOL_GPL(rcutorture_get_gp_data);

#if defined(CONFIG_NO_HZ_FULL) && (!defined(CONFIG_GENERIC_ENTRY) || !defined(CONFIG_KVM_XFER_TO_GUEST_WORK))
/*
 * An empty function that will trigger a reschedule on
 * IRQ tail once IRQs get re-enabled on userspace/guest resume.
 */
static void late_wakeup_func(struct irq_work *work)
{
}

static DEFINE_PER_CPU(struct irq_work, late_wakeup_work) =
	IRQ_WORK_INIT(late_wakeup_func);

/*
 * If either:
 *
 * 1) the task is about to enter in guest mode and $ARCH doesn't support KVM generic work
 * 2) the task is about to enter in user mode and $ARCH doesn't support generic entry.
 *
 * In these cases the late RCU wake ups aren't supported in the resched loops and our
 * last resort is to fire a local irq_work that will trigger a reschedule once IRQs
 * get re-enabled again.
 */
noinstr void rcu_irq_work_resched(void)
{
	struct rcu_data *rdp = this_cpu_ptr(&rcu_data);

	if (IS_ENABLED(CONFIG_GENERIC_ENTRY) && !(current->flags & PF_VCPU))
		return;

	if (IS_ENABLED(CONFIG_KVM_XFER_TO_GUEST_WORK) && (current->flags & PF_VCPU))
		return;

	instrumentation_begin();
	if (do_nocb_deferred_wakeup(rdp) && need_resched()) {
		irq_work_queue(this_cpu_ptr(&late_wakeup_work));
	}
	instrumentation_end();
}
#endif /* #if defined(CONFIG_NO_HZ_FULL) && (!defined(CONFIG_GENERIC_ENTRY) || !defined(CONFIG_KVM_XFER_TO_GUEST_WORK)) */

#ifdef CONFIG_PROVE_RCU
/**
 * rcu_irq_exit_check_preempt - Validate that scheduling is possible
 */
void rcu_irq_exit_check_preempt(void)
{
	lockdep_assert_irqs_disabled();

	RCU_LOCKDEP_WARN(ct_dynticks_nesting() <= 0,
			 "RCU dynticks_nesting counter underflow/zero!");
	RCU_LOCKDEP_WARN(ct_dynticks_nmi_nesting() !=
			 DYNTICK_IRQ_NONIDLE,
			 "Bad RCU  dynticks_nmi_nesting counter\n");
	RCU_LOCKDEP_WARN(rcu_dynticks_curr_cpu_in_eqs(),
			 "RCU in extended quiescent state!");
}
#endif /* #ifdef CONFIG_PROVE_RCU */

#ifdef CONFIG_NO_HZ_FULL
/**
 * __rcu_irq_enter_check_tick - Enable scheduler tick on CPU if RCU needs it.
 *
 * The scheduler tick is not normally enabled when CPUs enter the kernel
 * from nohz_full userspace execution.  After all, nohz_full userspace
 * execution is an RCU quiescent state and the time executing in the kernel
 * is quite short.  Except of course when it isn't.  And it is not hard to
 * cause a large system to spend tens of seconds or even minutes looping
 * in the kernel, which can cause a number of problems, include RCU CPU
 * stall warnings.
 *
 * Therefore, if a nohz_full CPU fails to report a quiescent state
 * in a timely manner, the RCU grace-period kthread sets that CPU's
 * ->rcu_urgent_qs flag with the expectation that the next interrupt or
 * exception will invoke this function, which will turn on the scheduler
 * tick, which will enable RCU to detect that CPU's quiescent states,
 * for example, due to cond_resched() calls in CONFIG_PREEMPT=n kernels.
 * The tick will be disabled once a quiescent state is reported for
 * this CPU.
 *
 * Of course, in carefully tuned systems, there might never be an
 * interrupt or exception.  In that case, the RCU grace-period kthread
 * will eventually cause one to happen.  However, in less carefully
 * controlled environments, this function allows RCU to get what it
 * needs without creating otherwise useless interruptions.
 */
void __rcu_irq_enter_check_tick(void)
{
	struct rcu_data *rdp = this_cpu_ptr(&rcu_data);

	// If we're here from NMI there's nothing to do.
	if (in_nmi())
		return;

	RCU_LOCKDEP_WARN(rcu_dynticks_curr_cpu_in_eqs(),
			 "Illegal rcu_irq_enter_check_tick() from extended quiescent state");

	if (!tick_nohz_full_cpu(rdp->cpu) ||
	    !READ_ONCE(rdp->rcu_urgent_qs) ||
	    READ_ONCE(rdp->rcu_forced_tick)) {
		// RCU doesn't need nohz_full help from this CPU, or it is
		// already getting that help.
		return;
	}

	// We get here only when not in an extended quiescent state and
	// from interrupts (as opposed to NMIs).  Therefore, (1) RCU is
	// already watching and (2) The fact that we are in an interrupt
	// handler and that the rcu_node lock is an irq-disabled lock
	// prevents self-deadlock.  So we can safely recheck under the lock.
	// Note that the nohz_full state currently cannot change.
	raw_spin_lock_rcu_node(rdp->mynode);
	if (rdp->rcu_urgent_qs && !rdp->rcu_forced_tick) {
		// A nohz_full CPU is in the kernel and RCU needs a
		// quiescent state.  Turn on the tick!
		WRITE_ONCE(rdp->rcu_forced_tick, true);
		tick_dep_set_cpu(rdp->cpu, TICK_DEP_BIT_RCU);
	}
	raw_spin_unlock_rcu_node(rdp->mynode);
}
NOKPROBE_SYMBOL(__rcu_irq_enter_check_tick);
#endif /* CONFIG_NO_HZ_FULL */

/*
 * Check to see if any future non-offloaded RCU-related work will need
 * to be done by the current CPU, even if none need be done immediately,
 * returning 1 if so.  This function is part of the RCU implementation;
 * it is -not- an exported member of the RCU API.  This is used by
 * the idle-entry code to figure out whether it is safe to disable the
 * scheduler-clock interrupt.
 *
 * Just check whether or not this CPU has non-offloaded RCU callbacks
 * queued.
 */
int rcu_needs_cpu(void)
{
	return !rcu_segcblist_empty(&this_cpu_ptr(&rcu_data)->cblist) &&
		!rcu_rdp_is_offloaded(this_cpu_ptr(&rcu_data));
}

/*
 * If any sort of urgency was applied to the current CPU (for example,
 * the scheduler-clock interrupt was enabled on a nohz_full CPU) in order
 * to get to a quiescent state, disable it.
 */
static void rcu_disable_urgency_upon_qs(struct rcu_data *rdp)
{
	raw_lockdep_assert_held_rcu_node(rdp->mynode);
	WRITE_ONCE(rdp->rcu_urgent_qs, false);
	WRITE_ONCE(rdp->rcu_need_heavy_qs, false);
	if (tick_nohz_full_cpu(rdp->cpu) && rdp->rcu_forced_tick) {
		tick_dep_clear_cpu(rdp->cpu, TICK_DEP_BIT_RCU);
		WRITE_ONCE(rdp->rcu_forced_tick, false);
	}
}

/**
 * rcu_is_watching - see if RCU thinks that the current CPU is not idle
 *
 * Return true if RCU is watching the running CPU, which means that this
 * CPU can safely enter RCU read-side critical sections.  In other words,
 * if the current CPU is not in its idle loop or is in an interrupt or
 * NMI handler, return true.
 *
 * Make notrace because it can be called by the internal functions of
 * ftrace, and making this notrace removes unnecessary recursion calls.
 */
notrace bool rcu_is_watching(void)
{
	bool ret;

	preempt_disable_notrace();
	ret = !rcu_dynticks_curr_cpu_in_eqs();
	preempt_enable_notrace();
	return ret;
}
EXPORT_SYMBOL_GPL(rcu_is_watching);

/*
 * If a holdout task is actually running, request an urgent quiescent
 * state from its CPU.  This is unsynchronized, so migrations can cause
 * the request to go to the wrong CPU.  Which is OK, all that will happen
 * is that the CPU's next context switch will be a bit slower and next
 * time around this task will generate another request.
 */
void rcu_request_urgent_qs_task(struct task_struct *t)
{
	int cpu;

	barrier();
	cpu = task_cpu(t);
	if (!task_curr(t))
		return; /* This task is not running on that CPU. */
	smp_store_release(per_cpu_ptr(&rcu_data.rcu_urgent_qs, cpu), true);
}

/*
 * When trying to report a quiescent state on behalf of some other CPU,
 * it is our responsibility to check for and handle potential overflow
 * of the rcu_node ->gp_seq counter with respect to the rcu_data counters.
 * After all, the CPU might be in deep idle state, and thus executing no
 * code whatsoever.
 */
static void rcu_gpnum_ovf(struct rcu_node *rnp, struct rcu_data *rdp)
{
	raw_lockdep_assert_held_rcu_node(rnp);
	if (ULONG_CMP_LT(rcu_seq_current(&rdp->gp_seq) + ULONG_MAX / 4,
			 rnp->gp_seq))
		WRITE_ONCE(rdp->gpwrap, true);
	if (ULONG_CMP_LT(rdp->rcu_iw_gp_seq + ULONG_MAX / 4, rnp->gp_seq))
		rdp->rcu_iw_gp_seq = rnp->gp_seq + ULONG_MAX / 4;
}

/*
 * Snapshot the specified CPU's dynticks counter so that we can later
 * credit them with an implicit quiescent state.  Return 1 if this CPU
 * is in dynticks idle mode, which is an extended quiescent state.
 */
static int dyntick_save_progress_counter(struct rcu_data *rdp)
{
	rdp->dynticks_snap = rcu_dynticks_snap(rdp->cpu);
	if (rcu_dynticks_in_eqs(rdp->dynticks_snap)) {
		trace_rcu_fqs(rcu_state.name, rdp->gp_seq, rdp->cpu, TPS("dti"));
		rcu_gpnum_ovf(rdp->mynode, rdp);
		return 1;
	}
	return 0;
}

/*
 * Return true if the specified CPU has passed through a quiescent
 * state by virtue of being in or having passed through an dynticks
 * idle state since the last call to dyntick_save_progress_counter()
 * for this same CPU, or by virtue of having been offline.
 */
static int rcu_implicit_dynticks_qs(struct rcu_data *rdp)
{
	unsigned long jtsq;
	struct rcu_node *rnp = rdp->mynode;

	/*
	 * If the CPU passed through or entered a dynticks idle phase with
	 * no active irq/NMI handlers, then we can safely pretend that the CPU
	 * already acknowledged the request to pass through a quiescent
	 * state.  Either way, that CPU cannot possibly be in an RCU
	 * read-side critical section that started before the beginning
	 * of the current RCU grace period.
	 */
	if (rcu_dynticks_in_eqs_since(rdp, rdp->dynticks_snap)) {
		trace_rcu_fqs(rcu_state.name, rdp->gp_seq, rdp->cpu, TPS("dti"));
		rcu_gpnum_ovf(rnp, rdp);
		return 1;
	}

	/*
	 * Complain if a CPU that is considered to be offline from RCU's
	 * perspective has not yet reported a quiescent state.  After all,
	 * the offline CPU should have reported a quiescent state during
	 * the CPU-offline process, or, failing that, by rcu_gp_init()
	 * if it ran concurrently with either the CPU going offline or the
	 * last task on a leaf rcu_node structure exiting its RCU read-side
	 * critical section while all CPUs corresponding to that structure
	 * are offline.  This added warning detects bugs in any of these
	 * code paths.
	 *
	 * The rcu_node structure's ->lock is held here, which excludes
	 * the relevant portions the CPU-hotplug code, the grace-period
	 * initialization code, and the rcu_read_unlock() code paths.
	 *
	 * For more detail, please refer to the "Hotplug CPU" section
	 * of RCU's Requirements documentation.
	 */
	if (WARN_ON_ONCE(!rcu_rdp_cpu_online(rdp))) {
		struct rcu_node *rnp1;

		pr_info("%s: grp: %d-%d level: %d ->gp_seq %ld ->completedqs %ld\n",
			__func__, rnp->grplo, rnp->grphi, rnp->level,
			(long)rnp->gp_seq, (long)rnp->completedqs);
		for (rnp1 = rnp; rnp1; rnp1 = rnp1->parent)
			pr_info("%s: %d:%d ->qsmask %#lx ->qsmaskinit %#lx ->qsmaskinitnext %#lx ->rcu_gp_init_mask %#lx\n",
				__func__, rnp1->grplo, rnp1->grphi, rnp1->qsmask, rnp1->qsmaskinit, rnp1->qsmaskinitnext, rnp1->rcu_gp_init_mask);
		pr_info("%s %d: %c online: %ld(%d) offline: %ld(%d)\n",
			__func__, rdp->cpu, ".o"[rcu_rdp_cpu_online(rdp)],
			(long)rdp->rcu_onl_gp_seq, rdp->rcu_onl_gp_flags,
			(long)rdp->rcu_ofl_gp_seq, rdp->rcu_ofl_gp_flags);
		return 1; /* Break things loose after complaining. */
	}

	/*
	 * A CPU running for an extended time within the kernel can
	 * delay RCU grace periods: (1) At age jiffies_to_sched_qs,
	 * set .rcu_urgent_qs, (2) At age 2*jiffies_to_sched_qs, set
	 * both .rcu_need_heavy_qs and .rcu_urgent_qs.  Note that the
	 * unsynchronized assignments to the per-CPU rcu_need_heavy_qs
	 * variable are safe because the assignments are repeated if this
	 * CPU failed to pass through a quiescent state.  This code
	 * also checks .jiffies_resched in case jiffies_to_sched_qs
	 * is set way high.
	 */
	jtsq = READ_ONCE(jiffies_to_sched_qs);
	if (!READ_ONCE(rdp->rcu_need_heavy_qs) &&
	    (time_after(jiffies, rcu_state.gp_start + jtsq * 2) ||
	     time_after(jiffies, rcu_state.jiffies_resched) ||
	     rcu_state.cbovld)) {
		WRITE_ONCE(rdp->rcu_need_heavy_qs, true);
		/* Store rcu_need_heavy_qs before rcu_urgent_qs. */
		smp_store_release(&rdp->rcu_urgent_qs, true);
	} else if (time_after(jiffies, rcu_state.gp_start + jtsq)) {
		WRITE_ONCE(rdp->rcu_urgent_qs, true);
	}

	/*
	 * NO_HZ_FULL CPUs can run in-kernel without rcu_sched_clock_irq!
	 * The above code handles this, but only for straight cond_resched().
	 * And some in-kernel loops check need_resched() before calling
	 * cond_resched(), which defeats the above code for CPUs that are
	 * running in-kernel with scheduling-clock interrupts disabled.
	 * So hit them over the head with the resched_cpu() hammer!
	 */
	if (tick_nohz_full_cpu(rdp->cpu) &&
	    (time_after(jiffies, READ_ONCE(rdp->last_fqs_resched) + jtsq * 3) ||
	     rcu_state.cbovld)) {
		WRITE_ONCE(rdp->rcu_urgent_qs, true);
		resched_cpu(rdp->cpu);
		WRITE_ONCE(rdp->last_fqs_resched, jiffies);
	}

	/*
	 * If more than halfway to RCU CPU stall-warning time, invoke
	 * resched_cpu() more frequently to try to loosen things up a bit.
	 * Also check to see if the CPU is getting hammered with interrupts,
	 * but only once per grace period, just to keep the IPIs down to
	 * a dull roar.
	 */
	if (time_after(jiffies, rcu_state.jiffies_resched)) {
		if (time_after(jiffies,
			       READ_ONCE(rdp->last_fqs_resched) + jtsq)) {
			resched_cpu(rdp->cpu);
			WRITE_ONCE(rdp->last_fqs_resched, jiffies);
		}
		if (IS_ENABLED(CONFIG_IRQ_WORK) &&
		    !rdp->rcu_iw_pending && rdp->rcu_iw_gp_seq != rnp->gp_seq &&
		    (rnp->ffmask & rdp->grpmask)) {
			rdp->rcu_iw_pending = true;
			rdp->rcu_iw_gp_seq = rnp->gp_seq;
			irq_work_queue_on(&rdp->rcu_iw, rdp->cpu);
		}

		if (rcu_cpu_stall_cputime && rdp->snap_record.gp_seq != rdp->gp_seq) {
			int cpu = rdp->cpu;
			struct rcu_snap_record *rsrp;
			struct kernel_cpustat *kcsp;

			kcsp = &kcpustat_cpu(cpu);

			rsrp = &rdp->snap_record;
			rsrp->cputime_irq     = kcpustat_field(kcsp, CPUTIME_IRQ, cpu);
			rsrp->cputime_softirq = kcpustat_field(kcsp, CPUTIME_SOFTIRQ, cpu);
			rsrp->cputime_system  = kcpustat_field(kcsp, CPUTIME_SYSTEM, cpu);
			rsrp->nr_hardirqs = kstat_cpu_irqs_sum(rdp->cpu);
			rsrp->nr_softirqs = kstat_cpu_softirqs_sum(rdp->cpu);
			rsrp->nr_csw = nr_context_switches_cpu(rdp->cpu);
			rsrp->jiffies = jiffies;
			rsrp->gp_seq = rdp->gp_seq;
		}
	}

	return 0;
}

/* Trace-event wrapper function for trace_rcu_future_grace_period.  */
static void trace_rcu_this_gp(struct rcu_node *rnp, struct rcu_data *rdp,
			      unsigned long gp_seq_req, const char *s)
{
	trace_rcu_future_grace_period(rcu_state.name, READ_ONCE(rnp->gp_seq),
				      gp_seq_req, rnp->level,
				      rnp->grplo, rnp->grphi, s);
}

/*
 * rcu_start_this_gp - Request the start of a particular grace period
 * @rnp_start: The leaf node of the CPU from which to start.
 * @rdp: The rcu_data corresponding to the CPU from which to start.
 * @gp_seq_req: The gp_seq of the grace period to start.
 *
 * Start the specified grace period, as needed to handle newly arrived
 * callbacks.  The required future grace periods are recorded in each
 * rcu_node structure's ->gp_seq_needed field.  Returns true if there
 * is reason to awaken the grace-period kthread.
 *
 * The caller must hold the specified rcu_node structure's ->lock, which
 * is why the caller is responsible for waking the grace-period kthread.
 *
 * Returns true if the GP thread needs to be awakened else false.
 */
static bool rcu_start_this_gp(struct rcu_node *rnp_start, struct rcu_data *rdp,
			      unsigned long gp_seq_req)
{
	bool ret = false;
	struct rcu_node *rnp;

	/*
	 * Use funnel locking to either acquire the root rcu_node
	 * structure's lock or bail out if the need for this grace period
	 * has already been recorded -- or if that grace period has in
	 * fact already started.  If there is already a grace period in
	 * progress in a non-leaf node, no recording is needed because the
	 * end of the grace period will scan the leaf rcu_node structures.
	 * Note that rnp_start->lock must not be released.
	 */
	raw_lockdep_assert_held_rcu_node(rnp_start);
	trace_rcu_this_gp(rnp_start, rdp, gp_seq_req, TPS("Startleaf"));
	for (rnp = rnp_start; 1; rnp = rnp->parent) {
		if (rnp != rnp_start)
			raw_spin_lock_rcu_node(rnp);
		if (ULONG_CMP_GE(rnp->gp_seq_needed, gp_seq_req) ||
		    rcu_seq_started(&rnp->gp_seq, gp_seq_req) ||
		    (rnp != rnp_start &&
		     rcu_seq_state(rcu_seq_current(&rnp->gp_seq)))) {
			trace_rcu_this_gp(rnp, rdp, gp_seq_req,
					  TPS("Prestarted"));
			goto unlock_out;
		}
		WRITE_ONCE(rnp->gp_seq_needed, gp_seq_req);
		if (rcu_seq_state(rcu_seq_current(&rnp->gp_seq))) {
			/*
			 * We just marked the leaf or internal node, and a
			 * grace period is in progress, which means that
			 * rcu_gp_cleanup() will see the marking.  Bail to
			 * reduce contention.
			 */
			trace_rcu_this_gp(rnp_start, rdp, gp_seq_req,
					  TPS("Startedleaf"));
			goto unlock_out;
		}
		if (rnp != rnp_start && rnp->parent != NULL)
			raw_spin_unlock_rcu_node(rnp);
		if (!rnp->parent)
			break;  /* At root, and perhaps also leaf. */
	}

	/* If GP already in progress, just leave, otherwise start one. */
	if (rcu_gp_in_progress()) {
		trace_rcu_this_gp(rnp, rdp, gp_seq_req, TPS("Startedleafroot"));
		goto unlock_out;
	}
	trace_rcu_this_gp(rnp, rdp, gp_seq_req, TPS("Startedroot"));
	WRITE_ONCE(rcu_state.gp_flags, rcu_state.gp_flags | RCU_GP_FLAG_INIT);
	WRITE_ONCE(rcu_state.gp_req_activity, jiffies);
	if (!READ_ONCE(rcu_state.gp_kthread)) {
		trace_rcu_this_gp(rnp, rdp, gp_seq_req, TPS("NoGPkthread"));
		goto unlock_out;
	}
	trace_rcu_grace_period(rcu_state.name, data_race(rcu_state.gp_seq), TPS("newreq"));
	ret = true;  /* Caller must wake GP kthread. */
unlock_out:
	/* Push furthest requested GP to leaf node and rcu_data structure. */
	if (ULONG_CMP_LT(gp_seq_req, rnp->gp_seq_needed)) {
		WRITE_ONCE(rnp_start->gp_seq_needed, rnp->gp_seq_needed);
		WRITE_ONCE(rdp->gp_seq_needed, rnp->gp_seq_needed);
	}
	if (rnp != rnp_start)
		raw_spin_unlock_rcu_node(rnp);
	return ret;
}

/*
 * Clean up any old requests for the just-ended grace period.  Also return
 * whether any additional grace periods have been requested.
 */
static bool rcu_future_gp_cleanup(struct rcu_node *rnp)
{
	bool needmore;
	struct rcu_data *rdp = this_cpu_ptr(&rcu_data);

	needmore = ULONG_CMP_LT(rnp->gp_seq, rnp->gp_seq_needed);
	if (!needmore)
		rnp->gp_seq_needed = rnp->gp_seq; /* Avoid counter wrap. */
	trace_rcu_this_gp(rnp, rdp, rnp->gp_seq,
			  needmore ? TPS("CleanupMore") : TPS("Cleanup"));
	return needmore;
}

/*
 * Awaken the grace-period kthread.  Don't do a self-awaken (unless in an
 * interrupt or softirq handler, in which case we just might immediately
 * sleep upon return, resulting in a grace-period hang), and don't bother
 * awakening when there is nothing for the grace-period kthread to do
 * (as in several CPUs raced to awaken, we lost), and finally don't try
 * to awaken a kthread that has not yet been created.  If all those checks
 * are passed, track some debug information and awaken.
 *
 * So why do the self-wakeup when in an interrupt or softirq handler
 * in the grace-period kthread's context?  Because the kthread might have
 * been interrupted just as it was going to sleep, and just after the final
 * pre-sleep check of the awaken condition.  In this case, a wakeup really
 * is required, and is therefore supplied.
 */
static void rcu_gp_kthread_wake(void)
{
	struct task_struct *t = READ_ONCE(rcu_state.gp_kthread);

	if ((current == t && !in_hardirq() && !in_serving_softirq()) ||
	    !READ_ONCE(rcu_state.gp_flags) || !t)
		return;
	WRITE_ONCE(rcu_state.gp_wake_time, jiffies);
	WRITE_ONCE(rcu_state.gp_wake_seq, READ_ONCE(rcu_state.gp_seq));
	swake_up_one(&rcu_state.gp_wq);
}

/*
 * If there is room, assign a ->gp_seq number to any callbacks on this
 * CPU that have not already been assigned.  Also accelerate any callbacks
 * that were previously assigned a ->gp_seq number that has since proven
 * to be too conservative, which can happen if callbacks get assigned a
 * ->gp_seq number while RCU is idle, but with reference to a non-root
 * rcu_node structure.  This function is idempotent, so it does not hurt
 * to call it repeatedly.  Returns an flag saying that we should awaken
 * the RCU grace-period kthread.
 *
 * The caller must hold rnp->lock with interrupts disabled.
 */
static bool rcu_accelerate_cbs(struct rcu_node *rnp, struct rcu_data *rdp)
{
	unsigned long gp_seq_req;
	bool ret = false;

	rcu_lockdep_assert_cblist_protected(rdp);
	raw_lockdep_assert_held_rcu_node(rnp);

	/* If no pending (not yet ready to invoke) callbacks, nothing to do. */
	if (!rcu_segcblist_pend_cbs(&rdp->cblist))
		return false;

	trace_rcu_segcb_stats(&rdp->cblist, TPS("SegCbPreAcc"));

	/*
	 * Callbacks are often registered with incomplete grace-period
	 * information.  Something about the fact that getting exact
	 * information requires acquiring a global lock...  RCU therefore
	 * makes a conservative estimate of the grace period number at which
	 * a given callback will become ready to invoke.	The following
	 * code checks this estimate and improves it when possible, thus
	 * accelerating callback invocation to an earlier grace-period
	 * number.
	 */
	gp_seq_req = rcu_seq_snap(&rcu_state.gp_seq);
	if (rcu_segcblist_accelerate(&rdp->cblist, gp_seq_req))
		ret = rcu_start_this_gp(rnp, rdp, gp_seq_req);

	/* Trace depending on how much we were able to accelerate. */
	if (rcu_segcblist_restempty(&rdp->cblist, RCU_WAIT_TAIL))
		trace_rcu_grace_period(rcu_state.name, gp_seq_req, TPS("AccWaitCB"));
	else
		trace_rcu_grace_period(rcu_state.name, gp_seq_req, TPS("AccReadyCB"));

	trace_rcu_segcb_stats(&rdp->cblist, TPS("SegCbPostAcc"));

	return ret;
}

/*
 * Similar to rcu_accelerate_cbs(), but does not require that the leaf
 * rcu_node structure's ->lock be held.  It consults the cached value
 * of ->gp_seq_needed in the rcu_data structure, and if that indicates
 * that a new grace-period request be made, invokes rcu_accelerate_cbs()
 * while holding the leaf rcu_node structure's ->lock.
 */
static void rcu_accelerate_cbs_unlocked(struct rcu_node *rnp,
					struct rcu_data *rdp)
{
	unsigned long c;
	bool needwake;

	rcu_lockdep_assert_cblist_protected(rdp);
	c = rcu_seq_snap(&rcu_state.gp_seq);
	if (!READ_ONCE(rdp->gpwrap) && ULONG_CMP_GE(rdp->gp_seq_needed, c)) {
		/* Old request still live, so mark recent callbacks. */
		(void)rcu_segcblist_accelerate(&rdp->cblist, c);
		return;
	}
	raw_spin_lock_rcu_node(rnp); /* irqs already disabled. */
	needwake = rcu_accelerate_cbs(rnp, rdp);
	raw_spin_unlock_rcu_node(rnp); /* irqs remain disabled. */
	if (needwake)
		rcu_gp_kthread_wake();
}

/*
 * Move any callbacks whose grace period has completed to the
 * RCU_DONE_TAIL sublist, then compact the remaining sublists and
 * assign ->gp_seq numbers to any callbacks in the RCU_NEXT_TAIL
 * sublist.  This function is idempotent, so it does not hurt to
 * invoke it repeatedly.  As long as it is not invoked -too- often...
 * Returns true if the RCU grace-period kthread needs to be awakened.
 *
 * The caller must hold rnp->lock with interrupts disabled.
 */
static bool rcu_advance_cbs(struct rcu_node *rnp, struct rcu_data *rdp)
{
	rcu_lockdep_assert_cblist_protected(rdp);
	raw_lockdep_assert_held_rcu_node(rnp);

	/* If no pending (not yet ready to invoke) callbacks, nothing to do. */
	if (!rcu_segcblist_pend_cbs(&rdp->cblist))
		return false;

	/*
	 * Find all callbacks whose ->gp_seq numbers indicate that they
	 * are ready to invoke, and put them into the RCU_DONE_TAIL sublist.
	 */
	rcu_segcblist_advance(&rdp->cblist, rnp->gp_seq);

	/* Classify any remaining callbacks. */
	return rcu_accelerate_cbs(rnp, rdp);
}

/*
 * Move and classify callbacks, but only if doing so won't require
 * that the RCU grace-period kthread be awakened.
 */
static void __maybe_unused rcu_advance_cbs_nowake(struct rcu_node *rnp,
						  struct rcu_data *rdp)
{
	rcu_lockdep_assert_cblist_protected(rdp);
	if (!rcu_seq_state(rcu_seq_current(&rnp->gp_seq)) || !raw_spin_trylock_rcu_node(rnp))
		return;
	// The grace period cannot end while we hold the rcu_node lock.
	if (rcu_seq_state(rcu_seq_current(&rnp->gp_seq)))
		WARN_ON_ONCE(rcu_advance_cbs(rnp, rdp));
	raw_spin_unlock_rcu_node(rnp);
}

/*
 * In CONFIG_RCU_STRICT_GRACE_PERIOD=y kernels, attempt to generate a
 * quiescent state.  This is intended to be invoked when the CPU notices
 * a new grace period.
 */
static void rcu_strict_gp_check_qs(void)
{
	if (IS_ENABLED(CONFIG_RCU_STRICT_GRACE_PERIOD)) {
		rcu_read_lock();
		rcu_read_unlock();
	}
}

/*
 * Update CPU-local rcu_data state to record the beginnings and ends of
 * grace periods.  The caller must hold the ->lock of the leaf rcu_node
 * structure corresponding to the current CPU, and must have irqs disabled.
 * Returns true if the grace-period kthread needs to be awakened.
 */
static bool __note_gp_changes(struct rcu_node *rnp, struct rcu_data *rdp)
{
	bool ret = false;
	bool need_qs;
	const bool offloaded = rcu_rdp_is_offloaded(rdp);

	raw_lockdep_assert_held_rcu_node(rnp);

	if (rdp->gp_seq == rnp->gp_seq)
		return false; /* Nothing to do. */

	/* Handle the ends of any preceding grace periods first. */
	if (rcu_seq_completed_gp(rdp->gp_seq, rnp->gp_seq) ||
	    unlikely(READ_ONCE(rdp->gpwrap))) {
		if (!offloaded)
			ret = rcu_advance_cbs(rnp, rdp); /* Advance CBs. */
		rdp->core_needs_qs = false;
		trace_rcu_grace_period(rcu_state.name, rdp->gp_seq, TPS("cpuend"));
	} else {
		if (!offloaded)
			ret = rcu_accelerate_cbs(rnp, rdp); /* Recent CBs. */
		if (rdp->core_needs_qs)
			rdp->core_needs_qs = !!(rnp->qsmask & rdp->grpmask);
	}

	/* Now handle the beginnings of any new-to-this-CPU grace periods. */
	if (rcu_seq_new_gp(rdp->gp_seq, rnp->gp_seq) ||
	    unlikely(READ_ONCE(rdp->gpwrap))) {
		/*
		 * If the current grace period is waiting for this CPU,
		 * set up to detect a quiescent state, otherwise don't
		 * go looking for one.
		 */
		trace_rcu_grace_period(rcu_state.name, rnp->gp_seq, TPS("cpustart"));
		need_qs = !!(rnp->qsmask & rdp->grpmask);
		rdp->cpu_no_qs.b.norm = need_qs;
		rdp->core_needs_qs = need_qs;
		zero_cpu_stall_ticks(rdp);
	}
	rdp->gp_seq = rnp->gp_seq;  /* Remember new grace-period state. */
	if (ULONG_CMP_LT(rdp->gp_seq_needed, rnp->gp_seq_needed) || rdp->gpwrap)
		WRITE_ONCE(rdp->gp_seq_needed, rnp->gp_seq_needed);
	if (IS_ENABLED(CONFIG_PROVE_RCU) && READ_ONCE(rdp->gpwrap))
		WRITE_ONCE(rdp->last_sched_clock, jiffies);
	WRITE_ONCE(rdp->gpwrap, false);
	rcu_gpnum_ovf(rnp, rdp);
	return ret;
}

static void note_gp_changes(struct rcu_data *rdp)
{
	unsigned long flags;
	bool needwake;
	struct rcu_node *rnp;

	local_irq_save(flags);
	rnp = rdp->mynode;
	if ((rdp->gp_seq == rcu_seq_current(&rnp->gp_seq) &&
	     !unlikely(READ_ONCE(rdp->gpwrap))) || /* w/out lock. */
	    !raw_spin_trylock_rcu_node(rnp)) { /* irqs already off, so later. */
		local_irq_restore(flags);
		return;
	}
	needwake = __note_gp_changes(rnp, rdp);
	raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
	rcu_strict_gp_check_qs();
	if (needwake)
		rcu_gp_kthread_wake();
}

static atomic_t *rcu_gp_slow_suppress;

/* Register a counter to suppress debugging grace-period delays. */
void rcu_gp_slow_register(atomic_t *rgssp)
{
	WARN_ON_ONCE(rcu_gp_slow_suppress);

	WRITE_ONCE(rcu_gp_slow_suppress, rgssp);
}
EXPORT_SYMBOL_GPL(rcu_gp_slow_register);

/* Unregister a counter, with NULL for not caring which. */
void rcu_gp_slow_unregister(atomic_t *rgssp)
{
	WARN_ON_ONCE(rgssp && rgssp != rcu_gp_slow_suppress);

	WRITE_ONCE(rcu_gp_slow_suppress, NULL);
}
EXPORT_SYMBOL_GPL(rcu_gp_slow_unregister);

static bool rcu_gp_slow_is_suppressed(void)
{
	atomic_t *rgssp = READ_ONCE(rcu_gp_slow_suppress);

	return rgssp && atomic_read(rgssp);
}

static void rcu_gp_slow(int delay)
{
	if (!rcu_gp_slow_is_suppressed() && delay > 0 &&
	    !(rcu_seq_ctr(rcu_state.gp_seq) % (rcu_num_nodes * PER_RCU_NODE_PERIOD * delay)))
		schedule_timeout_idle(delay);
}

static unsigned long sleep_duration;

/* Allow rcutorture to stall the grace-period kthread. */
void rcu_gp_set_torture_wait(int duration)
{
	if (IS_ENABLED(CONFIG_RCU_TORTURE_TEST) && duration > 0)
		WRITE_ONCE(sleep_duration, duration);
}
EXPORT_SYMBOL_GPL(rcu_gp_set_torture_wait);

/* Actually implement the aforementioned wait. */
static void rcu_gp_torture_wait(void)
{
	unsigned long duration;

	if (!IS_ENABLED(CONFIG_RCU_TORTURE_TEST))
		return;
	duration = xchg(&sleep_duration, 0UL);
	if (duration > 0) {
		pr_alert("%s: Waiting %lu jiffies\n", __func__, duration);
		schedule_timeout_idle(duration);
		pr_alert("%s: Wait complete\n", __func__);
	}
}

/*
 * Handler for on_each_cpu() to invoke the target CPU's RCU core
 * processing.
 */
static void rcu_strict_gp_boundary(void *unused)
{
	invoke_rcu_core();
}

// Make the polled API aware of the beginning of a grace period.
static void rcu_poll_gp_seq_start(unsigned long *snap)
{
	struct rcu_node *rnp = rcu_get_root();

	if (rcu_scheduler_active != RCU_SCHEDULER_INACTIVE)
		raw_lockdep_assert_held_rcu_node(rnp);

	// If RCU was idle, note beginning of GP.
	if (!rcu_seq_state(rcu_state.gp_seq_polled))
		rcu_seq_start(&rcu_state.gp_seq_polled);

	// Either way, record current state.
	*snap = rcu_state.gp_seq_polled;
}

// Make the polled API aware of the end of a grace period.
static void rcu_poll_gp_seq_end(unsigned long *snap)
{
	struct rcu_node *rnp = rcu_get_root();

	if (rcu_scheduler_active != RCU_SCHEDULER_INACTIVE)
		raw_lockdep_assert_held_rcu_node(rnp);

	// If the previously noted GP is still in effect, record the
	// end of that GP.  Either way, zero counter to avoid counter-wrap
	// problems.
	if (*snap && *snap == rcu_state.gp_seq_polled) {
		rcu_seq_end(&rcu_state.gp_seq_polled);
		rcu_state.gp_seq_polled_snap = 0;
		rcu_state.gp_seq_polled_exp_snap = 0;
	} else {
		*snap = 0;
	}
}

// Make the polled API aware of the beginning of a grace period, but
// where caller does not hold the root rcu_node structure's lock.
static void rcu_poll_gp_seq_start_unlocked(unsigned long *snap)
{
	unsigned long flags;
	struct rcu_node *rnp = rcu_get_root();

	if (rcu_init_invoked()) {
		if (rcu_scheduler_active != RCU_SCHEDULER_INACTIVE)
			lockdep_assert_irqs_enabled();
		raw_spin_lock_irqsave_rcu_node(rnp, flags);
	}
	rcu_poll_gp_seq_start(snap);
	if (rcu_init_invoked())
		raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
}

// Make the polled API aware of the end of a grace period, but where
// caller does not hold the root rcu_node structure's lock.
static void rcu_poll_gp_seq_end_unlocked(unsigned long *snap)
{
	unsigned long flags;
	struct rcu_node *rnp = rcu_get_root();

	if (rcu_init_invoked()) {
		if (rcu_scheduler_active != RCU_SCHEDULER_INACTIVE)
			lockdep_assert_irqs_enabled();
		raw_spin_lock_irqsave_rcu_node(rnp, flags);
	}
	rcu_poll_gp_seq_end(snap);
	if (rcu_init_invoked())
		raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
}

/*
 * Initialize a new grace period.  Return false if no grace period required.
 */
static noinline_for_stack bool rcu_gp_init(void)
{
	unsigned long flags;
	unsigned long oldmask;
	unsigned long mask;
	struct rcu_data *rdp;
	struct rcu_node *rnp = rcu_get_root();

	WRITE_ONCE(rcu_state.gp_activity, jiffies);
	raw_spin_lock_irq_rcu_node(rnp);
	if (!READ_ONCE(rcu_state.gp_flags)) {
		/* Spurious wakeup, tell caller to go back to sleep.  */
		raw_spin_unlock_irq_rcu_node(rnp);
		return false;
	}
	WRITE_ONCE(rcu_state.gp_flags, 0); /* Clear all flags: New GP. */

	if (WARN_ON_ONCE(rcu_gp_in_progress())) {
		/*
		 * Grace period already in progress, don't start another.
		 * Not supposed to be able to happen.
		 */
		raw_spin_unlock_irq_rcu_node(rnp);
		return false;
	}

	/* Advance to a new grace period and initialize state. */
	record_gp_stall_check_time();
	/* Record GP times before starting GP, hence rcu_seq_start(). */
	rcu_seq_start(&rcu_state.gp_seq);
	ASSERT_EXCLUSIVE_WRITER(rcu_state.gp_seq);
	trace_rcu_grace_period(rcu_state.name, rcu_state.gp_seq, TPS("start"));
	rcu_poll_gp_seq_start(&rcu_state.gp_seq_polled_snap);
	raw_spin_unlock_irq_rcu_node(rnp);

	/*
	 * Apply per-leaf buffered online and offline operations to
	 * the rcu_node tree. Note that this new grace period need not
	 * wait for subsequent online CPUs, and that RCU hooks in the CPU
	 * offlining path, when combined with checks in this function,
	 * will handle CPUs that are currently going offline or that will
	 * go offline later.  Please also refer to "Hotplug CPU" section
	 * of RCU's Requirements documentation.
	 */
	WRITE_ONCE(rcu_state.gp_state, RCU_GP_ONOFF);
	/* Exclude CPU hotplug operations. */
	rcu_for_each_leaf_node(rnp) {
		local_irq_save(flags);
		arch_spin_lock(&rcu_state.ofl_lock);
		raw_spin_lock_rcu_node(rnp);
		if (rnp->qsmaskinit == rnp->qsmaskinitnext &&
		    !rnp->wait_blkd_tasks) {
			/* Nothing to do on this leaf rcu_node structure. */
			raw_spin_unlock_rcu_node(rnp);
			arch_spin_unlock(&rcu_state.ofl_lock);
			local_irq_restore(flags);
			continue;
		}

		/* Record old state, apply changes to ->qsmaskinit field. */
		oldmask = rnp->qsmaskinit;
		rnp->qsmaskinit = rnp->qsmaskinitnext;

		/* If zero-ness of ->qsmaskinit changed, propagate up tree. */
		if (!oldmask != !rnp->qsmaskinit) {
			if (!oldmask) { /* First online CPU for rcu_node. */
				if (!rnp->wait_blkd_tasks) /* Ever offline? */
					rcu_init_new_rnp(rnp);
			} else if (rcu_preempt_has_tasks(rnp)) {
				rnp->wait_blkd_tasks = true; /* blocked tasks */
			} else { /* Last offline CPU and can propagate. */
				rcu_cleanup_dead_rnp(rnp);
			}
		}

		/*
		 * If all waited-on tasks from prior grace period are
		 * done, and if all this rcu_node structure's CPUs are
		 * still offline, propagate up the rcu_node tree and
		 * clear ->wait_blkd_tasks.  Otherwise, if one of this
		 * rcu_node structure's CPUs has since come back online,
		 * simply clear ->wait_blkd_tasks.
		 */
		if (rnp->wait_blkd_tasks &&
		    (!rcu_preempt_has_tasks(rnp) || rnp->qsmaskinit)) {
			rnp->wait_blkd_tasks = false;
			if (!rnp->qsmaskinit)
				rcu_cleanup_dead_rnp(rnp);
		}

		raw_spin_unlock_rcu_node(rnp);
		arch_spin_unlock(&rcu_state.ofl_lock);
		local_irq_restore(flags);
	}
	rcu_gp_slow(gp_preinit_delay); /* Races with CPU hotplug. */

	/*
	 * Set the quiescent-state-needed bits in all the rcu_node
	 * structures for all currently online CPUs in breadth-first
	 * order, starting from the root rcu_node structure, relying on the
	 * layout of the tree within the rcu_state.node[] array.  Note that
	 * other CPUs will access only the leaves of the hierarchy, thus
	 * seeing that no grace period is in progress, at least until the
	 * corresponding leaf node has been initialized.
	 *
	 * The grace period cannot complete until the initialization
	 * process finishes, because this kthread handles both.
	 */
	WRITE_ONCE(rcu_state.gp_state, RCU_GP_INIT);
	rcu_for_each_node_breadth_first(rnp) {
		rcu_gp_slow(gp_init_delay);
		raw_spin_lock_irqsave_rcu_node(rnp, flags);
		rdp = this_cpu_ptr(&rcu_data);
		rcu_preempt_check_blocked_tasks(rnp);
		rnp->qsmask = rnp->qsmaskinit;
		WRITE_ONCE(rnp->gp_seq, rcu_state.gp_seq);
		if (rnp == rdp->mynode)
			(void)__note_gp_changes(rnp, rdp);
		rcu_preempt_boost_start_gp(rnp);
		trace_rcu_grace_period_init(rcu_state.name, rnp->gp_seq,
					    rnp->level, rnp->grplo,
					    rnp->grphi, rnp->qsmask);
		/* Quiescent states for tasks on any now-offline CPUs. */
		mask = rnp->qsmask & ~rnp->qsmaskinitnext;
		rnp->rcu_gp_init_mask = mask;
		if ((mask || rnp->wait_blkd_tasks) && rcu_is_leaf_node(rnp))
			rcu_report_qs_rnp(mask, rnp, rnp->gp_seq, flags);
		else
			raw_spin_unlock_irq_rcu_node(rnp);
		cond_resched_tasks_rcu_qs();
		WRITE_ONCE(rcu_state.gp_activity, jiffies);
	}

	// If strict, make all CPUs aware of new grace period.
	if (IS_ENABLED(CONFIG_RCU_STRICT_GRACE_PERIOD))
		on_each_cpu(rcu_strict_gp_boundary, NULL, 0);

	return true;
}

/*
 * Helper function for swait_event_idle_exclusive() wakeup at force-quiescent-state
 * time.
 */
static bool rcu_gp_fqs_check_wake(int *gfp)
{
	struct rcu_node *rnp = rcu_get_root();

	// If under overload conditions, force an immediate FQS scan.
	if (*gfp & RCU_GP_FLAG_OVLD)
		return true;

	// Someone like call_rcu() requested a force-quiescent-state scan.
	*gfp = READ_ONCE(rcu_state.gp_flags);
	if (*gfp & RCU_GP_FLAG_FQS)
		return true;

	// The current grace period has completed.
	if (!READ_ONCE(rnp->qsmask) && !rcu_preempt_blocked_readers_cgp(rnp))
		return true;

	return false;
}

/*
 * Do one round of quiescent-state forcing.
 */
static void rcu_gp_fqs(bool first_time)
{
	struct rcu_node *rnp = rcu_get_root();

	WRITE_ONCE(rcu_state.gp_activity, jiffies);
	WRITE_ONCE(rcu_state.n_force_qs, rcu_state.n_force_qs + 1);
	if (first_time) {
		/* Collect dyntick-idle snapshots. */
		force_qs_rnp(dyntick_save_progress_counter);
	} else {
		/* Handle dyntick-idle and offline CPUs. */
		force_qs_rnp(rcu_implicit_dynticks_qs);
	}
	/* Clear flag to prevent immediate re-entry. */
	if (READ_ONCE(rcu_state.gp_flags) & RCU_GP_FLAG_FQS) {
		raw_spin_lock_irq_rcu_node(rnp);
		WRITE_ONCE(rcu_state.gp_flags,
			   READ_ONCE(rcu_state.gp_flags) & ~RCU_GP_FLAG_FQS);
		raw_spin_unlock_irq_rcu_node(rnp);
	}
}

/*
 * Loop doing repeated quiescent-state forcing until the grace period ends.
 */
static noinline_for_stack void rcu_gp_fqs_loop(void)
{
	bool first_gp_fqs = true;
	int gf = 0;
	unsigned long j;
	int ret;
	struct rcu_node *rnp = rcu_get_root();

	j = READ_ONCE(jiffies_till_first_fqs);
	if (rcu_state.cbovld)
		gf = RCU_GP_FLAG_OVLD;
	ret = 0;
	for (;;) {
		if (rcu_state.cbovld) {
			j = (j + 2) / 3;
			if (j <= 0)
				j = 1;
		}
		if (!ret || time_before(jiffies + j, rcu_state.jiffies_force_qs)) {
			WRITE_ONCE(rcu_state.jiffies_force_qs, jiffies + j);
			/*
			 * jiffies_force_qs before RCU_GP_WAIT_FQS state
			 * update; required for stall checks.
			 */
			smp_wmb();
			WRITE_ONCE(rcu_state.jiffies_kick_kthreads,
				   jiffies + (j ? 3 * j : 2));
		}
		trace_rcu_grace_period(rcu_state.name, rcu_state.gp_seq,
				       TPS("fqswait"));
		WRITE_ONCE(rcu_state.gp_state, RCU_GP_WAIT_FQS);
		(void)swait_event_idle_timeout_exclusive(rcu_state.gp_wq,
				 rcu_gp_fqs_check_wake(&gf), j);
		rcu_gp_torture_wait();
		WRITE_ONCE(rcu_state.gp_state, RCU_GP_DOING_FQS);
		/* Locking provides needed memory barriers. */
		/*
		 * Exit the loop if the root rcu_node structure indicates that the grace period
		 * has ended, leave the loop.  The rcu_preempt_blocked_readers_cgp(rnp) check
		 * is required only for single-node rcu_node trees because readers blocking
		 * the current grace period are queued only on leaf rcu_node structures.
		 * For multi-node trees, checking the root node's ->qsmask suffices, because a
		 * given root node's ->qsmask bit is cleared only when all CPUs and tasks from
		 * the corresponding leaf nodes have passed through their quiescent state.
		 */
		if (!READ_ONCE(rnp->qsmask) &&
		    !rcu_preempt_blocked_readers_cgp(rnp))
			break;
		/* If time for quiescent-state forcing, do it. */
		if (!time_after(rcu_state.jiffies_force_qs, jiffies) ||
		    (gf & (RCU_GP_FLAG_FQS | RCU_GP_FLAG_OVLD))) {
			trace_rcu_grace_period(rcu_state.name, rcu_state.gp_seq,
					       TPS("fqsstart"));
			rcu_gp_fqs(first_gp_fqs);
			gf = 0;
			if (first_gp_fqs) {
				first_gp_fqs = false;
				gf = rcu_state.cbovld ? RCU_GP_FLAG_OVLD : 0;
			}
			trace_rcu_grace_period(rcu_state.name, rcu_state.gp_seq,
					       TPS("fqsend"));
			cond_resched_tasks_rcu_qs();
			WRITE_ONCE(rcu_state.gp_activity, jiffies);
			ret = 0; /* Force full wait till next FQS. */
			j = READ_ONCE(jiffies_till_next_fqs);
		} else {
			/* Deal with stray signal. */
			cond_resched_tasks_rcu_qs();
			WRITE_ONCE(rcu_state.gp_activity, jiffies);
			WARN_ON(signal_pending(current));
			trace_rcu_grace_period(rcu_state.name, rcu_state.gp_seq,
					       TPS("fqswaitsig"));
			ret = 1; /* Keep old FQS timing. */
			j = jiffies;
			if (time_after(jiffies, rcu_state.jiffies_force_qs))
				j = 1;
			else
				j = rcu_state.jiffies_force_qs - j;
			gf = 0;
		}
	}
}

/*
 * Clean up after the old grace period.
 */
static noinline void rcu_gp_cleanup(void)
{
	int cpu;
	bool needgp = false;
	unsigned long gp_duration;
	unsigned long new_gp_seq;
	bool offloaded;
	struct rcu_data *rdp;
	struct rcu_node *rnp = rcu_get_root();
	struct swait_queue_head *sq;

	WRITE_ONCE(rcu_state.gp_activity, jiffies);
	raw_spin_lock_irq_rcu_node(rnp);
	rcu_state.gp_end = jiffies;
	gp_duration = rcu_state.gp_end - rcu_state.gp_start;
	if (gp_duration > rcu_state.gp_max)
		rcu_state.gp_max = gp_duration;

	/*
	 * We know the grace period is complete, but to everyone else
	 * it appears to still be ongoing.  But it is also the case
	 * that to everyone else it looks like there is nothing that
	 * they can do to advance the grace period.  It is therefore
	 * safe for us to drop the lock in order to mark the grace
	 * period as completed in all of the rcu_node structures.
	 */
	rcu_poll_gp_seq_end(&rcu_state.gp_seq_polled_snap);
	raw_spin_unlock_irq_rcu_node(rnp);

	/*
	 * Propagate new ->gp_seq value to rcu_node structures so that
	 * other CPUs don't have to wait until the start of the next grace
	 * period to process their callbacks.  This also avoids some nasty
	 * RCU grace-period initialization races by forcing the end of
	 * the current grace period to be completely recorded in all of
	 * the rcu_node structures before the beginning of the next grace
	 * period is recorded in any of the rcu_node structures.
	 */
	new_gp_seq = rcu_state.gp_seq;
	rcu_seq_end(&new_gp_seq);
	rcu_for_each_node_breadth_first(rnp) {
		raw_spin_lock_irq_rcu_node(rnp);
		if (WARN_ON_ONCE(rcu_preempt_blocked_readers_cgp(rnp)))
			dump_blkd_tasks(rnp, 10);
		WARN_ON_ONCE(rnp->qsmask);
		WRITE_ONCE(rnp->gp_seq, new_gp_seq);
		if (!rnp->parent)
			smp_mb(); // Order against failing poll_state_synchronize_rcu_full().
		rdp = this_cpu_ptr(&rcu_data);
		if (rnp == rdp->mynode)
			needgp = __note_gp_changes(rnp, rdp) || needgp;
		/* smp_mb() provided by prior unlock-lock pair. */
		needgp = rcu_future_gp_cleanup(rnp) || needgp;
		// Reset overload indication for CPUs no longer overloaded
		if (rcu_is_leaf_node(rnp))
			for_each_leaf_node_cpu_mask(rnp, cpu, rnp->cbovldmask) {
				rdp = per_cpu_ptr(&rcu_data, cpu);
				check_cb_ovld_locked(rdp, rnp);
			}
		sq = rcu_nocb_gp_get(rnp);
		raw_spin_unlock_irq_rcu_node(rnp);
		rcu_nocb_gp_cleanup(sq);
		cond_resched_tasks_rcu_qs();
		WRITE_ONCE(rcu_state.gp_activity, jiffies);
		rcu_gp_slow(gp_cleanup_delay);
	}
	rnp = rcu_get_root();
	raw_spin_lock_irq_rcu_node(rnp); /* GP before ->gp_seq update. */

	/* Declare grace period done, trace first to use old GP number. */
	trace_rcu_grace_period(rcu_state.name, rcu_state.gp_seq, TPS("end"));
	rcu_seq_end(&rcu_state.gp_seq);
	ASSERT_EXCLUSIVE_WRITER(rcu_state.gp_seq);
	WRITE_ONCE(rcu_state.gp_state, RCU_GP_IDLE);
	/* Check for GP requests since above loop. */
	rdp = this_cpu_ptr(&rcu_data);
	if (!needgp && ULONG_CMP_LT(rnp->gp_seq, rnp->gp_seq_needed)) {
		trace_rcu_this_gp(rnp, rdp, rnp->gp_seq_needed,
				  TPS("CleanupMore"));
		needgp = true;
	}
	/* Advance CBs to reduce false positives below. */
	offloaded = rcu_rdp_is_offloaded(rdp);
	if ((offloaded || !rcu_accelerate_cbs(rnp, rdp)) && needgp) {

		// We get here if a grace period was needed (“needgp”)
		// and the above call to rcu_accelerate_cbs() did not set
		// the RCU_GP_FLAG_INIT bit in ->gp_state (which records
		// the need for another grace period).  The purpose
		// of the “offloaded” check is to avoid invoking
		// rcu_accelerate_cbs() on an offloaded CPU because we do not
		// hold the ->nocb_lock needed to safely access an offloaded
		// ->cblist.  We do not want to acquire that lock because
		// it can be heavily contended during callback floods.

		WRITE_ONCE(rcu_state.gp_flags, RCU_GP_FLAG_INIT);
		WRITE_ONCE(rcu_state.gp_req_activity, jiffies);
		trace_rcu_grace_period(rcu_state.name, rcu_state.gp_seq, TPS("newreq"));
	} else {

		// We get here either if there is no need for an
		// additional grace period or if rcu_accelerate_cbs() has
		// already set the RCU_GP_FLAG_INIT bit in ->gp_flags. 
		// So all we need to do is to clear all of the other
		// ->gp_flags bits.

		WRITE_ONCE(rcu_state.gp_flags, rcu_state.gp_flags & RCU_GP_FLAG_INIT);
	}
	raw_spin_unlock_irq_rcu_node(rnp);

	// If strict, make all CPUs aware of the end of the old grace period.
	if (IS_ENABLED(CONFIG_RCU_STRICT_GRACE_PERIOD))
		on_each_cpu(rcu_strict_gp_boundary, NULL, 0);
}

/*
 * Body of kthread that handles grace periods.
 */
static int __noreturn rcu_gp_kthread(void *unused)
{
	rcu_bind_gp_kthread();
	for (;;) {

		/* Handle grace-period start. */
		for (;;) {
			trace_rcu_grace_period(rcu_state.name, rcu_state.gp_seq,
					       TPS("reqwait"));
			WRITE_ONCE(rcu_state.gp_state, RCU_GP_WAIT_GPS);
			swait_event_idle_exclusive(rcu_state.gp_wq,
					 READ_ONCE(rcu_state.gp_flags) &
					 RCU_GP_FLAG_INIT);
			rcu_gp_torture_wait();
			WRITE_ONCE(rcu_state.gp_state, RCU_GP_DONE_GPS);
			/* Locking provides needed memory barrier. */
			if (rcu_gp_init())
				break;
			cond_resched_tasks_rcu_qs();
			WRITE_ONCE(rcu_state.gp_activity, jiffies);
			WARN_ON(signal_pending(current));
			trace_rcu_grace_period(rcu_state.name, rcu_state.gp_seq,
					       TPS("reqwaitsig"));
		}

		/* Handle quiescent-state forcing. */
		rcu_gp_fqs_loop();

		/* Handle grace-period end. */
		WRITE_ONCE(rcu_state.gp_state, RCU_GP_CLEANUP);
		rcu_gp_cleanup();
		WRITE_ONCE(rcu_state.gp_state, RCU_GP_CLEANED);
	}
}

/*
 * Report a full set of quiescent states to the rcu_state data structure.
 * Invoke rcu_gp_kthread_wake() to awaken the grace-period kthread if
 * another grace period is required.  Whether we wake the grace-period
 * kthread or it awakens itself for the next round of quiescent-state
 * forcing, that kthread will clean up after the just-completed grace
 * period.  Note that the caller must hold rnp->lock, which is released
 * before return.
 */
static void rcu_report_qs_rsp(unsigned long flags)
	__releases(rcu_get_root()->lock)
{
	raw_lockdep_assert_held_rcu_node(rcu_get_root());
	WARN_ON_ONCE(!rcu_gp_in_progress());
	WRITE_ONCE(rcu_state.gp_flags,
		   READ_ONCE(rcu_state.gp_flags) | RCU_GP_FLAG_FQS);
	raw_spin_unlock_irqrestore_rcu_node(rcu_get_root(), flags);
	rcu_gp_kthread_wake();
}

/*
 * Similar to rcu_report_qs_rdp(), for which it is a helper function.
 * Allows quiescent states for a group of CPUs to be reported at one go
 * to the specified rcu_node structure, though all the CPUs in the group
 * must be represented by the same rcu_node structure (which need not be a
 * leaf rcu_node structure, though it often will be).  The gps parameter
 * is the grace-period snapshot, which means that the quiescent states
 * are valid only if rnp->gp_seq is equal to gps.  That structure's lock
 * must be held upon entry, and it is released before return.
 *
 * As a special case, if mask is zero, the bit-already-cleared check is
 * disabled.  This allows propagating quiescent state due to resumed tasks
 * during grace-period initialization.
 */
static void rcu_report_qs_rnp(unsigned long mask, struct rcu_node *rnp,
			      unsigned long gps, unsigned long flags)
	__releases(rnp->lock)
{
	unsigned long oldmask = 0;
	struct rcu_node *rnp_c;

	raw_lockdep_assert_held_rcu_node(rnp);

	/* Walk up the rcu_node hierarchy. */
	for (;;) {
		if ((!(rnp->qsmask & mask) && mask) || rnp->gp_seq != gps) {

			/*
			 * Our bit has already been cleared, or the
			 * relevant grace period is already over, so done.
			 */
			raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
			return;
		}
		WARN_ON_ONCE(oldmask); /* Any child must be all zeroed! */
		WARN_ON_ONCE(!rcu_is_leaf_node(rnp) &&
			     rcu_preempt_blocked_readers_cgp(rnp));
		WRITE_ONCE(rnp->qsmask, rnp->qsmask & ~mask);
		trace_rcu_quiescent_state_report(rcu_state.name, rnp->gp_seq,
						 mask, rnp->qsmask, rnp->level,
						 rnp->grplo, rnp->grphi,
						 !!rnp->gp_tasks);
		if (rnp->qsmask != 0 || rcu_preempt_blocked_readers_cgp(rnp)) {

			/* Other bits still set at this level, so done. */
			raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
			return;
		}
		rnp->completedqs = rnp->gp_seq;
		mask = rnp->grpmask;
		if (rnp->parent == NULL) {

			/* No more levels.  Exit loop holding root lock. */

			break;
		}
		raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
		rnp_c = rnp;
		rnp = rnp->parent;
		raw_spin_lock_irqsave_rcu_node(rnp, flags);
		oldmask = READ_ONCE(rnp_c->qsmask);
	}

	/*
	 * Get here if we are the last CPU to pass through a quiescent
	 * state for this grace period.  Invoke rcu_report_qs_rsp()
	 * to clean up and start the next grace period if one is needed.
	 */
	rcu_report_qs_rsp(flags); /* releases rnp->lock. */
}

/*
 * Record a quiescent state for all tasks that were previously queued
 * on the specified rcu_node structure and that were blocking the current
 * RCU grace period.  The caller must hold the corresponding rnp->lock with
 * irqs disabled, and this lock is released upon return, but irqs remain
 * disabled.
 */
static void __maybe_unused
rcu_report_unblock_qs_rnp(struct rcu_node *rnp, unsigned long flags)
	__releases(rnp->lock)
{
	unsigned long gps;
	unsigned long mask;
	struct rcu_node *rnp_p;

	raw_lockdep_assert_held_rcu_node(rnp);
	if (WARN_ON_ONCE(!IS_ENABLED(CONFIG_PREEMPT_RCU)) ||
	    WARN_ON_ONCE(rcu_preempt_blocked_readers_cgp(rnp)) ||
	    rnp->qsmask != 0) {
		raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
		return;  /* Still need more quiescent states! */
	}

	rnp->completedqs = rnp->gp_seq;
	rnp_p = rnp->parent;
	if (rnp_p == NULL) {
		/*
		 * Only one rcu_node structure in the tree, so don't
		 * try to report up to its nonexistent parent!
		 */
		rcu_report_qs_rsp(flags);
		return;
	}

	/* Report up the rest of the hierarchy, tracking current ->gp_seq. */
	gps = rnp->gp_seq;
	mask = rnp->grpmask;
	raw_spin_unlock_rcu_node(rnp);	/* irqs remain disabled. */
	raw_spin_lock_rcu_node(rnp_p);	/* irqs already disabled. */
	rcu_report_qs_rnp(mask, rnp_p, gps, flags);
}

/*
 * Record a quiescent state for the specified CPU to that CPU's rcu_data
 * structure.  This must be called from the specified CPU.
 */
static void
rcu_report_qs_rdp(struct rcu_data *rdp)
{
	unsigned long flags;
	unsigned long mask;
	bool needacc = false;
	struct rcu_node *rnp;

	WARN_ON_ONCE(rdp->cpu != smp_processor_id());
	rnp = rdp->mynode;
	raw_spin_lock_irqsave_rcu_node(rnp, flags);
	if (rdp->cpu_no_qs.b.norm || rdp->gp_seq != rnp->gp_seq ||
	    rdp->gpwrap) {

		/*
		 * The grace period in which this quiescent state was
		 * recorded has ended, so don't report it upwards.
		 * We will instead need a new quiescent state that lies
		 * within the current grace period.
		 */
		rdp->cpu_no_qs.b.norm = true;	/* need qs for new gp. */
		raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
		return;
	}
	mask = rdp->grpmask;
	rdp->core_needs_qs = false;
	if ((rnp->qsmask & mask) == 0) {
		raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
	} else {
		/*
		 * This GP can't end until cpu checks in, so all of our
		 * callbacks can be processed during the next GP.
		 *
		 * NOCB kthreads have their own way to deal with that...
		 */
		if (!rcu_rdp_is_offloaded(rdp)) {
			/*
			 * The current GP has not yet ended, so it
			 * should not be possible for rcu_accelerate_cbs()
			 * to return true.  So complain, but don't awaken.
			 */
			WARN_ON_ONCE(rcu_accelerate_cbs(rnp, rdp));
		} else if (!rcu_segcblist_completely_offloaded(&rdp->cblist)) {
			/*
			 * ...but NOCB kthreads may miss or delay callbacks acceleration
			 * if in the middle of a (de-)offloading process.
			 */
			needacc = true;
		}

		rcu_disable_urgency_upon_qs(rdp);
		rcu_report_qs_rnp(mask, rnp, rnp->gp_seq, flags);
		/* ^^^ Released rnp->lock */

		if (needacc) {
			rcu_nocb_lock_irqsave(rdp, flags);
			rcu_accelerate_cbs_unlocked(rnp, rdp);
			rcu_nocb_unlock_irqrestore(rdp, flags);
		}
	}
}

/*
 * Check to see if there is a new grace period of which this CPU
 * is not yet aware, and if so, set up local rcu_data state for it.
 * Otherwise, see if this CPU has just passed through its first
 * quiescent state for this grace period, and record that fact if so.
 */
static void
rcu_check_quiescent_state(struct rcu_data *rdp)
{
	/* Check for grace-period ends and beginnings. */
	note_gp_changes(rdp);

	/*
	 * Does this CPU still need to do its part for current grace period?
	 * If no, return and let the other CPUs do their part as well.
	 */
	if (!rdp->core_needs_qs)
		return;

	/*
	 * Was there a quiescent state since the beginning of the grace
	 * period? If no, then exit and wait for the next call.
	 */
	if (rdp->cpu_no_qs.b.norm)
		return;

	/*
	 * Tell RCU we are done (but rcu_report_qs_rdp() will be the
	 * judge of that).
	 */
	rcu_report_qs_rdp(rdp);
}

/* Return true if callback-invocation time limit exceeded. */
static bool rcu_do_batch_check_time(long count, long tlimit,
				    bool jlimit_check, unsigned long jlimit)
{
	// Invoke local_clock() only once per 32 consecutive callbacks.
	return unlikely(tlimit) &&
	       (!likely(count & 31) ||
		(IS_ENABLED(CONFIG_RCU_DOUBLE_CHECK_CB_TIME) &&
		 jlimit_check && time_after(jiffies, jlimit))) &&
	       local_clock() >= tlimit;
}

/*
 * Invoke any RCU callbacks that have made it to the end of their grace
 * period.  Throttle as specified by rdp->blimit.
 */
static void rcu_do_batch(struct rcu_data *rdp)
{
	long bl;
	long count = 0;
	int div;
	bool __maybe_unused empty;
	unsigned long flags;
	unsigned long jlimit;
	bool jlimit_check = false;
	long pending;
	struct rcu_cblist rcl = RCU_CBLIST_INITIALIZER(rcl);
	struct rcu_head *rhp;
	long tlimit = 0;

	/* If no callbacks are ready, just return. */
	if (!rcu_segcblist_ready_cbs(&rdp->cblist)) {
		trace_rcu_batch_start(rcu_state.name,
				      rcu_segcblist_n_cbs(&rdp->cblist), 0);
		trace_rcu_batch_end(rcu_state.name, 0,
				    !rcu_segcblist_empty(&rdp->cblist),
				    need_resched(), is_idle_task(current),
				    rcu_is_callbacks_kthread(rdp));
		return;
	}

	/*
	 * Extract the list of ready callbacks, disabling IRQs to prevent
	 * races with call_rcu() from interrupt handlers.  Leave the
	 * callback counts, as rcu_barrier() needs to be conservative.
	 */
	rcu_nocb_lock_irqsave(rdp, flags);
	WARN_ON_ONCE(cpu_is_offline(smp_processor_id()));
	pending = rcu_segcblist_get_seglen(&rdp->cblist, RCU_DONE_TAIL);
	div = READ_ONCE(rcu_divisor);
	div = div < 0 ? 7 : div > sizeof(long) * 8 - 2 ? sizeof(long) * 8 - 2 : div;
	bl = max(rdp->blimit, pending >> div);
	if ((in_serving_softirq() || rdp->rcu_cpu_kthread_status == RCU_KTHREAD_RUNNING) &&
	    (IS_ENABLED(CONFIG_RCU_DOUBLE_CHECK_CB_TIME) || unlikely(bl > 100))) {
		const long npj = NSEC_PER_SEC / HZ;
		long rrn = READ_ONCE(rcu_resched_ns);

		rrn = rrn < NSEC_PER_MSEC ? NSEC_PER_MSEC : rrn > NSEC_PER_SEC ? NSEC_PER_SEC : rrn;
		tlimit = local_clock() + rrn;
		jlimit = jiffies + (rrn + npj + 1) / npj;
		jlimit_check = true;
	}
	trace_rcu_batch_start(rcu_state.name,
			      rcu_segcblist_n_cbs(&rdp->cblist), bl);
	rcu_segcblist_extract_done_cbs(&rdp->cblist, &rcl);
	if (rcu_rdp_is_offloaded(rdp))
		rdp->qlen_last_fqs_check = rcu_segcblist_n_cbs(&rdp->cblist);

	trace_rcu_segcb_stats(&rdp->cblist, TPS("SegCbDequeued"));
	rcu_nocb_unlock_irqrestore(rdp, flags);

	/* Invoke callbacks. */
	tick_dep_set_task(current, TICK_DEP_BIT_RCU);
	rhp = rcu_cblist_dequeue(&rcl);

	for (; rhp; rhp = rcu_cblist_dequeue(&rcl)) {
		rcu_callback_t f;

		count++;
		debug_rcu_head_unqueue(rhp);

		rcu_lock_acquire(&rcu_callback_map);
		trace_rcu_invoke_callback(rcu_state.name, rhp);

		f = rhp->func;
		WRITE_ONCE(rhp->func, (rcu_callback_t)0L);
		f(rhp);

		rcu_lock_release(&rcu_callback_map);

		/*
		 * Stop only if limit reached and CPU has something to do.
		 */
		if (in_serving_softirq()) {
			if (count >= bl && (need_resched() || !is_idle_task(current)))
				break;
			/*
			 * Make sure we don't spend too much time here and deprive other
			 * softirq vectors of CPU cycles.
			 */
			if (rcu_do_batch_check_time(count, tlimit, jlimit_check, jlimit))
				break;
		} else {
			// In rcuc/rcuoc context, so no worries about
			// depriving other softirq vectors of CPU cycles.
			local_bh_enable();
			lockdep_assert_irqs_enabled();
			cond_resched_tasks_rcu_qs();
			lockdep_assert_irqs_enabled();
			local_bh_disable();
			// But rcuc kthreads can delay quiescent-state
			// reporting, so check time limits for them.
			if (rdp->rcu_cpu_kthread_status == RCU_KTHREAD_RUNNING &&
			    rcu_do_batch_check_time(count, tlimit, jlimit_check, jlimit)) {
				rdp->rcu_cpu_has_work = 1;
				break;
			}
		}
	}

	rcu_nocb_lock_irqsave(rdp, flags);
	rdp->n_cbs_invoked += count;
	trace_rcu_batch_end(rcu_state.name, count, !!rcl.head, need_resched(),
			    is_idle_task(current), rcu_is_callbacks_kthread(rdp));

	/* Update counts and requeue any remaining callbacks. */
	rcu_segcblist_insert_done_cbs(&rdp->cblist, &rcl);
	rcu_segcblist_add_len(&rdp->cblist, -count);

	/* Reinstate batch limit if we have worked down the excess. */
	count = rcu_segcblist_n_cbs(&rdp->cblist);
	if (rdp->blimit >= DEFAULT_MAX_RCU_BLIMIT && count <= qlowmark)
		rdp->blimit = blimit;

	/* Reset ->qlen_last_fqs_check trigger if enough CBs have drained. */
	if (count == 0 && rdp->qlen_last_fqs_check != 0) {
		rdp->qlen_last_fqs_check = 0;
		rdp->n_force_qs_snap = READ_ONCE(rcu_state.n_force_qs);
	} else if (count < rdp->qlen_last_fqs_check - qhimark)
		rdp->qlen_last_fqs_check = count;

	/*
	 * The following usually indicates a double call_rcu().  To track
	 * this down, try building with CONFIG_DEBUG_OBJECTS_RCU_HEAD=y.
	 */
	empty = rcu_segcblist_empty(&rdp->cblist);
	WARN_ON_ONCE(count == 0 && !empty);
	WARN_ON_ONCE(!IS_ENABLED(CONFIG_RCU_NOCB_CPU) &&
		     count != 0 && empty);
	WARN_ON_ONCE(count == 0 && rcu_segcblist_n_segment_cbs(&rdp->cblist) != 0);
	WARN_ON_ONCE(!empty && rcu_segcblist_n_segment_cbs(&rdp->cblist) == 0);

	rcu_nocb_unlock_irqrestore(rdp, flags);

	tick_dep_clear_task(current, TICK_DEP_BIT_RCU);
}

/*
 * This function is invoked from each scheduling-clock interrupt,
 * and checks to see if this CPU is in a non-context-switch quiescent
 * state, for example, user mode or idle loop.  It also schedules RCU
 * core processing.  If the current grace period has gone on too long,
 * it will ask the scheduler to manufacture a context switch for the sole
 * purpose of providing the needed quiescent state.
 */
void rcu_sched_clock_irq(int user)
{
	unsigned long j;

	if (IS_ENABLED(CONFIG_PROVE_RCU)) {
		j = jiffies;
		WARN_ON_ONCE(time_before(j, __this_cpu_read(rcu_data.last_sched_clock)));
		__this_cpu_write(rcu_data.last_sched_clock, j);
	}
	trace_rcu_utilization(TPS("Start scheduler-tick"));
	lockdep_assert_irqs_disabled();
	raw_cpu_inc(rcu_data.ticks_this_gp);
	/* The load-acquire pairs with the store-release setting to true. */
	if (smp_load_acquire(this_cpu_ptr(&rcu_data.rcu_urgent_qs))) {
		/* Idle and userspace execution already are quiescent states. */
		if (!rcu_is_cpu_rrupt_from_idle() && !user) {
			set_tsk_need_resched(current);
			set_preempt_need_resched();
		}
		__this_cpu_write(rcu_data.rcu_urgent_qs, false);
	}
	rcu_flavor_sched_clock_irq(user);
	if (rcu_pending(user))
		invoke_rcu_core();
	if (user || rcu_is_cpu_rrupt_from_idle())
		rcu_note_voluntary_context_switch(current);
	lockdep_assert_irqs_disabled();

	trace_rcu_utilization(TPS("End scheduler-tick"));
}

/*
 * Scan the leaf rcu_node structures.  For each structure on which all
 * CPUs have reported a quiescent state and on which there are tasks
 * blocking the current grace period, initiate RCU priority boosting.
 * Otherwise, invoke the specified function to check dyntick state for
 * each CPU that has not yet reported a quiescent state.
 */
static void force_qs_rnp(int (*f)(struct rcu_data *rdp))
{
	int cpu;
	unsigned long flags;
	unsigned long mask;
	struct rcu_data *rdp;
	struct rcu_node *rnp;

	rcu_state.cbovld = rcu_state.cbovldnext;
	rcu_state.cbovldnext = false;
	rcu_for_each_leaf_node(rnp) {
		cond_resched_tasks_rcu_qs();
		mask = 0;
		raw_spin_lock_irqsave_rcu_node(rnp, flags);
		rcu_state.cbovldnext |= !!rnp->cbovldmask;
		if (rnp->qsmask == 0) {
			if (rcu_preempt_blocked_readers_cgp(rnp)) {
				/*
				 * No point in scanning bits because they
				 * are all zero.  But we might need to
				 * priority-boost blocked readers.
				 */
				rcu_initiate_boost(rnp, flags);
				/* rcu_initiate_boost() releases rnp->lock */
				continue;
			}
			raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
			continue;
		}
		for_each_leaf_node_cpu_mask(rnp, cpu, rnp->qsmask) {
			rdp = per_cpu_ptr(&rcu_data, cpu);
			if (f(rdp)) {
				mask |= rdp->grpmask;
				rcu_disable_urgency_upon_qs(rdp);
			}
		}
		if (mask != 0) {
			/* Idle/offline CPUs, report (releases rnp->lock). */
			rcu_report_qs_rnp(mask, rnp, rnp->gp_seq, flags);
		} else {
			/* Nothing to do here, so just drop the lock. */
			raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
		}
	}
}

/*
 * Force quiescent states on reluctant CPUs, and also detect which
 * CPUs are in dyntick-idle mode.
 */
void rcu_force_quiescent_state(void)
{
	unsigned long flags;
	bool ret;
	struct rcu_node *rnp;
	struct rcu_node *rnp_old = NULL;

	/* Funnel through hierarchy to reduce memory contention. */
	rnp = raw_cpu_read(rcu_data.mynode);
	for (; rnp != NULL; rnp = rnp->parent) {
		ret = (READ_ONCE(rcu_state.gp_flags) & RCU_GP_FLAG_FQS) ||
		       !raw_spin_trylock(&rnp->fqslock);
		if (rnp_old != NULL)
			raw_spin_unlock(&rnp_old->fqslock);
		if (ret)
			return;
		rnp_old = rnp;
	}
	/* rnp_old == rcu_get_root(), rnp == NULL. */

	/* Reached the root of the rcu_node tree, acquire lock. */
	raw_spin_lock_irqsave_rcu_node(rnp_old, flags);
	raw_spin_unlock(&rnp_old->fqslock);
	if (READ_ONCE(rcu_state.gp_flags) & RCU_GP_FLAG_FQS) {
		raw_spin_unlock_irqrestore_rcu_node(rnp_old, flags);
		return;  /* Someone beat us to it. */
	}
	WRITE_ONCE(rcu_state.gp_flags,
		   READ_ONCE(rcu_state.gp_flags) | RCU_GP_FLAG_FQS);
	raw_spin_unlock_irqrestore_rcu_node(rnp_old, flags);
	rcu_gp_kthread_wake();
}
EXPORT_SYMBOL_GPL(rcu_force_quiescent_state);

// Workqueue handler for an RCU reader for kernels enforcing struct RCU
// grace periods.
static void strict_work_handler(struct work_struct *work)
{
	rcu_read_lock();
	rcu_read_unlock();
}

/* Perform RCU core processing work for the current CPU.  */
static __latent_entropy void rcu_core(void)
{
	unsigned long flags;
	struct rcu_data *rdp = raw_cpu_ptr(&rcu_data);
	struct rcu_node *rnp = rdp->mynode;
	/*
	 * On RT rcu_core() can be preempted when IRQs aren't disabled.
	 * Therefore this function can race with concurrent NOCB (de-)offloading
	 * on this CPU and the below condition must be considered volatile.
	 * However if we race with:
	 *
	 * _ Offloading:   In the worst case we accelerate or process callbacks
	 *                 concurrently with NOCB kthreads. We are guaranteed to
	 *                 call rcu_nocb_lock() if that happens.
	 *
	 * _ Deoffloading: In the worst case we miss callbacks acceleration or
	 *                 processing. This is fine because the early stage
	 *                 of deoffloading invokes rcu_core() after setting
	 *                 SEGCBLIST_RCU_CORE. So we guarantee that we'll process
	 *                 what could have been dismissed without the need to wait
	 *                 for the next rcu_pending() check in the next jiffy.
	 */
	const bool do_batch = !rcu_segcblist_completely_offloaded(&rdp->cblist);

	if (cpu_is_offline(smp_processor_id()))
		return;
	trace_rcu_utilization(TPS("Start RCU core"));
	WARN_ON_ONCE(!rdp->beenonline);

	/* Report any deferred quiescent states if preemption enabled. */
	if (IS_ENABLED(CONFIG_PREEMPT_COUNT) && (!(preempt_count() & PREEMPT_MASK))) {
		rcu_preempt_deferred_qs(current);
	} else if (rcu_preempt_need_deferred_qs(current)) {
		set_tsk_need_resched(current);
		set_preempt_need_resched();
	}

	/* Update RCU state based on any recent quiescent states. */
	rcu_check_quiescent_state(rdp);

	/* No grace period and unregistered callbacks? */
	if (!rcu_gp_in_progress() &&
	    rcu_segcblist_is_enabled(&rdp->cblist) && do_batch) {
		rcu_nocb_lock_irqsave(rdp, flags);
		if (!rcu_segcblist_restempty(&rdp->cblist, RCU_NEXT_READY_TAIL))
			rcu_accelerate_cbs_unlocked(rnp, rdp);
		rcu_nocb_unlock_irqrestore(rdp, flags);
	}

	rcu_check_gp_start_stall(rnp, rdp, rcu_jiffies_till_stall_check());

	/* If there are callbacks ready, invoke them. */
	if (do_batch && rcu_segcblist_ready_cbs(&rdp->cblist) &&
	    likely(READ_ONCE(rcu_scheduler_fully_active))) {
		rcu_do_batch(rdp);
		/* Re-invoke RCU core processing if there are callbacks remaining. */
		if (rcu_segcblist_ready_cbs(&rdp->cblist))
			invoke_rcu_core();
	}

	/* Do any needed deferred wakeups of rcuo kthreads. */
	do_nocb_deferred_wakeup(rdp);
	trace_rcu_utilization(TPS("End RCU core"));

	// If strict GPs, schedule an RCU reader in a clean environment.
	if (IS_ENABLED(CONFIG_RCU_STRICT_GRACE_PERIOD))
		queue_work_on(rdp->cpu, rcu_gp_wq, &rdp->strict_work);
}

static void rcu_core_si(struct softirq_action *h)
{
	rcu_core();
}

static void rcu_wake_cond(struct task_struct *t, int status)
{
	/*
	 * If the thread is yielding, only wake it when this
	 * is invoked from idle
	 */
	if (t && (status != RCU_KTHREAD_YIELDING || is_idle_task(current)))
		wake_up_process(t);
}

static void invoke_rcu_core_kthread(void)
{
	struct task_struct *t;
	unsigned long flags;

	local_irq_save(flags);
	__this_cpu_write(rcu_data.rcu_cpu_has_work, 1);
	t = __this_cpu_read(rcu_data.rcu_cpu_kthread_task);
	if (t != NULL && t != current)
		rcu_wake_cond(t, __this_cpu_read(rcu_data.rcu_cpu_kthread_status));
	local_irq_restore(flags);
}

/*
 * Wake up this CPU's rcuc kthread to do RCU core processing.
 */
static void invoke_rcu_core(void)
{
	if (!cpu_online(smp_processor_id()))
		return;
	if (use_softirq)
		raise_softirq(RCU_SOFTIRQ);
	else
		invoke_rcu_core_kthread();
}

static void rcu_cpu_kthread_park(unsigned int cpu)
{
	per_cpu(rcu_data.rcu_cpu_kthread_status, cpu) = RCU_KTHREAD_OFFCPU;
}

static int rcu_cpu_kthread_should_run(unsigned int cpu)
{
	return __this_cpu_read(rcu_data.rcu_cpu_has_work);
}

/*
 * Per-CPU kernel thread that invokes RCU callbacks.  This replaces
 * the RCU softirq used in configurations of RCU that do not support RCU
 * priority boosting.
 */
static void rcu_cpu_kthread(unsigned int cpu)
{
	unsigned int *statusp = this_cpu_ptr(&rcu_data.rcu_cpu_kthread_status);
	char work, *workp = this_cpu_ptr(&rcu_data.rcu_cpu_has_work);
	unsigned long *j = this_cpu_ptr(&rcu_data.rcuc_activity);
	int spincnt;

	trace_rcu_utilization(TPS("Start CPU kthread@rcu_run"));
	for (spincnt = 0; spincnt < 10; spincnt++) {
		WRITE_ONCE(*j, jiffies);
		local_bh_disable();
		*statusp = RCU_KTHREAD_RUNNING;
		local_irq_disable();
		work = *workp;
		WRITE_ONCE(*workp, 0);
		local_irq_enable();
		if (work)
			rcu_core();
		local_bh_enable();
		if (!READ_ONCE(*workp)) {
			trace_rcu_utilization(TPS("End CPU kthread@rcu_wait"));
			*statusp = RCU_KTHREAD_WAITING;
			return;
		}
	}
	*statusp = RCU_KTHREAD_YIELDING;
	trace_rcu_utilization(TPS("Start CPU kthread@rcu_yield"));
	schedule_timeout_idle(2);
	trace_rcu_utilization(TPS("End CPU kthread@rcu_yield"));
	*statusp = RCU_KTHREAD_WAITING;
	WRITE_ONCE(*j, jiffies);
}

static struct smp_hotplug_thread rcu_cpu_thread_spec = {
	.store			= &rcu_data.rcu_cpu_kthread_task,
	.thread_should_run	= rcu_cpu_kthread_should_run,
	.thread_fn		= rcu_cpu_kthread,
	.thread_comm		= "rcuc/%u",
	.setup			= rcu_cpu_kthread_setup,
	.park			= rcu_cpu_kthread_park,
};

/*
 * Spawn per-CPU RCU core processing kthreads.
 */
static int __init rcu_spawn_core_kthreads(void)
{
	int cpu;

	for_each_possible_cpu(cpu)
		per_cpu(rcu_data.rcu_cpu_has_work, cpu) = 0;
	if (use_softirq)
		return 0;
	WARN_ONCE(smpboot_register_percpu_thread(&rcu_cpu_thread_spec),
		  "%s: Could not start rcuc kthread, OOM is now expected behavior\n", __func__);
	return 0;
}

/*
 * Handle any core-RCU processing required by a call_rcu() invocation.
 */
static void __call_rcu_core(struct rcu_data *rdp, struct rcu_head *head,
			    unsigned long flags)
{
	/*
	 * If called from an extended quiescent state, invoke the RCU
	 * core in order to force a re-evaluation of RCU's idleness.
	 */
	if (!rcu_is_watching())
		invoke_rcu_core();

	/* If interrupts were disabled or CPU offline, don't invoke RCU core. */
	if (irqs_disabled_flags(flags) || cpu_is_offline(smp_processor_id()))
		return;

	/*
	 * Force the grace period if too many callbacks or too long waiting.
	 * Enforce hysteresis, and don't invoke rcu_force_quiescent_state()
	 * if some other CPU has recently done so.  Also, don't bother
	 * invoking rcu_force_quiescent_state() if the newly enqueued callback
	 * is the only one waiting for a grace period to complete.
	 */
	if (unlikely(rcu_segcblist_n_cbs(&rdp->cblist) >
		     rdp->qlen_last_fqs_check + qhimark)) {

		/* Are we ignoring a completed grace period? */
		note_gp_changes(rdp);

		/* Start a new grace period if one not already started. */
		if (!rcu_gp_in_progress()) {
			rcu_accelerate_cbs_unlocked(rdp->mynode, rdp);
		} else {
			/* Give the grace period a kick. */
			rdp->blimit = DEFAULT_MAX_RCU_BLIMIT;
			if (READ_ONCE(rcu_state.n_force_qs) == rdp->n_force_qs_snap &&
			    rcu_segcblist_first_pend_cb(&rdp->cblist) != head)
				rcu_force_quiescent_state();
			rdp->n_force_qs_snap = READ_ONCE(rcu_state.n_force_qs);
			rdp->qlen_last_fqs_check = rcu_segcblist_n_cbs(&rdp->cblist);
		}
	}
}

/*
 * RCU callback function to leak a callback.
 */
static void rcu_leak_callback(struct rcu_head *rhp)
{
}

/*
 * Check and if necessary update the leaf rcu_node structure's
 * ->cbovldmask bit corresponding to the current CPU based on that CPU's
 * number of queued RCU callbacks.  The caller must hold the leaf rcu_node
 * structure's ->lock.
 */
static void check_cb_ovld_locked(struct rcu_data *rdp, struct rcu_node *rnp)
{
	raw_lockdep_assert_held_rcu_node(rnp);
	if (qovld_calc <= 0)
		return; // Early boot and wildcard value set.
	if (rcu_segcblist_n_cbs(&rdp->cblist) >= qovld_calc)
		WRITE_ONCE(rnp->cbovldmask, rnp->cbovldmask | rdp->grpmask);
	else
		WRITE_ONCE(rnp->cbovldmask, rnp->cbovldmask & ~rdp->grpmask);
}

/*
 * Check and if necessary update the leaf rcu_node structure's
 * ->cbovldmask bit corresponding to the current CPU based on that CPU's
 * number of queued RCU callbacks.  No locks need be held, but the
 * caller must have disabled interrupts.
 *
 * Note that this function ignores the possibility that there are a lot
 * of callbacks all of which have already seen the end of their respective
 * grace periods.  This omission is due to the need for no-CBs CPUs to
 * be holding ->nocb_lock to do this check, which is too heavy for a
 * common-case operation.
 */
static void check_cb_ovld(struct rcu_data *rdp)
{
	struct rcu_node *const rnp = rdp->mynode;

	if (qovld_calc <= 0 ||
	    ((rcu_segcblist_n_cbs(&rdp->cblist) >= qovld_calc) ==
	     !!(READ_ONCE(rnp->cbovldmask) & rdp->grpmask)))
		return; // Early boot wildcard value or already set correctly.
	raw_spin_lock_rcu_node(rnp);
	check_cb_ovld_locked(rdp, rnp);
	raw_spin_unlock_rcu_node(rnp);
}

static void
__call_rcu_common(struct rcu_head *head, rcu_callback_t func, bool lazy_in)
{
	static atomic_t doublefrees;
	unsigned long flags;
	bool lazy;
	struct rcu_data *rdp;
	bool was_alldone;

	/* Misaligned rcu_head! */
	WARN_ON_ONCE((unsigned long)head & (sizeof(void *) - 1));

	if (debug_rcu_head_queue(head)) {
		/*
		 * Probable double call_rcu(), so leak the callback.
		 * Use rcu:rcu_callback trace event to find the previous
		 * time callback was passed to call_rcu().
		 */
		if (atomic_inc_return(&doublefrees) < 4) {
			pr_err("%s(): Double-freed CB %p->%pS()!!!  ", __func__, head, head->func);
			mem_dump_obj(head);
		}
		WRITE_ONCE(head->func, rcu_leak_callback);
		return;
	}
	head->func = func;
	head->next = NULL;
	kasan_record_aux_stack_noalloc(head);
	local_irq_save(flags);
	rdp = this_cpu_ptr(&rcu_data);
	lazy = lazy_in && !rcu_async_should_hurry();

	/* Add the callback to our list. */
	if (unlikely(!rcu_segcblist_is_enabled(&rdp->cblist))) {
		// This can trigger due to call_rcu() from offline CPU:
		WARN_ON_ONCE(rcu_scheduler_active != RCU_SCHEDULER_INACTIVE);
		WARN_ON_ONCE(!rcu_is_watching());
		// Very early boot, before rcu_init().  Initialize if needed
		// and then drop through to queue the callback.
		if (rcu_segcblist_empty(&rdp->cblist))
			rcu_segcblist_init(&rdp->cblist);
	}

	check_cb_ovld(rdp);
	if (rcu_nocb_try_bypass(rdp, head, &was_alldone, flags, lazy))
		return; // Enqueued onto ->nocb_bypass, so just leave.
	// If no-CBs CPU gets here, rcu_nocb_try_bypass() acquired ->nocb_lock.
	rcu_segcblist_enqueue(&rdp->cblist, head);
	if (__is_kvfree_rcu_offset((unsigned long)func))
		trace_rcu_kvfree_callback(rcu_state.name, head,
					 (unsigned long)func,
					 rcu_segcblist_n_cbs(&rdp->cblist));
	else
		trace_rcu_callback(rcu_state.name, head,
				   rcu_segcblist_n_cbs(&rdp->cblist));

	trace_rcu_segcb_stats(&rdp->cblist, TPS("SegCBQueued"));

	/* Go handle any RCU core processing required. */
	if (unlikely(rcu_rdp_is_offloaded(rdp))) {
		__call_rcu_nocb_wake(rdp, was_alldone, flags); /* unlocks */
	} else {
		__call_rcu_core(rdp, head, flags);
		local_irq_restore(flags);
	}
}

#ifdef CONFIG_RCU_LAZY
/**
 * call_rcu_hurry() - Queue RCU callback for invocation after grace period, and
 * flush all lazy callbacks (including the new one) to the main ->cblist while
 * doing so.
 *
 * @head: structure to be used for queueing the RCU updates.
 * @func: actual callback function to be invoked after the grace period
 *
 * The callback function will be invoked some time after a full grace
 * period elapses, in other words after all pre-existing RCU read-side
 * critical sections have completed.
 *
 * Use this API instead of call_rcu() if you don't want the callback to be
 * invoked after very long periods of time, which can happen on systems without
 * memory pressure and on systems which are lightly loaded or mostly idle.
 * This function will cause callbacks to be invoked sooner than later at the
 * expense of extra power. Other than that, this function is identical to, and
 * reuses call_rcu()'s logic. Refer to call_rcu() for more details about memory
 * ordering and other functionality.
 */
void call_rcu_hurry(struct rcu_head *head, rcu_callback_t func)
{
	return __call_rcu_common(head, func, false);
}
EXPORT_SYMBOL_GPL(call_rcu_hurry);
#endif

/**
 * call_rcu() - Queue an RCU callback for invocation after a grace period.
 * By default the callbacks are 'lazy' and are kept hidden from the main
 * ->cblist to prevent starting of grace periods too soon.
 * If you desire grace periods to start very soon, use call_rcu_hurry().
 *
 * @head: structure to be used for queueing the RCU updates.
 * @func: actual callback function to be invoked after the grace period
 *
 * The callback function will be invoked some time after a full grace
 * period elapses, in other words after all pre-existing RCU read-side
 * critical sections have completed.  However, the callback function
 * might well execute concurrently with RCU read-side critical sections
 * that started after call_rcu() was invoked.
 *
 * RCU read-side critical sections are delimited by rcu_read_lock()
 * and rcu_read_unlock(), and may be nested.  In addition, but only in
 * v5.0 and later, regions of code across which interrupts, preemption,
 * or softirqs have been disabled also serve as RCU read-side critical
 * sections.  This includes hardware interrupt handlers, softirq handlers,
 * and NMI handlers.
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
 *
 * Implementation of these memory-ordering guarantees is described here:
 * Documentation/RCU/Design/Memory-Ordering/Tree-RCU-Memory-Ordering.rst.
 */
void call_rcu(struct rcu_head *head, rcu_callback_t func)
{
	return __call_rcu_common(head, func, IS_ENABLED(CONFIG_RCU_LAZY));
}
EXPORT_SYMBOL_GPL(call_rcu);

/* Maximum number of jiffies to wait before draining a batch. */
#define KFREE_DRAIN_JIFFIES (5 * HZ)
#define KFREE_N_BATCHES 2
#define FREE_N_CHANNELS 2

/**
 * struct kvfree_rcu_bulk_data - single block to store kvfree_rcu() pointers
 * @list: List node. All blocks are linked between each other
 * @gp_snap: Snapshot of RCU state for objects placed to this bulk
 * @nr_records: Number of active pointers in the array
 * @records: Array of the kvfree_rcu() pointers
 */
struct kvfree_rcu_bulk_data {
	struct list_head list;
	struct rcu_gp_oldstate gp_snap;
	unsigned long nr_records;
	void *records[];
};

/*
 * This macro defines how many entries the "records" array
 * will contain. It is based on the fact that the size of
 * kvfree_rcu_bulk_data structure becomes exactly one page.
 */
#define KVFREE_BULK_MAX_ENTR \
	((PAGE_SIZE - sizeof(struct kvfree_rcu_bulk_data)) / sizeof(void *))

/**
 * struct kfree_rcu_cpu_work - single batch of kfree_rcu() requests
 * @rcu_work: Let queue_rcu_work() invoke workqueue handler after grace period
 * @head_free: List of kfree_rcu() objects waiting for a grace period
 * @head_free_gp_snap: Grace-period snapshot to check for attempted premature frees.
 * @bulk_head_free: Bulk-List of kvfree_rcu() objects waiting for a grace period
 * @krcp: Pointer to @kfree_rcu_cpu structure
 */

struct kfree_rcu_cpu_work {
	struct rcu_work rcu_work;
	struct rcu_head *head_free;
	struct rcu_gp_oldstate head_free_gp_snap;
	struct list_head bulk_head_free[FREE_N_CHANNELS];
	struct kfree_rcu_cpu *krcp;
};

/**
 * struct kfree_rcu_cpu - batch up kfree_rcu() requests for RCU grace period
 * @head: List of kfree_rcu() objects not yet waiting for a grace period
 * @head_gp_snap: Snapshot of RCU state for objects placed to "@head"
 * @bulk_head: Bulk-List of kvfree_rcu() objects not yet waiting for a grace period
 * @krw_arr: Array of batches of kfree_rcu() objects waiting for a grace period
 * @lock: Synchronize access to this structure
 * @monitor_work: Promote @head to @head_free after KFREE_DRAIN_JIFFIES
 * @initialized: The @rcu_work fields have been initialized
 * @head_count: Number of objects in rcu_head singular list
 * @bulk_count: Number of objects in bulk-list
 * @bkvcache:
 *	A simple cache list that contains objects for reuse purpose.
 *	In order to save some per-cpu space the list is singular.
 *	Even though it is lockless an access has to be protected by the
 *	per-cpu lock.
 * @page_cache_work: A work to refill the cache when it is empty
 * @backoff_page_cache_fill: Delay cache refills
 * @work_in_progress: Indicates that page_cache_work is running
 * @hrtimer: A hrtimer for scheduling a page_cache_work
 * @nr_bkv_objs: number of allocated objects at @bkvcache.
 *
 * This is a per-CPU structure.  The reason that it is not included in
 * the rcu_data structure is to permit this code to be extracted from
 * the RCU files.  Such extraction could allow further optimization of
 * the interactions with the slab allocators.
 */
struct kfree_rcu_cpu {
	// Objects queued on a linked list
	// through their rcu_head structures.
	struct rcu_head *head;
	unsigned long head_gp_snap;
	atomic_t head_count;

	// Objects queued on a bulk-list.
	struct list_head bulk_head[FREE_N_CHANNELS];
	atomic_t bulk_count[FREE_N_CHANNELS];

	struct kfree_rcu_cpu_work krw_arr[KFREE_N_BATCHES];
	raw_spinlock_t lock;
	struct delayed_work monitor_work;
	bool initialized;

	struct delayed_work page_cache_work;
	atomic_t backoff_page_cache_fill;
	atomic_t work_in_progress;
	struct hrtimer hrtimer;

	struct llist_head bkvcache;
	int nr_bkv_objs;
};

static DEFINE_PER_CPU(struct kfree_rcu_cpu, krc) = {
	.lock = __RAW_SPIN_LOCK_UNLOCKED(krc.lock),
};

static __always_inline void
debug_rcu_bhead_unqueue(struct kvfree_rcu_bulk_data *bhead)
{
#ifdef CONFIG_DEBUG_OBJECTS_RCU_HEAD
	int i;

	for (i = 0; i < bhead->nr_records; i++)
		debug_rcu_head_unqueue((struct rcu_head *)(bhead->records[i]));
#endif
}

static inline struct kfree_rcu_cpu *
krc_this_cpu_lock(unsigned long *flags)
{
	struct kfree_rcu_cpu *krcp;

	local_irq_save(*flags);	// For safely calling this_cpu_ptr().
	krcp = this_cpu_ptr(&krc);
	raw_spin_lock(&krcp->lock);

	return krcp;
}

static inline void
krc_this_cpu_unlock(struct kfree_rcu_cpu *krcp, unsigned long flags)
{
	raw_spin_unlock_irqrestore(&krcp->lock, flags);
}

static inline struct kvfree_rcu_bulk_data *
get_cached_bnode(struct kfree_rcu_cpu *krcp)
{
	if (!krcp->nr_bkv_objs)
		return NULL;

	WRITE_ONCE(krcp->nr_bkv_objs, krcp->nr_bkv_objs - 1);
	return (struct kvfree_rcu_bulk_data *)
		llist_del_first(&krcp->bkvcache);
}

static inline bool
put_cached_bnode(struct kfree_rcu_cpu *krcp,
	struct kvfree_rcu_bulk_data *bnode)
{
	// Check the limit.
	if (krcp->nr_bkv_objs >= rcu_min_cached_objs)
		return false;

	llist_add((struct llist_node *) bnode, &krcp->bkvcache);
	WRITE_ONCE(krcp->nr_bkv_objs, krcp->nr_bkv_objs + 1);
	return true;
}

static int
drain_page_cache(struct kfree_rcu_cpu *krcp)
{
	unsigned long flags;
	struct llist_node *page_list, *pos, *n;
	int freed = 0;

	if (!rcu_min_cached_objs)
		return 0;

	raw_spin_lock_irqsave(&krcp->lock, flags);
	page_list = llist_del_all(&krcp->bkvcache);
	WRITE_ONCE(krcp->nr_bkv_objs, 0);
	raw_spin_unlock_irqrestore(&krcp->lock, flags);

	llist_for_each_safe(pos, n, page_list) {
		free_page((unsigned long)pos);
		freed++;
	}

	return freed;
}

static void
kvfree_rcu_bulk(struct kfree_rcu_cpu *krcp,
	struct kvfree_rcu_bulk_data *bnode, int idx)
{
	unsigned long flags;
	int i;

	if (!WARN_ON_ONCE(!poll_state_synchronize_rcu_full(&bnode->gp_snap))) {
		debug_rcu_bhead_unqueue(bnode);
		rcu_lock_acquire(&rcu_callback_map);
		if (idx == 0) { // kmalloc() / kfree().
			trace_rcu_invoke_kfree_bulk_callback(
				rcu_state.name, bnode->nr_records,
				bnode->records);

			kfree_bulk(bnode->nr_records, bnode->records);
		} else { // vmalloc() / vfree().
			for (i = 0; i < bnode->nr_records; i++) {
				trace_rcu_invoke_kvfree_callback(
					rcu_state.name, bnode->records[i], 0);

				vfree(bnode->records[i]);
			}
		}
		rcu_lock_release(&rcu_callback_map);
	}

	raw_spin_lock_irqsave(&krcp->lock, flags);
	if (put_cached_bnode(krcp, bnode))
		bnode = NULL;
	raw_spin_unlock_irqrestore(&krcp->lock, flags);

	if (bnode)
		free_page((unsigned long) bnode);

	cond_resched_tasks_rcu_qs();
}

static void
kvfree_rcu_list(struct rcu_head *head)
{
	struct rcu_head *next;

	for (; head; head = next) {
		void *ptr = (void *) head->func;
		unsigned long offset = (void *) head - ptr;

		next = head->next;
		debug_rcu_head_unqueue((struct rcu_head *)ptr);
		rcu_lock_acquire(&rcu_callback_map);
		trace_rcu_invoke_kvfree_callback(rcu_state.name, head, offset);

		if (!WARN_ON_ONCE(!__is_kvfree_rcu_offset(offset)))
			kvfree(ptr);

		rcu_lock_release(&rcu_callback_map);
		cond_resched_tasks_rcu_qs();
	}
}

/*
 * This function is invoked in workqueue context after a grace period.
 * It frees all the objects queued on ->bulk_head_free or ->head_free.
 */
static void kfree_rcu_work(struct work_struct *work)
{
	unsigned long flags;
	struct kvfree_rcu_bulk_data *bnode, *n;
	struct list_head bulk_head[FREE_N_CHANNELS];
	struct rcu_head *head;
	struct kfree_rcu_cpu *krcp;
	struct kfree_rcu_cpu_work *krwp;
	struct rcu_gp_oldstate head_gp_snap;
	int i;

	krwp = container_of(to_rcu_work(work),
		struct kfree_rcu_cpu_work, rcu_work);
	krcp = krwp->krcp;

	raw_spin_lock_irqsave(&krcp->lock, flags);
	// Channels 1 and 2.
	for (i = 0; i < FREE_N_CHANNELS; i++)
		list_replace_init(&krwp->bulk_head_free[i], &bulk_head[i]);

	// Channel 3.
	head = krwp->head_free;
	krwp->head_free = NULL;
	head_gp_snap = krwp->head_free_gp_snap;
	raw_spin_unlock_irqrestore(&krcp->lock, flags);

	// Handle the first two channels.
	for (i = 0; i < FREE_N_CHANNELS; i++) {
		// Start from the tail page, so a GP is likely passed for it.
		list_for_each_entry_safe(bnode, n, &bulk_head[i], list)
			kvfree_rcu_bulk(krcp, bnode, i);
	}

	/*
	 * This is used when the "bulk" path can not be used for the
	 * double-argument of kvfree_rcu().  This happens when the
	 * page-cache is empty, which means that objects are instead
	 * queued on a linked list through their rcu_head structures.
	 * This list is named "Channel 3".
	 */
	if (head && !WARN_ON_ONCE(!poll_state_synchronize_rcu_full(&head_gp_snap)))
		kvfree_rcu_list(head);
}

static bool
need_offload_krc(struct kfree_rcu_cpu *krcp)
{
	int i;

	for (i = 0; i < FREE_N_CHANNELS; i++)
		if (!list_empty(&krcp->bulk_head[i]))
			return true;

	return !!READ_ONCE(krcp->head);
}

static bool
need_wait_for_krwp_work(struct kfree_rcu_cpu_work *krwp)
{
	int i;

	for (i = 0; i < FREE_N_CHANNELS; i++)
		if (!list_empty(&krwp->bulk_head_free[i]))
			return true;

	return !!krwp->head_free;
}

static int krc_count(struct kfree_rcu_cpu *krcp)
{
	int sum = atomic_read(&krcp->head_count);
	int i;

	for (i = 0; i < FREE_N_CHANNELS; i++)
		sum += atomic_read(&krcp->bulk_count[i]);

	return sum;
}

static void
schedule_delayed_monitor_work(struct kfree_rcu_cpu *krcp)
{
	long delay, delay_left;

	delay = krc_count(krcp) >= KVFREE_BULK_MAX_ENTR ? 1:KFREE_DRAIN_JIFFIES;
	if (delayed_work_pending(&krcp->monitor_work)) {
		delay_left = krcp->monitor_work.timer.expires - jiffies;
		if (delay < delay_left)
			mod_delayed_work(system_wq, &krcp->monitor_work, delay);
		return;
	}
	queue_delayed_work(system_wq, &krcp->monitor_work, delay);
}

static void
kvfree_rcu_drain_ready(struct kfree_rcu_cpu *krcp)
{
	struct list_head bulk_ready[FREE_N_CHANNELS];
	struct kvfree_rcu_bulk_data *bnode, *n;
	struct rcu_head *head_ready = NULL;
	unsigned long flags;
	int i;

	raw_spin_lock_irqsave(&krcp->lock, flags);
	for (i = 0; i < FREE_N_CHANNELS; i++) {
		INIT_LIST_HEAD(&bulk_ready[i]);

		list_for_each_entry_safe_reverse(bnode, n, &krcp->bulk_head[i], list) {
			if (!poll_state_synchronize_rcu_full(&bnode->gp_snap))
				break;

			atomic_sub(bnode->nr_records, &krcp->bulk_count[i]);
			list_move(&bnode->list, &bulk_ready[i]);
		}
	}

	if (krcp->head && poll_state_synchronize_rcu(krcp->head_gp_snap)) {
		head_ready = krcp->head;
		atomic_set(&krcp->head_count, 0);
		WRITE_ONCE(krcp->head, NULL);
	}
	raw_spin_unlock_irqrestore(&krcp->lock, flags);

	for (i = 0; i < FREE_N_CHANNELS; i++) {
		list_for_each_entry_safe(bnode, n, &bulk_ready[i], list)
			kvfree_rcu_bulk(krcp, bnode, i);
	}

	if (head_ready)
		kvfree_rcu_list(head_ready);
}

/*
 * This function is invoked after the KFREE_DRAIN_JIFFIES timeout.
 */
static void kfree_rcu_monitor(struct work_struct *work)
{
	struct kfree_rcu_cpu *krcp = container_of(work,
		struct kfree_rcu_cpu, monitor_work.work);
	unsigned long flags;
	int i, j;

	// Drain ready for reclaim.
	kvfree_rcu_drain_ready(krcp);

	raw_spin_lock_irqsave(&krcp->lock, flags);

	// Attempt to start a new batch.
	for (i = 0; i < KFREE_N_BATCHES; i++) {
		struct kfree_rcu_cpu_work *krwp = &(krcp->krw_arr[i]);

		// Try to detach bulk_head or head and attach it, only when
		// all channels are free.  Any channel is not free means at krwp
		// there is on-going rcu work to handle krwp's free business.
		if (need_wait_for_krwp_work(krwp))
			continue;

		// kvfree_rcu_drain_ready() might handle this krcp, if so give up.
		if (need_offload_krc(krcp)) {
			// Channel 1 corresponds to the SLAB-pointer bulk path.
			// Channel 2 corresponds to vmalloc-pointer bulk path.
			for (j = 0; j < FREE_N_CHANNELS; j++) {
				if (list_empty(&krwp->bulk_head_free[j])) {
					atomic_set(&krcp->bulk_count[j], 0);
					list_replace_init(&krcp->bulk_head[j],
						&krwp->bulk_head_free[j]);
				}
			}

			// Channel 3 corresponds to both SLAB and vmalloc
			// objects queued on the linked list.
			if (!krwp->head_free) {
				krwp->head_free = krcp->head;
				get_state_synchronize_rcu_full(&krwp->head_free_gp_snap);
				atomic_set(&krcp->head_count, 0);
				WRITE_ONCE(krcp->head, NULL);
			}

			// One work is per one batch, so there are three
			// "free channels", the batch can handle. It can
			// be that the work is in the pending state when
			// channels have been detached following by each
			// other.
			queue_rcu_work(system_wq, &krwp->rcu_work);
		}
	}

	raw_spin_unlock_irqrestore(&krcp->lock, flags);

	// If there is nothing to detach, it means that our job is
	// successfully done here. In case of having at least one
	// of the channels that is still busy we should rearm the
	// work to repeat an attempt. Because previous batches are
	// still in progress.
	if (need_offload_krc(krcp))
		schedule_delayed_monitor_work(krcp);
}

static enum hrtimer_restart
schedule_page_work_fn(struct hrtimer *t)
{
	struct kfree_rcu_cpu *krcp =
		container_of(t, struct kfree_rcu_cpu, hrtimer);

	queue_delayed_work(system_highpri_wq, &krcp->page_cache_work, 0);
	return HRTIMER_NORESTART;
}

static void fill_page_cache_func(struct work_struct *work)
{
	struct kvfree_rcu_bulk_data *bnode;
	struct kfree_rcu_cpu *krcp =
		container_of(work, struct kfree_rcu_cpu,
			page_cache_work.work);
	unsigned long flags;
	int nr_pages;
	bool pushed;
	int i;

	nr_pages = atomic_read(&krcp->backoff_page_cache_fill) ?
		1 : rcu_min_cached_objs;

	for (i = READ_ONCE(krcp->nr_bkv_objs); i < nr_pages; i++) {
		bnode = (struct kvfree_rcu_bulk_data *)
			__get_free_page(GFP_KERNEL | __GFP_NORETRY | __GFP_NOMEMALLOC | __GFP_NOWARN);

		if (!bnode)
			break;

		raw_spin_lock_irqsave(&krcp->lock, flags);
		pushed = put_cached_bnode(krcp, bnode);
		raw_spin_unlock_irqrestore(&krcp->lock, flags);

		if (!pushed) {
			free_page((unsigned long) bnode);
			break;
		}
	}

	atomic_set(&krcp->work_in_progress, 0);
	atomic_set(&krcp->backoff_page_cache_fill, 0);
}

static void
run_page_cache_worker(struct kfree_rcu_cpu *krcp)
{
	// If cache disabled, bail out.
	if (!rcu_min_cached_objs)
		return;

	if (rcu_scheduler_active == RCU_SCHEDULER_RUNNING &&
			!atomic_xchg(&krcp->work_in_progress, 1)) {
		if (atomic_read(&krcp->backoff_page_cache_fill)) {
			queue_delayed_work(system_wq,
				&krcp->page_cache_work,
					msecs_to_jiffies(rcu_delay_page_cache_fill_msec));
		} else {
			hrtimer_init(&krcp->hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
			krcp->hrtimer.function = schedule_page_work_fn;
			hrtimer_start(&krcp->hrtimer, 0, HRTIMER_MODE_REL);
		}
	}
}

// Record ptr in a page managed by krcp, with the pre-krc_this_cpu_lock()
// state specified by flags.  If can_alloc is true, the caller must
// be schedulable and not be holding any locks or mutexes that might be
// acquired by the memory allocator or anything that it might invoke.
// Returns true if ptr was successfully recorded, else the caller must
// use a fallback.
static inline bool
add_ptr_to_bulk_krc_lock(struct kfree_rcu_cpu **krcp,
	unsigned long *flags, void *ptr, bool can_alloc)
{
	struct kvfree_rcu_bulk_data *bnode;
	int idx;

	*krcp = krc_this_cpu_lock(flags);
	if (unlikely(!(*krcp)->initialized))
		return false;

	idx = !!is_vmalloc_addr(ptr);
	bnode = list_first_entry_or_null(&(*krcp)->bulk_head[idx],
		struct kvfree_rcu_bulk_data, list);

	/* Check if a new block is required. */
	if (!bnode || bnode->nr_records == KVFREE_BULK_MAX_ENTR) {
		bnode = get_cached_bnode(*krcp);
		if (!bnode && can_alloc) {
			krc_this_cpu_unlock(*krcp, *flags);

			// __GFP_NORETRY - allows a light-weight direct reclaim
			// what is OK from minimizing of fallback hitting point of
			// view. Apart of that it forbids any OOM invoking what is
			// also beneficial since we are about to release memory soon.
			//
			// __GFP_NOMEMALLOC - prevents from consuming of all the
			// memory reserves. Please note we have a fallback path.
			//
			// __GFP_NOWARN - it is supposed that an allocation can
			// be failed under low memory or high memory pressure
			// scenarios.
			bnode = (struct kvfree_rcu_bulk_data *)
				__get_free_page(GFP_KERNEL | __GFP_NORETRY | __GFP_NOMEMALLOC | __GFP_NOWARN);
			raw_spin_lock_irqsave(&(*krcp)->lock, *flags);
		}

		if (!bnode)
			return false;

		// Initialize the new block and attach it.
		bnode->nr_records = 0;
		list_add(&bnode->list, &(*krcp)->bulk_head[idx]);
	}

	// Finally insert and update the GP for this page.
	bnode->records[bnode->nr_records++] = ptr;
	get_state_synchronize_rcu_full(&bnode->gp_snap);
	atomic_inc(&(*krcp)->bulk_count[idx]);

	return true;
}

/*
 * Queue a request for lazy invocation of the appropriate free routine
 * after a grace period.  Please note that three paths are maintained,
 * two for the common case using arrays of pointers and a third one that
 * is used only when the main paths cannot be used, for example, due to
 * memory pressure.
 *
 * Each kvfree_call_rcu() request is added to a batch. The batch will be drained
 * every KFREE_DRAIN_JIFFIES number of jiffies. All the objects in the batch will
 * be free'd in workqueue context. This allows us to: batch requests together to
 * reduce the number of grace periods during heavy kfree_rcu()/kvfree_rcu() load.
 */
void kvfree_call_rcu(struct rcu_head *head, void *ptr)
{
	unsigned long flags;
	struct kfree_rcu_cpu *krcp;
	bool success;

	/*
	 * Please note there is a limitation for the head-less
	 * variant, that is why there is a clear rule for such
	 * objects: it can be used from might_sleep() context
	 * only. For other places please embed an rcu_head to
	 * your data.
	 */
	if (!head)
		might_sleep();

	// Queue the object but don't yet schedule the batch.
	if (debug_rcu_head_queue(ptr)) {
		// Probable double kfree_rcu(), just leak.
		WARN_ONCE(1, "%s(): Double-freed call. rcu_head %p\n",
			  __func__, head);

		// Mark as success and leave.
		return;
	}

	kasan_record_aux_stack_noalloc(ptr);
	success = add_ptr_to_bulk_krc_lock(&krcp, &flags, ptr, !head);
	if (!success) {
		run_page_cache_worker(krcp);

		if (head == NULL)
			// Inline if kvfree_rcu(one_arg) call.
			goto unlock_return;

		head->func = ptr;
		head->next = krcp->head;
		WRITE_ONCE(krcp->head, head);
		atomic_inc(&krcp->head_count);

		// Take a snapshot for this krcp.
		krcp->head_gp_snap = get_state_synchronize_rcu();
		success = true;
	}

	// Set timer to drain after KFREE_DRAIN_JIFFIES.
	if (rcu_scheduler_active == RCU_SCHEDULER_RUNNING)
		schedule_delayed_monitor_work(krcp);

unlock_return:
	krc_this_cpu_unlock(krcp, flags);

	/*
	 * Inline kvfree() after synchronize_rcu(). We can do
	 * it from might_sleep() context only, so the current
	 * CPU can pass the QS state.
	 */
	if (!success) {
		debug_rcu_head_unqueue((struct rcu_head *) ptr);
		synchronize_rcu();
		kvfree(ptr);
	}
}
EXPORT_SYMBOL_GPL(kvfree_call_rcu);

static unsigned long
kfree_rcu_shrink_count(struct shrinker *shrink, struct shrink_control *sc)
{
	int cpu;
	unsigned long count = 0;

	/* Snapshot count of all CPUs */
	for_each_possible_cpu(cpu) {
		struct kfree_rcu_cpu *krcp = per_cpu_ptr(&krc, cpu);

		count += krc_count(krcp);
		count += READ_ONCE(krcp->nr_bkv_objs);
		atomic_set(&krcp->backoff_page_cache_fill, 1);
	}

	return count == 0 ? SHRINK_EMPTY : count;
}

static unsigned long
kfree_rcu_shrink_scan(struct shrinker *shrink, struct shrink_control *sc)
{
	int cpu, freed = 0;

	for_each_possible_cpu(cpu) {
		int count;
		struct kfree_rcu_cpu *krcp = per_cpu_ptr(&krc, cpu);

		count = krc_count(krcp);
		count += drain_page_cache(krcp);
		kfree_rcu_monitor(&krcp->monitor_work.work);

		sc->nr_to_scan -= count;
		freed += count;

		if (sc->nr_to_scan <= 0)
			break;
	}

	return freed == 0 ? SHRINK_STOP : freed;
}

static struct shrinker kfree_rcu_shrinker = {
	.count_objects = kfree_rcu_shrink_count,
	.scan_objects = kfree_rcu_shrink_scan,
	.batch = 0,
	.seeks = DEFAULT_SEEKS,
};

void __init kfree_rcu_scheduler_running(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		struct kfree_rcu_cpu *krcp = per_cpu_ptr(&krc, cpu);

		if (need_offload_krc(krcp))
			schedule_delayed_monitor_work(krcp);
	}
}

/*
 * During early boot, any blocking grace-period wait automatically
 * implies a grace period.
 *
 * Later on, this could in theory be the case for kernels built with
 * CONFIG_SMP=y && CONFIG_PREEMPTION=y running on a single CPU, but this
 * is not a common case.  Furthermore, this optimization would cause
 * the rcu_gp_oldstate structure to expand by 50%, so this potential
 * grace-period optimization is ignored once the scheduler is running.
 */
static int rcu_blocking_is_gp(void)
{
	if (rcu_scheduler_active != RCU_SCHEDULER_INACTIVE) {
		might_sleep();
		return false;
	}
	return true;
}

/**
 * synchronize_rcu - wait until a grace period has elapsed.
 *
 * Control will return to the caller some time after a full grace
 * period has elapsed, in other words after all currently executing RCU
 * read-side critical sections have completed.  Note, however, that
 * upon return from synchronize_rcu(), the caller might well be executing
 * concurrently with new RCU read-side critical sections that began while
 * synchronize_rcu() was waiting.
 *
 * RCU read-side critical sections are delimited by rcu_read_lock()
 * and rcu_read_unlock(), and may be nested.  In addition, but only in
 * v5.0 and later, regions of code across which interrupts, preemption,
 * or softirqs have been disabled also serve as RCU read-side critical
 * sections.  This includes hardware interrupt handlers, softirq handlers,
 * and NMI handlers.
 *
 * Note that this guarantee implies further memory-ordering guarantees.
 * On systems with more than one CPU, when synchronize_rcu() returns,
 * each CPU is guaranteed to have executed a full memory barrier since
 * the end of its last RCU read-side critical section whose beginning
 * preceded the call to synchronize_rcu().  In addition, each CPU having
 * an RCU read-side critical section that extends beyond the return from
 * synchronize_rcu() is guaranteed to have executed a full memory barrier
 * after the beginning of synchronize_rcu() and before the beginning of
 * that RCU read-side critical section.  Note that these guarantees include
 * CPUs that are offline, idle, or executing in user mode, as well as CPUs
 * that are executing in the kernel.
 *
 * Furthermore, if CPU A invoked synchronize_rcu(), which returned
 * to its caller on CPU B, then both CPU A and CPU B are guaranteed
 * to have executed a full memory barrier during the execution of
 * synchronize_rcu() -- even if CPU A and CPU B are the same CPU (but
 * again only if the system has more than one CPU).
 *
 * Implementation of these memory-ordering guarantees is described here:
 * Documentation/RCU/Design/Memory-Ordering/Tree-RCU-Memory-Ordering.rst.
 */
void synchronize_rcu(void)
{
	unsigned long flags;
	struct rcu_node *rnp;

	RCU_LOCKDEP_WARN(lock_is_held(&rcu_bh_lock_map) ||
			 lock_is_held(&rcu_lock_map) ||
			 lock_is_held(&rcu_sched_lock_map),
			 "Illegal synchronize_rcu() in RCU read-side critical section");
	if (!rcu_blocking_is_gp()) {
		if (rcu_gp_is_expedited())
			synchronize_rcu_expedited();
		else
			wait_rcu_gp(call_rcu_hurry);
		return;
	}

	// Context allows vacuous grace periods.
	// Note well that this code runs with !PREEMPT && !SMP.
	// In addition, all code that advances grace periods runs at
	// process level.  Therefore, this normal GP overlaps with other
	// normal GPs only by being fully nested within them, which allows
	// reuse of ->gp_seq_polled_snap.
	rcu_poll_gp_seq_start_unlocked(&rcu_state.gp_seq_polled_snap);
	rcu_poll_gp_seq_end_unlocked(&rcu_state.gp_seq_polled_snap);

	// Update the normal grace-period counters to record
	// this grace period, but only those used by the boot CPU.
	// The rcu_scheduler_starting() will take care of the rest of
	// these counters.
	local_irq_save(flags);
	WARN_ON_ONCE(num_online_cpus() > 1);
	rcu_state.gp_seq += (1 << RCU_SEQ_CTR_SHIFT);
	for (rnp = this_cpu_ptr(&rcu_data)->mynode; rnp; rnp = rnp->parent)
		rnp->gp_seq_needed = rnp->gp_seq = rcu_state.gp_seq;
	local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(synchronize_rcu);

/**
 * get_completed_synchronize_rcu_full - Return a full pre-completed polled state cookie
 * @rgosp: Place to put state cookie
 *
 * Stores into @rgosp a value that will always be treated by functions
 * like poll_state_synchronize_rcu_full() as a cookie whose grace period
 * has already completed.
 */
void get_completed_synchronize_rcu_full(struct rcu_gp_oldstate *rgosp)
{
	rgosp->rgos_norm = RCU_GET_STATE_COMPLETED;
	rgosp->rgos_exp = RCU_GET_STATE_COMPLETED;
}
EXPORT_SYMBOL_GPL(get_completed_synchronize_rcu_full);

/**
 * get_state_synchronize_rcu - Snapshot current RCU state
 *
 * Returns a cookie that is used by a later call to cond_synchronize_rcu()
 * or poll_state_synchronize_rcu() to determine whether or not a full
 * grace period has elapsed in the meantime.
 */
unsigned long get_state_synchronize_rcu(void)
{
	/*
	 * Any prior manipulation of RCU-protected data must happen
	 * before the load from ->gp_seq.
	 */
	smp_mb();  /* ^^^ */
	return rcu_seq_snap(&rcu_state.gp_seq_polled);
}
EXPORT_SYMBOL_GPL(get_state_synchronize_rcu);

/**
 * get_state_synchronize_rcu_full - Snapshot RCU state, both normal and expedited
 * @rgosp: location to place combined normal/expedited grace-period state
 *
 * Places the normal and expedited grace-period states in @rgosp.  This
 * state value can be passed to a later call to cond_synchronize_rcu_full()
 * or poll_state_synchronize_rcu_full() to determine whether or not a
 * grace period (whether normal or expedited) has elapsed in the meantime.
 * The rcu_gp_oldstate structure takes up twice the memory of an unsigned
 * long, but is guaranteed to see all grace periods.  In contrast, the
 * combined state occupies less memory, but can sometimes fail to take
 * grace periods into account.
 *
 * This does not guarantee that the needed grace period will actually
 * start.
 */
void get_state_synchronize_rcu_full(struct rcu_gp_oldstate *rgosp)
{
	struct rcu_node *rnp = rcu_get_root();

	/*
	 * Any prior manipulation of RCU-protected data must happen
	 * before the loads from ->gp_seq and ->expedited_sequence.
	 */
	smp_mb();  /* ^^^ */
	rgosp->rgos_norm = rcu_seq_snap(&rnp->gp_seq);
	rgosp->rgos_exp = rcu_seq_snap(&rcu_state.expedited_sequence);
}
EXPORT_SYMBOL_GPL(get_state_synchronize_rcu_full);

/*
 * Helper function for start_poll_synchronize_rcu() and
 * start_poll_synchronize_rcu_full().
 */
static void start_poll_synchronize_rcu_common(void)
{
	unsigned long flags;
	bool needwake;
	struct rcu_data *rdp;
	struct rcu_node *rnp;

	lockdep_assert_irqs_enabled();
	local_irq_save(flags);
	rdp = this_cpu_ptr(&rcu_data);
	rnp = rdp->mynode;
	raw_spin_lock_rcu_node(rnp); // irqs already disabled.
	// Note it is possible for a grace period to have elapsed between
	// the above call to get_state_synchronize_rcu() and the below call
	// to rcu_seq_snap.  This is OK, the worst that happens is that we
	// get a grace period that no one needed.  These accesses are ordered
	// by smp_mb(), and we are accessing them in the opposite order
	// from which they are updated at grace-period start, as required.
	needwake = rcu_start_this_gp(rnp, rdp, rcu_seq_snap(&rcu_state.gp_seq));
	raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
	if (needwake)
		rcu_gp_kthread_wake();
}

/**
 * start_poll_synchronize_rcu - Snapshot and start RCU grace period
 *
 * Returns a cookie that is used by a later call to cond_synchronize_rcu()
 * or poll_state_synchronize_rcu() to determine whether or not a full
 * grace period has elapsed in the meantime.  If the needed grace period
 * is not already slated to start, notifies RCU core of the need for that
 * grace period.
 *
 * Interrupts must be enabled for the case where it is necessary to awaken
 * the grace-period kthread.
 */
unsigned long start_poll_synchronize_rcu(void)
{
	unsigned long gp_seq = get_state_synchronize_rcu();

	start_poll_synchronize_rcu_common();
	return gp_seq;
}
EXPORT_SYMBOL_GPL(start_poll_synchronize_rcu);

/**
 * start_poll_synchronize_rcu_full - Take a full snapshot and start RCU grace period
 * @rgosp: value from get_state_synchronize_rcu_full() or start_poll_synchronize_rcu_full()
 *
 * Places the normal and expedited grace-period states in *@rgos.  This
 * state value can be passed to a later call to cond_synchronize_rcu_full()
 * or poll_state_synchronize_rcu_full() to determine whether or not a
 * grace period (whether normal or expedited) has elapsed in the meantime.
 * If the needed grace period is not already slated to start, notifies
 * RCU core of the need for that grace period.
 *
 * Interrupts must be enabled for the case where it is necessary to awaken
 * the grace-period kthread.
 */
void start_poll_synchronize_rcu_full(struct rcu_gp_oldstate *rgosp)
{
	get_state_synchronize_rcu_full(rgosp);

	start_poll_synchronize_rcu_common();
}
EXPORT_SYMBOL_GPL(start_poll_synchronize_rcu_full);

/**
 * poll_state_synchronize_rcu - Has the specified RCU grace period completed?
 * @oldstate: value from get_state_synchronize_rcu() or start_poll_synchronize_rcu()
 *
 * If a full RCU grace period has elapsed since the earlier call from
 * which @oldstate was obtained, return @true, otherwise return @false.
 * If @false is returned, it is the caller's responsibility to invoke this
 * function later on until it does return @true.  Alternatively, the caller
 * can explicitly wait for a grace period, for example, by passing @oldstate
 * to either cond_synchronize_rcu() or cond_synchronize_rcu_expedited()
 * on the one hand or by directly invoking either synchronize_rcu() or
 * synchronize_rcu_expedited() on the other.
 *
 * Yes, this function does not take counter wrap into account.
 * But counter wrap is harmless.  If the counter wraps, we have waited for
 * more than a billion grace periods (and way more on a 64-bit system!).
 * Those needing to keep old state values for very long time periods
 * (many hours even on 32-bit systems) should check them occasionally and
 * either refresh them or set a flag indicating that the grace period has
 * completed.  Alternatively, they can use get_completed_synchronize_rcu()
 * to get a guaranteed-completed grace-period state.
 *
 * In addition, because oldstate compresses the grace-period state for
 * both normal and expedited grace periods into a single unsigned long,
 * it can miss a grace period when synchronize_rcu() runs concurrently
 * with synchronize_rcu_expedited().  If this is unacceptable, please
 * instead use the _full() variant of these polling APIs.
 *
 * This function provides the same memory-ordering guarantees that
 * would be provided by a synchronize_rcu() that was invoked at the call
 * to the function that provided @oldstate, and that returned at the end
 * of this function.
 */
bool poll_state_synchronize_rcu(unsigned long oldstate)
{
	if (oldstate == RCU_GET_STATE_COMPLETED ||
	    rcu_seq_done_exact(&rcu_state.gp_seq_polled, oldstate)) {
		smp_mb(); /* Ensure GP ends before subsequent accesses. */
		return true;
	}
	return false;
}
EXPORT_SYMBOL_GPL(poll_state_synchronize_rcu);

/**
 * poll_state_synchronize_rcu_full - Has the specified RCU grace period completed?
 * @rgosp: value from get_state_synchronize_rcu_full() or start_poll_synchronize_rcu_full()
 *
 * If a full RCU grace period has elapsed since the earlier call from
 * which *rgosp was obtained, return @true, otherwise return @false.
 * If @false is returned, it is the caller's responsibility to invoke this
 * function later on until it does return @true.  Alternatively, the caller
 * can explicitly wait for a grace period, for example, by passing @rgosp
 * to cond_synchronize_rcu() or by directly invoking synchronize_rcu().
 *
 * Yes, this function does not take counter wrap into account.
 * But counter wrap is harmless.  If the counter wraps, we have waited
 * for more than a billion grace periods (and way more on a 64-bit
 * system!).  Those needing to keep rcu_gp_oldstate values for very
 * long time periods (many hours even on 32-bit systems) should check
 * them occasionally and either refresh them or set a flag indicating
 * that the grace period has completed.  Alternatively, they can use
 * get_completed_synchronize_rcu_full() to get a guaranteed-completed
 * grace-period state.
 *
 * This function provides the same memory-ordering guarantees that would
 * be provided by a synchronize_rcu() that was invoked at the call to
 * the function that provided @rgosp, and that returned at the end of this
 * function.  And this guarantee requires that the root rcu_node structure's
 * ->gp_seq field be checked instead of that of the rcu_state structure.
 * The problem is that the just-ending grace-period's callbacks can be
 * invoked between the time that the root rcu_node structure's ->gp_seq
 * field is updated and the time that the rcu_state structure's ->gp_seq
 * field is updated.  Therefore, if a single synchronize_rcu() is to
 * cause a subsequent poll_state_synchronize_rcu_full() to return @true,
 * then the root rcu_node structure is the one that needs to be polled.
 */
bool poll_state_synchronize_rcu_full(struct rcu_gp_oldstate *rgosp)
{
	struct rcu_node *rnp = rcu_get_root();

	smp_mb(); // Order against root rcu_node structure grace-period cleanup.
	if (rgosp->rgos_norm == RCU_GET_STATE_COMPLETED ||
	    rcu_seq_done_exact(&rnp->gp_seq, rgosp->rgos_norm) ||
	    rgosp->rgos_exp == RCU_GET_STATE_COMPLETED ||
	    rcu_seq_done_exact(&rcu_state.expedited_sequence, rgosp->rgos_exp)) {
		smp_mb(); /* Ensure GP ends before subsequent accesses. */
		return true;
	}
	return false;
}
EXPORT_SYMBOL_GPL(poll_state_synchronize_rcu_full);

/**
 * cond_synchronize_rcu - Conditionally wait for an RCU grace period
 * @oldstate: value from get_state_synchronize_rcu(), start_poll_synchronize_rcu(), or start_poll_synchronize_rcu_expedited()
 *
 * If a full RCU grace period has elapsed since the earlier call to
 * get_state_synchronize_rcu() or start_poll_synchronize_rcu(), just return.
 * Otherwise, invoke synchronize_rcu() to wait for a full grace period.
 *
 * Yes, this function does not take counter wrap into account.
 * But counter wrap is harmless.  If the counter wraps, we have waited for
 * more than 2 billion grace periods (and way more on a 64-bit system!),
 * so waiting for a couple of additional grace periods should be just fine.
 *
 * This function provides the same memory-ordering guarantees that
 * would be provided by a synchronize_rcu() that was invoked at the call
 * to the function that provided @oldstate and that returned at the end
 * of this function.
 */
void cond_synchronize_rcu(unsigned long oldstate)
{
	if (!poll_state_synchronize_rcu(oldstate))
		synchronize_rcu();
}
EXPORT_SYMBOL_GPL(cond_synchronize_rcu);

/**
 * cond_synchronize_rcu_full - Conditionally wait for an RCU grace period
 * @rgosp: value from get_state_synchronize_rcu_full(), start_poll_synchronize_rcu_full(), or start_poll_synchronize_rcu_expedited_full()
 *
 * If a full RCU grace period has elapsed since the call to
 * get_state_synchronize_rcu_full(), start_poll_synchronize_rcu_full(),
 * or start_poll_synchronize_rcu_expedited_full() from which @rgosp was
 * obtained, just return.  Otherwise, invoke synchronize_rcu() to wait
 * for a full grace period.
 *
 * Yes, this function does not take counter wrap into account.
 * But counter wrap is harmless.  If the counter wraps, we have waited for
 * more than 2 billion grace periods (and way more on a 64-bit system!),
 * so waiting for a couple of additional grace periods should be just fine.
 *
 * This function provides the same memory-ordering guarantees that
 * would be provided by a synchronize_rcu() that was invoked at the call
 * to the function that provided @rgosp and that returned at the end of
 * this function.
 */
void cond_synchronize_rcu_full(struct rcu_gp_oldstate *rgosp)
{
	if (!poll_state_synchronize_rcu_full(rgosp))
		synchronize_rcu();
}
EXPORT_SYMBOL_GPL(cond_synchronize_rcu_full);

/*
 * Check to see if there is any immediate RCU-related work to be done by
 * the current CPU, returning 1 if so and zero otherwise.  The checks are
 * in order of increasing expense: checks that can be carried out against
 * CPU-local state are performed first.  However, we must check for CPU
 * stalls first, else we might not get a chance.
 */
static int rcu_pending(int user)
{
	bool gp_in_progress;
	struct rcu_data *rdp = this_cpu_ptr(&rcu_data);
	struct rcu_node *rnp = rdp->mynode;

	lockdep_assert_irqs_disabled();

	/* Check for CPU stalls, if enabled. */
	check_cpu_stall(rdp);

	/* Does this CPU need a deferred NOCB wakeup? */
	if (rcu_nocb_need_deferred_wakeup(rdp, RCU_NOCB_WAKE))
		return 1;

	/* Is this a nohz_full CPU in userspace or idle?  (Ignore RCU if so.) */
	if ((user || rcu_is_cpu_rrupt_from_idle()) && rcu_nohz_full_cpu())
		return 0;

	/* Is the RCU core waiting for a quiescent state from this CPU? */
	gp_in_progress = rcu_gp_in_progress();
	if (rdp->core_needs_qs && !rdp->cpu_no_qs.b.norm && gp_in_progress)
		return 1;

	/* Does this CPU have callbacks ready to invoke? */
	if (!rcu_rdp_is_offloaded(rdp) &&
	    rcu_segcblist_ready_cbs(&rdp->cblist))
		return 1;

	/* Has RCU gone idle with this CPU needing another grace period? */
	if (!gp_in_progress && rcu_segcblist_is_enabled(&rdp->cblist) &&
	    !rcu_rdp_is_offloaded(rdp) &&
	    !rcu_segcblist_restempty(&rdp->cblist, RCU_NEXT_READY_TAIL))
		return 1;

	/* Have RCU grace period completed or started?  */
	if (rcu_seq_current(&rnp->gp_seq) != rdp->gp_seq ||
	    unlikely(READ_ONCE(rdp->gpwrap))) /* outside lock */
		return 1;

	/* nothing to do */
	return 0;
}

/*
 * Helper function for rcu_barrier() tracing.  If tracing is disabled,
 * the compiler is expected to optimize this away.
 */
static void rcu_barrier_trace(const char *s, int cpu, unsigned long done)
{
	trace_rcu_barrier(rcu_state.name, s, cpu,
			  atomic_read(&rcu_state.barrier_cpu_count), done);
}

/*
 * RCU callback function for rcu_barrier().  If we are last, wake
 * up the task executing rcu_barrier().
 *
 * Note that the value of rcu_state.barrier_sequence must be captured
 * before the atomic_dec_and_test().  Otherwise, if this CPU is not last,
 * other CPUs might count the value down to zero before this CPU gets
 * around to invoking rcu_barrier_trace(), which might result in bogus
 * data from the next instance of rcu_barrier().
 */
static void rcu_barrier_callback(struct rcu_head *rhp)
{
	unsigned long __maybe_unused s = rcu_state.barrier_sequence;

	if (atomic_dec_and_test(&rcu_state.barrier_cpu_count)) {
		rcu_barrier_trace(TPS("LastCB"), -1, s);
		complete(&rcu_state.barrier_completion);
	} else {
		rcu_barrier_trace(TPS("CB"), -1, s);
	}
}

/*
 * If needed, entrain an rcu_barrier() callback on rdp->cblist.
 */
static void rcu_barrier_entrain(struct rcu_data *rdp)
{
	unsigned long gseq = READ_ONCE(rcu_state.barrier_sequence);
	unsigned long lseq = READ_ONCE(rdp->barrier_seq_snap);
	bool wake_nocb = false;
	bool was_alldone = false;

	lockdep_assert_held(&rcu_state.barrier_lock);
	if (rcu_seq_state(lseq) || !rcu_seq_state(gseq) || rcu_seq_ctr(lseq) != rcu_seq_ctr(gseq))
		return;
	rcu_barrier_trace(TPS("IRQ"), -1, rcu_state.barrier_sequence);
	rdp->barrier_head.func = rcu_barrier_callback;
	debug_rcu_head_queue(&rdp->barrier_head);
	rcu_nocb_lock(rdp);
	/*
	 * Flush bypass and wakeup rcuog if we add callbacks to an empty regular
	 * queue. This way we don't wait for bypass timer that can reach seconds
	 * if it's fully lazy.
	 */
	was_alldone = rcu_rdp_is_offloaded(rdp) && !rcu_segcblist_pend_cbs(&rdp->cblist);
	WARN_ON_ONCE(!rcu_nocb_flush_bypass(rdp, NULL, jiffies, false));
	wake_nocb = was_alldone && rcu_segcblist_pend_cbs(&rdp->cblist);
	if (rcu_segcblist_entrain(&rdp->cblist, &rdp->barrier_head)) {
		atomic_inc(&rcu_state.barrier_cpu_count);
	} else {
		debug_rcu_head_unqueue(&rdp->barrier_head);
		rcu_barrier_trace(TPS("IRQNQ"), -1, rcu_state.barrier_sequence);
	}
	rcu_nocb_unlock(rdp);
	if (wake_nocb)
		wake_nocb_gp(rdp, false);
	smp_store_release(&rdp->barrier_seq_snap, gseq);
}

/*
 * Called with preemption disabled, and from cross-cpu IRQ context.
 */
static void rcu_barrier_handler(void *cpu_in)
{
	uintptr_t cpu = (uintptr_t)cpu_in;
	struct rcu_data *rdp = per_cpu_ptr(&rcu_data, cpu);

	lockdep_assert_irqs_disabled();
	WARN_ON_ONCE(cpu != rdp->cpu);
	WARN_ON_ONCE(cpu != smp_processor_id());
	raw_spin_lock(&rcu_state.barrier_lock);
	rcu_barrier_entrain(rdp);
	raw_spin_unlock(&rcu_state.barrier_lock);
}

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
	uintptr_t cpu;
	unsigned long flags;
	unsigned long gseq;
	struct rcu_data *rdp;
	unsigned long s = rcu_seq_snap(&rcu_state.barrier_sequence);

	rcu_barrier_trace(TPS("Begin"), -1, s);

	/* Take mutex to serialize concurrent rcu_barrier() requests. */
	mutex_lock(&rcu_state.barrier_mutex);

	/* Did someone else do our work for us? */
	if (rcu_seq_done(&rcu_state.barrier_sequence, s)) {
		rcu_barrier_trace(TPS("EarlyExit"), -1, rcu_state.barrier_sequence);
		smp_mb(); /* caller's subsequent code after above check. */
		mutex_unlock(&rcu_state.barrier_mutex);
		return;
	}

	/* Mark the start of the barrier operation. */
	raw_spin_lock_irqsave(&rcu_state.barrier_lock, flags);
	rcu_seq_start(&rcu_state.barrier_sequence);
	gseq = rcu_state.barrier_sequence;
	rcu_barrier_trace(TPS("Inc1"), -1, rcu_state.barrier_sequence);

	/*
	 * Initialize the count to two rather than to zero in order
	 * to avoid a too-soon return to zero in case of an immediate
	 * invocation of the just-enqueued callback (or preemption of
	 * this task).  Exclude CPU-hotplug operations to ensure that no
	 * offline non-offloaded CPU has callbacks queued.
	 */
	init_completion(&rcu_state.barrier_completion);
	atomic_set(&rcu_state.barrier_cpu_count, 2);
	raw_spin_unlock_irqrestore(&rcu_state.barrier_lock, flags);

	/*
	 * Force each CPU with callbacks to register a new callback.
	 * When that callback is invoked, we will know that all of the
	 * corresponding CPU's preceding callbacks have been invoked.
	 */
	for_each_possible_cpu(cpu) {
		rdp = per_cpu_ptr(&rcu_data, cpu);
retry:
		if (smp_load_acquire(&rdp->barrier_seq_snap) == gseq)
			continue;
		raw_spin_lock_irqsave(&rcu_state.barrier_lock, flags);
		if (!rcu_segcblist_n_cbs(&rdp->cblist)) {
			WRITE_ONCE(rdp->barrier_seq_snap, gseq);
			raw_spin_unlock_irqrestore(&rcu_state.barrier_lock, flags);
			rcu_barrier_trace(TPS("NQ"), cpu, rcu_state.barrier_sequence);
			continue;
		}
		if (!rcu_rdp_cpu_online(rdp)) {
			rcu_barrier_entrain(rdp);
			WARN_ON_ONCE(READ_ONCE(rdp->barrier_seq_snap) != gseq);
			raw_spin_unlock_irqrestore(&rcu_state.barrier_lock, flags);
			rcu_barrier_trace(TPS("OfflineNoCBQ"), cpu, rcu_state.barrier_sequence);
			continue;
		}
		raw_spin_unlock_irqrestore(&rcu_state.barrier_lock, flags);
		if (smp_call_function_single(cpu, rcu_barrier_handler, (void *)cpu, 1)) {
			schedule_timeout_uninterruptible(1);
			goto retry;
		}
		WARN_ON_ONCE(READ_ONCE(rdp->barrier_seq_snap) != gseq);
		rcu_barrier_trace(TPS("OnlineQ"), cpu, rcu_state.barrier_sequence);
	}

	/*
	 * Now that we have an rcu_barrier_callback() callback on each
	 * CPU, and thus each counted, remove the initial count.
	 */
	if (atomic_sub_and_test(2, &rcu_state.barrier_cpu_count))
		complete(&rcu_state.barrier_completion);

	/* Wait for all rcu_barrier_callback() callbacks to be invoked. */
	wait_for_completion(&rcu_state.barrier_completion);

	/* Mark the end of the barrier operation. */
	rcu_barrier_trace(TPS("Inc2"), -1, rcu_state.barrier_sequence);
	rcu_seq_end(&rcu_state.barrier_sequence);
	gseq = rcu_state.barrier_sequence;
	for_each_possible_cpu(cpu) {
		rdp = per_cpu_ptr(&rcu_data, cpu);

		WRITE_ONCE(rdp->barrier_seq_snap, gseq);
	}

	/* Other rcu_barrier() invocations can now safely proceed. */
	mutex_unlock(&rcu_state.barrier_mutex);
}
EXPORT_SYMBOL_GPL(rcu_barrier);

/*
 * Compute the mask of online CPUs for the specified rcu_node structure.
 * This will not be stable unless the rcu_node structure's ->lock is
 * held, but the bit corresponding to the current CPU will be stable
 * in most contexts.
 */
static unsigned long rcu_rnp_online_cpus(struct rcu_node *rnp)
{
	return READ_ONCE(rnp->qsmaskinitnext);
}

/*
 * Is the CPU corresponding to the specified rcu_data structure online
 * from RCU's perspective?  This perspective is given by that structure's
 * ->qsmaskinitnext field rather than by the global cpu_online_mask.
 */
static bool rcu_rdp_cpu_online(struct rcu_data *rdp)
{
	return !!(rdp->grpmask & rcu_rnp_online_cpus(rdp->mynode));
}

#if defined(CONFIG_PROVE_RCU) && defined(CONFIG_HOTPLUG_CPU)

/*
 * Is the current CPU online as far as RCU is concerned?
 *
 * Disable preemption to avoid false positives that could otherwise
 * happen due to the current CPU number being sampled, this task being
 * preempted, its old CPU being taken offline, resuming on some other CPU,
 * then determining that its old CPU is now offline.
 *
 * Disable checking if in an NMI handler because we cannot safely
 * report errors from NMI handlers anyway.  In addition, it is OK to use
 * RCU on an offline processor during initial boot, hence the check for
 * rcu_scheduler_fully_active.
 */
bool rcu_lockdep_current_cpu_online(void)
{
	struct rcu_data *rdp;
	bool ret = false;

	if (in_nmi() || !rcu_scheduler_fully_active)
		return true;
	preempt_disable_notrace();
	rdp = this_cpu_ptr(&rcu_data);
	/*
	 * Strictly, we care here about the case where the current CPU is
	 * in rcu_cpu_starting() and thus has an excuse for rdp->grpmask
	 * not being up to date. So arch_spin_is_locked() might have a
	 * false positive if it's held by some *other* CPU, but that's
	 * OK because that just means a false *negative* on the warning.
	 */
	if (rcu_rdp_cpu_online(rdp) || arch_spin_is_locked(&rcu_state.ofl_lock))
		ret = true;
	preempt_enable_notrace();
	return ret;
}
EXPORT_SYMBOL_GPL(rcu_lockdep_current_cpu_online);

#endif /* #if defined(CONFIG_PROVE_RCU) && defined(CONFIG_HOTPLUG_CPU) */

// Has rcu_init() been invoked?  This is used (for example) to determine
// whether spinlocks may be acquired safely.
static bool rcu_init_invoked(void)
{
	return !!rcu_state.n_online_cpus;
}

/*
 * Near the end of the offline process.  Trace the fact that this CPU
 * is going offline.
 */
int rcutree_dying_cpu(unsigned int cpu)
{
	bool blkd;
	struct rcu_data *rdp = per_cpu_ptr(&rcu_data, cpu);
	struct rcu_node *rnp = rdp->mynode;

	if (!IS_ENABLED(CONFIG_HOTPLUG_CPU))
		return 0;

	blkd = !!(READ_ONCE(rnp->qsmask) & rdp->grpmask);
	trace_rcu_grace_period(rcu_state.name, READ_ONCE(rnp->gp_seq),
			       blkd ? TPS("cpuofl-bgp") : TPS("cpuofl"));
	return 0;
}

/*
 * All CPUs for the specified rcu_node structure have gone offline,
 * and all tasks that were preempted within an RCU read-side critical
 * section while running on one of those CPUs have since exited their RCU
 * read-side critical section.  Some other CPU is reporting this fact with
 * the specified rcu_node structure's ->lock held and interrupts disabled.
 * This function therefore goes up the tree of rcu_node structures,
 * clearing the corresponding bits in the ->qsmaskinit fields.  Note that
 * the leaf rcu_node structure's ->qsmaskinit field has already been
 * updated.
 *
 * This function does check that the specified rcu_node structure has
 * all CPUs offline and no blocked tasks, so it is OK to invoke it
 * prematurely.  That said, invoking it after the fact will cost you
 * a needless lock acquisition.  So once it has done its work, don't
 * invoke it again.
 */
static void rcu_cleanup_dead_rnp(struct rcu_node *rnp_leaf)
{
	long mask;
	struct rcu_node *rnp = rnp_leaf;

	raw_lockdep_assert_held_rcu_node(rnp_leaf);
	if (!IS_ENABLED(CONFIG_HOTPLUG_CPU) ||
	    WARN_ON_ONCE(rnp_leaf->qsmaskinit) ||
	    WARN_ON_ONCE(rcu_preempt_has_tasks(rnp_leaf)))
		return;
	for (;;) {
		mask = rnp->grpmask;
		rnp = rnp->parent;
		if (!rnp)
			break;
		raw_spin_lock_rcu_node(rnp); /* irqs already disabled. */
		rnp->qsmaskinit &= ~mask;
		/* Between grace periods, so better already be zero! */
		WARN_ON_ONCE(rnp->qsmask);
		if (rnp->qsmaskinit) {
			raw_spin_unlock_rcu_node(rnp);
			/* irqs remain disabled. */
			return;
		}
		raw_spin_unlock_rcu_node(rnp); /* irqs remain disabled. */
	}
}

/*
 * The CPU has been completely removed, and some other CPU is reporting
 * this fact from process context.  Do the remainder of the cleanup.
 * There can only be one CPU hotplug operation at a time, so no need for
 * explicit locking.
 */
int rcutree_dead_cpu(unsigned int cpu)
{
	if (!IS_ENABLED(CONFIG_HOTPLUG_CPU))
		return 0;

	WRITE_ONCE(rcu_state.n_online_cpus, rcu_state.n_online_cpus - 1);
	// Stop-machine done, so allow nohz_full to disable tick.
	tick_dep_clear(TICK_DEP_BIT_RCU);
	return 0;
}

/*
 * Propagate ->qsinitmask bits up the rcu_node tree to account for the
 * first CPU in a given leaf rcu_node structure coming online.  The caller
 * must hold the corresponding leaf rcu_node ->lock with interrupts
 * disabled.
 */
static void rcu_init_new_rnp(struct rcu_node *rnp_leaf)
{
	long mask;
	long oldmask;
	struct rcu_node *rnp = rnp_leaf;

	raw_lockdep_assert_held_rcu_node(rnp_leaf);
	WARN_ON_ONCE(rnp->wait_blkd_tasks);
	for (;;) {
		mask = rnp->grpmask;
		rnp = rnp->parent;
		if (rnp == NULL)
			return;
		raw_spin_lock_rcu_node(rnp); /* Interrupts already disabled. */
		oldmask = rnp->qsmaskinit;
		rnp->qsmaskinit |= mask;
		raw_spin_unlock_rcu_node(rnp); /* Interrupts remain disabled. */
		if (oldmask)
			return;
	}
}

/*
 * Do boot-time initialization of a CPU's per-CPU RCU data.
 */
static void __init
rcu_boot_init_percpu_data(int cpu)
{
	struct context_tracking *ct = this_cpu_ptr(&context_tracking);
	struct rcu_data *rdp = per_cpu_ptr(&rcu_data, cpu);

	/* Set up local state, ensuring consistent view of global state. */
	rdp->grpmask = leaf_node_cpu_bit(rdp->mynode, cpu);
	INIT_WORK(&rdp->strict_work, strict_work_handler);
	WARN_ON_ONCE(ct->dynticks_nesting != 1);
	WARN_ON_ONCE(rcu_dynticks_in_eqs(rcu_dynticks_snap(cpu)));
	rdp->barrier_seq_snap = rcu_state.barrier_sequence;
	rdp->rcu_ofl_gp_seq = rcu_state.gp_seq;
	rdp->rcu_ofl_gp_flags = RCU_GP_CLEANED;
	rdp->rcu_onl_gp_seq = rcu_state.gp_seq;
	rdp->rcu_onl_gp_flags = RCU_GP_CLEANED;
	rdp->last_sched_clock = jiffies;
	rdp->cpu = cpu;
	rcu_boot_init_nocb_percpu_data(rdp);
}

/*
 * Invoked early in the CPU-online process, when pretty much all services
 * are available.  The incoming CPU is not present.
 *
 * Initializes a CPU's per-CPU RCU data.  Note that only one online or
 * offline event can be happening at a given time.  Note also that we can
 * accept some slop in the rsp->gp_seq access due to the fact that this
 * CPU cannot possibly have any non-offloaded RCU callbacks in flight yet.
 * And any offloaded callbacks are being numbered elsewhere.
 */
int rcutree_prepare_cpu(unsigned int cpu)
{
	unsigned long flags;
	struct context_tracking *ct = per_cpu_ptr(&context_tracking, cpu);
	struct rcu_data *rdp = per_cpu_ptr(&rcu_data, cpu);
	struct rcu_node *rnp = rcu_get_root();

	/* Set up local state, ensuring consistent view of global state. */
	raw_spin_lock_irqsave_rcu_node(rnp, flags);
	rdp->qlen_last_fqs_check = 0;
	rdp->n_force_qs_snap = READ_ONCE(rcu_state.n_force_qs);
	rdp->blimit = blimit;
	ct->dynticks_nesting = 1;	/* CPU not up, no tearing. */
	raw_spin_unlock_rcu_node(rnp);		/* irqs remain disabled. */

	/*
	 * Only non-NOCB CPUs that didn't have early-boot callbacks need to be
	 * (re-)initialized.
	 */
	if (!rcu_segcblist_is_enabled(&rdp->cblist))
		rcu_segcblist_init(&rdp->cblist);  /* Re-enable callbacks. */

	/*
	 * Add CPU to leaf rcu_node pending-online bitmask.  Any needed
	 * propagation up the rcu_node tree will happen at the beginning
	 * of the next grace period.
	 */
	rnp = rdp->mynode;
	raw_spin_lock_rcu_node(rnp);		/* irqs already disabled. */
	rdp->gp_seq = READ_ONCE(rnp->gp_seq);
	rdp->gp_seq_needed = rdp->gp_seq;
	rdp->cpu_no_qs.b.norm = true;
	rdp->core_needs_qs = false;
	rdp->rcu_iw_pending = false;
	rdp->rcu_iw = IRQ_WORK_INIT_HARD(rcu_iw_handler);
	rdp->rcu_iw_gp_seq = rdp->gp_seq - 1;
	trace_rcu_grace_period(rcu_state.name, rdp->gp_seq, TPS("cpuonl"));
	raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
	rcu_spawn_one_boost_kthread(rnp);
	rcu_spawn_cpu_nocb_kthread(cpu);
	WRITE_ONCE(rcu_state.n_online_cpus, rcu_state.n_online_cpus + 1);

	return 0;
}

/*
 * Update RCU priority boot kthread affinity for CPU-hotplug changes.
 */
static void rcutree_affinity_setting(unsigned int cpu, int outgoing)
{
	struct rcu_data *rdp = per_cpu_ptr(&rcu_data, cpu);

	rcu_boost_kthread_setaffinity(rdp->mynode, outgoing);
}

/*
 * Has the specified (known valid) CPU ever been fully online?
 */
bool rcu_cpu_beenfullyonline(int cpu)
{
	struct rcu_data *rdp = per_cpu_ptr(&rcu_data, cpu);

	return smp_load_acquire(&rdp->beenonline);
}

/*
 * Near the end of the CPU-online process.  Pretty much all services
 * enabled, and the CPU is now very much alive.
 */
int rcutree_online_cpu(unsigned int cpu)
{
	unsigned long flags;
	struct rcu_data *rdp;
	struct rcu_node *rnp;

	rdp = per_cpu_ptr(&rcu_data, cpu);
	rnp = rdp->mynode;
	raw_spin_lock_irqsave_rcu_node(rnp, flags);
	rnp->ffmask |= rdp->grpmask;
	raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
	if (rcu_scheduler_active == RCU_SCHEDULER_INACTIVE)
		return 0; /* Too early in boot for scheduler work. */
	sync_sched_exp_online_cleanup(cpu);
	rcutree_affinity_setting(cpu, -1);

	// Stop-machine done, so allow nohz_full to disable tick.
	tick_dep_clear(TICK_DEP_BIT_RCU);
	return 0;
}

/*
 * Near the beginning of the process.  The CPU is still very much alive
 * with pretty much all services enabled.
 */
int rcutree_offline_cpu(unsigned int cpu)
{
	unsigned long flags;
	struct rcu_data *rdp;
	struct rcu_node *rnp;

	rdp = per_cpu_ptr(&rcu_data, cpu);
	rnp = rdp->mynode;
	raw_spin_lock_irqsave_rcu_node(rnp, flags);
	rnp->ffmask &= ~rdp->grpmask;
	raw_spin_unlock_irqrestore_rcu_node(rnp, flags);

	rcutree_affinity_setting(cpu, cpu);

	// nohz_full CPUs need the tick for stop-machine to work quickly
	tick_dep_set(TICK_DEP_BIT_RCU);
	return 0;
}

/*
 * Mark the specified CPU as being online so that subsequent grace periods
 * (both expedited and normal) will wait on it.  Note that this means that
 * incoming CPUs are not allowed to use RCU read-side critical sections
 * until this function is called.  Failing to observe this restriction
 * will result in lockdep splats.
 *
 * Note that this function is special in that it is invoked directly
 * from the incoming CPU rather than from the cpuhp_step mechanism.
 * This is because this function must be invoked at a precise location.
 * This incoming CPU must not have enabled interrupts yet.
 */
void rcu_cpu_starting(unsigned int cpu)
{
	unsigned long mask;
	struct rcu_data *rdp;
	struct rcu_node *rnp;
	bool newcpu;

	lockdep_assert_irqs_disabled();
	rdp = per_cpu_ptr(&rcu_data, cpu);
	if (rdp->cpu_started)
		return;
	rdp->cpu_started = true;

	rnp = rdp->mynode;
	mask = rdp->grpmask;
	arch_spin_lock(&rcu_state.ofl_lock);
	rcu_dynticks_eqs_online();
	raw_spin_lock(&rcu_state.barrier_lock);
	raw_spin_lock_rcu_node(rnp);
	WRITE_ONCE(rnp->qsmaskinitnext, rnp->qsmaskinitnext | mask);
	raw_spin_unlock(&rcu_state.barrier_lock);
	newcpu = !(rnp->expmaskinitnext & mask);
	rnp->expmaskinitnext |= mask;
	/* Allow lockless access for expedited grace periods. */
	smp_store_release(&rcu_state.ncpus, rcu_state.ncpus + newcpu); /* ^^^ */
	ASSERT_EXCLUSIVE_WRITER(rcu_state.ncpus);
	rcu_gpnum_ovf(rnp, rdp); /* Offline-induced counter wrap? */
	rdp->rcu_onl_gp_seq = READ_ONCE(rcu_state.gp_seq);
	rdp->rcu_onl_gp_flags = READ_ONCE(rcu_state.gp_flags);

	/* An incoming CPU should never be blocking a grace period. */
	if (WARN_ON_ONCE(rnp->qsmask & mask)) { /* RCU waiting on incoming CPU? */
		/* rcu_report_qs_rnp() *really* wants some flags to restore */
		unsigned long flags;

		local_irq_save(flags);
		rcu_disable_urgency_upon_qs(rdp);
		/* Report QS -after- changing ->qsmaskinitnext! */
		rcu_report_qs_rnp(mask, rnp, rnp->gp_seq, flags);
	} else {
		raw_spin_unlock_rcu_node(rnp);
	}
	arch_spin_unlock(&rcu_state.ofl_lock);
	smp_store_release(&rdp->beenonline, true);
	smp_mb(); /* Ensure RCU read-side usage follows above initialization. */
}

/*
 * The outgoing function has no further need of RCU, so remove it from
 * the rcu_node tree's ->qsmaskinitnext bit masks.
 *
 * Note that this function is special in that it is invoked directly
 * from the outgoing CPU rather than from the cpuhp_step mechanism.
 * This is because this function must be invoked at a precise location.
 */
void rcu_report_dead(unsigned int cpu)
{
	unsigned long flags, seq_flags;
	unsigned long mask;
	struct rcu_data *rdp = per_cpu_ptr(&rcu_data, cpu);
	struct rcu_node *rnp = rdp->mynode;  /* Outgoing CPU's rdp & rnp. */

	// Do any dangling deferred wakeups.
	do_nocb_deferred_wakeup(rdp);

	rcu_preempt_deferred_qs(current);

	/* Remove outgoing CPU from mask in the leaf rcu_node structure. */
	mask = rdp->grpmask;
	local_irq_save(seq_flags);
	arch_spin_lock(&rcu_state.ofl_lock);
	raw_spin_lock_irqsave_rcu_node(rnp, flags); /* Enforce GP memory-order guarantee. */
	rdp->rcu_ofl_gp_seq = READ_ONCE(rcu_state.gp_seq);
	rdp->rcu_ofl_gp_flags = READ_ONCE(rcu_state.gp_flags);
	if (rnp->qsmask & mask) { /* RCU waiting on outgoing CPU? */
		/* Report quiescent state -before- changing ->qsmaskinitnext! */
		rcu_disable_urgency_upon_qs(rdp);
		rcu_report_qs_rnp(mask, rnp, rnp->gp_seq, flags);
		raw_spin_lock_irqsave_rcu_node(rnp, flags);
	}
	WRITE_ONCE(rnp->qsmaskinitnext, rnp->qsmaskinitnext & ~mask);
	raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
	arch_spin_unlock(&rcu_state.ofl_lock);
	local_irq_restore(seq_flags);

	rdp->cpu_started = false;
}

#ifdef CONFIG_HOTPLUG_CPU
/*
 * The outgoing CPU has just passed through the dying-idle state, and we
 * are being invoked from the CPU that was IPIed to continue the offline
 * operation.  Migrate the outgoing CPU's callbacks to the current CPU.
 */
void rcutree_migrate_callbacks(int cpu)
{
	unsigned long flags;
	struct rcu_data *my_rdp;
	struct rcu_node *my_rnp;
	struct rcu_data *rdp = per_cpu_ptr(&rcu_data, cpu);
	bool needwake;

	if (rcu_rdp_is_offloaded(rdp) ||
	    rcu_segcblist_empty(&rdp->cblist))
		return;  /* No callbacks to migrate. */

	raw_spin_lock_irqsave(&rcu_state.barrier_lock, flags);
	WARN_ON_ONCE(rcu_rdp_cpu_online(rdp));
	rcu_barrier_entrain(rdp);
	my_rdp = this_cpu_ptr(&rcu_data);
	my_rnp = my_rdp->mynode;
	rcu_nocb_lock(my_rdp); /* irqs already disabled. */
	WARN_ON_ONCE(!rcu_nocb_flush_bypass(my_rdp, NULL, jiffies, false));
	raw_spin_lock_rcu_node(my_rnp); /* irqs already disabled. */
	/* Leverage recent GPs and set GP for new callbacks. */
	needwake = rcu_advance_cbs(my_rnp, rdp) ||
		   rcu_advance_cbs(my_rnp, my_rdp);
	rcu_segcblist_merge(&my_rdp->cblist, &rdp->cblist);
	raw_spin_unlock(&rcu_state.barrier_lock); /* irqs remain disabled. */
	needwake = needwake || rcu_advance_cbs(my_rnp, my_rdp);
	rcu_segcblist_disable(&rdp->cblist);
	WARN_ON_ONCE(rcu_segcblist_empty(&my_rdp->cblist) != !rcu_segcblist_n_cbs(&my_rdp->cblist));
	check_cb_ovld_locked(my_rdp, my_rnp);
	if (rcu_rdp_is_offloaded(my_rdp)) {
		raw_spin_unlock_rcu_node(my_rnp); /* irqs remain disabled. */
		__call_rcu_nocb_wake(my_rdp, true, flags);
	} else {
		rcu_nocb_unlock(my_rdp); /* irqs remain disabled. */
		raw_spin_unlock_irqrestore_rcu_node(my_rnp, flags);
	}
	if (needwake)
		rcu_gp_kthread_wake();
	lockdep_assert_irqs_enabled();
	WARN_ONCE(rcu_segcblist_n_cbs(&rdp->cblist) != 0 ||
		  !rcu_segcblist_empty(&rdp->cblist),
		  "rcu_cleanup_dead_cpu: Callbacks on offline CPU %d: qlen=%lu, 1stCB=%p\n",
		  cpu, rcu_segcblist_n_cbs(&rdp->cblist),
		  rcu_segcblist_first_cb(&rdp->cblist));
}
#endif

/*
 * On non-huge systems, use expedited RCU grace periods to make suspend
 * and hibernation run faster.
 */
static int rcu_pm_notify(struct notifier_block *self,
			 unsigned long action, void *hcpu)
{
	switch (action) {
	case PM_HIBERNATION_PREPARE:
	case PM_SUSPEND_PREPARE:
		rcu_async_hurry();
		rcu_expedite_gp();
		break;
	case PM_POST_HIBERNATION:
	case PM_POST_SUSPEND:
		rcu_unexpedite_gp();
		rcu_async_relax();
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

#ifdef CONFIG_RCU_EXP_KTHREAD
struct kthread_worker *rcu_exp_gp_kworker;
struct kthread_worker *rcu_exp_par_gp_kworker;

static void __init rcu_start_exp_gp_kworkers(void)
{
	const char *par_gp_kworker_name = "rcu_exp_par_gp_kthread_worker";
	const char *gp_kworker_name = "rcu_exp_gp_kthread_worker";
	struct sched_param param = { .sched_priority = kthread_prio };

	rcu_exp_gp_kworker = kthread_create_worker(0, gp_kworker_name);
	if (IS_ERR_OR_NULL(rcu_exp_gp_kworker)) {
		pr_err("Failed to create %s!\n", gp_kworker_name);
		return;
	}

	rcu_exp_par_gp_kworker = kthread_create_worker(0, par_gp_kworker_name);
	if (IS_ERR_OR_NULL(rcu_exp_par_gp_kworker)) {
		pr_err("Failed to create %s!\n", par_gp_kworker_name);
		kthread_destroy_worker(rcu_exp_gp_kworker);
		return;
	}

	sched_setscheduler_nocheck(rcu_exp_gp_kworker->task, SCHED_FIFO, &param);
	sched_setscheduler_nocheck(rcu_exp_par_gp_kworker->task, SCHED_FIFO,
				   &param);
}

static inline void rcu_alloc_par_gp_wq(void)
{
}
#else /* !CONFIG_RCU_EXP_KTHREAD */
struct workqueue_struct *rcu_par_gp_wq;

static void __init rcu_start_exp_gp_kworkers(void)
{
}

static inline void rcu_alloc_par_gp_wq(void)
{
	rcu_par_gp_wq = alloc_workqueue("rcu_par_gp", WQ_MEM_RECLAIM, 0);
	WARN_ON(!rcu_par_gp_wq);
}
#endif /* CONFIG_RCU_EXP_KTHREAD */

/*
 * Spawn the kthreads that handle RCU's grace periods.
 */
static int __init rcu_spawn_gp_kthread(void)
{
	unsigned long flags;
	struct rcu_node *rnp;
	struct sched_param sp;
	struct task_struct *t;
	struct rcu_data *rdp = this_cpu_ptr(&rcu_data);

	rcu_scheduler_fully_active = 1;
	t = kthread_create(rcu_gp_kthread, NULL, "%s", rcu_state.name);
	if (WARN_ONCE(IS_ERR(t), "%s: Could not start grace-period kthread, OOM is now expected behavior\n", __func__))
		return 0;
	if (kthread_prio) {
		sp.sched_priority = kthread_prio;
		sched_setscheduler_nocheck(t, SCHED_FIFO, &sp);
	}
	rnp = rcu_get_root();
	raw_spin_lock_irqsave_rcu_node(rnp, flags);
	WRITE_ONCE(rcu_state.gp_activity, jiffies);
	WRITE_ONCE(rcu_state.gp_req_activity, jiffies);
	// Reset .gp_activity and .gp_req_activity before setting .gp_kthread.
	smp_store_release(&rcu_state.gp_kthread, t);  /* ^^^ */
	raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
	wake_up_process(t);
	/* This is a pre-SMP initcall, we expect a single CPU */
	WARN_ON(num_online_cpus() > 1);
	/*
	 * Those kthreads couldn't be created on rcu_init() -> rcutree_prepare_cpu()
	 * due to rcu_scheduler_fully_active.
	 */
	rcu_spawn_cpu_nocb_kthread(smp_processor_id());
	rcu_spawn_one_boost_kthread(rdp->mynode);
	rcu_spawn_core_kthreads();
	/* Create kthread worker for expedited GPs */
	rcu_start_exp_gp_kworkers();
	return 0;
}
early_initcall(rcu_spawn_gp_kthread);

/*
 * This function is invoked towards the end of the scheduler's
 * initialization process.  Before this is called, the idle task might
 * contain synchronous grace-period primitives (during which time, this idle
 * task is booting the system, and such primitives are no-ops).  After this
 * function is called, any synchronous grace-period primitives are run as
 * expedited, with the requesting task driving the grace period forward.
 * A later core_initcall() rcu_set_runtime_mode() will switch to full
 * runtime RCU functionality.
 */
void rcu_scheduler_starting(void)
{
	unsigned long flags;
	struct rcu_node *rnp;

	WARN_ON(num_online_cpus() != 1);
	WARN_ON(nr_context_switches() > 0);
	rcu_test_sync_prims();

	// Fix up the ->gp_seq counters.
	local_irq_save(flags);
	rcu_for_each_node_breadth_first(rnp)
		rnp->gp_seq_needed = rnp->gp_seq = rcu_state.gp_seq;
	local_irq_restore(flags);

	// Switch out of early boot mode.
	rcu_scheduler_active = RCU_SCHEDULER_INIT;
	rcu_test_sync_prims();
}

/*
 * Helper function for rcu_init() that initializes the rcu_state structure.
 */
static void __init rcu_init_one(void)
{
	static const char * const buf[] = RCU_NODE_NAME_INIT;
	static const char * const fqs[] = RCU_FQS_NAME_INIT;
	static struct lock_class_key rcu_node_class[RCU_NUM_LVLS];
	static struct lock_class_key rcu_fqs_class[RCU_NUM_LVLS];

	int levelspread[RCU_NUM_LVLS];		/* kids/node in each level. */
	int cpustride = 1;
	int i;
	int j;
	struct rcu_node *rnp;

	BUILD_BUG_ON(RCU_NUM_LVLS > ARRAY_SIZE(buf));  /* Fix buf[] init! */

	/* Silence gcc 4.8 false positive about array index out of range. */
	if (rcu_num_lvls <= 0 || rcu_num_lvls > RCU_NUM_LVLS)
		panic("rcu_init_one: rcu_num_lvls out of range");

	/* Initialize the level-tracking arrays. */

	for (i = 1; i < rcu_num_lvls; i++)
		rcu_state.level[i] =
			rcu_state.level[i - 1] + num_rcu_lvl[i - 1];
	rcu_init_levelspread(levelspread, num_rcu_lvl);

	/* Initialize the elements themselves, starting from the leaves. */

	for (i = rcu_num_lvls - 1; i >= 0; i--) {
		cpustride *= levelspread[i];
		rnp = rcu_state.level[i];
		for (j = 0; j < num_rcu_lvl[i]; j++, rnp++) {
			raw_spin_lock_init(&ACCESS_PRIVATE(rnp, lock));
			lockdep_set_class_and_name(&ACCESS_PRIVATE(rnp, lock),
						   &rcu_node_class[i], buf[i]);
			raw_spin_lock_init(&rnp->fqslock);
			lockdep_set_class_and_name(&rnp->fqslock,
						   &rcu_fqs_class[i], fqs[i]);
			rnp->gp_seq = rcu_state.gp_seq;
			rnp->gp_seq_needed = rcu_state.gp_seq;
			rnp->completedqs = rcu_state.gp_seq;
			rnp->qsmask = 0;
			rnp->qsmaskinit = 0;
			rnp->grplo = j * cpustride;
			rnp->grphi = (j + 1) * cpustride - 1;
			if (rnp->grphi >= nr_cpu_ids)
				rnp->grphi = nr_cpu_ids - 1;
			if (i == 0) {
				rnp->grpnum = 0;
				rnp->grpmask = 0;
				rnp->parent = NULL;
			} else {
				rnp->grpnum = j % levelspread[i - 1];
				rnp->grpmask = BIT(rnp->grpnum);
				rnp->parent = rcu_state.level[i - 1] +
					      j / levelspread[i - 1];
			}
			rnp->level = i;
			INIT_LIST_HEAD(&rnp->blkd_tasks);
			rcu_init_one_nocb(rnp);
			init_waitqueue_head(&rnp->exp_wq[0]);
			init_waitqueue_head(&rnp->exp_wq[1]);
			init_waitqueue_head(&rnp->exp_wq[2]);
			init_waitqueue_head(&rnp->exp_wq[3]);
			spin_lock_init(&rnp->exp_lock);
			mutex_init(&rnp->boost_kthread_mutex);
			raw_spin_lock_init(&rnp->exp_poll_lock);
			rnp->exp_seq_poll_rq = RCU_GET_STATE_COMPLETED;
			INIT_WORK(&rnp->exp_poll_wq, sync_rcu_do_polled_gp);
		}
	}

	init_swait_queue_head(&rcu_state.gp_wq);
	init_swait_queue_head(&rcu_state.expedited_wq);
	rnp = rcu_first_leaf_node();
	for_each_possible_cpu(i) {
		while (i > rnp->grphi)
			rnp++;
		per_cpu_ptr(&rcu_data, i)->mynode = rnp;
		rcu_boot_init_percpu_data(i);
	}
}

/*
 * Force priority from the kernel command-line into range.
 */
static void __init sanitize_kthread_prio(void)
{
	int kthread_prio_in = kthread_prio;

	if (IS_ENABLED(CONFIG_RCU_BOOST) && kthread_prio < 2
	    && IS_BUILTIN(CONFIG_RCU_TORTURE_TEST))
		kthread_prio = 2;
	else if (IS_ENABLED(CONFIG_RCU_BOOST) && kthread_prio < 1)
		kthread_prio = 1;
	else if (kthread_prio < 0)
		kthread_prio = 0;
	else if (kthread_prio > 99)
		kthread_prio = 99;

	if (kthread_prio != kthread_prio_in)
		pr_alert("%s: Limited prio to %d from %d\n",
			 __func__, kthread_prio, kthread_prio_in);
}

/*
 * Compute the rcu_node tree geometry from kernel parameters.  This cannot
 * replace the definitions in tree.h because those are needed to size
 * the ->node array in the rcu_state structure.
 */
void rcu_init_geometry(void)
{
	ulong d;
	int i;
	static unsigned long old_nr_cpu_ids;
	int rcu_capacity[RCU_NUM_LVLS];
	static bool initialized;

	if (initialized) {
		/*
		 * Warn if setup_nr_cpu_ids() had not yet been invoked,
		 * unless nr_cpus_ids == NR_CPUS, in which case who cares?
		 */
		WARN_ON_ONCE(old_nr_cpu_ids != nr_cpu_ids);
		return;
	}

	old_nr_cpu_ids = nr_cpu_ids;
	initialized = true;

	/*
	 * Initialize any unspecified boot parameters.
	 * The default values of jiffies_till_first_fqs and
	 * jiffies_till_next_fqs are set to the RCU_JIFFIES_TILL_FORCE_QS
	 * value, which is a function of HZ, then adding one for each
	 * RCU_JIFFIES_FQS_DIV CPUs that might be on the system.
	 */
	d = RCU_JIFFIES_TILL_FORCE_QS + nr_cpu_ids / RCU_JIFFIES_FQS_DIV;
	if (jiffies_till_first_fqs == ULONG_MAX)
		jiffies_till_first_fqs = d;
	if (jiffies_till_next_fqs == ULONG_MAX)
		jiffies_till_next_fqs = d;
	adjust_jiffies_till_sched_qs();

	/* If the compile-time values are accurate, just leave. */
	if (rcu_fanout_leaf == RCU_FANOUT_LEAF &&
	    nr_cpu_ids == NR_CPUS)
		return;
	pr_info("Adjusting geometry for rcu_fanout_leaf=%d, nr_cpu_ids=%u\n",
		rcu_fanout_leaf, nr_cpu_ids);

	/*
	 * The boot-time rcu_fanout_leaf parameter must be at least two
	 * and cannot exceed the number of bits in the rcu_node masks.
	 * Complain and fall back to the compile-time values if this
	 * limit is exceeded.
	 */
	if (rcu_fanout_leaf < 2 ||
	    rcu_fanout_leaf > sizeof(unsigned long) * 8) {
		rcu_fanout_leaf = RCU_FANOUT_LEAF;
		WARN_ON(1);
		return;
	}

	/*
	 * Compute number of nodes that can be handled an rcu_node tree
	 * with the given number of levels.
	 */
	rcu_capacity[0] = rcu_fanout_leaf;
	for (i = 1; i < RCU_NUM_LVLS; i++)
		rcu_capacity[i] = rcu_capacity[i - 1] * RCU_FANOUT;

	/*
	 * The tree must be able to accommodate the configured number of CPUs.
	 * If this limit is exceeded, fall back to the compile-time values.
	 */
	if (nr_cpu_ids > rcu_capacity[RCU_NUM_LVLS - 1]) {
		rcu_fanout_leaf = RCU_FANOUT_LEAF;
		WARN_ON(1);
		return;
	}

	/* Calculate the number of levels in the tree. */
	for (i = 0; nr_cpu_ids > rcu_capacity[i]; i++) {
	}
	rcu_num_lvls = i + 1;

	/* Calculate the number of rcu_nodes at each level of the tree. */
	for (i = 0; i < rcu_num_lvls; i++) {
		int cap = rcu_capacity[(rcu_num_lvls - 1) - i];
		num_rcu_lvl[i] = DIV_ROUND_UP(nr_cpu_ids, cap);
	}

	/* Calculate the total number of rcu_node structures. */
	rcu_num_nodes = 0;
	for (i = 0; i < rcu_num_lvls; i++)
		rcu_num_nodes += num_rcu_lvl[i];
}

/*
 * Dump out the structure of the rcu_node combining tree associated
 * with the rcu_state structure.
 */
static void __init rcu_dump_rcu_node_tree(void)
{
	int level = 0;
	struct rcu_node *rnp;

	pr_info("rcu_node tree layout dump\n");
	pr_info(" ");
	rcu_for_each_node_breadth_first(rnp) {
		if (rnp->level != level) {
			pr_cont("\n");
			pr_info(" ");
			level = rnp->level;
		}
		pr_cont("%d:%d ^%d  ", rnp->grplo, rnp->grphi, rnp->grpnum);
	}
	pr_cont("\n");
}

struct workqueue_struct *rcu_gp_wq;

static void __init kfree_rcu_batch_init(void)
{
	int cpu;
	int i, j;

	/* Clamp it to [0:100] seconds interval. */
	if (rcu_delay_page_cache_fill_msec < 0 ||
		rcu_delay_page_cache_fill_msec > 100 * MSEC_PER_SEC) {

		rcu_delay_page_cache_fill_msec =
			clamp(rcu_delay_page_cache_fill_msec, 0,
				(int) (100 * MSEC_PER_SEC));

		pr_info("Adjusting rcutree.rcu_delay_page_cache_fill_msec to %d ms.\n",
			rcu_delay_page_cache_fill_msec);
	}

	for_each_possible_cpu(cpu) {
		struct kfree_rcu_cpu *krcp = per_cpu_ptr(&krc, cpu);

		for (i = 0; i < KFREE_N_BATCHES; i++) {
			INIT_RCU_WORK(&krcp->krw_arr[i].rcu_work, kfree_rcu_work);
			krcp->krw_arr[i].krcp = krcp;

			for (j = 0; j < FREE_N_CHANNELS; j++)
				INIT_LIST_HEAD(&krcp->krw_arr[i].bulk_head_free[j]);
		}

		for (i = 0; i < FREE_N_CHANNELS; i++)
			INIT_LIST_HEAD(&krcp->bulk_head[i]);

		INIT_DELAYED_WORK(&krcp->monitor_work, kfree_rcu_monitor);
		INIT_DELAYED_WORK(&krcp->page_cache_work, fill_page_cache_func);
		krcp->initialized = true;
	}
	if (register_shrinker(&kfree_rcu_shrinker, "rcu-kfree"))
		pr_err("Failed to register kfree_rcu() shrinker!\n");
}

void __init rcu_init(void)
{
	int cpu = smp_processor_id();

	rcu_early_boot_tests();

	kfree_rcu_batch_init();
	rcu_bootup_announce();
	sanitize_kthread_prio();
	rcu_init_geometry();
	rcu_init_one();
	if (dump_tree)
		rcu_dump_rcu_node_tree();
	if (use_softirq)
		open_softirq(RCU_SOFTIRQ, rcu_core_si);

	/*
	 * We don't need protection against CPU-hotplug here because
	 * this is called early in boot, before either interrupts
	 * or the scheduler are operational.
	 */
	pm_notifier(rcu_pm_notify, 0);
	WARN_ON(num_online_cpus() > 1); // Only one CPU this early in boot.
	rcutree_prepare_cpu(cpu);
	rcu_cpu_starting(cpu);
	rcutree_online_cpu(cpu);

	/* Create workqueue for Tree SRCU and for expedited GPs. */
	rcu_gp_wq = alloc_workqueue("rcu_gp", WQ_MEM_RECLAIM, 0);
	WARN_ON(!rcu_gp_wq);
	rcu_alloc_par_gp_wq();

	/* Fill in default value for rcutree.qovld boot parameter. */
	/* -After- the rcu_node ->lock fields are initialized! */
	if (qovld < 0)
		qovld_calc = DEFAULT_RCU_QOVLD_MULT * qhimark;
	else
		qovld_calc = qovld;

	// Kick-start in case any polled grace periods started early.
	(void)start_poll_synchronize_rcu_expedited();

	rcu_test_sync_prims();
}

#include "tree_stall.h"
#include "tree_exp.h"
#include "tree_nocb.h"
#include "tree_plugin.h"
