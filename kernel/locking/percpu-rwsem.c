// SPDX-License-Identifier: GPL-2.0-only
#include <linux/atomic.h>
#include <linux/percpu.h>
#include <linux/wait.h>
#include <linux/lockdep.h>
#include <linux/percpu-rwsem.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/errno.h>

int __percpu_init_rwsem(struct percpu_rw_semaphore *sem,
			const char *name, struct lock_class_key *key)
{
	sem->read_count = alloc_percpu(int);
	if (unlikely(!sem->read_count))
		return -ENOMEM;

	rcu_sync_init(&sem->rss);
	rcuwait_init(&sem->writer);
	init_waitqueue_head(&sem->waiters);
	atomic_set(&sem->block, 0);
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	debug_check_no_locks_freed((void *)sem, sizeof(*sem));
	lockdep_init_map(&sem->dep_map, name, key, 0);
#endif
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

static bool __percpu_down_read_trylock(struct percpu_rw_semaphore *sem)
{
	this_cpu_inc(*sem->read_count);

	/*
	 * Due to having preemption disabled the decrement happens on
	 * the same CPU as the increment, avoiding the
	 * increment-on-one-CPU-and-decrement-on-another problem.
	 *
	 * If the reader misses the writer's assignment of sem->block, then the
	 * writer is guaranteed to see the reader's increment.
	 *
	 * Conversely, any readers that increment their sem->read_count after
	 * the writer looks are guaranteed to see the sem->block value, which
	 * in turn means that they are guaranteed to immediately decrement
	 * their sem->read_count, so that it doesn't matter that the writer
	 * missed them.
	 */

	smp_mb(); /* A matches D */

	/*
	 * If !sem->block the critical section starts here, matched by the
	 * release in percpu_up_write().
	 */
	if (likely(!atomic_read_acquire(&sem->block)))
		return true;

	this_cpu_dec(*sem->read_count);

	/* Prod writer to re-evaluate readers_active_check() */
	rcuwait_wake_up(&sem->writer);

	return false;
}

static inline bool __percpu_down_write_trylock(struct percpu_rw_semaphore *sem)
{
	if (atomic_read(&sem->block))
		return false;

	return atomic_xchg(&sem->block, 1) == 0;
}

static bool __percpu_rwsem_trylock(struct percpu_rw_semaphore *sem, bool reader)
{
	if (reader) {
		bool ret;

		preempt_disable();
		ret = __percpu_down_read_trylock(sem);
		preempt_enable();

		return ret;
	}
	return __percpu_down_write_trylock(sem);
}

/*
 * The return value of wait_queue_entry::func means:
 *
 *  <0 - error, wakeup is terminated and the error is returned
 *   0 - no wakeup, a next waiter is tried
 *  >0 - woken, if EXCLUSIVE, counted towards @nr_exclusive.
 *
 * We use EXCLUSIVE for both readers and writers to preserve FIFO order,
 * and play games with the return value to allow waking multiple readers.
 *
 * Specifically, we wake readers until we've woken a single writer, or until a
 * trylock fails.
 */
static int percpu_rwsem_wake_function(struct wait_queue_entry *wq_entry,
				      unsigned int mode, int wake_flags,
				      void *key)
{
	bool reader = wq_entry->flags & WQ_FLAG_CUSTOM;
	struct percpu_rw_semaphore *sem = key;
	struct task_struct *p;

	/* concurrent against percpu_down_write(), can get stolen */
	if (!__percpu_rwsem_trylock(sem, reader))
		return 1;

	p = get_task_struct(wq_entry->private);
	list_del_init(&wq_entry->entry);
	smp_store_release(&wq_entry->private, NULL);

	wake_up_process(p);
	put_task_struct(p);

	return !reader; /* wake (readers until) 1 writer */
}

static void percpu_rwsem_wait(struct percpu_rw_semaphore *sem, bool reader)
{
	DEFINE_WAIT_FUNC(wq_entry, percpu_rwsem_wake_function);
	bool wait;

	spin_lock_irq(&sem->waiters.lock);
	/*
	 * Serialize against the wakeup in percpu_up_write(), if we fail
	 * the trylock, the wakeup must see us on the list.
	 */
	wait = !__percpu_rwsem_trylock(sem, reader);
	if (wait) {
		wq_entry.flags |= WQ_FLAG_EXCLUSIVE | reader * WQ_FLAG_CUSTOM;
		__add_wait_queue_entry_tail(&sem->waiters, &wq_entry);
	}
	spin_unlock_irq(&sem->waiters.lock);

	while (wait) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		if (!smp_load_acquire(&wq_entry.private))
			break;
		schedule();
	}
	__set_current_state(TASK_RUNNING);
}

bool __percpu_down_read(struct percpu_rw_semaphore *sem, bool try)
{
	if (__percpu_down_read_trylock(sem))
		return true;

	if (try)
		return false;

	preempt_enable();
	percpu_rwsem_wait(sem, /* .reader = */ true);
	preempt_disable();

	return true;
}
EXPORT_SYMBOL_GPL(__percpu_down_read);

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
 *
 * Assumes sem->block is set.
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
	might_sleep();
	rwsem_acquire(&sem->dep_map, 0, 0, _RET_IP_);

	/* Notify readers to take the slow path. */
	rcu_sync_enter(&sem->rss);

	/*
	 * Try set sem->block; this provides writer-writer exclusion.
	 * Having sem->block set makes new readers block.
	 */
	if (!__percpu_down_write_trylock(sem))
		percpu_rwsem_wait(sem, /* .reader = */ false);

	/* smp_mb() implied by __percpu_down_write_trylock() on success -- D matches A */

	/*
	 * If they don't see our store of sem->block, then we are guaranteed to
	 * see their sem->read_count increment, and therefore will wait for
	 * them.
	 */

	/* Wait for all active readers to complete. */
	rcuwait_wait_event(&sem->writer, readers_active_check(sem), TASK_UNINTERRUPTIBLE);
}
EXPORT_SYMBOL_GPL(percpu_down_write);

void percpu_up_write(struct percpu_rw_semaphore *sem)
{
	rwsem_release(&sem->dep_map, _RET_IP_);

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
	atomic_set_release(&sem->block, 0);

	/*
	 * Prod any pending reader/writer to make progress.
	 */
	__wake_up(&sem->waiters, TASK_NORMAL, 1, sem);

	/*
	 * Once this completes (at least one RCU-sched grace period hence) the
	 * reader fast path will be available again. Safe to use outside the
	 * exclusive write lock because its counting.
	 */
	rcu_sync_exit(&sem->rss);
}
EXPORT_SYMBOL_GPL(percpu_up_write);
