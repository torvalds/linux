/*
 *   Copyright (C) International Business Machines Corp., 2000-2001
 *   Portions Copyright (C) Christoph Hellwig, 2001-2002
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#ifndef _H_JFS_LOCK
#define _H_JFS_LOCK

#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/sched.h>

/*
 *	jfs_lock.h
 */

/*
 * Conditional sleep where condition is protected by spinlock
 *
 * lock_cmd and unlock_cmd take and release the spinlock
 */
#define __SLEEP_COND(wq, cond, lock_cmd, unlock_cmd)	\
do {							\
	DECLARE_WAITQUEUE(__wait, current);		\
							\
	add_wait_queue(&wq, &__wait);			\
	for (;;) {					\
		set_current_state(TASK_UNINTERRUPTIBLE);\
		if (cond)				\
			break;				\
		unlock_cmd;				\
		io_schedule();				\
		lock_cmd;				\
	}						\
	__set_current_state(TASK_RUNNING);			\
	remove_wait_queue(&wq, &__wait);		\
} while (0)

#endif				/* _H_JFS_LOCK */
