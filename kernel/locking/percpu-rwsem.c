// SPDX-License-Identifier: GPL-2.0-only
#include <linux/atomic.h>
#include <linux/rwsem.h>
#include <linux/percpu.h>
#include <linux/lockdep.h>
#include <linux/percpu-rwsem.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/errno.h>

#include "rwsem.h"

int __percpu_init_rwsem(struct percpu_rw_semaphore *sem,
			const char *name, struct lock_class_key *rwsem_key)
{
	sem->read_count = alloc_percpu(int);
	if (unlikely(!sem->read_count))
		return -ENOMEM;

	/* ->rw_sem represents the whole percpu_rw_semaphore for lockdep */
	rcu_sync_init(&sem->rss);
	__init_rwsem(&sem->rw_sem, name, rwsem_key);
	rcuwait_init(&sem->writer);
	sem->readers_block = 0;
	return 0;
}
EXPORT_SYMBOL_GPL(__percpu_init_rwsem);

void percpu_free_rwsem(struct percpu_rw_semaphore *sem)
{
	/*
	 * XXX: temporary kludge. The error path in alloc_super()
	 * assumes that percpu_free_rwsem() is safe after kzalloc().
	 */
	if (!sem->read_count)
		return;

	rcu_sync_dtor(&sem->rss);
	free_percpu(sem->read_count);
	sem->read_count = NULL; /* catch use after free bugs */
}
EXPORT_SYMBOL_GPL(percpu_free_rwsem);

int __percpu_down_read(struct percpu_rw_semaphore *sem, int try)
{
	/*
	 * Due to having preemption disabled the decrement happens on
	 * the same CPU as the increment, avoiding the
	 * increment-on-one-CPU-and-decrement-on-another problem.
	 *
	 * If the reader misses the writer's assignment of readers_block, then
	 * the writer is guaranteed to see the reader's increment.
	 *
	 * Conversely, any readers that increment their sem->read_count after
	 * the writer looks are guaranteed to see the readers_block value,
	 * which in turn means that they are guaranteed to immediately
	 * decrement their sem->read_count, so that it doesn't matter that the
	 * writer missed them.
	 */

	smp_mb(); /* A matches D */

	/*
	 * If !readers_block the critical section starts here, matched by the
	 * release in percpu_up_write().
	 */
	if (likely(!smp_load_acquire(&sem->readers_block)))
		return 1;

	/*
	 * Per the above comment; we still have preemption disabled and
	 * will thus decrement on the same CPU as we incremented.
	 */
	__percpu_up_read(sem);

	if (try)
		return 0;

	/*
	 * We either call schedule() in the wait, or we'll fall through
	 * and reschedule on the preempt_enable() in percpu_down_read().
	 */
	preempt_enable_no_resched();

	/*
	 * Avoid lockdep for the down/up_read() we already have them.
	 */
	__down_read(&sem->rw_sem);
	this_cpu_inc(*sem->read_count);
	__up_read(&sem->rw_sem);

	preempt_disable();
	return 1;
}
EXPORT_SYMBOL_GPL(__percpu_down_read);

void __percpu_up_read(struct percpu_rw_semaphore *sem)
{
	smp_mb(); /* B matches C */
	/*
	 * In other words, if they see our decrement (presumably to aggregate
	 * zero, as that is the only time it matters) they will also see our
	 * critical section.
	 */
	__this_cpu_dec(*sem->read_count);

	/* Prod writer to recheck readers_active */
	rcuwait_wake_up(&sem->writer);
}
EXPORT_SYMBOL_GPL(__percpu_up_read);

#define per_cpu_sum(var)						\
({									\
	typeof(var) __sum = 0;						\
	int cpu;							\
	compiletime_assert_atomic_type(__sum);				\
	for_each_possible_cpu(cpu)					\
		__sum += per_cpu(var, cpu);				\
	__sum;								\
})

/*
 * Return true if the modular sum of the sem->read_count per-CPU variable is
 * zero.  If this sum is zero, then it is stable due to the fact that if any
 * newly arriving readers increment a given counter, they will immediately
 * decrement that same counter.
 */
static bool readers_active_check(struct percpu_rw_semaphore *sem)
{
	if (per_cpu_sum(*sem->read_count) != 0)
		return false;

	/*
	 * If we observed the decrement; ensure we see the entire critical
	 * section.
	 */

	smp_mb(); /* C matches B */

	return true;
}

void percpu_down_write(struct percpu_rw_semaphore *sem)
{
	/* Notify readers to take the slow path. */
	rcu_sync_enter(&sem->rss);

	down_write(&sem->rw_sem);

	/*
	 * Notify new readers to block; up until now, and thus throughout the
	 * longish rcu_sync_enter() above, new readers could still come in.
	 */
	WRITE_ONCE(sem->readers_block, 1);

	smp_mb(); /* D matches A */

	/*
	 * If they don't see our writer of readers_block, then we are
	 * guaranteed to see their sem->read_count increment, and therefore
	 * will wait for them.
	 */

	/* Wait for all now active readers to complete. */
	rcuwait_wait_event(&sem->writer, readers_active_check(sem));
}
EXPORT_SYMBOL_GPL(percpu_down_write);

void percpu_up_write(struct percpu_rw_semaphore *sem)
{
	/*
	 * Signal the writer is done, no fast path yet.
	 *
	 * One reason that we cannot just immediately flip to readers_fast is
	 * that new readers might fail to see the results of this writer's
	 * critical section.
	 *
	 * Therefore we force it through the slow path which guarantees an
	 * acquire and thereby guarantees the critical section's consistency.
	 */
	smp_store_release(&sem->readers_block, 0);

	/*
	 * Release the write lock, this will allow readers back in the game.
	 */
	up_write(&sem->rw_sem);

	/*
	 * Once this completes (at least one RCU-sched grace period hence) the
	 * reader fast path will be available again. Safe to use outside the
	 * exclusive write lock because its counting.
	 */
	rcu_sync_exit(&sem->rss);
}
EXPORT_SYMBOL_GPL(percpu_up_write);
