/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_GENERIC_SPINLOCK_TYPES_H
#define __ASM_GENERIC_SPINLOCK_TYPES_H

#include <linux/types.h>
typedef atomic_t arch_spinlock_t;

/*
 * qrwlock_types depends on arch_spinlock_t, so we must typedef that before the
 * include.
 */
#include <asm/qrwlock_types.h>

#define __ARCH_SPIN_LOCK_UNLOCKED	ATOMIC_INIT(0)

#endif /* __ASM_GENERIC_SPINLOCK_TYPES_H */
