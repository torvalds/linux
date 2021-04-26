/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Wound/Wait Mutexes: blocking mutual exclusion locks with deadlock avoidance
 *
 * Original mutex implementation started by Ingo Molnar:
 *
 *  Copyright (C) 2004, 2005, 2006 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *
 * Wait/Die implementation:
 *  Copyright (C) 2013 Canonical Ltd.
 * Choice of algorithm:
 *  Copyright (C) 2018 WMWare Inc.
 *
 * This file contains the main data structure and API definitions.
 */

#ifndef __LINUX_WW_MUTEX_H
#define __LINUX_WW_MUTEX_H

#include <linux/mutex.h>

struct ww_class {
	atomic_long_t stamp;
	struct lock_class_key acquire_key;
	struct lock_class_key mutex_key;
	const char *acquire_name;
	const char *mutex_name;
	unsigned int is_wait_die;
};

struct ww_acquire_ctx {
	struct task_struct *task;
	unsigned long stamp;
	unsigned int acquired;
	unsigned short wounded;
	unsigned short is_wait_die;
#ifdef CONFIG_DEBUG_MUTEXES
	unsigned int done_acquire;
	struct ww_class *ww_class;
	struct ww_mutex *contending_lock;
#endif
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map dep_map;
#endif
#ifdef CONFIG_DEBUG_WW_MUTEX_SLOWPATH
	unsigned int deadlock_inject_interval;
	unsigned int deadlock_inject_countdown;
#endif
};

#ifdef CONFIG_DEBUG_LOCK_ALLOC
# define __WW_CLASS_MUTEX_INITIALIZER(lockname, class) \
		, .ww_class = class
#else
# define __WW_CLASS_MUTEX_INITIALIZER(lockname, class)
#endif

#define __WW_CLASS_INITIALIZER(ww_class, _is_wait_die)	    \
		{ .stamp = ATOMIC_LONG_INIT(0) \
		, .acquire_name = #ww_class "_acquire" \
		, .mutex_name = #ww_class "_mutex" \
		, .is_wait_die = _is_wait_die }

#define __WW_MUTEX_INITIALIZER(lockname, class) \
		{ .base =  __MUTEX_INITIALIZER(lockname.base) \
		__WW_CLASS_MUTEX_INITIALIZER(lockname, class) }

#define DEFINE_WD_CLASS(classname) \
	struct ww_class classname = __WW_CLASS_INITIALIZER(classname, 1)

#define DEFINE_WW_CLASS(classname) \
	struct ww_class classname = __WW_CLASS_INITIALIZER(classname, 0)

#define DEFINE_WW_MUTEX(mutexname, ww_class) \
	struct ww_mutex mutexname = __WW_MUTEX_INITIALIZER(mutexname, ww_class)

/**
 * ww_mutex_init - initialize the w/w mutex
 * @lock: the mutex to be initialized
 * @ww_class: the w/w class the mutex should belong to
 *
 * Initialize the w/w mutex to unlocked state and associate it with the given
 * class.
 *
 * It is not allowed to initialize an already locked mutex.
 */
static inline void ww_mutex_init(struct ww_mutex *lock,
				 struct ww_class *ww_class)
{
	__mutex_init(&lock->base, ww_class->mutex_name, &ww_class->mutex_key);
	lock->ctx = NULL;
#ifdef CONFIG_DEBUG_MUTEXES
	lock->ww_class = ww_class;
#endif
}

/**
 * ww_acquire_init - initialize a w/w acquire context
 * @ctx: w/w acquire context to initialize
 * @ww_class: w/w class of the context
 *
 * Initializes an context to acquire multiple mutexes of the given w/w class.
 *
 * Context-based w/w mutex acquiring can be done in any order whatsoever within
 * a given lock class. Deadlocks will be detected and handled with the
 * wait/die logic.
 *
 * Mixing of context-based w/w mutex acquiring and single w/w mutex locking can
 * result in undetected deadlocks and is so forbidden. Mixing different contexts
 * for the same w/w class when acquiring mutexes can also result in undetected
 * deadlocks, and is hence also forbidden. Both types of abuse will be caught by
 * enabling CONFIG_PROVE_LOCKING.
 *
 * Nesting of acquire contexts for _different_ w/w classes is possible, subject
 * to the usual locking rules between different lock classes.
 *
 * An acquire context must be released with ww_acquire_fini by the same task
 * before the memory is freed. It is recommended to allocate the context itself
 * on the stack.
 */
static inline void ww_acquire_init(struct ww_acquire_ctx *ctx,
				   struct ww_class *ww_class)
{
	ctx->task = current;
	ctx->stamp = atomic_long_inc_return_relaxed(&ww_class->stamp);
	ctx->acquired = 0;
	ctx->wounded = false;
	ctx->is_wait_die = ww_class->is_wait_die;
#ifdef CONFIG_DEBUG_MUTEXES
	ctx->ww_class = ww_class;
	ctx->done_acquire = 0;
	ctx->contending_lock = NULL;
#endif
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	debug_check_no_locks_freed((void *)ctx, sizeof(*ctx));
	lockdep_init_map(&ctx->dep_map, ww_class->acquire_name,
			 &ww_class->acquire_key, 0);
	mutex_acquire(&ctx->dep_map, 0, 0, _RET_IP_);
#endif
#ifdef CONFIG_DEBUG_WW_MUTEX_SLOWPATH
	ctx->deadlock_inject_interval = 1;
	ctx->deadlock_inject_countdown = ctx->stamp & 0xf;
#endif
}

/**
 * ww_acquire_done - marks the end of the acquire phase
 * @ctx: the acquire context
 *
 * Marks the end of the acquire phase, any further w/w mutex lock calls using
 * this context are forbidden.
 *
 * Calling this function is optional, it is just useful to document w/w mutex
 * code and clearly designated the acquire phase from actually using the locked
 * data structures.
 */
static inline void ww_acquire_done(struct ww_acquire_ctx *ctx)
{
#ifdef CONFIG_DEBUG_MUTEXES
	lockdep_assert_held(ctx);

	DEBUG_LOCKS_WARN_ON(ctx->done_acquire);
	ctx->done_acquire = 1;
#endif
}

/**
 * ww_acquire_fini - releases a w/w acquire context
 * @ctx: the acquire context to free
 *
 * Releases a w/w acquire context. This must be called _after_ all acquired w/w
 * mutexes have been released with ww_mutex_unlock.
 */
static inline void ww_acquire_fini(struct ww_acquire_ctx *ctx)
{
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	mutex_release(&ctx->dep_map, _THIS_IP_);
#endif
#ifdef CONFIG_DEBUG_MUTEXES
	DEBUG_LOCKS_WARN_ON(ctx->acquired);
	if (!IS_ENABLED(CONFIG_PROVE_LOCKING))
		/*
		 * lockdep will normally handle this,
		 * but fail without anyway
		 */
		ctx->done_acquire = 1;

	if (!IS_ENABLED(CONFIG_DEBUG_LOCK_ALLOC))
		/* ensure ww_acquire_fini will still fail if called twice */
		ctx->acquired = ~0U;
#endif
}

/**
 * ww_mutex_lock - acquire the w/w mutex
 * @lock: the mutex to be acquired
 * @ctx: w/w acquire context, or NULL to acquire only a single lock.
 *
 * Lock the w/w mutex exclusively for this task.
 *
 * Deadlocks within a given w/w class of locks are detected and handled with the
 * wait/die algorithm. If the lock isn't immediately available this function
 * will either sleep until it is (wait case). Or it selects the current context
 * for backing off by returning -EDEADLK (die case). Trying to acquire the
 * same lock with the same context twice is also detected and signalled by
 * returning -EALREADY. Returns 0 if the mutex was successfully acquired.
 *
 * In the die case the caller must release all currently held w/w mutexes for
 * the given context and then wait for this contending lock to be available by
 * calling ww_mutex_lock_slow. Alternatively callers can opt to not acquire this
 * lock and proceed with trying to acquire further w/w mutexes (e.g. when
 * scanning through lru lists trying to free resources).
 *
 * The mutex must later on be released by the same task that
 * acquired it. The task may not exit without first unlocking the mutex. Also,
 * kernel memory where the mutex resides must not be freed with the mutex still
 * locked. The mutex must first be initialized (or statically defined) before it
 * can be locked. memset()-ing the mutex to 0 is not allowed. The mutex must be
 * of the same w/w lock class as was used to initialize the acquire context.
 *
 * A mutex acquired with this function must be released with ww_mutex_unlock.
 */
extern int /* __must_check */ ww_mutex_lock(struct ww_mutex *lock, struct ww_acquire_ctx *ctx);

/**
 * ww_mutex_lock_interruptible - acquire the w/w mutex, interruptible
 * @lock: the mutex to be acquired
 * @ctx: w/w acquire context
 *
 * Lock the w/w mutex exclusively for this task.
 *
 * Deadlocks within a given w/w class of locks are detected and handled with the
 * wait/die algorithm. If the lock isn't immediately available this function
 * will either sleep until it is (wait case). Or it selects the current context
 * for backing off by returning -EDEADLK (die case). Trying to acquire the
 * same lock with the same context twice is also detected and signalled by
 * returning -EALREADY. Returns 0 if the mutex was successfully acquired. If a
 * signal arrives while waiting for the lock then this function returns -EINTR.
 *
 * In the die case the caller must release all currently held w/w mutexes for
 * the given context and then wait for this contending lock to be available by
 * calling ww_mutex_lock_slow_interruptible. Alternatively callers can opt to
 * not acquire this lock and proceed with trying to acquire further w/w mutexes
 * (e.g. when scanning through lru lists trying to free resources).
 *
 * The mutex must later on be released by the same task that
 * acquired it. The task may not exit without first unlocking the mutex. Also,
 * kernel memory where the mutex resides must not be freed with the mutex still
 * locked. The mutex must first be initialized (or statically defined) before it
 * can be locked. memset()-ing the mutex to 0 is not allowed. The mutex must be
 * of the same w/w lock class as was used to initialize the acquire context.
 *
 * A mutex acquired with this function must be released with ww_mutex_unlock.
 */
extern int __must_check ww_mutex_lock_interruptible(struct ww_mutex *lock,
						    struct ww_acquire_ctx *ctx);

/**
 * ww_mutex_lock_slow - slowpath acquiring of the w/w mutex
 * @lock: the mutex to be acquired
 * @ctx: w/w acquire context
 *
 * Acquires a w/w mutex with the given context after a die case. This function
 * will sleep until the lock becomes available.
 *
 * The caller must have released all w/w mutexes already acquired with the
 * context and then call this function on the contended lock.
 *
 * Afterwards the caller may continue to (re)acquire the other w/w mutexes it
 * needs with ww_mutex_lock. Note that the -EALREADY return code from
 * ww_mutex_lock can be used to avoid locking this contended mutex twice.
 *
 * It is forbidden to call this function with any other w/w mutexes associated
 * with the context held. It is forbidden to call this on anything else than the
 * contending mutex.
 *
 * Note that the slowpath lock acquiring can also be done by calling
 * ww_mutex_lock directly. This function here is simply to help w/w mutex
 * locking code readability by clearly denoting the slowpath.
 */
static inline void
ww_mutex_lock_slow(struct ww_mutex *lock, struct ww_acquire_ctx *ctx)
{
	int ret;
#ifdef CONFIG_DEBUG_MUTEXES
	DEBUG_LOCKS_WARN_ON(!ctx->contending_lock);
#endif
	ret = ww_mutex_lock(lock, ctx);
	(void)ret;
}

/**
 * ww_mutex_lock_slow_interruptible - slowpath acquiring of the w/w mutex, interruptible
 * @lock: the mutex to be acquired
 * @ctx: w/w acquire context
 *
 * Acquires a w/w mutex with the given context after a die case. This function
 * will sleep until the lock becomes available and returns 0 when the lock has
 * been acquired. If a signal arrives while waiting for the lock then this
 * function returns -EINTR.
 *
 * The caller must have released all w/w mutexes already acquired with the
 * context and then call this function on the contended lock.
 *
 * Afterwards the caller may continue to (re)acquire the other w/w mutexes it
 * needs with ww_mutex_lock. Note that the -EALREADY return code from
 * ww_mutex_lock can be used to avoid locking this contended mutex twice.
 *
 * It is forbidden to call this function with any other w/w mutexes associated
 * with the given context held. It is forbidden to call this on anything else
 * than the contending mutex.
 *
 * Note that the slowpath lock acquiring can also be done by calling
 * ww_mutex_lock_interruptible directly. This function here is simply to help
 * w/w mutex locking code readability by clearly denoting the slowpath.
 */
static inline int __must_check
ww_mutex_lock_slow_interruptible(struct ww_mutex *lock,
				 struct ww_acquire_ctx *ctx)
{
#ifdef CONFIG_DEBUG_MUTEXES
	DEBUG_LOCKS_WARN_ON(!ctx->contending_lock);
#endif
	return ww_mutex_lock_interruptible(lock, ctx);
}

extern void ww_mutex_unlock(struct ww_mutex *lock);

/**
 * ww_mutex_trylock - tries to acquire the w/w mutex without acquire context
 * @lock: mutex to lock
 *
 * Trylocks a mutex without acquire context, so no deadlock detection is
 * possible. Returns 1 if the mutex has been acquired successfully, 0 otherwise.
 */
static inline int __must_check ww_mutex_trylock(struct ww_mutex *lock)
{
	return mutex_trylock(&lock->base);
}

/***
 * ww_mutex_destroy - mark a w/w mutex unusable
 * @lock: the mutex to be destroyed
 *
 * This function marks the mutex uninitialized, and any subsequent
 * use of the mutex is forbidden. The mutex must not be locked when
 * this function is called.
 */
static inline void ww_mutex_destroy(struct ww_mutex *lock)
{
	mutex_destroy(&lock->base);
}

/**
 * ww_mutex_is_locked - is the w/w mutex locked
 * @lock: the mutex to be queried
 *
 * Returns 1 if the mutex is locked, 0 if unlocked.
 */
static inline bool ww_mutex_is_locked(struct ww_mutex *lock)
{
	return mutex_is_locked(&lock->base);
}

#endif
