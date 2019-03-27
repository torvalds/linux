/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013-2017 Mellanox Technologies, Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef	_LINUX_MUTEX_H_
#define	_LINUX_MUTEX_H_

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/sx.h>

#include <linux/spinlock.h>

typedef struct mutex {
	struct sx sx;
} mutex_t;

/*
 * By defining CONFIG_NO_MUTEX_SKIP LinuxKPI mutexes and asserts will
 * not be skipped during panic().
 */
#ifdef CONFIG_NO_MUTEX_SKIP
#define	MUTEX_SKIP(void) 0
#else
#define	MUTEX_SKIP(void) unlikely(SCHEDULER_STOPPED() || kdb_active)
#endif

#define	mutex_lock(_m) do {			\
	if (MUTEX_SKIP())			\
		break;				\
	sx_xlock(&(_m)->sx);			\
} while (0)

#define	mutex_lock_nested(_m, _s)	mutex_lock(_m)
#define	mutex_lock_nest_lock(_m, _s)	mutex_lock(_m)

#define	mutex_lock_interruptible(_m) ({		\
	MUTEX_SKIP() ? 0 :			\
	linux_mutex_lock_interruptible(_m);	\
})

#define	mutex_unlock(_m) do {			\
	if (MUTEX_SKIP())			\
		break;				\
	sx_xunlock(&(_m)->sx);			\
} while (0)

#define	mutex_trylock(_m) ({			\
	MUTEX_SKIP() ? 1 :			\
	!!sx_try_xlock(&(_m)->sx);		\
})

enum mutex_trylock_recursive_enum {
	MUTEX_TRYLOCK_FAILED = 0,
	MUTEX_TRYLOCK_SUCCESS = 1,
	MUTEX_TRYLOCK_RECURSIVE = 2,
};

static inline __must_check enum mutex_trylock_recursive_enum
mutex_trylock_recursive(struct mutex *lock)
{
	if (unlikely(sx_xholder(&lock->sx) == curthread))
		return (MUTEX_TRYLOCK_RECURSIVE);

	return (mutex_trylock(lock));
}

#define	mutex_init(_m) \
	linux_mutex_init(_m, mutex_name(#_m), SX_NOWITNESS)

#define	mutex_init_witness(_m) \
	linux_mutex_init(_m, mutex_name(#_m), SX_DUPOK)

#define	mutex_destroy(_m) \
	linux_mutex_destroy(_m)

static inline bool
mutex_is_locked(mutex_t *m)
{
	return ((struct thread *)SX_OWNER(m->sx.sx_lock) != NULL);
}

static inline bool
mutex_is_owned(mutex_t *m)
{
	return (sx_xlocked(&m->sx));
}

#ifdef WITNESS_ALL
/* NOTE: the maximum WITNESS name is 64 chars */
#define	__mutex_name(name, file, line)		\
	(((const char *){file ":" #line "-" name}) +	\
	(sizeof(file) > 16 ? sizeof(file) - 16 : 0))
#else
#define	__mutex_name(name, file, line)	name
#endif
#define	_mutex_name(...)	__mutex_name(__VA_ARGS__)
#define	mutex_name(name)	_mutex_name(name, __FILE__, __LINE__)

#define	DEFINE_MUTEX(lock)						\
	mutex_t lock;							\
	SX_SYSINIT_FLAGS(lock, &(lock).sx, mutex_name(#lock), SX_DUPOK)

static inline void
linux_mutex_init(mutex_t *m, const char *name, int flags)
{
	memset(m, 0, sizeof(*m));
	sx_init_flags(&m->sx, name, flags);
}

static inline void
linux_mutex_destroy(mutex_t *m)
{
	if (mutex_is_owned(m))
		mutex_unlock(m);
	sx_destroy(&m->sx);
}

extern int linux_mutex_lock_interruptible(mutex_t *m);

#endif					/* _LINUX_MUTEX_H_ */
