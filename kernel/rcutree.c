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
#include <linux/export.h>
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
#include <linux/delay.h>
#include <linux/stop_machine.h>

#include "rcutree.h"
#include <trace/events/rcu.h>

#include "rcu.h"

/* Data structures. */

static struct lock_class_key rcu_node_class[RCU_NUM_LVLS];

#define RCU_STATE_INITIALIZER(sname) { \
	.level = { &sname##_state.node[0] }, \
	.fqs_state = RCU_GP_IDLE, \
	.gpnum = -300, \
	.completed = -300, \
	.onofflock = __RAW_SPIN_LOCK_UNLOCKED(&sname##_state.onofflock), \
	.orphan_nxttail = &sname##_state.orphan_nxtlist, \
	.orphan_donetail = &sname##_state.orphan_donelist, \
	.fqslock = __RAW_SPIN_LOCK_UNLOCKED(&sname##_state.fqslock), \
	.n_force_qs = 0, \
	.n_force_qs_ngp = 0, \
	.name = #sname, \
}

struct rcu_state rcu_sched_state = RCU_STATE_INITIALIZER(rcu_sched);
DEFINE_PER_CPU(struct rcu_data, rcu_sched_data);

struct rcu_state rcu_bh_state = RCU_STATE_INITIALIZER(rcu_bh);
DEFINE_PER_CPU(struct rcu_data, rcu_bh_data);

static struct rcu_state *rcu_state;

/* Increase (but not decrease) the CONFIG_RCU_FANOUT_LEAF at boot time. */
static int rcu_fanout_leaf = CONFIG_RCU_FANOUT_LEAF;
module_param(rcu_fanout_leaf, int, 0);
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

/* State information for rcu_barrier() and friends. */

static DEFINE_PER_CPU(struct rcu_head, rcu_barrier_head) = {NULL};
static atomic_t rcu_barrier_cpu_count;
static DEFINE_MUTEX(rcu_barrier_mutex);
static struct completion rcu_barrier_completion;

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

DEFINE_PER_CPU(struct rcu_dynticks, rcu_dynticks) = {
	.dynticks_nesting = DYNTICK_TASK_EXIT_IDLE,
	.dynticks = ATOMIC_INIT(1),
};

static int blimit = 10;		/* Maximum callbacks per rcu_do_batch. */
static int qhimark = 10000;	/* If this many pending, ignore blimit. */
static int qlowmark = 100;	/* Once only this many pending, use blimit. */

module_param(blimit, int, 0);
module_param(qhimark, int, 0);
module_param(qlowmark, int, 0);

int rcu_cpu_stall_suppress __read_mostly; /* 1 = suppress stall warnings. */
int rcu_cpu_stall_timeout __read_mostly = CONFIG_RCU_CPU_STALL_TIMEOUT;

module_param(rcu_cpu_stall_suppress, int, 0644);
module_param(rcu_cpu_stall_timeout, int, 0644);

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
	 * If the CPU is offline for more than a jiffy, it is in a quiescent
	 * state.  We can trust its state not to change because interrupts
	 * are disabled.  The reason for the jiffy's worth of slack is to
	 * handle CPUs initializing on the way up and finding their way
	 * to the idle loop on the way down.
	 */
	if (cpu_is_offline(rdp->cpu) &&
	    ULONG_CMP_LT(rdp->rsp->gp_start + 2, jiffies)) {
		trace_rcu_fqs(rdp->rsp->name, rdp->gpnum, rdp->cpu, "ofl");
		rdp->offline_fqs++;
		return 1;
	}
	return 0;
}

/*
 * rcu_idle_enter_common - inform RCU that current CPU is moving towards idle
 *
 * If the new value of the ->dynticks_nesting counter now is zero,
 * we really have entered idle, and must do the appropriate accounting.
 * The caller must have disabled interrupts.
 */
static void rcu_idle_enter_common(struct rcu_dynticks *rdtp, long long oldval)
{
	trace_rcu_dyntick("Start", oldval, 0);
	if (!is_idle_task(current)) {
		struct task_struct *idle = idle_task(smp_processor_id());

		trace_rcu_dyntick("Error on entry: not idle task", oldval, 0);
		ftrace_dump(DUMP_ALL);
		WARN_ONCE(1, "Current pid: %d comm: %s / Idle pid: %d comm: %s",
			  current->pid, current->comm,
			  idle->pid, idle->comm); /* must be idle task! */
	}
	rcu_prepare_for_idle(smp_processor_id());
	/* CPUs seeing atomic_inc() must see prior RCU read-side crit sects */
	smp_mb__before_atomic_inc();  /* See above. */
	atomic_inc(&rdtp->dynticks);
	smp_mb__after_atomic_inc();  /* Force ordering with next sojourn. */
	WARN_ON_ONCE(atomic_read(&rdtp->dynticks) & 0x1);

	/*
	 * The idle task is not permitted to enter the idle loop while
	 * in an RCU read-side critical section.
	 */
	rcu_lockdep_assert(!lock_is_held(&rcu_lock_map),
			   "Illegal idle entry in RCU read-side critical section.");
	rcu_lockdep_assert(!lock_is_held(&rcu_bh_lock_map),
			   "Illegal idle entry in RCU-bh read-side critical section.");
	rcu_lockdep_assert(!lock_is_held(&rcu_sched_lock_map),
			   "Illegal idle entry in RCU-sched read-side critical section.");
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
	long long oldval;
	struct rcu_dynticks *rdtp;

	local_irq_save(flags);
	rdtp = &__get_cpu_var(rcu_dynticks);
	oldval = rdtp->dynticks_nesting;
	WARN_ON_ONCE((oldval & DYNTICK_TASK_NEST_MASK) == 0);
	if ((oldval & DYNTICK_TASK_NEST_MASK) == DYNTICK_TASK_NEST_VALUE)
		rdtp->dynticks_nesting = 0;
	else
		rdtp->dynticks_nesting -= DYNTICK_TASK_NEST_VALUE;
	rcu_idle_enter_common(rdtp, oldval);
	local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(rcu_idle_enter);

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
	rdtp = &__get_cpu_var(rcu_dynticks);
	oldval = rdtp->dynticks_nesting;
	rdtp->dynticks_nesting--;
	WARN_ON_ONCE(rdtp->dynticks_nesting < 0);
	if (rdtp->dynticks_nesting)
		trace_rcu_dyntick("--=", oldval, rdtp->dynticks_nesting);
	else
		rcu_idle_enter_common(rdtp, oldval);
	local_irq_restore(flags);
}

/*
 * rcu_idle_exit_common - inform RCU that current CPU is moving away from idle
 *
 * If the new value of the ->dynticks_nesting counter was previously zero,
 * we really have exited idle, and must do the appropriate accounting.
 * The caller must have disabled interrupts.
 */
static void rcu_idle_exit_common(struct rcu_dynticks *rdtp, long long oldval)
{
	smp_mb__before_atomic_inc();  /* Force ordering w/previous sojourn. */
	atomic_inc(&rdtp->dynticks);
	/* CPUs seeing atomic_inc() must see later RCU read-side crit sects */
	smp_mb__after_atomic_inc();  /* See above. */
	WARN_ON_ONCE(!(atomic_read(&rdtp->dynticks) & 0x1));
	rcu_cleanup_after_idle(smp_processor_id());
	trace_rcu_dyntick("End", oldval, rdtp->dynticks_nesting);
	if (!is_idle_task(current)) {
		struct task_struct *idle = idle_task(smp_processor_id());

		trace_rcu_dyntick("Error on exit: not idle task",
				  oldval, rdtp->dynticks_nesting);
		ftrace_dump(DUMP_ALL);
		WARN_ONCE(1, "Current pid: %d comm: %s / Idle pid: %d comm: %s",
			  current->pid, current->comm,
			  idle->pid, idle->comm); /* must be idle task! */
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
	struct rcu_dynticks *rdtp;
	long long oldval;

	local_irq_save(flags);
	rdtp = &__get_cpu_var(rcu_dynticks);
	oldval = rdtp->dynticks_nesting;
	WARN_ON_ONCE(oldval < 0);
	if (oldval & DYNTICK_TASK_NEST_MASK)
		rdtp->dynticks_nesting += DYNTICK_TASK_NEST_VALUE;
	else
		rdtp->dynticks_nesting = DYNTICK_TASK_EXIT_IDLE;
	rcu_idle_exit_common(rdtp, oldval);
	local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(rcu_idle_exit);

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
	rdtp = &__get_cpu_var(rcu_dynticks);
	oldval = rdtp->dynticks_nesting;
	rdtp->dynticks_nesting++;
	WARN_ON_ONCE(rdtp->dynticks_nesting == 0);
	if (oldval)
		trace_rcu_dyntick("++=", oldval, rdtp->dynticks_nesting);
	else
		rcu_idle_exit_common(rdtp, oldval);
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

#ifdef CONFIG_PROVE_RCU

/**
 * rcu_is_cpu_idle - see if RCU thinks that the current CPU is idle
 *
 * If the current CPU is in its idle loop and is neither in an interrupt
 * or NMI handler, return true.
 */
int rcu_is_cpu_idle(void)
{
	int ret;

	preempt_disable();
	ret = (atomic_read(&__get_cpu_var(rcu_dynticks).dynticks) & 0x1) == 0;
	preempt_enable();
	return ret;
}
EXPORT_SYMBOL(rcu_is_cpu_idle);

#ifdef CONFIG_HOTPLUG_CPU

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
		return 1;
	preempt_disable();
	rdp = &__get_cpu_var(rcu_sched_data);
	rnp = rdp->mynode;
	ret = (rdp->grpmask & rnp->qsmaskinit) ||
	      !rcu_scheduler_fully_active;
	preempt_enable();
	return ret;
}
EXPORT_SYMBOL_GPL(rcu_lockdep_current_cpu_online);

#endif /* #ifdef CONFIG_HOTPLUG_CPU */

#endif /* #ifdef CONFIG_PROVE_RCU */

/**
 * rcu_is_cpu_rrupt_from_idle - see if idle or immediately interrupted from idle
 *
 * If the current CPU is idle or running at a first-level (not nested)
 * interrupt from idle, return true.  The caller must have at least
 * disabled preemption.
 */
int rcu_is_cpu_rrupt_from_idle(void)
{
	return __get_cpu_var(rcu_dynticks).dynticks_nesting <= 1;
}

/*
 * Snapshot the specified CPU's dynticks counter so that we can later
 * credit them with an implicit quiescent state.  Return 1 if this CPU
 * is in dynticks idle mode, which is an extended quiescent state.
 */
static int dyntick_save_progress_counter(struct rcu_data *rdp)
{
	rdp->dynticks_snap = atomic_add_return(0, &rdp->dynticks->dynticks);
	return (rdp->dynticks_snap & 0x1) == 0;
}

/*
 * Return true if the specified CPU has passed through a quiescent
 * state by virtue of being in or having passed through an dynticks
 * idle state since the last call to dyntick_save_progress_counter()
 * for this same CPU.
 */
static int rcu_implicit_dynticks_qs(struct rcu_data *rdp)
{
	unsigned int curr;
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
		trace_rcu_fqs(rdp->rsp->name, rdp->gpnum, rdp->cpu, "dti");
		rdp->dynticks_fqs++;
		return 1;
	}

	/* Go check for the CPU being offline. */
	return rcu_implicit_offline_qs(rdp);
}

static int jiffies_till_stall_check(void)
{
	int till_stall_check = ACCESS_ONCE(rcu_cpu_stall_timeout);

	/*
	 * Limit check must be consistent with the Kconfig limits
	 * for CONFIG_RCU_CPU_STALL_TIMEOUT.
	 */
	if (till_stall_check < 3) {
		ACCESS_ONCE(rcu_cpu_stall_timeout) = 3;
		till_stall_check = 3;
	} else if (till_stall_check > 300) {
		ACCESS_ONCE(rcu_cpu_stall_timeout) = 300;
		till_stall_check = 300;
	}
	return till_stall_check * HZ + RCU_STALL_DELAY_DELTA;
}

static void record_gp_stall_check_time(struct rcu_state *rsp)
{
	rsp->gp_start = jiffies;
	rsp->jiffies_stall = jiffies + jiffies_till_stall_check();
}

static void print_other_cpu_stall(struct rcu_state *rsp)
{
	int cpu;
	long delta;
	unsigned long flags;
	int ndetected;
	struct rcu_node *rnp = rcu_get_root(rsp);

	/* Only let one CPU complain about others per time interval. */

	raw_spin_lock_irqsave(&rnp->lock, flags);
	delta = jiffies - rsp->jiffies_stall;
	if (delta < RCU_STALL_RAT_DELAY || !rcu_gp_in_progress(rsp)) {
		raw_spin_unlock_irqrestore(&rnp->lock, flags);
		return;
	}
	rsp->jiffies_stall = jiffies + 3 * jiffies_till_stall_check() + 3;
	raw_spin_unlock_irqrestore(&rnp->lock, flags);

	/*
	 * OK, time to rat on our buddy...
	 * See Documentation/RCU/stallwarn.txt for info on how to debug
	 * RCU CPU stall warnings.
	 */
	printk(KERN_ERR "INFO: %s detected stalls on CPUs/tasks:",
	       rsp->name);
	print_cpu_stall_info_begin();
	rcu_for_each_leaf_node(rsp, rnp) {
		raw_spin_lock_irqsave(&rnp->lock, flags);
		ndetected += rcu_print_task_stall(rnp);
		raw_spin_unlock_irqrestore(&rnp->lock, flags);
		if (rnp->qsmask == 0)
			continue;
		for (cpu = 0; cpu <= rnp->grphi - rnp->grplo; cpu++)
			if (rnp->qsmask & (1UL << cpu)) {
				print_cpu_stall_info(rsp, rnp->grplo + cpu);
				ndetected++;
			}
	}

	/*
	 * Now rat on any tasks that got kicked up to the root rcu_node
	 * due to CPU offlining.
	 */
	rnp = rcu_get_root(rsp);
	raw_spin_lock_irqsave(&rnp->lock, flags);
	ndetected = rcu_print_task_stall(rnp);
	raw_spin_unlock_irqrestore(&rnp->lock, flags);

	print_cpu_stall_info_end();
	printk(KERN_CONT "(detected by %d, t=%ld jiffies)\n",
	       smp_processor_id(), (long)(jiffies - rsp->gp_start));
	if (ndetected == 0)
		printk(KERN_ERR "INFO: Stall ended before state dump start\n");
	else if (!trigger_all_cpu_backtrace())
		dump_stack();

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
	printk(KERN_ERR "INFO: %s self-detected stall on CPU", rsp->name);
	print_cpu_stall_info_begin();
	print_cpu_stall_info(rsp, smp_processor_id());
	print_cpu_stall_info_end();
	printk(KERN_CONT " (t=%lu jiffies)\n", jiffies - rsp->gp_start);
	if (!trigger_all_cpu_backtrace())
		dump_stack();

	raw_spin_lock_irqsave(&rnp->lock, flags);
	if (ULONG_CMP_GE(jiffies, rsp->jiffies_stall))
		rsp->jiffies_stall = jiffies +
				     3 * jiffies_till_stall_check() + 3;
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
		zero_cpu_stall_ticks(rdp);
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
 *
 * Note that it is legal for a dying CPU (which is marked as offline) to
 * invoke this function.  This can happen when the dying CPU reports its
 * quiescent state.
 */
static void
rcu_start_gp(struct rcu_state *rsp, unsigned long flags)
	__releases(rcu_get_root(rsp)->lock)
{
	struct rcu_data *rdp = this_cpu_ptr(rsp->rda);
	struct rcu_node *rnp = rcu_get_root(rsp);

	if (!rcu_scheduler_fully_active ||
	    !cpu_needs_another_gp(rsp, rdp)) {
		/*
		 * Either the scheduler hasn't yet spawned the first
		 * non-idle task or this CPU does not need another
		 * grace period.  Either way, don't start a new grace
		 * period.
		 */
		raw_spin_unlock_irqrestore(&rnp->lock, flags);
		return;
	}

	if (rsp->fqs_active) {
		/*
		 * This CPU needs a grace period, but force_quiescent_state()
		 * is running.  Tell it to start one on this CPU's behalf.
		 */
		rsp->fqs_need_gp = 1;
		raw_spin_unlock_irqrestore(&rnp->lock, flags);
		return;
	}

	/* Advance to a new grace period and initialize state. */
	rsp->gpnum++;
	trace_rcu_grace_period(rsp->name, rsp->gpnum, "start");
	WARN_ON_ONCE(rsp->fqs_state == RCU_GP_INIT);
	rsp->fqs_state = RCU_GP_INIT; /* Hold off force_quiescent_state. */
	rsp->jiffies_force_qs = jiffies + RCU_JIFFIES_TILL_FORCE_QS;
	record_gp_stall_check_time(rsp);
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
	rsp->fqs_state = RCU_SIGNAL_INIT; /* force_quiescent_state now OK. */
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
	struct rcu_node *rnp = rcu_get_root(rsp);
	struct rcu_data *rdp = this_cpu_ptr(rsp->rda);

	WARN_ON_ONCE(!rcu_gp_in_progress(rsp));

	/*
	 * Ensure that all grace-period and pre-grace-period activity
	 * is seen before the assignment to rsp->completed.
	 */
	smp_mb(); /* See above block comment. */
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
	 *
	 * But if this CPU needs another grace period, it will take
	 * care of this while initializing the next grace period.
	 * We use RCU_WAIT_TAIL instead of the usual RCU_DONE_TAIL
	 * because the callbacks have not yet been advanced: Those
	 * callbacks are waiting on the grace period that just now
	 * completed.
	 */
	if (*rdp->nxttail[RCU_WAIT_TAIL] == NULL) {
		raw_spin_unlock(&rnp->lock);	 /* irqs remain disabled. */

		/*
		 * Propagate new ->completed value to rcu_node structures
		 * so that other CPUs don't have to wait until the start
		 * of the next grace period to process their callbacks.
		 */
		rcu_for_each_node_breadth_first(rsp, rnp) {
			raw_spin_lock(&rnp->lock); /* irqs already disabled. */
			rnp->completed = rsp->gpnum;
			raw_spin_unlock(&rnp->lock); /* irqs remain disabled. */
		}
		rnp = rcu_get_root(rsp);
		raw_spin_lock(&rnp->lock); /* irqs already disabled. */
	}

	rsp->completed = rsp->gpnum;  /* Declare the grace period complete. */
	trace_rcu_grace_period(rsp->name, rsp->completed, "end");
	rsp->fqs_state = RCU_GP_IDLE;
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
 * Send the specified CPU's RCU callbacks to the orphanage.  The
 * specified CPU must be offline, and the caller must hold the
 * ->onofflock.
 */
static void
rcu_send_cbs_to_orphanage(int cpu, struct rcu_state *rsp,
			  struct rcu_node *rnp, struct rcu_data *rdp)
{
	int i;

	/*
	 * Orphan the callbacks.  First adjust the counts.  This is safe
	 * because ->onofflock excludes _rcu_barrier()'s adoption of
	 * the callbacks, thus no memory barrier is required.
	 */
	if (rdp->nxtlist != NULL) {
		rsp->qlen_lazy += rdp->qlen_lazy;
		rsp->qlen += rdp->qlen;
		rdp->n_cbs_orphaned += rdp->qlen;
		rdp->qlen_lazy = 0;
		rdp->qlen = 0;
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
	rdp->nxtlist = NULL;
	for (i = 0; i < RCU_NEXT_SIZE; i++)
		rdp->nxttail[i] = &rdp->nxtlist;
}

/*
 * Adopt the RCU callbacks from the specified rcu_state structure's
 * orphanage.  The caller must hold the ->onofflock.
 */
static void rcu_adopt_orphan_cbs(struct rcu_state *rsp)
{
	int i;
	struct rcu_data *rdp = __this_cpu_ptr(rsp->rda);

	/*
	 * If there is an rcu_barrier() operation in progress, then
	 * only the task doing that operation is permitted to adopt
	 * callbacks.  To do otherwise breaks rcu_barrier() and friends
	 * by causing them to fail to wait for the callbacks in the
	 * orphanage.
	 */
	if (rsp->rcu_barrier_in_progress &&
	    rsp->rcu_barrier_in_progress != current)
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
			       "cpuofl");
}

/*
 * The CPU has been completely removed, and some other CPU is reporting
 * this fact from process context.  Do the remainder of the cleanup,
 * including orphaning the outgoing CPU's RCU callbacks, and also
 * adopting them, if there is no _rcu_barrier() instance running.
 * There can only be one CPU hotplug operation at a time, so no other
 * CPU can be attempting to update rcu_cpu_kthread_task.
 */
static void rcu_cleanup_dead_cpu(int cpu, struct rcu_state *rsp)
{
	unsigned long flags;
	unsigned long mask;
	int need_report = 0;
	struct rcu_data *rdp = per_cpu_ptr(rsp->rda, cpu);
	struct rcu_node *rnp = rdp->mynode;  /* Outgoing CPU's rdp & rnp. */

	/* Adjust any no-longer-needed kthreads. */
	rcu_stop_cpu_kthread(cpu);
	rcu_node_kthread_setaffinity(rnp, -1);

	/* Remove the dead CPU from the bitmasks in the rcu_node hierarchy. */

	/* Exclude any attempts to start a new grace period. */
	raw_spin_lock_irqsave(&rsp->onofflock, flags);

	/* Orphan the dead CPU's callbacks, and adopt them if appropriate. */
	rcu_send_cbs_to_orphanage(cpu, rsp, rnp, rdp);
	rcu_adopt_orphan_cbs(rsp);

	/* Remove the outgoing CPU from the masks in the rcu_node hierarchy. */
	mask = rdp->grpmask;	/* rnp->grplo is constant. */
	do {
		raw_spin_lock(&rnp->lock);	/* irqs already disabled. */
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
		rcu_report_exp_rnp(rsp, rnp, true);
}

#else /* #ifdef CONFIG_HOTPLUG_CPU */

static void rcu_adopt_orphan_cbs(struct rcu_state *rsp)
{
}

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
	int bl, count, count_lazy, i;

	/* If no callbacks are ready, just return.*/
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
	rdp->qlen -= count;
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
	trace_rcu_utilization("Start scheduler-tick");
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
	trace_rcu_utilization("End scheduler-tick");
}

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
	switch (rsp->fqs_state) {
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
			rsp->fqs_state = RCU_FORCE_QS;
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
	   struct rcu_state *rsp, bool lazy)
{
	unsigned long flags;
	struct rcu_data *rdp;

	WARN_ON_ONCE((unsigned long)head & 0x3); /* Misaligned rcu_head! */
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
	rdp->qlen++;
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
	__call_rcu(head, func, &rcu_sched_state, 0);
}
EXPORT_SYMBOL_GPL(call_rcu_sched);

/*
 * Queue an RCU callback for invocation after a quicker grace period.
 */
void call_rcu_bh(struct rcu_head *head, void (*func)(struct rcu_head *rcu))
{
	__call_rcu(head, func, &rcu_bh_state, 0);
}
EXPORT_SYMBOL_GPL(call_rcu_bh);

/*
 * Because a context switch is a grace period for RCU-sched and RCU-bh,
 * any blocking grace-period wait automatically implies a grace period
 * if there is only one CPU online at any point time during execution
 * of either synchronize_sched() or synchronize_rcu_bh().  It is OK to
 * occasionally incorrectly indicate that there are multiple CPUs online
 * when there was in fact only one the whole time, as this just adds
 * some overhead: RCU still operates correctly.
 *
 * Of course, sampling num_online_cpus() with preemption enabled can
 * give erroneous results if there are concurrent CPU-hotplug operations.
 * For example, given a demonic sequence of preemptions in num_online_cpus()
 * and CPU-hotplug operations, there could be two or more CPUs online at
 * all times, but num_online_cpus() might well return one (or even zero).
 *
 * However, all such demonic sequences require at least one CPU-offline
 * operation.  Furthermore, rcu_blocking_is_gp() giving the wrong answer
 * is only a problem if there is an RCU read-side critical section executing
 * throughout.  But RCU-sched and RCU-bh read-side critical sections
 * disable either preemption or bh, which prevents a CPU from going offline.
 * Therefore, the only way that rcu_blocking_is_gp() can incorrectly return
 * that there is only one CPU when in fact there was more than one throughout
 * is when there were no RCU readers in the system.  If there are no
 * RCU readers, the grace period by definition can be of zero length,
 * regardless of the number of online CPUs.
 */
static inline int rcu_blocking_is_gp(void)
{
	might_sleep();  /* Check for RCU read-side critical section. */
	return num_online_cpus() <= 1;
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
	rcu_lockdep_assert(!lock_is_held(&rcu_bh_lock_map) &&
			   !lock_is_held(&rcu_lock_map) &&
			   !lock_is_held(&rcu_sched_lock_map),
			   "Illegal synchronize_sched() in RCU-sched read-side critical section");
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
	rcu_lockdep_assert(!lock_is_held(&rcu_bh_lock_map) &&
			   !lock_is_held(&rcu_lock_map) &&
			   !lock_is_held(&rcu_sched_lock_map),
			   "Illegal synchronize_rcu_bh() in RCU-bh read-side critical section");
	if (rcu_blocking_is_gp())
		return;
	wait_rcu_gp(call_rcu_bh);
}
EXPORT_SYMBOL_GPL(synchronize_rcu_bh);

static atomic_t sync_sched_expedited_started = ATOMIC_INIT(0);
static atomic_t sync_sched_expedited_done = ATOMIC_INIT(0);

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
	int firstsnap, s, snap, trycount = 0;

	/* Note that atomic_inc_return() implies full memory barrier. */
	firstsnap = snap = atomic_inc_return(&sync_sched_expedited_started);
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

		/* No joy, try again later.  Or just synchronize_sched(). */
		if (trycount++ < 10)
			udelay(trycount * num_online_cpus());
		else {
			synchronize_sched();
			return;
		}

		/* Check to see if someone else did our work for us. */
		s = atomic_read(&sync_sched_expedited_done);
		if (UINT_CMP_GE((unsigned)s, (unsigned)firstsnap)) {
			smp_mb(); /* ensure test happens before caller kfree */
			return;
		}

		/*
		 * Refetching sync_sched_expedited_started allows later
		 * callers to piggyback on our grace period.  We subtract
		 * 1 to get the same token that the last incrementer got.
		 * We retry after they started, so our grace period works
		 * for them, and they started after our first try, so their
		 * grace period works for us.
		 */
		get_online_cpus();
		snap = atomic_read(&sync_sched_expedited_started);
		smp_mb(); /* ensure read is before try_stop_cpus(). */
	}

	/*
	 * Everyone up to our most recent fetch is covered by our grace
	 * period.  Update the counter, but only if our work is still
	 * relevant -- which it won't be if someone who started later
	 * than we did beat us to the punch.
	 */
	do {
		s = atomic_read(&sync_sched_expedited_done);
		if (UINT_CMP_GE((unsigned)s, (unsigned)snap)) {
			smp_mb(); /* ensure test happens before caller kfree */
			break;
		}
	} while (atomic_cmpxchg(&sync_sched_expedited_done, s, snap) != s);

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

	/* Is the RCU core waiting for a quiescent state from this CPU? */
	if (rcu_scheduler_fully_active &&
	    rdp->qs_pending && !rdp->passed_quiesce) {

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
static int rcu_cpu_has_callbacks(int cpu)
{
	/* RCU callbacks either ready or pending? */
	return per_cpu(rcu_sched_data, cpu).nxtlist ||
	       per_cpu(rcu_bh_data, cpu).nxtlist ||
	       rcu_preempt_cpu_has_callbacks(cpu);
}

/*
 * RCU callback function for _rcu_barrier().  If we are last, wake
 * up the task executing _rcu_barrier().
 */
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
	int cpu;
	unsigned long flags;
	struct rcu_data *rdp;
	struct rcu_head rh;

	init_rcu_head_on_stack(&rh);

	/* Take mutex to serialize concurrent rcu_barrier() requests. */
	mutex_lock(&rcu_barrier_mutex);

	smp_mb();  /* Prevent any prior operations from leaking in. */

	/*
	 * Initialize the count to one rather than to zero in order to
	 * avoid a too-soon return to zero in case of a short grace period
	 * (or preemption of this task).  Also flag this task as doing
	 * an rcu_barrier().  This will prevent anyone else from adopting
	 * orphaned callbacks, which could cause otherwise failure if a
	 * CPU went offline and quickly came back online.  To see this,
	 * consider the following sequence of events:
	 *
	 * 1.	We cause CPU 0 to post an rcu_barrier_callback() callback.
	 * 2.	CPU 1 goes offline, orphaning its callbacks.
	 * 3.	CPU 0 adopts CPU 1's orphaned callbacks.
	 * 4.	CPU 1 comes back online.
	 * 5.	We cause CPU 1 to post an rcu_barrier_callback() callback.
	 * 6.	Both rcu_barrier_callback() callbacks are invoked, awakening
	 *	us -- but before CPU 1's orphaned callbacks are invoked!!!
	 */
	init_completion(&rcu_barrier_completion);
	atomic_set(&rcu_barrier_cpu_count, 1);
	raw_spin_lock_irqsave(&rsp->onofflock, flags);
	rsp->rcu_barrier_in_progress = current;
	raw_spin_unlock_irqrestore(&rsp->onofflock, flags);

	/*
	 * Force every CPU with callbacks to register a new callback
	 * that will tell us when all the preceding callbacks have
	 * been invoked.  If an offline CPU has callbacks, wait for
	 * it to either come back online or to finish orphaning those
	 * callbacks.
	 */
	for_each_possible_cpu(cpu) {
		preempt_disable();
		rdp = per_cpu_ptr(rsp->rda, cpu);
		if (cpu_is_offline(cpu)) {
			preempt_enable();
			while (cpu_is_offline(cpu) && ACCESS_ONCE(rdp->qlen))
				schedule_timeout_interruptible(1);
		} else if (ACCESS_ONCE(rdp->qlen)) {
			smp_call_function_single(cpu, rcu_barrier_func,
						 (void *)call_rcu_func, 1);
			preempt_enable();
		} else {
			preempt_enable();
		}
	}

	/*
	 * Now that all online CPUs have rcu_barrier_callback() callbacks
	 * posted, we can adopt all of the orphaned callbacks and place
	 * an rcu_barrier_callback() callback after them.  When that is done,
	 * we are guaranteed to have an rcu_barrier_callback() callback
	 * following every callback that could possibly have been
	 * registered before _rcu_barrier() was called.
	 */
	raw_spin_lock_irqsave(&rsp->onofflock, flags);
	rcu_adopt_orphan_cbs(rsp);
	rsp->rcu_barrier_in_progress = NULL;
	raw_spin_unlock_irqrestore(&rsp->onofflock, flags);
	atomic_inc(&rcu_barrier_cpu_count);
	smp_mb__after_atomic_inc(); /* Ensure atomic_inc() before callback. */
	call_rcu_func(&rh, rcu_barrier_callback);

	/*
	 * Now that we have an rcu_barrier_callback() callback on each
	 * CPU, and thus each counted, remove the initial count.
	 */
	if (atomic_dec_and_test(&rcu_barrier_cpu_count))
		complete(&rcu_barrier_completion);

	/* Wait for all rcu_barrier_callback() callbacks to be invoked. */
	wait_for_completion(&rcu_barrier_completion);

	/* Other rcu_barrier() invocations can now safely proceed. */
	mutex_unlock(&rcu_barrier_mutex);

	destroy_rcu_head_on_stack(&rh);
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
	rdp->qlen_lazy = 0;
	rdp->qlen = 0;
	rdp->dynticks = &per_cpu(rcu_dynticks, cpu);
	WARN_ON_ONCE(rdp->dynticks->dynticks_nesting != DYNTICK_TASK_EXIT_IDLE);
	WARN_ON_ONCE(atomic_read(&rdp->dynticks->dynticks) != 1);
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
	rdp->beenonline = 1;	 /* We have now been online. */
	rdp->preemptible = preemptible;
	rdp->qlen_last_fqs_check = 0;
	rdp->n_force_qs_snap = rsp->n_force_qs;
	rdp->blimit = blimit;
	rdp->dynticks->dynticks_nesting = DYNTICK_TASK_EXIT_IDLE;
	atomic_set(&rdp->dynticks->dynticks,
		   (atomic_read(&rdp->dynticks->dynticks) & ~0x1) + 1);
	rcu_prepare_for_idle_init(cpu);
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
			/*
			 * If there is a grace period in progress, we will
			 * set up to wait for it next time we run the
			 * RCU core code.
			 */
			rdp->gpnum = rnp->completed;
			rdp->completed = rnp->completed;
			rdp->passed_quiesce = 0;
			rdp->qs_pending = 0;
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
		rcu_cleanup_dying_cpu(&rcu_bh_state);
		rcu_cleanup_dying_cpu(&rcu_sched_state);
		rcu_preempt_cleanup_dying_cpu();
		rcu_cleanup_after_idle(cpu);
		break;
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
	case CPU_UP_CANCELED:
	case CPU_UP_CANCELED_FROZEN:
		rcu_cleanup_dead_cpu(cpu, &rcu_bh_state);
		rcu_cleanup_dead_cpu(cpu, &rcu_sched_state);
		rcu_preempt_cleanup_dead_cpu(cpu);
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

	for (i = rcu_num_lvls - 1; i > 0; i--)
		rsp->levelspread[i] = CONFIG_RCU_FANOUT;
	rsp->levelspread[0] = rcu_fanout_leaf;
}
#else /* #ifdef CONFIG_RCU_FANOUT_EXACT */
static void __init rcu_init_levelspread(struct rcu_state *rsp)
{
	int ccur;
	int cprv;
	int i;

	cprv = NR_CPUS;
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

	for (i = 0; i < rcu_num_lvls; i++)
		rsp->levelcnt[i] = num_rcu_lvl[i];
	for (i = 1; i < rcu_num_lvls; i++)
		rsp->level[i] = rsp->level[i - 1] + rsp->levelcnt[i - 1];
	rcu_init_levelspread(rsp);

	/* Initialize the elements themselves, starting from the leaves. */

	for (i = rcu_num_lvls - 1; i >= 0; i--) {
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
	rnp = rsp->level[rcu_num_lvls - 1];
	for_each_possible_cpu(i) {
		while (i > rnp->grphi)
			rnp++;
		per_cpu_ptr(rsp->rda, i)->mynode = rnp;
		rcu_boot_init_percpu_data(i, rsp);
	}
}

/*
 * Compute the rcu_node tree geometry from kernel parameters.  This cannot
 * replace the definitions in rcutree.h because those are needed to size
 * the ->node array in the rcu_state structure.
 */
static void __init rcu_init_geometry(void)
{
	int i;
	int j;
	int n = nr_cpu_ids;
	int rcu_capacity[MAX_RCU_LVLS + 1];

	/* If the compile-time values are accurate, just leave. */
	if (rcu_fanout_leaf == CONFIG_RCU_FANOUT_LEAF)
		return;

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
