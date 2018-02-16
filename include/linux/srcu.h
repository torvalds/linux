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
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * Copyright (C) IBM Corporation, 2006
 * Copyright (C) Fujitsu, 2012
 *
 * Author: Paul McKenney <paulmck@us.ibm.com>
 *	   Lai Jiangshan <laijs@cn.fujitsu.com>
 *
 * For detailed explanation of Read-Copy Update mechanism see -
 *		Documentation/RCU/ *.txt
 *
 */

#ifndef _LINUX_SRCU_H
#define _LINUX_SRCU_H

#include <linux/mutex.h>
#include <linux/rcupdate.h>
#include <linux/workqueue.h>
#include <linux/rcu_segcblist.h>

struct srcu_struct;

#ifdef CONFIG_DEBUG_LOCK_ALLOC

int __init_srcu_struct(struct srcu_struct *sp, const char *name,
		       struct lock_class_key *key);

#define init_srcu_struct(sp) \
({ \
	static struct lock_class_key __srcu_key; \
	\
	__init_srcu_struct((sp), #sp, &__srcu_key); \
})

#define __SRCU_DEP_MAP_INIT(srcu_name)	.dep_map = { .name = #srcu_name },
#else /* #ifdef CONFIG_DEBUG_LOCK_ALLOC */

int init_srcu_struct(struct srcu_struct *sp);

#define __SRCU_DEP_MAP_INIT(srcu_name)
#endif /* #else #ifdef CONFIG_DEBUG_LOCK_ALLOC */

#ifdef CONFIG_TINY_SRCU
#include <linux/srcutiny.h>
#elif defined(CONFIG_TREE_SRCU)
#include <linux/srcutree.h>
#elif defined(CONFIG_SRCU)
#error "Unknown SRCU implementation specified to kernel configuration"
#else
/* Dummy definition for things like notifiers.  Actual use gets link error. */
struct srcu_struct { };
#endif

void call_srcu(struct srcu_struct *sp, struct rcu_head *head,
		void (*func)(struct rcu_head *head));
void cleanup_srcu_struct(struct srcu_struct *sp);
int __srcu_read_lock(struct srcu_struct *sp) __acquires(sp);
void __srcu_read_unlock(struct srcu_struct *sp, int idx) __releases(sp);
void synchronize_srcu(struct srcu_struct *sp);

#ifdef CONFIG_DEBUG_LOCK_ALLOC

/**
 * srcu_read_lock_held - might we be in SRCU read-side critical section?
 * @sp: The srcu_struct structure to check
 *
 * If CONFIG_DEBUG_LOCK_ALLOC is selected, returns nonzero iff in an SRCU
 * read-side critical section.  In absence of CONFIG_DEBUG_LOCK_ALLOC,
 * this assumes we are in an SRCU read-side critical section unless it can
 * prove otherwise.
 *
 * Checks debug_lockdep_rcu_enabled() to prevent false positives during boot
 * and while lockdep is disabled.
 *
 * Note that SRCU is based on its own statemachine and it doesn't
 * relies on normal RCU, it can be called from the CPU which
 * is in the idle loop from an RCU point of view or offline.
 */
static inline int srcu_read_lock_held(const struct srcu_struct *sp)
{
	if (!debug_lockdep_rcu_enabled())
		return 1;
	return lock_is_held(&sp->dep_map);
}

#else /* #ifdef CONFIG_DEBUG_LOCK_ALLOC */

static inline int srcu_read_lock_held(const struct srcu_struct *sp)
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
	__rcu_dereference_check((p), (c) || srcu_read_lock_held(sp), __rcu)

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
	int retval;

	retval = __srcu_read_lock(sp);
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
 * smp_mb__after_srcu_read_unlock - ensure full ordering after srcu_read_unlock
 *
 * Converts the preceding srcu_read_unlock into a two-way memory barrier.
 *
 * Call this after srcu_read_unlock, to guarantee that all memory operations
 * that occur after smp_mb__after_srcu_read_unlock will appear to happen after
 * the preceding srcu_read_unlock.
 */
static inline void smp_mb__after_srcu_read_unlock(void)
{
	/* __srcu_read_unlock has smp_mb() internally so nothing to do here. */
}

#endif
