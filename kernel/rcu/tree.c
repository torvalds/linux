/*
 * Read-Copy Update mechanism for mutual exclusion
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
 * Copyright IBM Corporation, 2008
 *
 * Authors: Dipankar Sarma <dipankar@in.ibm.com>
 *	    Manfred Spraul <manfred@colorfullife.com>
 *	    Paul E. McKenney <paulmck@linux.vnet.ibm.com> Hierarchical version
 *
 * Based on the original work by Paul McKenney <paulmck@us.ibm.com>
 * and inputs from Rusty Russell, Andrea Arcangeli and Andi Kleen.
 *
 * For detailed explanation of Read-Copy Update mechanism see -
 *	Documentation/RCU
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/smp.h>
#include <linux/rcupdate.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/nmi.h>
#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/export.h>
#include <linux/completion.h>
#include <linux/moduleparam.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/mutex.h>
#include <linux/time.h>
#include <linux/kernel_stat.h>
#include <linux/wait.h>
#include <linux/kthread.h>
#include <linux/prefetch.h>
#include <linux/delay.h>
#include <linux/stop_machine.h>
#include <linux/random.h>
#include <linux/ftrace_event.h>
#include <linux/suspend.h>

#include "tree.h"
#include "rcu.h"

MODULE_ALIAS("rcutree");
#ifdef MODULE_PARAM_PREFIX
#undef MODULE_PARAM_PREFIX
#endif
#define MODULE_PARAM_PREFIX "rcutree."

/* Data structures. */

static struct lock_class_key rcu_node_class[RCU_NUM_LVLS];
static struct lock_class_key rcu_fqs_class[RCU_NUM_LVLS];

/*
 * In order to export the rcu_state name to the tracing tools, it
 * needs to be added in the __tracepoint_string section.
 * This requires defining a separate variable tp_<sname>_varname
 * that points to the string being used, and this will allow
 * the tracing userspace tools to be able to decipher the string
 * address to the matching string.
 */
#define RCU_STATE_INITIALIZER(sname, sabbr, cr) \
static char sname##_varname[] = #sname; \
static const char *tp_##sname##_varname __used __tracepoint_string = sname##_varname; \
struct rcu_state sname##_state = { \
	.level = { &sname##_state.node[0] }, \
	.call = cr, \
	.fqs_state = RCU_GP_IDLE, \
	.gpnum = 0UL - 300UL, \
	.completed = 0UL - 300UL, \
	.orphan_lock = __RAW_SPIN_LOCK_UNLOCKED(&sname##_state.orphan_lock), \
	.orphan_nxttail = &sname##_state.orphan_nxtlist, \
	.orphan_donetail = &sname##_state.orphan_donelist, \
	.barrier_mutex = __MUTEX_INITIALIZER(sname##_state.barrier_mutex), \
	.onoff_mutex = __MUTEX_INITIALIZER(sname##_state.onoff_mutex), \
	.name = sname##_varname, \
	.abbr = sabbr, \
}; \
DEFINE_PER_CPU(struct rcu_data, sname##_data)

RCU_STATE_INITIALIZER(rcu_sched, 's', call_rcu_sched);
RCU_STATE_INITIALIZER(rcu_bh, 'b', call_rcu_bh);

static struct rcu_state *rcu_state_p;
LIST_HEAD(rcu_struct_flavors);

/* Increase (but not decrease) the CONFIG_RCU_FANOUT_LEAF at boot time. */
static int rcu_fanout_leaf = CONFIG_RCU_FANOUT_LEAF;
module_param(rcu_fanout_leaf, int, 0444);
int rcu_num_lvls __read_mostly = RCU_NUM_LVLS;
static int num_rcu_lvl[] = {  /* Number of rcu_nodes at specified level. */
	NUM_RCU_LVL_0,
	NUM_RCU_LVL_1,
	NUM_RCU_LVL_2,
	NUM_RCU_LVL_3,
	NUM_RCU_LVL_4,
};
int rcu_num_nodes __read_mostly = NUM_RCU_NODES; /* Total # rcu_nodes in use. */

/*
 * The rcu_scheduler_active variable transitions from zero to one just
 * before the first task is spawned.  So when this variable is zero, RCU
 * can assume that there is but one task, allowing RCU to (for example)
 * optimize synchronize_sched() to a simple barrier().  When this variable
 * is one, RCU must actually do all the hard work required to detect real
 * grace periods.  This variable is also used to suppress boot-time false
 * positives from lockdep-RCU error checking.
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

#ifdef CONFIG_RCU_BOOST

/*
 * Control variables for per-CPU and per-rcu_node kthreads.  These
 * handle all flavors of RCU.
 */
static DEFINE_PER_CPU(struct task_struct *, rcu_cpu_kthread_task);
DEFINE_PER_CPU(unsigned int, rcu_cpu_kthread_status);
DEFINE_PER_CPU(unsigned int, rcu_cpu_kthread_loops);
DEFINE_PER_CPU(char, rcu_cpu_has_work);

#endif /* #ifdef CONFIG_RCU_BOOST */

static void rcu_boost_kthread_setaffinity(struct rcu_node *rnp, int outgoingcpu);
static void invoke_rcu_core(void);
static void invoke_rcu_callbacks(struct rcu_state *rsp, struct rcu_data *rdp);

/*
 * Track the rcutorture test sequence number and the update version
 * number within a given test.  The rcutorture_testseq is incremented
 * on every rcutorture module load and unload, so has an odd value
 * when a test is running.  The rcutorture_vernum is set to zero
 * when rcutorture starts and is incremented on each rcutorture update.
 * These variables enable correlating rcutorture output with the
 * RCU tracing information.
 */
unsigned long rcutorture_testseq;
unsigned long rcutorture_vernum;

/*
 * Return true if an RCU grace period is in progress.  The ACCESS_ONCE()s
 * permit this function to be invoked without holding the root rcu_node
 * structure's ->lock, but of course results can be subject to change.
 */
static int rcu_gp_in_progress(struct rcu_state *rsp)
{
	return ACCESS_ONCE(rsp->completed) != ACCESS_ONCE(rsp->gpnum);
}

/*
 * Note a quiescent state.  Because we do not need to know
 * how many quiescent states passed, just if there was at least
 * one since the start of the grace period, this just sets a flag.
 * The caller must have disabled preemption.
 */
void rcu_sched_qs(int cpu)
{
	struct rcu_data *rdp = &per_cpu(rcu_sched_data, cpu);

	if (rdp->passed_quiesce == 0)
		trace_rcu_grace_period(TPS("rcu_sched"), rdp->gpnum, TPS("cpuqs"));
	rdp->passed_quiesce = 1;
}

void rcu_bh_qs(int cpu)
{
	struct rcu_data *rdp = &per_cpu(rcu_bh_data, cpu);

	if (rdp->passed_quiesce == 0)
		trace_rcu_grace_period(TPS("rcu_bh"), rdp->gpnum, TPS("cpuqs"));
	rdp->passed_quiesce = 1;
}

static DEFINE_PER_CPU(int, rcu_sched_qs_mask);

static DEFINE_PER_CPU(struct rcu_dynticks, rcu_dynticks) = {
	.dynticks_nesting = DYNTICK_TASK_EXIT_IDLE,
	.dynticks = ATOMIC_INIT(1),
#ifdef CONFIG_NO_HZ_FULL_SYSIDLE
	.dynticks_idle_nesting = DYNTICK_TASK_NEST_VALUE,
	.dynticks_idle = ATOMIC_INIT(1),
#endif /* #ifdef CONFIG_NO_HZ_FULL_SYSIDLE */
};

/*
 * Let the RCU core know that this CPU has gone through the scheduler,
 * which is a quiescent state.  This is called when the need for a
 * quiescent state is urgent, so we burn an atomic operation and full
 * memory barriers to let the RCU core know about it, regardless of what
 * this CPU might (or might not) do in the near future.
 *
 * We inform the RCU core by emulating a zero-duration dyntick-idle
 * period, which we in turn do by incrementing the ->dynticks counter
 * by two.
 */
static void rcu_momentary_dyntick_idle(void)
{
	unsigned long flags;
	struct rcu_data *rdp;
	struct rcu_dynticks *rdtp;
	int resched_mask;
	struct rcu_state *rsp;

	local_irq_save(flags);

	/*
	 * Yes, we can lose flag-setting operations.  This is OK, because
	 * the flag will be set again after some delay.
	 */
	resched_mask = raw_cpu_read(rcu_sched_qs_mask);
	raw_cpu_write(rcu_sched_qs_mask, 0);

	/* Find the flavor that needs a quiescent state. */
	for_each_rcu_flavor(rsp) {
		rdp = raw_cpu_ptr(rsp->rda);
		if (!(resched_mask & rsp->flavor_mask))
			continue;
		smp_mb(); /* rcu_sched_qs_mask before cond_resched_completed. */
		if (ACCESS_ONCE(rdp->mynode->completed) !=
		    ACCESS_ONCE(rdp->cond_resched_completed))
			continue;

		/*
		 * Pretend to be momentarily idle for the quiescent state.
		 * This allows the grace-period kthread to record the
		 * quiescent state, with no need for this CPU to do anything
		 * further.
		 */
		rdtp = this_cpu_ptr(&rcu_dynticks);
		smp_mb__before_atomic(); /* Earlier stuff before QS. */
		atomic_add(2, &rdtp->dynticks);  /* QS. */
		smp_mb__after_atomic(); /* Later stuff after QS. */
		break;
	}
	local_irq_restore(flags);
}

/*
 * Note a context switch.  This is a quiescent state for RCU-sched,
 * and requires special handling for preemptible RCU.
 * The caller must have disabled preemption.
 */
void rcu_note_context_switch(int cpu)
{
	trace_rcu_utilization(TPS("Start context switch"));
	rcu_sched_qs(cpu);
	rcu_preempt_note_context_switch(cpu);
	if (unlikely(raw_cpu_read(rcu_sched_qs_mask)))
		rcu_momentary_dyntick_idle();
	trace_rcu_utilization(TPS("End context switch"));
}
EXPORT_SYMBOL_GPL(rcu_note_context_switch);

static long blimit = 10;	/* Maximum callbacks per rcu_do_batch. */
static long qhimark = 10000;	/* If this many pending, ignore blimit. */
static long qlowmark = 100;	/* Once only this many pending, use blimit. */

module_param(blimit, long, 0444);
module_param(qhimark, long, 0444);
module_param(qlowmark, long, 0444);

static ulong jiffies_till_first_fqs = ULONG_MAX;
static ulong jiffies_till_next_fqs = ULONG_MAX;

module_param(jiffies_till_first_fqs, ulong, 0644);
module_param(jiffies_till_next_fqs, ulong, 0644);

/*
 * How long the grace period must be before we start recruiting
 * quiescent-state help from rcu_note_context_switch().
 */
static ulong jiffies_till_sched_qs = HZ / 20;
module_param(jiffies_till_sched_qs, ulong, 0644);

static bool rcu_start_gp_advanced(struct rcu_state *rsp, struct rcu_node *rnp,
				  struct rcu_data *rdp);
static void force_qs_rnp(struct rcu_state *rsp,
			 int (*f)(struct rcu_data *rsp, bool *isidle,
				  unsigned long *maxj),
			 bool *isidle, unsigned long *maxj);
static void force_quiescent_state(struct rcu_state *rsp);
static int rcu_pending(int cpu);

/*
 * Return the number of RCU-sched batches processed thus far for debug & stats.
 */
long rcu_batches_completed_sched(void)
{
	return rcu_sched_state.completed;
}
EXPORT_SYMBOL_GPL(rcu_batches_completed_sched);

/*
 * Return the number of RCU BH batches processed thus far for debug & stats.
 */
long rcu_batches_completed_bh(void)
{
	return rcu_bh_state.completed;
}
EXPORT_SYMBOL_GPL(rcu_batches_completed_bh);

/*
 * Force a quiescent state.
 */
void rcu_force_quiescent_state(void)
{
	force_quiescent_state(rcu_state_p);
}
EXPORT_SYMBOL_GPL(rcu_force_quiescent_state);

/*
 * Force a quiescent state for RCU BH.
 */
void rcu_bh_force_quiescent_state(void)
{
	force_quiescent_state(&rcu_bh_state);
}
EXPORT_SYMBOL_GPL(rcu_bh_force_quiescent_state);

/*
 * Show the state of the grace-period kthreads.
 */
void show_rcu_gp_kthreads(void)
{
	struct rcu_state *rsp;

	for_each_rcu_flavor(rsp) {
		pr_info("%s: wait state: %d ->state: %#lx\n",
			rsp->name, rsp->gp_state, rsp->gp_kthread->state);
		/* sched_show_task(rsp->gp_kthread); */
	}
}
EXPORT_SYMBOL_GPL(show_rcu_gp_kthreads);

/*
 * Record the number of times rcutorture tests have been initiated and
 * terminated.  This information allows the debugfs tracing stats to be
 * correlated to the rcutorture messages, even when the rcutorture module
 * is being repeatedly loaded and unloaded.  In other words, we cannot
 * store this state in rcutorture itself.
 */
void rcutorture_record_test_transition(void)
{
	rcutorture_testseq++;
	rcutorture_vernum = 0;
}
EXPORT_SYMBOL_GPL(rcutorture_record_test_transition);

/*
 * Send along grace-period-related data for rcutorture diagnostics.
 */
void rcutorture_get_gp_data(enum rcutorture_type test_type, int *flags,
			    unsigned long *gpnum, unsigned long *completed)
{
	struct rcu_state *rsp = NULL;

	switch (test_type) {
	case RCU_FLAVOR:
		rsp = rcu_state_p;
		break;
	case RCU_BH_FLAVOR:
		rsp = &rcu_bh_state;
		break;
	case RCU_SCHED_FLAVOR:
		rsp = &rcu_sched_state;
		break;
	default:
		break;
	}
	if (rsp != NULL) {
		*flags = ACCESS_ONCE(rsp->gp_flags);
		*gpnum = ACCESS_ONCE(rsp->gpnum);
		*completed = ACCESS_ONCE(rsp->completed);
		return;
	}
	*flags = 0;
	*gpnum = 0;
	*completed = 0;
}
EXPORT_SYMBOL_GPL(rcutorture_get_gp_data);

/*
 * Record the number of writer passes through the current rcutorture test.
 * This is also used to correlate debugfs tracing stats with the rcutorture
 * messages.
 */
void rcutorture_record_progress(unsigned long vernum)
{
	rcutorture_vernum++;
}
EXPORT_SYMBOL_GPL(rcutorture_record_progress);

/*
 * Force a quiescent state for RCU-sched.
 */
void rcu_sched_force_quiescent_state(void)
{
	force_quiescent_state(&rcu_sched_state);
}
EXPORT_SYMBOL_GPL(rcu_sched_force_quiescent_state);

/*
 * Does the CPU have callbacks ready to be invoked?
 */
static int
cpu_has_callbacks_ready_to_invoke(struct rcu_data *rdp)
{
	return &rdp->nxtlist != rdp->nxttail[RCU_DONE_TAIL] &&
	       rdp->nxttail[RCU_DONE_TAIL] != NULL;
}

/*
 * Return the root node of the specified rcu_state structure.
 */
static struct rcu_node *rcu_get_root(struct rcu_state *rsp)
{
	return &rsp->node[0];
}

/*
 * Is there any need for future grace periods?
 * Interrupts must be disabled.  If the caller does not hold the root
 * rnp_node structure's ->lock, the results are advisory only.
 */
static int rcu_future_needs_gp(struct rcu_state *rsp)
{
	struct rcu_node *rnp = rcu_get_root(rsp);
	int idx = (ACCESS_ONCE(rnp->completed) + 1) & 0x1;
	int *fp = &rnp->need_future_gp[idx];

	return ACCESS_ONCE(*fp);
}

/*
 * Does the current CPU require a not-yet-started grace period?
 * The caller must have disabled interrupts to prevent races with
 * normal callback registry.
 */
static int
cpu_needs_another_gp(struct rcu_state *rsp, struct rcu_data *rdp)
{
	int i;

	if (rcu_gp_in_progress(rsp))
		return 0;  /* No, a grace period is already in progress. */
	if (rcu_future_needs_gp(rsp))
		return 1;  /* Yes, a no-CBs CPU needs one. */
	if (!rdp->nxttail[RCU_NEXT_TAIL])
		return 0;  /* No, this is a no-CBs (or offline) CPU. */
	if (*rdp->nxttail[RCU_NEXT_READY_TAIL])
		return 1;  /* Yes, this CPU has newly registered callbacks. */
	for (i = RCU_WAIT_TAIL; i < RCU_NEXT_TAIL; i++)
		if (rdp->nxttail[i - 1] != rdp->nxttail[i] &&
		    ULONG_CMP_LT(ACCESS_ONCE(rsp->completed),
				 rdp->nxtcompleted[i]))
			return 1;  /* Yes, CBs for future grace period. */
	return 0; /* No grace period needed. */
}

/*
 * rcu_eqs_enter_common - current CPU is moving towards extended quiescent state
 *
 * If the new value of the ->dynticks_nesting counter now is zero,
 * we really have entered idle, and must do the appropriate accounting.
 * The caller must have disabled interrupts.
 */
static void rcu_eqs_enter_common(struct rcu_dynticks *rdtp, long long oldval,
				bool user)
{
	struct rcu_state *rsp;
	struct rcu_data *rdp;

	trace_rcu_dyntick(TPS("Start"), oldval, rdtp->dynticks_nesting);
	if (!user && !is_idle_task(current)) {
		struct task_struct *idle __maybe_unused =
			idle_task(smp_processor_id());

		trace_rcu_dyntick(TPS("Error on entry: not idle task"), oldval, 0);
		ftrace_dump(DUMP_ORIG);
		WARN_ONCE(1, "Current pid: %d comm: %s / Idle pid: %d comm: %s",
			  current->pid, current->comm,
			  idle->pid, idle->comm); /* must be idle task! */
	}
	for_each_rcu_flavor(rsp) {
		rdp = this_cpu_ptr(rsp->rda);
		do_nocb_deferred_wakeup(rdp);
	}
	rcu_prepare_for_idle(smp_processor_id());
	/* CPUs seeing atomic_inc() must see prior RCU read-side crit sects */
	smp_mb__before_atomic();  /* See above. */
	atomic_inc(&rdtp->dynticks);
	smp_mb__after_atomic();  /* Force ordering with next sojourn. */
	WARN_ON_ONCE(atomic_read(&rdtp->dynticks) & 0x1);

	/*
	 * It is illegal to enter an extended quiescent state while
	 * in an RCU read-side critical section.
	 */
	rcu_lockdep_assert(!lock_is_held(&rcu_lock_map),
			   "Illegal idle entry in RCU read-side critical section.");
	rcu_lockdep_assert(!lock_is_held(&rcu_bh_lock_map),
			   "Illegal idle entry in RCU-bh read-side critical section.");
	rcu_lockdep_assert(!lock_is_held(&rcu_sched_lock_map),
			   "Illegal idle entry in RCU-sched read-side critical section.");
}

/*
 * Enter an RCU extended quiescent state, which can be either the
 * idle loop or adaptive-tickless usermode execution.
 */
static void rcu_eqs_enter(bool user)
{
	long long oldval;
	struct rcu_dynticks *rdtp;

	rdtp = this_cpu_ptr(&rcu_dynticks);
	oldval = rdtp->dynticks_nesting;
	WARN_ON_ONCE((oldval & DYNTICK_TASK_NEST_MASK) == 0);
	if ((oldval & DYNTICK_TASK_NEST_MASK) == DYNTICK_TASK_NEST_VALUE) {
		rdtp->dynticks_nesting = 0;
		rcu_eqs_enter_common(rdtp, oldval, user);
	} else {
		rdtp->dynticks_nesting -= DYNTICK_TASK_NEST_VALUE;
	}
}

/**
 * rcu_idle_enter - inform RCU that current CPU is entering idle
 *
 * Enter idle mode, in other words, -leave- the mode in which RCU
 * read-side critical sections can occur.  (Though RCU read-side
 * critical sections can occur in irq handlers in idle, a possibility
 * handled by irq_enter() and irq_exit().)
 *
 * We crowbar the ->dynticks_nesting field to zero to allow for
 * the possibility of usermode upcalls having messed up our count
 * of interrupt nesting level during the prior busy period.
 */
void rcu_idle_enter(void)
{
	unsigned long flags;

	local_irq_save(flags);
	rcu_eqs_enter(false);
	rcu_sysidle_enter(this_cpu_ptr(&rcu_dynticks), 0);
	local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(rcu_idle_enter);

#ifdef CONFIG_RCU_USER_QS
/**
 * rcu_user_enter - inform RCU that we are resuming userspace.
 *
 * Enter RCU idle mode right before resuming userspace.  No use of RCU
 * is permitted between this call and rcu_user_exit(). This way the
 * CPU doesn't need to maintain the tick for RCU maintenance purposes
 * when the CPU runs in userspace.
 */
void rcu_user_enter(void)
{
	rcu_eqs_enter(1);
}
#endif /* CONFIG_RCU_USER_QS */

/**
 * rcu_irq_exit - inform RCU that current CPU is exiting irq towards idle
 *
 * Exit from an interrupt handler, which might possibly result in entering
 * idle mode, in other words, leaving the mode in which read-side critical
 * sections can occur.
 *
 * This code assumes that the idle loop never does anything that might
 * result in unbalanced calls to irq_enter() and irq_exit().  If your
 * architecture violates this assumption, RCU will give you what you
 * deserve, good and hard.  But very infrequently and irreproducibly.
 *
 * Use things like work queues to work around this limitation.
 *
 * You have been warned.
 */
void rcu_irq_exit(void)
{
	unsigned long flags;
	long long oldval;
	struct rcu_dynticks *rdtp;

	local_irq_save(flags);
	rdtp = this_cpu_ptr(&rcu_dynticks);
	oldval = rdtp->dynticks_nesting;
	rdtp->dynticks_nesting--;
	WARN_ON_ONCE(rdtp->dynticks_nesting < 0);
	if (rdtp->dynticks_nesting)
		trace_rcu_dyntick(TPS("--="), oldval, rdtp->dynticks_nesting);
	else
		rcu_eqs_enter_common(rdtp, oldval, true);
	rcu_sysidle_enter(rdtp, 1);
	local_irq_restore(flags);
}

/*
 * rcu_eqs_exit_common - current CPU moving away from extended quiescent state
 *
 * If the new value of the ->dynticks_nesting counter was previously zero,
 * we really have exited idle, and must do the appropriate accounting.
 * The caller must have disabled interrupts.
 */
static void rcu_eqs_exit_common(struct rcu_dynticks *rdtp, long long oldval,
			       int user)
{
	smp_mb__before_atomic();  /* Force ordering w/previous sojourn. */
	atomic_inc(&rdtp->dynticks);
	/* CPUs seeing atomic_inc() must see later RCU read-side crit sects */
	smp_mb__after_atomic();  /* See above. */
	WARN_ON_ONCE(!(atomic_read(&rdtp->dynticks) & 0x1));
	rcu_cleanup_after_idle(smp_processor_id());
	trace_rcu_dyntick(TPS("End"), oldval, rdtp->dynticks_nesting);
	if (!user && !is_idle_task(current)) {
		struct task_struct *idle __maybe_unused =
			idle_task(smp_processor_id());

		trace_rcu_dyntick(TPS("Error on exit: not idle task"),
				  oldval, rdtp->dynticks_nesting);
		ftrace_dump(DUMP_ORIG);
		WARN_ONCE(1, "Current pid: %d comm: %s / Idle pid: %d comm: %s",
			  current->pid, current->comm,
			  idle->pid, idle->comm); /* must be idle task! */
	}
}

/*
 * Exit an RCU extended quiescent state, which can be either the
 * idle loop or adaptive-tickless usermode execution.
 */
static void rcu_eqs_exit(bool user)
{
	struct rcu_dynticks *rdtp;
	long long oldval;

	rdtp = this_cpu_ptr(&rcu_dynticks);
	oldval = rdtp->dynticks_nesting;
	WARN_ON_ONCE(oldval < 0);
	if (oldval & DYNTICK_TASK_NEST_MASK) {
		rdtp->dynticks_nesting += DYNTICK_TASK_NEST_VALUE;
	} else {
		rdtp->dynticks_nesting = DYNTICK_TASK_EXIT_IDLE;
		rcu_eqs_exit_common(rdtp, oldval, user);
	}
}

/**
 * rcu_idle_exit - inform RCU that current CPU is leaving idle
 *
 * Exit idle mode, in other words, -enter- the mode in which RCU
 * read-side critical sections can occur.
 *
 * We crowbar the ->dynticks_nesting field to DYNTICK_TASK_NEST to
 * allow for the possibility of usermode upcalls messing up our count
 * of interrupt nesting level during the busy period that is just
 * now starting.
 */
void rcu_idle_exit(void)
{
	unsigned long flags;

	local_irq_save(flags);
	rcu_eqs_exit(false);
	rcu_sysidle_exit(this_cpu_ptr(&rcu_dynticks), 0);
	local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(rcu_idle_exit);

#ifdef CONFIG_RCU_USER_QS
/**
 * rcu_user_exit - inform RCU that we are exiting userspace.
 *
 * Exit RCU idle mode while entering the kernel because it can
 * run a RCU read side critical section anytime.
 */
void rcu_user_exit(void)
{
	rcu_eqs_exit(1);
}
#endif /* CONFIG_RCU_USER_QS */

/**
 * rcu_irq_enter - inform RCU that current CPU is entering irq away from idle
 *
 * Enter an interrupt handler, which might possibly result in exiting
 * idle mode, in other words, entering the mode in which read-side critical
 * sections can occur.
 *
 * Note that the Linux kernel is fully capable of entering an interrupt
 * handler that it never exits, for example when doing upcalls to
 * user mode!  This code assumes that the idle loop never does upcalls to
 * user mode.  If your architecture does do upcalls from the idle loop (or
 * does anything else that results in unbalanced calls to the irq_enter()
 * and irq_exit() functions), RCU will give you what you deserve, good
 * and hard.  But very infrequently and irreproducibly.
 *
 * Use things like work queues to work around this limitation.
 *
 * You have been warned.
 */
void rcu_irq_enter(void)
{
	unsigned long flags;
	struct rcu_dynticks *rdtp;
	long long oldval;

	local_irq_save(flags);
	rdtp = this_cpu_ptr(&rcu_dynticks);
	oldval = rdtp->dynticks_nesting;
	rdtp->dynticks_nesting++;
	WARN_ON_ONCE(rdtp->dynticks_nesting == 0);
	if (oldval)
		trace_rcu_dyntick(TPS("++="), oldval, rdtp->dynticks_nesting);
	else
		rcu_eqs_exit_common(rdtp, oldval, true);
	rcu_sysidle_exit(rdtp, 1);
	local_irq_restore(flags);
}

/**
 * rcu_nmi_enter - inform RCU of entry to NMI context
 *
 * If the CPU was idle with dynamic ticks active, and there is no
 * irq handler running, this updates rdtp->dynticks_nmi to let the
 * RCU grace-period handling know that the CPU is active.
 */
void rcu_nmi_enter(void)
{
	struct rcu_dynticks *rdtp = this_cpu_ptr(&rcu_dynticks);

	if (rdtp->dynticks_nmi_nesting == 0 &&
	    (atomic_read(&rdtp->dynticks) & 0x1))
		return;
	rdtp->dynticks_nmi_nesting++;
	smp_mb__before_atomic();  /* Force delay from prior write. */
	atomic_inc(&rdtp->dynticks);
	/* CPUs seeing atomic_inc() must see later RCU read-side crit sects */
	smp_mb__after_atomic();  /* See above. */
	WARN_ON_ONCE(!(atomic_read(&rdtp->dynticks) & 0x1));
}

/**
 * rcu_nmi_exit - inform RCU of exit from NMI context
 *
 * If the CPU was idle with dynamic ticks active, and there is no
 * irq handler running, this updates rdtp->dynticks_nmi to let the
 * RCU grace-period handling know that the CPU is no longer active.
 */
void rcu_nmi_exit(void)
{
	struct rcu_dynticks *rdtp = this_cpu_ptr(&rcu_dynticks);

	if (rdtp->dynticks_nmi_nesting == 0 ||
	    --rdtp->dynticks_nmi_nesting != 0)
		return;
	/* CPUs seeing atomic_inc() must see prior RCU read-side crit sects */
	smp_mb__before_atomic();  /* See above. */
	atomic_inc(&rdtp->dynticks);
	smp_mb__after_atomic();  /* Force delay to next write. */
	WARN_ON_ONCE(atomic_read(&rdtp->dynticks) & 0x1);
}

/**
 * __rcu_is_watching - are RCU read-side critical sections safe?
 *
 * Return true if RCU is watching the running CPU, which means that
 * this CPU can safely enter RCU read-side critical sections.  Unlike
 * rcu_is_watching(), the caller of __rcu_is_watching() must have at
 * least disabled preemption.
 */
bool notrace __rcu_is_watching(void)
{
	return atomic_read(this_cpu_ptr(&rcu_dynticks.dynticks)) & 0x1;
}

/**
 * rcu_is_watching - see if RCU thinks that the current CPU is idle
 *
 * If the current CPU is in its idle loop and is neither in an interrupt
 * or NMI handler, return true.
 */
bool notrace rcu_is_watching(void)
{
	int ret;

	preempt_disable();
	ret = __rcu_is_watching();
	preempt_enable();
	return ret;
}
EXPORT_SYMBOL_GPL(rcu_is_watching);

#if defined(CONFIG_PROVE_RCU) && defined(CONFIG_HOTPLUG_CPU)

/*
 * Is the current CPU online?  Disable preemption to avoid false positives
 * that could otherwise happen due to the current CPU number being sampled,
 * this task being preempted, its old CPU being taken offline, resuming
 * on some other CPU, then determining that its old CPU is now offline.
 * It is OK to use RCU on an offline processor during initial boot, hence
 * the check for rcu_scheduler_fully_active.  Note also that it is OK
 * for a CPU coming online to use RCU for one jiffy prior to marking itself
 * online in the cpu_online_mask.  Similarly, it is OK for a CPU going
 * offline to continue to use RCU for one jiffy after marking itself
 * offline in the cpu_online_mask.  This leniency is necessary given the
 * non-atomic nature of the online and offline processing, for example,
 * the fact that a CPU enters the scheduler after completing the CPU_DYING
 * notifiers.
 *
 * This is also why RCU internally marks CPUs online during the
 * CPU_UP_PREPARE phase and offline during the CPU_DEAD phase.
 *
 * Disable checking if in an NMI handler because we cannot safely report
 * errors from NMI handlers anyway.
 */
bool rcu_lockdep_current_cpu_online(void)
{
	struct rcu_data *rdp;
	struct rcu_node *rnp;
	bool ret;

	if (in_nmi())
		return true;
	preempt_disable();
	rdp = this_cpu_ptr(&rcu_sched_data);
	rnp = rdp->mynode;
	ret = (rdp->grpmask & rnp->qsmaskinit) ||
	      !rcu_scheduler_fully_active;
	preempt_enable();
	return ret;
}
EXPORT_SYMBOL_GPL(rcu_lockdep_current_cpu_online);

#endif /* #if defined(CONFIG_PROVE_RCU) && defined(CONFIG_HOTPLUG_CPU) */

/**
 * rcu_is_cpu_rrupt_from_idle - see if idle or immediately interrupted from idle
 *
 * If the current CPU is idle or running at a first-level (not nested)
 * interrupt from idle, return true.  The caller must have at least
 * disabled preemption.
 */
static int rcu_is_cpu_rrupt_from_idle(void)
{
	return __this_cpu_read(rcu_dynticks.dynticks_nesting) <= 1;
}

/*
 * Snapshot the specified CPU's dynticks counter so that we can later
 * credit them with an implicit quiescent state.  Return 1 if this CPU
 * is in dynticks idle mode, which is an extended quiescent state.
 */
static int dyntick_save_progress_counter(struct rcu_data *rdp,
					 bool *isidle, unsigned long *maxj)
{
	rdp->dynticks_snap = atomic_add_return(0, &rdp->dynticks->dynticks);
	rcu_sysidle_check_cpu(rdp, isidle, maxj);
	if ((rdp->dynticks_snap & 0x1) == 0) {
		trace_rcu_fqs(rdp->rsp->name, rdp->gpnum, rdp->cpu, TPS("dti"));
		return 1;
	} else {
		return 0;
	}
}

/*
 * This function really isn't for public consumption, but RCU is special in
 * that context switches can allow the state machine to make progress.
 */
extern void resched_cpu(int cpu);

/*
 * Return true if the specified CPU has passed through a quiescent
 * state by virtue of being in or having passed through an dynticks
 * idle state since the last call to dyntick_save_progress_counter()
 * for this same CPU, or by virtue of having been offline.
 */
static int rcu_implicit_dynticks_qs(struct rcu_data *rdp,
				    bool *isidle, unsigned long *maxj)
{
	unsigned int curr;
	int *rcrmp;
	unsigned int snap;

	curr = (unsigned int)atomic_add_return(0, &rdp->dynticks->dynticks);
	snap = (unsigned int)rdp->dynticks_snap;

	/*
	 * If the CPU passed through or entered a dynticks idle phase with
	 * no active irq/NMI handlers, then we can safely pretend that the CPU
	 * already acknowledged the request to pass through a quiescent
	 * state.  Either way, that CPU cannot possibly be in an RCU
	 * read-side critical section that started before the beginning
	 * of the current RCU grace period.
	 */
	if ((curr & 0x1) == 0 || UINT_CMP_GE(curr, snap + 2)) {
		trace_rcu_fqs(rdp->rsp->name, rdp->gpnum, rdp->cpu, TPS("dti"));
		rdp->dynticks_fqs++;
		return 1;
	}

	/*
	 * Check for the CPU being offline, but only if the grace period
	 * is old enough.  We don't need to worry about the CPU changing
	 * state: If we see it offline even once, it has been through a
	 * quiescent state.
	 *
	 * The reason for insisting that the grace period be at least
	 * one jiffy old is that CPUs that are not quite online and that
	 * have just gone offline can still execute RCU read-side critical
	 * sections.
	 */
	if (ULONG_CMP_GE(rdp->rsp->gp_start + 2, jiffies))
		return 0;  /* Grace period is not old enough. */
	barrier();
	if (cpu_is_offline(rdp->cpu)) {
		trace_rcu_fqs(rdp->rsp->name, rdp->gpnum, rdp->cpu, TPS("ofl"));
		rdp->offline_fqs++;
		return 1;
	}

	/*
	 * A CPU running for an extended time within the kernel can
	 * delay RCU grace periods.  When the CPU is in NO_HZ_FULL mode,
	 * even context-switching back and forth between a pair of
	 * in-kernel CPU-bound tasks cannot advance grace periods.
	 * So if the grace period is old enough, make the CPU pay attention.
	 * Note that the unsynchronized assignments to the per-CPU
	 * rcu_sched_qs_mask variable are safe.  Yes, setting of
	 * bits can be lost, but they will be set again on the next
	 * force-quiescent-state pass.  So lost bit sets do not result
	 * in incorrect behavior, merely in a grace period lasting
	 * a few jiffies longer than it might otherwise.  Because
	 * there are at most four threads involved, and because the
	 * updates are only once every few jiffies, the probability of
	 * lossage (and thus of slight grace-period extension) is
	 * quite low.
	 *
	 * Note that if the jiffies_till_sched_qs boot/sysfs parameter
	 * is set too high, we override with half of the RCU CPU stall
	 * warning delay.
	 */
	rcrmp = &per_cpu(rcu_sched_qs_mask, rdp->cpu);
	if (ULONG_CMP_GE(jiffies,
			 rdp->rsp->gp_start + jiffies_till_sched_qs) ||
	    ULONG_CMP_GE(jiffies, rdp->rsp->jiffies_resched)) {
		if (!(ACCESS_ONCE(*rcrmp) & rdp->rsp->flavor_mask)) {
			ACCESS_ONCE(rdp->cond_resched_completed) =
				ACCESS_ONCE(rdp->mynode->completed);
			smp_mb(); /* ->cond_resched_completed before *rcrmp. */
			ACCESS_ONCE(*rcrmp) =
				ACCESS_ONCE(*rcrmp) + rdp->rsp->flavor_mask;
			resched_cpu(rdp->cpu);  /* Force CPU into scheduler. */
			rdp->rsp->jiffies_resched += 5; /* Enable beating. */
		} else if (ULONG_CMP_GE(jiffies, rdp->rsp->jiffies_resched)) {
			/* Time to beat on that CPU again! */
			resched_cpu(rdp->cpu);  /* Force CPU into scheduler. */
			rdp->rsp->jiffies_resched += 5; /* Re-enable beating. */
		}
	}

	return 0;
}

static void record_gp_stall_check_time(struct rcu_state *rsp)
{
	unsigned long j = jiffies;
	unsigned long j1;

	rsp->gp_start = j;
	smp_wmb(); /* Record start time before stall time. */
	j1 = rcu_jiffies_till_stall_check();
	ACCESS_ONCE(rsp->jiffies_stall) = j + j1;
	rsp->jiffies_resched = j + j1 / 2;
}

/*
 * Dump stacks of all tasks running on stalled CPUs.
 */
static void rcu_dump_cpu_stacks(struct rcu_state *rsp)
{
	int cpu;
	unsigned long flags;
	struct rcu_node *rnp;

	rcu_for_each_leaf_node(rsp, rnp) {
		raw_spin_lock_irqsave(&rnp->lock, flags);
		if (rnp->qsmask != 0) {
			for (cpu = 0; cpu <= rnp->grphi - rnp->grplo; cpu++)
				if (rnp->qsmask & (1UL << cpu))
					dump_cpu_task(rnp->grplo + cpu);
		}
		raw_spin_unlock_irqrestore(&rnp->lock, flags);
	}
}

static void print_other_cpu_stall(struct rcu_state *rsp)
{
	int cpu;
	long delta;
	unsigned long flags;
	int ndetected = 0;
	struct rcu_node *rnp = rcu_get_root(rsp);
	long totqlen = 0;

	/* Only let one CPU complain about others per time interval. */

	raw_spin_lock_irqsave(&rnp->lock, flags);
	delta = jiffies - ACCESS_ONCE(rsp->jiffies_stall);
	if (delta < RCU_STALL_RAT_DELAY || !rcu_gp_in_progress(rsp)) {
		raw_spin_unlock_irqrestore(&rnp->lock, flags);
		return;
	}
	ACCESS_ONCE(rsp->jiffies_stall) = jiffies + 3 * rcu_jiffies_till_stall_check() + 3;
	raw_spin_unlock_irqrestore(&rnp->lock, flags);

	/*
	 * OK, time to rat on our buddy...
	 * See Documentation/RCU/stallwarn.txt for info on how to debug
	 * RCU CPU stall warnings.
	 */
	pr_err("INFO: %s detected stalls on CPUs/tasks:",
	       rsp->name);
	print_cpu_stall_info_begin();
	rcu_for_each_leaf_node(rsp, rnp) {
		raw_spin_lock_irqsave(&rnp->lock, flags);
		ndetected += rcu_print_task_stall(rnp);
		if (rnp->qsmask != 0) {
			for (cpu = 0; cpu <= rnp->grphi - rnp->grplo; cpu++)
				if (rnp->qsmask & (1UL << cpu)) {
					print_cpu_stall_info(rsp,
							     rnp->grplo + cpu);
					ndetected++;
				}
		}
		raw_spin_unlock_irqrestore(&rnp->lock, flags);
	}

	/*
	 * Now rat on any tasks that got kicked up to the root rcu_node
	 * due to CPU offlining.
	 */
	rnp = rcu_get_root(rsp);
	raw_spin_lock_irqsave(&rnp->lock, flags);
	ndetected += rcu_print_task_stall(rnp);
	raw_spin_unlock_irqrestore(&rnp->lock, flags);

	print_cpu_stall_info_end();
	for_each_possible_cpu(cpu)
		totqlen += per_cpu_ptr(rsp->rda, cpu)->qlen;
	pr_cont("(detected by %d, t=%ld jiffies, g=%ld, c=%ld, q=%lu)\n",
	       smp_processor_id(), (long)(jiffies - rsp->gp_start),
	       (long)rsp->gpnum, (long)rsp->completed, totqlen);
	if (ndetected == 0)
		pr_err("INFO: Stall ended before state dump start\n");
	else
		rcu_dump_cpu_stacks(rsp);

	/* Complain about tasks blocking the grace period. */

	rcu_print_detail_task_stall(rsp);

	force_quiescent_state(rsp);  /* Kick them all. */
}

static void print_cpu_stall(struct rcu_state *rsp)
{
	int cpu;
	unsigned long flags;
	struct rcu_node *rnp = rcu_get_root(rsp);
	long totqlen = 0;

	/*
	 * OK, time to rat on ourselves...
	 * See Documentation/RCU/stallwarn.txt for info on how to debug
	 * RCU CPU stall warnings.
	 */
	pr_err("INFO: %s self-detected stall on CPU", rsp->name);
	print_cpu_stall_info_begin();
	print_cpu_stall_info(rsp, smp_processor_id());
	print_cpu_stall_info_end();
	for_each_possible_cpu(cpu)
		totqlen += per_cpu_ptr(rsp->rda, cpu)->qlen;
	pr_cont(" (t=%lu jiffies g=%ld c=%ld q=%lu)\n",
		jiffies - rsp->gp_start,
		(long)rsp->gpnum, (long)rsp->completed, totqlen);
	rcu_dump_cpu_stacks(rsp);

	raw_spin_lock_irqsave(&rnp->lock, flags);
	if (ULONG_CMP_GE(jiffies, ACCESS_ONCE(rsp->jiffies_stall)))
		ACCESS_ONCE(rsp->jiffies_stall) = jiffies +
				     3 * rcu_jiffies_till_stall_check() + 3;
	raw_spin_unlock_irqrestore(&rnp->lock, flags);

	/*
	 * Attempt to revive the RCU machinery by forcing a context switch.
	 *
	 * A context switch would normally allow the RCU state machine to make
	 * progress and it could be we're stuck in kernel space without context
	 * switches for an entirely unreasonable amount of time.
	 */
	resched_cpu(smp_processor_id());
}

static void check_cpu_stall(struct rcu_state *rsp, struct rcu_data *rdp)
{
	unsigned long completed;
	unsigned long gpnum;
	unsigned long gps;
	unsigned long j;
	unsigned long js;
	struct rcu_node *rnp;

	if (rcu_cpu_stall_suppress || !rcu_gp_in_progress(rsp))
		return;
	j = jiffies;

	/*
	 * Lots of memory barriers to reject false positives.
	 *
	 * The idea is to pick up rsp->gpnum, then rsp->jiffies_stall,
	 * then rsp->gp_start, and finally rsp->completed.  These values
	 * are updated in the opposite order with memory barriers (or
	 * equivalent) during grace-period initialization and cleanup.
	 * Now, a false positive can occur if we get an new value of
	 * rsp->gp_start and a old value of rsp->jiffies_stall.  But given
	 * the memory barriers, the only way that this can happen is if one
	 * grace period ends and another starts between these two fetches.
	 * Detect this by comparing rsp->completed with the previous fetch
	 * from rsp->gpnum.
	 *
	 * Given this check, comparisons of jiffies, rsp->jiffies_stall,
	 * and rsp->gp_start suffice to forestall false positives.
	 */
	gpnum = ACCESS_ONCE(rsp->gpnum);
	smp_rmb(); /* Pick up ->gpnum first... */
	js = ACCESS_ONCE(rsp->jiffies_stall);
	smp_rmb(); /* ...then ->jiffies_stall before the rest... */
	gps = ACCESS_ONCE(rsp->gp_start);
	smp_rmb(); /* ...and finally ->gp_start before ->completed. */
	completed = ACCESS_ONCE(rsp->completed);
	if (ULONG_CMP_GE(completed, gpnum) ||
	    ULONG_CMP_LT(j, js) ||
	    ULONG_CMP_GE(gps, js))
		return; /* No stall or GP completed since entering function. */
	rnp = rdp->mynode;
	if (rcu_gp_in_progress(rsp) &&
	    (ACCESS_ONCE(rnp->qsmask) & rdp->grpmask)) {

		/* We haven't checked in, so go dump stack. */
		print_cpu_stall(rsp);

	} else if (rcu_gp_in_progress(rsp) &&
		   ULONG_CMP_GE(j, js + RCU_STALL_RAT_DELAY)) {

		/* They had a few time units to dump stack, so complain. */
		print_other_cpu_stall(rsp);
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
	struct rcu_state *rsp;

	for_each_rcu_flavor(rsp)
		ACCESS_ONCE(rsp->jiffies_stall) = jiffies + ULONG_MAX / 2;
}

/*
 * Initialize the specified rcu_data structure's callback list to empty.
 */
static void init_callback_list(struct rcu_data *rdp)
{
	int i;

	if (init_nocb_callback_list(rdp))
		return;
	rdp->nxtlist = NULL;
	for (i = 0; i < RCU_NEXT_SIZE; i++)
		rdp->nxttail[i] = &rdp->nxtlist;
}

/*
 * Determine the value that ->completed will have at the end of the
 * next subsequent grace period.  This is used to tag callbacks so that
 * a CPU can invoke callbacks in a timely fashion even if that CPU has
 * been dyntick-idle for an extended period with callbacks under the
 * influence of RCU_FAST_NO_HZ.
 *
 * The caller must hold rnp->lock with interrupts disabled.
 */
static unsigned long rcu_cbs_completed(struct rcu_state *rsp,
				       struct rcu_node *rnp)
{
	/*
	 * If RCU is idle, we just wait for the next grace period.
	 * But we can only be sure that RCU is idle if we are looking
	 * at the root rcu_node structure -- otherwise, a new grace
	 * period might have started, but just not yet gotten around
	 * to initializing the current non-root rcu_node structure.
	 */
	if (rcu_get_root(rsp) == rnp && rnp->gpnum == rnp->completed)
		return rnp->completed + 1;

	/*
	 * Otherwise, wait for a possible partial grace period and
	 * then the subsequent full grace period.
	 */
	return rnp->completed + 2;
}

/*
 * Trace-event helper function for rcu_start_future_gp() and
 * rcu_nocb_wait_gp().
 */
static void trace_rcu_future_gp(struct rcu_node *rnp, struct rcu_data *rdp,
				unsigned long c, const char *s)
{
	trace_rcu_future_grace_period(rdp->rsp->name, rnp->gpnum,
				      rnp->completed, c, rnp->level,
				      rnp->grplo, rnp->grphi, s);
}

/*
 * Start some future grace period, as needed to handle newly arrived
 * callbacks.  The required future grace periods are recorded in each
 * rcu_node structure's ->need_future_gp field.  Returns true if there
 * is reason to awaken the grace-period kthread.
 *
 * The caller must hold the specified rcu_node structure's ->lock.
 */
static bool __maybe_unused
rcu_start_future_gp(struct rcu_node *rnp, struct rcu_data *rdp,
		    unsigned long *c_out)
{
	unsigned long c;
	int i;
	bool ret = false;
	struct rcu_node *rnp_root = rcu_get_root(rdp->rsp);

	/*
	 * Pick up grace-period number for new callbacks.  If this
	 * grace period is already marked as needed, return to the caller.
	 */
	c = rcu_cbs_completed(rdp->rsp, rnp);
	trace_rcu_future_gp(rnp, rdp, c, TPS("Startleaf"));
	if (rnp->need_future_gp[c & 0x1]) {
		trace_rcu_future_gp(rnp, rdp, c, TPS("Prestartleaf"));
		goto out;
	}

	/*
	 * If either this rcu_node structure or the root rcu_node structure
	 * believe that a grace period is in progress, then we must wait
	 * for the one following, which is in "c".  Because our request
	 * will be noticed at the end of the current grace period, we don't
	 * need to explicitly start one.  We only do the lockless check
	 * of rnp_root's fields if the current rcu_node structure thinks
	 * there is no grace period in flight, and because we hold rnp->lock,
	 * the only possible change is when rnp_root's two fields are
	 * equal, in which case rnp_root->gpnum might be concurrently
	 * incremented.  But that is OK, as it will just result in our
	 * doing some extra useless work.
	 */
	if (rnp->gpnum != rnp->completed ||
	    ACCESS_ONCE(rnp_root->gpnum) != ACCESS_ONCE(rnp_root->completed)) {
		rnp->need_future_gp[c & 0x1]++;
		trace_rcu_future_gp(rnp, rdp, c, TPS("Startedleaf"));
		goto out;
	}

	/*
	 * There might be no grace period in progress.  If we don't already
	 * hold it, acquire the root rcu_node structure's lock in order to
	 * start one (if needed).
	 */
	if (rnp != rnp_root) {
		raw_spin_lock(&rnp_root->lock);
		smp_mb__after_unlock_lock();
	}

	/*
	 * Get a new grace-period number.  If there really is no grace
	 * period in progress, it will be smaller than the one we obtained
	 * earlier.  Adjust callbacks as needed.  Note that even no-CBs
	 * CPUs have a ->nxtcompleted[] array, so no no-CBs checks needed.
	 */
	c = rcu_cbs_completed(rdp->rsp, rnp_root);
	for (i = RCU_DONE_TAIL; i < RCU_NEXT_TAIL; i++)
		if (ULONG_CMP_LT(c, rdp->nxtcompleted[i]))
			rdp->nxtcompleted[i] = c;

	/*
	 * If the needed for the required grace period is already
	 * recorded, trace and leave.
	 */
	if (rnp_root->need_future_gp[c & 0x1]) {
		trace_rcu_future_gp(rnp, rdp, c, TPS("Prestartedroot"));
		goto unlock_out;
	}

	/* Record the need for the future grace period. */
	rnp_root->need_future_gp[c & 0x1]++;

	/* If a grace period is not already in progress, start one. */
	if (rnp_root->gpnum != rnp_root->completed) {
		trace_rcu_future_gp(rnp, rdp, c, TPS("Startedleafroot"));
	} else {
		trace_rcu_future_gp(rnp, rdp, c, TPS("Startedroot"));
		ret = rcu_start_gp_advanced(rdp->rsp, rnp_root, rdp);
	}
unlock_out:
	if (rnp != rnp_root)
		raw_spin_unlock(&rnp_root->lock);
out:
	if (c_out != NULL)
		*c_out = c;
	return ret;
}

/*
 * Clean up any old requests for the just-ended grace period.  Also return
 * whether any additional grace periods have been requested.  Also invoke
 * rcu_nocb_gp_cleanup() in order to wake up any no-callbacks kthreads
 * waiting for this grace period to complete.
 */
static int rcu_future_gp_cleanup(struct rcu_state *rsp, struct rcu_node *rnp)
{
	int c = rnp->completed;
	int needmore;
	struct rcu_data *rdp = this_cpu_ptr(rsp->rda);

	rcu_nocb_gp_cleanup(rsp, rnp);
	rnp->need_future_gp[c & 0x1] = 0;
	needmore = rnp->need_future_gp[(c + 1) & 0x1];
	trace_rcu_future_gp(rnp, rdp, c,
			    needmore ? TPS("CleanupMore") : TPS("Cleanup"));
	return needmore;
}

/*
 * Awaken the grace-period kthread for the specified flavor of RCU.
 * Don't do a self-awaken, and don't bother awakening when there is
 * nothing for the grace-period kthread to do (as in several CPUs
 * raced to awaken, and we lost), and finally don't try to awaken
 * a kthread that has not yet been created.
 */
static void rcu_gp_kthread_wake(struct rcu_state *rsp)
{
	if (current == rsp->gp_kthread ||
	    !ACCESS_ONCE(rsp->gp_flags) ||
	    !rsp->gp_kthread)
		return;
	wake_up(&rsp->gp_wq);
}

/*
 * If there is room, assign a ->completed number to any callbacks on
 * this CPU that have not already been assigned.  Also accelerate any
 * callbacks that were previously assigned a ->completed number that has
 * since proven to be too conservative, which can happen if callbacks get
 * assigned a ->completed number while RCU is idle, but with reference to
 * a non-root rcu_node structure.  This function is idempotent, so it does
 * not hurt to call it repeatedly.  Returns an flag saying that we should
 * awaken the RCU grace-period kthread.
 *
 * The caller must hold rnp->lock with interrupts disabled.
 */
static bool rcu_accelerate_cbs(struct rcu_state *rsp, struct rcu_node *rnp,
			       struct rcu_data *rdp)
{
	unsigned long c;
	int i;
	bool ret;

	/* If the CPU has no callbacks, nothing to do. */
	if (!rdp->nxttail[RCU_NEXT_TAIL] || !*rdp->nxttail[RCU_DONE_TAIL])
		return false;

	/*
	 * Starting from the sublist containing the callbacks most
	 * recently assigned a ->completed number and working down, find the
	 * first sublist that is not assignable to an upcoming grace period.
	 * Such a sublist has something in it (first two tests) and has
	 * a ->completed number assigned that will complete sooner than
	 * the ->completed number for newly arrived callbacks (last test).
	 *
	 * The key point is that any later sublist can be assigned the
	 * same ->completed number as the newly arrived callbacks, which
	 * means that the callbacks in any of these later sublist can be
	 * grouped into a single sublist, whether or not they have already
	 * been assigned a ->completed number.
	 */
	c = rcu_cbs_completed(rsp, rnp);
	for (i = RCU_NEXT_TAIL - 1; i > RCU_DONE_TAIL; i--)
		if (rdp->nxttail[i] != rdp->nxttail[i - 1] &&
		    !ULONG_CMP_GE(rdp->nxtcompleted[i], c))
			break;

	/*
	 * If there are no sublist for unassigned callbacks, leave.
	 * At the same time, advance "i" one sublist, so that "i" will
	 * index into the sublist where all the remaining callbacks should
	 * be grouped into.
	 */
	if (++i >= RCU_NEXT_TAIL)
		return false;

	/*
	 * Assign all subsequent callbacks' ->completed number to the next
	 * full grace period and group them all in the sublist initially
	 * indexed by "i".
	 */
	for (; i <= RCU_NEXT_TAIL; i++) {
		rdp->nxttail[i] = rdp->nxttail[RCU_NEXT_TAIL];
		rdp->nxtcompleted[i] = c;
	}
	/* Record any needed additional grace periods. */
	ret = rcu_start_future_gp(rnp, rdp, NULL);

	/* Trace depending on how much we were able to accelerate. */
	if (!*rdp->nxttail[RCU_WAIT_TAIL])
		trace_rcu_grace_period(rsp->name, rdp->gpnum, TPS("AccWaitCB"));
	else
		trace_rcu_grace_period(rsp->name, rdp->gpnum, TPS("AccReadyCB"));
	return ret;
}

/*
 * Move any callbacks whose grace period has completed to the
 * RCU_DONE_TAIL sublist, then compact the remaining sublists and
 * assign ->completed numbers to any callbacks in the RCU_NEXT_TAIL
 * sublist.  This function is idempotent, so it does not hurt to
 * invoke it repeatedly.  As long as it is not invoked -too- often...
 * Returns true if the RCU grace-period kthread needs to be awakened.
 *
 * The caller must hold rnp->lock with interrupts disabled.
 */
static bool rcu_advance_cbs(struct rcu_state *rsp, struct rcu_node *rnp,
			    struct rcu_data *rdp)
{
	int i, j;

	/* If the CPU has no callbacks, nothing to do. */
	if (!rdp->nxttail[RCU_NEXT_TAIL] || !*rdp->nxttail[RCU_DONE_TAIL])
		return false;

	/*
	 * Find all callbacks whose ->completed numbers indicate that they
	 * are ready to invoke, and put them into the RCU_DONE_TAIL sublist.
	 */
	for (i = RCU_WAIT_TAIL; i < RCU_NEXT_TAIL; i++) {
		if (ULONG_CMP_LT(rnp->completed, rdp->nxtcompleted[i]))
			break;
		rdp->nxttail[RCU_DONE_TAIL] = rdp->nxttail[i];
	}
	/* Clean up any sublist tail pointers that were misordered above. */
	for (j = RCU_WAIT_TAIL; j < i; j++)
		rdp->nxttail[j] = rdp->nxttail[RCU_DONE_TAIL];

	/* Copy down callbacks to fill in empty sublists. */
	for (j = RCU_WAIT_TAIL; i < RCU_NEXT_TAIL; i++, j++) {
		if (rdp->nxttail[j] == rdp->nxttail[RCU_NEXT_TAIL])
			break;
		rdp->nxttail[j] = rdp->nxttail[i];
		rdp->nxtcompleted[j] = rdp->nxtcompleted[i];
	}

	/* Classify any remaining callbacks. */
	return rcu_accelerate_cbs(rsp, rnp, rdp);
}

/*
 * Update CPU-local rcu_data state to record the beginnings and ends of
 * grace periods.  The caller must hold the ->lock of the leaf rcu_node
 * structure corresponding to the current CPU, and must have irqs disabled.
 * Returns true if the grace-period kthread needs to be awakened.
 */
static bool __note_gp_changes(struct rcu_state *rsp, struct rcu_node *rnp,
			      struct rcu_data *rdp)
{
	bool ret;

	/* Handle the ends of any preceding grace periods first. */
	if (rdp->completed == rnp->completed) {

		/* No grace period end, so just accelerate recent callbacks. */
		ret = rcu_accelerate_cbs(rsp, rnp, rdp);

	} else {

		/* Advance callbacks. */
		ret = rcu_advance_cbs(rsp, rnp, rdp);

		/* Remember that we saw this grace-period completion. */
		rdp->completed = rnp->completed;
		trace_rcu_grace_period(rsp->name, rdp->gpnum, TPS("cpuend"));
	}

	if (rdp->gpnum != rnp->gpnum) {
		/*
		 * If the current grace period is waiting for this CPU,
		 * set up to detect a quiescent state, otherwise don't
		 * go looking for one.
		 */
		rdp->gpnum = rnp->gpnum;
		trace_rcu_grace_period(rsp->name, rdp->gpnum, TPS("cpustart"));
		rdp->passed_quiesce = 0;
		rdp->qs_pending = !!(rnp->qsmask & rdp->grpmask);
		zero_cpu_stall_ticks(rdp);
	}
	return ret;
}

static void note_gp_changes(struct rcu_state *rsp, struct rcu_data *rdp)
{
	unsigned long flags;
	bool needwake;
	struct rcu_node *rnp;

	local_irq_save(flags);
	rnp = rdp->mynode;
	if ((rdp->gpnum == ACCESS_ONCE(rnp->gpnum) &&
	     rdp->completed == ACCESS_ONCE(rnp->completed)) || /* w/out lock. */
	    !raw_spin_trylock(&rnp->lock)) { /* irqs already off, so later. */
		local_irq_restore(flags);
		return;
	}
	smp_mb__after_unlock_lock();
	needwake = __note_gp_changes(rsp, rnp, rdp);
	raw_spin_unlock_irqrestore(&rnp->lock, flags);
	if (needwake)
		rcu_gp_kthread_wake(rsp);
}

/*
 * Initialize a new grace period.  Return 0 if no grace period required.
 */
static int rcu_gp_init(struct rcu_state *rsp)
{
	struct rcu_data *rdp;
	struct rcu_node *rnp = rcu_get_root(rsp);

	rcu_bind_gp_kthread();
	raw_spin_lock_irq(&rnp->lock);
	smp_mb__after_unlock_lock();
	if (!ACCESS_ONCE(rsp->gp_flags)) {
		/* Spurious wakeup, tell caller to go back to sleep.  */
		raw_spin_unlock_irq(&rnp->lock);
		return 0;
	}
	ACCESS_ONCE(rsp->gp_flags) = 0; /* Clear all flags: New grace period. */

	if (WARN_ON_ONCE(rcu_gp_in_progress(rsp))) {
		/*
		 * Grace period already in progress, don't start another.
		 * Not supposed to be able to happen.
		 */
		raw_spin_unlock_irq(&rnp->lock);
		return 0;
	}

	/* Advance to a new grace period and initialize state. */
	record_gp_stall_check_time(rsp);
	/* Record GP times before starting GP, hence smp_store_release(). */
	smp_store_release(&rsp->gpnum, rsp->gpnum + 1);
	trace_rcu_grace_period(rsp->name, rsp->gpnum, TPS("start"));
	raw_spin_unlock_irq(&rnp->lock);

	/* Exclude any concurrent CPU-hotplug operations. */
	mutex_lock(&rsp->onoff_mutex);
	smp_mb__after_unlock_lock(); /* ->gpnum increment before GP! */

	/*
	 * Set the quiescent-state-needed bits in all the rcu_node
	 * structures for all currently online CPUs in breadth-first order,
	 * starting from the root rcu_node structure, relying on the layout
	 * of the tree within the rsp->node[] array.  Note that other CPUs
	 * will access only the leaves of the hierarchy, thus seeing that no
	 * grace period is in progress, at least until the corresponding
	 * leaf node has been initialized.  In addition, we have excluded
	 * CPU-hotplug operations.
	 *
	 * The grace period cannot complete until the initialization
	 * process finishes, because this kthread handles both.
	 */
	rcu_for_each_node_breadth_first(rsp, rnp) {
		raw_spin_lock_irq(&rnp->lock);
		smp_mb__after_unlock_lock();
		rdp = this_cpu_ptr(rsp->rda);
		rcu_preempt_check_blocked_tasks(rnp);
		rnp->qsmask = rnp->qsmaskinit;
		ACCESS_ONCE(rnp->gpnum) = rsp->gpnum;
		WARN_ON_ONCE(rnp->completed != rsp->completed);
		ACCESS_ONCE(rnp->completed) = rsp->completed;
		if (rnp == rdp->mynode)
			(void)__note_gp_changes(rsp, rnp, rdp);
		rcu_preempt_boost_start_gp(rnp);
		trace_rcu_grace_period_init(rsp->name, rnp->gpnum,
					    rnp->level, rnp->grplo,
					    rnp->grphi, rnp->qsmask);
		raw_spin_unlock_irq(&rnp->lock);
		cond_resched();
	}

	mutex_unlock(&rsp->onoff_mutex);
	return 1;
}

/*
 * Do one round of quiescent-state forcing.
 */
static int rcu_gp_fqs(struct rcu_state *rsp, int fqs_state_in)
{
	int fqs_state = fqs_state_in;
	bool isidle = false;
	unsigned long maxj;
	struct rcu_node *rnp = rcu_get_root(rsp);

	rsp->n_force_qs++;
	if (fqs_state == RCU_SAVE_DYNTICK) {
		/* Collect dyntick-idle snapshots. */
		if (is_sysidle_rcu_state(rsp)) {
			isidle = 1;
			maxj = jiffies - ULONG_MAX / 4;
		}
		force_qs_rnp(rsp, dyntick_save_progress_counter,
			     &isidle, &maxj);
		rcu_sysidle_report_gp(rsp, isidle, maxj);
		fqs_state = RCU_FORCE_QS;
	} else {
		/* Handle dyntick-idle and offline CPUs. */
		isidle = 0;
		force_qs_rnp(rsp, rcu_implicit_dynticks_qs, &isidle, &maxj);
	}
	/* Clear flag to prevent immediate re-entry. */
	if (ACCESS_ONCE(rsp->gp_flags) & RCU_GP_FLAG_FQS) {
		raw_spin_lock_irq(&rnp->lock);
		smp_mb__after_unlock_lock();
		ACCESS_ONCE(rsp->gp_flags) &= ~RCU_GP_FLAG_FQS;
		raw_spin_unlock_irq(&rnp->lock);
	}
	return fqs_state;
}

/*
 * Clean up after the old grace period.
 */
static void rcu_gp_cleanup(struct rcu_state *rsp)
{
	unsigned long gp_duration;
	bool needgp = false;
	int nocb = 0;
	struct rcu_data *rdp;
	struct rcu_node *rnp = rcu_get_root(rsp);

	raw_spin_lock_irq(&rnp->lock);
	smp_mb__after_unlock_lock();
	gp_duration = jiffies - rsp->gp_start;
	if (gp_duration > rsp->gp_max)
		rsp->gp_max = gp_duration;

	/*
	 * We know the grace period is complete, but to everyone else
	 * it appears to still be ongoing.  But it is also the case
	 * that to everyone else it looks like there is nothing that
	 * they can do to advance the grace period.  It is therefore
	 * safe for us to drop the lock in order to mark the grace
	 * period as completed in all of the rcu_node structures.
	 */
	raw_spin_unlock_irq(&rnp->lock);

	/*
	 * Propagate new ->completed value to rcu_node structures so
	 * that other CPUs don't have to wait until the start of the next
	 * grace period to process their callbacks.  This also avoids
	 * some nasty RCU grace-period initialization races by forcing
	 * the end of the current grace period to be completely recorded in
	 * all of the rcu_node structures before the beginning of the next
	 * grace period is recorded in any of the rcu_node structures.
	 */
	rcu_for_each_node_breadth_first(rsp, rnp) {
		raw_spin_lock_irq(&rnp->lock);
		smp_mb__after_unlock_lock();
		ACCESS_ONCE(rnp->completed) = rsp->gpnum;
		rdp = this_cpu_ptr(rsp->rda);
		if (rnp == rdp->mynode)
			needgp = __note_gp_changes(rsp, rnp, rdp) || needgp;
		/* smp_mb() provided by prior unlock-lock pair. */
		nocb += rcu_future_gp_cleanup(rsp, rnp);
		raw_spin_unlock_irq(&rnp->lock);
		cond_resched();
	}
	rnp = rcu_get_root(rsp);
	raw_spin_lock_irq(&rnp->lock);
	smp_mb__after_unlock_lock(); /* Order GP before ->completed update. */
	rcu_nocb_gp_set(rnp, nocb);

	/* Declare grace period done. */
	ACCESS_ONCE(rsp->completed) = rsp->gpnum;
	trace_rcu_grace_period(rsp->name, rsp->completed, TPS("end"));
	rsp->fqs_state = RCU_GP_IDLE;
	rdp = this_cpu_ptr(rsp->rda);
	/* Advance CBs to reduce false positives below. */
	needgp = rcu_advance_cbs(rsp, rnp, rdp) || needgp;
	if (needgp || cpu_needs_another_gp(rsp, rdp)) {
		ACCESS_ONCE(rsp->gp_flags) = RCU_GP_FLAG_INIT;
		trace_rcu_grace_period(rsp->name,
				       ACCESS_ONCE(rsp->gpnum),
				       TPS("newreq"));
	}
	raw_spin_unlock_irq(&rnp->lock);
}

/*
 * Body of kthread that handles grace periods.
 */
static int __noreturn rcu_gp_kthread(void *arg)
{
	int fqs_state;
	int gf;
	unsigned long j;
	int ret;
	struct rcu_state *rsp = arg;
	struct rcu_node *rnp = rcu_get_root(rsp);

	for (;;) {

		/* Handle grace-period start. */
		for (;;) {
			trace_rcu_grace_period(rsp->name,
					       ACCESS_ONCE(rsp->gpnum),
					       TPS("reqwait"));
			rsp->gp_state = RCU_GP_WAIT_GPS;
			wait_event_interruptible(rsp->gp_wq,
						 ACCESS_ONCE(rsp->gp_flags) &
						 RCU_GP_FLAG_INIT);
			/* Locking provides needed memory barrier. */
			if (rcu_gp_init(rsp))
				break;
			cond_resched();
			flush_signals(current);
			trace_rcu_grace_period(rsp->name,
					       ACCESS_ONCE(rsp->gpnum),
					       TPS("reqwaitsig"));
		}

		/* Handle quiescent-state forcing. */
		fqs_state = RCU_SAVE_DYNTICK;
		j = jiffies_till_first_fqs;
		if (j > HZ) {
			j = HZ;
			jiffies_till_first_fqs = HZ;
		}
		ret = 0;
		for (;;) {
			if (!ret)
				rsp->jiffies_force_qs = jiffies + j;
			trace_rcu_grace_period(rsp->name,
					       ACCESS_ONCE(rsp->gpnum),
					       TPS("fqswait"));
			rsp->gp_state = RCU_GP_WAIT_FQS;
			ret = wait_event_interruptible_timeout(rsp->gp_wq,
					((gf = ACCESS_ONCE(rsp->gp_flags)) &
					 RCU_GP_FLAG_FQS) ||
					(!ACCESS_ONCE(rnp->qsmask) &&
					 !rcu_preempt_blocked_readers_cgp(rnp)),
					j);
			/* Locking provides needed memory barriers. */
			/* If grace period done, leave loop. */
			if (!ACCESS_ONCE(rnp->qsmask) &&
			    !rcu_preempt_blocked_readers_cgp(rnp))
				break;
			/* If time for quiescent-state forcing, do it. */
			if (ULONG_CMP_GE(jiffies, rsp->jiffies_force_qs) ||
			    (gf & RCU_GP_FLAG_FQS)) {
				trace_rcu_grace_period(rsp->name,
						       ACCESS_ONCE(rsp->gpnum),
						       TPS("fqsstart"));
				fqs_state = rcu_gp_fqs(rsp, fqs_state);
				trace_rcu_grace_period(rsp->name,
						       ACCESS_ONCE(rsp->gpnum),
						       TPS("fqsend"));
				cond_resched();
			} else {
				/* Deal with stray signal. */
				cond_resched();
				flush_signals(current);
				trace_rcu_grace_period(rsp->name,
						       ACCESS_ONCE(rsp->gpnum),
						       TPS("fqswaitsig"));
			}
			j = jiffies_till_next_fqs;
			if (j > HZ) {
				j = HZ;
				jiffies_till_next_fqs = HZ;
			} else if (j < 1) {
				j = 1;
				jiffies_till_next_fqs = 1;
			}
		}

		/* Handle grace-period end. */
		rcu_gp_cleanup(rsp);
	}
}

/*
 * Start a new RCU grace period if warranted, re-initializing the hierarchy
 * in preparation for detecting the next grace period.  The caller must hold
 * the root node's ->lock and hard irqs must be disabled.
 *
 * Note that it is legal for a dying CPU (which is marked as offline) to
 * invoke this function.  This can happen when the dying CPU reports its
 * quiescent state.
 *
 * Returns true if the grace-period kthread must be awakened.
 */
static bool
rcu_start_gp_advanced(struct rcu_state *rsp, struct rcu_node *rnp,
		      struct rcu_data *rdp)
{
	if (!rsp->gp_kthread || !cpu_needs_another_gp(rsp, rdp)) {
		/*
		 * Either we have not yet spawned the grace-period
		 * task, this CPU does not need another grace period,
		 * or a grace period is already in progress.
		 * Either way, don't start a new grace period.
		 */
		return false;
	}
	ACCESS_ONCE(rsp->gp_flags) = RCU_GP_FLAG_INIT;
	trace_rcu_grace_period(rsp->name, ACCESS_ONCE(rsp->gpnum),
			       TPS("newreq"));

	/*
	 * We can't do wakeups while holding the rnp->lock, as that
	 * could cause possible deadlocks with the rq->lock. Defer
	 * the wakeup to our caller.
	 */
	return true;
}

/*
 * Similar to rcu_start_gp_advanced(), but also advance the calling CPU's
 * callbacks.  Note that rcu_start_gp_advanced() cannot do this because it
 * is invoked indirectly from rcu_advance_cbs(), which would result in
 * endless recursion -- or would do so if it wasn't for the self-deadlock
 * that is encountered beforehand.
 *
 * Returns true if the grace-period kthread needs to be awakened.
 */
static bool rcu_start_gp(struct rcu_state *rsp)
{
	struct rcu_data *rdp = this_cpu_ptr(rsp->rda);
	struct rcu_node *rnp = rcu_get_root(rsp);
	bool ret = false;

	/*
	 * If there is no grace period in progress right now, any
	 * callbacks we have up to this point will be satisfied by the
	 * next grace period.  Also, advancing the callbacks reduces the
	 * probability of false positives from cpu_needs_another_gp()
	 * resulting in pointless grace periods.  So, advance callbacks
	 * then start the grace period!
	 */
	ret = rcu_advance_cbs(rsp, rnp, rdp) || ret;
	ret = rcu_start_gp_advanced(rsp, rnp, rdp) || ret;
	return ret;
}

/*
 * Report a full set of quiescent states to the specified rcu_state
 * data structure.  This involves cleaning up after the prior grace
 * period and letting rcu_start_gp() start up the next grace period
 * if one is needed.  Note that the caller must hold rnp->lock, which
 * is released before return.
 */
static void rcu_report_qs_rsp(struct rcu_state *rsp, unsigned long flags)
	__releases(rcu_get_root(rsp)->lock)
{
	WARN_ON_ONCE(!rcu_gp_in_progress(rsp));
	raw_spin_unlock_irqrestore(&rcu_get_root(rsp)->lock, flags);
	wake_up(&rsp->gp_wq);  /* Memory barrier implied by wake_up() path. */
}

/*
 * Similar to rcu_report_qs_rdp(), for which it is a helper function.
 * Allows quiescent states for a group of CPUs to be reported at one go
 * to the specified rcu_node structure, though all the CPUs in the group
 * must be represented by the same rcu_node structure (which need not be
 * a leaf rcu_node structure, though it often will be).  That structure's
 * lock must be held upon entry, and it is released before return.
 */
static void
rcu_report_qs_rnp(unsigned long mask, struct rcu_state *rsp,
		  struct rcu_node *rnp, unsigned long flags)
	__releases(rnp->lock)
{
	struct rcu_node *rnp_c;

	/* Walk up the rcu_node hierarchy. */
	for (;;) {
		if (!(rnp->qsmask & mask)) {

			/* Our bit has already been cleared, so done. */
			raw_spin_unlock_irqrestore(&rnp->lock, flags);
			return;
		}
		rnp->qsmask &= ~mask;
		trace_rcu_quiescent_state_report(rsp->name, rnp->gpnum,
						 mask, rnp->qsmask, rnp->level,
						 rnp->grplo, rnp->grphi,
						 !!rnp->gp_tasks);
		if (rnp->qsmask != 0 || rcu_preempt_blocked_readers_cgp(rnp)) {

			/* Other bits still set at this level, so done. */
			raw_spin_unlock_irqrestore(&rnp->lock, flags);
			return;
		}
		mask = rnp->grpmask;
		if (rnp->parent == NULL) {

			/* No more levels.  Exit loop holding root lock. */

			break;
		}
		raw_spin_unlock_irqrestore(&rnp->lock, flags);
		rnp_c = rnp;
		rnp = rnp->parent;
		raw_spin_lock_irqsave(&rnp->lock, flags);
		smp_mb__after_unlock_lock();
		WARN_ON_ONCE(rnp_c->qsmask);
	}

	/*
	 * Get here if we are the last CPU to pass through a quiescent
	 * state for this grace period.  Invoke rcu_report_qs_rsp()
	 * to clean up and start the next grace period if one is needed.
	 */
	rcu_report_qs_rsp(rsp, flags); /* releases rnp->lock. */
}

/*
 * Record a quiescent state for the specified CPU to that CPU's rcu_data
 * structure.  This must be either called from the specified CPU, or
 * called when the specified CPU is known to be offline (and when it is
 * also known that no other CPU is concurrently trying to help the offline
 * CPU).  The lastcomp argument is used to make sure we are still in the
 * grace period of interest.  We don't want to end the current grace period
 * based on quiescent states detected in an earlier grace period!
 */
static void
rcu_report_qs_rdp(int cpu, struct rcu_state *rsp, struct rcu_data *rdp)
{
	unsigned long flags;
	unsigned long mask;
	bool needwake;
	struct rcu_node *rnp;

	rnp = rdp->mynode;
	raw_spin_lock_irqsave(&rnp->lock, flags);
	smp_mb__after_unlock_lock();
	if (rdp->passed_quiesce == 0 || rdp->gpnum != rnp->gpnum ||
	    rnp->completed == rnp->gpnum) {

		/*
		 * The grace period in which this quiescent state was
		 * recorded has ended, so don't report it upwards.
		 * We will instead need a new quiescent state that lies
		 * within the current grace period.
		 */
		rdp->passed_quiesce = 0;	/* need qs for new gp. */
		raw_spin_unlock_irqrestore(&rnp->lock, flags);
		return;
	}
	mask = rdp->grpmask;
	if ((rnp->qsmask & mask) == 0) {
		raw_spin_unlock_irqrestore(&rnp->lock, flags);
	} else {
		rdp->qs_pending = 0;

		/*
		 * This GP can't end until cpu checks in, so all of our
		 * callbacks can be processed during the next GP.
		 */
		needwake = rcu_accelerate_cbs(rsp, rnp, rdp);

		rcu_report_qs_rnp(mask, rsp, rnp, flags); /* rlses rnp->lock */
		if (needwake)
			rcu_gp_kthread_wake(rsp);
	}
}

/*
 * Check to see if there is a new grace period of which this CPU
 * is not yet aware, and if so, set up local rcu_data state for it.
 * Otherwise, see if this CPU has just passed through its first
 * quiescent state for this grace period, and record that fact if so.
 */
static void
rcu_check_quiescent_state(struct rcu_state *rsp, struct rcu_data *rdp)
{
	/* Check for grace-period ends and beginnings. */
	note_gp_changes(rsp, rdp);

	/*
	 * Does this CPU still need to do its part for current grace period?
	 * If no, return and let the other CPUs do their part as well.
	 */
	if (!rdp->qs_pending)
		return;

	/*
	 * Was there a quiescent state since the beginning of the grace
	 * period? If no, then exit and wait for the next call.
	 */
	if (!rdp->passed_quiesce)
		return;

	/*
	 * Tell RCU we are done (but rcu_report_qs_rdp() will be the
	 * judge of that).
	 */
	rcu_report_qs_rdp(rdp->cpu, rsp, rdp);
}

#ifdef CONFIG_HOTPLUG_CPU

/*
 * Send the specified CPU's RCU callbacks to the orphanage.  The
 * specified CPU must be offline, and the caller must hold the
 * ->orphan_lock.
 */
static void
rcu_send_cbs_to_orphanage(int cpu, struct rcu_state *rsp,
			  struct rcu_node *rnp, struct rcu_data *rdp)
{
	/* No-CBs CPUs do not have orphanable callbacks. */
	if (rcu_is_nocb_cpu(rdp->cpu))
		return;

	/*
	 * Orphan the callbacks.  First adjust the counts.  This is safe
	 * because _rcu_barrier() excludes CPU-hotplug operations, so it
	 * cannot be running now.  Thus no memory barrier is required.
	 */
	if (rdp->nxtlist != NULL) {
		rsp->qlen_lazy += rdp->qlen_lazy;
		rsp->qlen += rdp->qlen;
		rdp->n_cbs_orphaned += rdp->qlen;
		rdp->qlen_lazy = 0;
		ACCESS_ONCE(rdp->qlen) = 0;
	}

	/*
	 * Next, move those callbacks still needing a grace period to
	 * the orphanage, where some other CPU will pick them up.
	 * Some of the callbacks might have gone partway through a grace
	 * period, but that is too bad.  They get to start over because we
	 * cannot assume that grace periods are synchronized across CPUs.
	 * We don't bother updating the ->nxttail[] array yet, instead
	 * we just reset the whole thing later on.
	 */
	if (*rdp->nxttail[RCU_DONE_TAIL] != NULL) {
		*rsp->orphan_nxttail = *rdp->nxttail[RCU_DONE_TAIL];
		rsp->orphan_nxttail = rdp->nxttail[RCU_NEXT_TAIL];
		*rdp->nxttail[RCU_DONE_TAIL] = NULL;
	}

	/*
	 * Then move the ready-to-invoke callbacks to the orphanage,
	 * where some other CPU will pick them up.  These will not be
	 * required to pass though another grace period: They are done.
	 */
	if (rdp->nxtlist != NULL) {
		*rsp->orphan_donetail = rdp->nxtlist;
		rsp->orphan_donetail = rdp->nxttail[RCU_DONE_TAIL];
	}

	/* Finally, initialize the rcu_data structure's list to empty.  */
	init_callback_list(rdp);
}

/*
 * Adopt the RCU callbacks from the specified rcu_state structure's
 * orphanage.  The caller must hold the ->orphan_lock.
 */
static void rcu_adopt_orphan_cbs(struct rcu_state *rsp, unsigned long flags)
{
	int i;
	struct rcu_data *rdp = raw_cpu_ptr(rsp->rda);

	/* No-CBs CPUs are handled specially. */
	if (rcu_nocb_adopt_orphan_cbs(rsp, rdp, flags))
		return;

	/* Do the accounting first. */
	rdp->qlen_lazy += rsp->qlen_lazy;
	rdp->qlen += rsp->qlen;
	rdp->n_cbs_adopted += rsp->qlen;
	if (rsp->qlen_lazy != rsp->qlen)
		rcu_idle_count_callbacks_posted();
	rsp->qlen_lazy = 0;
	rsp->qlen = 0;

	/*
	 * We do not need a memory barrier here because the only way we
	 * can get here if there is an rcu_barrier() in flight is if
	 * we are the task doing the rcu_barrier().
	 */

	/* First adopt the ready-to-invoke callbacks. */
	if (rsp->orphan_donelist != NULL) {
		*rsp->orphan_donetail = *rdp->nxttail[RCU_DONE_TAIL];
		*rdp->nxttail[RCU_DONE_TAIL] = rsp->orphan_donelist;
		for (i = RCU_NEXT_SIZE - 1; i >= RCU_DONE_TAIL; i--)
			if (rdp->nxttail[i] == rdp->nxttail[RCU_DONE_TAIL])
				rdp->nxttail[i] = rsp->orphan_donetail;
		rsp->orphan_donelist = NULL;
		rsp->orphan_donetail = &rsp->orphan_donelist;
	}

	/* And then adopt the callbacks that still need a grace period. */
	if (rsp->orphan_nxtlist != NULL) {
		*rdp->nxttail[RCU_NEXT_TAIL] = rsp->orphan_nxtlist;
		rdp->nxttail[RCU_NEXT_TAIL] = rsp->orphan_nxttail;
		rsp->orphan_nxtlist = NULL;
		rsp->orphan_nxttail = &rsp->orphan_nxtlist;
	}
}

/*
 * Trace the fact that this CPU is going offline.
 */
static void rcu_cleanup_dying_cpu(struct rcu_state *rsp)
{
	RCU_TRACE(unsigned long mask);
	RCU_TRACE(struct rcu_data *rdp = this_cpu_ptr(rsp->rda));
	RCU_TRACE(struct rcu_node *rnp = rdp->mynode);

	RCU_TRACE(mask = rdp->grpmask);
	trace_rcu_grace_period(rsp->name,
			       rnp->gpnum + 1 - !!(rnp->qsmask & mask),
			       TPS("cpuofl"));
}

/*
 * The CPU has been completely removed, and some other CPU is reporting
 * this fact from process context.  Do the remainder of the cleanup,
 * including orphaning the outgoing CPU's RCU callbacks, and also
 * adopting them.  There can only be one CPU hotplug operation at a time,
 * so no other CPU can be attempting to update rcu_cpu_kthread_task.
 */
static void rcu_cleanup_dead_cpu(int cpu, struct rcu_state *rsp)
{
	unsigned long flags;
	unsigned long mask;
	int need_report = 0;
	struct rcu_data *rdp = per_cpu_ptr(rsp->rda, cpu);
	struct rcu_node *rnp = rdp->mynode;  /* Outgoing CPU's rdp & rnp. */

	/* Adjust any no-longer-needed kthreads. */
	rcu_boost_kthread_setaffinity(rnp, -1);

	/* Remove the dead CPU from the bitmasks in the rcu_node hierarchy. */

	/* Exclude any attempts to start a new grace period. */
	mutex_lock(&rsp->onoff_mutex);
	raw_spin_lock_irqsave(&rsp->orphan_lock, flags);

	/* Orphan the dead CPU's callbacks, and adopt them if appropriate. */
	rcu_send_cbs_to_orphanage(cpu, rsp, rnp, rdp);
	rcu_adopt_orphan_cbs(rsp, flags);

	/* Remove the outgoing CPU from the masks in the rcu_node hierarchy. */
	mask = rdp->grpmask;	/* rnp->grplo is constant. */
	do {
		raw_spin_lock(&rnp->lock);	/* irqs already disabled. */
		smp_mb__after_unlock_lock();
		rnp->qsmaskinit &= ~mask;
		if (rnp->qsmaskinit != 0) {
			if (rnp != rdp->mynode)
				raw_spin_unlock(&rnp->lock); /* irqs remain disabled. */
			break;
		}
		if (rnp == rdp->mynode)
			need_report = rcu_preempt_offline_tasks(rsp, rnp, rdp);
		else
			raw_spin_unlock(&rnp->lock); /* irqs remain disabled. */
		mask = rnp->grpmask;
		rnp = rnp->parent;
	} while (rnp != NULL);

	/*
	 * We still hold the leaf rcu_node structure lock here, and
	 * irqs are still disabled.  The reason for this subterfuge is
	 * because invoking rcu_report_unblock_qs_rnp() with ->orphan_lock
	 * held leads to deadlock.
	 */
	raw_spin_unlock(&rsp->orphan_lock); /* irqs remain disabled. */
	rnp = rdp->mynode;
	if (need_report & RCU_OFL_TASKS_NORM_GP)
		rcu_report_unblock_qs_rnp(rnp, flags);
	else
		raw_spin_unlock_irqrestore(&rnp->lock, flags);
	if (need_report & RCU_OFL_TASKS_EXP_GP)
		rcu_report_exp_rnp(rsp, rnp, true);
	WARN_ONCE(rdp->qlen != 0 || rdp->nxtlist != NULL,
		  "rcu_cleanup_dead_cpu: Callbacks on offline CPU %d: qlen=%lu, nxtlist=%p\n",
		  cpu, rdp->qlen, rdp->nxtlist);
	init_callback_list(rdp);
	/* Disallow further callbacks on this CPU. */
	rdp->nxttail[RCU_NEXT_TAIL] = NULL;
	mutex_unlock(&rsp->onoff_mutex);
}

#else /* #ifdef CONFIG_HOTPLUG_CPU */

static void rcu_cleanup_dying_cpu(struct rcu_state *rsp)
{
}

static void rcu_cleanup_dead_cpu(int cpu, struct rcu_state *rsp)
{
}

#endif /* #else #ifdef CONFIG_HOTPLUG_CPU */

/*
 * Invoke any RCU callbacks that have made it to the end of their grace
 * period.  Thottle as specified by rdp->blimit.
 */
static void rcu_do_batch(struct rcu_state *rsp, struct rcu_data *rdp)
{
	unsigned long flags;
	struct rcu_head *next, *list, **tail;
	long bl, count, count_lazy;
	int i;

	/* If no callbacks are ready, just return. */
	if (!cpu_has_callbacks_ready_to_invoke(rdp)) {
		trace_rcu_batch_start(rsp->name, rdp->qlen_lazy, rdp->qlen, 0);
		trace_rcu_batch_end(rsp->name, 0, !!ACCESS_ONCE(rdp->nxtlist),
				    need_resched(), is_idle_task(current),
				    rcu_is_callbacks_kthread());
		return;
	}

	/*
	 * Extract the list of ready callbacks, disabling to prevent
	 * races with call_rcu() from interrupt handlers.
	 */
	local_irq_save(flags);
	WARN_ON_ONCE(cpu_is_offline(smp_processor_id()));
	bl = rdp->blimit;
	trace_rcu_batch_start(rsp->name, rdp->qlen_lazy, rdp->qlen, bl);
	list = rdp->nxtlist;
	rdp->nxtlist = *rdp->nxttail[RCU_DONE_TAIL];
	*rdp->nxttail[RCU_DONE_TAIL] = NULL;
	tail = rdp->nxttail[RCU_DONE_TAIL];
	for (i = RCU_NEXT_SIZE - 1; i >= 0; i--)
		if (rdp->nxttail[i] == rdp->nxttail[RCU_DONE_TAIL])
			rdp->nxttail[i] = &rdp->nxtlist;
	local_irq_restore(flags);

	/* Invoke callbacks. */
	count = count_lazy = 0;
	while (list) {
		next = list->next;
		prefetch(next);
		debug_rcu_head_unqueue(list);
		if (__rcu_reclaim(rsp->name, list))
			count_lazy++;
		list = next;
		/* Stop only if limit reached and CPU has something to do. */
		if (++count >= bl &&
		    (need_resched() ||
		     (!is_idle_task(current) && !rcu_is_callbacks_kthread())))
			break;
	}

	local_irq_save(flags);
	trace_rcu_batch_end(rsp->name, count, !!list, need_resched(),
			    is_idle_task(current),
			    rcu_is_callbacks_kthread());

	/* Update count, and requeue any remaining callbacks. */
	if (list != NULL) {
		*tail = rdp->nxtlist;
		rdp->nxtlist = list;
		for (i = 0; i < RCU_NEXT_SIZE; i++)
			if (&rdp->nxtlist == rdp->nxttail[i])
				rdp->nxttail[i] = tail;
			else
				break;
	}
	smp_mb(); /* List handling before counting for rcu_barrier(). */
	rdp->qlen_lazy -= count_lazy;
	ACCESS_ONCE(rdp->qlen) = rdp->qlen - count;
	rdp->n_cbs_invoked += count;

	/* Reinstate batch limit if we have worked down the excess. */
	if (rdp->blimit == LONG_MAX && rdp->qlen <= qlowmark)
		rdp->blimit = blimit;

	/* Reset ->qlen_last_fqs_check trigger if enough CBs have drained. */
	if (rdp->qlen == 0 && rdp->qlen_last_fqs_check != 0) {
		rdp->qlen_last_fqs_check = 0;
		rdp->n_force_qs_snap = rsp->n_force_qs;
	} else if (rdp->qlen < rdp->qlen_last_fqs_check - qhimark)
		rdp->qlen_last_fqs_check = rdp->qlen;
	WARN_ON_ONCE((rdp->nxtlist == NULL) != (rdp->qlen == 0));

	local_irq_restore(flags);

	/* Re-invoke RCU core processing if there are callbacks remaining. */
	if (cpu_has_callbacks_ready_to_invoke(rdp))
		invoke_rcu_core();
}

/*
 * Check to see if this CPU is in a non-context-switch quiescent state
 * (user mode or idle loop for rcu, non-softirq execution for rcu_bh).
 * Also schedule RCU core processing.
 *
 * This function must be called from hardirq context.  It is normally
 * invoked from the scheduling-clock interrupt.  If rcu_pending returns
 * false, there is no point in invoking rcu_check_callbacks().
 */
void rcu_check_callbacks(int cpu, int user)
{
	trace_rcu_utilization(TPS("Start scheduler-tick"));
	increment_cpu_stall_ticks();
	if (user || rcu_is_cpu_rrupt_from_idle()) {

		/*
		 * Get here if this CPU took its interrupt from user
		 * mode or from the idle loop, and if this is not a
		 * nested interrupt.  In this case, the CPU is in
		 * a quiescent state, so note it.
		 *
		 * No memory barrier is required here because both
		 * rcu_sched_qs() and rcu_bh_qs() reference only CPU-local
		 * variables that other CPUs neither access nor modify,
		 * at least not while the corresponding CPU is online.
		 */

		rcu_sched_qs(cpu);
		rcu_bh_qs(cpu);

	} else if (!in_softirq()) {

		/*
		 * Get here if this CPU did not take its interrupt from
		 * softirq, in other words, if it is not interrupting
		 * a rcu_bh read-side critical section.  This is an _bh
		 * critical section, so note it.
		 */

		rcu_bh_qs(cpu);
	}
	rcu_preempt_check_callbacks(cpu);
	if (rcu_pending(cpu))
		invoke_rcu_core();
	trace_rcu_utilization(TPS("End scheduler-tick"));
}

/*
 * Scan the leaf rcu_node structures, processing dyntick state for any that
 * have not yet encountered a quiescent state, using the function specified.
 * Also initiate boosting for any threads blocked on the root rcu_node.
 *
 * The caller must have suppressed start of new grace periods.
 */
static void force_qs_rnp(struct rcu_state *rsp,
			 int (*f)(struct rcu_data *rsp, bool *isidle,
				  unsigned long *maxj),
			 bool *isidle, unsigned long *maxj)
{
	unsigned long bit;
	int cpu;
	unsigned long flags;
	unsigned long mask;
	struct rcu_node *rnp;

	rcu_for_each_leaf_node(rsp, rnp) {
		cond_resched();
		mask = 0;
		raw_spin_lock_irqsave(&rnp->lock, flags);
		smp_mb__after_unlock_lock();
		if (!rcu_gp_in_progress(rsp)) {
			raw_spin_unlock_irqrestore(&rnp->lock, flags);
			return;
		}
		if (rnp->qsmask == 0) {
			rcu_initiate_boost(rnp, flags); /* releases rnp->lock */
			continue;
		}
		cpu = rnp->grplo;
		bit = 1;
		for (; cpu <= rnp->grphi; cpu++, bit <<= 1) {
			if ((rnp->qsmask & bit) != 0) {
				if ((rnp->qsmaskinit & bit) != 0)
					*isidle = 0;
				if (f(per_cpu_ptr(rsp->rda, cpu), isidle, maxj))
					mask |= bit;
			}
		}
		if (mask != 0) {

			/* rcu_report_qs_rnp() releases rnp->lock. */
			rcu_report_qs_rnp(mask, rsp, rnp, flags);
			continue;
		}
		raw_spin_unlock_irqrestore(&rnp->lock, flags);
	}
	rnp = rcu_get_root(rsp);
	if (rnp->qsmask == 0) {
		raw_spin_lock_irqsave(&rnp->lock, flags);
		smp_mb__after_unlock_lock();
		rcu_initiate_boost(rnp, flags); /* releases rnp->lock. */
	}
}

/*
 * Force quiescent states on reluctant CPUs, and also detect which
 * CPUs are in dyntick-idle mode.
 */
static void force_quiescent_state(struct rcu_state *rsp)
{
	unsigned long flags;
	bool ret;
	struct rcu_node *rnp;
	struct rcu_node *rnp_old = NULL;

	/* Funnel through hierarchy to reduce memory contention. */
	rnp = __this_cpu_read(rsp->rda->mynode);
	for (; rnp != NULL; rnp = rnp->parent) {
		ret = (ACCESS_ONCE(rsp->gp_flags) & RCU_GP_FLAG_FQS) ||
		      !raw_spin_trylock(&rnp->fqslock);
		if (rnp_old != NULL)
			raw_spin_unlock(&rnp_old->fqslock);
		if (ret) {
			rsp->n_force_qs_lh++;
			return;
		}
		rnp_old = rnp;
	}
	/* rnp_old == rcu_get_root(rsp), rnp == NULL. */

	/* Reached the root of the rcu_node tree, acquire lock. */
	raw_spin_lock_irqsave(&rnp_old->lock, flags);
	smp_mb__after_unlock_lock();
	raw_spin_unlock(&rnp_old->fqslock);
	if (ACCESS_ONCE(rsp->gp_flags) & RCU_GP_FLAG_FQS) {
		rsp->n_force_qs_lh++;
		raw_spin_unlock_irqrestore(&rnp_old->lock, flags);
		return;  /* Someone beat us to it. */
	}
	ACCESS_ONCE(rsp->gp_flags) |= RCU_GP_FLAG_FQS;
	raw_spin_unlock_irqrestore(&rnp_old->lock, flags);
	wake_up(&rsp->gp_wq);  /* Memory barrier implied by wake_up() path. */
}

/*
 * This does the RCU core processing work for the specified rcu_state
 * and rcu_data structures.  This may be called only from the CPU to
 * whom the rdp belongs.
 */
static void
__rcu_process_callbacks(struct rcu_state *rsp)
{
	unsigned long flags;
	bool needwake;
	struct rcu_data *rdp = raw_cpu_ptr(rsp->rda);

	WARN_ON_ONCE(rdp->beenonline == 0);

	/* Update RCU state based on any recent quiescent states. */
	rcu_check_quiescent_state(rsp, rdp);

	/* Does this CPU require a not-yet-started grace period? */
	local_irq_save(flags);
	if (cpu_needs_another_gp(rsp, rdp)) {
		raw_spin_lock(&rcu_get_root(rsp)->lock); /* irqs disabled. */
		needwake = rcu_start_gp(rsp);
		raw_spin_unlock_irqrestore(&rcu_get_root(rsp)->lock, flags);
		if (needwake)
			rcu_gp_kthread_wake(rsp);
	} else {
		local_irq_restore(flags);
	}

	/* If there are callbacks ready, invoke them. */
	if (cpu_has_callbacks_ready_to_invoke(rdp))
		invoke_rcu_callbacks(rsp, rdp);

	/* Do any needed deferred wakeups of rcuo kthreads. */
	do_nocb_deferred_wakeup(rdp);
}

/*
 * Do RCU core processing for the current CPU.
 */
static void rcu_process_callbacks(struct softirq_action *unused)
{
	struct rcu_state *rsp;

	if (cpu_is_offline(smp_processor_id()))
		return;
	trace_rcu_utilization(TPS("Start RCU core"));
	for_each_rcu_flavor(rsp)
		__rcu_process_callbacks(rsp);
	trace_rcu_utilization(TPS("End RCU core"));
}

/*
 * Schedule RCU callback invocation.  If the specified type of RCU
 * does not support RCU priority boosting, just do a direct call,
 * otherwise wake up the per-CPU kernel kthread.  Note that because we
 * are running on the current CPU with interrupts disabled, the
 * rcu_cpu_kthread_task cannot disappear out from under us.
 */
static void invoke_rcu_callbacks(struct rcu_state *rsp, struct rcu_data *rdp)
{
	if (unlikely(!ACCESS_ONCE(rcu_scheduler_fully_active)))
		return;
	if (likely(!rsp->boost)) {
		rcu_do_batch(rsp, rdp);
		return;
	}
	invoke_rcu_callbacks_kthread();
}

static void invoke_rcu_core(void)
{
	if (cpu_online(smp_processor_id()))
		raise_softirq(RCU_SOFTIRQ);
}

/*
 * Handle any core-RCU processing required by a call_rcu() invocation.
 */
static void __call_rcu_core(struct rcu_state *rsp, struct rcu_data *rdp,
			    struct rcu_head *head, unsigned long flags)
{
	bool needwake;

	/*
	 * If called from an extended quiescent state, invoke the RCU
	 * core in order to force a re-evaluation of RCU's idleness.
	 */
	if (!rcu_is_watching() && cpu_online(smp_processor_id()))
		invoke_rcu_core();

	/* If interrupts were disabled or CPU offline, don't invoke RCU core. */
	if (irqs_disabled_flags(flags) || cpu_is_offline(smp_processor_id()))
		return;

	/*
	 * Force the grace period if too many callbacks or too long waiting.
	 * Enforce hysteresis, and don't invoke force_quiescent_state()
	 * if some other CPU has recently done so.  Also, don't bother
	 * invoking force_quiescent_state() if the newly enqueued callback
	 * is the only one waiting for a grace period to complete.
	 */
	if (unlikely(rdp->qlen > rdp->qlen_last_fqs_check + qhimark)) {

		/* Are we ignoring a completed grace period? */
		note_gp_changes(rsp, rdp);

		/* Start a new grace period if one not already started. */
		if (!rcu_gp_in_progress(rsp)) {
			struct rcu_node *rnp_root = rcu_get_root(rsp);

			raw_spin_lock(&rnp_root->lock);
			smp_mb__after_unlock_lock();
			needwake = rcu_start_gp(rsp);
			raw_spin_unlock(&rnp_root->lock);
			if (needwake)
				rcu_gp_kthread_wake(rsp);
		} else {
			/* Give the grace period a kick. */
			rdp->blimit = LONG_MAX;
			if (rsp->n_force_qs == rdp->n_force_qs_snap &&
			    *rdp->nxttail[RCU_DONE_TAIL] != head)
				force_quiescent_state(rsp);
			rdp->n_force_qs_snap = rsp->n_force_qs;
			rdp->qlen_last_fqs_check = rdp->qlen;
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
 * Helper function for call_rcu() and friends.  The cpu argument will
 * normally be -1, indicating "currently running CPU".  It may specify
 * a CPU only if that CPU is a no-CBs CPU.  Currently, only _rcu_barrier()
 * is expected to specify a CPU.
 */
static void
__call_rcu(struct rcu_head *head, void (*func)(struct rcu_head *rcu),
	   struct rcu_state *rsp, int cpu, bool lazy)
{
	unsigned long flags;
	struct rcu_data *rdp;

	WARN_ON_ONCE((unsigned long)head & 0x1); /* Misaligned rcu_head! */
	if (debug_rcu_head_queue(head)) {
		/* Probable double call_rcu(), so leak the callback. */
		ACCESS_ONCE(head->func) = rcu_leak_callback;
		WARN_ONCE(1, "__call_rcu(): Leaked duplicate callback\n");
		return;
	}
	head->func = func;
	head->next = NULL;

	/*
	 * Opportunistically note grace-period endings and beginnings.
	 * Note that we might see a beginning right after we see an
	 * end, but never vice versa, since this CPU has to pass through
	 * a quiescent state betweentimes.
	 */
	local_irq_save(flags);
	rdp = this_cpu_ptr(rsp->rda);

	/* Add the callback to our list. */
	if (unlikely(rdp->nxttail[RCU_NEXT_TAIL] == NULL) || cpu != -1) {
		int offline;

		if (cpu != -1)
			rdp = per_cpu_ptr(rsp->rda, cpu);
		offline = !__call_rcu_nocb(rdp, head, lazy, flags);
		WARN_ON_ONCE(offline);
		/* _call_rcu() is illegal on offline CPU; leak the callback. */
		local_irq_restore(flags);
		return;
	}
	ACCESS_ONCE(rdp->qlen) = rdp->qlen + 1;
	if (lazy)
		rdp->qlen_lazy++;
	else
		rcu_idle_count_callbacks_posted();
	smp_mb();  /* Count before adding callback for rcu_barrier(). */
	*rdp->nxttail[RCU_NEXT_TAIL] = head;
	rdp->nxttail[RCU_NEXT_TAIL] = &head->next;

	if (__is_kfree_rcu_offset((unsigned long)func))
		trace_rcu_kfree_callback(rsp->name, head, (unsigned long)func,
					 rdp->qlen_lazy, rdp->qlen);
	else
		trace_rcu_callback(rsp->name, head, rdp->qlen_lazy, rdp->qlen);

	/* Go handle any RCU core processing required. */
	__call_rcu_core(rsp, rdp, head, flags);
	local_irq_restore(flags);
}

/*
 * Queue an RCU-sched callback for invocation after a grace period.
 */
void call_rcu_sched(struct rcu_head *head, void (*func)(struct rcu_head *rcu))
{
	__call_rcu(head, func, &rcu_sched_state, -1, 0);
}
EXPORT_SYMBOL_GPL(call_rcu_sched);

/*
 * Queue an RCU callback for invocation after a quicker grace period.
 */
void call_rcu_bh(struct rcu_head *head, void (*func)(struct rcu_head *rcu))
{
	__call_rcu(head, func, &rcu_bh_state, -1, 0);
}
EXPORT_SYMBOL_GPL(call_rcu_bh);

/*
 * Queue an RCU callback for lazy invocation after a grace period.
 * This will likely be later named something like "call_rcu_lazy()",
 * but this change will require some way of tagging the lazy RCU
 * callbacks in the list of pending callbacks. Until then, this
 * function may only be called from __kfree_rcu().
 */
void kfree_call_rcu(struct rcu_head *head,
		    void (*func)(struct rcu_head *rcu))
{
	__call_rcu(head, func, rcu_state_p, -1, 1);
}
EXPORT_SYMBOL_GPL(kfree_call_rcu);

/*
 * Because a context switch is a grace period for RCU-sched and RCU-bh,
 * any blocking grace-period wait automatically implies a grace period
 * if there is only one CPU online at any point time during execution
 * of either synchronize_sched() or synchronize_rcu_bh().  It is OK to
 * occasionally incorrectly indicate that there are multiple CPUs online
 * when there was in fact only one the whole time, as this just adds
 * some overhead: RCU still operates correctly.
 */
static inline int rcu_blocking_is_gp(void)
{
	int ret;

	might_sleep();  /* Check for RCU read-side critical section. */
	preempt_disable();
	ret = num_online_cpus() <= 1;
	preempt_enable();
	return ret;
}

/**
 * synchronize_sched - wait until an rcu-sched grace period has elapsed.
 *
 * Control will return to the caller some time after a full rcu-sched
 * grace period has elapsed, in other words after all currently executing
 * rcu-sched read-side critical sections have completed.   These read-side
 * critical sections are delimited by rcu_read_lock_sched() and
 * rcu_read_unlock_sched(), and may be nested.  Note that preempt_disable(),
 * local_irq_disable(), and so on may be used in place of
 * rcu_read_lock_sched().
 *
 * This means that all preempt_disable code sequences, including NMI and
 * non-threaded hardware-interrupt handlers, in progress on entry will
 * have completed before this primitive returns.  However, this does not
 * guarantee that softirq handlers will have completed, since in some
 * kernels, these handlers can run in process context, and can block.
 *
 * Note that this guarantee implies further memory-ordering guarantees.
 * On systems with more than one CPU, when synchronize_sched() returns,
 * each CPU is guaranteed to have executed a full memory barrier since the
 * end of its last RCU-sched read-side critical section whose beginning
 * preceded the call to synchronize_sched().  In addition, each CPU having
 * an RCU read-side critical section that extends beyond the return from
 * synchronize_sched() is guaranteed to have executed a full memory barrier
 * after the beginning of synchronize_sched() and before the beginning of
 * that RCU read-side critical section.  Note that these guarantees include
 * CPUs that are offline, idle, or executing in user mode, as well as CPUs
 * that are executing in the kernel.
 *
 * Furthermore, if CPU A invoked synchronize_sched(), which returned
 * to its caller on CPU B, then both CPU A and CPU B are guaranteed
 * to have executed a full memory barrier during the execution of
 * synchronize_sched() -- even if CPU A and CPU B are the same CPU (but
 * again only if the system has more than one CPU).
 *
 * This primitive provides the guarantees made by the (now removed)
 * synchronize_kernel() API.  In contrast, synchronize_rcu() only
 * guarantees that rcu_read_lock() sections will have completed.
 * In "classic RCU", these two guarantees happen to be one and
 * the same, but can differ in realtime RCU implementations.
 */
void synchronize_sched(void)
{
	rcu_lockdep_assert(!lock_is_held(&rcu_bh_lock_map) &&
			   !lock_is_held(&rcu_lock_map) &&
			   !lock_is_held(&rcu_sched_lock_map),
			   "Illegal synchronize_sched() in RCU-sched read-side critical section");
	if (rcu_blocking_is_gp())
		return;
	if (rcu_expedited)
		synchronize_sched_expedited();
	else
		wait_rcu_gp(call_rcu_sched);
}
EXPORT_SYMBOL_GPL(synchronize_sched);

/**
 * synchronize_rcu_bh - wait until an rcu_bh grace period has elapsed.
 *
 * Control will return to the caller some time after a full rcu_bh grace
 * period has elapsed, in other words after all currently executing rcu_bh
 * read-side critical sections have completed.  RCU read-side critical
 * sections are delimited by rcu_read_lock_bh() and rcu_read_unlock_bh(),
 * and may be nested.
 *
 * See the description of synchronize_sched() for more detailed information
 * on memory ordering guarantees.
 */
void synchronize_rcu_bh(void)
{
	rcu_lockdep_assert(!lock_is_held(&rcu_bh_lock_map) &&
			   !lock_is_held(&rcu_lock_map) &&
			   !lock_is_held(&rcu_sched_lock_map),
			   "Illegal synchronize_rcu_bh() in RCU-bh read-side critical section");
	if (rcu_blocking_is_gp())
		return;
	if (rcu_expedited)
		synchronize_rcu_bh_expedited();
	else
		wait_rcu_gp(call_rcu_bh);
}
EXPORT_SYMBOL_GPL(synchronize_rcu_bh);

/**
 * get_state_synchronize_rcu - Snapshot current RCU state
 *
 * Returns a cookie that is used by a later call to cond_synchronize_rcu()
 * to determine whether or not a full grace period has elapsed in the
 * meantime.
 */
unsigned long get_state_synchronize_rcu(void)
{
	/*
	 * Any prior manipulation of RCU-protected data must happen
	 * before the load from ->gpnum.
	 */
	smp_mb();  /* ^^^ */

	/*
	 * Make sure this load happens before the purportedly
	 * time-consuming work between get_state_synchronize_rcu()
	 * and cond_synchronize_rcu().
	 */
	return smp_load_acquire(&rcu_state_p->gpnum);
}
EXPORT_SYMBOL_GPL(get_state_synchronize_rcu);

/**
 * cond_synchronize_rcu - Conditionally wait for an RCU grace period
 *
 * @oldstate: return value from earlier call to get_state_synchronize_rcu()
 *
 * If a full RCU grace period has elapsed since the earlier call to
 * get_state_synchronize_rcu(), just return.  Otherwise, invoke
 * synchronize_rcu() to wait for a full grace period.
 *
 * Yes, this function does not take counter wrap into account.  But
 * counter wrap is harmless.  If the counter wraps, we have waited for
 * more than 2 billion grace periods (and way more on a 64-bit system!),
 * so waiting for one additional grace period should be just fine.
 */
void cond_synchronize_rcu(unsigned long oldstate)
{
	unsigned long newstate;

	/*
	 * Ensure that this load happens before any RCU-destructive
	 * actions the caller might carry out after we return.
	 */
	newstate = smp_load_acquire(&rcu_state_p->completed);
	if (ULONG_CMP_GE(oldstate, newstate))
		synchronize_rcu();
}
EXPORT_SYMBOL_GPL(cond_synchronize_rcu);

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
 * Note that it is illegal to call this function while holding any lock
 * that is acquired by a CPU-hotplug notifier.  And yes, it is also illegal
 * to call this function from a CPU-hotplug notifier.  Failing to observe
 * these restriction will result in deadlock.
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
	long firstsnap, s, snap;
	int trycount = 0;
	struct rcu_state *rsp = &rcu_sched_state;

	/*
	 * If we are in danger of counter wrap, just do synchronize_sched().
	 * By allowing sync_sched_expedited_started to advance no more than
	 * ULONG_MAX/8 ahead of sync_sched_expedited_done, we are ensuring
	 * that more than 3.5 billion CPUs would be required to force a
	 * counter wrap on a 32-bit system.  Quite a few more CPUs would of
	 * course be required on a 64-bit system.
	 */
	if (ULONG_CMP_GE((ulong)atomic_long_read(&rsp->expedited_start),
			 (ulong)atomic_long_read(&rsp->expedited_done) +
			 ULONG_MAX / 8)) {
		synchronize_sched();
		atomic_long_inc(&rsp->expedited_wrap);
		return;
	}

	/*
	 * Take a ticket.  Note that atomic_inc_return() implies a
	 * full memory barrier.
	 */
	snap = atomic_long_inc_return(&rsp->expedited_start);
	firstsnap = snap;
	get_online_cpus();
	WARN_ON_ONCE(cpu_is_offline(raw_smp_processor_id()));

	/*
	 * Each pass through the following loop attempts to force a
	 * context switch on each CPU.
	 */
	while (try_stop_cpus(cpu_online_mask,
			     synchronize_sched_expedited_cpu_stop,
			     NULL) == -EAGAIN) {
		put_online_cpus();
		atomic_long_inc(&rsp->expedited_tryfail);

		/* Check to see if someone else did our work for us. */
		s = atomic_long_read(&rsp->expedited_done);
		if (ULONG_CMP_GE((ulong)s, (ulong)firstsnap)) {
			/* ensure test happens before caller kfree */
			smp_mb__before_atomic(); /* ^^^ */
			atomic_long_inc(&rsp->expedited_workdone1);
			return;
		}

		/* No joy, try again later.  Or just synchronize_sched(). */
		if (trycount++ < 10) {
			udelay(trycount * num_online_cpus());
		} else {
			wait_rcu_gp(call_rcu_sched);
			atomic_long_inc(&rsp->expedited_normal);
			return;
		}

		/* Recheck to see if someone else did our work for us. */
		s = atomic_long_read(&rsp->expedited_done);
		if (ULONG_CMP_GE((ulong)s, (ulong)firstsnap)) {
			/* ensure test happens before caller kfree */
			smp_mb__before_atomic(); /* ^^^ */
			atomic_long_inc(&rsp->expedited_workdone2);
			return;
		}

		/*
		 * Refetching sync_sched_expedited_started allows later
		 * callers to piggyback on our grace period.  We retry
		 * after they started, so our grace period works for them,
		 * and they started after our first try, so their grace
		 * period works for us.
		 */
		get_online_cpus();
		snap = atomic_long_read(&rsp->expedited_start);
		smp_mb(); /* ensure read is before try_stop_cpus(). */
	}
	atomic_long_inc(&rsp->expedited_stoppedcpus);

	/*
	 * Everyone up to our most recent fetch is covered by our grace
	 * period.  Update the counter, but only if our work is still
	 * relevant -- which it won't be if someone who started later
	 * than we did already did their update.
	 */
	do {
		atomic_long_inc(&rsp->expedited_done_tries);
		s = atomic_long_read(&rsp->expedited_done);
		if (ULONG_CMP_GE((ulong)s, (ulong)snap)) {
			/* ensure test happens before caller kfree */
			smp_mb__before_atomic(); /* ^^^ */
			atomic_long_inc(&rsp->expedited_done_lost);
			break;
		}
	} while (atomic_long_cmpxchg(&rsp->expedited_done, s, snap) != s);
	atomic_long_inc(&rsp->expedited_done_exit);

	put_online_cpus();
}
EXPORT_SYMBOL_GPL(synchronize_sched_expedited);

/*
 * Check to see if there is any immediate RCU-related work to be done
 * by the current CPU, for the specified type of RCU, returning 1 if so.
 * The checks are in order of increasing expense: checks that can be
 * carried out against CPU-local state are performed first.  However,
 * we must check for CPU stalls first, else we might not get a chance.
 */
static int __rcu_pending(struct rcu_state *rsp, struct rcu_data *rdp)
{
	struct rcu_node *rnp = rdp->mynode;

	rdp->n_rcu_pending++;

	/* Check for CPU stalls, if enabled. */
	check_cpu_stall(rsp, rdp);

	/* Is this CPU a NO_HZ_FULL CPU that should ignore RCU? */
	if (rcu_nohz_full_cpu(rsp))
		return 0;

	/* Is the RCU core waiting for a quiescent state from this CPU? */
	if (rcu_scheduler_fully_active &&
	    rdp->qs_pending && !rdp->passed_quiesce) {
		rdp->n_rp_qs_pending++;
	} else if (rdp->qs_pending && rdp->passed_quiesce) {
		rdp->n_rp_report_qs++;
		return 1;
	}

	/* Does this CPU have callbacks ready to invoke? */
	if (cpu_has_callbacks_ready_to_invoke(rdp)) {
		rdp->n_rp_cb_ready++;
		return 1;
	}

	/* Has RCU gone idle with this CPU needing another grace period? */
	if (cpu_needs_another_gp(rsp, rdp)) {
		rdp->n_rp_cpu_needs_gp++;
		return 1;
	}

	/* Has another RCU grace period completed?  */
	if (ACCESS_ONCE(rnp->completed) != rdp->completed) { /* outside lock */
		rdp->n_rp_gp_completed++;
		return 1;
	}

	/* Has a new RCU grace period started? */
	if (ACCESS_ONCE(rnp->gpnum) != rdp->gpnum) { /* outside lock */
		rdp->n_rp_gp_started++;
		return 1;
	}

	/* Does this CPU need a deferred NOCB wakeup? */
	if (rcu_nocb_need_deferred_wakeup(rdp)) {
		rdp->n_rp_nocb_defer_wakeup++;
		return 1;
	}

	/* nothing to do */
	rdp->n_rp_need_nothing++;
	return 0;
}

/*
 * Check to see if there is any immediate RCU-related work to be done
 * by the current CPU, returning 1 if so.  This function is part of the
 * RCU implementation; it is -not- an exported member of the RCU API.
 */
static int rcu_pending(int cpu)
{
	struct rcu_state *rsp;

	for_each_rcu_flavor(rsp)
		if (__rcu_pending(rsp, per_cpu_ptr(rsp->rda, cpu)))
			return 1;
	return 0;
}

/*
 * Return true if the specified CPU has any callback.  If all_lazy is
 * non-NULL, store an indication of whether all callbacks are lazy.
 * (If there are no callbacks, all of them are deemed to be lazy.)
 */
static int __maybe_unused rcu_cpu_has_callbacks(int cpu, bool *all_lazy)
{
	bool al = true;
	bool hc = false;
	struct rcu_data *rdp;
	struct rcu_state *rsp;

	for_each_rcu_flavor(rsp) {
		rdp = per_cpu_ptr(rsp->rda, cpu);
		if (!rdp->nxtlist)
			continue;
		hc = true;
		if (rdp->qlen != rdp->qlen_lazy || !all_lazy) {
			al = false;
			break;
		}
	}
	if (all_lazy)
		*all_lazy = al;
	return hc;
}

/*
 * Helper function for _rcu_barrier() tracing.  If tracing is disabled,
 * the compiler is expected to optimize this away.
 */
static void _rcu_barrier_trace(struct rcu_state *rsp, const char *s,
			       int cpu, unsigned long done)
{
	trace_rcu_barrier(rsp->name, s, cpu,
			  atomic_read(&rsp->barrier_cpu_count), done);
}

/*
 * RCU callback function for _rcu_barrier().  If we are last, wake
 * up the task executing _rcu_barrier().
 */
static void rcu_barrier_callback(struct rcu_head *rhp)
{
	struct rcu_data *rdp = container_of(rhp, struct rcu_data, barrier_head);
	struct rcu_state *rsp = rdp->rsp;

	if (atomic_dec_and_test(&rsp->barrier_cpu_count)) {
		_rcu_barrier_trace(rsp, "LastCB", -1, rsp->n_barrier_done);
		complete(&rsp->barrier_completion);
	} else {
		_rcu_barrier_trace(rsp, "CB", -1, rsp->n_barrier_done);
	}
}

/*
 * Called with preemption disabled, and from cross-cpu IRQ context.
 */
static void rcu_barrier_func(void *type)
{
	struct rcu_state *rsp = type;
	struct rcu_data *rdp = raw_cpu_ptr(rsp->rda);

	_rcu_barrier_trace(rsp, "IRQ", -1, rsp->n_barrier_done);
	atomic_inc(&rsp->barrier_cpu_count);
	rsp->call(&rdp->barrier_head, rcu_barrier_callback);
}

/*
 * Orchestrate the specified type of RCU barrier, waiting for all
 * RCU callbacks of the specified type to complete.
 */
static void _rcu_barrier(struct rcu_state *rsp)
{
	int cpu;
	struct rcu_data *rdp;
	unsigned long snap = ACCESS_ONCE(rsp->n_barrier_done);
	unsigned long snap_done;

	_rcu_barrier_trace(rsp, "Begin", -1, snap);

	/* Take mutex to serialize concurrent rcu_barrier() requests. */
	mutex_lock(&rsp->barrier_mutex);

	/*
	 * Ensure that all prior references, including to ->n_barrier_done,
	 * are ordered before the _rcu_barrier() machinery.
	 */
	smp_mb();  /* See above block comment. */

	/*
	 * Recheck ->n_barrier_done to see if others did our work for us.
	 * This means checking ->n_barrier_done for an even-to-odd-to-even
	 * transition.  The "if" expression below therefore rounds the old
	 * value up to the next even number and adds two before comparing.
	 */
	snap_done = rsp->n_barrier_done;
	_rcu_barrier_trace(rsp, "Check", -1, snap_done);

	/*
	 * If the value in snap is odd, we needed to wait for the current
	 * rcu_barrier() to complete, then wait for the next one, in other
	 * words, we need the value of snap_done to be three larger than
	 * the value of snap.  On the other hand, if the value in snap is
	 * even, we only had to wait for the next rcu_barrier() to complete,
	 * in other words, we need the value of snap_done to be only two
	 * greater than the value of snap.  The "(snap + 3) & ~0x1" computes
	 * this for us (thank you, Linus!).
	 */
	if (ULONG_CMP_GE(snap_done, (snap + 3) & ~0x1)) {
		_rcu_barrier_trace(rsp, "EarlyExit", -1, snap_done);
		smp_mb(); /* caller's subsequent code after above check. */
		mutex_unlock(&rsp->barrier_mutex);
		return;
	}

	/*
	 * Increment ->n_barrier_done to avoid duplicate work.  Use
	 * ACCESS_ONCE() to prevent the compiler from speculating
	 * the increment to precede the early-exit check.
	 */
	ACCESS_ONCE(rsp->n_barrier_done) = rsp->n_barrier_done + 1;
	WARN_ON_ONCE((rsp->n_barrier_done & 0x1) != 1);
	_rcu_barrier_trace(rsp, "Inc1", -1, rsp->n_barrier_done);
	smp_mb(); /* Order ->n_barrier_done increment with below mechanism. */

	/*
	 * Initialize the count to one rather than to zero in order to
	 * avoid a too-soon return to zero in case of a short grace period
	 * (or preemption of this task).  Exclude CPU-hotplug operations
	 * to ensure that no offline CPU has callbacks queued.
	 */
	init_completion(&rsp->barrier_completion);
	atomic_set(&rsp->barrier_cpu_count, 1);
	get_online_cpus();

	/*
	 * Force each CPU with callbacks to register a new callback.
	 * When that callback is invoked, we will know that all of the
	 * corresponding CPU's preceding callbacks have been invoked.
	 */
	for_each_possible_cpu(cpu) {
		if (!cpu_online(cpu) && !rcu_is_nocb_cpu(cpu))
			continue;
		rdp = per_cpu_ptr(rsp->rda, cpu);
		if (rcu_is_nocb_cpu(cpu)) {
			_rcu_barrier_trace(rsp, "OnlineNoCB", cpu,
					   rsp->n_barrier_done);
			atomic_inc(&rsp->barrier_cpu_count);
			__call_rcu(&rdp->barrier_head, rcu_barrier_callback,
				   rsp, cpu, 0);
		} else if (ACCESS_ONCE(rdp->qlen)) {
			_rcu_barrier_trace(rsp, "OnlineQ", cpu,
					   rsp->n_barrier_done);
			smp_call_function_single(cpu, rcu_barrier_func, rsp, 1);
		} else {
			_rcu_barrier_trace(rsp, "OnlineNQ", cpu,
					   rsp->n_barrier_done);
		}
	}
	put_online_cpus();

	/*
	 * Now that we have an rcu_barrier_callback() callback on each
	 * CPU, and thus each counted, remove the initial count.
	 */
	if (atomic_dec_and_test(&rsp->barrier_cpu_count))
		complete(&rsp->barrier_completion);

	/* Increment ->n_barrier_done to prevent duplicate work. */
	smp_mb(); /* Keep increment after above mechanism. */
	ACCESS_ONCE(rsp->n_barrier_done) = rsp->n_barrier_done + 1;
	WARN_ON_ONCE((rsp->n_barrier_done & 0x1) != 0);
	_rcu_barrier_trace(rsp, "Inc2", -1, rsp->n_barrier_done);
	smp_mb(); /* Keep increment before caller's subsequent code. */

	/* Wait for all rcu_barrier_callback() callbacks to be invoked. */
	wait_for_completion(&rsp->barrier_completion);

	/* Other rcu_barrier() invocations can now safely proceed. */
	mutex_unlock(&rsp->barrier_mutex);
}

/**
 * rcu_barrier_bh - Wait until all in-flight call_rcu_bh() callbacks complete.
 */
void rcu_barrier_bh(void)
{
	_rcu_barrier(&rcu_bh_state);
}
EXPORT_SYMBOL_GPL(rcu_barrier_bh);

/**
 * rcu_barrier_sched - Wait for in-flight call_rcu_sched() callbacks.
 */
void rcu_barrier_sched(void)
{
	_rcu_barrier(&rcu_sched_state);
}
EXPORT_SYMBOL_GPL(rcu_barrier_sched);

/*
 * Do boot-time initialization of a CPU's per-CPU RCU data.
 */
static void __init
rcu_boot_init_percpu_data(int cpu, struct rcu_state *rsp)
{
	unsigned long flags;
	struct rcu_data *rdp = per_cpu_ptr(rsp->rda, cpu);
	struct rcu_node *rnp = rcu_get_root(rsp);

	/* Set up local state, ensuring consistent view of global state. */
	raw_spin_lock_irqsave(&rnp->lock, flags);
	rdp->grpmask = 1UL << (cpu - rdp->mynode->grplo);
	init_callback_list(rdp);
	rdp->qlen_lazy = 0;
	ACCESS_ONCE(rdp->qlen) = 0;
	rdp->dynticks = &per_cpu(rcu_dynticks, cpu);
	WARN_ON_ONCE(rdp->dynticks->dynticks_nesting != DYNTICK_TASK_EXIT_IDLE);
	WARN_ON_ONCE(atomic_read(&rdp->dynticks->dynticks) != 1);
	rdp->cpu = cpu;
	rdp->rsp = rsp;
	rcu_boot_init_nocb_percpu_data(rdp);
	raw_spin_unlock_irqrestore(&rnp->lock, flags);
}

/*
 * Initialize a CPU's per-CPU RCU data.  Note that only one online or
 * offline event can be happening at a given time.  Note also that we
 * can accept some slop in the rsp->completed access due to the fact
 * that this CPU cannot possibly have any RCU callbacks in flight yet.
 */
static void
rcu_init_percpu_data(int cpu, struct rcu_state *rsp)
{
	unsigned long flags;
	unsigned long mask;
	struct rcu_data *rdp = per_cpu_ptr(rsp->rda, cpu);
	struct rcu_node *rnp = rcu_get_root(rsp);

	/* Exclude new grace periods. */
	mutex_lock(&rsp->onoff_mutex);

	/* Set up local state, ensuring consistent view of global state. */
	raw_spin_lock_irqsave(&rnp->lock, flags);
	rdp->beenonline = 1;	 /* We have now been online. */
	rdp->qlen_last_fqs_check = 0;
	rdp->n_force_qs_snap = rsp->n_force_qs;
	rdp->blimit = blimit;
	init_callback_list(rdp);  /* Re-enable callbacks on this CPU. */
	rdp->dynticks->dynticks_nesting = DYNTICK_TASK_EXIT_IDLE;
	rcu_sysidle_init_percpu_data(rdp->dynticks);
	atomic_set(&rdp->dynticks->dynticks,
		   (atomic_read(&rdp->dynticks->dynticks) & ~0x1) + 1);
	raw_spin_unlock(&rnp->lock);		/* irqs remain disabled. */

	/* Add CPU to rcu_node bitmasks. */
	rnp = rdp->mynode;
	mask = rdp->grpmask;
	do {
		/* Exclude any attempts to start a new GP on small systems. */
		raw_spin_lock(&rnp->lock);	/* irqs already disabled. */
		rnp->qsmaskinit |= mask;
		mask = rnp->grpmask;
		if (rnp == rdp->mynode) {
			/*
			 * If there is a grace period in progress, we will
			 * set up to wait for it next time we run the
			 * RCU core code.
			 */
			rdp->gpnum = rnp->completed;
			rdp->completed = rnp->completed;
			rdp->passed_quiesce = 0;
			rdp->qs_pending = 0;
			trace_rcu_grace_period(rsp->name, rdp->gpnum, TPS("cpuonl"));
		}
		raw_spin_unlock(&rnp->lock); /* irqs already disabled. */
		rnp = rnp->parent;
	} while (rnp != NULL && !(rnp->qsmaskinit & mask));
	local_irq_restore(flags);

	mutex_unlock(&rsp->onoff_mutex);
}

static void rcu_prepare_cpu(int cpu)
{
	struct rcu_state *rsp;

	for_each_rcu_flavor(rsp)
		rcu_init_percpu_data(cpu, rsp);
}

/*
 * Handle CPU online/offline notification events.
 */
static int rcu_cpu_notify(struct notifier_block *self,
				    unsigned long action, void *hcpu)
{
	long cpu = (long)hcpu;
	struct rcu_data *rdp = per_cpu_ptr(rcu_state_p->rda, cpu);
	struct rcu_node *rnp = rdp->mynode;
	struct rcu_state *rsp;

	trace_rcu_utilization(TPS("Start CPU hotplug"));
	switch (action) {
	case CPU_UP_PREPARE:
	case CPU_UP_PREPARE_FROZEN:
		rcu_prepare_cpu(cpu);
		rcu_prepare_kthreads(cpu);
		break;
	case CPU_ONLINE:
	case CPU_DOWN_FAILED:
		rcu_boost_kthread_setaffinity(rnp, -1);
		break;
	case CPU_DOWN_PREPARE:
		rcu_boost_kthread_setaffinity(rnp, cpu);
		break;
	case CPU_DYING:
	case CPU_DYING_FROZEN:
		for_each_rcu_flavor(rsp)
			rcu_cleanup_dying_cpu(rsp);
		break;
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
	case CPU_UP_CANCELED:
	case CPU_UP_CANCELED_FROZEN:
		for_each_rcu_flavor(rsp)
			rcu_cleanup_dead_cpu(cpu, rsp);
		break;
	default:
		break;
	}
	trace_rcu_utilization(TPS("End CPU hotplug"));
	return NOTIFY_OK;
}

static int rcu_pm_notify(struct notifier_block *self,
			 unsigned long action, void *hcpu)
{
	switch (action) {
	case PM_HIBERNATION_PREPARE:
	case PM_SUSPEND_PREPARE:
		if (nr_cpu_ids <= 256) /* Expediting bad for large systems. */
			rcu_expedited = 1;
		break;
	case PM_POST_HIBERNATION:
	case PM_POST_SUSPEND:
		rcu_expedited = 0;
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

/*
 * Spawn the kthread that handles this RCU flavor's grace periods.
 */
static int __init rcu_spawn_gp_kthread(void)
{
	unsigned long flags;
	struct rcu_node *rnp;
	struct rcu_state *rsp;
	struct task_struct *t;

	for_each_rcu_flavor(rsp) {
		t = kthread_run(rcu_gp_kthread, rsp, "%s", rsp->name);
		BUG_ON(IS_ERR(t));
		rnp = rcu_get_root(rsp);
		raw_spin_lock_irqsave(&rnp->lock, flags);
		rsp->gp_kthread = t;
		raw_spin_unlock_irqrestore(&rnp->lock, flags);
		rcu_spawn_nocb_kthreads(rsp);
	}
	return 0;
}
early_initcall(rcu_spawn_gp_kthread);

/*
 * This function is invoked towards the end of the scheduler's initialization
 * process.  Before this is called, the idle task might contain
 * RCU read-side critical sections (during which time, this idle
 * task is booting the system).  After this function is called, the
 * idle tasks are prohibited from containing RCU read-side critical
 * sections.  This function also enables RCU lockdep checking.
 */
void rcu_scheduler_starting(void)
{
	WARN_ON(num_online_cpus() != 1);
	WARN_ON(nr_context_switches() > 0);
	rcu_scheduler_active = 1;
}

/*
 * Compute the per-level fanout, either using the exact fanout specified
 * or balancing the tree, depending on CONFIG_RCU_FANOUT_EXACT.
 */
#ifdef CONFIG_RCU_FANOUT_EXACT
static void __init rcu_init_levelspread(struct rcu_state *rsp)
{
	int i;

	rsp->levelspread[rcu_num_lvls - 1] = rcu_fanout_leaf;
	for (i = rcu_num_lvls - 2; i >= 0; i--)
		rsp->levelspread[i] = CONFIG_RCU_FANOUT;
}
#else /* #ifdef CONFIG_RCU_FANOUT_EXACT */
static void __init rcu_init_levelspread(struct rcu_state *rsp)
{
	int ccur;
	int cprv;
	int i;

	cprv = nr_cpu_ids;
	for (i = rcu_num_lvls - 1; i >= 0; i--) {
		ccur = rsp->levelcnt[i];
		rsp->levelspread[i] = (cprv + ccur - 1) / ccur;
		cprv = ccur;
	}
}
#endif /* #else #ifdef CONFIG_RCU_FANOUT_EXACT */

/*
 * Helper function for rcu_init() that initializes one rcu_state structure.
 */
static void __init rcu_init_one(struct rcu_state *rsp,
		struct rcu_data __percpu *rda)
{
	static const char * const buf[] = {
		"rcu_node_0",
		"rcu_node_1",
		"rcu_node_2",
		"rcu_node_3" };  /* Match MAX_RCU_LVLS */
	static const char * const fqs[] = {
		"rcu_node_fqs_0",
		"rcu_node_fqs_1",
		"rcu_node_fqs_2",
		"rcu_node_fqs_3" };  /* Match MAX_RCU_LVLS */
	static u8 fl_mask = 0x1;
	int cpustride = 1;
	int i;
	int j;
	struct rcu_node *rnp;

	BUILD_BUG_ON(MAX_RCU_LVLS > ARRAY_SIZE(buf));  /* Fix buf[] init! */

	/* Silence gcc 4.8 warning about array index out of range. */
	if (rcu_num_lvls > RCU_NUM_LVLS)
		panic("rcu_init_one: rcu_num_lvls overflow");

	/* Initialize the level-tracking arrays. */

	for (i = 0; i < rcu_num_lvls; i++)
		rsp->levelcnt[i] = num_rcu_lvl[i];
	for (i = 1; i < rcu_num_lvls; i++)
		rsp->level[i] = rsp->level[i - 1] + rsp->levelcnt[i - 1];
	rcu_init_levelspread(rsp);
	rsp->flavor_mask = fl_mask;
	fl_mask <<= 1;

	/* Initialize the elements themselves, starting from the leaves. */

	for (i = rcu_num_lvls - 1; i >= 0; i--) {
		cpustride *= rsp->levelspread[i];
		rnp = rsp->level[i];
		for (j = 0; j < rsp->levelcnt[i]; j++, rnp++) {
			raw_spin_lock_init(&rnp->lock);
			lockdep_set_class_and_name(&rnp->lock,
						   &rcu_node_class[i], buf[i]);
			raw_spin_lock_init(&rnp->fqslock);
			lockdep_set_class_and_name(&rnp->fqslock,
						   &rcu_fqs_class[i], fqs[i]);
			rnp->gpnum = rsp->gpnum;
			rnp->completed = rsp->completed;
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
				rnp->grpnum = j % rsp->levelspread[i - 1];
				rnp->grpmask = 1UL << rnp->grpnum;
				rnp->parent = rsp->level[i - 1] +
					      j / rsp->levelspread[i - 1];
			}
			rnp->level = i;
			INIT_LIST_HEAD(&rnp->blkd_tasks);
			rcu_init_one_nocb(rnp);
		}
	}

	rsp->rda = rda;
	init_waitqueue_head(&rsp->gp_wq);
	rnp = rsp->level[rcu_num_lvls - 1];
	for_each_possible_cpu(i) {
		while (i > rnp->grphi)
			rnp++;
		per_cpu_ptr(rsp->rda, i)->mynode = rnp;
		rcu_boot_init_percpu_data(i, rsp);
	}
	list_add(&rsp->flavors, &rcu_struct_flavors);
}

/*
 * Compute the rcu_node tree geometry from kernel parameters.  This cannot
 * replace the definitions in tree.h because those are needed to size
 * the ->node array in the rcu_state structure.
 */
static void __init rcu_init_geometry(void)
{
	ulong d;
	int i;
	int j;
	int n = nr_cpu_ids;
	int rcu_capacity[MAX_RCU_LVLS + 1];

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

	/* If the compile-time values are accurate, just leave. */
	if (rcu_fanout_leaf == CONFIG_RCU_FANOUT_LEAF &&
	    nr_cpu_ids == NR_CPUS)
		return;
	pr_info("RCU: Adjusting geometry for rcu_fanout_leaf=%d, nr_cpu_ids=%d\n",
		rcu_fanout_leaf, nr_cpu_ids);

	/*
	 * Compute number of nodes that can be handled an rcu_node tree
	 * with the given number of levels.  Setting rcu_capacity[0] makes
	 * some of the arithmetic easier.
	 */
	rcu_capacity[0] = 1;
	rcu_capacity[1] = rcu_fanout_leaf;
	for (i = 2; i <= MAX_RCU_LVLS; i++)
		rcu_capacity[i] = rcu_capacity[i - 1] * CONFIG_RCU_FANOUT;

	/*
	 * The boot-time rcu_fanout_leaf parameter is only permitted
	 * to increase the leaf-level fanout, not decrease it.  Of course,
	 * the leaf-level fanout cannot exceed the number of bits in
	 * the rcu_node masks.  Finally, the tree must be able to accommodate
	 * the configured number of CPUs.  Complain and fall back to the
	 * compile-time values if these limits are exceeded.
	 */
	if (rcu_fanout_leaf < CONFIG_RCU_FANOUT_LEAF ||
	    rcu_fanout_leaf > sizeof(unsigned long) * 8 ||
	    n > rcu_capacity[MAX_RCU_LVLS]) {
		WARN_ON(1);
		return;
	}

	/* Calculate the number of rcu_nodes at each level of the tree. */
	for (i = 1; i <= MAX_RCU_LVLS; i++)
		if (n <= rcu_capacity[i]) {
			for (j = 0; j <= i; j++)
				num_rcu_lvl[j] =
					DIV_ROUND_UP(n, rcu_capacity[i - j]);
			rcu_num_lvls = i;
			for (j = i + 1; j <= MAX_RCU_LVLS; j++)
				num_rcu_lvl[j] = 0;
			break;
		}

	/* Calculate the total number of rcu_node structures. */
	rcu_num_nodes = 0;
	for (i = 0; i <= MAX_RCU_LVLS; i++)
		rcu_num_nodes += num_rcu_lvl[i];
	rcu_num_nodes -= n;
}

void __init rcu_init(void)
{
	int cpu;

	rcu_bootup_announce();
	rcu_init_geometry();
	rcu_init_one(&rcu_bh_state, &rcu_bh_data);
	rcu_init_one(&rcu_sched_state, &rcu_sched_data);
	__rcu_init_preempt();
	open_softirq(RCU_SOFTIRQ, rcu_process_callbacks);

	/*
	 * We don't need protection against CPU-hotplug here because
	 * this is called early in boot, before either interrupts
	 * or the scheduler are operational.
	 */
	cpu_notifier(rcu_cpu_notify, 0);
	pm_notifier(rcu_pm_notify, 0);
	for_each_online_cpu(cpu)
		rcu_cpu_notify(NULL, CPU_UP_PREPARE, (void *)(long)cpu);
}

#include "tree_plugin.h"
