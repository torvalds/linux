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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
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
#include <linux/module.h>
#include <linux/completion.h>
#include <linux/moduleparam.h>
#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/mutex.h>
#include <linux/time.h>
#include <linux/kernel_stat.h>
#include <linux/wait.h>
#include <linux/kthread.h>
#include <linux/prefetch.h>

#include "rcutree.h"
#include <trace/events/rcu.h>

#include "rcu.h"

/* Data structures. */

static struct lock_class_key rcu_node_class[NUM_RCU_LVLS];

#define RCU_STATE_INITIALIZER(structname) { \
	.level = { &structname##_state.node[0] }, \
	.levelcnt = { \
		NUM_RCU_LVL_0,  /* root of hierarchy. */ \
		NUM_RCU_LVL_1, \
		NUM_RCU_LVL_2, \
		NUM_RCU_LVL_3, \
		NUM_RCU_LVL_4, /* == MAX_RCU_LVLS */ \
	}, \
	.signaled = RCU_GP_IDLE, \
	.gpnum = -300, \
	.completed = -300, \
	.onofflock = __RAW_SPIN_LOCK_UNLOCKED(&structname##_state.onofflock), \
	.fqslock = __RAW_SPIN_LOCK_UNLOCKED(&structname##_state.fqslock), \
	.n_force_qs = 0, \
	.n_force_qs_ngp = 0, \
	.name = #structname, \
}

struct rcu_state rcu_sched_state = RCU_STATE_INITIALIZER(rcu_sched);
DEFINE_PER_CPU(struct rcu_data, rcu_sched_data);

struct rcu_state rcu_bh_state = RCU_STATE_INITIALIZER(rcu_bh);
DEFINE_PER_CPU(struct rcu_data, rcu_bh_data);

static struct rcu_state *rcu_state;

/*
 * The rcu_scheduler_active variable transitions from zero to one just
 * before the first task is spawned.  So when this variable is zero, RCU
 * can assume that there is but one task, allowing RCU to (for example)
 * optimized synchronize_sched() to a simple barrier().  When this variable
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
DEFINE_PER_CPU(int, rcu_cpu_kthread_cpu);
DEFINE_PER_CPU(unsigned int, rcu_cpu_kthread_loops);
DEFINE_PER_CPU(char, rcu_cpu_has_work);

#endif /* #ifdef CONFIG_RCU_BOOST */

static void rcu_node_kthread_setaffinity(struct rcu_node *rnp, int outgoingcpu);
static void invoke_rcu_core(void);
static void invoke_rcu_callbacks(struct rcu_state *rsp, struct rcu_data *rdp);

#define RCU_KTHREAD_PRIO 1	/* RT priority for per-CPU kthreads. */

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

	rdp->passed_quiesce_gpnum = rdp->gpnum;
	barrier();
	if (rdp->passed_quiesce == 0)
		trace_rcu_grace_period("rcu_sched", rdp->gpnum, "cpuqs");
	rdp->passed_quiesce = 1;
}

void rcu_bh_qs(int cpu)
{
	struct rcu_data *rdp = &per_cpu(rcu_bh_data, cpu);

	rdp->passed_quiesce_gpnum = rdp->gpnum;
	barrier();
	if (rdp->passed_quiesce == 0)
		trace_rcu_grace_period("rcu_bh", rdp->gpnum, "cpuqs");
	rdp->passed_quiesce = 1;
}

/*
 * Note a context switch.  This is a quiescent state for RCU-sched,
 * and requires special handling for preemptible RCU.
 * The caller must have disabled preemption.
 */
void rcu_note_context_switch(int cpu)
{
	trace_rcu_utilization("Start context switch");
	rcu_sched_qs(cpu);
	rcu_preempt_note_context_switch(cpu);
	trace_rcu_utilization("End context switch");
}
EXPORT_SYMBOL_GPL(rcu_note_context_switch);

#ifdef CONFIG_NO_HZ
DEFINE_PER_CPU(struct rcu_dynticks, rcu_dynticks) = {
	.dynticks_nesting = 1,
	.dynticks = ATOMIC_INIT(1),
};
#endif /* #ifdef CONFIG_NO_HZ */

static int blimit = 10;		/* Maximum callbacks per rcu_do_batch. */
static int qhimark = 10000;	/* If this many pending, ignore blimit. */
static int qlowmark = 100;	/* Once only this many pending, use blimit. */

module_param(blimit, int, 0);
module_param(qhimark, int, 0);
module_param(qlowmark, int, 0);

int rcu_cpu_stall_suppress __read_mostly;
module_param(rcu_cpu_stall_suppress, int, 0644);

static void force_quiescent_state(struct rcu_state *rsp, int relaxed);
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
 * Force a quiescent state for RCU BH.
 */
void rcu_bh_force_quiescent_state(void)
{
	force_quiescent_state(&rcu_bh_state, 0);
}
EXPORT_SYMBOL_GPL(rcu_bh_force_quiescent_state);

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
	force_quiescent_state(&rcu_sched_state, 0);
}
EXPORT_SYMBOL_GPL(rcu_sched_force_quiescent_state);

/*
 * Does the CPU have callbacks ready to be invoked?
 */
static int
cpu_has_callbacks_ready_to_invoke(struct rcu_data *rdp)
{
	return &rdp->nxtlist != rdp->nxttail[RCU_DONE_TAIL];
}

/*
 * Does the current CPU require a yet-as-unscheduled grace period?
 */
static int
cpu_needs_another_gp(struct rcu_state *rsp, struct rcu_data *rdp)
{
	return *rdp->nxttail[RCU_DONE_TAIL] && !rcu_gp_in_progress(rsp);
}

/*
 * Return the root node of the specified rcu_state structure.
 */
static struct rcu_node *rcu_get_root(struct rcu_state *rsp)
{
	return &rsp->node[0];
}

#ifdef CONFIG_SMP

/*
 * If the specified CPU is offline, tell the caller that it is in
 * a quiescent state.  Otherwise, whack it with a reschedule IPI.
 * Grace periods can end up waiting on an offline CPU when that
 * CPU is in the process of coming online -- it will be added to the
 * rcu_node bitmasks before it actually makes it online.  The same thing
 * can happen while a CPU is in the process of coming online.  Because this
 * race is quite rare, we check for it after detecting that the grace
 * period has been delayed rather than checking each and every CPU
 * each and every time we start a new grace period.
 */
static int rcu_implicit_offline_qs(struct rcu_data *rdp)
{
	/*
	 * If the CPU is offline, it is in a quiescent state.  We can
	 * trust its state not to change because interrupts are disabled.
	 */
	if (cpu_is_offline(rdp->cpu)) {
		trace_rcu_fqs(rdp->rsp->name, rdp->gpnum, rdp->cpu, "ofl");
		rdp->offline_fqs++;
		return 1;
	}

	/* If preemptible RCU, no point in sending reschedule IPI. */
	if (rdp->preemptible)
		return 0;

	/* The CPU is online, so send it a reschedule IPI. */
	if (rdp->cpu != smp_processor_id())
		smp_send_reschedule(rdp->cpu);
	else
		set_need_resched();
	rdp->resched_ipi++;
	return 0;
}

#endif /* #ifdef CONFIG_SMP */

#ifdef CONFIG_NO_HZ

/**
 * rcu_enter_nohz - inform RCU that current CPU is entering nohz
 *
 * Enter nohz mode, in other words, -leave- the mode in which RCU
 * read-side critical sections can occur.  (Though RCU read-side
 * critical sections can occur in irq handlers in nohz mode, a possibility
 * handled by rcu_irq_enter() and rcu_irq_exit()).
 */
void rcu_enter_nohz(void)
{
	unsigned long flags;
	struct rcu_dynticks *rdtp;

	local_irq_save(flags);
	rdtp = &__get_cpu_var(rcu_dynticks);
	if (--rdtp->dynticks_nesting) {
		local_irq_restore(flags);
		return;
	}
	trace_rcu_dyntick("Start");
	/* CPUs seeing atomic_inc() must see prior RCU read-side crit sects */
	smp_mb__before_atomic_inc();  /* See above. */
	atomic_inc(&rdtp->dynticks);
	smp_mb__after_atomic_inc();  /* Force ordering with next sojourn. */
	WARN_ON_ONCE(atomic_read(&rdtp->dynticks) & 0x1);
	local_irq_restore(flags);
}

/*
 * rcu_exit_nohz - inform RCU that current CPU is leaving nohz
 *
 * Exit nohz mode, in other words, -enter- the mode in which RCU
 * read-side critical sections normally occur.
 */
void rcu_exit_nohz(void)
{
	unsigned long flags;
	struct rcu_dynticks *rdtp;

	local_irq_save(flags);
	rdtp = &__get_cpu_var(rcu_dynticks);
	if (rdtp->dynticks_nesting++) {
		local_irq_restore(flags);
		return;
	}
	smp_mb__before_atomic_inc();  /* Force ordering w/previous sojourn. */
	atomic_inc(&rdtp->dynticks);
	/* CPUs seeing atomic_inc() must see later RCU read-side crit sects */
	smp_mb__after_atomic_inc();  /* See above. */
	WARN_ON_ONCE(!(atomic_read(&rdtp->dynticks) & 0x1));
	trace_rcu_dyntick("End");
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
	struct rcu_dynticks *rdtp = &__get_cpu_var(rcu_dynticks);

	if (rdtp->dynticks_nmi_nesting == 0 &&
	    (atomic_read(&rdtp->dynticks) & 0x1))
		return;
	rdtp->dynticks_nmi_nesting++;
	smp_mb__before_atomic_inc();  /* Force delay from prior write. */
	atomic_inc(&rdtp->dynticks);
	/* CPUs seeing atomic_inc() must see later RCU read-side crit sects */
	smp_mb__after_atomic_inc();  /* See above. */
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
	struct rcu_dynticks *rdtp = &__get_cpu_var(rcu_dynticks);

	if (rdtp->dynticks_nmi_nesting == 0 ||
	    --rdtp->dynticks_nmi_nesting != 0)
		return;
	/* CPUs seeing atomic_inc() must see prior RCU read-side crit sects */
	smp_mb__before_atomic_inc();  /* See above. */
	atomic_inc(&rdtp->dynticks);
	smp_mb__after_atomic_inc();  /* Force delay to next write. */
	WARN_ON_ONCE(atomic_read(&rdtp->dynticks) & 0x1);
}

/**
 * rcu_irq_enter - inform RCU of entry to hard irq context
 *
 * If the CPU was idle with dynamic ticks active, this updates the
 * rdtp->dynticks to let the RCU handling know that the CPU is active.
 */
void rcu_irq_enter(void)
{
	rcu_exit_nohz();
}

/**
 * rcu_irq_exit - inform RCU of exit from hard irq context
 *
 * If the CPU was idle with dynamic ticks active, update the rdp->dynticks
 * to put let the RCU handling be aware that the CPU is going back to idle
 * with no ticks.
 */
void rcu_irq_exit(void)
{
	rcu_enter_nohz();
}

#ifdef CONFIG_SMP

/*
 * Snapshot the specified CPU's dynticks counter so that we can later
 * credit them with an implicit quiescent state.  Return 1 if this CPU
 * is in dynticks idle mode, which is an extended quiescent state.
 */
static int dyntick_save_progress_counter(struct rcu_data *rdp)
{
	rdp->dynticks_snap = atomic_add_return(0, &rdp->dynticks->dynticks);
	return 0;
}

/*
 * Return true if the specified CPU has passed through a quiescent
 * state by virtue of being in or having passed through an dynticks
 * idle state since the last call to dyntick_save_progress_counter()
 * for this same CPU.
 */
static int rcu_implicit_dynticks_qs(struct rcu_data *rdp)
{
	unsigned long curr;
	unsigned long snap;

	curr = (unsigned long)atomic_add_return(0, &rdp->dynticks->dynticks);
	snap = (unsigned long)rdp->dynticks_snap;

	/*
	 * If the CPU passed through or entered a dynticks idle phase with
	 * no active irq/NMI handlers, then we can safely pretend that the CPU
	 * already acknowledged the request to pass through a quiescent
	 * state.  Either way, that CPU cannot possibly be in an RCU
	 * read-side critical section that started before the beginning
	 * of the current RCU grace period.
	 */
	if ((curr & 0x1) == 0 || ULONG_CMP_GE(curr, snap + 2)) {
		trace_rcu_fqs(rdp->rsp->name, rdp->gpnum, rdp->cpu, "dti");
		rdp->dynticks_fqs++;
		return 1;
	}

	/* Go check for the CPU being offline. */
	return rcu_implicit_offline_qs(rdp);
}

#endif /* #ifdef CONFIG_SMP */

#else /* #ifdef CONFIG_NO_HZ */

#ifdef CONFIG_SMP

static int dyntick_save_progress_counter(struct rcu_data *rdp)
{
	return 0;
}

static int rcu_implicit_dynticks_qs(struct rcu_data *rdp)
{
	return rcu_implicit_offline_qs(rdp);
}

#endif /* #ifdef CONFIG_SMP */

#endif /* #else #ifdef CONFIG_NO_HZ */

int rcu_cpu_stall_suppress __read_mostly;

static void record_gp_stall_check_time(struct rcu_state *rsp)
{
	rsp->gp_start = jiffies;
	rsp->jiffies_stall = jiffies + RCU_SECONDS_TILL_STALL_CHECK;
}

static void print_other_cpu_stall(struct rcu_state *rsp)
{
	int cpu;
	long delta;
	unsigned long flags;
	struct rcu_node *rnp = rcu_get_root(rsp);

	/* Only let one CPU complain about others per time interval. */

	raw_spin_lock_irqsave(&rnp->lock, flags);
	delta = jiffies - rsp->jiffies_stall;
	if (delta < RCU_STALL_RAT_DELAY || !rcu_gp_in_progress(rsp)) {
		raw_spin_unlock_irqrestore(&rnp->lock, flags);
		return;
	}
	rsp->jiffies_stall = jiffies + RCU_SECONDS_TILL_STALL_RECHECK;

	/*
	 * Now rat on any tasks that got kicked up to the root rcu_node
	 * due to CPU offlining.
	 */
	rcu_print_task_stall(rnp);
	raw_spin_unlock_irqrestore(&rnp->lock, flags);

	/*
	 * OK, time to rat on our buddy...
	 * See Documentation/RCU/stallwarn.txt for info on how to debug
	 * RCU CPU stall warnings.
	 */
	printk(KERN_ERR "INFO: %s detected stalls on CPUs/tasks: {",
	       rsp->name);
	rcu_for_each_leaf_node(rsp, rnp) {
		raw_spin_lock_irqsave(&rnp->lock, flags);
		rcu_print_task_stall(rnp);
		raw_spin_unlock_irqrestore(&rnp->lock, flags);
		if (rnp->qsmask == 0)
			continue;
		for (cpu = 0; cpu <= rnp->grphi - rnp->grplo; cpu++)
			if (rnp->qsmask & (1UL << cpu))
				printk(" %d", rnp->grplo + cpu);
	}
	printk("} (detected by %d, t=%ld jiffies)\n",
	       smp_processor_id(), (long)(jiffies - rsp->gp_start));
	trigger_all_cpu_backtrace();

	/* If so configured, complain about tasks blocking the grace period. */

	rcu_print_detail_task_stall(rsp);

	force_quiescent_state(rsp, 0);  /* Kick them all. */
}

static void print_cpu_stall(struct rcu_state *rsp)
{
	unsigned long flags;
	struct rcu_node *rnp = rcu_get_root(rsp);

	/*
	 * OK, time to rat on ourselves...
	 * See Documentation/RCU/stallwarn.txt for info on how to debug
	 * RCU CPU stall warnings.
	 */
	printk(KERN_ERR "INFO: %s detected stall on CPU %d (t=%lu jiffies)\n",
	       rsp->name, smp_processor_id(), jiffies - rsp->gp_start);
	trigger_all_cpu_backtrace();

	raw_spin_lock_irqsave(&rnp->lock, flags);
	if (ULONG_CMP_GE(jiffies, rsp->jiffies_stall))
		rsp->jiffies_stall =
			jiffies + RCU_SECONDS_TILL_STALL_RECHECK;
	raw_spin_unlock_irqrestore(&rnp->lock, flags);

	set_need_resched();  /* kick ourselves to get things going. */
}

static void check_cpu_stall(struct rcu_state *rsp, struct rcu_data *rdp)
{
	unsigned long j;
	unsigned long js;
	struct rcu_node *rnp;

	if (rcu_cpu_stall_suppress)
		return;
	j = ACCESS_ONCE(jiffies);
	js = ACCESS_ONCE(rsp->jiffies_stall);
	rnp = rdp->mynode;
	if ((ACCESS_ONCE(rnp->qsmask) & rdp->grpmask) && ULONG_CMP_GE(j, js)) {

		/* We haven't checked in, so go dump stack. */
		print_cpu_stall(rsp);

	} else if (rcu_gp_in_progress(rsp) &&
		   ULONG_CMP_GE(j, js + RCU_STALL_RAT_DELAY)) {

		/* They had a few time units to dump stack, so complain. */
		print_other_cpu_stall(rsp);
	}
}

static int rcu_panic(struct notifier_block *this, unsigned long ev, void *ptr)
{
	rcu_cpu_stall_suppress = 1;
	return NOTIFY_DONE;
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
	rcu_sched_state.jiffies_stall = jiffies + ULONG_MAX / 2;
	rcu_bh_state.jiffies_stall = jiffies + ULONG_MAX / 2;
	rcu_preempt_stall_reset();
}

static struct notifier_block rcu_panic_block = {
	.notifier_call = rcu_panic,
};

static void __init check_cpu_stall_init(void)
{
	atomic_notifier_chain_register(&panic_notifier_list, &rcu_panic_block);
}

/*
 * Update CPU-local rcu_data state to record the newly noticed grace period.
 * This is used both when we started the grace period and when we notice
 * that someone else started the grace period.  The caller must hold the
 * ->lock of the leaf rcu_node structure corresponding to the current CPU,
 *  and must have irqs disabled.
 */
static void __note_new_gpnum(struct rcu_state *rsp, struct rcu_node *rnp, struct rcu_data *rdp)
{
	if (rdp->gpnum != rnp->gpnum) {
		/*
		 * If the current grace period is waiting for this CPU,
		 * set up to detect a quiescent state, otherwise don't
		 * go looking for one.
		 */
		rdp->gpnum = rnp->gpnum;
		trace_rcu_grace_period(rsp->name, rdp->gpnum, "cpustart");
		if (rnp->qsmask & rdp->grpmask) {
			rdp->qs_pending = 1;
			rdp->passed_quiesce = 0;
		} else
			rdp->qs_pending = 0;
	}
}

static void note_new_gpnum(struct rcu_state *rsp, struct rcu_data *rdp)
{
	unsigned long flags;
	struct rcu_node *rnp;

	local_irq_save(flags);
	rnp = rdp->mynode;
	if (rdp->gpnum == ACCESS_ONCE(rnp->gpnum) || /* outside lock. */
	    !raw_spin_trylock(&rnp->lock)) { /* irqs already off, so later. */
		local_irq_restore(flags);
		return;
	}
	__note_new_gpnum(rsp, rnp, rdp);
	raw_spin_unlock_irqrestore(&rnp->lock, flags);
}

/*
 * Did someone else start a new RCU grace period start since we last
 * checked?  Update local state appropriately if so.  Must be called
 * on the CPU corresponding to rdp.
 */
static int
check_for_new_grace_period(struct rcu_state *rsp, struct rcu_data *rdp)
{
	unsigned long flags;
	int ret = 0;

	local_irq_save(flags);
	if (rdp->gpnum != rsp->gpnum) {
		note_new_gpnum(rsp, rdp);
		ret = 1;
	}
	local_irq_restore(flags);
	return ret;
}

/*
 * Advance this CPU's callbacks, but only if the current grace period
 * has ended.  This may be called only from the CPU to whom the rdp
 * belongs.  In addition, the corresponding leaf rcu_node structure's
 * ->lock must be held by the caller, with irqs disabled.
 */
static void
__rcu_process_gp_end(struct rcu_state *rsp, struct rcu_node *rnp, struct rcu_data *rdp)
{
	/* Did another grace period end? */
	if (rdp->completed != rnp->completed) {

		/* Advance callbacks.  No harm if list empty. */
		rdp->nxttail[RCU_DONE_TAIL] = rdp->nxttail[RCU_WAIT_TAIL];
		rdp->nxttail[RCU_WAIT_TAIL] = rdp->nxttail[RCU_NEXT_READY_TAIL];
		rdp->nxttail[RCU_NEXT_READY_TAIL] = rdp->nxttail[RCU_NEXT_TAIL];

		/* Remember that we saw this grace-period completion. */
		rdp->completed = rnp->completed;
		trace_rcu_grace_period(rsp->name, rdp->gpnum, "cpuend");

		/*
		 * If we were in an extended quiescent state, we may have
		 * missed some grace periods that others CPUs handled on
		 * our behalf. Catch up with this state to avoid noting
		 * spurious new grace periods.  If another grace period
		 * has started, then rnp->gpnum will have advanced, so
		 * we will detect this later on.
		 */
		if (ULONG_CMP_LT(rdp->gpnum, rdp->completed))
			rdp->gpnum = rdp->completed;

		/*
		 * If RCU does not need a quiescent state from this CPU,
		 * then make sure that this CPU doesn't go looking for one.
		 */
		if ((rnp->qsmask & rdp->grpmask) == 0)
			rdp->qs_pending = 0;
	}
}

/*
 * Advance this CPU's callbacks, but only if the current grace period
 * has ended.  This may be called only from the CPU to whom the rdp
 * belongs.
 */
static void
rcu_process_gp_end(struct rcu_state *rsp, struct rcu_data *rdp)
{
	unsigned long flags;
	struct rcu_node *rnp;

	local_irq_save(flags);
	rnp = rdp->mynode;
	if (rdp->completed == ACCESS_ONCE(rnp->completed) || /* outside lock. */
	    !raw_spin_trylock(&rnp->lock)) { /* irqs already off, so later. */
		local_irq_restore(flags);
		return;
	}
	__rcu_process_gp_end(rsp, rnp, rdp);
	raw_spin_unlock_irqrestore(&rnp->lock, flags);
}

/*
 * Do per-CPU grace-period initialization for running CPU.  The caller
 * must hold the lock of the leaf rcu_node structure corresponding to
 * this CPU.
 */
static void
rcu_start_gp_per_cpu(struct rcu_state *rsp, struct rcu_node *rnp, struct rcu_data *rdp)
{
	/* Prior grace period ended, so advance callbacks for current CPU. */
	__rcu_process_gp_end(rsp, rnp, rdp);

	/*
	 * Because this CPU just now started the new grace period, we know
	 * that all of its callbacks will be covered by this upcoming grace
	 * period, even the ones that were registered arbitrarily recently.
	 * Therefore, advance all outstanding callbacks to RCU_WAIT_TAIL.
	 *
	 * Other CPUs cannot be sure exactly when the grace period started.
	 * Therefore, their recently registered callbacks must pass through
	 * an additional RCU_NEXT_READY stage, so that they will be handled
	 * by the next RCU grace period.
	 */
	rdp->nxttail[RCU_NEXT_READY_TAIL] = rdp->nxttail[RCU_NEXT_TAIL];
	rdp->nxttail[RCU_WAIT_TAIL] = rdp->nxttail[RCU_NEXT_TAIL];

	/* Set state so that this CPU will detect the next quiescent state. */
	__note_new_gpnum(rsp, rnp, rdp);
}

/*
 * Start a new RCU grace period if warranted, re-initializing the hierarchy
 * in preparation for detecting the next grace period.  The caller must hold
 * the root node's ->lock, which is released before return.  Hard irqs must
 * be disabled.
 */
static void
rcu_start_gp(struct rcu_state *rsp, unsigned long flags)
	__releases(rcu_get_root(rsp)->lock)
{
	struct rcu_data *rdp = this_cpu_ptr(rsp->rda);
	struct rcu_node *rnp = rcu_get_root(rsp);

	if (!cpu_needs_another_gp(rsp, rdp) || rsp->fqs_active) {
		if (cpu_needs_another_gp(rsp, rdp))
			rsp->fqs_need_gp = 1;
		if (rnp->completed == rsp->completed) {
			raw_spin_unlock_irqrestore(&rnp->lock, flags);
			return;
		}
		raw_spin_unlock(&rnp->lock);	 /* irqs remain disabled. */

		/*
		 * Propagate new ->completed value to rcu_node structures
		 * so that other CPUs don't have to wait until the start
		 * of the next grace period to process their callbacks.
		 */
		rcu_for_each_node_breadth_first(rsp, rnp) {
			raw_spin_lock(&rnp->lock); /* irqs already disabled. */
			rnp->completed = rsp->completed;
			raw_spin_unlock(&rnp->lock); /* irqs remain disabled. */
		}
		local_irq_restore(flags);
		return;
	}

	/* Advance to a new grace period and initialize state. */
	rsp->gpnum++;
	trace_rcu_grace_period(rsp->name, rsp->gpnum, "start");
	WARN_ON_ONCE(rsp->signaled == RCU_GP_INIT);
	rsp->signaled = RCU_GP_INIT; /* Hold off force_quiescent_state. */
	rsp->jiffies_force_qs = jiffies + RCU_JIFFIES_TILL_FORCE_QS;
	record_gp_stall_check_time(rsp);

	/* Special-case the common single-level case. */
	if (NUM_RCU_NODES == 1) {
		rcu_preempt_check_blocked_tasks(rnp);
		rnp->qsmask = rnp->qsmaskinit;
		rnp->gpnum = rsp->gpnum;
		rnp->completed = rsp->completed;
		rsp->signaled = RCU_SIGNAL_INIT; /* force_quiescent_state OK. */
		rcu_start_gp_per_cpu(rsp, rnp, rdp);
		rcu_preempt_boost_start_gp(rnp);
		trace_rcu_grace_period_init(rsp->name, rnp->gpnum,
					    rnp->level, rnp->grplo,
					    rnp->grphi, rnp->qsmask);
		raw_spin_unlock_irqrestore(&rnp->lock, flags);
		return;
	}

	raw_spin_unlock(&rnp->lock);  /* leave irqs disabled. */


	/* Exclude any concurrent CPU-hotplug operations. */
	raw_spin_lock(&rsp->onofflock);  /* irqs already disabled. */

	/*
	 * Set the quiescent-state-needed bits in all the rcu_node
	 * structures for all currently online CPUs in breadth-first
	 * order, starting from the root rcu_node structure.  This
	 * operation relies on the layout of the hierarchy within the
	 * rsp->node[] array.  Note that other CPUs will access only
	 * the leaves of the hierarchy, which still indicate that no
	 * grace period is in progress, at least until the corresponding
	 * leaf node has been initialized.  In addition, we have excluded
	 * CPU-hotplug operations.
	 *
	 * Note that the grace period cannot complete until we finish
	 * the initialization process, as there will be at least one
	 * qsmask bit set in the root node until that time, namely the
	 * one corresponding to this CPU, due to the fact that we have
	 * irqs disabled.
	 */
	rcu_for_each_node_breadth_first(rsp, rnp) {
		raw_spin_lock(&rnp->lock);	/* irqs already disabled. */
		rcu_preempt_check_blocked_tasks(rnp);
		rnp->qsmask = rnp->qsmaskinit;
		rnp->gpnum = rsp->gpnum;
		rnp->completed = rsp->completed;
		if (rnp == rdp->mynode)
			rcu_start_gp_per_cpu(rsp, rnp, rdp);
		rcu_preempt_boost_start_gp(rnp);
		trace_rcu_grace_period_init(rsp->name, rnp->gpnum,
					    rnp->level, rnp->grplo,
					    rnp->grphi, rnp->qsmask);
		raw_spin_unlock(&rnp->lock);	/* irqs remain disabled. */
	}

	rnp = rcu_get_root(rsp);
	raw_spin_lock(&rnp->lock);		/* irqs already disabled. */
	rsp->signaled = RCU_SIGNAL_INIT; /* force_quiescent_state now OK. */
	raw_spin_unlock(&rnp->lock);		/* irqs remain disabled. */
	raw_spin_unlock_irqrestore(&rsp->onofflock, flags);
}

/*
 * Report a full set of quiescent states to the specified rcu_state
 * data structure.  This involves cleaning up after the prior grace
 * period and letting rcu_start_gp() start up the next grace period
 * if one is needed.  Note that the caller must hold rnp->lock, as
 * required by rcu_start_gp(), which will release it.
 */
static void rcu_report_qs_rsp(struct rcu_state *rsp, unsigned long flags)
	__releases(rcu_get_root(rsp)->lock)
{
	unsigned long gp_duration;

	WARN_ON_ONCE(!rcu_gp_in_progress(rsp));

	/*
	 * Ensure that all grace-period and pre-grace-period activity
	 * is seen before the assignment to rsp->completed.
	 */
	smp_mb(); /* See above block comment. */
	gp_duration = jiffies - rsp->gp_start;
	if (gp_duration > rsp->gp_max)
		rsp->gp_max = gp_duration;
	rsp->completed = rsp->gpnum;
	trace_rcu_grace_period(rsp->name, rsp->completed, "end");
	rsp->signaled = RCU_GP_IDLE;
	rcu_start_gp(rsp, flags);  /* releases root node's rnp->lock. */
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
rcu_report_qs_rdp(int cpu, struct rcu_state *rsp, struct rcu_data *rdp, long lastgp)
{
	unsigned long flags;
	unsigned long mask;
	struct rcu_node *rnp;

	rnp = rdp->mynode;
	raw_spin_lock_irqsave(&rnp->lock, flags);
	if (lastgp != rnp->gpnum || rnp->completed == rnp->gpnum) {

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
		rdp->nxttail[RCU_NEXT_READY_TAIL] = rdp->nxttail[RCU_NEXT_TAIL];

		rcu_report_qs_rnp(mask, rsp, rnp, flags); /* rlses rnp->lock */
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
	/* If there is now a new grace period, record and return. */
	if (check_for_new_grace_period(rsp, rdp))
		return;

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
	rcu_report_qs_rdp(rdp->cpu, rsp, rdp, rdp->passed_quiesce_gpnum);
}

#ifdef CONFIG_HOTPLUG_CPU

/*
 * Move a dying CPU's RCU callbacks to online CPU's callback list.
 * Synchronization is not required because this function executes
 * in stop_machine() context.
 */
static void rcu_send_cbs_to_online(struct rcu_state *rsp)
{
	int i;
	/* current DYING CPU is cleared in the cpu_online_mask */
	int receive_cpu = cpumask_any(cpu_online_mask);
	struct rcu_data *rdp = this_cpu_ptr(rsp->rda);
	struct rcu_data *receive_rdp = per_cpu_ptr(rsp->rda, receive_cpu);

	if (rdp->nxtlist == NULL)
		return;  /* irqs disabled, so comparison is stable. */

	*receive_rdp->nxttail[RCU_NEXT_TAIL] = rdp->nxtlist;
	receive_rdp->nxttail[RCU_NEXT_TAIL] = rdp->nxttail[RCU_NEXT_TAIL];
	receive_rdp->qlen += rdp->qlen;
	receive_rdp->n_cbs_adopted += rdp->qlen;
	rdp->n_cbs_orphaned += rdp->qlen;

	rdp->nxtlist = NULL;
	for (i = 0; i < RCU_NEXT_SIZE; i++)
		rdp->nxttail[i] = &rdp->nxtlist;
	rdp->qlen = 0;
}

/*
 * Remove the outgoing CPU from the bitmasks in the rcu_node hierarchy
 * and move all callbacks from the outgoing CPU to the current one.
 * There can only be one CPU hotplug operation at a time, so no other
 * CPU can be attempting to update rcu_cpu_kthread_task.
 */
static void __rcu_offline_cpu(int cpu, struct rcu_state *rsp)
{
	unsigned long flags;
	unsigned long mask;
	int need_report = 0;
	struct rcu_data *rdp = per_cpu_ptr(rsp->rda, cpu);
	struct rcu_node *rnp;

	rcu_stop_cpu_kthread(cpu);

	/* Exclude any attempts to start a new grace period. */
	raw_spin_lock_irqsave(&rsp->onofflock, flags);

	/* Remove the outgoing CPU from the masks in the rcu_node hierarchy. */
	rnp = rdp->mynode;	/* this is the outgoing CPU's rnp. */
	mask = rdp->grpmask;	/* rnp->grplo is constant. */
	do {
		raw_spin_lock(&rnp->lock);	/* irqs already disabled. */
		rnp->qsmaskinit &= ~mask;
		if (rnp->qsmaskinit != 0) {
			if (rnp != rdp->mynode)
				raw_spin_unlock(&rnp->lock); /* irqs remain disabled. */
			else
				trace_rcu_grace_period(rsp->name,
						       rnp->gpnum + 1 -
						       !!(rnp->qsmask & mask),
						       "cpuofl");
			break;
		}
		if (rnp == rdp->mynode) {
			trace_rcu_grace_period(rsp->name,
					       rnp->gpnum + 1 -
					       !!(rnp->qsmask & mask),
					       "cpuofl");
			need_report = rcu_preempt_offline_tasks(rsp, rnp, rdp);
		} else
			raw_spin_unlock(&rnp->lock); /* irqs remain disabled. */
		mask = rnp->grpmask;
		rnp = rnp->parent;
	} while (rnp != NULL);

	/*
	 * We still hold the leaf rcu_node structure lock here, and
	 * irqs are still disabled.  The reason for this subterfuge is
	 * because invoking rcu_report_unblock_qs_rnp() with ->onofflock
	 * held leads to deadlock.
	 */
	raw_spin_unlock(&rsp->onofflock); /* irqs remain disabled. */
	rnp = rdp->mynode;
	if (need_report & RCU_OFL_TASKS_NORM_GP)
		rcu_report_unblock_qs_rnp(rnp, flags);
	else
		raw_spin_unlock_irqrestore(&rnp->lock, flags);
	if (need_report & RCU_OFL_TASKS_EXP_GP)
		rcu_report_exp_rnp(rsp, rnp);
	rcu_node_kthread_setaffinity(rnp, -1);
}

/*
 * Remove the specified CPU from the RCU hierarchy and move any pending
 * callbacks that it might have to the current CPU.  This code assumes
 * that at least one CPU in the system will remain running at all times.
 * Any attempt to offline -all- CPUs is likely to strand RCU callbacks.
 */
static void rcu_offline_cpu(int cpu)
{
	__rcu_offline_cpu(cpu, &rcu_sched_state);
	__rcu_offline_cpu(cpu, &rcu_bh_state);
	rcu_preempt_offline_cpu(cpu);
}

#else /* #ifdef CONFIG_HOTPLUG_CPU */

static void rcu_send_cbs_to_online(struct rcu_state *rsp)
{
}

static void rcu_offline_cpu(int cpu)
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
	int bl, count;

	/* If no callbacks are ready, just return.*/
	if (!cpu_has_callbacks_ready_to_invoke(rdp)) {
		trace_rcu_batch_start(rsp->name, 0, 0);
		trace_rcu_batch_end(rsp->name, 0);
		return;
	}

	/*
	 * Extract the list of ready callbacks, disabling to prevent
	 * races with call_rcu() from interrupt handlers.
	 */
	local_irq_save(flags);
	bl = rdp->blimit;
	trace_rcu_batch_start(rsp->name, rdp->qlen, bl);
	list = rdp->nxtlist;
	rdp->nxtlist = *rdp->nxttail[RCU_DONE_TAIL];
	*rdp->nxttail[RCU_DONE_TAIL] = NULL;
	tail = rdp->nxttail[RCU_DONE_TAIL];
	for (count = RCU_NEXT_SIZE - 1; count >= 0; count--)
		if (rdp->nxttail[count] == rdp->nxttail[RCU_DONE_TAIL])
			rdp->nxttail[count] = &rdp->nxtlist;
	local_irq_restore(flags);

	/* Invoke callbacks. */
	count = 0;
	while (list) {
		next = list->next;
		prefetch(next);
		debug_rcu_head_unqueue(list);
		__rcu_reclaim(rsp->name, list);
		list = next;
		if (++count >= bl)
			break;
	}

	local_irq_save(flags);
	trace_rcu_batch_end(rsp->name, count);

	/* Update count, and requeue any remaining callbacks. */
	rdp->qlen -= count;
	rdp->n_cbs_invoked += count;
	if (list != NULL) {
		*tail = rdp->nxtlist;
		rdp->nxtlist = list;
		for (count = 0; count < RCU_NEXT_SIZE; count++)
			if (&rdp->nxtlist == rdp->nxttail[count])
				rdp->nxttail[count] = tail;
			else
				break;
	}

	/* Reinstate batch limit if we have worked down the excess. */
	if (rdp->blimit == LONG_MAX && rdp->qlen <= qlowmark)
		rdp->blimit = blimit;

	/* Reset ->qlen_last_fqs_check trigger if enough CBs have drained. */
	if (rdp->qlen == 0 && rdp->qlen_last_fqs_check != 0) {
		rdp->qlen_last_fqs_check = 0;
		rdp->n_force_qs_snap = rsp->n_force_qs;
	} else if (rdp->qlen < rdp->qlen_last_fqs_check - qhimark)
		rdp->qlen_last_fqs_check = rdp->qlen;

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
 * This function must be called with hardirqs disabled.  It is normally
 * invoked from the scheduling-clock interrupt.  If rcu_pending returns
 * false, there is no point in invoking rcu_check_callbacks().
 */
void rcu_check_callbacks(int cpu, int user)
{
	trace_rcu_utilization("Start scheduler-tick");
	if (user ||
	    (idle_cpu(cpu) && rcu_scheduler_active &&
	     !in_softirq() && hardirq_count() <= (1 << HARDIRQ_SHIFT))) {

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
	trace_rcu_utilization("End scheduler-tick");
}

#ifdef CONFIG_SMP

/*
 * Scan the leaf rcu_node structures, processing dyntick state for any that
 * have not yet encountered a quiescent state, using the function specified.
 * Also initiate boosting for any threads blocked on the root rcu_node.
 *
 * The caller must have suppressed start of new grace periods.
 */
static void force_qs_rnp(struct rcu_state *rsp, int (*f)(struct rcu_data *))
{
	unsigned long bit;
	int cpu;
	unsigned long flags;
	unsigned long mask;
	struct rcu_node *rnp;

	rcu_for_each_leaf_node(rsp, rnp) {
		mask = 0;
		raw_spin_lock_irqsave(&rnp->lock, flags);
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
			if ((rnp->qsmask & bit) != 0 &&
			    f(per_cpu_ptr(rsp->rda, cpu)))
				mask |= bit;
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
		rcu_initiate_boost(rnp, flags); /* releases rnp->lock. */
	}
}

/*
 * Force quiescent states on reluctant CPUs, and also detect which
 * CPUs are in dyntick-idle mode.
 */
static void force_quiescent_state(struct rcu_state *rsp, int relaxed)
{
	unsigned long flags;
	struct rcu_node *rnp = rcu_get_root(rsp);

	trace_rcu_utilization("Start fqs");
	if (!rcu_gp_in_progress(rsp)) {
		trace_rcu_utilization("End fqs");
		return;  /* No grace period in progress, nothing to force. */
	}
	if (!raw_spin_trylock_irqsave(&rsp->fqslock, flags)) {
		rsp->n_force_qs_lh++; /* Inexact, can lose counts.  Tough! */
		trace_rcu_utilization("End fqs");
		return;	/* Someone else is already on the job. */
	}
	if (relaxed && ULONG_CMP_GE(rsp->jiffies_force_qs, jiffies))
		goto unlock_fqs_ret; /* no emergency and done recently. */
	rsp->n_force_qs++;
	raw_spin_lock(&rnp->lock);  /* irqs already disabled */
	rsp->jiffies_force_qs = jiffies + RCU_JIFFIES_TILL_FORCE_QS;
	if(!rcu_gp_in_progress(rsp)) {
		rsp->n_force_qs_ngp++;
		raw_spin_unlock(&rnp->lock);  /* irqs remain disabled */
		goto unlock_fqs_ret;  /* no GP in progress, time updated. */
	}
	rsp->fqs_active = 1;
	switch (rsp->signaled) {
	case RCU_GP_IDLE:
	case RCU_GP_INIT:

		break; /* grace period idle or initializing, ignore. */

	case RCU_SAVE_DYNTICK:
		if (RCU_SIGNAL_INIT != RCU_SAVE_DYNTICK)
			break; /* So gcc recognizes the dead code. */

		raw_spin_unlock(&rnp->lock);  /* irqs remain disabled */

		/* Record dyntick-idle state. */
		force_qs_rnp(rsp, dyntick_save_progress_counter);
		raw_spin_lock(&rnp->lock);  /* irqs already disabled */
		if (rcu_gp_in_progress(rsp))
			rsp->signaled = RCU_FORCE_QS;
		break;

	case RCU_FORCE_QS:

		/* Check dyntick-idle state, send IPI to laggarts. */
		raw_spin_unlock(&rnp->lock);  /* irqs remain disabled */
		force_qs_rnp(rsp, rcu_implicit_dynticks_qs);

		/* Leave state in case more forcing is required. */

		raw_spin_lock(&rnp->lock);  /* irqs already disabled */
		break;
	}
	rsp->fqs_active = 0;
	if (rsp->fqs_need_gp) {
		raw_spin_unlock(&rsp->fqslock); /* irqs remain disabled */
		rsp->fqs_need_gp = 0;
		rcu_start_gp(rsp, flags); /* releases rnp->lock */
		trace_rcu_utilization("End fqs");
		return;
	}
	raw_spin_unlock(&rnp->lock);  /* irqs remain disabled */
unlock_fqs_ret:
	raw_spin_unlock_irqrestore(&rsp->fqslock, flags);
	trace_rcu_utilization("End fqs");
}

#else /* #ifdef CONFIG_SMP */

static void force_quiescent_state(struct rcu_state *rsp, int relaxed)
{
	set_need_resched();
}

#endif /* #else #ifdef CONFIG_SMP */

/*
 * This does the RCU core processing work for the specified rcu_state
 * and rcu_data structures.  This may be called only from the CPU to
 * whom the rdp belongs.
 */
static void
__rcu_process_callbacks(struct rcu_state *rsp, struct rcu_data *rdp)
{
	unsigned long flags;

	WARN_ON_ONCE(rdp->beenonline == 0);

	/*
	 * If an RCU GP has gone long enough, go check for dyntick
	 * idle CPUs and, if needed, send resched IPIs.
	 */
	if (ULONG_CMP_LT(ACCESS_ONCE(rsp->jiffies_force_qs), jiffies))
		force_quiescent_state(rsp, 1);

	/*
	 * Advance callbacks in response to end of earlier grace
	 * period that some other CPU ended.
	 */
	rcu_process_gp_end(rsp, rdp);

	/* Update RCU state based on any recent quiescent states. */
	rcu_check_quiescent_state(rsp, rdp);

	/* Does this CPU require a not-yet-started grace period? */
	if (cpu_needs_another_gp(rsp, rdp)) {
		raw_spin_lock_irqsave(&rcu_get_root(rsp)->lock, flags);
		rcu_start_gp(rsp, flags);  /* releases above lock */
	}

	/* If there are callbacks ready, invoke them. */
	if (cpu_has_callbacks_ready_to_invoke(rdp))
		invoke_rcu_callbacks(rsp, rdp);
}

/*
 * Do RCU core processing for the current CPU.
 */
static void rcu_process_callbacks(struct softirq_action *unused)
{
	trace_rcu_utilization("Start RCU core");
	__rcu_process_callbacks(&rcu_sched_state,
				&__get_cpu_var(rcu_sched_data));
	__rcu_process_callbacks(&rcu_bh_state, &__get_cpu_var(rcu_bh_data));
	rcu_preempt_process_callbacks();

	/* If we are last CPU on way to dyntick-idle mode, accelerate it. */
	rcu_needs_cpu_flush();
	trace_rcu_utilization("End RCU core");
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
	raise_softirq(RCU_SOFTIRQ);
}

static void
__call_rcu(struct rcu_head *head, void (*func)(struct rcu_head *rcu),
	   struct rcu_state *rsp)
{
	unsigned long flags;
	struct rcu_data *rdp;

	debug_rcu_head_queue(head);
	head->func = func;
	head->next = NULL;

	smp_mb(); /* Ensure RCU update seen before callback registry. */

	/*
	 * Opportunistically note grace-period endings and beginnings.
	 * Note that we might see a beginning right after we see an
	 * end, but never vice versa, since this CPU has to pass through
	 * a quiescent state betweentimes.
	 */
	local_irq_save(flags);
	rdp = this_cpu_ptr(rsp->rda);

	/* Add the callback to our list. */
	*rdp->nxttail[RCU_NEXT_TAIL] = head;
	rdp->nxttail[RCU_NEXT_TAIL] = &head->next;
	rdp->qlen++;

	if (__is_kfree_rcu_offset((unsigned long)func))
		trace_rcu_kfree_callback(rsp->name, head, (unsigned long)func,
					 rdp->qlen);
	else
		trace_rcu_callback(rsp->name, head, rdp->qlen);

	/* If interrupts were disabled, don't dive into RCU core. */
	if (irqs_disabled_flags(flags)) {
		local_irq_restore(flags);
		return;
	}

	/*
	 * Force the grace period if too many callbacks or too long waiting.
	 * Enforce hysteresis, and don't invoke force_quiescent_state()
	 * if some other CPU has recently done so.  Also, don't bother
	 * invoking force_quiescent_state() if the newly enqueued callback
	 * is the only one waiting for a grace period to complete.
	 */
	if (unlikely(rdp->qlen > rdp->qlen_last_fqs_check + qhimark)) {

		/* Are we ignoring a completed grace period? */
		rcu_process_gp_end(rsp, rdp);
		check_for_new_grace_period(rsp, rdp);

		/* Start a new grace period if one not already started. */
		if (!rcu_gp_in_progress(rsp)) {
			unsigned long nestflag;
			struct rcu_node *rnp_root = rcu_get_root(rsp);

			raw_spin_lock_irqsave(&rnp_root->lock, nestflag);
			rcu_start_gp(rsp, nestflag);  /* rlses rnp_root->lock */
		} else {
			/* Give the grace period a kick. */
			rdp->blimit = LONG_MAX;
			if (rsp->n_force_qs == rdp->n_force_qs_snap &&
			    *rdp->nxttail[RCU_DONE_TAIL] != head)
				force_quiescent_state(rsp, 0);
			rdp->n_force_qs_snap = rsp->n_force_qs;
			rdp->qlen_last_fqs_check = rdp->qlen;
		}
	} else if (ULONG_CMP_LT(ACCESS_ONCE(rsp->jiffies_force_qs), jiffies))
		force_quiescent_state(rsp, 1);
	local_irq_restore(flags);
}

/*
 * Queue an RCU-sched callback for invocation after a grace period.
 */
void call_rcu_sched(struct rcu_head *head, void (*func)(struct rcu_head *rcu))
{
	__call_rcu(head, func, &rcu_sched_state);
}
EXPORT_SYMBOL_GPL(call_rcu_sched);

/*
 * Queue an RCU for invocation after a quicker grace period.
 */
void call_rcu_bh(struct rcu_head *head, void (*func)(struct rcu_head *rcu))
{
	__call_rcu(head, func, &rcu_bh_state);
}
EXPORT_SYMBOL_GPL(call_rcu_bh);

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
 * hardware-interrupt handlers, in progress on entry will have completed
 * before this primitive returns.  However, this does not guarantee that
 * softirq handlers will have completed, since in some kernels, these
 * handlers can run in process context, and can block.
 *
 * This primitive provides the guarantees made by the (now removed)
 * synchronize_kernel() API.  In contrast, synchronize_rcu() only
 * guarantees that rcu_read_lock() sections will have completed.
 * In "classic RCU", these two guarantees happen to be one and
 * the same, but can differ in realtime RCU implementations.
 */
void synchronize_sched(void)
{
	if (rcu_blocking_is_gp())
		return;
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
 */
void synchronize_rcu_bh(void)
{
	if (rcu_blocking_is_gp())
		return;
	wait_rcu_gp(call_rcu_bh);
}
EXPORT_SYMBOL_GPL(synchronize_rcu_bh);

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

	/* Is the RCU core waiting for a quiescent state from this CPU? */
	if (rdp->qs_pending && !rdp->passed_quiesce) {

		/*
		 * If force_quiescent_state() coming soon and this CPU
		 * needs a quiescent state, and this is either RCU-sched
		 * or RCU-bh, force a local reschedule.
		 */
		rdp->n_rp_qs_pending++;
		if (!rdp->preemptible &&
		    ULONG_CMP_LT(ACCESS_ONCE(rsp->jiffies_force_qs) - 1,
				 jiffies))
			set_need_resched();
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

	/* Has an RCU GP gone long enough to send resched IPIs &c? */
	if (rcu_gp_in_progress(rsp) &&
	    ULONG_CMP_LT(ACCESS_ONCE(rsp->jiffies_force_qs), jiffies)) {
		rdp->n_rp_need_fqs++;
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
	return __rcu_pending(&rcu_sched_state, &per_cpu(rcu_sched_data, cpu)) ||
	       __rcu_pending(&rcu_bh_state, &per_cpu(rcu_bh_data, cpu)) ||
	       rcu_preempt_pending(cpu);
}

/*
 * Check to see if any future RCU-related work will need to be done
 * by the current CPU, even if none need be done immediately, returning
 * 1 if so.
 */
static int rcu_needs_cpu_quick_check(int cpu)
{
	/* RCU callbacks either ready or pending? */
	return per_cpu(rcu_sched_data, cpu).nxtlist ||
	       per_cpu(rcu_bh_data, cpu).nxtlist ||
	       rcu_preempt_needs_cpu(cpu);
}

static DEFINE_PER_CPU(struct rcu_head, rcu_barrier_head) = {NULL};
static atomic_t rcu_barrier_cpu_count;
static DEFINE_MUTEX(rcu_barrier_mutex);
static struct completion rcu_barrier_completion;

static void rcu_barrier_callback(struct rcu_head *notused)
{
	if (atomic_dec_and_test(&rcu_barrier_cpu_count))
		complete(&rcu_barrier_completion);
}

/*
 * Called with preemption disabled, and from cross-cpu IRQ context.
 */
static void rcu_barrier_func(void *type)
{
	int cpu = smp_processor_id();
	struct rcu_head *head = &per_cpu(rcu_barrier_head, cpu);
	void (*call_rcu_func)(struct rcu_head *head,
			      void (*func)(struct rcu_head *head));

	atomic_inc(&rcu_barrier_cpu_count);
	call_rcu_func = type;
	call_rcu_func(head, rcu_barrier_callback);
}

/*
 * Orchestrate the specified type of RCU barrier, waiting for all
 * RCU callbacks of the specified type to complete.
 */
static void _rcu_barrier(struct rcu_state *rsp,
			 void (*call_rcu_func)(struct rcu_head *head,
					       void (*func)(struct rcu_head *head)))
{
	BUG_ON(in_interrupt());
	/* Take mutex to serialize concurrent rcu_barrier() requests. */
	mutex_lock(&rcu_barrier_mutex);
	init_completion(&rcu_barrier_completion);
	/*
	 * Initialize rcu_barrier_cpu_count to 1, then invoke
	 * rcu_barrier_func() on each CPU, so that each CPU also has
	 * incremented rcu_barrier_cpu_count.  Only then is it safe to
	 * decrement rcu_barrier_cpu_count -- otherwise the first CPU
	 * might complete its grace period before all of the other CPUs
	 * did their increment, causing this function to return too
	 * early.  Note that on_each_cpu() disables irqs, which prevents
	 * any CPUs from coming online or going offline until each online
	 * CPU has queued its RCU-barrier callback.
	 */
	atomic_set(&rcu_barrier_cpu_count, 1);
	on_each_cpu(rcu_barrier_func, (void *)call_rcu_func, 1);
	if (atomic_dec_and_test(&rcu_barrier_cpu_count))
		complete(&rcu_barrier_completion);
	wait_for_completion(&rcu_barrier_completion);
	mutex_unlock(&rcu_barrier_mutex);
}

/**
 * rcu_barrier_bh - Wait until all in-flight call_rcu_bh() callbacks complete.
 */
void rcu_barrier_bh(void)
{
	_rcu_barrier(&rcu_bh_state, call_rcu_bh);
}
EXPORT_SYMBOL_GPL(rcu_barrier_bh);

/**
 * rcu_barrier_sched - Wait for in-flight call_rcu_sched() callbacks.
 */
void rcu_barrier_sched(void)
{
	_rcu_barrier(&rcu_sched_state, call_rcu_sched);
}
EXPORT_SYMBOL_GPL(rcu_barrier_sched);

/*
 * Do boot-time initialization of a CPU's per-CPU RCU data.
 */
static void __init
rcu_boot_init_percpu_data(int cpu, struct rcu_state *rsp)
{
	unsigned long flags;
	int i;
	struct rcu_data *rdp = per_cpu_ptr(rsp->rda, cpu);
	struct rcu_node *rnp = rcu_get_root(rsp);

	/* Set up local state, ensuring consistent view of global state. */
	raw_spin_lock_irqsave(&rnp->lock, flags);
	rdp->grpmask = 1UL << (cpu - rdp->mynode->grplo);
	rdp->nxtlist = NULL;
	for (i = 0; i < RCU_NEXT_SIZE; i++)
		rdp->nxttail[i] = &rdp->nxtlist;
	rdp->qlen = 0;
#ifdef CONFIG_NO_HZ
	rdp->dynticks = &per_cpu(rcu_dynticks, cpu);
#endif /* #ifdef CONFIG_NO_HZ */
	rdp->cpu = cpu;
	rdp->rsp = rsp;
	raw_spin_unlock_irqrestore(&rnp->lock, flags);
}

/*
 * Initialize a CPU's per-CPU RCU data.  Note that only one online or
 * offline event can be happening at a given time.  Note also that we
 * can accept some slop in the rsp->completed access due to the fact
 * that this CPU cannot possibly have any RCU callbacks in flight yet.
 */
static void __cpuinit
rcu_init_percpu_data(int cpu, struct rcu_state *rsp, int preemptible)
{
	unsigned long flags;
	unsigned long mask;
	struct rcu_data *rdp = per_cpu_ptr(rsp->rda, cpu);
	struct rcu_node *rnp = rcu_get_root(rsp);

	/* Set up local state, ensuring consistent view of global state. */
	raw_spin_lock_irqsave(&rnp->lock, flags);
	rdp->passed_quiesce = 0;  /* We could be racing with new GP, */
	rdp->qs_pending = 1;	 /*  so set up to respond to current GP. */
	rdp->beenonline = 1;	 /* We have now been online. */
	rdp->preemptible = preemptible;
	rdp->qlen_last_fqs_check = 0;
	rdp->n_force_qs_snap = rsp->n_force_qs;
	rdp->blimit = blimit;
	raw_spin_unlock(&rnp->lock);		/* irqs remain disabled. */

	/*
	 * A new grace period might start here.  If so, we won't be part
	 * of it, but that is OK, as we are currently in a quiescent state.
	 */

	/* Exclude any attempts to start a new GP on large systems. */
	raw_spin_lock(&rsp->onofflock);		/* irqs already disabled. */

	/* Add CPU to rcu_node bitmasks. */
	rnp = rdp->mynode;
	mask = rdp->grpmask;
	do {
		/* Exclude any attempts to start a new GP on small systems. */
		raw_spin_lock(&rnp->lock);	/* irqs already disabled. */
		rnp->qsmaskinit |= mask;
		mask = rnp->grpmask;
		if (rnp == rdp->mynode) {
			rdp->gpnum = rnp->completed; /* if GP in progress... */
			rdp->completed = rnp->completed;
			rdp->passed_quiesce_gpnum = rnp->gpnum - 1;
			trace_rcu_grace_period(rsp->name, rdp->gpnum, "cpuonl");
		}
		raw_spin_unlock(&rnp->lock); /* irqs already disabled. */
		rnp = rnp->parent;
	} while (rnp != NULL && !(rnp->qsmaskinit & mask));

	raw_spin_unlock_irqrestore(&rsp->onofflock, flags);
}

static void __cpuinit rcu_prepare_cpu(int cpu)
{
	rcu_init_percpu_data(cpu, &rcu_sched_state, 0);
	rcu_init_percpu_data(cpu, &rcu_bh_state, 0);
	rcu_preempt_init_percpu_data(cpu);
}

/*
 * Handle CPU online/offline notification events.
 */
static int __cpuinit rcu_cpu_notify(struct notifier_block *self,
				    unsigned long action, void *hcpu)
{
	long cpu = (long)hcpu;
	struct rcu_data *rdp = per_cpu_ptr(rcu_state->rda, cpu);
	struct rcu_node *rnp = rdp->mynode;

	trace_rcu_utilization("Start CPU hotplug");
	switch (action) {
	case CPU_UP_PREPARE:
	case CPU_UP_PREPARE_FROZEN:
		rcu_prepare_cpu(cpu);
		rcu_prepare_kthreads(cpu);
		break;
	case CPU_ONLINE:
	case CPU_DOWN_FAILED:
		rcu_node_kthread_setaffinity(rnp, -1);
		rcu_cpu_kthread_setrt(cpu, 1);
		break;
	case CPU_DOWN_PREPARE:
		rcu_node_kthread_setaffinity(rnp, cpu);
		rcu_cpu_kthread_setrt(cpu, 0);
		break;
	case CPU_DYING:
	case CPU_DYING_FROZEN:
		/*
		 * The whole machine is "stopped" except this CPU, so we can
		 * touch any data without introducing corruption. We send the
		 * dying CPU's callbacks to an arbitrarily chosen online CPU.
		 */
		rcu_send_cbs_to_online(&rcu_bh_state);
		rcu_send_cbs_to_online(&rcu_sched_state);
		rcu_preempt_send_cbs_to_online();
		break;
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
	case CPU_UP_CANCELED:
	case CPU_UP_CANCELED_FROZEN:
		rcu_offline_cpu(cpu);
		break;
	default:
		break;
	}
	trace_rcu_utilization("End CPU hotplug");
	return NOTIFY_OK;
}

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

	for (i = NUM_RCU_LVLS - 1; i > 0; i--)
		rsp->levelspread[i] = CONFIG_RCU_FANOUT;
	rsp->levelspread[0] = RCU_FANOUT_LEAF;
}
#else /* #ifdef CONFIG_RCU_FANOUT_EXACT */
static void __init rcu_init_levelspread(struct rcu_state *rsp)
{
	int ccur;
	int cprv;
	int i;

	cprv = NR_CPUS;
	for (i = NUM_RCU_LVLS - 1; i >= 0; i--) {
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
	static char *buf[] = { "rcu_node_level_0",
			       "rcu_node_level_1",
			       "rcu_node_level_2",
			       "rcu_node_level_3" };  /* Match MAX_RCU_LVLS */
	int cpustride = 1;
	int i;
	int j;
	struct rcu_node *rnp;

	BUILD_BUG_ON(MAX_RCU_LVLS > ARRAY_SIZE(buf));  /* Fix buf[] init! */

	/* Initialize the level-tracking arrays. */

	for (i = 1; i < NUM_RCU_LVLS; i++)
		rsp->level[i] = rsp->level[i - 1] + rsp->levelcnt[i - 1];
	rcu_init_levelspread(rsp);

	/* Initialize the elements themselves, starting from the leaves. */

	for (i = NUM_RCU_LVLS - 1; i >= 0; i--) {
		cpustride *= rsp->levelspread[i];
		rnp = rsp->level[i];
		for (j = 0; j < rsp->levelcnt[i]; j++, rnp++) {
			raw_spin_lock_init(&rnp->lock);
			lockdep_set_class_and_name(&rnp->lock,
						   &rcu_node_class[i], buf[i]);
			rnp->gpnum = 0;
			rnp->qsmask = 0;
			rnp->qsmaskinit = 0;
			rnp->grplo = j * cpustride;
			rnp->grphi = (j + 1) * cpustride - 1;
			if (rnp->grphi >= NR_CPUS)
				rnp->grphi = NR_CPUS - 1;
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
		}
	}

	rsp->rda = rda;
	rnp = rsp->level[NUM_RCU_LVLS - 1];
	for_each_possible_cpu(i) {
		while (i > rnp->grphi)
			rnp++;
		per_cpu_ptr(rsp->rda, i)->mynode = rnp;
		rcu_boot_init_percpu_data(i, rsp);
	}
}

void __init rcu_init(void)
{
	int cpu;

	rcu_bootup_announce();
	rcu_init_one(&rcu_sched_state, &rcu_sched_data);
	rcu_init_one(&rcu_bh_state, &rcu_bh_data);
	__rcu_init_preempt();
	 open_softirq(RCU_SOFTIRQ, rcu_process_callbacks);

	/*
	 * We don't need protection against CPU-hotplug here because
	 * this is called early in boot, before either interrupts
	 * or the scheduler are operational.
	 */
	cpu_notifier(rcu_cpu_notify, 0);
	for_each_online_cpu(cpu)
		rcu_cpu_notify(NULL, CPU_UP_PREPARE, (void *)(long)cpu);
	check_cpu_stall_init();
}

#include "rcutree_plugin.h"
