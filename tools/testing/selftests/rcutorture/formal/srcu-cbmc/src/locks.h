/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LOCKS_H
#define LOCKS_H

#include <limits.h>
#include <pthread.h>
#include <stdbool.h>

#include "assume.h"
#include "bug_on.h"
#include "preempt.h"

int nondet_int(void);

#define __acquire(x)
#define __acquires(x)
#define __release(x)
#define __releases(x)

/* Only use one lock mechanism. Select which one. */
#ifdef PTHREAD_LOCK
struct lock_impl {
	pthread_mutex_t mutex;
};

static inline void lock_impl_lock(struct lock_impl *lock)
{
	BUG_ON(pthread_mutex_lock(&lock->mutex));
}

static inline void lock_impl_unlock(struct lock_impl *lock)
{
	BUG_ON(pthread_mutex_unlock(&lock->mutex));
}

static inline bool lock_impl_trylock(struct lock_impl *lock)
{
	int err = pthread_mutex_trylock(&lock->mutex);

	if (!err)
		return true;
	else if (err == EBUSY)
		return false;
	BUG();
}

static inline void lock_impl_init(struct lock_impl *lock)
{
	pthread_mutex_init(&lock->mutex, NULL);
}

#define LOCK_IMPL_INITIALIZER {.mutex = PTHREAD_MUTEX_INITIALIZER}

#else /* !defined(PTHREAD_LOCK) */
/* Spinlock that assumes that it always gets the lock immediately. */

struct lock_impl {
	bool locked;
};

static inline bool lock_impl_trylock(struct lock_impl *lock)
{
#ifdef RUN
	/* TODO: Should this be a test and set? */
	return __sync_bool_compare_and_swap(&lock->locked, false, true);
#else
	__CPROVER_atomic_begin();
	bool old_locked = lock->locked;
	lock->locked = true;
	__CPROVER_atomic_end();

	/* Minimal barrier to prevent accesses leaking out of lock. */
	__CPROVER_fence("RRfence", "RWfence");

	return !old_locked;
#endif
}

static inline void lock_impl_lock(struct lock_impl *lock)
{
	/*
	 * CBMC doesn't support busy waiting, so just assume that the
	 * lock is available.
	 */
	assume(lock_impl_trylock(lock));

	/*
	 * If the lock was already held by this thread then the assumption
	 * is unsatisfiable (deadlock).
	 */
}

static inline void lock_impl_unlock(struct lock_impl *lock)
{
#ifdef RUN
	BUG_ON(!__sync_bool_compare_and_swap(&lock->locked, true, false));
#else
	/* Minimal barrier to prevent accesses leaking out of lock. */
	__CPROVER_fence("RWfence", "WWfence");

	__CPROVER_atomic_begin();
	bool old_locked = lock->locked;
	lock->locked = false;
	__CPROVER_atomic_end();

	BUG_ON(!old_locked);
#endif
}

static inline void lock_impl_init(struct lock_impl *lock)
{
	lock->locked = false;
}

#define LOCK_IMPL_INITIALIZER {.locked = false}

#endif /* !defined(PTHREAD_LOCK) */

/*
 * Implement spinlocks using the lock mechanism. Wrap the lock to prevent mixing
 * locks of different types.
 */
typedef struct {
	struct lock_impl internal_lock;
} spinlock_t;

#define SPIN_LOCK_UNLOCKED {.internal_lock = LOCK_IMPL_INITIALIZER}
#define __SPIN_LOCK_UNLOCKED(x) SPIN_LOCK_UNLOCKED
#define DEFINE_SPINLOCK(x) spinlock_t x = SPIN_LOCK_UNLOCKED

static inline void spin_lock_init(spinlock_t *lock)
{
	lock_impl_init(&lock->internal_lock);
}

static inline void spin_lock(spinlock_t *lock)
{
	/*
	 * Spin locks also need to be removed in order to eliminate all
	 * memory barriers. They are only used by the write side anyway.
	 */
#ifndef NO_SYNC_SMP_MB
	preempt_disable();
	lock_impl_lock(&lock->internal_lock);
#endif
}

static inline void spin_unlock(spinlock_t *lock)
{
#ifndef NO_SYNC_SMP_MB
	lock_impl_unlock(&lock->internal_lock);
	preempt_enable();
#endif
}

/* Don't bother with interrupts */
#define spin_lock_irq(lock) spin_lock(lock)
#define spin_unlock_irq(lock) spin_unlock(lock)
#define spin_lock_irqsave(lock, flags) spin_lock(lock)
#define spin_unlock_irqrestore(lock, flags) spin_unlock(lock)

/*
 * This is supposed to return an int, but I think that a bool should work as
 * well.
 */
static inline bool spin_trylock(spinlock_t *lock)
{
#ifndef NO_SYNC_SMP_MB
	preempt_disable();
	return lock_impl_trylock(&lock->internal_lock);
#else
	return true;
#endif
}

struct completion {
	/* Hopefuly this won't overflow. */
	unsigned int count;
};

#define COMPLETION_INITIALIZER(x) {.count = 0}
#define DECLARE_COMPLETION(x) struct completion x = COMPLETION_INITIALIZER(x)
#define DECLARE_COMPLETION_ONSTACK(x) DECLARE_COMPLETION(x)

static inline void init_completion(struct completion *c)
{
	c->count = 0;
}

static inline void wait_for_completion(struct completion *c)
{
	unsigned int prev_count = __sync_fetch_and_sub(&c->count, 1);

	assume(prev_count);
}

static inline void complete(struct completion *c)
{
	unsigned int prev_count = __sync_fetch_and_add(&c->count, 1);

	BUG_ON(prev_count == UINT_MAX);
}

/* This function probably isn't very useful for CBMC. */
static inline bool try_wait_for_completion(struct completion *c)
{
	BUG();
}

static inline bool completion_done(struct completion *c)
{
	return c->count;
}

/* TODO: Implement complete_all */
static inline void complete_all(struct completion *c)
{
	BUG();
}

#endif
