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
 * Copyright (C) Fujitsu, 2012
 *
 * Author: Paul McKenney <paulmck@us.ibm.com>
 *	   Lai Jiangshan <laijs@cn.fujitsu.com>
 *
 * For detailed explanation of Read-Copy Update mechanism see -
 * 		Documentation/RCU/ *.txt
 *
 */

#ifndef _LINUX_SRCU_H
#define _LINUX_SRCU_H

#include <linux/mutex.h>
#include <linux/rcupdate.h>
#include <linux/workqueue.h>

struct srcu_struct_array {
	unsigned long c[2];
	unsigned long seq[2];
};

struct rcu_batch {
	struct rcu_head *head, **tail;
};

#define RCU_BATCH_INIT(name) { NULL, &(name.head) }

struct srcu_struct {
	unsigned completed;
	struct srcu_struct_array __percpu *per_cpu_ref;
	spinlock_t queue_lock; /* protect ->batch_queue, ->running */
	bool running;
	/* callbacks just queued */
	struct rcu_batch batch_queue;
	/* callbacks try to do the first check_zero */
	struct rcu_batch batch_check0;
	/* callbacks done with the first check_zero and the flip */
	struct rcu_batch batch_check1;
	struct rcu_batch batch_done;
	struct delayed_work work;
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map dep_map;
#endif /* #ifdef CONFIG_DEBUG_LOCK_ALLOC */
};

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

void process_srcu(struct work_struct *work);

#define __SRCU_STRUCT_INIT(name)					\
	{								\
		.completed = -300,					\
		.per_cpu_ref = &name##_srcu_array,			\
		.queue_lock = __SPIN_LOCK_UNLOCKED(name.queue_lock),	\
		.running = false,					\
		.batch_queue = RCU_BATCH_INIT(name.batch_queue),	\
		.batch_check0 = RCU_BATCH_INIT(name.batch_check0),	\
		.batch_check1 = RCU_BATCH_INIT(name.batch_check1),	\
		.batch_done = RCU_BATCH_INIT(name.batch_done),		\
		.work = __DELAYED_WORK_INITIALIZER(name.work, process_srcu, 0),\
		__SRCU_DEP_MAP_INIT(name)				\
	}

/*
 * define and init a srcu struct at build time.
 * dont't call init_srcu_struct() nor cleanup_srcu_struct() on it.
 */
#define DEFINE_SRCU(name)						\
	static DEFINE_PER_CPU(struct srcu_struct_array, name##_srcu_array);\
	struct srcu_struct name = __SRCU_STRUCT_INIT(name);

#define DEFINE_STATIC_SRCU(name)					\
	static DEFINE_PER_CPU(struct srcu_struct_array, name##_srcu_array);\
	static struct srcu_struct name = __SRCU_STRUCT_INIT(name);

/**
 * call_srcu() - Queue a callback for invocation after an SRCU grace period
 * @sp: srcu_struct in queue the callback
 * @head: structure to be used for queueing the SRCU callback.
 * @func: function to be invoked after the SRCU grace period
 *
 * The callback function will be invoked some time after a full SRCU
 * grace period elapses, in other words after all pre-existing SRCU
 * read-side critical sections have completed.  However, the callback
 * function might well execute concurrently with other SRCU read-side
 * critical sections that started after call_srcu() was invoked.  SRCU
 * read-side critical sections are delimited by srcu_read_lock() and
 * srcu_read_unlock(), and may be nested.
 *
 * The callback will be invoked from process context, but must nevertheless
 * be fast and must not block.
 */
void call_srcu(struct srcu_struct *sp, struct rcu_head *head,
		void (*func)(struct rcu_head *head));

void cleanup_srcu_struct(struct srcu_struct *sp);
int __srcu_read_lock(struct srcu_struct *sp) __acquires(sp);
void __srcu_read_unlock(struct srcu_struct *sp, int idx) __releases(sp);
void synchronize_srcu(struct srcu_struct *sp);
void synchronize_srcu_expedited(struct srcu_struct *sp);
long srcu_batches_completed(struct srcu_struct *sp);
void srcu_barrier(struct srcu_struct *sp);

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
 * Note that SRCU is based on its own statemachine and it doesn't
 * relies on normal RCU, it can be called from the CPU which
 * is in the idle loop from an RCU point of view or offline.
 */
static inline int srcu_read_lock_held(struct srcu_struct *sp)
{
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
