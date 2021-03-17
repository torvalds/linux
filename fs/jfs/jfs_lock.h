/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) International Business Machines Corp., 2000-2001
 *   Portions Copyright (C) Christoph Hellwig, 2001-2002
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
