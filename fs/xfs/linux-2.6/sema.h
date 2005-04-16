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
#ifndef __XFS_SUPPORT_SEMA_H__
#define __XFS_SUPPORT_SEMA_H__

#include <linux/time.h>
#include <linux/wait.h>
#include <asm/atomic.h>
#include <asm/semaphore.h>

/*
 * sema_t structure just maps to struct semaphore in Linux kernel.
 */

typedef struct semaphore sema_t;

#define init_sema(sp, val, c, d)	sema_init(sp, val)
#define initsema(sp, val)		sema_init(sp, val)
#define initnsema(sp, val, name)	sema_init(sp, val)
#define psema(sp, b)			down(sp)
#define vsema(sp)			up(sp)
#define valusema(sp)			(atomic_read(&(sp)->count))
#define freesema(sema)

/*
 * Map cpsema (try to get the sema) to down_trylock. We need to switch
 * the return values since cpsema returns 1 (acquired) 0 (failed) and
 * down_trylock returns the reverse 0 (acquired) 1 (failed).
 */

#define cpsema(sp)			(down_trylock(sp) ? 0 : 1)

/*
 * Didn't do cvsema(sp). Not sure how to map this to up/down/...
 * It does a vsema if the values is < 0 other wise nothing.
 */

#endif /* __XFS_SUPPORT_SEMA_H__ */
