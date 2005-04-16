/*
 * Copyright (c) 2000-2002 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
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

#define spinlock_init(lock, name)	spin_lock_init(lock)
#define	spinlock_destroy(lock)
#define mutex_spinlock(lock)		({ spin_lock(lock); 0; })
#define mutex_spinunlock(lock, s)	do { spin_unlock(lock); (void)s; } while (0)
#define nested_spinlock(lock)		spin_lock(lock)
#define nested_spinunlock(lock)		spin_unlock(lock)

#endif /* __XFS_SUPPORT_SPIN_H__ */
