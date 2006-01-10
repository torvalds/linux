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
#include <linux/mutex.h>

/*
 * Map the mutex'es from IRIX to Linux semaphores.
 *
 * Destroy just simply initializes to -99 which should block all other
 * callers.
 */
#define MUTEX_DEFAULT		0x0

typedef struct mutex		mutex_t;
//#define mutex_destroy(lock)			do{}while(0)

#endif /* __XFS_SUPPORT_MUTEX_H__ */
