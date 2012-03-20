/*
 * Sleepable Read-Copy Update mechanism for mutual exclusion
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
 * Copyright (C) IBM Corporation, 2006
 *
 * Author: Paul McKenney <paulmck@us.ibm.com>
 *
 * For detailed explanation of Read-Copy Update mechanism see -
 * 		Documentation/RCU/ *.txt
 *
 */

#ifndef _LINUX_SRCU_H
#define _LINUX_SRCU_H

#include <linux/mutex.h>
#include <linux/rcupdate.h>

struct srcu_struct_array {
	int c[2];
};

struct srcu_struct {
	int completed;
	struct srcu_struct_array __percpu *per_cpu_ref;
	struct mutex mutex;
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map dep_map;
#endif /* #ifdef CONFIG_DEBUG_LOCK_ALLOC */
};

#ifndef CONFIG_PREEMPT
#define srcu_barrier() barrier()
#else /* #ifndef CONFIG_PREEMPT */
#define srcu_barrier()
#endif /* #else #ifndef CONFIG_PREEMPT */

#ifdef CONFIG_DEBUG_LOCK_ALLOC

int __init_srcu_struct(struct srcu_struct *sp, const char *name,
		       struct lock_class_key *key);

#define init_srcu_struct(sp) \
({ \
	static struct lock_class_key __srcu_key; \
	\
	__init_srcu_struct((sp), #sp, &__srcu_key); \
})

#else /* #ifdef CONFIG_DEBUG_LOCK_ALLOC */

int init_srcu_struct(struct srcu_struct *sp);

#endif /* #else #ifdef CONFIG_DEBUG_LOCK_ALLOC */

void cleanup_srcu_struct(struct srcu_struct *sp);
int __srcu_read_lock(struct srcu_struct *sp) __acquires(sp);
void __srcu_read_unlock(struct srcu_struct *sp, int idx) __releases(sp);
void synchronize_srcu(struct srcu_struct *sp);
void synchronize_srcu_expedited(struct srcu_struct *sp);
long srcu_batches_completed(struct srcu_struct *sp);

#ifdef CONFIG_DEBUG_LOCK_ALLOC

/**
 * srcu_read_lock_held - might we be in SRCU read-side critical section?
 *
 * If CONFIG_DEBUG_LOCK_ALLOC is selected, returns nonzero iff in an SRCU
 * read-side critical section.  In absence of CONFIG_DEBUG_LOCK_ALLOC,
 * this assumes we are in an SRCU read-side critical section unless it can
 * prove otherwise.
 *
 * Checks debug_lockdep_rcu_enabled() to prevent false positives during boot
 * and while lockdep is disabled.
 *
 * Note that if the CPU is in the idle loop from an RCU point of view
 * (ie: that we are in the section between rcu_idle_enter() and
 * rcu_idle_exit()) then srcu_read_lock_held() returns false even if
 * the CPU did an srcu_read_lock().  The reason for this is that RCU
 * ignores CPUs that are in such a section, considering these as in
 * extended quiescent state, so such a CPU is effectively never in an
 * RCU read-side critical section regardless of what RCU primitives it
 * invokes.  This state of affairs is required --- we need to keep an
 * RCU-free window in idle where the CPU may possibly enter into low
 * power mode. This way we can notice an extended quiescent state to
 * other CPUs that started a grace period. Otherwise we would delay any
 * grace period as long as we run in the idle task.
 */
static inline int srcu_read_lock_held(struct srcu_struct *sp)
{
	if (rcu_is_cpu_idle())
		return 0;

	if (!debug_lockdep_rcu_enabled())
		return 1;

	return lock_is_held(&sp->dep_map);
}

#else /* #ifdef CONFIG_DEBUG_LOCK_ALLOC */

static inline int srcu_read_lock_held(struct srcu_struct *sp)
{
	return 1;
}

#endif /* #else #ifdef CONFIG_DEBUG_LOCK_ALLOC */

/**
 * srcu_dereference_check - fetch SRCU-protected pointer for later dereferencing
 * @p: the pointer to fetch and protect for later dereferencing
 * @sp: pointer to the srcu_struct, which is used to check that we
 *	really are in an SRCU read-side critical section.
 * @c: condition to check for update-side use
 *
 * If PROVE_RCU is enabled, invoking this outside of an RCU read-side
 * critical section will result in an RCU-lockdep splat, unless @c evaluates
 * to 1.  The @c argument will normally be a logical expression containing
 * lockdep_is_held() calls.
 */
#define srcu_dereference_check(p, sp, c) \
	__rcu_dereference_check((p), srcu_read_lock_held(sp) || (c), __rcu)

/**
 * srcu_dereference - fetch SRCU-protected pointer for later dereferencing
 * @p: the pointer to fetch and protect for later dereferencing
 * @sp: pointer to the srcu_struct, which is used to check that we
 *	really are in an SRCU read-side critical section.
 *
 * Makes rcu_dereference_check() do the dirty work.  If PROVE_RCU
 * is enabled, invoking this outside of an RCU read-side critical
 * section will result in an RCU-lockdep splat.
 */
#define srcu_dereference(p, sp) srcu_dereference_check((p), (sp), 0)

/**
 * srcu_read_lock - register a new reader for an SRCU-protected structure.
 * @sp: srcu_struct in which to register the new reader.
 *
 * Enter an SRCU read-side critical section.  Note that SRCU read-side
 * critical sections may be nested.  However, it is illegal to
 * call anything that waits on an SRCU grace period for the same
 * srcu_struct, whether directly or indirectly.  Please note that
 * one way to indirectly wait on an SRCU grace period is to acquire
 * a mutex that is held elsewhere while calling synchronize_srcu() or
 * synchronize_srcu_expedited().
 *
 * Note that srcu_read_lock() and the matching srcu_read_unlock() must
 * occur in the same context, for example, it is illegal to invoke
 * srcu_read_unlock() in an irq handler if the matching srcu_read_lock()
 * was invoked in process context.
 */
static inline int srcu_read_lock(struct srcu_struct *sp) __acquires(sp)
{
	int retval = __srcu_read_lock(sp);

	rcu_lock_acquire(&(sp)->dep_map);
	return retval;
}

/**
 * srcu_read_unlock - unregister a old reader from an SRCU-protected structure.
 * @sp: srcu_struct in which to unregister the old reader.
 * @idx: return value from corresponding srcu_read_lock().
 *
 * Exit an SRCU read-side critical section.
 */
static inline void srcu_read_unlock(struct srcu_struct *sp, int idx)
	__releases(sp)
{
	rcu_lock_release(&(sp)->dep_map);
	__srcu_read_unlock(sp, idx);
}

/**
 * srcu_read_lock_raw - register a new reader for an SRCU-protected structure.
 * @sp: srcu_struct in which to register the new reader.
 *
 * Enter an SRCU read-side critical section.  Similar to srcu_read_lock(),
 * but avoids the RCU-lockdep checking.  This means that it is legal to
 * use srcu_read_lock_raw() in one context, for example, in an exception
 * handler, and then have the matching srcu_read_unlock_raw() in another
 * context, for example in the task that took the exception.
 *
 * However, the entire SRCU read-side critical section must reside within a
 * single task.  For example, beware of using srcu_read_lock_raw() in
 * a device interrupt handler and srcu_read_unlock() in the interrupted
 * task:  This will not work if interrupts are threaded.
 */
static inline int srcu_read_lock_raw(struct srcu_struct *sp)
{
	unsigned long flags;
	int ret;

	local_irq_save(flags);
	ret =  __srcu_read_lock(sp);
	local_irq_restore(flags);
	return ret;
}

/**
 * srcu_read_unlock_raw - unregister reader from an SRCU-protected structure.
 * @sp: srcu_struct in which to unregister the old reader.
 * @idx: return value from corresponding srcu_read_lock_raw().
 *
 * Exit an SRCU read-side critical section without lockdep-RCU checking.
 * See srcu_read_lock_raw() for more details.
 */
static inline void srcu_read_unlock_raw(struct srcu_struct *sp, int idx)
{
	unsigned long flags;

	local_irq_save(flags);
	__srcu_read_unlock(sp, idx);
	local_irq_restore(flags);
}

#endif
