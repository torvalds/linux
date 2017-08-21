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
#include <linux/stop_machine.h>
#include <linux/random.h>
#include <linux/trace_events.h>
#include <linux/suspend.h>
#include <linux/ftrace.h>

#include "tree.h"
#include "rcu.h"

#ifdef MODULE_PARAM_PREFIX
#undef MODULE_PARAM_PREFIX
#endif
#define MODULE_PARAM_PREFIX "rcutree."

/* Data structures. */

/*
 * In order to export the rcu_state name to the tracing tools, it
 * needs to be added in the __tracepoint_string section.
 * This requires defining a separate variable tp_<sname>_varname
 * that points to the string being used, and this will allow
 * the tracing userspace tools to be able to decipher the string
 * address to the matching string.
 */
#ifdef CONFIG_TRACING
# define DEFINE_RCU_TPS(sname) \
static char sname##_varname[] = #sname; \
static const char *tp_##sname##_varname __used __tracepoint_string = sname##_varname;
# define RCU_STATE_NAME(sname) sname##_varname
#else
# define DEFINE_RCU_TPS(sname)
# define RCU_STATE_NAME(sname) __stringify(sname)
#endif

#define RCU_STATE_INITIALIZER(sname, sabbr, cr) \
DEFINE_RCU_TPS(sname) \
static DEFINE_PER_CPU_SHARED_ALIGNED(struct rcu_data, sname##_data); \
struct rcu_state sname##_state = { \
	.level = { &sname##_state.node[0] }, \
	.rda = &sname##_data, \
	.call = cr, \
	.gp_state = RCU_GP_IDLE, \
	.gpnum = 0UL - 300UL, \
	.completed = 0UL - 300UL, \
	.barrier_mutex = __MUTEX_INITIALIZER(sname##_state.barrier_mutex), \
	.name = RCU_STATE_NAME(sname), \
	.abbr = sabbr, \
	.exp_mutex = __MUTEX_INITIALIZER(sname##_state.exp_mutex), \
	.exp_wake_mutex = __MUTEX_INITIALIZER(sname##_state.exp_wake_mutex), \
}

RCU_STATE_INITIALIZER(rcu_sched, 's', call_rcu_sched);
RCU_STATE_INITIALIZER(rcu_bh, 'b', call_rcu_bh);

static struct rcu_state *const rcu_state_p;
LIST_HEAD(rcu_struct_flavors);

/* Dump rcu_node combining tree at boot to verify correct setup. */
static bool dump_tree;
module_param(dump_tree, bool, 0444);
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
/* panic() on RCU Stall sysctl. */
int sysctl_panic_on_rcu_stall __read_mostly;

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

static void rcu_init_new_rnp(struct rcu_node *rnp_leaf);
static void rcu_cleanup_dead_rnp(struct rcu_node *rnp_leaf);
static void rcu_boost_kthread_setaffinity(struct rcu_node *rnp, int outgoingcpu);
static void invoke_rcu_core(void);
static void invoke_rcu_callbacks(struct rcu_state *rsp, struct rcu_data *rdp);
static void rcu_report_exp_rdp(struct rcu_state *rsp,
			       struct rcu_data *rdp, bool wake);
static void sync_sched_exp_online_cleanup(int cpu);

/* rcuc/rcub kthread realtime priority */
static int kthread_prio = IS_ENABLED(CONFIG_RCU_BOOST) ? 1 : 0;
module_param(kthread_prio, int, 0644);

/* Delay in jiffies for grace-period initialization delays, debug only. */

static int gp_preinit_delay;
module_param(gp_preinit_delay, int, 0444);
static int gp_init_delay;
module_param(gp_init_delay, int, 0444);
static int gp_cleanup_delay;
module_param(gp_cleanup_delay, int, 0444);

/*
 * Number of grace periods between delays, normalized by the duration of
 * the delay.  The longer the delay, the more the grace periods between
 * each delay.  The reason for this normalization is that it means that,
 * for non-zero delays, the overall slowdown of grace periods is constant
 * regardless of the duration of the delay.  This arrangement balances
 * the need for long delays to increase some race probabilities with the
 * need for fast grace periods to increase other race probabilities.
 */
#define PER_RCU_NODE_PERIOD 3	/* Number of grace periods between delays. */

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
 * Compute the mask of online CPUs for the specified rcu_node structure.
 * This will not be stable unless the rcu_node structure's ->lock is
 * held, but the bit corresponding to the current CPU will be stable
 * in most contexts.
 */
unsigned long rcu_rnp_online_cpus(struct rcu_node *rnp)
{
	return READ_ONCE(rnp->qsmaskinitnext);
}

/*
 * Return true if an RCU grace period is in progress.  The READ_ONCE()s
 * permit this function to be invoked without holding the root rcu_node
 * structure's ->lock, but of course results can be subject to change.
 */
static int rcu_gp_in_progress(struct rcu_state *rsp)
{
	return READ_ONCE(rsp->completed) != READ_ONCE(rsp->gpnum);
}

/*
 * Note a quiescent state.  Because we do not need to know
 * how many quiescent states passed, just if there was at least
 * one since the start of the grace period, this just sets a flag.
 * The caller must have disabled preemption.
 */
void rcu_sched_qs(void)
{
	RCU_LOCKDEP_WARN(preemptible(), "rcu_sched_qs() invoked with preemption enabled!!!");
	if (!__this_cpu_read(rcu_sched_data.cpu_no_qs.s))
		return;
	trace_rcu_grace_period(TPS("rcu_sched"),
			       __this_cpu_read(rcu_sched_data.gpnum),
			       TPS("cpuqs"));
	__this_cpu_write(rcu_sched_data.cpu_no_qs.b.norm, false);
	if (!__this_cpu_read(rcu_sched_data.cpu_no_qs.b.exp))
		return;
	__this_cpu_write(rcu_sched_data.cpu_no_qs.b.exp, false);
	rcu_report_exp_rdp(&rcu_sched_state,
			   this_cpu_ptr(&rcu_sched_data), true);
}

void rcu_bh_qs(void)
{
	RCU_LOCKDEP_WARN(preemptible(), "rcu_bh_qs() invoked with preemption enabled!!!");
	if (__this_cpu_read(rcu_bh_data.cpu_no_qs.s)) {
		trace_rcu_grace_period(TPS("rcu_bh"),
				       __this_cpu_read(rcu_bh_data.gpnum),
				       TPS("cpuqs"));
		__this_cpu_write(rcu_bh_data.cpu_no_qs.b.norm, false);
	}
}

/*
 * Steal a bit from the bottom of ->dynticks for idle entry/exit
 * control.  Initially this is for TLB flushing.
 */
#define RCU_DYNTICK_CTRL_MASK 0x1
#define RCU_DYNTICK_CTRL_CTR  (RCU_DYNTICK_CTRL_MASK + 1)
#ifndef rcu_eqs_special_exit
#define rcu_eqs_special_exit() do { } while (0)
#endif

static DEFINE_PER_CPU(struct rcu_dynticks, rcu_dynticks) = {
	.dynticks_nesting = DYNTICK_TASK_EXIT_IDLE,
	.dynticks = ATOMIC_INIT(RCU_DYNTICK_CTRL_CTR),
};

/*
 * There's a few places, currently just in the tracing infrastructure,
 * that uses rcu_irq_enter() to make sure RCU is watching. But there's
 * a small location where that will not even work. In those cases
 * rcu_irq_enter_disabled() needs to be checked to make sure rcu_irq_enter()
 * can be called.
 */
static DEFINE_PER_CPU(bool, disable_rcu_irq_enter);

bool rcu_irq_enter_disabled(void)
{
	return this_cpu_read(disable_rcu_irq_enter);
}

/*
 * Record entry into an extended quiescent state.  This is only to be
 * called when not already in an extended quiescent state.
 */
static void rcu_dynticks_eqs_enter(void)
{
	struct rcu_dynticks *rdtp = this_cpu_ptr(&rcu_dynticks);
	int seq;

	/*
	 * CPUs seeing atomic_add_return() must see prior RCU read-side
	 * critical sections, and we also must force ordering with the
	 * next idle sojourn.
	 */
	seq = atomic_add_return(RCU_DYNTICK_CTRL_CTR, &rdtp->dynticks);
	/* Better be in an extended quiescent state! */
	WARN_ON_ONCE(IS_ENABLED(CONFIG_RCU_EQS_DEBUG) &&
		     (seq & RCU_DYNTICK_CTRL_CTR));
	/* Better not have special action (TLB flush) pending! */
	WARN_ON_ONCE(IS_ENABLED(CONFIG_RCU_EQS_DEBUG) &&
		     (seq & RCU_DYNTICK_CTRL_MASK));
}

/*
 * Record exit from an extended quiescent state.  This is only to be
 * called from an extended quiescent state.
 */
static void rcu_dynticks_eqs_exit(void)
{
	struct rcu_dynticks *rdtp = this_cpu_ptr(&rcu_dynticks);
	int seq;

	/*
	 * CPUs seeing atomic_add_return() must see prior idle sojourns,
	 * and we also must force ordering with the next RCU read-side
	 * critical section.
	 */
	seq = atomic_add_return(RCU_DYNTICK_CTRL_CTR, &rdtp->dynticks);
	WARN_ON_ONCE(IS_ENABLED(CONFIG_RCU_EQS_DEBUG) &&
		     !(seq & RCU_DYNTICK_CTRL_CTR));
	if (seq & RCU_DYNTICK_CTRL_MASK) {
		atomic_andnot(RCU_DYNTICK_CTRL_MASK, &rdtp->dynticks);
		smp_mb__after_atomic(); /* _exit after clearing mask. */
		/* Prefer duplicate flushes to losing a flush. */
		rcu_eqs_special_exit();
	}
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
	struct rcu_dynticks *rdtp = this_cpu_ptr(&rcu_dynticks);

	if (atomic_read(&rdtp->dynticks) & RCU_DYNTICK_CTRL_CTR)
		return;
	atomic_add(RCU_DYNTICK_CTRL_CTR, &rdtp->dynticks);
}

/*
 * Is the current CPU in an extended quiescent state?
 *
 * No ordering, as we are sampling CPU-local information.
 */
bool rcu_dynticks_curr_cpu_in_eqs(void)
{
	struct rcu_dynticks *rdtp = this_cpu_ptr(&rcu_dynticks);

	return !(atomic_read(&rdtp->dynticks) & RCU_DYNTICK_CTRL_CTR);
}

/*
 * Snapshot the ->dynticks counter with full ordering so as to allow
 * stable comparison of this counter with past and future snapshots.
 */
int rcu_dynticks_snap(struct rcu_dynticks *rdtp)
{
	int snap = atomic_add_return(0, &rdtp->dynticks);

	return snap & ~RCU_DYNTICK_CTRL_MASK;
}

/*
 * Return true if the snapshot returned from rcu_dynticks_snap()
 * indicates that RCU is in an extended quiescent state.
 */
static bool rcu_dynticks_in_eqs(int snap)
{
	return !(snap & RCU_DYNTICK_CTRL_CTR);
}

/*
 * Return true if the CPU corresponding to the specified rcu_dynticks
 * structure has spent some time in an extended quiescent state since
 * rcu_dynticks_snap() returned the specified snapshot.
 */
static bool rcu_dynticks_in_eqs_since(struct rcu_dynticks *rdtp, int snap)
{
	return snap != rcu_dynticks_snap(rdtp);
}

/*
 * Do a double-increment of the ->dynticks counter to emulate a
 * momentary idle-CPU quiescent state.
 */
static void rcu_dynticks_momentary_idle(void)
{
	struct rcu_dynticks *rdtp = this_cpu_ptr(&rcu_dynticks);
	int special = atomic_add_return(2 * RCU_DYNTICK_CTRL_CTR,
					&rdtp->dynticks);

	/* It is illegal to call this from idle state. */
	WARN_ON_ONCE(!(special & RCU_DYNTICK_CTRL_CTR));
}

/*
 * Set the special (bottom) bit of the specified CPU so that it
 * will take special action (such as flushing its TLB) on the
 * next exit from an extended quiescent state.  Returns true if
 * the bit was successfully set, or false if the CPU was not in
 * an extended quiescent state.
 */
bool rcu_eqs_special_set(int cpu)
{
	int old;
	int new;
	struct rcu_dynticks *rdtp = &per_cpu(rcu_dynticks, cpu);

	do {
		old = atomic_read(&rdtp->dynticks);
		if (old & RCU_DYNTICK_CTRL_CTR)
			return false;
		new = old | RCU_DYNTICK_CTRL_MASK;
	} while (atomic_cmpxchg(&rdtp->dynticks, old, new) != old);
	return true;
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
 * The caller must have disabled interrupts.
 */
static void rcu_momentary_dyntick_idle(void)
{
	raw_cpu_write(rcu_dynticks.rcu_need_heavy_qs, false);
	rcu_dynticks_momentary_idle();
}

/*
 * Note a context switch.  This is a quiescent state for RCU-sched,
 * and requires special handling for preemptible RCU.
 * The caller must have disabled interrupts.
 */
void rcu_note_context_switch(bool preempt)
{
	barrier(); /* Avoid RCU read-side critical sections leaking down. */
	trace_rcu_utilization(TPS("Start context switch"));
	rcu_sched_qs();
	rcu_preempt_note_context_switch(preempt);
	/* Load rcu_urgent_qs before other flags. */
	if (!smp_load_acquire(this_cpu_ptr(&rcu_dynticks.rcu_urgent_qs)))
		goto out;
	this_cpu_write(rcu_dynticks.rcu_urgent_qs, false);
	if (unlikely(raw_cpu_read(rcu_dynticks.rcu_need_heavy_qs)))
		rcu_momentary_dyntick_idle();
	this_cpu_inc(rcu_dynticks.rcu_qs_ctr);
	if (!preempt)
		rcu_note_voluntary_context_switch_lite(current);
out:
	trace_rcu_utilization(TPS("End context switch"));
	barrier(); /* Avoid RCU read-side critical sections leaking up. */
}
EXPORT_SYMBOL_GPL(rcu_note_context_switch);

/*
 * Register a quiescent state for all RCU flavors.  If there is an
 * emergency, invoke rcu_momentary_dyntick_idle() to do a heavy-weight
 * dyntick-idle quiescent state visible to other CPUs (but only for those
 * RCU flavors in desperate need of a quiescent state, which will normally
 * be none of them).  Either way, do a lightweight quiescent state for
 * all RCU flavors.
 *
 * The barrier() calls are redundant in the common case when this is
 * called externally, but just in case this is called from within this
 * file.
 *
 */
void rcu_all_qs(void)
{
	unsigned long flags;

	if (!raw_cpu_read(rcu_dynticks.rcu_urgent_qs))
		return;
	preempt_disable();
	/* Load rcu_urgent_qs before other flags. */
	if (!smp_load_acquire(this_cpu_ptr(&rcu_dynticks.rcu_urgent_qs))) {
		preempt_enable();
		return;
	}
	this_cpu_write(rcu_dynticks.rcu_urgent_qs, false);
	barrier(); /* Avoid RCU read-side critical sections leaking down. */
	if (unlikely(raw_cpu_read(rcu_dynticks.rcu_need_heavy_qs))) {
		local_irq_save(flags);
		rcu_momentary_dyntick_idle();
		local_irq_restore(flags);
	}
	if (unlikely(raw_cpu_read(rcu_sched_data.cpu_no_qs.b.exp)))
		rcu_sched_qs();
	this_cpu_inc(rcu_dynticks.rcu_qs_ctr);
	barrier(); /* Avoid RCU read-side critical sections leaking up. */
	preempt_enable();
}
EXPORT_SYMBOL_GPL(rcu_all_qs);

#define DEFAULT_RCU_BLIMIT 10     /* Maximum callbacks per rcu_do_batch. */
static long blimit = DEFAULT_RCU_BLIMIT;
#define DEFAULT_RCU_QHIMARK 10000 /* If this many pending, ignore blimit. */
static long qhimark = DEFAULT_RCU_QHIMARK;
#define DEFAULT_RCU_QLOMARK 100   /* Once only this many pending, use blimit. */
static long qlowmark = DEFAULT_RCU_QLOMARK;

module_param(blimit, long, 0444);
module_param(qhimark, long, 0444);
module_param(qlowmark, long, 0444);

static ulong jiffies_till_first_fqs = ULONG_MAX;
static ulong jiffies_till_next_fqs = ULONG_MAX;
static bool rcu_kick_kthreads;

module_param(jiffies_till_first_fqs, ulong, 0644);
module_param(jiffies_till_next_fqs, ulong, 0644);
module_param(rcu_kick_kthreads, bool, 0644);

/*
 * How long the grace period must be before we start recruiting
 * quiescent-state help from rcu_note_context_switch().
 */
static ulong jiffies_till_sched_qs = HZ / 20;
module_param(jiffies_till_sched_qs, ulong, 0644);

static bool rcu_start_gp_advanced(struct rcu_state *rsp, struct rcu_node *rnp,
				  struct rcu_data *rdp);
static void force_qs_rnp(struct rcu_state *rsp, int (*f)(struct rcu_data *rsp));
static void force_quiescent_state(struct rcu_state *rsp);
static int rcu_pending(void);

/*
 * Return the number of RCU batches started thus far for debug & stats.
 */
unsigned long rcu_batches_started(void)
{
	return rcu_state_p->gpnum;
}
EXPORT_SYMBOL_GPL(rcu_batches_started);

/*
 * Return the number of RCU-sched batches started thus far for debug & stats.
 */
unsigned long rcu_batches_started_sched(void)
{
	return rcu_sched_state.gpnum;
}
EXPORT_SYMBOL_GPL(rcu_batches_started_sched);

/*
 * Return the number of RCU BH batches started thus far for debug & stats.
 */
unsigned long rcu_batches_started_bh(void)
{
	return rcu_bh_state.gpnum;
}
EXPORT_SYMBOL_GPL(rcu_batches_started_bh);

/*
 * Return the number of RCU batches completed thus far for debug & stats.
 */
unsigned long rcu_batches_completed(void)
{
	return rcu_state_p->completed;
}
EXPORT_SYMBOL_GPL(rcu_batches_completed);

/*
 * Return the number of RCU-sched batches completed thus far for debug & stats.
 */
unsigned long rcu_batches_completed_sched(void)
{
	return rcu_sched_state.completed;
}
EXPORT_SYMBOL_GPL(rcu_batches_completed_sched);

/*
 * Return the number of RCU BH batches completed thus far for debug & stats.
 */
unsigned long rcu_batches_completed_bh(void)
{
	return rcu_bh_state.completed;
}
EXPORT_SYMBOL_GPL(rcu_batches_completed_bh);

/*
 * Return the number of RCU expedited batches completed thus far for
 * debug & stats.  Odd numbers mean that a batch is in progress, even
 * numbers mean idle.  The value returned will thus be roughly double
 * the cumulative batches since boot.
 */
unsigned long rcu_exp_batches_completed(void)
{
	return rcu_state_p->expedited_sequence;
}
EXPORT_SYMBOL_GPL(rcu_exp_batches_completed);

/*
 * Return the number of RCU-sched expedited batches completed thus far
 * for debug & stats.  Similar to rcu_exp_batches_completed().
 */
unsigned long rcu_exp_batches_completed_sched(void)
{
	return rcu_sched_state.expedited_sequence;
}
EXPORT_SYMBOL_GPL(rcu_exp_batches_completed_sched);

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
 * Force a quiescent state for RCU-sched.
 */
void rcu_sched_force_quiescent_state(void)
{
	force_quiescent_state(&rcu_sched_state);
}
EXPORT_SYMBOL_GPL(rcu_sched_force_quiescent_state);

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
	if (rsp == NULL)
		return;
	*flags = READ_ONCE(rsp->gp_flags);
	*gpnum = READ_ONCE(rsp->gpnum);
	*completed = READ_ONCE(rsp->completed);
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
	int idx = (READ_ONCE(rnp->completed) + 1) & 0x1;
	int *fp = &rnp->need_future_gp[idx];

	RCU_LOCKDEP_WARN(!irqs_disabled(), "rcu_future_needs_gp() invoked with irqs enabled!!!");
	return READ_ONCE(*fp);
}

/*
 * Does the current CPU require a not-yet-started grace period?
 * The caller must have disabled interrupts to prevent races with
 * normal callback registry.
 */
static bool
cpu_needs_another_gp(struct rcu_state *rsp, struct rcu_data *rdp)
{
	RCU_LOCKDEP_WARN(!irqs_disabled(), "cpu_needs_another_gp() invoked with irqs enabled!!!");
	if (rcu_gp_in_progress(rsp))
		return false;  /* No, a grace period is already in progress. */
	if (rcu_future_needs_gp(rsp))
		return true;  /* Yes, a no-CBs CPU needs one. */
	if (!rcu_segcblist_is_enabled(&rdp->cblist))
		return false;  /* No, this is a no-CBs (or offline) CPU. */
	if (!rcu_segcblist_restempty(&rdp->cblist, RCU_NEXT_READY_TAIL))
		return true;  /* Yes, CPU has newly registered callbacks. */
	if (rcu_segcblist_future_gp_needed(&rdp->cblist,
					   READ_ONCE(rsp->completed)))
		return true;  /* Yes, CBs for future grace period. */
	return false; /* No grace period needed. */
}

/*
 * rcu_eqs_enter_common - current CPU is entering an extended quiescent state
 *
 * Enter idle, doing appropriate accounting.  The caller must have
 * disabled interrupts.
 */
static void rcu_eqs_enter_common(bool user)
{
	struct rcu_state *rsp;
	struct rcu_data *rdp;
	struct rcu_dynticks *rdtp = this_cpu_ptr(&rcu_dynticks);

	RCU_LOCKDEP_WARN(!irqs_disabled(), "rcu_eqs_enter_common() invoked with irqs enabled!!!");
	trace_rcu_dyntick(TPS("Start"), rdtp->dynticks_nesting, 0);
	if (IS_ENABLED(CONFIG_RCU_EQS_DEBUG) &&
	    !user && !is_idle_task(current)) {
		struct task_struct *idle __maybe_unused =
			idle_task(smp_processor_id());

		trace_rcu_dyntick(TPS("Error on entry: not idle task"), rdtp->dynticks_nesting, 0);
		rcu_ftrace_dump(DUMP_ORIG);
		WARN_ONCE(1, "Current pid: %d comm: %s / Idle pid: %d comm: %s",
			  current->pid, current->comm,
			  idle->pid, idle->comm); /* must be idle task! */
	}
	for_each_rcu_flavor(rsp) {
		rdp = this_cpu_ptr(rsp->rda);
		do_nocb_deferred_wakeup(rdp);
	}
	rcu_prepare_for_idle();
	__this_cpu_inc(disable_rcu_irq_enter);
	rdtp->dynticks_nesting = 0; /* Breaks tracing momentarily. */
	rcu_dynticks_eqs_enter(); /* After this, tracing works again. */
	__this_cpu_dec(disable_rcu_irq_enter);
	rcu_dynticks_task_enter();

	/*
	 * It is illegal to enter an extended quiescent state while
	 * in an RCU read-side critical section.
	 */
	RCU_LOCKDEP_WARN(lock_is_held(&rcu_lock_map),
			 "Illegal idle entry in RCU read-side critical section.");
	RCU_LOCKDEP_WARN(lock_is_held(&rcu_bh_lock_map),
			 "Illegal idle entry in RCU-bh read-side critical section.");
	RCU_LOCKDEP_WARN(lock_is_held(&rcu_sched_lock_map),
			 "Illegal idle entry in RCU-sched read-side critical section.");
}

/*
 * Enter an RCU extended quiescent state, which can be either the
 * idle loop or adaptive-tickless usermode execution.
 */
static void rcu_eqs_enter(bool user)
{
	struct rcu_dynticks *rdtp;

	rdtp = this_cpu_ptr(&rcu_dynticks);
	WARN_ON_ONCE(IS_ENABLED(CONFIG_RCU_EQS_DEBUG) &&
		     (rdtp->dynticks_nesting & DYNTICK_TASK_NEST_MASK) == 0);
	if ((rdtp->dynticks_nesting & DYNTICK_TASK_NEST_MASK) == DYNTICK_TASK_NEST_VALUE)
		rcu_eqs_enter_common(user);
	else
		rdtp->dynticks_nesting -= DYNTICK_TASK_NEST_VALUE;
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
	RCU_LOCKDEP_WARN(!irqs_disabled(), "rcu_idle_enter() invoked with irqs enabled!!!");
	rcu_eqs_enter(false);
}

#ifdef CONFIG_NO_HZ_FULL
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
	RCU_LOCKDEP_WARN(!irqs_disabled(), "rcu_user_enter() invoked with irqs enabled!!!");
	rcu_eqs_enter(true);
}
#endif /* CONFIG_NO_HZ_FULL */

/**
 * rcu_irq_exit - inform RCU that current CPU is exiting irq towards idle
 *
 * Exit from an interrupt handler, which might possibly result in entering
 * idle mode, in other words, leaving the mode in which read-side critical
 * sections can occur.  The caller must have disabled interrupts.
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
	struct rcu_dynticks *rdtp;

	RCU_LOCKDEP_WARN(!irqs_disabled(), "rcu_irq_exit() invoked with irqs enabled!!!");
	rdtp = this_cpu_ptr(&rcu_dynticks);
	WARN_ON_ONCE(IS_ENABLED(CONFIG_RCU_EQS_DEBUG) &&
		     rdtp->dynticks_nesting < 1);
	if (rdtp->dynticks_nesting <= 1) {
		rcu_eqs_enter_common(true);
	} else {
		trace_rcu_dyntick(TPS("--="), rdtp->dynticks_nesting, rdtp->dynticks_nesting - 1);
		rdtp->dynticks_nesting--;
	}
}

/*
 * Wrapper for rcu_irq_exit() where interrupts are enabled.
 */
void rcu_irq_exit_irqson(void)
{
	unsigned long flags;

	local_irq_save(flags);
	rcu_irq_exit();
	local_irq_restore(flags);
}

/*
 * rcu_eqs_exit_common - current CPU moving away from extended quiescent state
 *
 * If the new value of the ->dynticks_nesting counter was previously zero,
 * we really have exited idle, and must do the appropriate accounting.
 * The caller must have disabled interrupts.
 */
static void rcu_eqs_exit_common(long long oldval, int user)
{
	RCU_TRACE(struct rcu_dynticks *rdtp = this_cpu_ptr(&rcu_dynticks);)

	rcu_dynticks_task_exit();
	rcu_dynticks_eqs_exit();
	rcu_cleanup_after_idle();
	trace_rcu_dyntick(TPS("End"), oldval, rdtp->dynticks_nesting);
	if (IS_ENABLED(CONFIG_RCU_EQS_DEBUG) &&
	    !user && !is_idle_task(current)) {
		struct task_struct *idle __maybe_unused =
			idle_task(smp_processor_id());

		trace_rcu_dyntick(TPS("Error on exit: not idle task"),
				  oldval, rdtp->dynticks_nesting);
		rcu_ftrace_dump(DUMP_ORIG);
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

	RCU_LOCKDEP_WARN(!irqs_disabled(), "rcu_eqs_exit() invoked with irqs enabled!!!");
	rdtp = this_cpu_ptr(&rcu_dynticks);
	oldval = rdtp->dynticks_nesting;
	WARN_ON_ONCE(IS_ENABLED(CONFIG_RCU_EQS_DEBUG) && oldval < 0);
	if (oldval & DYNTICK_TASK_NEST_MASK) {
		rdtp->dynticks_nesting += DYNTICK_TASK_NEST_VALUE;
	} else {
		__this_cpu_inc(disable_rcu_irq_enter);
		rdtp->dynticks_nesting = DYNTICK_TASK_EXIT_IDLE;
		rcu_eqs_exit_common(oldval, user);
		__this_cpu_dec(disable_rcu_irq_enter);
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
	local_irq_restore(flags);
}

#ifdef CONFIG_NO_HZ_FULL
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
#endif /* CONFIG_NO_HZ_FULL */

/**
 * rcu_irq_enter - inform RCU that current CPU is entering irq away from idle
 *
 * Enter an interrupt handler, which might possibly result in exiting
 * idle mode, in other words, entering the mode in which read-side critical
 * sections can occur.  The caller must have disabled interrupts.
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
	struct rcu_dynticks *rdtp;
	long long oldval;

	RCU_LOCKDEP_WARN(!irqs_disabled(), "rcu_irq_enter() invoked with irqs enabled!!!");
	rdtp = this_cpu_ptr(&rcu_dynticks);
	oldval = rdtp->dynticks_nesting;
	rdtp->dynticks_nesting++;
	WARN_ON_ONCE(IS_ENABLED(CONFIG_RCU_EQS_DEBUG) &&
		     rdtp->dynticks_nesting == 0);
	if (oldval)
		trace_rcu_dyntick(TPS("++="), oldval, rdtp->dynticks_nesting);
	else
		rcu_eqs_exit_common(oldval, true);
}

/*
 * Wrapper for rcu_irq_enter() where interrupts are enabled.
 */
void rcu_irq_enter_irqson(void)
{
	unsigned long flags;

	local_irq_save(flags);
	rcu_irq_enter();
	local_irq_restore(flags);
}

/**
 * rcu_nmi_enter - inform RCU of entry to NMI context
 *
 * If the CPU was idle from RCU's viewpoint, update rdtp->dynticks and
 * rdtp->dynticks_nmi_nesting to let the RCU grace-period handling know
 * that the CPU is active.  This implementation permits nested NMIs, as
 * long as the nesting level does not overflow an int.  (You will probably
 * run out of stack space first.)
 */
void rcu_nmi_enter(void)
{
	struct rcu_dynticks *rdtp = this_cpu_ptr(&rcu_dynticks);
	int incby = 2;

	/* Complain about underflow. */
	WARN_ON_ONCE(rdtp->dynticks_nmi_nesting < 0);

	/*
	 * If idle from RCU viewpoint, atomically increment ->dynticks
	 * to mark non-idle and increment ->dynticks_nmi_nesting by one.
	 * Otherwise, increment ->dynticks_nmi_nesting by two.  This means
	 * if ->dynticks_nmi_nesting is equal to one, we are guaranteed
	 * to be in the outermost NMI handler that interrupted an RCU-idle
	 * period (observation due to Andy Lutomirski).
	 */
	if (rcu_dynticks_curr_cpu_in_eqs()) {
		rcu_dynticks_eqs_exit();
		incby = 1;
	}
	rdtp->dynticks_nmi_nesting += incby;
	barrier();
}

/**
 * rcu_nmi_exit - inform RCU of exit from NMI context
 *
 * If we are returning from the outermost NMI handler that interrupted an
 * RCU-idle period, update rdtp->dynticks and rdtp->dynticks_nmi_nesting
 * to let the RCU grace-period handling know that the CPU is back to
 * being RCU-idle.
 */
void rcu_nmi_exit(void)
{
	struct rcu_dynticks *rdtp = this_cpu_ptr(&rcu_dynticks);

	/*
	 * Check for ->dynticks_nmi_nesting underflow and bad ->dynticks.
	 * (We are exiting an NMI handler, so RCU better be paying attention
	 * to us!)
	 */
	WARN_ON_ONCE(rdtp->dynticks_nmi_nesting <= 0);
	WARN_ON_ONCE(rcu_dynticks_curr_cpu_in_eqs());

	/*
	 * If the nesting level is not 1, the CPU wasn't RCU-idle, so
	 * leave it in non-RCU-idle state.
	 */
	if (rdtp->dynticks_nmi_nesting != 1) {
		rdtp->dynticks_nmi_nesting -= 2;
		return;
	}

	/* This NMI interrupted an RCU-idle CPU, restore RCU-idleness. */
	rdtp->dynticks_nmi_nesting = 0;
	rcu_dynticks_eqs_enter();
}

/**
 * rcu_is_watching - see if RCU thinks that the current CPU is idle
 *
 * Return true if RCU is watching the running CPU, which means that this
 * CPU can safely enter RCU read-side critical sections.  In other words,
 * if the current CPU is in its idle loop and is neither in an interrupt
 * or NMI handler, return true.
 */
bool notrace rcu_is_watching(void)
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
	smp_store_release(per_cpu_ptr(&rcu_dynticks.rcu_urgent_qs, cpu), true);
}

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
 * the fact that a CPU enters the scheduler after completing the teardown
 * of the CPU.
 *
 * This is also why RCU internally marks CPUs online during in the
 * preparation phase and offline after the CPU has been taken down.
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
	ret = (rdp->grpmask & rcu_rnp_online_cpus(rnp)) ||
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
static int dyntick_save_progress_counter(struct rcu_data *rdp)
{
	rdp->dynticks_snap = rcu_dynticks_snap(rdp->dynticks);
	if (rcu_dynticks_in_eqs(rdp->dynticks_snap)) {
		trace_rcu_fqs(rdp->rsp->name, rdp->gpnum, rdp->cpu, TPS("dti"));
		if (ULONG_CMP_LT(READ_ONCE(rdp->gpnum) + ULONG_MAX / 4,
				 rdp->mynode->gpnum))
			WRITE_ONCE(rdp->gpwrap, true);
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
	bool *rnhqp;
	bool *ruqp;
	unsigned long rjtsc;
	struct rcu_node *rnp;

	/*
	 * If the CPU passed through or entered a dynticks idle phase with
	 * no active irq/NMI handlers, then we can safely pretend that the CPU
	 * already acknowledged the request to pass through a quiescent
	 * state.  Either way, that CPU cannot possibly be in an RCU
	 * read-side critical section that started before the beginning
	 * of the current RCU grace period.
	 */
	if (rcu_dynticks_in_eqs_since(rdp->dynticks, rdp->dynticks_snap)) {
		trace_rcu_fqs(rdp->rsp->name, rdp->gpnum, rdp->cpu, TPS("dti"));
		rdp->dynticks_fqs++;
		return 1;
	}

	/* Compute and saturate jiffies_till_sched_qs. */
	jtsq = jiffies_till_sched_qs;
	rjtsc = rcu_jiffies_till_stall_check();
	if (jtsq > rjtsc / 2) {
		WRITE_ONCE(jiffies_till_sched_qs, rjtsc);
		jtsq = rjtsc / 2;
	} else if (jtsq < 1) {
		WRITE_ONCE(jiffies_till_sched_qs, 1);
		jtsq = 1;
	}

	/*
	 * Has this CPU encountered a cond_resched_rcu_qs() since the
	 * beginning of the grace period?  For this to be the case,
	 * the CPU has to have noticed the current grace period.  This
	 * might not be the case for nohz_full CPUs looping in the kernel.
	 */
	rnp = rdp->mynode;
	ruqp = per_cpu_ptr(&rcu_dynticks.rcu_urgent_qs, rdp->cpu);
	if (time_after(jiffies, rdp->rsp->gp_start + jtsq) &&
	    READ_ONCE(rdp->rcu_qs_ctr_snap) != per_cpu(rcu_dynticks.rcu_qs_ctr, rdp->cpu) &&
	    READ_ONCE(rdp->gpnum) == rnp->gpnum && !rdp->gpwrap) {
		trace_rcu_fqs(rdp->rsp->name, rdp->gpnum, rdp->cpu, TPS("rqc"));
		return 1;
	} else {
		/* Load rcu_qs_ctr before store to rcu_urgent_qs. */
		smp_store_release(ruqp, true);
	}

	/* Check for the CPU being offline. */
	if (!(rdp->grpmask & rcu_rnp_online_cpus(rnp))) {
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
	 * rcu_need_heavy_qs variable are safe.  Yes, setting of
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
	rnhqp = &per_cpu(rcu_dynticks.rcu_need_heavy_qs, rdp->cpu);
	if (!READ_ONCE(*rnhqp) &&
	    (time_after(jiffies, rdp->rsp->gp_start + jtsq) ||
	     time_after(jiffies, rdp->rsp->jiffies_resched))) {
		WRITE_ONCE(*rnhqp, true);
		/* Store rcu_need_heavy_qs before rcu_urgent_qs. */
		smp_store_release(ruqp, true);
		rdp->rsp->jiffies_resched += 5; /* Re-enable beating. */
	}

	/*
	 * If more than halfway to RCU CPU stall-warning time, do
	 * a resched_cpu() to try to loosen things up a bit.
	 */
	if (jiffies - rdp->rsp->gp_start > rcu_jiffies_till_stall_check() / 2)
		resched_cpu(rdp->cpu);

	return 0;
}

static void record_gp_stall_check_time(struct rcu_state *rsp)
{
	unsigned long j = jiffies;
	unsigned long j1;

	rsp->gp_start = j;
	smp_wmb(); /* Record start time before stall time. */
	j1 = rcu_jiffies_till_stall_check();
	WRITE_ONCE(rsp->jiffies_stall, j + j1);
	rsp->jiffies_resched = j + j1 / 2;
	rsp->n_force_qs_gpstart = READ_ONCE(rsp->n_force_qs);
}

/*
 * Convert a ->gp_state value to a character string.
 */
static const char *gp_state_getname(short gs)
{
	if (gs < 0 || gs >= ARRAY_SIZE(gp_state_names))
		return "???";
	return gp_state_names[gs];
}

/*
 * Complain about starvation of grace-period kthread.
 */
static void rcu_check_gp_kthread_starvation(struct rcu_state *rsp)
{
	unsigned long gpa;
	unsigned long j;

	j = jiffies;
	gpa = READ_ONCE(rsp->gp_activity);
	if (j - gpa > 2 * HZ) {
		pr_err("%s kthread starved for %ld jiffies! g%lu c%lu f%#x %s(%d) ->state=%#lx ->cpu=%d\n",
		       rsp->name, j - gpa,
		       rsp->gpnum, rsp->completed,
		       rsp->gp_flags,
		       gp_state_getname(rsp->gp_state), rsp->gp_state,
		       rsp->gp_kthread ? rsp->gp_kthread->state : ~0,
		       rsp->gp_kthread ? task_cpu(rsp->gp_kthread) : -1);
		if (rsp->gp_kthread) {
			sched_show_task(rsp->gp_kthread);
			wake_up_process(rsp->gp_kthread);
		}
	}
}

/*
 * Dump stacks of all tasks running on stalled CPUs.  First try using
 * NMIs, but fall back to manual remote stack tracing on architectures
 * that don't support NMI-based stack dumps.  The NMI-triggered stack
 * traces are more accurate because they are printed by the target CPU.
 */
static void rcu_dump_cpu_stacks(struct rcu_state *rsp)
{
	int cpu;
	unsigned long flags;
	struct rcu_node *rnp;

	rcu_for_each_leaf_node(rsp, rnp) {
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
static void rcu_stall_kick_kthreads(struct rcu_state *rsp)
{
	unsigned long j;

	if (!rcu_kick_kthreads)
		return;
	j = READ_ONCE(rsp->jiffies_kick_kthreads);
	if (time_after(jiffies, j) && rsp->gp_kthread &&
	    (rcu_gp_in_progress(rsp) || READ_ONCE(rsp->gp_flags))) {
		WARN_ONCE(1, "Kicking %s grace-period kthread\n", rsp->name);
		rcu_ftrace_dump(DUMP_ALL);
		wake_up_process(rsp->gp_kthread);
		WRITE_ONCE(rsp->jiffies_kick_kthreads, j + HZ);
	}
}

static inline void panic_on_rcu_stall(void)
{
	if (sysctl_panic_on_rcu_stall)
		panic("RCU Stall\n");
}

static void print_other_cpu_stall(struct rcu_state *rsp, unsigned long gpnum)
{
	int cpu;
	long delta;
	unsigned long flags;
	unsigned long gpa;
	unsigned long j;
	int ndetected = 0;
	struct rcu_node *rnp = rcu_get_root(rsp);
	long totqlen = 0;

	/* Kick and suppress, if so configured. */
	rcu_stall_kick_kthreads(rsp);
	if (rcu_cpu_stall_suppress)
		return;

	/* Only let one CPU complain about others per time interval. */

	raw_spin_lock_irqsave_rcu_node(rnp, flags);
	delta = jiffies - READ_ONCE(rsp->jiffies_stall);
	if (delta < RCU_STALL_RAT_DELAY || !rcu_gp_in_progress(rsp)) {
		raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
		return;
	}
	WRITE_ONCE(rsp->jiffies_stall,
		   jiffies + 3 * rcu_jiffies_till_stall_check() + 3);
	raw_spin_unlock_irqrestore_rcu_node(rnp, flags);

	/*
	 * OK, time to rat on our buddy...
	 * See Documentation/RCU/stallwarn.txt for info on how to debug
	 * RCU CPU stall warnings.
	 */
	pr_err("INFO: %s detected stalls on CPUs/tasks:",
	       rsp->name);
	print_cpu_stall_info_begin();
	rcu_for_each_leaf_node(rsp, rnp) {
		raw_spin_lock_irqsave_rcu_node(rnp, flags);
		ndetected += rcu_print_task_stall(rnp);
		if (rnp->qsmask != 0) {
			for_each_leaf_node_possible_cpu(rnp, cpu)
				if (rnp->qsmask & leaf_node_cpu_bit(rnp, cpu)) {
					print_cpu_stall_info(rsp, cpu);
					ndetected++;
				}
		}
		raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
	}

	print_cpu_stall_info_end();
	for_each_possible_cpu(cpu)
		totqlen += rcu_segcblist_n_cbs(&per_cpu_ptr(rsp->rda,
							    cpu)->cblist);
	pr_cont("(detected by %d, t=%ld jiffies, g=%ld, c=%ld, q=%lu)\n",
	       smp_processor_id(), (long)(jiffies - rsp->gp_start),
	       (long)rsp->gpnum, (long)rsp->completed, totqlen);
	if (ndetected) {
		rcu_dump_cpu_stacks(rsp);

		/* Complain about tasks blocking the grace period. */
		rcu_print_detail_task_stall(rsp);
	} else {
		if (READ_ONCE(rsp->gpnum) != gpnum ||
		    READ_ONCE(rsp->completed) == gpnum) {
			pr_err("INFO: Stall ended before state dump start\n");
		} else {
			j = jiffies;
			gpa = READ_ONCE(rsp->gp_activity);
			pr_err("All QSes seen, last %s kthread activity %ld (%ld-%ld), jiffies_till_next_fqs=%ld, root ->qsmask %#lx\n",
			       rsp->name, j - gpa, j, gpa,
			       jiffies_till_next_fqs,
			       rcu_get_root(rsp)->qsmask);
			/* In this case, the current CPU might be at fault. */
			sched_show_task(current);
		}
	}

	rcu_check_gp_kthread_starvation(rsp);

	panic_on_rcu_stall();

	force_quiescent_state(rsp);  /* Kick them all. */
}

static void print_cpu_stall(struct rcu_state *rsp)
{
	int cpu;
	unsigned long flags;
	struct rcu_node *rnp = rcu_get_root(rsp);
	long totqlen = 0;

	/* Kick and suppress, if so configured. */
	rcu_stall_kick_kthreads(rsp);
	if (rcu_cpu_stall_suppress)
		return;

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
		totqlen += rcu_segcblist_n_cbs(&per_cpu_ptr(rsp->rda,
							    cpu)->cblist);
	pr_cont(" (t=%lu jiffies g=%ld c=%ld q=%lu)\n",
		jiffies - rsp->gp_start,
		(long)rsp->gpnum, (long)rsp->completed, totqlen);

	rcu_check_gp_kthread_starvation(rsp);

	rcu_dump_cpu_stacks(rsp);

	raw_spin_lock_irqsave_rcu_node(rnp, flags);
	if (ULONG_CMP_GE(jiffies, READ_ONCE(rsp->jiffies_stall)))
		WRITE_ONCE(rsp->jiffies_stall,
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

	if ((rcu_cpu_stall_suppress && !rcu_kick_kthreads) ||
	    !rcu_gp_in_progress(rsp))
		return;
	rcu_stall_kick_kthreads(rsp);
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
	gpnum = READ_ONCE(rsp->gpnum);
	smp_rmb(); /* Pick up ->gpnum first... */
	js = READ_ONCE(rsp->jiffies_stall);
	smp_rmb(); /* ...then ->jiffies_stall before the rest... */
	gps = READ_ONCE(rsp->gp_start);
	smp_rmb(); /* ...and finally ->gp_start before ->completed. */
	completed = READ_ONCE(rsp->completed);
	if (ULONG_CMP_GE(completed, gpnum) ||
	    ULONG_CMP_LT(j, js) ||
	    ULONG_CMP_GE(gps, js))
		return; /* No stall or GP completed since entering function. */
	rnp = rdp->mynode;
	if (rcu_gp_in_progress(rsp) &&
	    (READ_ONCE(rnp->qsmask) & rdp->grpmask)) {

		/* We haven't checked in, so go dump stack. */
		print_cpu_stall(rsp);

	} else if (rcu_gp_in_progress(rsp) &&
		   ULONG_CMP_GE(j, js + RCU_STALL_RAT_DELAY)) {

		/* They had a few time units to dump stack, so complain. */
		print_other_cpu_stall(rsp, gpnum);
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
		WRITE_ONCE(rsp->jiffies_stall, jiffies + ULONG_MAX / 2);
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
	lockdep_assert_held(&rnp->lock);

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
	bool ret = false;
	struct rcu_node *rnp_root = rcu_get_root(rdp->rsp);

	lockdep_assert_held(&rnp->lock);

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
	    READ_ONCE(rnp_root->gpnum) != READ_ONCE(rnp_root->completed)) {
		rnp->need_future_gp[c & 0x1]++;
		trace_rcu_future_gp(rnp, rdp, c, TPS("Startedleaf"));
		goto out;
	}

	/*
	 * There might be no grace period in progress.  If we don't already
	 * hold it, acquire the root rcu_node structure's lock in order to
	 * start one (if needed).
	 */
	if (rnp != rnp_root)
		raw_spin_lock_rcu_node(rnp_root);

	/*
	 * Get a new grace-period number.  If there really is no grace
	 * period in progress, it will be smaller than the one we obtained
	 * earlier.  Adjust callbacks as needed.
	 */
	c = rcu_cbs_completed(rdp->rsp, rnp_root);
	if (!rcu_is_nocb_cpu(rdp->cpu))
		(void)rcu_segcblist_accelerate(&rdp->cblist, c);

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
		raw_spin_unlock_rcu_node(rnp_root);
out:
	if (c_out != NULL)
		*c_out = c;
	return ret;
}

/*
 * Clean up any old requests for the just-ended grace period.  Also return
 * whether any additional grace periods have been requested.
 */
static int rcu_future_gp_cleanup(struct rcu_state *rsp, struct rcu_node *rnp)
{
	int c = rnp->completed;
	int needmore;
	struct rcu_data *rdp = this_cpu_ptr(rsp->rda);

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
	    !READ_ONCE(rsp->gp_flags) ||
	    !rsp->gp_kthread)
		return;
	swake_up(&rsp->gp_wq);
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
	bool ret = false;

	lockdep_assert_held(&rnp->lock);

	/* If no pending (not yet ready to invoke) callbacks, nothing to do. */
	if (!rcu_segcblist_pend_cbs(&rdp->cblist))
		return false;

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
	if (rcu_segcblist_accelerate(&rdp->cblist, rcu_cbs_completed(rsp, rnp)))
		ret = rcu_start_future_gp(rnp, rdp, NULL);

	/* Trace depending on how much we were able to accelerate. */
	if (rcu_segcblist_restempty(&rdp->cblist, RCU_WAIT_TAIL))
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
	lockdep_assert_held(&rnp->lock);

	/* If no pending (not yet ready to invoke) callbacks, nothing to do. */
	if (!rcu_segcblist_pend_cbs(&rdp->cblist))
		return false;

	/*
	 * Find all callbacks whose ->completed numbers indicate that they
	 * are ready to invoke, and put them into the RCU_DONE_TAIL sublist.
	 */
	rcu_segcblist_advance(&rdp->cblist, rnp->completed);

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
	bool need_gp;

	lockdep_assert_held(&rnp->lock);

	/* Handle the ends of any preceding grace periods first. */
	if (rdp->completed == rnp->completed &&
	    !unlikely(READ_ONCE(rdp->gpwrap))) {

		/* No grace period end, so just accelerate recent callbacks. */
		ret = rcu_accelerate_cbs(rsp, rnp, rdp);

	} else {

		/* Advance callbacks. */
		ret = rcu_advance_cbs(rsp, rnp, rdp);

		/* Remember that we saw this grace-period completion. */
		rdp->completed = rnp->completed;
		trace_rcu_grace_period(rsp->name, rdp->gpnum, TPS("cpuend"));
	}

	if (rdp->gpnum != rnp->gpnum || unlikely(READ_ONCE(rdp->gpwrap))) {
		/*
		 * If the current grace period is waiting for this CPU,
		 * set up to detect a quiescent state, otherwise don't
		 * go looking for one.
		 */
		rdp->gpnum = rnp->gpnum;
		trace_rcu_grace_period(rsp->name, rdp->gpnum, TPS("cpustart"));
		need_gp = !!(rnp->qsmask & rdp->grpmask);
		rdp->cpu_no_qs.b.norm = need_gp;
		rdp->rcu_qs_ctr_snap = __this_cpu_read(rcu_dynticks.rcu_qs_ctr);
		rdp->core_needs_qs = need_gp;
		zero_cpu_stall_ticks(rdp);
		WRITE_ONCE(rdp->gpwrap, false);
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
	if ((rdp->gpnum == READ_ONCE(rnp->gpnum) &&
	     rdp->completed == READ_ONCE(rnp->completed) &&
	     !unlikely(READ_ONCE(rdp->gpwrap))) || /* w/out lock. */
	    !raw_spin_trylock_rcu_node(rnp)) { /* irqs already off, so later. */
		local_irq_restore(flags);
		return;
	}
	needwake = __note_gp_changes(rsp, rnp, rdp);
	raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
	if (needwake)
		rcu_gp_kthread_wake(rsp);
}

static void rcu_gp_slow(struct rcu_state *rsp, int delay)
{
	if (delay > 0 &&
	    !(rsp->gpnum % (rcu_num_nodes * PER_RCU_NODE_PERIOD * delay)))
		schedule_timeout_uninterruptible(delay);
}

/*
 * Initialize a new grace period.  Return false if no grace period required.
 */
static bool rcu_gp_init(struct rcu_state *rsp)
{
	unsigned long oldmask;
	struct rcu_data *rdp;
	struct rcu_node *rnp = rcu_get_root(rsp);

	WRITE_ONCE(rsp->gp_activity, jiffies);
	raw_spin_lock_irq_rcu_node(rnp);
	if (!READ_ONCE(rsp->gp_flags)) {
		/* Spurious wakeup, tell caller to go back to sleep.  */
		raw_spin_unlock_irq_rcu_node(rnp);
		return false;
	}
	WRITE_ONCE(rsp->gp_flags, 0); /* Clear all flags: New grace period. */

	if (WARN_ON_ONCE(rcu_gp_in_progress(rsp))) {
		/*
		 * Grace period already in progress, don't start another.
		 * Not supposed to be able to happen.
		 */
		raw_spin_unlock_irq_rcu_node(rnp);
		return false;
	}

	/* Advance to a new grace period and initialize state. */
	record_gp_stall_check_time(rsp);
	/* Record GP times before starting GP, hence smp_store_release(). */
	smp_store_release(&rsp->gpnum, rsp->gpnum + 1);
	trace_rcu_grace_period(rsp->name, rsp->gpnum, TPS("start"));
	raw_spin_unlock_irq_rcu_node(rnp);

	/*
	 * Apply per-leaf buffered online and offline operations to the
	 * rcu_node tree.  Note that this new grace period need not wait
	 * for subsequent online CPUs, and that quiescent-state forcing
	 * will handle subsequent offline CPUs.
	 */
	rcu_for_each_leaf_node(rsp, rnp) {
		rcu_gp_slow(rsp, gp_preinit_delay);
		raw_spin_lock_irq_rcu_node(rnp);
		if (rnp->qsmaskinit == rnp->qsmaskinitnext &&
		    !rnp->wait_blkd_tasks) {
			/* Nothing to do on this leaf rcu_node structure. */
			raw_spin_unlock_irq_rcu_node(rnp);
			continue;
		}

		/* Record old state, apply changes to ->qsmaskinit field. */
		oldmask = rnp->qsmaskinit;
		rnp->qsmaskinit = rnp->qsmaskinitnext;

		/* If zero-ness of ->qsmaskinit changed, propagate up tree. */
		if (!oldmask != !rnp->qsmaskinit) {
			if (!oldmask) /* First online CPU for this rcu_node. */
				rcu_init_new_rnp(rnp);
			else if (rcu_preempt_has_tasks(rnp)) /* blocked tasks */
				rnp->wait_blkd_tasks = true;
			else /* Last offline CPU and can propagate. */
				rcu_cleanup_dead_rnp(rnp);
		}

		/*
		 * If all waited-on tasks from prior grace period are
		 * done, and if all this rcu_node structure's CPUs are
		 * still offline, propagate up the rcu_node tree and
		 * clear ->wait_blkd_tasks.  Otherwise, if one of this
		 * rcu_node structure's CPUs has since come back online,
		 * simply clear ->wait_blkd_tasks (but rcu_cleanup_dead_rnp()
		 * checks for this, so just call it unconditionally).
		 */
		if (rnp->wait_blkd_tasks &&
		    (!rcu_preempt_has_tasks(rnp) ||
		     rnp->qsmaskinit)) {
			rnp->wait_blkd_tasks = false;
			rcu_cleanup_dead_rnp(rnp);
		}

		raw_spin_unlock_irq_rcu_node(rnp);
	}

	/*
	 * Set the quiescent-state-needed bits in all the rcu_node
	 * structures for all currently online CPUs in breadth-first order,
	 * starting from the root rcu_node structure, relying on the layout
	 * of the tree within the rsp->node[] array.  Note that other CPUs
	 * will access only the leaves of the hierarchy, thus seeing that no
	 * grace period is in progress, at least until the corresponding
	 * leaf node has been initialized.
	 *
	 * The grace period cannot complete until the initialization
	 * process finishes, because this kthread handles both.
	 */
	rcu_for_each_node_breadth_first(rsp, rnp) {
		rcu_gp_slow(rsp, gp_init_delay);
		raw_spin_lock_irq_rcu_node(rnp);
		rdp = this_cpu_ptr(rsp->rda);
		rcu_preempt_check_blocked_tasks(rnp);
		rnp->qsmask = rnp->qsmaskinit;
		WRITE_ONCE(rnp->gpnum, rsp->gpnum);
		if (WARN_ON_ONCE(rnp->completed != rsp->completed))
			WRITE_ONCE(rnp->completed, rsp->completed);
		if (rnp == rdp->mynode)
			(void)__note_gp_changes(rsp, rnp, rdp);
		rcu_preempt_boost_start_gp(rnp);
		trace_rcu_grace_period_init(rsp->name, rnp->gpnum,
					    rnp->level, rnp->grplo,
					    rnp->grphi, rnp->qsmask);
		raw_spin_unlock_irq_rcu_node(rnp);
		cond_resched_rcu_qs();
		WRITE_ONCE(rsp->gp_activity, jiffies);
	}

	return true;
}

/*
 * Helper function for swait_event_idle() wakeup at force-quiescent-state
 * time.
 */
static bool rcu_gp_fqs_check_wake(struct rcu_state *rsp, int *gfp)
{
	struct rcu_node *rnp = rcu_get_root(rsp);

	/* Someone like call_rcu() requested a force-quiescent-state scan. */
	*gfp = READ_ONCE(rsp->gp_flags);
	if (*gfp & RCU_GP_FLAG_FQS)
		return true;

	/* The current grace period has completed. */
	if (!READ_ONCE(rnp->qsmask) && !rcu_preempt_blocked_readers_cgp(rnp))
		return true;

	return false;
}

/*
 * Do one round of quiescent-state forcing.
 */
static void rcu_gp_fqs(struct rcu_state *rsp, bool first_time)
{
	struct rcu_node *rnp = rcu_get_root(rsp);

	WRITE_ONCE(rsp->gp_activity, jiffies);
	rsp->n_force_qs++;
	if (first_time) {
		/* Collect dyntick-idle snapshots. */
		force_qs_rnp(rsp, dyntick_save_progress_counter);
	} else {
		/* Handle dyntick-idle and offline CPUs. */
		force_qs_rnp(rsp, rcu_implicit_dynticks_qs);
	}
	/* Clear flag to prevent immediate re-entry. */
	if (READ_ONCE(rsp->gp_flags) & RCU_GP_FLAG_FQS) {
		raw_spin_lock_irq_rcu_node(rnp);
		WRITE_ONCE(rsp->gp_flags,
			   READ_ONCE(rsp->gp_flags) & ~RCU_GP_FLAG_FQS);
		raw_spin_unlock_irq_rcu_node(rnp);
	}
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
	struct swait_queue_head *sq;

	WRITE_ONCE(rsp->gp_activity, jiffies);
	raw_spin_lock_irq_rcu_node(rnp);
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
	raw_spin_unlock_irq_rcu_node(rnp);

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
		raw_spin_lock_irq_rcu_node(rnp);
		WARN_ON_ONCE(rcu_preempt_blocked_readers_cgp(rnp));
		WARN_ON_ONCE(rnp->qsmask);
		WRITE_ONCE(rnp->completed, rsp->gpnum);
		rdp = this_cpu_ptr(rsp->rda);
		if (rnp == rdp->mynode)
			needgp = __note_gp_changes(rsp, rnp, rdp) || needgp;
		/* smp_mb() provided by prior unlock-lock pair. */
		nocb += rcu_future_gp_cleanup(rsp, rnp);
		sq = rcu_nocb_gp_get(rnp);
		raw_spin_unlock_irq_rcu_node(rnp);
		rcu_nocb_gp_cleanup(sq);
		cond_resched_rcu_qs();
		WRITE_ONCE(rsp->gp_activity, jiffies);
		rcu_gp_slow(rsp, gp_cleanup_delay);
	}
	rnp = rcu_get_root(rsp);
	raw_spin_lock_irq_rcu_node(rnp); /* Order GP before ->completed update. */
	rcu_nocb_gp_set(rnp, nocb);

	/* Declare grace period done. */
	WRITE_ONCE(rsp->completed, rsp->gpnum);
	trace_rcu_grace_period(rsp->name, rsp->completed, TPS("end"));
	rsp->gp_state = RCU_GP_IDLE;
	rdp = this_cpu_ptr(rsp->rda);
	/* Advance CBs to reduce false positives below. */
	needgp = rcu_advance_cbs(rsp, rnp, rdp) || needgp;
	if (needgp || cpu_needs_another_gp(rsp, rdp)) {
		WRITE_ONCE(rsp->gp_flags, RCU_GP_FLAG_INIT);
		trace_rcu_grace_period(rsp->name,
				       READ_ONCE(rsp->gpnum),
				       TPS("newreq"));
	}
	raw_spin_unlock_irq_rcu_node(rnp);
}

/*
 * Body of kthread that handles grace periods.
 */
static int __noreturn rcu_gp_kthread(void *arg)
{
	bool first_gp_fqs;
	int gf;
	unsigned long j;
	int ret;
	struct rcu_state *rsp = arg;
	struct rcu_node *rnp = rcu_get_root(rsp);

	rcu_bind_gp_kthread();
	for (;;) {

		/* Handle grace-period start. */
		for (;;) {
			trace_rcu_grace_period(rsp->name,
					       READ_ONCE(rsp->gpnum),
					       TPS("reqwait"));
			rsp->gp_state = RCU_GP_WAIT_GPS;
			swait_event_idle(rsp->gp_wq, READ_ONCE(rsp->gp_flags) &
						     RCU_GP_FLAG_INIT);
			rsp->gp_state = RCU_GP_DONE_GPS;
			/* Locking provides needed memory barrier. */
			if (rcu_gp_init(rsp))
				break;
			cond_resched_rcu_qs();
			WRITE_ONCE(rsp->gp_activity, jiffies);
			WARN_ON(signal_pending(current));
			trace_rcu_grace_period(rsp->name,
					       READ_ONCE(rsp->gpnum),
					       TPS("reqwaitsig"));
		}

		/* Handle quiescent-state forcing. */
		first_gp_fqs = true;
		j = jiffies_till_first_fqs;
		if (j > HZ) {
			j = HZ;
			jiffies_till_first_fqs = HZ;
		}
		ret = 0;
		for (;;) {
			if (!ret) {
				rsp->jiffies_force_qs = jiffies + j;
				WRITE_ONCE(rsp->jiffies_kick_kthreads,
					   jiffies + 3 * j);
			}
			trace_rcu_grace_period(rsp->name,
					       READ_ONCE(rsp->gpnum),
					       TPS("fqswait"));
			rsp->gp_state = RCU_GP_WAIT_FQS;
			ret = swait_event_idle_timeout(rsp->gp_wq,
					rcu_gp_fqs_check_wake(rsp, &gf), j);
			rsp->gp_state = RCU_GP_DOING_FQS;
			/* Locking provides needed memory barriers. */
			/* If grace period done, leave loop. */
			if (!READ_ONCE(rnp->qsmask) &&
			    !rcu_preempt_blocked_readers_cgp(rnp))
				break;
			/* If time for quiescent-state forcing, do it. */
			if (ULONG_CMP_GE(jiffies, rsp->jiffies_force_qs) ||
			    (gf & RCU_GP_FLAG_FQS)) {
				trace_rcu_grace_period(rsp->name,
						       READ_ONCE(rsp->gpnum),
						       TPS("fqsstart"));
				rcu_gp_fqs(rsp, first_gp_fqs);
				first_gp_fqs = false;
				trace_rcu_grace_period(rsp->name,
						       READ_ONCE(rsp->gpnum),
						       TPS("fqsend"));
				cond_resched_rcu_qs();
				WRITE_ONCE(rsp->gp_activity, jiffies);
				ret = 0; /* Force full wait till next FQS. */
				j = jiffies_till_next_fqs;
				if (j > HZ) {
					j = HZ;
					jiffies_till_next_fqs = HZ;
				} else if (j < 1) {
					j = 1;
					jiffies_till_next_fqs = 1;
				}
			} else {
				/* Deal with stray signal. */
				cond_resched_rcu_qs();
				WRITE_ONCE(rsp->gp_activity, jiffies);
				WARN_ON(signal_pending(current));
				trace_rcu_grace_period(rsp->name,
						       READ_ONCE(rsp->gpnum),
						       TPS("fqswaitsig"));
				ret = 1; /* Keep old FQS timing. */
				j = jiffies;
				if (time_after(jiffies, rsp->jiffies_force_qs))
					j = 1;
				else
					j = rsp->jiffies_force_qs - j;
			}
		}

		/* Handle grace-period end. */
		rsp->gp_state = RCU_GP_CLEANUP;
		rcu_gp_cleanup(rsp);
		rsp->gp_state = RCU_GP_CLEANED;
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
	lockdep_assert_held(&rnp->lock);
	if (!rsp->gp_kthread || !cpu_needs_another_gp(rsp, rdp)) {
		/*
		 * Either we have not yet spawned the grace-period
		 * task, this CPU does not need another grace period,
		 * or a grace period is already in progress.
		 * Either way, don't start a new grace period.
		 */
		return false;
	}
	WRITE_ONCE(rsp->gp_flags, RCU_GP_FLAG_INIT);
	trace_rcu_grace_period(rsp->name, READ_ONCE(rsp->gpnum),
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
 * Report a full set of quiescent states to the specified rcu_state data
 * structure.  Invoke rcu_gp_kthread_wake() to awaken the grace-period
 * kthread if another grace period is required.  Whether we wake
 * the grace-period kthread or it awakens itself for the next round
 * of quiescent-state forcing, that kthread will clean up after the
 * just-completed grace period.  Note that the caller must hold rnp->lock,
 * which is released before return.
 */
static void rcu_report_qs_rsp(struct rcu_state *rsp, unsigned long flags)
	__releases(rcu_get_root(rsp)->lock)
{
	lockdep_assert_held(&rcu_get_root(rsp)->lock);
	WARN_ON_ONCE(!rcu_gp_in_progress(rsp));
	WRITE_ONCE(rsp->gp_flags, READ_ONCE(rsp->gp_flags) | RCU_GP_FLAG_FQS);
	raw_spin_unlock_irqrestore_rcu_node(rcu_get_root(rsp), flags);
	rcu_gp_kthread_wake(rsp);
}

/*
 * Similar to rcu_report_qs_rdp(), for which it is a helper function.
 * Allows quiescent states for a group of CPUs to be reported at one go
 * to the specified rcu_node structure, though all the CPUs in the group
 * must be represented by the same rcu_node structure (which need not be a
 * leaf rcu_node structure, though it often will be).  The gps parameter
 * is the grace-period snapshot, which means that the quiescent states
 * are valid only if rnp->gpnum is equal to gps.  That structure's lock
 * must be held upon entry, and it is released before return.
 */
static void
rcu_report_qs_rnp(unsigned long mask, struct rcu_state *rsp,
		  struct rcu_node *rnp, unsigned long gps, unsigned long flags)
	__releases(rnp->lock)
{
	unsigned long oldmask = 0;
	struct rcu_node *rnp_c;

	lockdep_assert_held(&rnp->lock);

	/* Walk up the rcu_node hierarchy. */
	for (;;) {
		if (!(rnp->qsmask & mask) || rnp->gpnum != gps) {

			/*
			 * Our bit has already been cleared, or the
			 * relevant grace period is already over, so done.
			 */
			raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
			return;
		}
		WARN_ON_ONCE(oldmask); /* Any child must be all zeroed! */
		WARN_ON_ONCE(rnp->level != rcu_num_lvls - 1 &&
			     rcu_preempt_blocked_readers_cgp(rnp));
		rnp->qsmask &= ~mask;
		trace_rcu_quiescent_state_report(rsp->name, rnp->gpnum,
						 mask, rnp->qsmask, rnp->level,
						 rnp->grplo, rnp->grphi,
						 !!rnp->gp_tasks);
		if (rnp->qsmask != 0 || rcu_preempt_blocked_readers_cgp(rnp)) {

			/* Other bits still set at this level, so done. */
			raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
			return;
		}
		mask = rnp->grpmask;
		if (rnp->parent == NULL) {

			/* No more levels.  Exit loop holding root lock. */

			break;
		}
		raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
		rnp_c = rnp;
		rnp = rnp->parent;
		raw_spin_lock_irqsave_rcu_node(rnp, flags);
		oldmask = rnp_c->qsmask;
	}

	/*
	 * Get here if we are the last CPU to pass through a quiescent
	 * state for this grace period.  Invoke rcu_report_qs_rsp()
	 * to clean up and start the next grace period if one is needed.
	 */
	rcu_report_qs_rsp(rsp, flags); /* releases rnp->lock. */
}

/*
 * Record a quiescent state for all tasks that were previously queued
 * on the specified rcu_node structure and that were blocking the current
 * RCU grace period.  The caller must hold the specified rnp->lock with
 * irqs disabled, and this lock is released upon return, but irqs remain
 * disabled.
 */
static void rcu_report_unblock_qs_rnp(struct rcu_state *rsp,
				      struct rcu_node *rnp, unsigned long flags)
	__releases(rnp->lock)
{
	unsigned long gps;
	unsigned long mask;
	struct rcu_node *rnp_p;

	lockdep_assert_held(&rnp->lock);
	if (rcu_state_p == &rcu_sched_state || rsp != rcu_state_p ||
	    rnp->qsmask != 0 || rcu_preempt_blocked_readers_cgp(rnp)) {
		raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
		return;  /* Still need more quiescent states! */
	}

	rnp_p = rnp->parent;
	if (rnp_p == NULL) {
		/*
		 * Only one rcu_node structure in the tree, so don't
		 * try to report up to its nonexistent parent!
		 */
		rcu_report_qs_rsp(rsp, flags);
		return;
	}

	/* Report up the rest of the hierarchy, tracking current ->gpnum. */
	gps = rnp->gpnum;
	mask = rnp->grpmask;
	raw_spin_unlock_rcu_node(rnp);	/* irqs remain disabled. */
	raw_spin_lock_rcu_node(rnp_p);	/* irqs already disabled. */
	rcu_report_qs_rnp(mask, rsp, rnp_p, gps, flags);
}

/*
 * Record a quiescent state for the specified CPU to that CPU's rcu_data
 * structure.  This must be called from the specified CPU.
 */
static void
rcu_report_qs_rdp(int cpu, struct rcu_state *rsp, struct rcu_data *rdp)
{
	unsigned long flags;
	unsigned long mask;
	bool needwake;
	struct rcu_node *rnp;

	rnp = rdp->mynode;
	raw_spin_lock_irqsave_rcu_node(rnp, flags);
	if (rdp->cpu_no_qs.b.norm || rdp->gpnum != rnp->gpnum ||
	    rnp->completed == rnp->gpnum || rdp->gpwrap) {

		/*
		 * The grace period in which this quiescent state was
		 * recorded has ended, so don't report it upwards.
		 * We will instead need a new quiescent state that lies
		 * within the current grace period.
		 */
		rdp->cpu_no_qs.b.norm = true;	/* need qs for new gp. */
		rdp->rcu_qs_ctr_snap = __this_cpu_read(rcu_dynticks.rcu_qs_ctr);
		raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
		return;
	}
	mask = rdp->grpmask;
	if ((rnp->qsmask & mask) == 0) {
		raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
	} else {
		rdp->core_needs_qs = false;

		/*
		 * This GP can't end until cpu checks in, so all of our
		 * callbacks can be processed during the next GP.
		 */
		needwake = rcu_accelerate_cbs(rsp, rnp, rdp);

		rcu_report_qs_rnp(mask, rsp, rnp, rnp->gpnum, flags);
		/* ^^^ Released rnp->lock */
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
	rcu_report_qs_rdp(rdp->cpu, rsp, rdp);
}

/*
 * Trace the fact that this CPU is going offline.
 */
static void rcu_cleanup_dying_cpu(struct rcu_state *rsp)
{
	RCU_TRACE(unsigned long mask;)
	RCU_TRACE(struct rcu_data *rdp = this_cpu_ptr(rsp->rda);)
	RCU_TRACE(struct rcu_node *rnp = rdp->mynode;)

	if (!IS_ENABLED(CONFIG_HOTPLUG_CPU))
		return;

	RCU_TRACE(mask = rdp->grpmask;)
	trace_rcu_grace_period(rsp->name,
			       rnp->gpnum + 1 - !!(rnp->qsmask & mask),
			       TPS("cpuofl"));
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
 * updated
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

	lockdep_assert_held(&rnp->lock);
	if (!IS_ENABLED(CONFIG_HOTPLUG_CPU) ||
	    rnp->qsmaskinit || rcu_preempt_has_tasks(rnp))
		return;
	for (;;) {
		mask = rnp->grpmask;
		rnp = rnp->parent;
		if (!rnp)
			break;
		raw_spin_lock_rcu_node(rnp); /* irqs already disabled. */
		rnp->qsmaskinit &= ~mask;
		rnp->qsmask &= ~mask;
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
static void rcu_cleanup_dead_cpu(int cpu, struct rcu_state *rsp)
{
	struct rcu_data *rdp = per_cpu_ptr(rsp->rda, cpu);
	struct rcu_node *rnp = rdp->mynode;  /* Outgoing CPU's rdp & rnp. */

	if (!IS_ENABLED(CONFIG_HOTPLUG_CPU))
		return;

	/* Adjust any no-longer-needed kthreads. */
	rcu_boost_kthread_setaffinity(rnp, -1);
}

/*
 * Invoke any RCU callbacks that have made it to the end of their grace
 * period.  Thottle as specified by rdp->blimit.
 */
static void rcu_do_batch(struct rcu_state *rsp, struct rcu_data *rdp)
{
	unsigned long flags;
	struct rcu_head *rhp;
	struct rcu_cblist rcl = RCU_CBLIST_INITIALIZER(rcl);
	long bl, count;

	/* If no callbacks are ready, just return. */
	if (!rcu_segcblist_ready_cbs(&rdp->cblist)) {
		trace_rcu_batch_start(rsp->name,
				      rcu_segcblist_n_lazy_cbs(&rdp->cblist),
				      rcu_segcblist_n_cbs(&rdp->cblist), 0);
		trace_rcu_batch_end(rsp->name, 0,
				    !rcu_segcblist_empty(&rdp->cblist),
				    need_resched(), is_idle_task(current),
				    rcu_is_callbacks_kthread());
		return;
	}

	/*
	 * Extract the list of ready callbacks, disabling to prevent
	 * races with call_rcu() from interrupt handlers.  Leave the
	 * callback counts, as rcu_barrier() needs to be conservative.
	 */
	local_irq_save(flags);
	WARN_ON_ONCE(cpu_is_offline(smp_processor_id()));
	bl = rdp->blimit;
	trace_rcu_batch_start(rsp->name, rcu_segcblist_n_lazy_cbs(&rdp->cblist),
			      rcu_segcblist_n_cbs(&rdp->cblist), bl);
	rcu_segcblist_extract_done_cbs(&rdp->cblist, &rcl);
	local_irq_restore(flags);

	/* Invoke callbacks. */
	rhp = rcu_cblist_dequeue(&rcl);
	for (; rhp; rhp = rcu_cblist_dequeue(&rcl)) {
		debug_rcu_head_unqueue(rhp);
		if (__rcu_reclaim(rsp->name, rhp))
			rcu_cblist_dequeued_lazy(&rcl);
		/*
		 * Stop only if limit reached and CPU has something to do.
		 * Note: The rcl structure counts down from zero.
		 */
		if (-rcl.len >= bl &&
		    (need_resched() ||
		     (!is_idle_task(current) && !rcu_is_callbacks_kthread())))
			break;
	}

	local_irq_save(flags);
	count = -rcl.len;
	trace_rcu_batch_end(rsp->name, count, !!rcl.head, need_resched(),
			    is_idle_task(current), rcu_is_callbacks_kthread());

	/* Update counts and requeue any remaining callbacks. */
	rcu_segcblist_insert_done_cbs(&rdp->cblist, &rcl);
	smp_mb(); /* List handling before counting for rcu_barrier(). */
	rdp->n_cbs_invoked += count;
	rcu_segcblist_insert_count(&rdp->cblist, &rcl);

	/* Reinstate batch limit if we have worked down the excess. */
	count = rcu_segcblist_n_cbs(&rdp->cblist);
	if (rdp->blimit == LONG_MAX && count <= qlowmark)
		rdp->blimit = blimit;

	/* Reset ->qlen_last_fqs_check trigger if enough CBs have drained. */
	if (count == 0 && rdp->qlen_last_fqs_check != 0) {
		rdp->qlen_last_fqs_check = 0;
		rdp->n_force_qs_snap = rsp->n_force_qs;
	} else if (count < rdp->qlen_last_fqs_check - qhimark)
		rdp->qlen_last_fqs_check = count;
	WARN_ON_ONCE(rcu_segcblist_empty(&rdp->cblist) != (count == 0));

	local_irq_restore(flags);

	/* Re-invoke RCU core processing if there are callbacks remaining. */
	if (rcu_segcblist_ready_cbs(&rdp->cblist))
		invoke_rcu_core();
}

/*
 * Check to see if this CPU is in a non-context-switch quiescent state
 * (user mode or idle loop for rcu, non-softirq execution for rcu_bh).
 * Also schedule RCU core processing.
 *
 * This function must be called from hardirq context.  It is normally
 * invoked from the scheduling-clock interrupt.
 */
void rcu_check_callbacks(int user)
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

		rcu_sched_qs();
		rcu_bh_qs();

	} else if (!in_softirq()) {

		/*
		 * Get here if this CPU did not take its interrupt from
		 * softirq, in other words, if it is not interrupting
		 * a rcu_bh read-side critical section.  This is an _bh
		 * critical section, so note it.
		 */

		rcu_bh_qs();
	}
	rcu_preempt_check_callbacks();
	if (rcu_pending())
		invoke_rcu_core();
	if (user)
		rcu_note_voluntary_context_switch(current);
	trace_rcu_utilization(TPS("End scheduler-tick"));
}

/*
 * Scan the leaf rcu_node structures, processing dyntick state for any that
 * have not yet encountered a quiescent state, using the function specified.
 * Also initiate boosting for any threads blocked on the root rcu_node.
 *
 * The caller must have suppressed start of new grace periods.
 */
static void force_qs_rnp(struct rcu_state *rsp, int (*f)(struct rcu_data *rsp))
{
	int cpu;
	unsigned long flags;
	unsigned long mask;
	struct rcu_node *rnp;

	rcu_for_each_leaf_node(rsp, rnp) {
		cond_resched_rcu_qs();
		mask = 0;
		raw_spin_lock_irqsave_rcu_node(rnp, flags);
		if (rnp->qsmask == 0) {
			if (rcu_state_p == &rcu_sched_state ||
			    rsp != rcu_state_p ||
			    rcu_preempt_blocked_readers_cgp(rnp)) {
				/*
				 * No point in scanning bits because they
				 * are all zero.  But we might need to
				 * priority-boost blocked readers.
				 */
				rcu_initiate_boost(rnp, flags);
				/* rcu_initiate_boost() releases rnp->lock */
				continue;
			}
			if (rnp->parent &&
			    (rnp->parent->qsmask & rnp->grpmask)) {
				/*
				 * Race between grace-period
				 * initialization and task exiting RCU
				 * read-side critical section: Report.
				 */
				rcu_report_unblock_qs_rnp(rsp, rnp, flags);
				/* rcu_report_unblock_qs_rnp() rlses ->lock */
				continue;
			}
		}
		for_each_leaf_node_possible_cpu(rnp, cpu) {
			unsigned long bit = leaf_node_cpu_bit(rnp, cpu);
			if ((rnp->qsmask & bit) != 0) {
				if (f(per_cpu_ptr(rsp->rda, cpu)))
					mask |= bit;
			}
		}
		if (mask != 0) {
			/* Idle/offline CPUs, report (releases rnp->lock. */
			rcu_report_qs_rnp(mask, rsp, rnp, rnp->gpnum, flags);
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
static void force_quiescent_state(struct rcu_state *rsp)
{
	unsigned long flags;
	bool ret;
	struct rcu_node *rnp;
	struct rcu_node *rnp_old = NULL;

	/* Funnel through hierarchy to reduce memory contention. */
	rnp = __this_cpu_read(rsp->rda->mynode);
	for (; rnp != NULL; rnp = rnp->parent) {
		ret = (READ_ONCE(rsp->gp_flags) & RCU_GP_FLAG_FQS) ||
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
	raw_spin_lock_irqsave_rcu_node(rnp_old, flags);
	raw_spin_unlock(&rnp_old->fqslock);
	if (READ_ONCE(rsp->gp_flags) & RCU_GP_FLAG_FQS) {
		rsp->n_force_qs_lh++;
		raw_spin_unlock_irqrestore_rcu_node(rnp_old, flags);
		return;  /* Someone beat us to it. */
	}
	WRITE_ONCE(rsp->gp_flags, READ_ONCE(rsp->gp_flags) | RCU_GP_FLAG_FQS);
	raw_spin_unlock_irqrestore_rcu_node(rnp_old, flags);
	rcu_gp_kthread_wake(rsp);
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

	WARN_ON_ONCE(!rdp->beenonline);

	/* Update RCU state based on any recent quiescent states. */
	rcu_check_quiescent_state(rsp, rdp);

	/* Does this CPU require a not-yet-started grace period? */
	local_irq_save(flags);
	if (cpu_needs_another_gp(rsp, rdp)) {
		raw_spin_lock_rcu_node(rcu_get_root(rsp)); /* irqs disabled. */
		needwake = rcu_start_gp(rsp);
		raw_spin_unlock_irqrestore_rcu_node(rcu_get_root(rsp), flags);
		if (needwake)
			rcu_gp_kthread_wake(rsp);
	} else {
		local_irq_restore(flags);
	}

	/* If there are callbacks ready, invoke them. */
	if (rcu_segcblist_ready_cbs(&rdp->cblist))
		invoke_rcu_callbacks(rsp, rdp);

	/* Do any needed deferred wakeups of rcuo kthreads. */
	do_nocb_deferred_wakeup(rdp);
}

/*
 * Do RCU core processing for the current CPU.
 */
static __latent_entropy void rcu_process_callbacks(struct softirq_action *unused)
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
 * are running on the current CPU with softirqs disabled, the
 * rcu_cpu_kthread_task cannot disappear out from under us.
 */
static void invoke_rcu_callbacks(struct rcu_state *rsp, struct rcu_data *rdp)
{
	if (unlikely(!READ_ONCE(rcu_scheduler_fully_active)))
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
	if (!rcu_is_watching())
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
	if (unlikely(rcu_segcblist_n_cbs(&rdp->cblist) >
		     rdp->qlen_last_fqs_check + qhimark)) {

		/* Are we ignoring a completed grace period? */
		note_gp_changes(rsp, rdp);

		/* Start a new grace period if one not already started. */
		if (!rcu_gp_in_progress(rsp)) {
			struct rcu_node *rnp_root = rcu_get_root(rsp);

			raw_spin_lock_rcu_node(rnp_root);
			needwake = rcu_start_gp(rsp);
			raw_spin_unlock_rcu_node(rnp_root);
			if (needwake)
				rcu_gp_kthread_wake(rsp);
		} else {
			/* Give the grace period a kick. */
			rdp->blimit = LONG_MAX;
			if (rsp->n_force_qs == rdp->n_force_qs_snap &&
			    rcu_segcblist_first_pend_cb(&rdp->cblist) != head)
				force_quiescent_state(rsp);
			rdp->n_force_qs_snap = rsp->n_force_qs;
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
 * Helper function for call_rcu() and friends.  The cpu argument will
 * normally be -1, indicating "currently running CPU".  It may specify
 * a CPU only if that CPU is a no-CBs CPU.  Currently, only _rcu_barrier()
 * is expected to specify a CPU.
 */
static void
__call_rcu(struct rcu_head *head, rcu_callback_t func,
	   struct rcu_state *rsp, int cpu, bool lazy)
{
	unsigned long flags;
	struct rcu_data *rdp;

	/* Misaligned rcu_head! */
	WARN_ON_ONCE((unsigned long)head & (sizeof(void *) - 1));

	if (debug_rcu_head_queue(head)) {
		/*
		 * Probable double call_rcu(), so leak the callback.
		 * Use rcu:rcu_callback trace event to find the previous
		 * time callback was passed to __call_rcu().
		 */
		WARN_ONCE(1, "__call_rcu(): Double-freed CB %p->%pF()!!!\n",
			  head, head->func);
		WRITE_ONCE(head->func, rcu_leak_callback);
		return;
	}
	head->func = func;
	head->next = NULL;
	local_irq_save(flags);
	rdp = this_cpu_ptr(rsp->rda);

	/* Add the callback to our list. */
	if (unlikely(!rcu_segcblist_is_enabled(&rdp->cblist)) || cpu != -1) {
		int offline;

		if (cpu != -1)
			rdp = per_cpu_ptr(rsp->rda, cpu);
		if (likely(rdp->mynode)) {
			/* Post-boot, so this should be for a no-CBs CPU. */
			offline = !__call_rcu_nocb(rdp, head, lazy, flags);
			WARN_ON_ONCE(offline);
			/* Offline CPU, _call_rcu() illegal, leak callback.  */
			local_irq_restore(flags);
			return;
		}
		/*
		 * Very early boot, before rcu_init().  Initialize if needed
		 * and then drop through to queue the callback.
		 */
		BUG_ON(cpu != -1);
		WARN_ON_ONCE(!rcu_is_watching());
		if (rcu_segcblist_empty(&rdp->cblist))
			rcu_segcblist_init(&rdp->cblist);
	}
	rcu_segcblist_enqueue(&rdp->cblist, head, lazy);
	if (!lazy)
		rcu_idle_count_callbacks_posted();

	if (__is_kfree_rcu_offset((unsigned long)func))
		trace_rcu_kfree_callback(rsp->name, head, (unsigned long)func,
					 rcu_segcblist_n_lazy_cbs(&rdp->cblist),
					 rcu_segcblist_n_cbs(&rdp->cblist));
	else
		trace_rcu_callback(rsp->name, head,
				   rcu_segcblist_n_lazy_cbs(&rdp->cblist),
				   rcu_segcblist_n_cbs(&rdp->cblist));

	/* Go handle any RCU core processing required. */
	__call_rcu_core(rsp, rdp, head, flags);
	local_irq_restore(flags);
}

/**
 * call_rcu_sched() - Queue an RCU for invocation after sched grace period.
 * @head: structure to be used for queueing the RCU updates.
 * @func: actual callback function to be invoked after the grace period
 *
 * The callback function will be invoked some time after a full grace
 * period elapses, in other words after all currently executing RCU
 * read-side critical sections have completed. call_rcu_sched() assumes
 * that the read-side critical sections end on enabling of preemption
 * or on voluntary preemption.
 * RCU read-side critical sections are delimited by :
 *  - rcu_read_lock_sched() and rcu_read_unlock_sched(), OR
 *  - anything that disables preemption.
 *
 *  These may be nested.
 *
 * See the description of call_rcu() for more detailed information on
 * memory ordering guarantees.
 */
void call_rcu_sched(struct rcu_head *head, rcu_callback_t func)
{
	__call_rcu(head, func, &rcu_sched_state, -1, 0);
}
EXPORT_SYMBOL_GPL(call_rcu_sched);

/**
 * call_rcu_bh() - Queue an RCU for invocation after a quicker grace period.
 * @head: structure to be used for queueing the RCU updates.
 * @func: actual callback function to be invoked after the grace period
 *
 * The callback function will be invoked some time after a full grace
 * period elapses, in other words after all currently executing RCU
 * read-side critical sections have completed. call_rcu_bh() assumes
 * that the read-side critical sections end on completion of a softirq
 * handler. This means that read-side critical sections in process
 * context must not be interrupted by softirqs. This interface is to be
 * used when most of the read-side critical sections are in softirq context.
 * RCU read-side critical sections are delimited by :
 *  - rcu_read_lock() and  rcu_read_unlock(), if in interrupt context.
 *  OR
 *  - rcu_read_lock_bh() and rcu_read_unlock_bh(), if in process context.
 *  These may be nested.
 *
 * See the description of call_rcu() for more detailed information on
 * memory ordering guarantees.
 */
void call_rcu_bh(struct rcu_head *head, rcu_callback_t func)
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
		    rcu_callback_t func)
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
 */
void synchronize_sched(void)
{
	RCU_LOCKDEP_WARN(lock_is_held(&rcu_bh_lock_map) ||
			 lock_is_held(&rcu_lock_map) ||
			 lock_is_held(&rcu_sched_lock_map),
			 "Illegal synchronize_sched() in RCU-sched read-side critical section");
	if (rcu_blocking_is_gp())
		return;
	if (rcu_gp_is_expedited())
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
	RCU_LOCKDEP_WARN(lock_is_held(&rcu_bh_lock_map) ||
			 lock_is_held(&rcu_lock_map) ||
			 lock_is_held(&rcu_sched_lock_map),
			 "Illegal synchronize_rcu_bh() in RCU-bh read-side critical section");
	if (rcu_blocking_is_gp())
		return;
	if (rcu_gp_is_expedited())
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

/**
 * get_state_synchronize_sched - Snapshot current RCU-sched state
 *
 * Returns a cookie that is used by a later call to cond_synchronize_sched()
 * to determine whether or not a full grace period has elapsed in the
 * meantime.
 */
unsigned long get_state_synchronize_sched(void)
{
	/*
	 * Any prior manipulation of RCU-protected data must happen
	 * before the load from ->gpnum.
	 */
	smp_mb();  /* ^^^ */

	/*
	 * Make sure this load happens before the purportedly
	 * time-consuming work between get_state_synchronize_sched()
	 * and cond_synchronize_sched().
	 */
	return smp_load_acquire(&rcu_sched_state.gpnum);
}
EXPORT_SYMBOL_GPL(get_state_synchronize_sched);

/**
 * cond_synchronize_sched - Conditionally wait for an RCU-sched grace period
 *
 * @oldstate: return value from earlier call to get_state_synchronize_sched()
 *
 * If a full RCU-sched grace period has elapsed since the earlier call to
 * get_state_synchronize_sched(), just return.  Otherwise, invoke
 * synchronize_sched() to wait for a full grace period.
 *
 * Yes, this function does not take counter wrap into account.  But
 * counter wrap is harmless.  If the counter wraps, we have waited for
 * more than 2 billion grace periods (and way more on a 64-bit system!),
 * so waiting for one additional grace period should be just fine.
 */
void cond_synchronize_sched(unsigned long oldstate)
{
	unsigned long newstate;

	/*
	 * Ensure that this load happens before any RCU-destructive
	 * actions the caller might carry out after we return.
	 */
	newstate = smp_load_acquire(&rcu_sched_state.completed);
	if (ULONG_CMP_GE(oldstate, newstate))
		synchronize_sched();
}
EXPORT_SYMBOL_GPL(cond_synchronize_sched);

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
	    rdp->core_needs_qs && rdp->cpu_no_qs.b.norm &&
	    rdp->rcu_qs_ctr_snap == __this_cpu_read(rcu_dynticks.rcu_qs_ctr)) {
		rdp->n_rp_core_needs_qs++;
	} else if (rdp->core_needs_qs && !rdp->cpu_no_qs.b.norm) {
		rdp->n_rp_report_qs++;
		return 1;
	}

	/* Does this CPU have callbacks ready to invoke? */
	if (rcu_segcblist_ready_cbs(&rdp->cblist)) {
		rdp->n_rp_cb_ready++;
		return 1;
	}

	/* Has RCU gone idle with this CPU needing another grace period? */
	if (cpu_needs_another_gp(rsp, rdp)) {
		rdp->n_rp_cpu_needs_gp++;
		return 1;
	}

	/* Has another RCU grace period completed?  */
	if (READ_ONCE(rnp->completed) != rdp->completed) { /* outside lock */
		rdp->n_rp_gp_completed++;
		return 1;
	}

	/* Has a new RCU grace period started? */
	if (READ_ONCE(rnp->gpnum) != rdp->gpnum ||
	    unlikely(READ_ONCE(rdp->gpwrap))) { /* outside lock */
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
static int rcu_pending(void)
{
	struct rcu_state *rsp;

	for_each_rcu_flavor(rsp)
		if (__rcu_pending(rsp, this_cpu_ptr(rsp->rda)))
			return 1;
	return 0;
}

/*
 * Return true if the specified CPU has any callback.  If all_lazy is
 * non-NULL, store an indication of whether all callbacks are lazy.
 * (If there are no callbacks, all of them are deemed to be lazy.)
 */
static bool __maybe_unused rcu_cpu_has_callbacks(bool *all_lazy)
{
	bool al = true;
	bool hc = false;
	struct rcu_data *rdp;
	struct rcu_state *rsp;

	for_each_rcu_flavor(rsp) {
		rdp = this_cpu_ptr(rsp->rda);
		if (rcu_segcblist_empty(&rdp->cblist))
			continue;
		hc = true;
		if (rcu_segcblist_n_nonlazy_cbs(&rdp->cblist) || !all_lazy) {
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
		_rcu_barrier_trace(rsp, TPS("LastCB"), -1,
				   rsp->barrier_sequence);
		complete(&rsp->barrier_completion);
	} else {
		_rcu_barrier_trace(rsp, TPS("CB"), -1, rsp->barrier_sequence);
	}
}

/*
 * Called with preemption disabled, and from cross-cpu IRQ context.
 */
static void rcu_barrier_func(void *type)
{
	struct rcu_state *rsp = type;
	struct rcu_data *rdp = raw_cpu_ptr(rsp->rda);

	_rcu_barrier_trace(rsp, TPS("IRQ"), -1, rsp->barrier_sequence);
	rdp->barrier_head.func = rcu_barrier_callback;
	debug_rcu_head_queue(&rdp->barrier_head);
	if (rcu_segcblist_entrain(&rdp->cblist, &rdp->barrier_head, 0)) {
		atomic_inc(&rsp->barrier_cpu_count);
	} else {
		debug_rcu_head_unqueue(&rdp->barrier_head);
		_rcu_barrier_trace(rsp, TPS("IRQNQ"), -1,
				   rsp->barrier_sequence);
	}
}

/*
 * Orchestrate the specified type of RCU barrier, waiting for all
 * RCU callbacks of the specified type to complete.
 */
static void _rcu_barrier(struct rcu_state *rsp)
{
	int cpu;
	struct rcu_data *rdp;
	unsigned long s = rcu_seq_snap(&rsp->barrier_sequence);

	_rcu_barrier_trace(rsp, TPS("Begin"), -1, s);

	/* Take mutex to serialize concurrent rcu_barrier() requests. */
	mutex_lock(&rsp->barrier_mutex);

	/* Did someone else do our work for us? */
	if (rcu_seq_done(&rsp->barrier_sequence, s)) {
		_rcu_barrier_trace(rsp, TPS("EarlyExit"), -1,
				   rsp->barrier_sequence);
		smp_mb(); /* caller's subsequent code after above check. */
		mutex_unlock(&rsp->barrier_mutex);
		return;
	}

	/* Mark the start of the barrier operation. */
	rcu_seq_start(&rsp->barrier_sequence);
	_rcu_barrier_trace(rsp, TPS("Inc1"), -1, rsp->barrier_sequence);

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
			if (!rcu_nocb_cpu_needs_barrier(rsp, cpu)) {
				_rcu_barrier_trace(rsp, TPS("OfflineNoCB"), cpu,
						   rsp->barrier_sequence);
			} else {
				_rcu_barrier_trace(rsp, TPS("OnlineNoCB"), cpu,
						   rsp->barrier_sequence);
				smp_mb__before_atomic();
				atomic_inc(&rsp->barrier_cpu_count);
				__call_rcu(&rdp->barrier_head,
					   rcu_barrier_callback, rsp, cpu, 0);
			}
		} else if (rcu_segcblist_n_cbs(&rdp->cblist)) {
			_rcu_barrier_trace(rsp, TPS("OnlineQ"), cpu,
					   rsp->barrier_sequence);
			smp_call_function_single(cpu, rcu_barrier_func, rsp, 1);
		} else {
			_rcu_barrier_trace(rsp, TPS("OnlineNQ"), cpu,
					   rsp->barrier_sequence);
		}
	}
	put_online_cpus();

	/*
	 * Now that we have an rcu_barrier_callback() callback on each
	 * CPU, and thus each counted, remove the initial count.
	 */
	if (atomic_dec_and_test(&rsp->barrier_cpu_count))
		complete(&rsp->barrier_completion);

	/* Wait for all rcu_barrier_callback() callbacks to be invoked. */
	wait_for_completion(&rsp->barrier_completion);

	/* Mark the end of the barrier operation. */
	_rcu_barrier_trace(rsp, TPS("Inc2"), -1, rsp->barrier_sequence);
	rcu_seq_end(&rsp->barrier_sequence);

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
 * Propagate ->qsinitmask bits up the rcu_node tree to account for the
 * first CPU in a given leaf rcu_node structure coming online.  The caller
 * must hold the corresponding leaf rcu_node ->lock with interrrupts
 * disabled.
 */
static void rcu_init_new_rnp(struct rcu_node *rnp_leaf)
{
	long mask;
	struct rcu_node *rnp = rnp_leaf;

	lockdep_assert_held(&rnp->lock);
	for (;;) {
		mask = rnp->grpmask;
		rnp = rnp->parent;
		if (rnp == NULL)
			return;
		raw_spin_lock_rcu_node(rnp); /* Interrupts already disabled. */
		rnp->qsmaskinit |= mask;
		raw_spin_unlock_rcu_node(rnp); /* Interrupts remain disabled. */
	}
}

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
	raw_spin_lock_irqsave_rcu_node(rnp, flags);
	rdp->grpmask = leaf_node_cpu_bit(rdp->mynode, cpu);
	rdp->dynticks = &per_cpu(rcu_dynticks, cpu);
	WARN_ON_ONCE(rdp->dynticks->dynticks_nesting != DYNTICK_TASK_EXIT_IDLE);
	WARN_ON_ONCE(rcu_dynticks_in_eqs(rcu_dynticks_snap(rdp->dynticks)));
	rdp->cpu = cpu;
	rdp->rsp = rsp;
	rcu_boot_init_nocb_percpu_data(rdp);
	raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
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
	struct rcu_data *rdp = per_cpu_ptr(rsp->rda, cpu);
	struct rcu_node *rnp = rcu_get_root(rsp);

	/* Set up local state, ensuring consistent view of global state. */
	raw_spin_lock_irqsave_rcu_node(rnp, flags);
	rdp->qlen_last_fqs_check = 0;
	rdp->n_force_qs_snap = rsp->n_force_qs;
	rdp->blimit = blimit;
	if (rcu_segcblist_empty(&rdp->cblist) && /* No early-boot CBs? */
	    !init_nocb_callback_list(rdp))
		rcu_segcblist_init(&rdp->cblist);  /* Re-enable callbacks. */
	rdp->dynticks->dynticks_nesting = DYNTICK_TASK_EXIT_IDLE;
	rcu_dynticks_eqs_online();
	raw_spin_unlock_rcu_node(rnp);		/* irqs remain disabled. */

	/*
	 * Add CPU to leaf rcu_node pending-online bitmask.  Any needed
	 * propagation up the rcu_node tree will happen at the beginning
	 * of the next grace period.
	 */
	rnp = rdp->mynode;
	raw_spin_lock_rcu_node(rnp);		/* irqs already disabled. */
	rdp->beenonline = true;	 /* We have now been online. */
	rdp->gpnum = rnp->completed; /* Make CPU later note any new GP. */
	rdp->completed = rnp->completed;
	rdp->cpu_no_qs.b.norm = true;
	rdp->rcu_qs_ctr_snap = per_cpu(rcu_dynticks.rcu_qs_ctr, cpu);
	rdp->core_needs_qs = false;
	trace_rcu_grace_period(rsp->name, rdp->gpnum, TPS("cpuonl"));
	raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
}

/*
 * Invoked early in the CPU-online process, when pretty much all
 * services are available.  The incoming CPU is not present.
 */
int rcutree_prepare_cpu(unsigned int cpu)
{
	struct rcu_state *rsp;

	for_each_rcu_flavor(rsp)
		rcu_init_percpu_data(cpu, rsp);

	rcu_prepare_kthreads(cpu);
	rcu_spawn_all_nocb_kthreads(cpu);

	return 0;
}

/*
 * Update RCU priority boot kthread affinity for CPU-hotplug changes.
 */
static void rcutree_affinity_setting(unsigned int cpu, int outgoing)
{
	struct rcu_data *rdp = per_cpu_ptr(rcu_state_p->rda, cpu);

	rcu_boost_kthread_setaffinity(rdp->mynode, outgoing);
}

/*
 * Near the end of the CPU-online process.  Pretty much all services
 * enabled, and the CPU is now very much alive.
 */
int rcutree_online_cpu(unsigned int cpu)
{
	sync_sched_exp_online_cleanup(cpu);
	rcutree_affinity_setting(cpu, -1);
	if (IS_ENABLED(CONFIG_TREE_SRCU))
		srcu_online_cpu(cpu);
	return 0;
}

/*
 * Near the beginning of the process.  The CPU is still very much alive
 * with pretty much all services enabled.
 */
int rcutree_offline_cpu(unsigned int cpu)
{
	rcutree_affinity_setting(cpu, cpu);
	if (IS_ENABLED(CONFIG_TREE_SRCU))
		srcu_offline_cpu(cpu);
	return 0;
}

/*
 * Near the end of the offline process.  We do only tracing here.
 */
int rcutree_dying_cpu(unsigned int cpu)
{
	struct rcu_state *rsp;

	for_each_rcu_flavor(rsp)
		rcu_cleanup_dying_cpu(rsp);
	return 0;
}

/*
 * The outgoing CPU is gone and we are running elsewhere.
 */
int rcutree_dead_cpu(unsigned int cpu)
{
	struct rcu_state *rsp;

	for_each_rcu_flavor(rsp) {
		rcu_cleanup_dead_cpu(cpu, rsp);
		do_nocb_deferred_wakeup(per_cpu_ptr(rsp->rda, cpu));
	}
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
 */
void rcu_cpu_starting(unsigned int cpu)
{
	unsigned long flags;
	unsigned long mask;
	int nbits;
	unsigned long oldmask;
	struct rcu_data *rdp;
	struct rcu_node *rnp;
	struct rcu_state *rsp;

	for_each_rcu_flavor(rsp) {
		rdp = per_cpu_ptr(rsp->rda, cpu);
		rnp = rdp->mynode;
		mask = rdp->grpmask;
		raw_spin_lock_irqsave_rcu_node(rnp, flags);
		rnp->qsmaskinitnext |= mask;
		oldmask = rnp->expmaskinitnext;
		rnp->expmaskinitnext |= mask;
		oldmask ^= rnp->expmaskinitnext;
		nbits = bitmap_weight(&oldmask, BITS_PER_LONG);
		/* Allow lockless access for expedited grace periods. */
		smp_store_release(&rsp->ncpus, rsp->ncpus + nbits); /* ^^^ */
		raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
	}
	smp_mb(); /* Ensure RCU read-side usage follows above initialization. */
}

#ifdef CONFIG_HOTPLUG_CPU
/*
 * The CPU is exiting the idle loop into the arch_cpu_idle_dead()
 * function.  We now remove it from the rcu_node tree's ->qsmaskinit
 * bit masks.
 */
static void rcu_cleanup_dying_idle_cpu(int cpu, struct rcu_state *rsp)
{
	unsigned long flags;
	unsigned long mask;
	struct rcu_data *rdp = per_cpu_ptr(rsp->rda, cpu);
	struct rcu_node *rnp = rdp->mynode;  /* Outgoing CPU's rdp & rnp. */

	/* Remove outgoing CPU from mask in the leaf rcu_node structure. */
	mask = rdp->grpmask;
	raw_spin_lock_irqsave_rcu_node(rnp, flags); /* Enforce GP memory-order guarantee. */
	rnp->qsmaskinitnext &= ~mask;
	raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
}

/*
 * The outgoing function has no further need of RCU, so remove it from
 * the list of CPUs that RCU must track.
 *
 * Note that this function is special in that it is invoked directly
 * from the outgoing CPU rather than from the cpuhp_step mechanism.
 * This is because this function must be invoked at a precise location.
 */
void rcu_report_dead(unsigned int cpu)
{
	struct rcu_state *rsp;

	/* QS for any half-done expedited RCU-sched GP. */
	preempt_disable();
	rcu_report_exp_rdp(&rcu_sched_state,
			   this_cpu_ptr(rcu_sched_state.rda), true);
	preempt_enable();
	for_each_rcu_flavor(rsp)
		rcu_cleanup_dying_idle_cpu(cpu, rsp);
}

/* Migrate the dead CPU's callbacks to the current CPU. */
static void rcu_migrate_callbacks(int cpu, struct rcu_state *rsp)
{
	unsigned long flags;
	struct rcu_data *my_rdp;
	struct rcu_data *rdp = per_cpu_ptr(rsp->rda, cpu);
	struct rcu_node *rnp_root = rcu_get_root(rdp->rsp);

	if (rcu_is_nocb_cpu(cpu) || rcu_segcblist_empty(&rdp->cblist))
		return;  /* No callbacks to migrate. */

	local_irq_save(flags);
	my_rdp = this_cpu_ptr(rsp->rda);
	if (rcu_nocb_adopt_orphan_cbs(my_rdp, rdp, flags)) {
		local_irq_restore(flags);
		return;
	}
	raw_spin_lock_rcu_node(rnp_root); /* irqs already disabled. */
	rcu_advance_cbs(rsp, rnp_root, rdp); /* Leverage recent GPs. */
	rcu_advance_cbs(rsp, rnp_root, my_rdp); /* Assign GP to pending CBs. */
	rcu_segcblist_merge(&my_rdp->cblist, &rdp->cblist);
	WARN_ON_ONCE(rcu_segcblist_empty(&my_rdp->cblist) !=
		     !rcu_segcblist_n_cbs(&my_rdp->cblist));
	raw_spin_unlock_irqrestore_rcu_node(rnp_root, flags);
	WARN_ONCE(rcu_segcblist_n_cbs(&rdp->cblist) != 0 ||
		  !rcu_segcblist_empty(&rdp->cblist),
		  "rcu_cleanup_dead_cpu: Callbacks on offline CPU %d: qlen=%lu, 1stCB=%p\n",
		  cpu, rcu_segcblist_n_cbs(&rdp->cblist),
		  rcu_segcblist_first_cb(&rdp->cblist));
}

/*
 * The outgoing CPU has just passed through the dying-idle state,
 * and we are being invoked from the CPU that was IPIed to continue the
 * offline operation.  We need to migrate the outgoing CPU's callbacks.
 */
void rcutree_migrate_callbacks(int cpu)
{
	struct rcu_state *rsp;

	for_each_rcu_flavor(rsp)
		rcu_migrate_callbacks(cpu, rsp);
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
		if (nr_cpu_ids <= 256) /* Expediting bad for large systems. */
			rcu_expedite_gp();
		break;
	case PM_POST_HIBERNATION:
	case PM_POST_SUSPEND:
		if (nr_cpu_ids <= 256) /* Expediting bad for large systems. */
			rcu_unexpedite_gp();
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

/*
 * Spawn the kthreads that handle each RCU flavor's grace periods.
 */
static int __init rcu_spawn_gp_kthread(void)
{
	unsigned long flags;
	int kthread_prio_in = kthread_prio;
	struct rcu_node *rnp;
	struct rcu_state *rsp;
	struct sched_param sp;
	struct task_struct *t;

	/* Force priority into range. */
	if (IS_ENABLED(CONFIG_RCU_BOOST) && kthread_prio < 1)
		kthread_prio = 1;
	else if (kthread_prio < 0)
		kthread_prio = 0;
	else if (kthread_prio > 99)
		kthread_prio = 99;
	if (kthread_prio != kthread_prio_in)
		pr_alert("rcu_spawn_gp_kthread(): Limited prio to %d from %d\n",
			 kthread_prio, kthread_prio_in);

	rcu_scheduler_fully_active = 1;
	for_each_rcu_flavor(rsp) {
		t = kthread_create(rcu_gp_kthread, rsp, "%s", rsp->name);
		BUG_ON(IS_ERR(t));
		rnp = rcu_get_root(rsp);
		raw_spin_lock_irqsave_rcu_node(rnp, flags);
		rsp->gp_kthread = t;
		if (kthread_prio) {
			sp.sched_priority = kthread_prio;
			sched_setscheduler_nocheck(t, SCHED_FIFO, &sp);
		}
		raw_spin_unlock_irqrestore_rcu_node(rnp, flags);
		wake_up_process(t);
	}
	rcu_spawn_nocb_kthreads();
	rcu_spawn_boost_kthreads();
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
	WARN_ON(num_online_cpus() != 1);
	WARN_ON(nr_context_switches() > 0);
	rcu_test_sync_prims();
	rcu_scheduler_active = RCU_SCHEDULER_INIT;
	rcu_test_sync_prims();
}

/*
 * Helper function for rcu_init() that initializes one rcu_state structure.
 */
static void __init rcu_init_one(struct rcu_state *rsp)
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
		rsp->level[i] = rsp->level[i - 1] + num_rcu_lvl[i - 1];
	rcu_init_levelspread(levelspread, num_rcu_lvl);

	/* Initialize the elements themselves, starting from the leaves. */

	for (i = rcu_num_lvls - 1; i >= 0; i--) {
		cpustride *= levelspread[i];
		rnp = rsp->level[i];
		for (j = 0; j < num_rcu_lvl[i]; j++, rnp++) {
			raw_spin_lock_init(&ACCESS_PRIVATE(rnp, lock));
			lockdep_set_class_and_name(&ACCESS_PRIVATE(rnp, lock),
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
				rnp->grpnum = j % levelspread[i - 1];
				rnp->grpmask = 1UL << rnp->grpnum;
				rnp->parent = rsp->level[i - 1] +
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
		}
	}

	init_swait_queue_head(&rsp->gp_wq);
	init_swait_queue_head(&rsp->expedited_wq);
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
	int rcu_capacity[RCU_NUM_LVLS];

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
	if (rcu_fanout_leaf == RCU_FANOUT_LEAF &&
	    nr_cpu_ids == NR_CPUS)
		return;
	pr_info("RCU: Adjusting geometry for rcu_fanout_leaf=%d, nr_cpu_ids=%d\n",
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
 * with the rcu_state structure referenced by rsp.
 */
static void __init rcu_dump_rcu_node_tree(struct rcu_state *rsp)
{
	int level = 0;
	struct rcu_node *rnp;

	pr_info("rcu_node tree layout dump\n");
	pr_info(" ");
	rcu_for_each_node_breadth_first(rsp, rnp) {
		if (rnp->level != level) {
			pr_cont("\n");
			pr_info(" ");
			level = rnp->level;
		}
		pr_cont("%d:%d ^%d  ", rnp->grplo, rnp->grphi, rnp->grpnum);
	}
	pr_cont("\n");
}

void __init rcu_init(void)
{
	int cpu;

	rcu_early_boot_tests();

	rcu_bootup_announce();
	rcu_init_geometry();
	rcu_init_one(&rcu_bh_state);
	rcu_init_one(&rcu_sched_state);
	if (dump_tree)
		rcu_dump_rcu_node_tree(&rcu_sched_state);
	__rcu_init_preempt();
	open_softirq(RCU_SOFTIRQ, rcu_process_callbacks);

	/*
	 * We don't need protection against CPU-hotplug here because
	 * this is called early in boot, before either interrupts
	 * or the scheduler are operational.
	 */
	pm_notifier(rcu_pm_notify, 0);
	for_each_online_cpu(cpu) {
		rcutree_prepare_cpu(cpu);
		rcu_cpu_starting(cpu);
		if (IS_ENABLED(CONFIG_TREE_SRCU))
			srcu_online_cpu(cpu);
	}
}

#include "tree_exp.h"
#include "tree_plugin.h"
