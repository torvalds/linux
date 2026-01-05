// SPDX-License-Identifier: GPL-2.0

#include <linux/mutex.h>

__rust_helper void rust_helper_mutex_lock(struct mutex *lock)
{
	mutex_lock(lock);
}

__rust_helper int rust_helper_mutex_trylock(struct mutex *lock)
{
	return mutex_trylock(lock);
}

__rust_helper void rust_helper___mutex_init(struct mutex *mutex,
					    const char *name,
					    struct lock_class_key *key)
{
	__mutex_init(mutex, name, key);
}

__rust_helper void rust_helper_mutex_assert_is_held(struct mutex *mutex)
{
	lockdep_assert_held(mutex);
}

__rust_helper void rust_helper_mutex_destroy(struct mutex *lock)
{
	mutex_destroy(lock);
}
