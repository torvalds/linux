// SPDX-License-Identifier: GPL-2.0-only

#ifndef KVM_MMU_LOCK_H
#define KVM_MMU_LOCK_H 1

/*
 * Architectures can choose whether to use an rwlock or spinlock
 * for the mmu_lock.  These macros, for use in common code
 * only, avoids using #ifdefs in places that must deal with
 * multiple architectures.
 */

#ifdef KVM_HAVE_MMU_RWLOCK
#define KVM_MMU_LOCK_INIT(kvm) rwlock_init(&(kvm)->mmu_lock)
#define KVM_MMU_LOCK(kvm)      write_lock(&(kvm)->mmu_lock)
#define KVM_MMU_UNLOCK(kvm)    write_unlock(&(kvm)->mmu_lock)
#else
#define KVM_MMU_LOCK_INIT(kvm) spin_lock_init(&(kvm)->mmu_lock)
#define KVM_MMU_LOCK(kvm)      spin_lock(&(kvm)->mmu_lock)
#define KVM_MMU_UNLOCK(kvm)    spin_unlock(&(kvm)->mmu_lock)
#endif /* KVM_HAVE_MMU_RWLOCK */

#endif
