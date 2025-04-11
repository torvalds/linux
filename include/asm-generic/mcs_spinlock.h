#ifndef __ASM_MCS_SPINLOCK_H
#define __ASM_MCS_SPINLOCK_H

struct mcs_spinlock {
	struct mcs_spinlock *next;
	int locked; /* 1 if lock acquired */
	int count;  /* nesting count, see qspinlock.c */
};

/*
 * Architectures can define their own:
 *
 *   arch_mcs_spin_lock_contended(l)
 *   arch_mcs_spin_unlock_contended(l)
 *
 * See kernel/locking/mcs_spinlock.c.
 */

#endif /* __ASM_MCS_SPINLOCK_H */
