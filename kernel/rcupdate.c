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
 * Copyright IBM Corporation, 2001
 *
 * Authors: Dipankar Sarma <dipankar@in.ibm.com>
 *	    Manfred Spraul <manfred@colorfullife.com>
 *
 * Based on the original work by Paul McKenney <paulmck@us.ibm.com>
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
#include <linux/sched.h>
#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/mutex.h>
#include <linux/export.h>
#include <linux/hardirq.h>

#define CREATE_TRACE_POINTS
#include <trace/events/rcu.h>

#include "rcu.h"

#ifdef CONFIG_PREEMPT_RCU

/*
 * Preemptible RCU implementation for rcu_read_lock().
 * Just increment ->rcu_read_lock_nesting, shared state will be updated
 * if we block.
 */
void __rcu_read_lock(void)
{
	current->rcu_read_lock_nesting++;
	barrier();  /* critical section after entry code. */
}
EXPORT_SYMBOL_GPL(__rcu_read_lock);

/*
 * Preemptible RCU implementation for rcu_read_unlock().
 * Decrement ->rcu_read_lock_nesting.  If the result is zero (outermost
 * rcu_read_unlock()) and ->rcu_read_unlock_special is non-zero, then
 * invoke rcu_read_unlock_special() to clean up after a context switch
 * in an RCU read-side critical section and other special cases.
 */
void __rcu_read_unlock(void)
{
	struct task_struct *t = current;

	if (t->rcu_read_lock_nesting != 1) {
		--t->rcu_read_lock_nesting;
	} else {
		barrier();  /* critical section before exit code. */
		t->rcu_read_lock_nesting = INT_MIN;
		barrier();  /* assign before ->rcu_read_unlock_special load */
		if (unlikely(ACCESS_ONCE(t->rcu_read_unlock_special)))
			rcu_read_unlock_special(t);
		barrier();  /* ->rcu_read_unlock_special load before assign */
		t->rcu_read_lock_nesting = 0;
	}
#ifdef CONFIG_PROVE_LOCKING
	{
		int rrln = ACCESS_ONCE(t->rcu_read_lock_nesting);

		WARN_ON_ONCE(rrln < 0 && rrln > INT_MIN / 2);
	}
#endif /* #ifdef CONFIG_PROVE_LOCKING */
}
EXPORT_SYMBOL_GPL(__rcu_read_unlock);

/*
 * Check for a task exiting while in a preemptible-RCU read-side
 * critical section, clean up if so.  No need to issue warnings,
 * as debug_check_no_locks_held() already does this if lockdep
 * is enabled.
 */
void exit_rcu(void)
{
	struct task_struct *t = current;

	if (likely(list_empty(&current->rcu_node_entry)))
		return;
	t->rcu_read_lock_nesting = 1;
	barrier();
	t->rcu_read_unlock_special = RCU_READ_UNLOCK_BLOCKED;
	__rcu_read_unlock();
}

#else /* #ifdef CONFIG_PREEMPT_RCU */

void exit_rcu(void)
{
}

#endif /* #else #ifdef CONFIG_PREEMPT_RCU */

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
#endif

#ifdef CONFIG_DEBUG_LOCK_ALLOC

int debug_lockdep_rcu_enabled(void)
{
	return rcu_scheduler_active && debug_locks &&
	       current->lockdep_recursion == 0;
}
EXPORT_SYMBOL_GPL(debug_lockdep_rcu_enabled);

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
 * Note that rcu_read_lock() is disallowed if the CPU is either idle or
 * offline from an RCU perspective, so check for those as well.
 */
int rcu_read_lock_bh_held(void)
{
	if (!debug_lockdep_rcu_enabled())
		return 1;
	if (rcu_is_cpu_idle())
		return 0;
	if (!rcu_lockdep_current_cpu_online())
		return 0;
	return in_softirq() || irqs_disabled();
}
EXPORT_SYMBOL_GPL(rcu_read_lock_bh_held);

#endif /* #ifdef CONFIG_DEBUG_LOCK_ALLOC */

struct rcu_synchronize {
	struct rcu_head head;
	struct completion completion;
};

/*
 * Awaken the corresponding synchronize_rcu() instance now that a
 * grace period has elapsed.
 */
static void wakeme_after_rcu(struct rcu_head  *head)
{
	struct rcu_synchronize *rcu;

	rcu = container_of(head, struct rcu_synchronize, head);
	complete(&rcu->completion);
}

void wait_rcu_gp(call_rcu_func_t crf)
{
	struct rcu_synchronize rcu;

	init_rcu_head_on_stack(&rcu.head);
	init_completion(&rcu.completion);
	/* Will wake me after RCU finished. */
	crf(&rcu.head, wakeme_after_rcu);
	/* Wait for it. */
	wait_for_completion(&rcu.completion);
	destroy_rcu_head_on_stack(&rcu.head);
}
EXPORT_SYMBOL_GPL(wait_rcu_gp);

#ifdef CONFIG_PROVE_RCU
/*
 * wrapper function to avoid #include problems.
 */
int rcu_my_thread_group_empty(void)
{
	return thread_group_empty(current);
}
EXPORT_SYMBOL_GPL(rcu_my_thread_group_empty);
#endif /* #ifdef CONFIG_PROVE_RCU */

#ifdef CONFIG_DEBUG_OBJECTS_RCU_HEAD
static inline void debug_init_rcu_head(struct rcu_head *head)
{
	debug_object_init(head, &rcuhead_debug_descr);
}

static inline void debug_rcu_head_free(struct rcu_head *head)
{
	debug_object_free(head, &rcuhead_debug_descr);
}

/*
 * fixup_init is called when:
 * - an active object is initialized
 */
static int rcuhead_fixup_init(void *addr, enum debug_obj_state state)
{
	struct rcu_head *head = addr;

	switch (state) {
	case ODEBUG_STATE_ACTIVE:
		/*
		 * Ensure that queued callbacks are all executed.
		 * If we detect that we are nested in a RCU read-side critical
		 * section, we should simply fail, otherwise we would deadlock.
		 * In !PREEMPT configurations, there is no way to tell if we are
		 * in a RCU read-side critical section or not, so we never
		 * attempt any fixup and just print a warning.
		 */
#ifndef CONFIG_PREEMPT
		WARN_ON_ONCE(1);
		return 0;
#endif
		if (rcu_preempt_depth() != 0 || preempt_count() != 0 ||
		    irqs_disabled()) {
			WARN_ON_ONCE(1);
			return 0;
		}
		rcu_barrier();
		rcu_barrier_sched();
		rcu_barrier_bh();
		debug_object_init(head, &rcuhead_debug_descr);
		return 1;
	default:
		return 0;
	}
}

/*
 * fixup_activate is called when:
 * - an active object is activated
 * - an unknown object is activated (might be a statically initialized object)
 * Activation is performed internally by call_rcu().
 */
static int rcuhead_fixup_activate(void *addr, enum debug_obj_state state)
{
	struct rcu_head *head = addr;

	switch (state) {

	case ODEBUG_STATE_NOTAVAILABLE:
		/*
		 * This is not really a fixup. We just make sure that it is
		 * tracked in the object tracker.
		 */
		debug_object_init(head, &rcuhead_debug_descr);
		debug_object_activate(head, &rcuhead_debug_descr);
		return 0;

	case ODEBUG_STATE_ACTIVE:
		/*
		 * Ensure that queued callbacks are all executed.
		 * If we detect that we are nested in a RCU read-side critical
		 * section, we should simply fail, otherwise we would deadlock.
		 * In !PREEMPT configurations, there is no way to tell if we are
		 * in a RCU read-side critical section or not, so we never
		 * attempt any fixup and just print a warning.
		 */
#ifndef CONFIG_PREEMPT
		WARN_ON_ONCE(1);
		return 0;
#endif
		if (rcu_preempt_depth() != 0 || preempt_count() != 0 ||
		    irqs_disabled()) {
			WARN_ON_ONCE(1);
			return 0;
		}
		rcu_barrier();
		rcu_barrier_sched();
		rcu_barrier_bh();
		debug_object_activate(head, &rcuhead_debug_descr);
		return 1;
	default:
		return 0;
	}
}

/*
 * fixup_free is called when:
 * - an active object is freed
 */
static int rcuhead_fixup_free(void *addr, enum debug_obj_state state)
{
	struct rcu_head *head = addr;

	switch (state) {
	case ODEBUG_STATE_ACTIVE:
		/*
		 * Ensure that queued callbacks are all executed.
		 * If we detect that we are nested in a RCU read-side critical
		 * section, we should simply fail, otherwise we would deadlock.
		 * In !PREEMPT configurations, there is no way to tell if we are
		 * in a RCU read-side critical section or not, so we never
		 * attempt any fixup and just print a warning.
		 */
#ifndef CONFIG_PREEMPT
		WARN_ON_ONCE(1);
		return 0;
#endif
		if (rcu_preempt_depth() != 0 || preempt_count() != 0 ||
		    irqs_disabled()) {
			WARN_ON_ONCE(1);
			return 0;
		}
		rcu_barrier();
		rcu_barrier_sched();
		rcu_barrier_bh();
		debug_object_free(head, &rcuhead_debug_descr);
		return 1;
	default:
		return 0;
	}
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
	.fixup_init = rcuhead_fixup_init,
	.fixup_activate = rcuhead_fixup_activate,
	.fixup_free = rcuhead_fixup_free,
};
EXPORT_SYMBOL_GPL(rcuhead_debug_descr);
#endif /* #ifdef CONFIG_DEBUG_OBJECTS_RCU_HEAD */

#if defined(CONFIG_TREE_RCU) || defined(CONFIG_TREE_PREEMPT_RCU) || defined(CONFIG_RCU_TRACE)
void do_trace_rcu_torture_read(char *rcutorturename, struct rcu_head *rhp)
{
	trace_rcu_torture_read(rcutorturename, rhp);
}
EXPORT_SYMBOL_GPL(do_trace_rcu_torture_read);
#else
#define do_trace_rcu_torture_read(rcutorturename, rhp) do { } while (0)
#endif
