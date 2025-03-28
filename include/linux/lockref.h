/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_LOCKREF_H
#define __LINUX_LOCKREF_H

/*
 * Locked reference counts.
 *
 * These are different from just plain atomic refcounts in that they
 * are atomic with respect to the spinlock that goes with them.  In
 * particular, there can be implementations that don't actually get
 * the spinlock for the common decrement/increment operations, but they
 * still have to check that the operation is done semantically as if
 * the spinlock had been taken (using a cmpxchg operation that covers
 * both the lock and the count word, or using memory transactions, for
 * example).
 */

#include <linux/spinlock.h>
#include <generated/bounds.h>

#define USE_CMPXCHG_LOCKREF \
	(IS_ENABLED(CONFIG_ARCH_USE_CMPXCHG_LOCKREF) && \
	 IS_ENABLED(CONFIG_SMP) && SPINLOCK_SIZE <= 4)

struct lockref {
	union {
#if USE_CMPXCHG_LOCKREF
		aligned_u64 lock_count;
#endif
		struct {
			spinlock_t lock;
			int count;
		};
	};
};

/**
 * lockref_init - Initialize a lockref
 * @lockref: pointer to lockref structure
 *
 * Initializes @lockref->count to 1.
 */
static inline void lockref_init(struct lockref *lockref)
{
	spin_lock_init(&lockref->lock);
	lockref->count = 1;
}

void lockref_get(struct lockref *lockref);
int lockref_put_return(struct lockref *lockref);
bool lockref_get_not_zero(struct lockref *lockref);
bool lockref_put_or_lock(struct lockref *lockref);

void lockref_mark_dead(struct lockref *lockref);
bool lockref_get_not_dead(struct lockref *lockref);

/* Must be called under spinlock for reliable results */
static inline bool __lockref_is_dead(const struct lockref *l)
{
	return ((int)l->count < 0);
}

#endif /* __LINUX_LOCKREF_H */
