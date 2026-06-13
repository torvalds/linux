// SPDX-License-Identifier: GPL-2.0

#include <linux/srcu.h>

__rust_helper int rust_helper_init_srcu_struct_with_key(struct srcu_struct *ssp,
							const char *name,
							struct lock_class_key *key)
{
	return __init_srcu_struct(ssp, name, key);
}

__rust_helper bool rust_helper_srcu_readers_active(struct srcu_struct *ssp)
{
	return srcu_readers_active(ssp);
}

__rust_helper int rust_helper_srcu_read_lock(struct srcu_struct *ssp)
{
	return srcu_read_lock(ssp);
}

__rust_helper void rust_helper_srcu_read_unlock(struct srcu_struct *ssp, int idx)
{
	srcu_read_unlock(ssp, idx);
}

__rust_helper void rust_helper_srcu_barrier(struct srcu_struct *ssp)
{
	srcu_barrier(ssp);
}

__rust_helper void rust_helper_synchronize_srcu_expedited(struct srcu_struct *ssp)
{
	synchronize_srcu_expedited(ssp);
}
