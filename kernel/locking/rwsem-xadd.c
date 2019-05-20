// SPDX-License-Identifier: GPL-2.0
/* rwsem.c: R/W semaphores: contention handling functions
 *
 * Written by David Howells (dhowells@redhat.com).
 * Derived from arch/i386/kernel/semaphore.c
 *
 * Writer lock-stealing by Alex Shi <alex.shi@intel.com>
 * and Michel Lespinasse <walken@google.com>
 *
 * Optimistic spinning by Tim Chen <tim.c.chen@intel.com>
 * and Davidlohr Bueso <davidlohr@hp.com>. Based on mutexes.
 *
 * Rwsem count bit fields re-definition by Waiman Long <longman@redhat.com>.
 */
#include <linux/rwsem.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/sched/signal.h>
#include <linux/sched/rt.h>
#include <linux/sched/wake_q.h>
#include <linux/sched/debug.h>
#include <linux/osq_lock.h>

#include "rwsem.h"

/*
 * Guide to the rw_semaphore's count field.
 *
 * When the RWSEM_WRITER_LOCKED bit in count is set, the lock is owned
 * by a writer.
 *
 * The lock is owned by readers when
 * (1) the RWSEM_WRITER_LOCKED isn't set in count,
 * (2) some of the reader bits are set in count, and
 * (3) the owner field has RWSEM_READ_OWNED bit set.
 *
 * Having some reader bits set is not enough to guarantee a readers owned
 * lock as the readers may be in the process of backing out from the count
 * and a writer has just released the lock. So another writer may steal
 * the lock immediately after that.
 */

/*
 * Initialize an rwsem:
 */
void __init_rwsem(struct rw_semaphore *sem, const char *name,
		  struct lock_class_key *key)
{
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	/*
	 * Make sure we are not reinitializing a held semaphore:
	 */
	debug_check_no_locks_freed((void *)sem, sizeof(*sem));
	lockdep_init_map(&sem->dep_map, name, key, 0);
#endif
	atomic_long_set(&sem->count, RWSEM_UNLOCKED_VALUE);
	raw_spin_lock_init(&sem->wait_lock);
	INIT_LIST_HEAD(&sem->wait_list);
	sem->owner = NULL;
#ifdef CONFIG_RWSEM_SPIN_ON_OWNER
	osq_lock_init(&sem->osq);
#endif
}

EXPORT_SYMBOL(__init_rwsem);

enum rwsem_waiter_type {
	RWSEM_WAITING_FOR_WRITE,
	RWSEM_WAITING_FOR_READ
};

struct rwsem_waiter {
	struct list_head list;
	struct task_struct *task;
	enum rwsem_waiter_type type;
};

enum rwsem_wake_type {
	RWSEM_WAKE_ANY,		/* Wake whatever's at head of wait list */
	RWSEM_WAKE_READERS,	/* Wake readers only */
	RWSEM_WAKE_READ_OWNED	/* Waker thread holds the read lock */
};

/*
 * handle the lock release when processes blocked on it that can now run
 * - if we come here from up_xxxx(), then the RWSEM_FLAG_WAITERS bit must
 *   have been set.
 * - there must be someone on the queue
 * - the wait_lock must be held by the caller
 * - tasks are marked for wakeup, the caller must later invoke wake_up_q()
 *   to actually wakeup the blocked task(s) and drop the reference count,
 *   preferably when the wait_lock is released
 * - woken process blocks are discarded from the list after having task zeroed
 * - writers are only marked woken if downgrading is false
 */
static void __rwsem_mark_wake(struct rw_semaphore *sem,
			      enum rwsem_wake_type wake_type,
			      struct wake_q_head *wake_q)
{
	struct rwsem_waiter *waiter, *tmp;
	long oldcount, woken = 0, adjustment = 0;
	struct list_head wlist;

	/*
	 * Take a peek at the queue head waiter such that we can determine
	 * the wakeup(s) to perform.
	 */
	waiter = list_first_entry(&sem->wait_list, struct rwsem_waiter, list);

	if (waiter->type == RWSEM_WAITING_FOR_WRITE) {
		if (wake_type == RWSEM_WAKE_ANY) {
			/*
			 * Mark writer at the front of the queue for wakeup.
			 * Until the task is actually later awoken later by
			 * the caller, other writers are able to steal it.
			 * Readers, on the other hand, will block as they
			 * will notice the queued writer.
			 */
			wake_q_add(wake_q, waiter->task);
			lockevent_inc(rwsem_wake_writer);
		}

		return;
	}

	/*
	 * Writers might steal the lock before we grant it to the next reader.
	 * We prefer to do the first reader grant before counting readers
	 * so we can bail out early if a writer stole the lock.
	 */
	if (wake_type != RWSEM_WAKE_READ_OWNED) {
		adjustment = RWSEM_READER_BIAS;
		oldcount = atomic_long_fetch_add(adjustment, &sem->count);
		if (unlikely(oldcount & RWSEM_WRITER_MASK)) {
			atomic_long_sub(adjustment, &sem->count);
			return;
		}
		/*
		 * Set it to reader-owned to give spinners an early
		 * indication that readers now have the lock.
		 */
		__rwsem_set_reader_owned(sem, waiter->task);
	}

	/*
	 * Grant an infinite number of read locks to the readers at the front
	 * of the queue. We know that woken will be at least 1 as we accounted
	 * for above. Note we increment the 'active part' of the count by the
	 * number of readers before waking any processes up.
	 *
	 * We have to do wakeup in 2 passes to prevent the possibility that
	 * the reader count may be decremented before it is incremented. It
	 * is because the to-be-woken waiter may not have slept yet. So it
	 * may see waiter->task got cleared, finish its critical section and
	 * do an unlock before the reader count increment.
	 *
	 * 1) Collect the read-waiters in a separate list, count them and
	 *    fully increment the reader count in rwsem.
	 * 2) For each waiters in the new list, clear waiter->task and
	 *    put them into wake_q to be woken up later.
	 */
	list_for_each_entry(waiter, &sem->wait_list, list) {
		if (waiter->type == RWSEM_WAITING_FOR_WRITE)
			break;

		woken++;
	}
	list_cut_before(&wlist, &sem->wait_list, &waiter->list);

	adjustment = woken * RWSEM_READER_BIAS - adjustment;
	lockevent_cond_inc(rwsem_wake_reader, woken);
	if (list_empty(&sem->wait_list)) {
		/* hit end of list above */
		adjustment -= RWSEM_FLAG_WAITERS;
	}

	if (adjustment)
		atomic_long_add(adjustment, &sem->count);

	/* 2nd pass */
	list_for_each_entry_safe(waiter, tmp, &wlist, list) {
		struct task_struct *tsk;

		tsk = waiter->task;
		get_task_struct(tsk);

		/*
		 * Ensure calling get_task_struct() before setting the reader
		 * waiter to nil such that rwsem_down_read_failed() cannot
		 * race with do_exit() by always holding a reference count
		 * to the task to wakeup.
		 */
		smp_store_release(&waiter->task, NULL);
		/*
		 * Ensure issuing the wakeup (either by us or someone else)
		 * after setting the reader waiter to nil.
		 */
		wake_q_add_safe(wake_q, tsk);
	}
}

/*
 * This function must be called with the sem->wait_lock held to prevent
 * race conditions between checking the rwsem wait list and setting the
 * sem->count accordingly.
 */
static inline bool rwsem_try_write_lock(long count, struct rw_semaphore *sem)
{
	long new;

	if (count & RWSEM_LOCK_MASK)
		return false;

	new = count + RWSEM_WRITER_LOCKED -
	     (list_is_singular(&sem->wait_list) ? RWSEM_FLAG_WAITERS : 0);

	if (atomic_long_try_cmpxchg_acquire(&sem->count, &count, new)) {
		rwsem_set_owner(sem);
		return true;
	}

	return false;
}

#ifdef CONFIG_RWSEM_SPIN_ON_OWNER
/*
 * Try to acquire write lock before the writer has been put on wait queue.
 */
static inline bool rwsem_try_write_lock_unqueued(struct rw_semaphore *sem)
{
	long count = atomic_long_read(&sem->count);

	while (!(count & RWSEM_LOCK_MASK)) {
		if (atomic_long_try_cmpxchg_acquire(&sem->count, &count,
					count + RWSEM_WRITER_LOCKED)) {
			rwsem_set_owner(sem);
			lockevent_inc(rwsem_opt_wlock);
			return true;
		}
	}
	return false;
}

static inline bool owner_on_cpu(struct task_struct *owner)
{
	/*
	 * As lock holder preemption issue, we both skip spinning if
	 * task is not on cpu or its cpu is preempted
	 */
	return owner->on_cpu && !vcpu_is_preempted(task_cpu(owner));
}

static inline bool rwsem_can_spin_on_owner(struct rw_semaphore *sem)
{
	struct task_struct *owner;
	bool ret = true;

	BUILD_BUG_ON(!rwsem_has_anonymous_owner(RWSEM_OWNER_UNKNOWN));

	if (need_resched())
		return false;

	rcu_read_lock();
	owner = READ_ONCE(sem->owner);
	if (owner) {
		ret = is_rwsem_owner_spinnable(owner) &&
		      owner_on_cpu(owner);
	}
	rcu_read_unlock();
	return ret;
}

/*
 * Return true only if we can still spin on the owner field of the rwsem.
 */
static noinline bool rwsem_spin_on_owner(struct rw_semaphore *sem)
{
	struct task_struct *owner = READ_ONCE(sem->owner);

	if (!is_rwsem_owner_spinnable(owner))
		return false;

	rcu_read_lock();
	while (owner && (READ_ONCE(sem->owner) == owner)) {
		/*
		 * Ensure we emit the owner->on_cpu, dereference _after_
		 * checking sem->owner still matches owner, if that fails,
		 * owner might point to free()d memory, if it still matches,
		 * the rcu_read_lock() ensures the memory stays valid.
		 */
		barrier();

		/*
		 * abort spinning when need_resched or owner is not running or
		 * owner's cpu is preempted.
		 */
		if (need_resched() || !owner_on_cpu(owner)) {
			rcu_read_unlock();
			return false;
		}

		cpu_relax();
	}
	rcu_read_unlock();

	/*
	 * If there is a new owner or the owner is not set, we continue
	 * spinning.
	 */
	return is_rwsem_owner_spinnable(READ_ONCE(sem->owner));
}

static bool rwsem_optimistic_spin(struct rw_semaphore *sem)
{
	bool taken = false;

	preempt_disable();

	/* sem->wait_lock should not be held when doing optimistic spinning */
	if (!rwsem_can_spin_on_owner(sem))
		goto done;

	if (!osq_lock(&sem->osq))
		goto done;

	/*
	 * Optimistically spin on the owner field and attempt to acquire the
	 * lock whenever the owner changes. Spinning will be stopped when:
	 *  1) the owning writer isn't running; or
	 *  2) readers own the lock as we can't determine if they are
	 *     actively running or not.
	 */
	while (rwsem_spin_on_owner(sem)) {
		/*
		 * Try to acquire the lock
		 */
		if (rwsem_try_write_lock_unqueued(sem)) {
			taken = true;
			break;
		}

		/*
		 * When there's no owner, we might have preempted between the
		 * owner acquiring the lock and setting the owner field. If
		 * we're an RT task that will live-lock because we won't let
		 * the owner complete.
		 */
		if (!sem->owner && (need_resched() || rt_task(current)))
			break;

		/*
		 * The cpu_relax() call is a compiler barrier which forces
		 * everything in this loop to be re-loaded. We don't need
		 * memory barriers as we'll eventually observe the right
		 * values at the cost of a few extra spins.
		 */
		cpu_relax();
	}
	osq_unlock(&sem->osq);
done:
	preempt_enable();
	lockevent_cond_inc(rwsem_opt_fail, !taken);
	return taken;
}
#else
static bool rwsem_optimistic_spin(struct rw_semaphore *sem)
{
	return false;
}
#endif

/*
 * Wait for the read lock to be granted
 */
static inline struct rw_semaphore __sched *
__rwsem_down_read_failed_common(struct rw_semaphore *sem, int state)
{
	long count, adjustment = -RWSEM_READER_BIAS;
	struct rwsem_waiter waiter;
	DEFINE_WAKE_Q(wake_q);

	waiter.task = current;
	waiter.type = RWSEM_WAITING_FOR_READ;

	raw_spin_lock_irq(&sem->wait_lock);
	if (list_empty(&sem->wait_list)) {
		/*
		 * In case the wait queue is empty and the lock isn't owned
		 * by a writer, this reader can exit the slowpath and return
		 * immediately as its RWSEM_READER_BIAS has already been
		 * set in the count.
		 */
		if (!(atomic_long_read(&sem->count) & RWSEM_WRITER_MASK)) {
			raw_spin_unlock_irq(&sem->wait_lock);
			rwsem_set_reader_owned(sem);
			lockevent_inc(rwsem_rlock_fast);
			return sem;
		}
		adjustment += RWSEM_FLAG_WAITERS;
	}
	list_add_tail(&waiter.list, &sem->wait_list);

	/* we're now waiting on the lock, but no longer actively locking */
	count = atomic_long_add_return(adjustment, &sem->count);

	/*
	 * If there are no active locks, wake the front queued process(es).
	 *
	 * If there are no writers and we are first in the queue,
	 * wake our own waiter to join the existing active readers !
	 */
	if (!(count & RWSEM_LOCK_MASK) ||
	   (!(count & RWSEM_WRITER_MASK) && (adjustment & RWSEM_FLAG_WAITERS)))
		__rwsem_mark_wake(sem, RWSEM_WAKE_ANY, &wake_q);

	raw_spin_unlock_irq(&sem->wait_lock);
	wake_up_q(&wake_q);

	/* wait to be given the lock */
	while (true) {
		set_current_state(state);
		if (!waiter.task)
			break;
		if (signal_pending_state(state, current)) {
			raw_spin_lock_irq(&sem->wait_lock);
			if (waiter.task)
				goto out_nolock;
			raw_spin_unlock_irq(&sem->wait_lock);
			break;
		}
		schedule();
		lockevent_inc(rwsem_sleep_reader);
	}

	__set_current_state(TASK_RUNNING);
	lockevent_inc(rwsem_rlock);
	return sem;
out_nolock:
	list_del(&waiter.list);
	if (list_empty(&sem->wait_list))
		atomic_long_andnot(RWSEM_FLAG_WAITERS, &sem->count);
	raw_spin_unlock_irq(&sem->wait_lock);
	__set_current_state(TASK_RUNNING);
	lockevent_inc(rwsem_rlock_fail);
	return ERR_PTR(-EINTR);
}

__visible struct rw_semaphore * __sched
rwsem_down_read_failed(struct rw_semaphore *sem)
{
	return __rwsem_down_read_failed_common(sem, TASK_UNINTERRUPTIBLE);
}
EXPORT_SYMBOL(rwsem_down_read_failed);

__visible struct rw_semaphore * __sched
rwsem_down_read_failed_killable(struct rw_semaphore *sem)
{
	return __rwsem_down_read_failed_common(sem, TASK_KILLABLE);
}
EXPORT_SYMBOL(rwsem_down_read_failed_killable);

/*
 * Wait until we successfully acquire the write lock
 */
static inline struct rw_semaphore *
__rwsem_down_write_failed_common(struct rw_semaphore *sem, int state)
{
	long count;
	bool waiting = true; /* any queued threads before us */
	struct rwsem_waiter waiter;
	struct rw_semaphore *ret = sem;
	DEFINE_WAKE_Q(wake_q);

	/* do optimistic spinning and steal lock if possible */
	if (rwsem_optimistic_spin(sem))
		return sem;

	/*
	 * Optimistic spinning failed, proceed to the slowpath
	 * and block until we can acquire the sem.
	 */
	waiter.task = current;
	waiter.type = RWSEM_WAITING_FOR_WRITE;

	raw_spin_lock_irq(&sem->wait_lock);

	/* account for this before adding a new element to the list */
	if (list_empty(&sem->wait_list))
		waiting = false;

	list_add_tail(&waiter.list, &sem->wait_list);

	/* we're now waiting on the lock */
	if (waiting) {
		count = atomic_long_read(&sem->count);

		/*
		 * If there were already threads queued before us and there are
		 * no active writers and some readers, the lock must be read
		 * owned; so we try to  any read locks that were queued ahead
		 * of us.
		 */
		if (!(count & RWSEM_WRITER_MASK) &&
		     (count & RWSEM_READER_MASK)) {
			__rwsem_mark_wake(sem, RWSEM_WAKE_READERS, &wake_q);
			/*
			 * The wakeup is normally called _after_ the wait_lock
			 * is released, but given that we are proactively waking
			 * readers we can deal with the wake_q overhead as it is
			 * similar to releasing and taking the wait_lock again
			 * for attempting rwsem_try_write_lock().
			 */
			wake_up_q(&wake_q);

			/*
			 * Reinitialize wake_q after use.
			 */
			wake_q_init(&wake_q);
		}

	} else {
		count = atomic_long_add_return(RWSEM_FLAG_WAITERS, &sem->count);
	}

	/* wait until we successfully acquire the lock */
	set_current_state(state);
	while (true) {
		if (rwsem_try_write_lock(count, sem))
			break;
		raw_spin_unlock_irq(&sem->wait_lock);

		/* Block until there are no active lockers. */
		do {
			if (signal_pending_state(state, current))
				goto out_nolock;

			schedule();
			lockevent_inc(rwsem_sleep_writer);
			set_current_state(state);
			count = atomic_long_read(&sem->count);
		} while (count & RWSEM_LOCK_MASK);

		raw_spin_lock_irq(&sem->wait_lock);
	}
	__set_current_state(TASK_RUNNING);
	list_del(&waiter.list);
	raw_spin_unlock_irq(&sem->wait_lock);
	lockevent_inc(rwsem_wlock);

	return ret;

out_nolock:
	__set_current_state(TASK_RUNNING);
	raw_spin_lock_irq(&sem->wait_lock);
	list_del(&waiter.list);
	if (list_empty(&sem->wait_list))
		atomic_long_andnot(RWSEM_FLAG_WAITERS, &sem->count);
	else
		__rwsem_mark_wake(sem, RWSEM_WAKE_ANY, &wake_q);
	raw_spin_unlock_irq(&sem->wait_lock);
	wake_up_q(&wake_q);
	lockevent_inc(rwsem_wlock_fail);

	return ERR_PTR(-EINTR);
}

__visible struct rw_semaphore * __sched
rwsem_down_write_failed(struct rw_semaphore *sem)
{
	return __rwsem_down_write_failed_common(sem, TASK_UNINTERRUPTIBLE);
}
EXPORT_SYMBOL(rwsem_down_write_failed);

__visible struct rw_semaphore * __sched
rwsem_down_write_failed_killable(struct rw_semaphore *sem)
{
	return __rwsem_down_write_failed_common(sem, TASK_KILLABLE);
}
EXPORT_SYMBOL(rwsem_down_write_failed_killable);

/*
 * handle waking up a waiter on the semaphore
 * - up_read/up_write has decremented the active part of count if we come here
 */
__visible
struct rw_semaphore *rwsem_wake(struct rw_semaphore *sem)
{
	unsigned long flags;
	DEFINE_WAKE_Q(wake_q);

	raw_spin_lock_irqsave(&sem->wait_lock, flags);

	if (!list_empty(&sem->wait_list))
		__rwsem_mark_wake(sem, RWSEM_WAKE_ANY, &wake_q);

	raw_spin_unlock_irqrestore(&sem->wait_lock, flags);
	wake_up_q(&wake_q);

	return sem;
}
EXPORT_SYMBOL(rwsem_wake);

/*
 * downgrade a write lock into a read lock
 * - caller incremented waiting part of count and discovered it still negative
 * - just wake up any readers at the front of the queue
 */
__visible
struct rw_semaphore *rwsem_downgrade_wake(struct rw_semaphore *sem)
{
	unsigned long flags;
	DEFINE_WAKE_Q(wake_q);

	raw_spin_lock_irqsave(&sem->wait_lock, flags);

	if (!list_empty(&sem->wait_list))
		__rwsem_mark_wake(sem, RWSEM_WAKE_READ_OWNED, &wake_q);

	raw_spin_unlock_irqrestore(&sem->wait_lock, flags);
	wake_up_q(&wake_q);

	return sem;
}
EXPORT_SYMBOL(rwsem_downgrade_wake);
