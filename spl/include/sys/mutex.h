/*
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
 */

#ifndef _SPL_MUTEX_H
#define	_SPL_MUTEX_H

#include <sys/types.h>
#include <linux/mutex.h>
#include <linux/compiler_compat.h>
#include <linux/lockdep.h>

typedef enum {
	MUTEX_DEFAULT	= 0,
	MUTEX_SPIN	= 1,
	MUTEX_ADAPTIVE	= 2,
	MUTEX_NOLOCKDEP	= 3
} kmutex_type_t;

typedef struct {
	struct mutex		m_mutex;
	spinlock_t		m_lock;	/* used for serializing mutex_exit */
	kthread_t		*m_owner;
#ifdef CONFIG_LOCKDEP
	kmutex_type_t		m_type;
#endif /* CONFIG_LOCKDEP */
} kmutex_t;

#define	MUTEX(mp)		(&((mp)->m_mutex))

static inline void
spl_mutex_set_owner(kmutex_t *mp)
{
	mp->m_owner = current;
}

static inline void
spl_mutex_clear_owner(kmutex_t *mp)
{
	mp->m_owner = NULL;
}

#define	mutex_owner(mp)		(ACCESS_ONCE((mp)->m_owner))
#define	mutex_owned(mp)		(mutex_owner(mp) == current)
#define	MUTEX_HELD(mp)		mutex_owned(mp)
#define	MUTEX_NOT_HELD(mp)	(!MUTEX_HELD(mp))

#ifdef CONFIG_LOCKDEP
static inline void
spl_mutex_set_type(kmutex_t *mp, kmutex_type_t type)
{
	mp->m_type = type;
}
static inline void
spl_mutex_lockdep_off_maybe(kmutex_t *mp)			\
{								\
	if (mp && mp->m_type == MUTEX_NOLOCKDEP)		\
		lockdep_off();					\
}
static inline void
spl_mutex_lockdep_on_maybe(kmutex_t *mp)			\
{								\
	if (mp && mp->m_type == MUTEX_NOLOCKDEP)		\
		lockdep_on();					\
}
#else  /* CONFIG_LOCKDEP */
#define spl_mutex_set_type(mp, type)
#define spl_mutex_lockdep_off_maybe(mp)
#define spl_mutex_lockdep_on_maybe(mp)
#endif /* CONFIG_LOCKDEP */

/*
 * The following functions must be a #define and not static inline.
 * This ensures that the native linux mutex functions (lock/unlock)
 * will be correctly located in the users code which is important
 * for the built in kernel lock analysis tools
 */
#undef mutex_init
#define	mutex_init(mp, name, type, ibc)				\
{								\
	static struct lock_class_key __key;			\
	ASSERT(type == MUTEX_DEFAULT || type == MUTEX_NOLOCKDEP); \
								\
	__mutex_init(MUTEX(mp), (name) ? (#name) : (#mp), &__key); \
	spin_lock_init(&(mp)->m_lock);				\
	spl_mutex_clear_owner(mp);				\
	spl_mutex_set_type(mp, type);				\
}

#undef mutex_destroy
#define	mutex_destroy(mp)					\
{								\
	VERIFY3P(mutex_owner(mp), ==, NULL);			\
}

#define	mutex_tryenter(mp)					\
({								\
	int _rc_;						\
								\
	spl_mutex_lockdep_off_maybe(mp);			\
	if ((_rc_ = mutex_trylock(MUTEX(mp))) == 1)		\
		spl_mutex_set_owner(mp);			\
	spl_mutex_lockdep_on_maybe(mp);				\
								\
	_rc_;							\
})

#ifdef CONFIG_DEBUG_LOCK_ALLOC
#define	mutex_enter_nested(mp, subclass)			\
{								\
	ASSERT3P(mutex_owner(mp), !=, current);			\
	spl_mutex_lockdep_off_maybe(mp);			\
	mutex_lock_nested(MUTEX(mp), (subclass));		\
	spl_mutex_lockdep_on_maybe(mp);				\
	spl_mutex_set_owner(mp);				\
}
#else /* CONFIG_DEBUG_LOCK_ALLOC */
#define	mutex_enter_nested(mp, subclass)			\
{								\
	ASSERT3P(mutex_owner(mp), !=, current);			\
	spl_mutex_lockdep_off_maybe(mp);			\
	mutex_lock(MUTEX(mp));					\
	spl_mutex_lockdep_on_maybe(mp);				\
	spl_mutex_set_owner(mp);				\
}
#endif /*  CONFIG_DEBUG_LOCK_ALLOC */

#define	mutex_enter(mp) mutex_enter_nested((mp), 0)

/*
 * The reason for the spinlock:
 *
 * The Linux mutex is designed with a fast-path/slow-path design such that it
 * does not guarantee serialization upon itself, allowing a race where latter
 * acquirers finish mutex_unlock before former ones.
 *
 * The race renders it unsafe to be used for serializing the freeing of an
 * object in which the mutex is embedded, where the latter acquirer could go
 * on to free the object while the former one is still doing mutex_unlock and
 * causing memory corruption.
 *
 * However, there are many places in ZFS where the mutex is used for
 * serializing object freeing, and the code is shared among other OSes without
 * this issue. Thus, we need the spinlock to force the serialization on
 * mutex_exit().
 *
 * See http://lwn.net/Articles/575477/ for the information about the race.
 */
#define	mutex_exit(mp)						\
{								\
	spl_mutex_clear_owner(mp);				\
	spin_lock(&(mp)->m_lock);				\
	spl_mutex_lockdep_off_maybe(mp);			\
	mutex_unlock(MUTEX(mp));				\
	spl_mutex_lockdep_on_maybe(mp);				\
	spin_unlock(&(mp)->m_lock);				\
	/* NOTE: do not dereference mp after this point */	\
}

int spl_mutex_init(void);
void spl_mutex_fini(void);

#endif /* _SPL_MUTEX_H */
