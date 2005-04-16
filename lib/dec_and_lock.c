#include <linux/module.h>
#include <linux/spinlock.h>
#include <asm/atomic.h>

/*
 * This is an architecture-neutral, but slow,
 * implementation of the notion of "decrement
 * a reference count, and return locked if it
 * decremented to zero".
 *
 * NOTE NOTE NOTE! This is _not_ equivalent to
 *
 *	if (atomic_dec_and_test(&atomic)) {
 *		spin_lock(&lock);
 *		return 1;
 *	}
 *	return 0;
 *
 * because the spin-lock and the decrement must be
 * "atomic".
 *
 * This slow version gets the spinlock unconditionally,
 * and releases it if it isn't needed. Architectures
 * are encouraged to come up with better approaches,
 * this is trivially done efficiently using a load-locked
 * store-conditional approach, for example.
 */

#ifndef ATOMIC_DEC_AND_LOCK
int _atomic_dec_and_lock(atomic_t *atomic, spinlock_t *lock)
{
	spin_lock(lock);
	if (atomic_dec_and_test(atomic))
		return 1;
	spin_unlock(lock);
	return 0;
}

EXPORT_SYMBOL(_atomic_dec_and_lock);
#endif
