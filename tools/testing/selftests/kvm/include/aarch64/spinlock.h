/* SPDX-License-Identifier: GPL-2.0 */

#ifndef SELFTEST_KVM_ARM64_SPINLOCK_H
#define SELFTEST_KVM_ARM64_SPINLOCK_H

struct spinlock {
	int v;
};

extern void spin_lock(struct spinlock *lock);
extern void spin_unlock(struct spinlock *lock);

#endif /* SELFTEST_KVM_ARM64_SPINLOCK_H */
