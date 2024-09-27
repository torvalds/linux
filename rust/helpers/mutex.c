// SPDX-License-Identifier: GPL-2.0

#include <linux/export.h>
#include <linux/mutex.h>

void rust_helper_mutex_lock(struct mutex *lock)
{
	mutex_lock(lock);
}
