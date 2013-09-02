#include <linux/export.h>
#include <linux/lockref.h>

/**
 * lockref_get - Increments reference count unconditionally
 * @lockcnt: pointer to lockref structure
 *
 * This operation is only valid if you already hold a reference
 * to the object, so you know the count cannot be zero.
 */
void lockref_get(struct lockref *lockref)
{
	spin_lock(&lockref->lock);
	lockref->count++;
	spin_unlock(&lockref->lock);
}
EXPORT_SYMBOL(lockref_get);

/**
 * lockref_get_not_zero - Increments count unless the count is 0
 * @lockcnt: pointer to lockref structure
 * Return: 1 if count updated successfully or 0 if count was zero
 */
int lockref_get_not_zero(struct lockref *lockref)
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
EXPORT_SYMBOL(lockref_get_not_zero);

/**
 * lockref_get_or_lock - Increments count unless the count is 0
 * @lockcnt: pointer to lockref structure
 * Return: 1 if count updated successfully or 0 if count was zero
 * and we got the lock instead.
 */
int lockref_get_or_lock(struct lockref *lockref)
{
	spin_lock(&lockref->lock);
	if (!lockref->count)
		return 0;
	lockref->count++;
	spin_unlock(&lockref->lock);
	return 1;
}
EXPORT_SYMBOL(lockref_get_or_lock);

/**
 * lockref_put_or_lock - decrements count unless count <= 1 before decrement
 * @lockcnt: pointer to lockref structure
 * Return: 1 if count updated successfully or 0 if count <= 1 and lock taken
 */
int lockref_put_or_lock(struct lockref *lockref)
{
	spin_lock(&lockref->lock);
	if (lockref->count <= 1)
		return 0;
	lockref->count--;
	spin_unlock(&lockref->lock);
	return 1;
}
EXPORT_SYMBOL(lockref_put_or_lock);
