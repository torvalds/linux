// SPDX-License-Identifier: GPL-2.0-only
/*
 * PREEMPT_RT substitution for spin/rw_locks
 *
 * spinlocks and rwlocks on RT are based on rtmutexes, with a few twists to
 * resemble the non RT semantics:
 *
 * - Contrary to plain rtmutexes, spinlocks and rwlocks are state
 *   preserving. The task state is saved before blocking on the underlying
 *   rtmutex, and restored when the lock has been acquired. Regular wakeups
 *   during that time are redirected to the saved state so no wake up is
 *   missed.
 *
 * - Non RT spin/rwlocks disable preemption and eventually interrupts.
 *   Disabling preemption has the side effect of disabling migration and
 *   preventing RCU grace periods.
 *
 *   The RT substitutions explicitly disable migration and take
 *   rcu_read_lock() across the lock held section.
 */
#include <linux/spinlock.h>
#include <linux/export.h>

#define RT_MUTEX_BUILD_SPINLOCKS
#include "rtmutex.c"

static __always_inline void rtlock_lock(struct rt_mutex_base *rtm)
{
	if (unlikely(!rt_mutex_cmpxchg_acquire(rtm, NULL, current)))
		rtlock_slowlock(rtm);
}

static __always_inline void __rt_spin_lock(spinlock_t *lock)
{
	___might_sleep(__FILE__, __LINE__, 0);
	rtlock_lock(&lock->lock);
	rcu_read_lock();
	migrate_disable();
}

void __sched rt_spin_lock(spinlock_t *lock)
{
	spin_acquire(&lock->dep_map, 0, 0, _RET_IP_);
	__rt_spin_lock(lock);
}
EXPORT_SYMBOL(rt_spin_lock);

#ifdef CONFIG_DEBUG_LOCK_ALLOC
void __sched rt_spin_lock_nested(spinlock_t *lock, int subclass)
{
	spin_acquire(&lock->dep_map, subclass, 0, _RET_IP_);
	__rt_spin_lock(lock);
}
EXPORT_SYMBOL(rt_spin_lock_nested);

void __sched rt_spin_lock_nest_lock(spinlock_t *lock,
				    struct lockdep_map *nest_lock)
{
	spin_acquire_nest(&lock->dep_map, 0, 0, nest_lock, _RET_IP_);
	__rt_spin_lock(lock);
}
EXPORT_SYMBOL(rt_spin_lock_nest_lock);
#endif

void __sched rt_spin_unlock(spinlock_t *lock)
{
	spin_release(&lock->dep_map, _RET_IP_);
	migrate_enable();
	rcu_read_unlock();

	if (unlikely(!rt_mutex_cmpxchg_release(&lock->lock, current, NULL)))
		rt_mutex_slowunlock(&lock->lock);
}
EXPORT_SYMBOL(rt_spin_unlock);

/*
 * Wait for the lock to get unlocked: instead of polling for an unlock
 * (like raw spinlocks do), lock and unlock, to force the kernel to
 * schedule if there's contention:
 */
void __sched rt_spin_lock_unlock(spinlock_t *lock)
{
	spin_lock(lock);
	spin_unlock(lock);
}
EXPORT_SYMBOL(rt_spin_lock_unlock);

static __always_inline int __rt_spin_trylock(spinlock_t *lock)
{
	int ret = 1;

	if (unlikely(!rt_mutex_cmpxchg_acquire(&lock->lock, NULL, current)))
		ret = rt_mutex_slowtrylock(&lock->lock);

	if (ret) {
		spin_acquire(&lock->dep_map, 0, 1, _RET_IP_);
		rcu_read_lock();
		migrate_disable();
	}
	return ret;
}

int __sched rt_spin_trylock(spinlock_t *lock)
{
	return __rt_spin_trylock(lock);
}
EXPORT_SYMBOL(rt_spin_trylock);

int __sched rt_spin_trylock_bh(spinlock_t *lock)
{
	int ret;

	local_bh_disable();
	ret = __rt_spin_trylock(lock);
	if (!ret)
		local_bh_enable();
	return ret;
}
EXPORT_SYMBOL(rt_spin_trylock_bh);

#ifdef CONFIG_DEBUG_LOCK_ALLOC
void __rt_spin_lock_init(spinlock_t *lock, const char *name,
			 struct lock_class_key *key)
{
	debug_check_no_locks_freed((void *)lock, sizeof(*lock));
	lockdep_init_map_wait(&lock->dep_map, name, key, 0, LD_WAIT_CONFIG);
}
EXPORT_SYMBOL(__rt_spin_lock_init);
#endif
