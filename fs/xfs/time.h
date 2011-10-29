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
#ifndef __XFS_SUPPORT_TIME_H__
#define __XFS_SUPPORT_TIME_H__

#include <linux/sched.h>
#include <linux/time.h>

typedef struct timespec timespec_t;

static inline void delay(long ticks)
{
	schedule_timeout_uninterruptible(ticks);
}

static inline void nanotime(struct timespec *tvp)
{
	*tvp = CURRENT_TIME;
}

#endif /* __XFS_SUPPORT_TIME_H__ */
