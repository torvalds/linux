// SPDX-License-Identifier: GPL-2.0-only
/*
 * rtmutex API
 */
#include <linux/spinlock.h>
#include <linux/export.h>

#define RT_MUTEX_BUILD_MUTEX
#include "rtmutex.c"

/*
 * Max number of times we'll walk the boosting chain:
 */
int max_lock_depth = 1024;

/*
 * Debug aware fast / slowpath lock,trylock,unlock
 *
 * The atomic acquire/release ops are compiled away, when either the
 * architecture does not support cmpxchg or when debugging is enabled.
 */
static __always_inline int __rt_mutex_lock_common(struct rt_mutex *lock,
						  unsigned int state,
						  unsigned int subclass)
{
	int ret;

	might_sleep();
	mutex_acquire(&lock->dep_map, subclass, 0, _RET_IP_);
	ret = __rt_mutex_lock(&lock->rtmutex, state);
	if (ret)
		mutex_release(&lock->dep_map, _RET_IP_);
	return ret;
}

void rt_mutex_base_init(struct rt_mutex_base *rtb)
{
	__rt_mutex_base_init(rtb);
}
EXPORT_SYMBOL(rt_mutex_base_init);

#ifdef CONFIG_DEBUG_LOCK_ALLOC
/**
 * rt_mutex_lock_nested - lock a rt_mutex
 *
 * @lock: the rt_mutex to be locked
 * @subclass: the lockdep subclass
 */
void __sched rt_mutex_lock_nested(struct rt_mutex *lock, unsigned int subclass)
{
	__rt_mutex_lock_common(lock, TASK_UNINTERRUPTIBLE, subclass);
}
EXPORT_SYMBOL_GPL(rt_mutex_lock_nested);

#else /* !CONFIG_DEBUG_LOCK_ALLOC */

/**
 * rt_mutex_lock - lock a rt_mutex
 *
 * @lock: the rt_mutex to be locked
 */
void __sched rt_mutex_lock(struct rt_mutex *lock)
{
	__rt_mutex_lock_common(lock, TASK_UNINTERRUPTIBLE, 0);
}
EXPORT_SYMBOL_GPL(rt_mutex_lock);
#endif

/**
 * rt_mutex_lock_interruptible - lock a rt_mutex interruptible
 *
 * @lock:		the rt_mutex to be locked
 *
 * Returns:
 *  0		on success
 * -EINTR	when interrupted by a signal
 */
int __sched rt_mutex_lock_interruptible(struct rt_mutex *lock)
{
	return __rt_mutex_lock_common(lock, TASK_INTERRUPTIBLE, 0);
}
EXPORT_SYMBOL_GPL(rt_mutex_lock_interruptible);

/**
 * rt_mutex_trylock - try to lock a rt_mutex
 *
 * @lock:	the rt_mutex to be locked
 *
 * This function can only be called in thread context. It's safe to call it
 * from atomic regions, but not from hard or soft interrupt context.
 *
 * Returns:
 *  1 on success
 *  0 on contention
 */
int __sched rt_mutex_trylock(struct rt_mutex *lock)
{
	int ret;

	if (IS_ENABLED(CONFIG_DEBUG_RT_MUTEXES) && WARN_ON_ONCE(!in_task()))
		return 0;

	ret = __rt_mutex_trylock(&lock->rtmutex);
	if (ret)
		mutex_acquire(&lock->dep_map, 0, 1, _RET_IP_);

	return ret;
}
EXPORT_SYMBOL_GPL(rt_mutex_trylock);

/**
 * rt_mutex_unlock - unlock a rt_mutex
 *
 * @lock: the rt_mutex to be unlocked
 */
void __sched rt_mutex_unlock(struct rt_mutex *lock)
{
	mutex_release(&lock->dep_map, _RET_IP_);
	__rt_mutex_unlock(&lock->rtmutex);
}
EXPORT_SYMBOL_GPL(rt_mutex_unlock);

/*
 * Futex variants, must not use fastpath.
 */
int __sched rt_mutex_futex_trylock(struct rt_mutex_base *lock)
{
	return rt_mutex_slowtrylock(lock);
}

int __sched __rt_mutex_futex_trylock(struct rt_mutex_base *lock)
{
	return __rt_mutex_slowtrylock(lock);
}

/**
 * __rt_mutex_futex_unlock - Futex variant, that since futex variants
 * do not use the fast-path, can be simple and will not need to retry.
 *
 * @lock:	The rt_mutex to be unlocked
 * @wqh:	The wake queue head from which to get the next lock waiter
 */
bool __sched __rt_mutex_futex_unlock(struct rt_mutex_base *lock,
				     struct rt_wake_q_head *wqh)
{
	lockdep_assert_held(&lock->wait_lock);

	debug_rt_mutex_unlock(lock);

	if (!rt_mutex_has_waiters(lock)) {
		lock->owner = NULL;
		return false; /* done */
	}

	/*
	 * We've already deboosted, mark_wakeup_next_waiter() will
	 * retain preempt_disabled when we drop the wait_lock, to
	 * avoid inversion prior to the wakeup.  preempt_disable()
	 * therein pairs with rt_mutex_postunlock().
	 */
	mark_wakeup_next_waiter(wqh, lock);

	return true; /* call postunlock() */
}

void __sched rt_mutex_futex_unlock(struct rt_mutex_base *lock)
{
	DEFINE_RT_WAKE_Q(wqh);
	unsigned long flags;
	bool postunlock;

	raw_spin_lock_irqsave(&lock->wait_lock, flags);
	postunlock = __rt_mutex_futex_unlock(lock, &wqh);
	raw_spin_unlock_irqrestore(&lock->wait_lock, flags);

	if (postunlock)
		rt_mutex_postunlock(&wqh);
}

/**
 * __rt_mutex_init - initialize the rt_mutex
 *
 * @lock:	The rt_mutex to be initialized
 * @name:	The lock name used for debugging
 * @key:	The lock class key used for debugging
 *
 * Initialize the rt_mutex to unlocked state.
 *
 * Initializing of a locked rt_mutex is not allowed
 */
void __sched __rt_mutex_init(struct rt_mutex *lock, const char *name,
			     struct lock_class_key *key)
{
	debug_check_no_locks_freed((void *)lock, sizeof(*lock));
	__rt_mutex_base_init(&lock->rtmutex);
	lockdep_init_map_wait(&lock->dep_map, name, key, 0, LD_WAIT_SLEEP);
}
EXPORT_SYMBOL_GPL(__rt_mutex_init);

/**
 * rt_mutex_init_proxy_locked - initialize and lock a rt_mutex on behalf of a
 *				proxy owner
 *
 * @lock:	the rt_mutex to be locked
 * @proxy_owner:the task to set as owner
 *
 * No locking. Caller has to do serializing itself
 *
 * Special API call for PI-futex support. This initializes the rtmutex and
 * assigns it to @proxy_owner. Concurrent operations on the rtmutex are not
 * possible at this point because the pi_state which contains the rtmutex
 * is not yet visible to other tasks.
 */
void __sched rt_mutex_init_proxy_locked(struct rt_mutex_base *lock,
					struct task_struct *proxy_owner)
{
	static struct lock_class_key pi_futex_key;

	__rt_mutex_base_init(lock);
	/*
	 * On PREEMPT_RT the futex hashbucket spinlock becomes 'sleeping'
	 * and rtmutex based. That causes a lockdep false positive, because
	 * some of the futex functions invoke spin_unlock(&hb->lock) with
	 * the wait_lock of the rtmutex associated to the pi_futex held.
	 * spin_unlock() in turn takes wait_lock of the rtmutex on which
	 * the spinlock is based, which makes lockdep notice a lock
	 * recursion. Give the futex/rtmutex wait_lock a separate key.
	 */
	lockdep_set_class(&lock->wait_lock, &pi_futex_key);
	rt_mutex_set_owner(lock, proxy_owner);
}

/**
 * rt_mutex_proxy_unlock - release a lock on behalf of owner
 *
 * @lock:	the rt_mutex to be locked
 *
 * No locking. Caller has to do serializing itself
 *
 * Special API call for PI-futex support. This just cleans up the rtmutex
 * (debugging) state. Concurrent operations on this rt_mutex are not
 * possible because it belongs to the pi_state which is about to be freed
 * and it is not longer visible to other tasks.
 */
void __sched rt_mutex_proxy_unlock(struct rt_mutex_base *lock)
{
	debug_rt_mutex_proxy_unlock(lock);
	rt_mutex_set_owner(lock, NULL);
}

/**
 * __rt_mutex_start_proxy_lock() - Start lock acquisition for another task
 * @lock:		the rt_mutex to take
 * @waiter:		the pre-initialized rt_mutex_waiter
 * @task:		the task to prepare
 *
 * Starts the rt_mutex acquire; it enqueues the @waiter and does deadlock
 * detection. It does not wait, see rt_mutex_wait_proxy_lock() for that.
 *
 * NOTE: does _NOT_ remove the @waiter on failure; must either call
 * rt_mutex_wait_proxy_lock() or rt_mutex_cleanup_proxy_lock() after this.
 *
 * Returns:
 *  0 - task blocked on lock
 *  1 - acquired the lock for task, caller should wake it up
 * <0 - error
 *
 * Special API call for PI-futex support.
 */
int __sched __rt_mutex_start_proxy_lock(struct rt_mutex_base *lock,
					struct rt_mutex_waiter *waiter,
					struct task_struct *task)
{
	int ret;

	lockdep_assert_held(&lock->wait_lock);

	if (try_to_take_rt_mutex(lock, task, NULL))
		return 1;

	/* We enforce deadlock detection for futexes */
	ret = task_blocks_on_rt_mutex(lock, waiter, task, NULL,
				      RT_MUTEX_FULL_CHAINWALK);

	if (ret && !rt_mutex_owner(lock)) {
		/*
		 * Reset the return value. We might have
		 * returned with -EDEADLK and the owner
		 * released the lock while we were walking the
		 * pi chain.  Let the waiter sort it out.
		 */
		ret = 0;
	}

	return ret;
}

/**
 * rt_mutex_start_proxy_lock() - Start lock acquisition for another task
 * @lock:		the rt_mutex to take
 * @waiter:		the pre-initialized rt_mutex_waiter
 * @task:		the task to prepare
 *
 * Starts the rt_mutex acquire; it enqueues the @waiter and does deadlock
 * detection. It does not wait, see rt_mutex_wait_proxy_lock() for that.
 *
 * NOTE: unlike __rt_mutex_start_proxy_lock this _DOES_ remove the @waiter
 * on failure.
 *
 * Returns:
 *  0 - task blocked on lock
 *  1 - acquired the lock for task, caller should wake it up
 * <0 - error
 *
 * Special API call for PI-futex support.
 */
int __sched rt_mutex_start_proxy_lock(struct rt_mutex_base *lock,
				      struct rt_mutex_waiter *waiter,
				      struct task_struct *task)
{
	int ret;

	raw_spin_lock_irq(&lock->wait_lock);
	ret = __rt_mutex_start_proxy_lock(lock, waiter, task);
	if (unlikely(ret))
		remove_waiter(lock, waiter);
	raw_spin_unlock_irq(&lock->wait_lock);

	return ret;
}

/**
 * rt_mutex_wait_proxy_lock() - Wait for lock acquisition
 * @lock:		the rt_mutex we were woken on
 * @to:			the timeout, null if none. hrtimer should already have
 *			been started.
 * @waiter:		the pre-initialized rt_mutex_waiter
 *
 * Wait for the lock acquisition started on our behalf by
 * rt_mutex_start_proxy_lock(). Upon failure, the caller must call
 * rt_mutex_cleanup_proxy_lock().
 *
 * Returns:
 *  0 - success
 * <0 - error, one of -EINTR, -ETIMEDOUT
 *
 * Special API call for PI-futex support
 */
int __sched rt_mutex_wait_proxy_lock(struct rt_mutex_base *lock,
				     struct hrtimer_sleeper *to,
				     struct rt_mutex_waiter *waiter)
{
	int ret;

	raw_spin_lock_irq(&lock->wait_lock);
	/* sleep on the mutex */
	set_current_state(TASK_INTERRUPTIBLE);
	ret = rt_mutex_slowlock_block(lock, NULL, TASK_INTERRUPTIBLE, to, waiter);
	/*
	 * try_to_take_rt_mutex() sets the waiter bit unconditionally. We might
	 * have to fix that up.
	 */
	fixup_rt_mutex_waiters(lock);
	raw_spin_unlock_irq(&lock->wait_lock);

	return ret;
}

/**
 * rt_mutex_cleanup_proxy_lock() - Cleanup failed lock acquisition
 * @lock:		the rt_mutex we were woken on
 * @waiter:		the pre-initialized rt_mutex_waiter
 *
 * Attempt to clean up after a failed __rt_mutex_start_proxy_lock() or
 * rt_mutex_wait_proxy_lock().
 *
 * Unless we acquired the lock; we're still enqueued on the wait-list and can
 * in fact still be granted ownership until we're removed. Therefore we can
 * find we are in fact the owner and must disregard the
 * rt_mutex_wait_proxy_lock() failure.
 *
 * Returns:
 *  true  - did the cleanup, we done.
 *  false - we acquired the lock after rt_mutex_wait_proxy_lock() returned,
 *          caller should disregards its return value.
 *
 * Special API call for PI-futex support
 */
bool __sched rt_mutex_cleanup_proxy_lock(struct rt_mutex_base *lock,
					 struct rt_mutex_waiter *waiter)
{
	bool cleanup = false;

	raw_spin_lock_irq(&lock->wait_lock);
	/*
	 * Do an unconditional try-lock, this deals with the lock stealing
	 * state where __rt_mutex_futex_unlock() -> mark_wakeup_next_waiter()
	 * sets a NULL owner.
	 *
	 * We're not interested in the return value, because the subsequent
	 * test on rt_mutex_owner() will infer that. If the trylock succeeded,
	 * we will own the lock and it will have removed the waiter. If we
	 * failed the trylock, we're still not owner and we need to remove
	 * ourselves.
	 */
	try_to_take_rt_mutex(lock, current, waiter);
	/*
	 * Unless we're the owner; we're still enqueued on the wait_list.
	 * So check if we became owner, if not, take us off the wait_list.
	 */
	if (rt_mutex_owner(lock) != current) {
		remove_waiter(lock, waiter);
		cleanup = true;
	}
	/*
	 * try_to_take_rt_mutex() sets the waiter bit unconditionally. We might
	 * have to fix that up.
	 */
	fixup_rt_mutex_waiters(lock);

	raw_spin_unlock_irq(&lock->wait_lock);

	return cleanup;
}

/*
 * Recheck the pi chain, in case we got a priority setting
 *
 * Called from sched_setscheduler
 */
void __sched rt_mutex_adjust_pi(struct task_struct *task)
{
	struct rt_mutex_waiter *waiter;
	struct rt_mutex_base *next_lock;
	unsigned long flags;

	raw_spin_lock_irqsave(&task->pi_lock, flags);

	waiter = task->pi_blocked_on;
	if (!waiter || rt_mutex_waiter_equal(waiter, task_to_waiter(task))) {
		raw_spin_unlock_irqrestore(&task->pi_lock, flags);
		return;
	}
	next_lock = waiter->lock;
	raw_spin_unlock_irqrestore(&task->pi_lock, flags);

	/* gets dropped in rt_mutex_adjust_prio_chain()! */
	get_task_struct(task);

	rt_mutex_adjust_prio_chain(task, RT_MUTEX_MIN_CHAINWALK, NULL,
				   next_lock, NULL, task);
}

/*
 * Performs the wakeup of the top-waiter and re-enables preemption.
 */
void __sched rt_mutex_postunlock(struct rt_wake_q_head *wqh)
{
	rt_mutex_wake_up_q(wqh);
}

#ifdef CONFIG_DEBUG_RT_MUTEXES
void rt_mutex_debug_task_free(struct task_struct *task)
{
	DEBUG_LOCKS_WARN_ON(!RB_EMPTY_ROOT(&task->pi_waiters.rb_root));
	DEBUG_LOCKS_WARN_ON(task->pi_blocked_on);
}
#endif

#ifdef CONFIG_PREEMPT_RT
/* Mutexes */
void __mutex_rt_init(struct mutex *mutex, const char *name,
		     struct lock_class_key *key)
{
	debug_check_no_locks_freed((void *)mutex, sizeof(*mutex));
	lockdep_init_map_wait(&mutex->dep_map, name, key, 0, LD_WAIT_SLEEP);
}
EXPORT_SYMBOL(__mutex_rt_init);

static __always_inline int __mutex_lock_common(struct mutex *lock,
					       unsigned int state,
					       unsigned int subclass,
					       struct lockdep_map *nest_lock,
					       unsigned long ip)
{
	int ret;

	might_sleep();
	mutex_acquire_nest(&lock->dep_map, subclass, 0, nest_lock, ip);
	ret = __rt_mutex_lock(&lock->rtmutex, state);
	if (ret)
		mutex_release(&lock->dep_map, ip);
	else
		lock_acquired(&lock->dep_map, ip);
	return ret;
}

#ifdef CONFIG_DEBUG_LOCK_ALLOC
void __sched mutex_lock_nested(struct mutex *lock, unsigned int subclass)
{
	__mutex_lock_common(lock, TASK_UNINTERRUPTIBLE, subclass, NULL, _RET_IP_);
}
EXPORT_SYMBOL_GPL(mutex_lock_nested);

void __sched _mutex_lock_nest_lock(struct mutex *lock,
				   struct lockdep_map *nest_lock)
{
	__mutex_lock_common(lock, TASK_UNINTERRUPTIBLE, 0, nest_lock, _RET_IP_);
}
EXPORT_SYMBOL_GPL(_mutex_lock_nest_lock);

int __sched mutex_lock_interruptible_nested(struct mutex *lock,
					    unsigned int subclass)
{
	return __mutex_lock_common(lock, TASK_INTERRUPTIBLE, subclass, NULL, _RET_IP_);
}
EXPORT_SYMBOL_GPL(mutex_lock_interruptible_nested);

int __sched mutex_lock_killable_nested(struct mutex *lock,
					    unsigned int subclass)
{
	return __mutex_lock_common(lock, TASK_KILLABLE, subclass, NULL, _RET_IP_);
}
EXPORT_SYMBOL_GPL(mutex_lock_killable_nested);

void __sched mutex_lock_io_nested(struct mutex *lock, unsigned int subclass)
{
	int token;

	might_sleep();

	token = io_schedule_prepare();
	__mutex_lock_common(lock, TASK_UNINTERRUPTIBLE, subclass, NULL, _RET_IP_);
	io_schedule_finish(token);
}
EXPORT_SYMBOL_GPL(mutex_lock_io_nested);

#else /* CONFIG_DEBUG_LOCK_ALLOC */

void __sched mutex_lock(struct mutex *lock)
{
	__mutex_lock_common(lock, TASK_UNINTERRUPTIBLE, 0, NULL, _RET_IP_);
}
EXPORT_SYMBOL(mutex_lock);

int __sched mutex_lock_interruptible(struct mutex *lock)
{
	return __mutex_lock_common(lock, TASK_INTERRUPTIBLE, 0, NULL, _RET_IP_);
}
EXPORT_SYMBOL(mutex_lock_interruptible);

int __sched mutex_lock_killable(struct mutex *lock)
{
	return __mutex_lock_common(lock, TASK_KILLABLE, 0, NULL, _RET_IP_);
}
EXPORT_SYMBOL(mutex_lock_killable);

void __sched mutex_lock_io(struct mutex *lock)
{
	int token = io_schedule_prepare();

	__mutex_lock_common(lock, TASK_UNINTERRUPTIBLE, 0, NULL, _RET_IP_);
	io_schedule_finish(token);
}
EXPORT_SYMBOL(mutex_lock_io);
#endif /* !CONFIG_DEBUG_LOCK_ALLOC */

int __sched mutex_trylock(struct mutex *lock)
{
	int ret;

	if (IS_ENABLED(CONFIG_DEBUG_RT_MUTEXES) && WARN_ON_ONCE(!in_task()))
		return 0;

	ret = __rt_mutex_trylock(&lock->rtmutex);
	if (ret)
		mutex_acquire(&lock->dep_map, 0, 1, _RET_IP_);

	return ret;
}
EXPORT_SYMBOL(mutex_trylock);

void __sched mutex_unlock(struct mutex *lock)
{
	mutex_release(&lock->dep_map, _RET_IP_);
	__rt_mutex_unlock(&lock->rtmutex);
}
EXPORT_SYMBOL(mutex_unlock);

#endif /* CONFIG_PREEMPT_RT */
