/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Read-Copy Update mechanism for mutual exclusion, adapted for tracing.
 *
 * Copyright (C) 2020 Paul E. McKenney.
 */

#ifndef __LINUX_RCUPDATE_TRACE_H
#define __LINUX_RCUPDATE_TRACE_H

#include <linux/sched.h>
#include <linux/rcupdate.h>
#include <linux/cleanup.h>

#ifdef CONFIG_TASKS_TRACE_RCU
extern struct srcu_struct rcu_tasks_trace_srcu_struct;
#endif // #ifdef CONFIG_TASKS_TRACE_RCU

#if defined(CONFIG_DEBUG_LOCK_ALLOC) && defined(CONFIG_TASKS_TRACE_RCU)

static inline int rcu_read_lock_trace_held(void)
{
	return srcu_read_lock_held(&rcu_tasks_trace_srcu_struct);
}

#else // #if defined(CONFIG_DEBUG_LOCK_ALLOC) && defined(CONFIG_TASKS_TRACE_RCU)

static inline int rcu_read_lock_trace_held(void)
{
	return 1;
}

#endif // #else // #if defined(CONFIG_DEBUG_LOCK_ALLOC) && defined(CONFIG_TASKS_TRACE_RCU)

#ifdef CONFIG_TASKS_TRACE_RCU

/**
 * rcu_read_lock_tasks_trace - mark beginning of RCU-trace read-side critical section
 *
 * When synchronize_rcu_tasks_trace() is invoked by one task, then that
 * task is guaranteed to block until all other tasks exit their read-side
 * critical sections.  Similarly, if call_rcu_trace() is invoked on one
 * task while other tasks are within RCU read-side critical sections,
 * invocation of the corresponding RCU callback is deferred until after
 * the all the other tasks exit their critical sections.
 *
 * For more details, please see the documentation for
 * srcu_read_lock_fast().  For a description of how implicit RCU
 * readers provide the needed ordering for architectures defining the
 * ARCH_WANTS_NO_INSTR Kconfig option (and thus promising never to trace
 * code where RCU is not watching), please see the __srcu_read_lock_fast()
 * (non-kerneldoc) header comment.  Otherwise, the smp_mb() below provided
 * the needed ordering.
 */
static inline struct srcu_ctr __percpu *rcu_read_lock_tasks_trace(void)
{
	struct srcu_ctr __percpu *ret = __srcu_read_lock_fast(&rcu_tasks_trace_srcu_struct);

	rcu_try_lock_acquire(&rcu_tasks_trace_srcu_struct.dep_map);
	if (!IS_ENABLED(CONFIG_TASKS_TRACE_RCU_NO_MB))
		smp_mb(); // Provide ordering on noinstr-incomplete architectures.
	return ret;
}

/**
 * rcu_read_unlock_tasks_trace - mark end of RCU-trace read-side critical section
 * @scp: return value from corresponding rcu_read_lock_tasks_trace().
 *
 * Pairs with the preceding call to rcu_read_lock_tasks_trace() that
 * returned the value passed in via scp.
 *
 * For more details, please see the documentation for rcu_read_unlock().
 * For memory-ordering information, please see the header comment for the
 * rcu_read_lock_tasks_trace() function.
 */
static inline void rcu_read_unlock_tasks_trace(struct srcu_ctr __percpu *scp)
{
	if (!IS_ENABLED(CONFIG_TASKS_TRACE_RCU_NO_MB))
		smp_mb(); // Provide ordering on noinstr-incomplete architectures.
	__srcu_read_unlock_fast(&rcu_tasks_trace_srcu_struct, scp);
	srcu_lock_release(&rcu_tasks_trace_srcu_struct.dep_map);
}

/**
 * rcu_read_lock_trace - mark beginning of RCU-trace read-side critical section
 *
 * When synchronize_rcu_tasks_trace() is invoked by one task, then that
 * task is guaranteed to block until all other tasks exit their read-side
 * critical sections.  Similarly, if call_rcu_trace() is invoked on one
 * task while other tasks are within RCU read-side critical sections,
 * invocation of the corresponding RCU callback is deferred until after
 * the all the other tasks exit their critical sections.
 *
 * For more details, please see the documentation for rcu_read_lock().
 */
static inline void rcu_read_lock_trace(void)
{
	struct task_struct *t = current;

	rcu_try_lock_acquire(&rcu_tasks_trace_srcu_struct.dep_map);
	if (t->trc_reader_nesting++) {
		// In case we interrupted a Tasks Trace RCU reader.
		return;
	}
	barrier();  // nesting before scp to protect against interrupt handler.
	t->trc_reader_scp = __srcu_read_lock_fast(&rcu_tasks_trace_srcu_struct);
	if (!IS_ENABLED(CONFIG_TASKS_TRACE_RCU_NO_MB))
		smp_mb(); // Placeholder for more selective ordering
}

/**
 * rcu_read_unlock_trace - mark end of RCU-trace read-side critical section
 *
 * Pairs with a preceding call to rcu_read_lock_trace(), and nesting is
 * allowed.  Invoking a rcu_read_unlock_trace() when there is no matching
 * rcu_read_lock_trace() is verboten, and will result in lockdep complaints.
 *
 * For more details, please see the documentation for rcu_read_unlock().
 */
static inline void rcu_read_unlock_trace(void)
{
	struct srcu_ctr __percpu *scp;
	struct task_struct *t = current;

	scp = t->trc_reader_scp;
	barrier();  // scp before nesting to protect against interrupt handler.
	if (!--t->trc_reader_nesting) {
		if (!IS_ENABLED(CONFIG_TASKS_TRACE_RCU_NO_MB))
			smp_mb(); // Placeholder for more selective ordering
		__srcu_read_unlock_fast(&rcu_tasks_trace_srcu_struct, scp);
	}
	srcu_lock_release(&rcu_tasks_trace_srcu_struct.dep_map);
}

/**
 * call_rcu_tasks_trace() - Queue a callback trace task-based grace period
 * @rhp: structure to be used for queueing the RCU updates.
 * @func: actual callback function to be invoked after the grace period
 *
 * The callback function will be invoked some time after a trace rcu-tasks
 * grace period elapses, in other words after all currently executing
 * trace rcu-tasks read-side critical sections have completed. These
 * read-side critical sections are delimited by calls to rcu_read_lock_trace()
 * and rcu_read_unlock_trace().
 *
 * See the description of call_rcu() for more detailed information on
 * memory ordering guarantees.
 */
static inline void call_rcu_tasks_trace(struct rcu_head *rhp, rcu_callback_t func)
{
	call_srcu(&rcu_tasks_trace_srcu_struct, rhp, func);
}

/**
 * synchronize_rcu_tasks_trace - wait for a trace rcu-tasks grace period
 *
 * Control will return to the caller some time after a trace rcu-tasks
 * grace period has elapsed, in other words after all currently executing
 * trace rcu-tasks read-side critical sections have elapsed. These read-side
 * critical sections are delimited by calls to rcu_read_lock_trace()
 * and rcu_read_unlock_trace().
 *
 * This is a very specialized primitive, intended only for a few uses in
 * tracing and other situations requiring manipulation of function preambles
 * and profiling hooks.  The synchronize_rcu_tasks_trace() function is not
 * (yet) intended for heavy use from multiple CPUs.
 *
 * See the description of synchronize_rcu() for more detailed information
 * on memory ordering guarantees.
 */
static inline void synchronize_rcu_tasks_trace(void)
{
	synchronize_srcu(&rcu_tasks_trace_srcu_struct);
}

/**
 * rcu_barrier_tasks_trace - Wait for in-flight call_rcu_tasks_trace() callbacks.
 *
 * Note that rcu_barrier_tasks_trace() is not obligated to actually wait,
 * for example, if there are no pending callbacks.
 */
static inline void rcu_barrier_tasks_trace(void)
{
	srcu_barrier(&rcu_tasks_trace_srcu_struct);
}

/**
 * rcu_tasks_trace_expedite_current - Expedite the current Tasks Trace RCU grace period
 *
 * Cause the current Tasks Trace RCU grace period to become expedited.
 * The grace period following the current one might also be expedited.
 * If there is no current grace period, one might be created.  If the
 * current grace period is currently sleeping, that sleep will complete
 * before expediting will take effect.
 */
static inline void rcu_tasks_trace_expedite_current(void)
{
	srcu_expedite_current(&rcu_tasks_trace_srcu_struct);
}

// Placeholders to enable stepwise transition.
void __init rcu_tasks_trace_suppress_unused(void);

#else
/*
 * The BPF JIT forms these addresses even when it doesn't call these
 * functions, so provide definitions that result in runtime errors.
 */
static inline void call_rcu_tasks_trace(struct rcu_head *rhp, rcu_callback_t func) { BUG(); }
static inline void rcu_read_lock_trace(void) { BUG(); }
static inline void rcu_read_unlock_trace(void) { BUG(); }
#endif /* #ifdef CONFIG_TASKS_TRACE_RCU */

DEFINE_LOCK_GUARD_0(rcu_tasks_trace,
	rcu_read_lock_trace(),
	rcu_read_unlock_trace())

#endif /* __LINUX_RCUPDATE_TRACE_H */
