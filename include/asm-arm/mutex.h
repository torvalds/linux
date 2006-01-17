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
#define __mutex_fastpath_lock(count, fail_fn)				\
do {									\
	int __ex_flag, __res;						\
									\
	typecheck(atomic_t *, count);					\
	typecheck_fn(fastcall void (*)(atomic_t *), fail_fn);		\
									\
	__asm__ (							\
		"ldrex	%0, [%2]	\n"				\
		"sub	%0, %0, #1	\n"				\
		"strex	%1, %0, [%2]	\n"				\
									\
		: "=&r" (__res), "=&r" (__ex_flag)			\
		: "r" (&(count)->counter)				\
		: "cc","memory" );					\
									\
	if (unlikely(__res || __ex_flag))				\
		fail_fn(count);						\
} while (0)

#define __mutex_fastpath_lock_retval(count, fail_fn)			\
({									\
	int __ex_flag, __res;						\
									\
	typecheck(atomic_t *, count);					\
	typecheck_fn(fastcall int (*)(atomic_t *), fail_fn);		\
									\
	__asm__ (							\
		"ldrex	%0, [%2]	\n"				\
		"sub	%0, %0, #1	\n"				\
		"strex	%1, %0, [%2]	\n"				\
									\
		: "=&r" (__res), "=&r" (__ex_flag)			\
		: "r" (&(count)->counter)				\
		: "cc","memory" );					\
									\
	__res |= __ex_flag;						\
	if (unlikely(__res != 0))					\
		__res = fail_fn(count);					\
	__res;								\
})

/*
 * Same trick is used for the unlock fast path. However the original value,
 * rather than the result, is used to test for success in order to have
 * better generated assembly.
 */
#define __mutex_fastpath_unlock(count, fail_fn)				\
do {									\
	int __ex_flag, __res, __orig;					\
									\
	typecheck(atomic_t *, count);					\
	typecheck_fn(fastcall void (*)(atomic_t *), fail_fn);		\
									\
	__asm__ (							\
		"ldrex	%0, [%3]	\n"				\
		"add	%1, %0, #1	\n"				\
		"strex	%2, %1, [%3]	\n"				\
									\
		: "=&r" (__orig), "=&r" (__res), "=&r" (__ex_flag)	\
		: "r" (&(count)->counter)				\
		: "cc","memory" );					\
									\
	if (unlikely(__orig || __ex_flag))				\
		fail_fn(count);						\
} while (0)

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

		"1: ldrex	%0, [%3]	\n"
		"subs		%1, %0, #1	\n"
		"strexeq	%2, %1, [%3]	\n"
		"movlt		%0, #0		\n"
		"cmpeq		%2, #0		\n"
		"bgt		1b		\n"

		: "=&r" (__orig), "=&r" (__res), "=&r" (__ex_flag)
		: "r" (&count->counter)
		: "cc", "memory" );

	return __orig;
}

#endif
#endif
