// SPDX-License-Identifier: GPL-2.0

#include <linux/mutex.h>

void rust_helper_mutex_lock(struct mutex *lock)
{
	mutex_lock(lock);
}

void rust_helper___mutex_init(struct mutex *mutex, const char *name,
			      struct lock_class_key *key)
{
	__mutex_init(mutex, name, key);
}

void rust_helper_mutex_assert_is_held(struct mutex *mutex)
{
	lockdep_assert_held(mutex);
}
