/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
\*****************************************************************************/

#ifndef _SPL_RWLOCK_H
#define _SPL_RWLOCK_H

#include <sys/types.h>
#include <linux/rwsem.h>
#include <linux/rwsem_compat.h>

typedef enum {
	RW_DRIVER	= 2,
	RW_DEFAULT	= 4,
	RW_NOLOCKDEP	= 5
} krw_type_t;

typedef enum {
	RW_NONE		= 0,
	RW_WRITER	= 1,
	RW_READER	= 2
} krw_t;

/*
 * If CONFIG_RWSEM_SPIN_ON_OWNER is defined, rw_semaphore will have an owner
 * field, so we don't need our own.
 */
typedef struct {
	struct rw_semaphore rw_rwlock;
#ifndef CONFIG_RWSEM_SPIN_ON_OWNER
	kthread_t *rw_owner;
#endif
#ifdef CONFIG_LOCKDEP
	krw_type_t	rw_type;
#endif /* CONFIG_LOCKDEP */
} krwlock_t;

#define SEM(rwp)	(&(rwp)->rw_rwlock)

static inline void
spl_rw_set_owner(krwlock_t *rwp)
{
/*
 * If CONFIG_RWSEM_SPIN_ON_OWNER is defined, down_write, up_write,
 * downgrade_write and __init_rwsem will set/clear owner for us.
 */
#ifndef CONFIG_RWSEM_SPIN_ON_OWNER
	rwp->rw_owner = current;
#endif
}

static inline void
spl_rw_clear_owner(krwlock_t *rwp)
{
#ifndef CONFIG_RWSEM_SPIN_ON_OWNER
	rwp->rw_owner = NULL;
#endif
}

static inline kthread_t *
rw_owner(krwlock_t *rwp)
{
#ifdef CONFIG_RWSEM_SPIN_ON_OWNER
	return SEM(rwp)->owner;
#else
	return rwp->rw_owner;
#endif
}

#ifdef CONFIG_LOCKDEP
static inline void
spl_rw_set_type(krwlock_t *rwp, krw_type_t type)
{
	rwp->rw_type = type;
}
static inline void
spl_rw_lockdep_off_maybe(krwlock_t *rwp)		\
{							\
	if (rwp && rwp->rw_type == RW_NOLOCKDEP)	\
		lockdep_off();				\
}
static inline void
spl_rw_lockdep_on_maybe(krwlock_t *rwp)			\
{							\
	if (rwp && rwp->rw_type == RW_NOLOCKDEP)	\
		lockdep_on();				\
}
#else  /* CONFIG_LOCKDEP */
#define spl_rw_set_type(rwp, type)
#define spl_rw_lockdep_off_maybe(rwp)
#define spl_rw_lockdep_on_maybe(rwp)
#endif /* CONFIG_LOCKDEP */

static inline int
RW_READ_HELD(krwlock_t *rwp)
{
	/*
	 * Linux 4.8 will set owner to 1 when read held instead of leave it
	 * NULL. So we check whether owner <= 1.
	 */
	return (spl_rwsem_is_locked(SEM(rwp)) &&
	    (unsigned long)rw_owner(rwp) <= 1);
}

static inline int
RW_WRITE_HELD(krwlock_t *rwp)
{
	return (rw_owner(rwp) == current);
}

static inline int
RW_LOCK_HELD(krwlock_t *rwp)
{
	return spl_rwsem_is_locked(SEM(rwp));
}

/*
 * The following functions must be a #define and not static inline.
 * This ensures that the native linux semaphore functions (down/up)
 * will be correctly located in the users code which is important
 * for the built in kernel lock analysis tools
 */
#define rw_init(rwp, name, type, arg)					\
({									\
	static struct lock_class_key __key;				\
	ASSERT(type == RW_DEFAULT || type == RW_NOLOCKDEP);		\
									\
	__init_rwsem(SEM(rwp), #rwp, &__key);				\
	spl_rw_clear_owner(rwp);					\
	spl_rw_set_type(rwp, type);					\
})

#define rw_destroy(rwp)							\
({									\
	VERIFY(!RW_LOCK_HELD(rwp));					\
})

#define rw_tryenter(rwp, rw)						\
({									\
	int _rc_ = 0;							\
									\
	spl_rw_lockdep_off_maybe(rwp);					\
	switch (rw) {							\
	case RW_READER:							\
		_rc_ = down_read_trylock(SEM(rwp));			\
		break;							\
	case RW_WRITER:							\
		if ((_rc_ = down_write_trylock(SEM(rwp))))		\
			spl_rw_set_owner(rwp);				\
		break;							\
	default:							\
		VERIFY(0);						\
	}								\
	spl_rw_lockdep_on_maybe(rwp);					\
	_rc_;								\
})

#define rw_enter(rwp, rw)						\
({									\
	spl_rw_lockdep_off_maybe(rwp);					\
	switch (rw) {							\
	case RW_READER:							\
		down_read(SEM(rwp));					\
		break;							\
	case RW_WRITER:							\
		down_write(SEM(rwp));					\
		spl_rw_set_owner(rwp);					\
		break;							\
	default:							\
		VERIFY(0);						\
	}								\
	spl_rw_lockdep_on_maybe(rwp);					\
})

#define rw_exit(rwp)							\
({									\
	spl_rw_lockdep_off_maybe(rwp);					\
	if (RW_WRITE_HELD(rwp)) {					\
		spl_rw_clear_owner(rwp);				\
		up_write(SEM(rwp));					\
	} else {							\
		ASSERT(RW_READ_HELD(rwp));				\
		up_read(SEM(rwp));					\
	}								\
	spl_rw_lockdep_on_maybe(rwp);					\
})

#define rw_downgrade(rwp)						\
({									\
	spl_rw_lockdep_off_maybe(rwp);					\
	spl_rw_clear_owner(rwp);					\
	downgrade_write(SEM(rwp));					\
	spl_rw_lockdep_on_maybe(rwp);					\
})

#define rw_tryupgrade(rwp)						\
({									\
	int _rc_ = 0;							\
									\
	if (RW_WRITE_HELD(rwp)) {					\
		_rc_ = 1;						\
	} else {							\
		spl_rw_lockdep_off_maybe(rwp);				\
		if ((_rc_ = rwsem_tryupgrade(SEM(rwp))))		\
			spl_rw_set_owner(rwp);				\
		spl_rw_lockdep_on_maybe(rwp);				\
	}								\
	_rc_;								\
})

int spl_rw_init(void);
void spl_rw_fini(void);

#endif /* _SPL_RWLOCK_H */
