// SPDX-License-Identifier: GPL-2.0+
/*
 * Read-Copy Update mechanism for mutual exclusion
 *
 * Copyright IBM Corporation, 2001
 *
 * Authors: Dipankar Sarma <dipankar@in.ibm.com>
 *	    Manfred Spraul <manfred@colorfullife.com>
 *
 * Based on the original work by Paul McKenney <paulmck@linux.ibm.com>
 * and inputs from Rusty Russell, Andrea Arcangeli and Andi Kleen.
 * Papers:
 * http://www.rdrop.com/users/paulmck/paper/rclockpdcsproof.pdf
 * http://lse.sourceforge.net/locking/rclock_OLS.2001.05.01c.sc.pdf (OLS2001)
 *
 * For detailed explanation of Read-Copy Update mechanism see -
 *		http://lse.sourceforge.net/locking/rcupdate.html
 *
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/sched/signal.h>
#include <linux/sched/debug.h>
#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/mutex.h>
#include <linux/export.h>
#include <linux/hardirq.h>
#include <linux/delay.h>
#include <linux/moduleparam.h>
#include <linux/kthread.h>
#include <linux/tick.h>
#include <linux/rcupdate_wait.h>
#include <linux/sched/isolation.h>
#include <linux/kprobes.h>
#include <linux/slab.h>

#define CREATE_TRACE_POINTS

#include "rcu.h"

#ifdef MODULE_PARAM_PREFIX
#undef MODULE_PARAM_PREFIX
#endif
#define MODULE_PARAM_PREFIX "rcupdate."

#ifndef CONFIG_TINY_RCU
module_param(rcu_expedited, int, 0);
module_param(rcu_normal, int, 0);
static int rcu_normal_after_boot;
module_param(rcu_normal_after_boot, int, 0);
#endif /* #ifndef CONFIG_TINY_RCU */

#ifdef CONFIG_DEBUG_LOCK_ALLOC
/**
 * rcu_read_lock_held_common() - might we be in RCU-sched read-side critical section?
 * @ret:	Best guess answer if lockdep cannot be relied on
 *
 * Returns true if lockdep must be ignored, in which case *ret contains
 * the best guess described below.  Otherwise returns false, in which
 * case *ret tells the caller nothing and the caller should instead
 * consult lockdep.
 *
 * If CONFIG_DEBUG_LOCK_ALLOC is selected, set *ret to nonzero iff in an
 * RCU-sched read-side critical section.  In absence of
 * CONFIG_DEBUG_LOCK_ALLOC, this assumes we are in an RCU-sched read-side
 * critical section unless it can prove otherwise.  Note that disabling
 * of preemption (including disabling irqs) counts as an RCU-sched
 * read-side critical section.  This is useful for debug checks in functions
 * that required that they be called within an RCU-sched read-side
 * critical section.
 *
 * Check debug_lockdep_rcu_enabled() to prevent false positives during boot
 * and while lockdep is disabled.
 *
 * Note that if the CPU is in the idle loop from an RCU point of view (ie:
 * that we are in the section between rcu_idle_enter() and rcu_idle_exit())
 * then rcu_read_lock_held() sets *ret to false even if the CPU did an
 * rcu_read_lock().  The reason for this is that RCU ignores CPUs that are
 * in such a section, considering these as in extended quiescent state,
 * so such a CPU is effectively never in an RCU read-side critical section
 * regardless of what RCU primitives it invokes.  This state of affairs is
 * required --- we need to keep an RCU-free window in idle where the CPU may
 * possibly enter into low power mode. This way we can notice an extended
 * quiescent state to other CPUs that started a grace period. Otherwise
 * we would delay any grace period as long as we run in the idle task.
 *
 * Similarly, we avoid claiming an RCU read lock held if the current
 * CPU is offline.
 */
static bool rcu_read_lock_held_common(bool *ret)
{
	if (!debug_lockdep_rcu_enabled()) {
		*ret = 1;
		return true;
	}
	if (!rcu_is_watching()) {
		*ret = 0;
		return true;
	}
	if (!rcu_lockdep_current_cpu_online()) {
		*ret = 0;
		return true;
	}
	return false;
}

int rcu_read_lock_sched_held(void)
{
	bool ret;

	if (rcu_read_lock_held_common(&ret))
		return ret;
	return lock_is_held(&rcu_sched_lock_map) || !preemptible();
}
EXPORT_SYMBOL(rcu_read_lock_sched_held);
#endif

#ifndef CONFIG_TINY_RCU

/*
 * Should expedited grace-period primitives always fall back to their
 * non-expedited counterparts?  Intended for use within RCU.  Note
 * that if the user specifies both rcu_expedited and rcu_normal, then
 * rcu_normal wins.  (Except during the time period during boot from
 * when the first task is spawned until the rcu_set_runtime_mode()
 * core_initcall() is invoked, at which point everything is expedited.)
 */
bool rcu_gp_is_normal(void)
{
	return READ_ONCE(rcu_normal) &&
	       rcu_scheduler_active != RCU_SCHEDULER_INIT;
}
EXPORT_SYMBOL_GPL(rcu_gp_is_normal);

static atomic_t rcu_expedited_nesting = ATOMIC_INIT(1);

/*
 * Should normal grace-period primitives be expedited?  Intended for
 * use within RCU.  Note that this function takes the rcu_expedited
 * sysfs/boot variable and rcu_scheduler_active into account as well
 * as the rcu_expedite_gp() nesting.  So looping on rcu_unexpedite_gp()
 * until rcu_gp_is_expedited() returns false is a -really- bad idea.
 */
bool rcu_gp_is_expedited(void)
{
	return rcu_expedited || atomic_read(&rcu_expedited_nesting);
}
EXPORT_SYMBOL_GPL(rcu_gp_is_expedited);

/**
 * rcu_expedite_gp - Expedite future RCU grace periods
 *
 * After a call to this function, future calls to synchronize_rcu() and
 * friends act as the corresponding synchronize_rcu_expedited() function
 * had instead been called.
 */
void rcu_expedite_gp(void)
{
	atomic_inc(&rcu_expedited_nesting);
}
EXPORT_SYMBOL_GPL(rcu_expedite_gp);

/**
 * rcu_unexpedite_gp - Cancel prior rcu_expedite_gp() invocation
 *
 * Undo a prior call to rcu_expedite_gp().  If all prior calls to
 * rcu_expedite_gp() are undone by a subsequent call to rcu_unexpedite_gp(),
 * and if the rcu_expedited sysfs/boot parameter is not set, then all
 * subsequent calls to synchronize_rcu() and friends will return to
 * their normal non-expedited behavior.
 */
void rcu_unexpedite_gp(void)
{
	atomic_dec(&rcu_expedited_nesting);
}
EXPORT_SYMBOL_GPL(rcu_unexpedite_gp);

static bool rcu_boot_ended __read_mostly;

/*
 * Inform RCU of the end of the in-kernel boot sequence.
 */
void rcu_end_inkernel_boot(void)
{
	rcu_unexpedite_gp();
	if (rcu_normal_after_boot)
		WRITE_ONCE(rcu_normal, 1);
	rcu_boot_ended = 1;
}

/*
 * Let rcutorture know when it is OK to turn it up to eleven.
 */
bool rcu_inkernel_boot_has_ended(void)
{
	return rcu_boot_ended;
}
EXPORT_SYMBOL_GPL(rcu_inkernel_boot_has_ended);

#endif /* #ifndef CONFIG_TINY_RCU */

/*
 * Test each non-SRCU synchronous grace-period wait API.  This is
 * useful just after a change in mode for these primitives, and
 * during early boot.
 */
void rcu_test_sync_prims(void)
{
	if (!IS_ENABLED(CONFIG_PROVE_RCU))
		return;
	synchronize_rcu();
	synchronize_rcu_expedited();
}

#if !defined(CONFIG_TINY_RCU) || defined(CONFIG_SRCU)

/*
 * Switch to run-time mode once RCU has fully initialized.
 */
static int __init rcu_set_runtime_mode(void)
{
	rcu_test_sync_prims();
	rcu_scheduler_active = RCU_SCHEDULER_RUNNING;
	kfree_rcu_scheduler_running();
	rcu_test_sync_prims();
	return 0;
}
core_initcall(rcu_set_runtime_mode);

#endif /* #if !defined(CONFIG_TINY_RCU) || defined(CONFIG_SRCU) */

#ifdef CONFIG_DEBUG_LOCK_ALLOC
static struct lock_class_key rcu_lock_key;
struct lockdep_map rcu_lock_map =
	STATIC_LOCKDEP_MAP_INIT("rcu_read_lock", &rcu_lock_key);
EXPORT_SYMBOL_GPL(rcu_lock_map);

static struct lock_class_key rcu_bh_lock_key;
struct lockdep_map rcu_bh_lock_map =
	STATIC_LOCKDEP_MAP_INIT("rcu_read_lock_bh", &rcu_bh_lock_key);
EXPORT_SYMBOL_GPL(rcu_bh_lock_map);

static struct lock_class_key rcu_sched_lock_key;
struct lockdep_map rcu_sched_lock_map =
	STATIC_LOCKDEP_MAP_INIT("rcu_read_lock_sched", &rcu_sched_lock_key);
EXPORT_SYMBOL_GPL(rcu_sched_lock_map);

static struct lock_class_key rcu_callback_key;
struct lockdep_map rcu_callback_map =
	STATIC_LOCKDEP_MAP_INIT("rcu_callback", &rcu_callback_key);
EXPORT_SYMBOL_GPL(rcu_callback_map);

int notrace debug_lockdep_rcu_enabled(void)
{
	return rcu_scheduler_active != RCU_SCHEDULER_INACTIVE && debug_locks &&
	       current->lockdep_recursion == 0;
}
EXPORT_SYMBOL_GPL(debug_lockdep_rcu_enabled);
NOKPROBE_SYMBOL(debug_lockdep_rcu_enabled);

/**
 * rcu_read_lock_held() - might we be in RCU read-side critical section?
 *
 * If CONFIG_DEBUG_LOCK_ALLOC is selected, returns nonzero iff in an RCU
 * read-side critical section.  In absence of CONFIG_DEBUG_LOCK_ALLOC,
 * this assumes we are in an RCU read-side critical section unless it can
 * prove otherwise.  This is useful for debug checks in functions that
 * require that they be called within an RCU read-side critical section.
 *
 * Checks debug_lockdep_rcu_enabled() to prevent false positives during boot
 * and while lockdep is disabled.
 *
 * Note that rcu_read_lock() and the matching rcu_read_unlock() must
 * occur in the same context, for example, it is illegal to invoke
 * rcu_read_unlock() in process context if the matching rcu_read_lock()
 * was invoked from within an irq handler.
 *
 * Note that rcu_read_lock() is disallowed if the CPU is either idle or
 * offline from an RCU perspective, so check for those as well.
 */
int rcu_read_lock_held(void)
{
	bool ret;

	if (rcu_read_lock_held_common(&ret))
		return ret;
	return lock_is_held(&rcu_lock_map);
}
EXPORT_SYMBOL_GPL(rcu_read_lock_held);

/**
 * rcu_read_lock_bh_held() - might we be in RCU-bh read-side critical section?
 *
 * Check for bottom half being disabled, which covers both the
 * CONFIG_PROVE_RCU and not cases.  Note that if someone uses
 * rcu_read_lock_bh(), but then later enables BH, lockdep (if enabled)
 * will show the situation.  This is useful for debug checks in functions
 * that require that they be called within an RCU read-side critical
 * section.
 *
 * Check debug_lockdep_rcu_enabled() to prevent false positives during boot.
 *
 * Note that rcu_read_lock_bh() is disallowed if the CPU is either idle or
 * offline from an RCU perspective, so check for those as well.
 */
int rcu_read_lock_bh_held(void)
{
	bool ret;

	if (rcu_read_lock_held_common(&ret))
		return ret;
	return in_softirq() || irqs_disabled();
}
EXPORT_SYMBOL_GPL(rcu_read_lock_bh_held);

int rcu_read_lock_any_held(void)
{
	bool ret;

	if (rcu_read_lock_held_common(&ret))
		return ret;
	if (lock_is_held(&rcu_lock_map) ||
	    lock_is_held(&rcu_bh_lock_map) ||
	    lock_is_held(&rcu_sched_lock_map))
		return 1;
	return !preemptible();
}
EXPORT_SYMBOL_GPL(rcu_read_lock_any_held);

#endif /* #ifdef CONFIG_DEBUG_LOCK_ALLOC */

/**
 * wakeme_after_rcu() - Callback function to awaken a task after grace period
 * @head: Pointer to rcu_head member within rcu_synchronize structure
 *
 * Awaken the corresponding task now that a grace period has elapsed.
 */
void wakeme_after_rcu(struct rcu_head *head)
{
	struct rcu_synchronize *rcu;

	rcu = container_of(head, struct rcu_synchronize, head);
	complete(&rcu->completion);
}
EXPORT_SYMBOL_GPL(wakeme_after_rcu);

void __wait_rcu_gp(bool checktiny, int n, call_rcu_func_t *crcu_array,
		   struct rcu_synchronize *rs_array)
{
	int i;
	int j;

	/* Initialize and register callbacks for each crcu_array element. */
	for (i = 0; i < n; i++) {
		if (checktiny &&
		    (crcu_array[i] == call_rcu)) {
			might_sleep();
			continue;
		}
		init_rcu_head_on_stack(&rs_array[i].head);
		init_completion(&rs_array[i].completion);
		for (j = 0; j < i; j++)
			if (crcu_array[j] == crcu_array[i])
				break;
		if (j == i)
			(crcu_array[i])(&rs_array[i].head, wakeme_after_rcu);
	}

	/* Wait for all callbacks to be invoked. */
	for (i = 0; i < n; i++) {
		if (checktiny &&
		    (crcu_array[i] == call_rcu))
			continue;
		for (j = 0; j < i; j++)
			if (crcu_array[j] == crcu_array[i])
				break;
		if (j == i)
			wait_for_completion(&rs_array[i].completion);
		destroy_rcu_head_on_stack(&rs_array[i].head);
	}
}
EXPORT_SYMBOL_GPL(__wait_rcu_gp);

#ifdef CONFIG_DEBUG_OBJECTS_RCU_HEAD
void init_rcu_head(struct rcu_head *head)
{
	debug_object_init(head, &rcuhead_debug_descr);
}
EXPORT_SYMBOL_GPL(init_rcu_head);

void destroy_rcu_head(struct rcu_head *head)
{
	debug_object_free(head, &rcuhead_debug_descr);
}
EXPORT_SYMBOL_GPL(destroy_rcu_head);

static bool rcuhead_is_static_object(void *addr)
{
	return true;
}

/**
 * init_rcu_head_on_stack() - initialize on-stack rcu_head for debugobjects
 * @head: pointer to rcu_head structure to be initialized
 *
 * This function informs debugobjects of a new rcu_head structure that
 * has been allocated as an auto variable on the stack.  This function
 * is not required for rcu_head structures that are statically defined or
 * that are dynamically allocated on the heap.  This function has no
 * effect for !CONFIG_DEBUG_OBJECTS_RCU_HEAD kernel builds.
 */
void init_rcu_head_on_stack(struct rcu_head *head)
{
	debug_object_init_on_stack(head, &rcuhead_debug_descr);
}
EXPORT_SYMBOL_GPL(init_rcu_head_on_stack);

/**
 * destroy_rcu_head_on_stack() - destroy on-stack rcu_head for debugobjects
 * @head: pointer to rcu_head structure to be initialized
 *
 * This function informs debugobjects that an on-stack rcu_head structure
 * is about to go out of scope.  As with init_rcu_head_on_stack(), this
 * function is not required for rcu_head structures that are statically
 * defined or that are dynamically allocated on the heap.  Also as with
 * init_rcu_head_on_stack(), this function has no effect for
 * !CONFIG_DEBUG_OBJECTS_RCU_HEAD kernel builds.
 */
void destroy_rcu_head_on_stack(struct rcu_head *head)
{
	debug_object_free(head, &rcuhead_debug_descr);
}
EXPORT_SYMBOL_GPL(destroy_rcu_head_on_stack);

struct debug_obj_descr rcuhead_debug_descr = {
	.name = "rcu_head",
	.is_static_object = rcuhead_is_static_object,
};
EXPORT_SYMBOL_GPL(rcuhead_debug_descr);
#endif /* #ifdef CONFIG_DEBUG_OBJECTS_RCU_HEAD */

#if defined(CONFIG_TREE_RCU) || defined(CONFIG_RCU_TRACE)
void do_trace_rcu_torture_read(const char *rcutorturename, struct rcu_head *rhp,
			       unsigned long secs,
			       unsigned long c_old, unsigned long c)
{
	trace_rcu_torture_read(rcutorturename, rhp, secs, c_old, c);
}
EXPORT_SYMBOL_GPL(do_trace_rcu_torture_read);
#else
#define do_trace_rcu_torture_read(rcutorturename, rhp, secs, c_old, c) \
	do { } while (0)
#endif

#if IS_ENABLED(CONFIG_RCU_TORTURE_TEST) || IS_MODULE(CONFIG_RCU_TORTURE_TEST)
/* Get rcutorture access to sched_setaffinity(). */
long rcutorture_sched_setaffinity(pid_t pid, const struct cpumask *in_mask)
{
	int ret;

	ret = sched_setaffinity(pid, in_mask);
	WARN_ONCE(ret, "%s: sched_setaffinity() returned %d\n", __func__, ret);
	return ret;
}
EXPORT_SYMBOL_GPL(rcutorture_sched_setaffinity);
#endif

#ifdef CONFIG_RCU_STALL_COMMON
int rcu_cpu_stall_ftrace_dump __read_mostly;
module_param(rcu_cpu_stall_ftrace_dump, int, 0644);
int rcu_cpu_stall_suppress __read_mostly; // !0 = suppress stall warnings.
EXPORT_SYMBOL_GPL(rcu_cpu_stall_suppress);
module_param(rcu_cpu_stall_suppress, int, 0644);
int rcu_cpu_stall_timeout __read_mostly = CONFIG_RCU_CPU_STALL_TIMEOUT;
module_param(rcu_cpu_stall_timeout, int, 0644);
#endif /* #ifdef CONFIG_RCU_STALL_COMMON */

// Suppress boot-time RCU CPU stall warnings and rcutorture writer stall
// warnings.  Also used by rcutorture even if stall warnings are excluded.
int rcu_cpu_stall_suppress_at_boot __read_mostly; // !0 = suppress boot stalls.
EXPORT_SYMBOL_GPL(rcu_cpu_stall_suppress_at_boot);
module_param(rcu_cpu_stall_suppress_at_boot, int, 0444);

#ifdef CONFIG_TASKS_RCU

/*
 * Simple variant of RCU whose quiescent states are voluntary context
 * switch, cond_resched_rcu_qs(), user-space execution, and idle.
 * As such, grace periods can take one good long time.  There are no
 * read-side primitives similar to rcu_read_lock() and rcu_read_unlock()
 * because this implementation is intended to get the system into a safe
 * state for some of the manipulations involved in tracing and the like.
 * Finally, this implementation does not support high call_rcu_tasks()
 * rates from multiple CPUs.  If this is required, per-CPU callback lists
 * will be needed.
 */

/* Global list of callbacks and associated lock. */
static struct rcu_head *rcu_tasks_cbs_head;
static struct rcu_head **rcu_tasks_cbs_tail = &rcu_tasks_cbs_head;
static DECLARE_WAIT_QUEUE_HEAD(rcu_tasks_cbs_wq);
static DEFINE_RAW_SPINLOCK(rcu_tasks_cbs_lock);

/* Track exiting tasks in order to allow them to be waited for. */
DEFINE_STATIC_SRCU(tasks_rcu_exit_srcu);

/* Control stall timeouts.  Disable with <= 0, otherwise jiffies till stall. */
#define RCU_TASK_STALL_TIMEOUT (HZ * 60 * 10)
static int rcu_task_stall_timeout __read_mostly = RCU_TASK_STALL_TIMEOUT;
module_param(rcu_task_stall_timeout, int, 0644);

static struct task_struct *rcu_tasks_kthread_ptr;

/**
 * call_rcu_tasks() - Queue an RCU for invocation task-based grace period
 * @rhp: structure to be used for queueing the RCU updates.
 * @func: actual callback function to be invoked after the grace period
 *
 * The callback function will be invoked some time after a full grace
 * period elapses, in other words after all currently executing RCU
 * read-side critical sections have completed. call_rcu_tasks() assumes
 * that the read-side critical sections end at a voluntary context
 * switch (not a preemption!), cond_resched_rcu_qs(), entry into idle,
 * or transition to usermode execution.  As such, there are no read-side
 * primitives analogous to rcu_read_lock() and rcu_read_unlock() because
 * this primitive is intended to determine that all tasks have passed
 * through a safe state, not so much for data-strcuture synchronization.
 *
 * See the description of call_rcu() for more detailed information on
 * memory ordering guarantees.
 */
void call_rcu_tasks(struct rcu_head *rhp, rcu_callback_t func)
{
	unsigned long flags;
	bool needwake;

	rhp->next = NULL;
	rhp->func = func;
	raw_spin_lock_irqsave(&rcu_tasks_cbs_lock, flags);
	needwake = !rcu_tasks_cbs_head;
	WRITE_ONCE(*rcu_tasks_cbs_tail, rhp);
	rcu_tasks_cbs_tail = &rhp->next;
	raw_spin_unlock_irqrestore(&rcu_tasks_cbs_lock, flags);
	/* We can't create the thread unless interrupts are enabled. */
	if (needwake && READ_ONCE(rcu_tasks_kthread_ptr))
		wake_up(&rcu_tasks_cbs_wq);
}
EXPORT_SYMBOL_GPL(call_rcu_tasks);

/**
 * synchronize_rcu_tasks - wait until an rcu-tasks grace period has elapsed.
 *
 * Control will return to the caller some time after a full rcu-tasks
 * grace period has elapsed, in other words after all currently
 * executing rcu-tasks read-side critical sections have elapsed.  These
 * read-side critical sections are delimited by calls to schedule(),
 * cond_resched_tasks_rcu_qs(), idle execution, userspace execution, calls
 * to synchronize_rcu_tasks(), and (in theory, anyway) cond_resched().
 *
 * This is a very specialized primitive, intended only for a few uses in
 * tracing and other situations requiring manipulation of function
 * preambles and profiling hooks.  The synchronize_rcu_tasks() function
 * is not (yet) intended for heavy use from multiple CPUs.
 *
 * Note that this guarantee implies further memory-ordering guarantees.
 * On systems with more than one CPU, when synchronize_rcu_tasks() returns,
 * each CPU is guaranteed to have executed a full memory barrier since the
 * end of its last RCU-tasks read-side critical section whose beginning
 * preceded the call to synchronize_rcu_tasks().  In addition, each CPU
 * having an RCU-tasks read-side critical section that extends beyond
 * the return from synchronize_rcu_tasks() is guaranteed to have executed
 * a full memory barrier after the beginning of synchronize_rcu_tasks()
 * and before the beginning of that RCU-tasks read-side critical section.
 * Note that these guarantees include CPUs that are offline, idle, or
 * executing in user mode, as well as CPUs that are executing in the kernel.
 *
 * Furthermore, if CPU A invoked synchronize_rcu_tasks(), which returned
 * to its caller on CPU B, then both CPU A and CPU B are guaranteed
 * to have executed a full memory barrier during the execution of
 * synchronize_rcu_tasks() -- even if CPU A and CPU B are the same CPU
 * (but again only if the system has more than one CPU).
 */
void synchronize_rcu_tasks(void)
{
	/* Complain if the scheduler has not started.  */
	RCU_LOCKDEP_WARN(rcu_scheduler_active == RCU_SCHEDULER_INACTIVE,
			 "synchronize_rcu_tasks called too soon");

	/* Wait for the grace period. */
	wait_rcu_gp(call_rcu_tasks);
}
EXPORT_SYMBOL_GPL(synchronize_rcu_tasks);

/**
 * rcu_barrier_tasks - Wait for in-flight call_rcu_tasks() callbacks.
 *
 * Although the current implementation is guaranteed to wait, it is not
 * obligated to, for example, if there are no pending callbacks.
 */
void rcu_barrier_tasks(void)
{
	/* There is only one callback queue, so this is easy.  ;-) */
	synchronize_rcu_tasks();
}
EXPORT_SYMBOL_GPL(rcu_barrier_tasks);

/* See if tasks are still holding out, complain if so. */
static void check_holdout_task(struct task_struct *t,
			       bool needreport, bool *firstreport)
{
	int cpu;

	if (!READ_ONCE(t->rcu_tasks_holdout) ||
	    t->rcu_tasks_nvcsw != READ_ONCE(t->nvcsw) ||
	    !READ_ONCE(t->on_rq) ||
	    (IS_ENABLED(CONFIG_NO_HZ_FULL) &&
	     !is_idle_task(t) && t->rcu_tasks_idle_cpu >= 0)) {
		WRITE_ONCE(t->rcu_tasks_holdout, false);
		list_del_init(&t->rcu_tasks_holdout_list);
		put_task_struct(t);
		return;
	}
	rcu_request_urgent_qs_task(t);
	if (!needreport)
		return;
	if (*firstreport) {
		pr_err("INFO: rcu_tasks detected stalls on tasks:\n");
		*firstreport = false;
	}
	cpu = task_cpu(t);
	pr_alert("%p: %c%c nvcsw: %lu/%lu holdout: %d idle_cpu: %d/%d\n",
		 t, ".I"[is_idle_task(t)],
		 "N."[cpu < 0 || !tick_nohz_full_cpu(cpu)],
		 t->rcu_tasks_nvcsw, t->nvcsw, t->rcu_tasks_holdout,
		 t->rcu_tasks_idle_cpu, cpu);
	sched_show_task(t);
}

/* RCU-tasks kthread that detects grace periods and invokes callbacks. */
static int __noreturn rcu_tasks_kthread(void *arg)
{
	unsigned long flags;
	struct task_struct *g, *t;
	unsigned long lastreport;
	struct rcu_head *list;
	struct rcu_head *next;
	LIST_HEAD(rcu_tasks_holdouts);
	int fract;

	/* Run on housekeeping CPUs by default.  Sysadm can move if desired. */
	housekeeping_affine(current, HK_FLAG_RCU);

	/*
	 * Each pass through the following loop makes one check for
	 * newly arrived callbacks, and, if there are some, waits for
	 * one RCU-tasks grace period and then invokes the callbacks.
	 * This loop is terminated by the system going down.  ;-)
	 */
	for (;;) {

		/* Pick up any new callbacks. */
		raw_spin_lock_irqsave(&rcu_tasks_cbs_lock, flags);
		list = rcu_tasks_cbs_head;
		rcu_tasks_cbs_head = NULL;
		rcu_tasks_cbs_tail = &rcu_tasks_cbs_head;
		raw_spin_unlock_irqrestore(&rcu_tasks_cbs_lock, flags);

		/* If there were none, wait a bit and start over. */
		if (!list) {
			wait_event_interruptible(rcu_tasks_cbs_wq,
						 READ_ONCE(rcu_tasks_cbs_head));
			if (!rcu_tasks_cbs_head) {
				WARN_ON(signal_pending(current));
				schedule_timeout_interruptible(HZ/10);
			}
			continue;
		}

		/*
		 * Wait for all pre-existing t->on_rq and t->nvcsw
		 * transitions to complete.  Invoking synchronize_rcu()
		 * suffices because all these transitions occur with
		 * interrupts disabled.  Without this synchronize_rcu(),
		 * a read-side critical section that started before the
		 * grace period might be incorrectly seen as having started
		 * after the grace period.
		 *
		 * This synchronize_rcu() also dispenses with the
		 * need for a memory barrier on the first store to
		 * ->rcu_tasks_holdout, as it forces the store to happen
		 * after the beginning of the grace period.
		 */
		synchronize_rcu();

		/*
		 * There were callbacks, so we need to wait for an
		 * RCU-tasks grace period.  Start off by scanning
		 * the task list for tasks that are not already
		 * voluntarily blocked.  Mark these tasks and make
		 * a list of them in rcu_tasks_holdouts.
		 */
		rcu_read_lock();
		for_each_process_thread(g, t) {
			if (t != current && READ_ONCE(t->on_rq) &&
			    !is_idle_task(t)) {
				get_task_struct(t);
				t->rcu_tasks_nvcsw = READ_ONCE(t->nvcsw);
				WRITE_ONCE(t->rcu_tasks_holdout, true);
				list_add(&t->rcu_tasks_holdout_list,
					 &rcu_tasks_holdouts);
			}
		}
		rcu_read_unlock();

		/*
		 * Wait for tasks that are in the process of exiting.
		 * This does only part of the job, ensuring that all
		 * tasks that were previously exiting reach the point
		 * where they have disabled preemption, allowing the
		 * later synchronize_rcu() to finish the job.
		 */
		synchronize_srcu(&tasks_rcu_exit_srcu);

		/*
		 * Each pass through the following loop scans the list
		 * of holdout tasks, removing any that are no longer
		 * holdouts.  When the list is empty, we are done.
		 */
		lastreport = jiffies;

		/* Start off with HZ/10 wait and slowly back off to 1 HZ wait*/
		fract = 10;

		for (;;) {
			bool firstreport;
			bool needreport;
			int rtst;
			struct task_struct *t1;

			if (list_empty(&rcu_tasks_holdouts))
				break;

			/* Slowly back off waiting for holdouts */
			schedule_timeout_interruptible(HZ/fract);

			if (fract > 1)
				fract--;

			rtst = READ_ONCE(rcu_task_stall_timeout);
			needreport = rtst > 0 &&
				     time_after(jiffies, lastreport + rtst);
			if (needreport)
				lastreport = jiffies;
			firstreport = true;
			WARN_ON(signal_pending(current));
			list_for_each_entry_safe(t, t1, &rcu_tasks_holdouts,
						rcu_tasks_holdout_list) {
				check_holdout_task(t, needreport, &firstreport);
				cond_resched();
			}
		}

		/*
		 * Because ->on_rq and ->nvcsw are not guaranteed
		 * to have a full memory barriers prior to them in the
		 * schedule() path, memory reordering on other CPUs could
		 * cause their RCU-tasks read-side critical sections to
		 * extend past the end of the grace period.  However,
		 * because these ->nvcsw updates are carried out with
		 * interrupts disabled, we can use synchronize_rcu()
		 * to force the needed ordering on all such CPUs.
		 *
		 * This synchronize_rcu() also confines all
		 * ->rcu_tasks_holdout accesses to be within the grace
		 * period, avoiding the need for memory barriers for
		 * ->rcu_tasks_holdout accesses.
		 *
		 * In addition, this synchronize_rcu() waits for exiting
		 * tasks to complete their final preempt_disable() region
		 * of execution, cleaning up after the synchronize_srcu()
		 * above.
		 */
		synchronize_rcu();

		/* Invoke the callbacks. */
		while (list) {
			next = list->next;
			local_bh_disable();
			list->func(list);
			local_bh_enable();
			list = next;
			cond_resched();
		}
		/* Paranoid sleep to keep this from entering a tight loop */
		schedule_timeout_uninterruptible(HZ/10);
	}
}

/* Spawn rcu_tasks_kthread() at core_initcall() time. */
static int __init rcu_spawn_tasks_kthread(void)
{
	struct task_struct *t;

	t = kthread_run(rcu_tasks_kthread, NULL, "rcu_tasks_kthread");
	if (WARN_ONCE(IS_ERR(t), "%s: Could not start Tasks-RCU grace-period kthread, OOM is now expected behavior\n", __func__))
		return 0;
	smp_mb(); /* Ensure others see full kthread. */
	WRITE_ONCE(rcu_tasks_kthread_ptr, t);
	return 0;
}
core_initcall(rcu_spawn_tasks_kthread);

/* Do the srcu_read_lock() for the above synchronize_srcu().  */
void exit_tasks_rcu_start(void) __acquires(&tasks_rcu_exit_srcu)
{
	preempt_disable();
	current->rcu_tasks_idx = __srcu_read_lock(&tasks_rcu_exit_srcu);
	preempt_enable();
}

/* Do the srcu_read_unlock() for the above synchronize_srcu().  */
void exit_tasks_rcu_finish(void) __releases(&tasks_rcu_exit_srcu)
{
	preempt_disable();
	__srcu_read_unlock(&tasks_rcu_exit_srcu, current->rcu_tasks_idx);
	preempt_enable();
}

#endif /* #ifdef CONFIG_TASKS_RCU */

#ifndef CONFIG_TINY_RCU

/*
 * Print any non-default Tasks RCU settings.
 */
static void __init rcu_tasks_bootup_oddness(void)
{
#ifdef CONFIG_TASKS_RCU
	if (rcu_task_stall_timeout != RCU_TASK_STALL_TIMEOUT)
		pr_info("\tTasks-RCU CPU stall warnings timeout set to %d (rcu_task_stall_timeout).\n", rcu_task_stall_timeout);
	else
		pr_info("\tTasks RCU enabled.\n");
#endif /* #ifdef CONFIG_TASKS_RCU */
}

#endif /* #ifndef CONFIG_TINY_RCU */

#ifdef CONFIG_PROVE_RCU

/*
 * Early boot self test parameters.
 */
static bool rcu_self_test;
module_param(rcu_self_test, bool, 0444);

static int rcu_self_test_counter;

static void test_callback(struct rcu_head *r)
{
	rcu_self_test_counter++;
	pr_info("RCU test callback executed %d\n", rcu_self_test_counter);
}

DEFINE_STATIC_SRCU(early_srcu);

struct early_boot_kfree_rcu {
	struct rcu_head rh;
};

static void early_boot_test_call_rcu(void)
{
	static struct rcu_head head;
	static struct rcu_head shead;
	struct early_boot_kfree_rcu *rhp;

	call_rcu(&head, test_callback);
	if (IS_ENABLED(CONFIG_SRCU))
		call_srcu(&early_srcu, &shead, test_callback);
	rhp = kmalloc(sizeof(*rhp), GFP_KERNEL);
	if (!WARN_ON_ONCE(!rhp))
		kfree_rcu(rhp, rh);
}

void rcu_early_boot_tests(void)
{
	pr_info("Running RCU self tests\n");

	if (rcu_self_test)
		early_boot_test_call_rcu();
	rcu_test_sync_prims();
}

static int rcu_verify_early_boot_tests(void)
{
	int ret = 0;
	int early_boot_test_counter = 0;

	if (rcu_self_test) {
		early_boot_test_counter++;
		rcu_barrier();
		if (IS_ENABLED(CONFIG_SRCU)) {
			early_boot_test_counter++;
			srcu_barrier(&early_srcu);
		}
	}
	if (rcu_self_test_counter != early_boot_test_counter) {
		WARN_ON(1);
		ret = -1;
	}

	return ret;
}
late_initcall(rcu_verify_early_boot_tests);
#else
void rcu_early_boot_tests(void) {}
#endif /* CONFIG_PROVE_RCU */

#ifndef CONFIG_TINY_RCU

/*
 * Print any significant non-default boot-time settings.
 */
void __init rcupdate_announce_bootup_oddness(void)
{
	if (rcu_normal)
		pr_info("\tNo expedited grace period (rcu_normal).\n");
	else if (rcu_normal_after_boot)
		pr_info("\tNo expedited grace period (rcu_normal_after_boot).\n");
	else if (rcu_expedited)
		pr_info("\tAll grace periods are expedited (rcu_expedited).\n");
	if (rcu_cpu_stall_suppress)
		pr_info("\tRCU CPU stall warnings suppressed (rcu_cpu_stall_suppress).\n");
	if (rcu_cpu_stall_timeout != CONFIG_RCU_CPU_STALL_TIMEOUT)
		pr_info("\tRCU CPU stall warnings timeout set to %d (rcu_cpu_stall_timeout).\n", rcu_cpu_stall_timeout);
	rcu_tasks_bootup_oddness();
}

#endif /* #ifndef CONFIG_TINY_RCU */
