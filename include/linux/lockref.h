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

struct lockref {
	spinlock_t lock;
	unsigned int count;
};

/**
 * lockref_get - Increments reference count unconditionally
 * @lockcnt: pointer to lockref structure
 *
 * This operation is only valid if you already hold a reference
 * to the object, so you know the count cannot be zero.
 */
static inline void lockref_get(struct lockref *lockref)
{
	spin_lock(&lockref->lock);
	lockref->count++;
	spin_unlock(&lockref->lock);
}

/**
 * lockref_get_not_zero - Increments count unless the count is 0
 * @lockcnt: pointer to lockref structure
 * Return: 1 if count updated successfully or 0 if count is 0
 */
static inline int lockref_get_not_zero(struct lockref *lockref)
{
	int retval = 0;

	spin_lock(&lockref->lock);
	if (lockref->count) {
		lockref->count++;
		retval = 1;
	}
	spin_unlock(&lockref->lock);
	return retval;
}

/**
 * lockref_get_or_lock - Increments count unless the count is 0
 * @lockcnt: pointer to lockref structure
 * Return: 1 if count updated successfully or 0 if count was zero
 * and we got the lock instead.
 */
static inline int lockref_get_or_lock(struct lockref *lockref)
{
	spin_lock(&lockref->lock);
	if (!lockref->count)
		return 0;
	lockref->count++;
	spin_unlock(&lockref->lock);
	return 1;
}

/**
 * lockref_put_or_lock - decrements count unless count <= 1 before decrement
 * @lockcnt: pointer to lockref structure
 * Return: 1 if count updated successfully or 0 if count <= 1 and lock taken
 */
static inline int lockref_put_or_lock(struct lockref *lockref)
{
	spin_lock(&lockref->lock);
	if (lockref->count <= 1)
		return 0;
	lockref->count--;
	spin_unlock(&lockref->lock);
	return 1;
}

#endif /* __LINUX_LOCKREF_H */
