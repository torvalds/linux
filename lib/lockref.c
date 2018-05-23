// SPDX-License-Identifier: GPL-2.0
#include <linux/export.h>
#include <linux/lockref.h>

#if USE_CMPXCHG_LOCKREF

/*
 * Note that the "cmpxchg()" reloads the "old" value for the
 * failure case.
 */
#define CMPXCHG_LOOP(CODE, SUCCESS) do {					\
	struct lockref old;							\
	BUILD_BUG_ON(sizeof(old) != 8);						\
	old.lock_count = READ_ONCE(lockref->lock_count);			\
	while (likely(arch_spin_value_unlocked(old.lock.rlock.raw_lock))) {  	\
		struct lockref new = old, prev = old;				\
		CODE								\
		old.lock_count = cmpxchg64_relaxed(&lockref->lock_count,	\
						   old.lock_count,		\
						   new.lock_count);		\
		if (likely(old.lock_count == prev.lock_count)) {		\
			SUCCESS;						\
		}								\
		cpu_relax();							\
	}									\
} while (0)

#else

#define CMPXCHG_LOOP(CODE, SUCCESS) do { } while (0)

#endif

/**
 * lockref_get - Increments reference count unconditionally
 * @lockref: pointer to lockref structure
 *
 * This operation is only valid if you already hold a reference
 * to the object, so you know the count cannot be zero.
 */
void lockref_get(struct lockref *lockref)
{
	CMPXCHG_LOOP(
		new.count++;
	,
		return;
	);

	spin_lock(&lockref->lock);
	lockref->count++;
	spin_unlock(&lockref->lock);
}
EXPORT_SYMBOL(lockref_get);

/**
 * lockref_get_not_zero - Increments count unless the count is 0 or dead
 * @lockref: pointer to lockref structure
 * Return: 1 if count updated successfully or 0 if count was zero
 */
int lockref_get_not_zero(struct lockref *lockref)
{
	int retval;

	CMPXCHG_LOOP(
		new.count++;
		if (old.count <= 0)
			return 0;
	,
		return 1;
	);

	spin_lock(&lockref->lock);
	retval = 0;
	if (lockref->count > 0) {
		lockref->count++;
		retval = 1;
	}
	spin_unlock(&lockref->lock);
	return retval;
}
EXPORT_SYMBOL(lockref_get_not_zero);

/**
 * lockref_put_not_zero - Decrements count unless count <= 1 before decrement
 * @lockref: pointer to lockref structure
 * Return: 1 if count updated successfully or 0 if count would become zero
 */
int lockref_put_not_zero(struct lockref *lockref)
{
	int retval;

	CMPXCHG_LOOP(
		new.count--;
		if (old.count <= 1)
			return 0;
	,
		return 1;
	);

	spin_lock(&lockref->lock);
	retval = 0;
	if (lockref->count > 1) {
		lockref->count--;
		retval = 1;
	}
	spin_unlock(&lockref->lock);
	return retval;
}
EXPORT_SYMBOL(lockref_put_not_zero);

/**
 * lockref_get_or_lock - Increments count unless the count is 0 or dead
 * @lockref: pointer to lockref structure
 * Return: 1 if count updated successfully or 0 if count was zero
 * and we got the lock instead.
 */
int lockref_get_or_lock(struct lockref *lockref)
{
	CMPXCHG_LOOP(
		new.count++;
		if (old.count <= 0)
			break;
	,
		return 1;
	);

	spin_lock(&lockref->lock);
	if (lockref->count <= 0)
		return 0;
	lockref->count++;
	spin_unlock(&lockref->lock);
	return 1;
}
EXPORT_SYMBOL(lockref_get_or_lock);

/**
 * lockref_put_return - Decrement reference count if possible
 * @lockref: pointer to lockref structure
 *
 * Decrement the reference count and return the new value.
 * If the lockref was dead or locked, return an error.
 */
int lockref_put_return(struct lockref *lockref)
{
	CMPXCHG_LOOP(
		new.count--;
		if (old.count <= 0)
			return -1;
	,
		return new.count;
	);
	return -1;
}
EXPORT_SYMBOL(lockref_put_return);

/**
 * lockref_put_or_lock - decrements count unless count <= 1 before decrement
 * @lockref: pointer to lockref structure
 * Return: 1 if count updated successfully or 0 if count <= 1 and lock taken
 */
int lockref_put_or_lock(struct lockref *lockref)
{
	CMPXCHG_LOOP(
		new.count--;
		if (old.count <= 1)
			break;
	,
		return 1;
	);

	spin_lock(&lockref->lock);
	if (lockref->count <= 1)
		return 0;
	lockref->count--;
	spin_unlock(&lockref->lock);
	return 1;
}
EXPORT_SYMBOL(lockref_put_or_lock);

/**
 * lockref_mark_dead - mark lockref dead
 * @lockref: pointer to lockref structure
 */
void lockref_mark_dead(struct lockref *lockref)
{
	assert_spin_locked(&lockref->lock);
	lockref->count = -128;
}
EXPORT_SYMBOL(lockref_mark_dead);

/**
 * lockref_get_not_dead - Increments count unless the ref is dead
 * @lockref: pointer to lockref structure
 * Return: 1 if count updated successfully or 0 if lockref was dead
 */
int lockref_get_not_dead(struct lockref *lockref)
{
	int retval;

	CMPXCHG_LOOP(
		new.count++;
		if (old.count < 0)
			return 0;
	,
		return 1;
	);

	spin_lock(&lockref->lock);
	retval = 0;
	if (lockref->count >= 0) {
		lockref->count++;
		retval = 1;
	}
	spin_unlock(&lockref->lock);
	return retval;
}
EXPORT_SYMBOL(lockref_get_not_dead);
