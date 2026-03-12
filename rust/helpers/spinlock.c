// SPDX-License-Identifier: GPL-2.0

#include <linux/spinlock.h>

__rust_helper void rust_helper___spin_lock_init(spinlock_t *lock,
						const char *name,
						struct lock_class_key *key)
{
#ifdef CONFIG_DEBUG_SPINLOCK
# if defined(CONFIG_PREEMPT_RT)
	__spin_lock_init(lock, name, key, false);
# else /*!CONFIG_PREEMPT_RT */
	__raw_spin_lock_init(spinlock_check(lock), name, key, LD_WAIT_CONFIG);
# endif /* CONFIG_PREEMPT_RT */
#else /* !CONFIG_DEBUG_SPINLOCK */
	spin_lock_init(lock);
#endif /* CONFIG_DEBUG_SPINLOCK */
}

__rust_helper void rust_helper_spin_lock(spinlock_t *lock)
{
	spin_lock(lock);
}

__rust_helper void rust_helper_spin_unlock(spinlock_t *lock)
{
	spin_unlock(lock);
}

__rust_helper int rust_helper_spin_trylock(spinlock_t *lock)
{
	return spin_trylock(lock);
}

__rust_helper void rust_helper_spin_assert_is_held(spinlock_t *lock)
{
	lockdep_assert_held(lock);
}
