/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
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
#ifndef __XFS_SUPPORT_MRLOCK_H__
#define __XFS_SUPPORT_MRLOCK_H__

#include <linux/rwsem.h>

enum { MR_NONE, MR_ACCESS, MR_UPDATE };

typedef struct {
	struct rw_semaphore	mr_lock;
	int			mr_writer;
} mrlock_t;

#define mrinit(mrp, name)	\
	( (mrp)->mr_writer = 0, init_rwsem(&(mrp)->mr_lock) )
#define mrlock_init(mrp, t,n,s)	mrinit(mrp, n)
#define mrfree(mrp)		do { } while (0)
#define mraccess(mrp)		mraccessf(mrp, 0)
#define mrupdate(mrp)		mrupdatef(mrp, 0)

static inline void mraccessf(mrlock_t *mrp, int flags)
{
	down_read(&mrp->mr_lock);
}

static inline void mrupdatef(mrlock_t *mrp, int flags)
{
	down_write(&mrp->mr_lock);
	mrp->mr_writer = 1;
}

static inline int mrtryaccess(mrlock_t *mrp)
{
	return down_read_trylock(&mrp->mr_lock);
}

static inline int mrtryupdate(mrlock_t *mrp)
{
	if (!down_write_trylock(&mrp->mr_lock))
		return 0;
	mrp->mr_writer = 1;
	return 1;
}

static inline void mrunlock(mrlock_t *mrp)
{
	if (mrp->mr_writer) {
		mrp->mr_writer = 0;
		up_write(&mrp->mr_lock);
	} else {
		up_read(&mrp->mr_lock);
	}
}

static inline void mrdemote(mrlock_t *mrp)
{
	mrp->mr_writer = 0;
	downgrade_write(&mrp->mr_lock);
}

#ifdef DEBUG
/*
 * Debug-only routine, without some platform-specific asm code, we can
 * now only answer requests regarding whether we hold the lock for write
 * (reader state is outside our visibility, we only track writer state).
 * Note: means !ismrlocked would give false positivies, so don't do that.
 */
static inline int ismrlocked(mrlock_t *mrp, int type)
{
	if (mrp && type == MR_UPDATE)
		return mrp->mr_writer;
	return 1;
}
#endif

#endif /* __XFS_SUPPORT_MRLOCK_H__ */
