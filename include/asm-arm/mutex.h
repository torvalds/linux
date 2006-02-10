/*
 * include/asm-arm/mutex.h
 *
 * ARM optimized mutex locking primitives
 *
 * Please look into asm-generic/mutex-xchg.h for a formal definition.
 */
#ifndef _ASM_MUTEX_H
#define _ASM_MUTEX_H

#if __LINUX_ARM_ARCH__ < 6
/* On pre-ARMv6 hardware the swp based implementation is the most efficient. */
# include <asm-generic/mutex-xchg.h>
#else

/*
 * Attempting to lock a mutex on ARMv6+ can be done with a bastardized
 * atomic decrement (it is not a reliable atomic decrement but it satisfies
 * the defined semantics for our purpose, while being smaller and faster
 * than a real atomic decrement or atomic swap.  The idea is to attempt
 * decrementing the lock value only once.  If once decremented it isn't zero,
 * or if its store-back fails due to a dispute on the exclusive store, we
 * simply bail out immediately through the slow path where the lock will be
 * reattempted until it succeeds.
 */
static inline void
__mutex_fastpath_lock(atomic_t *count, fastcall void (*fail_fn)(atomic_t *))
{
	int __ex_flag, __res;

	__asm__ (

		"ldrex	%0, [%2]	\n\t"
		"sub	%0, %0, #1	\n\t"
		"strex	%1, %0, [%2]	"

		: "=&r" (__res), "=&r" (__ex_flag)
		: "r" (&(count)->counter)
		: "cc","memory" );

	__res |= __ex_flag;
	if (unlikely(__res != 0))
		fail_fn(count);
}

static inline int
__mutex_fastpath_lock_retval(atomic_t *count, fastcall int (*fail_fn)(atomic_t *))
{
	int __ex_flag, __res;

	__asm__ (

		"ldrex	%0, [%2]	\n\t"
		"sub	%0, %0, #1	\n\t"
		"strex	%1, %0, [%2]	"

		: "=&r" (__res), "=&r" (__ex_flag)
		: "r" (&(count)->counter)
		: "cc","memory" );

	__res |= __ex_flag;
	if (unlikely(__res != 0))
		__res = fail_fn(count);
	return __res;
}

/*
 * Same trick is used for the unlock fast path. However the original value,
 * rather than the result, is used to test for success in order to have
 * better generated assembly.
 */
static inline void
__mutex_fastpath_unlock(atomic_t *count, fastcall void (*fail_fn)(atomic_t *))
{
	int __ex_flag, __res, __orig;

	__asm__ (

		"ldrex	%0, [%3]	\n\t"
		"add	%1, %0, #1	\n\t"
		"strex	%2, %1, [%3]	"

		: "=&r" (__orig), "=&r" (__res), "=&r" (__ex_flag)
		: "r" (&(count)->counter)
		: "cc","memory" );

	__orig |= __ex_flag;
	if (unlikely(__orig != 0))
		fail_fn(count);
}

/*
 * If the unlock was done on a contended lock, or if the unlock simply fails
 * then the mutex remains locked.
 */
#define __mutex_slowpath_needs_to_unlock()	1

/*
 * For __mutex_fastpath_trylock we use another construct which could be
 * described as a "single value cmpxchg".
 *
 * This provides the needed trylock semantics like cmpxchg would, but it is
 * lighter and less generic than a true cmpxchg implementation.
 */
static inline int
__mutex_fastpath_trylock(atomic_t *count, int (*fail_fn)(atomic_t *))
{
	int __ex_flag, __res, __orig;

	__asm__ (

		"1: ldrex	%0, [%3]	\n\t"
		"subs		%1, %0, #1	\n\t"
		"strexeq	%2, %1, [%3]	\n\t"
		"movlt		%0, #0		\n\t"
		"cmpeq		%2, #0		\n\t"
		"bgt		1b		"

		: "=&r" (__orig), "=&r" (__res), "=&r" (__ex_flag)
		: "r" (&count->counter)
		: "cc", "memory" );

	return __orig;
}

#endif
#endif
