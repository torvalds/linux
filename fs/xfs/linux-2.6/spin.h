/*
 * Copyright (c) 2000-2002,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifndef __XFS_SUPPORT_SPIN_H__
#define __XFS_SUPPORT_SPIN_H__

#include <linux/sched.h>	/* preempt needs this */
#include <linux/spinlock.h>

/*
 * Map lock_t from IRIX to Linux spinlocks.
 *
 * We do not make use of lock_t from interrupt context, so we do not
 * have to worry about disabling interrupts at all (unlike IRIX).
 */

typedef spinlock_t lock_t;

#define SPLDECL(s)			unsigned long s
#ifndef DEFINE_SPINLOCK
#define DEFINE_SPINLOCK(s)		spinlock_t s = SPIN_LOCK_UNLOCKED
#endif

#define spinlock_init(lock, name)	spin_lock_init(lock)
#define	spinlock_destroy(lock)
#define mutex_spinlock(lock)		({ spin_lock(lock); 0; })
#define mutex_spinunlock(lock, s)	do { spin_unlock(lock); (void)s; } while (0)
#define nested_spinlock(lock)		spin_lock(lock)
#define nested_spinunlock(lock)		spin_unlock(lock)

#endif /* __XFS_SUPPORT_SPIN_H__ */
