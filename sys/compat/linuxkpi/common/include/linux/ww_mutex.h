/*-
 * Copyright (c) 2017 Mellanox Technologies, Ltd.
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
#ifndef	_LINUX_WW_MUTEX_H_
#define	_LINUX_WW_MUTEX_H_

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/condvar.h>
#include <sys/kernel.h>

#include <linux/mutex.h>

struct ww_class {
	const char *mutex_name;
};

struct ww_acquire_ctx {
};

struct ww_mutex {
	struct mutex base;
	struct cv condvar;
};

#define	DEFINE_WW_CLASS(name)					\
	struct ww_class name = {				\
		.mutex_name = mutex_name(#name "_mutex")	\
	}

#define	DEFINE_WW_MUTEX(name, ww_class)					\
	struct ww_mutex name;						\
	static void name##_init(void *arg)				\
	{								\
		ww_mutex_init(&name, &ww_class);			\
	}								\
	SYSINIT(name, SI_SUB_LOCK, SI_ORDER_SECOND, name##_init, NULL)

#define	ww_mutex_is_locked(_m) \
	sx_xlocked(&(_m)->base.sx)

#define	ww_mutex_lock_slow(_m, _x) \
	ww_mutex_lock(_m, _x)

#define	ww_mutex_lock_slow_interruptible(_m, _x) \
	ww_mutex_lock_interruptible(_m, _x)

static inline int __must_check
ww_mutex_trylock(struct ww_mutex *lock)
{
	return (mutex_trylock(&lock->base));
}

extern int linux_ww_mutex_lock_sub(struct ww_mutex *, int catch_signal);

static inline int
ww_mutex_lock(struct ww_mutex *lock, struct ww_acquire_ctx *ctx)
{
	if (MUTEX_SKIP())
		return (0);
	else if ((struct thread *)SX_OWNER(lock->base.sx.sx_lock) == curthread)
		return (-EALREADY);
	else
		return (linux_ww_mutex_lock_sub(lock, 0));
}

static inline int
ww_mutex_lock_interruptible(struct ww_mutex *lock, struct ww_acquire_ctx *ctx)
{
	if (MUTEX_SKIP())
		return (0);
	else if ((struct thread *)SX_OWNER(lock->base.sx.sx_lock) == curthread)
		return (-EALREADY);
	else
		return (linux_ww_mutex_lock_sub(lock, 1));
}

extern void linux_ww_mutex_unlock_sub(struct ww_mutex *);

static inline void
ww_mutex_unlock(struct ww_mutex *lock)
{
	if (MUTEX_SKIP())
		return;
	else
		linux_ww_mutex_unlock_sub(lock);
}

static inline void
ww_mutex_destroy(struct ww_mutex *lock)
{
	cv_destroy(&lock->condvar);
	mutex_destroy(&lock->base);
}

static inline void
ww_acquire_init(struct ww_acquire_ctx *ctx, struct ww_class *ww_class)
{
}

static inline void
ww_mutex_init(struct ww_mutex *lock, struct ww_class *ww_class)
{
	linux_mutex_init(&lock->base, ww_class->mutex_name, SX_NOWITNESS);
	cv_init(&lock->condvar, "lkpi-ww");
}

static inline void
ww_acquire_fini(struct ww_acquire_ctx *ctx)
{
}

static inline void
ww_acquire_done(struct ww_acquire_ctx *ctx)
{
}

#endif					/* _LINUX_WW_MUTEX_H_ */
