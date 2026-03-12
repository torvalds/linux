// SPDX-License-Identifier: GPL-2.0

#include <linux/rcupdate.h>

__rust_helper void rust_helper_rcu_read_lock(void)
{
	rcu_read_lock();
}

__rust_helper void rust_helper_rcu_read_unlock(void)
{
	rcu_read_unlock();
}
