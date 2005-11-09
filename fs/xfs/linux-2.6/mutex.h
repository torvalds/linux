/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
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
#ifndef __XFS_SUPPORT_MUTEX_H__
#define __XFS_SUPPORT_MUTEX_H__

#include <linux/spinlock.h>
#include <asm/semaphore.h>

/*
 * Map the mutex'es from IRIX to Linux semaphores.
 *
 * Destroy just simply initializes to -99 which should block all other
 * callers.
 */
#define MUTEX_DEFAULT		0x0
typedef struct semaphore	mutex_t;

#define mutex_init(lock, type, name)		sema_init(lock, 1)
#define mutex_destroy(lock)			sema_init(lock, -99)
#define mutex_lock(lock, num)			down(lock)
#define mutex_trylock(lock)			(down_trylock(lock) ? 0 : 1)
#define mutex_unlock(lock)			up(lock)

#endif /* __XFS_SUPPORT_MUTEX_H__ */
