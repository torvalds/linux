// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_SUPPORT_MRLOCK_H__
#define __XFS_SUPPORT_MRLOCK_H__

#include <linux/rwsem.h>

typedef struct {
	struct rw_semaphore	mr_lock;
#if defined(DEBUG) || defined(XFS_WARN)
	int			mr_writer;
#endif
} mrlock_t;

#if defined(DEBUG) || defined(XFS_WARN)
#define mrinit(mrp, name)	\
	do { (mrp)->mr_writer = 0; init_rwsem(&(mrp)->mr_lock); } while (0)
#else
#define mrinit(mrp, name)	\
	do { init_rwsem(&(mrp)->mr_lock); } while (0)
#endif

#define mrlock_init(mrp, t,n,s)	mrinit(mrp, n)
#define mrfree(mrp)		do { } while (0)

static inline void mraccess_nested(mrlock_t *mrp, int subclass)
{
	down_read_nested(&mrp->mr_lock, subclass);
}

static inline void mrupdate_nested(mrlock_t *mrp, int subclass)
{
	down_write_nested(&mrp->mr_lock, subclass);
#if defined(DEBUG) || defined(XFS_WARN)
	mrp->mr_writer = 1;
#endif
}

static inline int mrtryaccess(mrlock_t *mrp)
{
	return down_read_trylock(&mrp->mr_lock);
}

static inline int mrtryupdate(mrlock_t *mrp)
{
	if (!down_write_trylock(&mrp->mr_lock))
		return 0;
#if defined(DEBUG) || defined(XFS_WARN)
	mrp->mr_writer = 1;
#endif
	return 1;
}

static inline void mrunlock_excl(mrlock_t *mrp)
{
#if defined(DEBUG) || defined(XFS_WARN)
	mrp->mr_writer = 0;
#endif
	up_write(&mrp->mr_lock);
}

static inline void mrunlock_shared(mrlock_t *mrp)
{
	up_read(&mrp->mr_lock);
}

static inline void mrdemote(mrlock_t *mrp)
{
#if defined(DEBUG) || defined(XFS_WARN)
	mrp->mr_writer = 0;
#endif
	downgrade_write(&mrp->mr_lock);
}

#endif /* __XFS_SUPPORT_MRLOCK_H__ */
