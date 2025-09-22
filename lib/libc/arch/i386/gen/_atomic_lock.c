/*	$OpenBSD: _atomic_lock.c,v 1.1 2017/08/15 06:13:24 guenther Exp $	*/
/* David Leonard, <d@csee.uq.edu.au>. Public domain. */

/*
 * Atomic lock for i386
 */

#include <machine/spinlock.h>

int
_atomic_lock(volatile _atomic_lock_t *lock)
{
	_atomic_lock_t old;

	/*
	 * Use the eXCHanGe instruction to swap the lock value with
	 * a local variable containing the locked state.
	 */
	old = _ATOMIC_LOCK_LOCKED;
	__asm__("xchg %0,(%2)"
		: "=r" (old)
		: "0"  (old), "r"  (lock));

	return (old != _ATOMIC_LOCK_UNLOCKED);
}
