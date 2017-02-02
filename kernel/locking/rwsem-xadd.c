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
 */
#include <linux/rwsem.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/sched/signal.h>
#include <linux/sched/rt.h>
#include <linux/sched/wake_q.h>
#include <linux/osq_lock.h>

#include "rwsem.h"

/*
 * Guide to the rw_semaphore's count field for common values.
 * (32-bit case illustrated, similar for 64-bit)
 *
 * 0x0000000X	(1) X readers active or attempting lock, no writer waiting
 *		    X = #active_readers + #readers attempting to lock
 *		    (X*ACTIVE_BIAS)
 *
 * 0x00000000	rwsem is unlocked, and no one is waiting for the lock or
 *		attempting to read lock or write lock.
 *
 * 0xffff000X	(1) X readers active or attempting lock, with waiters for lock
 *		    X = #active readers + # readers attempting lock
 *		    (X*ACTIVE_BIAS + WAITING_BIAS)
 *		(2) 1 writer attempting lock, no waiters for lock
 *		    X-1 = #active readers + #readers attempting lock
 *		    ((X-1)*ACTIVE_BIAS + ACTIVE_WRITE_BIAS)
 *		(3) 1 writer active, no waiters for lock
 *		    X-1 = #active readers + #readers attempting lock
 *		    ((X-1)*ACTIVE_BIAS + ACTIVE_WRITE_BIAS)
 *
 * 0xffff0001	(1) 1 reader active or attempting lock, waiters for lock
 *		    (WAITING_BIAS + ACTIVE_BIAS)
 *		(2) 1 writer active or attempting lock, no waiters for lock
 *		    (ACTIVE_WRITE_BIAS)
 *
 * 0xffff0000	(1) There are writers or readers queued but none active
 *		    or in the process of attempting lock.
 *		    (WAITING_BIAS)
 *		Note: writer can attempt to steal lock for this count by adding
 *		ACTIVE_WRITE_BIAS in cmpxchg and checking the old count
 *
 * 0xfffe0001	(1) 1 writer active, or attempting lock. Waiters on queue.
 *		    (ACTIVE_WRITE_BIAS + WAITING_BIAS)
 *
 * Note: Readers attempt to lock by adding ACTIVE_BIAS in down_read and checking
 *	 the count becomes more than 0 for successful lock acquisition,
 *	 i.e. the case where there are only readers or nobody has lock.
 *	 (1st and 2nd case above).
 *
 *	 Writers attempt to lock by adding ACTIVE_WRITE_BIAS in down_write and
 *	 checking the count becomes ACTIVE_WRITE_BIAS for successful lock
 *	 acquisition (i.e. nobody else has lock or attempts lock).  If
 *	 unsuccessful, in rwsem_down_write_failed, we'll check to see if there
 *	 are only waiters but none active (5th case above), and attempt to
 *	 steal the lock.
 *
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
#ifdef CONFIG_RWSEM_SPIN_ON_OWNER
	sem->owner = NULL;
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
 * - if we come here from up_xxxx(), then:
 *   - the 'active part' of count (&0x0000ffff) reached 0 (but may have changed)
 *   - the 'waiting part' of count (&0xffff0000) is -ve (and will still be so)
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
		}

		return;
	}

	/*
	 * Writers might steal the lock before we grant it to the next reader.
	 * We prefer to do the first reader grant before counting readers
	 * so we can bail out early if a writer stole the lock.
	 */
	if (wake_type != RWSEM_WAKE_READ_OWNED) {
		adjustment = RWSEM_ACTIVE_READ_BIAS;
 try_reader_grant:
		oldcount = atomic_long_fetch_add(adjustment, &sem->count);
		if (unlikely(oldcount < RWSEM_WAITING_BIAS)) {
			/*
			 * If the count is still less than RWSEM_WAITING_BIAS
			 * after removing the adjustment, it is assumed that
			 * a writer has stolen the lock. We have to undo our
			 * reader grant.
			 */
			if (atomic_long_add_return(-adjustment, &sem->count) <
			    RWSEM_WAITING_BIAS)
				return;

			/* Last active locker left. Retry waking readers. */
			goto try_reader_grant;
		}
		/*
		 * It is not really necessary to set it to reader-owned here,
		 * but it gives the spinners an early indication that the
		 * readers now have the lock.
		 */
		rwsem_set_reader_owned(sem);
	}

	/*
	 * Grant an infinite number of read locks to the readers at the front
	 * of the queue. We know that woken will be at least 1 as we accounted
	 * for above. Note we increment the 'active part' of the count by the
	 * number of readers before waking any processes up.
	 */
	list_for_each_entry_safe(waiter, tmp, &sem->wait_list, list) {
		struct task_struct *tsk;

		if (waiter->type == RWSEM_WAITING_FOR_WRITE)
			break;

		woken++;
		tsk = waiter->task;

		wake_q_add(wake_q, tsk);
		list_del(&waiter->list);
		/*
		 * Ensure that the last operation is setting the reader
		 * waiter to nil such that rwsem_down_read_failed() cannot
		 * race with do_exit() by always holding a reference count
		 * to the task to wakeup.
		 */
		smp_store_release(&waiter->task, NULL);
	}

	adjustment = woken * RWSEM_ACTIVE_READ_BIAS - adjustment;
	if (list_empty(&sem->wait_list)) {
		/* hit end of list above */
		adjustment -= RWSEM_WAITING_BIAS;
	}

	if (adjustment)
		atomic_long_add(adjustment, &sem->count);
}

/*
 * Wait for the read lock to be granted
 */
__visible
struct rw_semaphore __sched *rwsem_down_read_failed(struct rw_semaphore *sem)
{
	long count, adjustment = -RWSEM_ACTIVE_READ_BIAS;
	struct rwsem_waiter waiter;
	DEFINE_WAKE_Q(wake_q);

	waiter.task = current;
	waiter.type = RWSEM_WAITING_FOR_READ;

	raw_spin_lock_irq(&sem->wait_lock);
	if (list_empty(&sem->wait_list))
		adjustment += RWSEM_WAITING_BIAS;
	list_add_tail(&waiter.list, &sem->wait_list);

	/* we're now waiting on the lock, but no longer actively locking */
	count = atomic_long_add_return(adjustment, &sem->count);

	/*
	 * If there are no active locks, wake the front queued process(es).
	 *
	 * If there are no writers and we are first in the queue,
	 * wake our own waiter to join the existing active readers !
	 */
	if (count == RWSEM_WAITING_BIAS ||
	    (count > RWSEM_WAITING_BIAS &&
	     adjustment != -RWSEM_ACTIVE_READ_BIAS))
		__rwsem_mark_wake(sem, RWSEM_WAKE_ANY, &wake_q);

	raw_spin_unlock_irq(&sem->wait_lock);
	wake_up_q(&wake_q);

	/* wait to be given the lock */
	while (true) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		if (!waiter.task)
			break;
		schedule();
	}

	__set_current_state(TASK_RUNNING);
	return sem;
}
EXPORT_SYMBOL(rwsem_down_read_failed);

/*
 * This function must be called with the sem->wait_lock held to prevent
 * race conditions between checking the rwsem wait list and setting the
 * sem->count accordingly.
 */
static inline bool rwsem_try_write_lock(long count, struct rw_semaphore *sem)
{
	/*
	 * Avoid trying to acquire write lock if count isn't RWSEM_WAITING_BIAS.
	 */
	if (count != RWSEM_WAITING_BIAS)
		return false;

	/*
	 * Acquire the lock by trying to set it to ACTIVE_WRITE_BIAS. If there
	 * are other tasks on the wait list, we need to add on WAITING_BIAS.
	 */
	count = list_is_singular(&sem->wait_list) ?
			RWSEM_ACTIVE_WRITE_BIAS :
			RWSEM_ACTIVE_WRITE_BIAS + RWSEM_WAITING_BIAS;

	if (atomic_long_cmpxchg_acquire(&sem->count, RWSEM_WAITING_BIAS, count)
							== RWSEM_WAITING_BIAS) {
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
	long old, count = atomic_long_read(&sem->count);

	while (true) {
		if (!(count == 0 || count == RWSEM_WAITING_BIAS))
			return false;

		old = atomic_long_cmpxchg_acquire(&sem->count, count,
				      count + RWSEM_ACTIVE_WRITE_BIAS);
		if (old == count) {
			rwsem_set_owner(sem);
			return true;
		}

		count = old;
	}
}

static inline bool rwsem_can_spin_on_owner(struct rw_semaphore *sem)
{
	struct task_struct *owner;
	bool ret = true;

	if (need_resched())
		return false;

	rcu_read_lock();
	owner = READ_ONCE(sem->owner);
	if (!rwsem_owner_is_writer(owner)) {
		/*
		 * Don't spin if the rwsem is readers owned.
		 */
		ret = !rwsem_owner_is_reader(owner);
		goto done;
	}

	/*
	 * As lock holder preemption issue, we both skip spinning if task is not
	 * on cpu or its cpu is preempted
	 */
	ret = owner->on_cpu && !vcpu_is_preempted(task_cpu(owner));
done:
	rcu_read_unlock();
	return ret;
}

/*
 * Return true only if we can still spin on the owner field of the rwsem.
 */
static noinline bool rwsem_spin_on_owner(struct rw_semaphore *sem)
{
	struct task_struct *owner = READ_ONCE(sem->owner);

	if (!rwsem_owner_is_writer(owner))
		goto out;

	rcu_read_lock();
	while (sem->owner == owner) {
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
		if (!owner->on_cpu || need_resched() ||
				vcpu_is_preempted(task_cpu(owner))) {
			rcu_read_unlock();
			return false;
		}

		cpu_relax();
	}
	rcu_read_unlock();
out:
	/*
	 * If there is a new owner or the owner is not set, we continue
	 * spinning.
	 */
	return !rwsem_owner_is_reader(READ_ONCE(sem->owner));
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
	return taken;
}

/*
 * Return true if the rwsem has active spinner
 */
static inline bool rwsem_has_spinner(struct rw_semaphore *sem)
{
	return osq_is_locked(&sem->osq);
}

#else
static bool rwsem_optimistic_spin(struct rw_semaphore *sem)
{
	return false;
}

static inline bool rwsem_has_spinner(struct rw_semaphore *sem)
{
	return false;
}
#endif

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

	/* undo write bias from down_write operation, stop active locking */
	count = atomic_long_sub_return(RWSEM_ACTIVE_WRITE_BIAS, &sem->count);

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

	/* we're now waiting on the lock, but no longer actively locking */
	if (waiting) {
		count = atomic_long_read(&sem->count);

		/*
		 * If there were already threads queued before us and there are
		 * no active writers, the lock must be read owned; so we try to
		 * wake any read locks that were queued ahead of us.
		 */
		if (count > RWSEM_WAITING_BIAS) {
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

	} else
		count = atomic_long_add_return(RWSEM_WAITING_BIAS, &sem->count);

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
			set_current_state(state);
		} while ((count = atomic_long_read(&sem->count)) & RWSEM_ACTIVE_MASK);

		raw_spin_lock_irq(&sem->wait_lock);
	}
	__set_current_state(TASK_RUNNING);
	list_del(&waiter.list);
	raw_spin_unlock_irq(&sem->wait_lock);

	return ret;

out_nolock:
	__set_current_state(TASK_RUNNING);
	raw_spin_lock_irq(&sem->wait_lock);
	list_del(&waiter.list);
	if (list_empty(&sem->wait_list))
		atomic_long_add(-RWSEM_WAITING_BIAS, &sem->count);
	else
		__rwsem_mark_wake(sem, RWSEM_WAKE_ANY, &wake_q);
	raw_spin_unlock_irq(&sem->wait_lock);
	wake_up_q(&wake_q);

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

	/*
	 * If a spinner is present, it is not necessary to do the wakeup.
	 * Try to do wakeup only if the trylock succeeds to minimize
	 * spinlock contention which may introduce too much delay in the
	 * unlock operation.
	 *
	 *    spinning writer		up_write/up_read caller
	 *    ---------------		-----------------------
	 * [S]   osq_unlock()		[L]   osq
	 *	 MB			      RMB
	 * [RmW] rwsem_try_write_lock() [RmW] spin_trylock(wait_lock)
	 *
	 * Here, it is important to make sure that there won't be a missed
	 * wakeup while the rwsem is free and the only spinning writer goes
	 * to sleep without taking the rwsem. Even when the spinning writer
	 * is just going to break out of the waiting loop, it will still do
	 * a trylock in rwsem_down_write_failed() before sleeping. IOW, if
	 * rwsem_has_spinner() is true, it will guarantee at least one
	 * trylock attempt on the rwsem later on.
	 */
	if (rwsem_has_spinner(sem)) {
		/*
		 * The smp_rmb() here is to make sure that the spinner
		 * state is consulted before reading the wait_lock.
		 */
		smp_rmb();
		if (!raw_spin_trylock_irqsave(&sem->wait_lock, flags))
			return sem;
		goto locked;
	}
	raw_spin_lock_irqsave(&sem->wait_lock, flags);
locked:

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
