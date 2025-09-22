/*
 * Copyright (c) 2015 Michael Neumann <mneumann@ntecs.de>
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
 */

#ifndef _LINUX_WW_MUTEX_H_
#define _LINUX_WW_MUTEX_H_

/*
 * A basic, unoptimized implementation of wound/wait mutexes for DragonFly
 * modelled after the Linux API [1].
 *
 * [1]: http://lxr.free-electrons.com/source/include/linux/ww_mutex.h
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/param.h>
#include <sys/mutex.h>
#include <machine/intr.h>
#include <linux/mutex.h>

struct ww_class {
	volatile u_long			stamp;
	const char			*name;
};

struct ww_acquire_ctx {
	u_long				stamp;
	struct ww_class			*ww_class;
};

struct ww_mutex {
	struct mutex			base;
	volatile int			acquired;
	struct ww_acquire_ctx		*ctx;
	volatile struct proc		*owner;
};

#define DEFINE_WW_CLASS(classname)	\
	struct ww_class classname = {	\
		.stamp = 0,		\
		.name = #classname	\
	}

#define DEFINE_WD_CLASS(classname)	\
	struct ww_class classname = {	\
		.stamp = 0,		\
		.name = #classname	\
	}

static inline void
ww_acquire_init(struct ww_acquire_ctx *ctx, struct ww_class *ww_class) {
	ctx->stamp = __sync_fetch_and_add(&ww_class->stamp, 1);
	ctx->ww_class = ww_class;
}

static inline void
ww_acquire_done(__unused struct ww_acquire_ctx *ctx) {
}

static inline void
ww_acquire_fini(__unused struct ww_acquire_ctx *ctx) {
}

static inline void
ww_mutex_init(struct ww_mutex *lock, struct ww_class *ww_class) {
	mtx_init(&lock->base, IPL_NONE);
	lock->acquired = 0;
	lock->ctx = NULL;
	lock->owner = NULL;
}

static inline bool
ww_mutex_is_locked(struct ww_mutex *lock) {
	bool res = false;
	mtx_enter(&lock->base);
	if (lock->acquired > 0) res = true;
	mtx_leave(&lock->base);
	return res;
}

/*
 * Return 1 if lock could be acquired, else 0 (contended).
 */
static inline int
ww_mutex_trylock(struct ww_mutex *lock, struct ww_acquire_ctx *ctx) {
	int res = 0;

	mtx_enter(&lock->base);
	/*
	 * In case no one holds the ww_mutex yet, we acquire it.
	 */
	if (lock->acquired == 0) {
		KASSERT(lock->ctx == NULL);
		lock->acquired = 1;
		lock->owner = curproc;
		res = 1;
	}
	mtx_leave(&lock->base);
	return res;
}

/*
 * When `slow` is `true`, it will always block if the ww_mutex is contended.
 * It is assumed that the called will not hold any (ww_mutex) resources when
 * calling the slow path as this could lead to deadlocks.
 *
 * When `intr` is `true`, the ssleep will be interruptible.
 */
static inline int
__ww_mutex_lock(struct ww_mutex *lock, struct ww_acquire_ctx *ctx, bool slow, bool intr) {
	int err;

	mtx_enter(&lock->base);
	for (;;) {
		/*
		 * In case no one holds the ww_mutex yet, we acquire it.
		 */
		if (lock->acquired == 0) {
			KASSERT(lock->ctx == NULL);
			lock->acquired = 1;
			lock->ctx = ctx;
			lock->owner = curproc;
			err = 0;
			break;
		}
		/*
		 * In case we already hold the return -EALREADY.
		 */
		else if (lock->owner == curproc) {
			err = -EALREADY;
			break;
		}
		/*
		 * This is the contention case where the ww_mutex is
		 * already held by another context.
		 */
		else {
			/*
			 * Three cases:
			 *
			 * - We are in the slow-path (first lock to obtain).
                         *
			 * - No context was specified. We assume a single
			 *   resource, so there is no danger of a deadlock.
                         *
			 * - An `older` process (`ctx`) tries to acquire a
			 *   lock already held by a `younger` process.
                         *   We put the `older` process to sleep until
                         *   the `younger` process gives up all it's
                         *   resources.
			 */
			if (slow || ctx == NULL ||
			    (lock->ctx && ctx->stamp < lock->ctx->stamp)) {
				KASSERT(!cold);
				int s = msleep_nsec(lock, &lock->base,
				    intr ? PCATCH : 0,
				    ctx ? ctx->ww_class->name : "ww_mutex_lock",
				    INFSLP);
				if (intr && (s == EINTR || s == ERESTART)) {
					// XXX: Should we handle ERESTART?
					err = -EINTR;
					break;
				}
			}
			/*
			 * If a `younger` process tries to acquire a lock
			 * already held by an `older` process, we `wound` it,
			 * i.e. we return -EDEADLK because there is a potential
			 * risk for a deadlock. The `younger` process then
			 * should give up all it's resources and try again to
			 * acquire the lock in question, this time in a
			 * blocking manner.
			 */
			else {
				err = -EDEADLK;
				break;
			}
		}

	} /* for */
	mtx_leave(&lock->base);
	return err;
}

static inline int
ww_mutex_lock(struct ww_mutex *lock, struct ww_acquire_ctx *ctx) {
	return __ww_mutex_lock(lock, ctx, false, false);
}
	
static inline void
ww_mutex_lock_slow(struct ww_mutex *lock, struct ww_acquire_ctx *ctx) {
	(void)__ww_mutex_lock(lock, ctx, true, false);
}

static inline int
ww_mutex_lock_interruptible(struct ww_mutex *lock, struct ww_acquire_ctx *ctx) {
	return __ww_mutex_lock(lock, ctx, false, true);
}
	
static inline int __must_check
ww_mutex_lock_slow_interruptible(struct ww_mutex *lock, struct ww_acquire_ctx *ctx) {
	return __ww_mutex_lock(lock, ctx, true, true);
}

static inline void
ww_mutex_unlock(struct ww_mutex *lock) {
	mtx_enter(&lock->base);
	KASSERT(lock->owner == curproc);
	KASSERT(lock->acquired == 1);

	lock->acquired = 0;
	lock->ctx = NULL;
	lock->owner = NULL;
	mtx_leave(&lock->base);
	wakeup(lock);
}

static inline void
ww_mutex_destroy(struct ww_mutex *lock) {
	KASSERT(lock->acquired == 0);
	KASSERT(lock->ctx == NULL);
	KASSERT(lock->owner == NULL);
}

#endif	/* _LINUX_WW_MUTEX_H_ */
