/*
 * asm-generic/mutex-dec.h
 *
 * Generic implementation of the mutex fastpath, based on atomic
 * decrement/increment.
 */
#ifndef _ASM_GENERIC_MUTEX_DEC_H
#define _ASM_GENERIC_MUTEX_DEC_H

/**
 *  __mutex_fastpath_lock - try to take the lock by moving the count
 *                          from 1 to a 0 value
 *  @count: pointer of type atomic_t
 *  @fail_fn: function to call if the original value was not 1
 *
 * Change the count from 1 to a value lower than 1, and call <fail_fn> if
 * it wasn't 1 originally. This function MUST leave the value lower than
 * 1 even when the "1" assertion wasn't true.
 */
#define __mutex_fastpath_lock(count, fail_fn)				\
do {									\
	if (unlikely(atomic_dec_return(count) < 0))			\
		fail_fn(count);						\
	else								\
		smp_mb();						\
} while (0)

/**
 *  __mutex_fastpath_lock_retval - try to take the lock by moving the count
 *                                 from 1 to a 0 value
 *  @count: pointer of type atomic_t
 *  @fail_fn: function to call if the original value was not 1
 *
 * Change the count from 1 to a value lower than 1, and call <fail_fn> if
 * it wasn't 1 originally. This function returns 0 if the fastpath succeeds,
 * or anything the slow path function returns.
 */
static inline int
__mutex_fastpath_lock_retval(atomic_t *count, int (*fail_fn)(atomic_t *))
{
	if (unlikely(atomic_dec_return(count) < 0))
		return fail_fn(count);
	else {
		smp_mb();
		return 0;
	}
}

/**
 *  __mutex_fastpath_unlock - try to promote the count from 0 to 1
 *  @count: pointer of type atomic_t
 *  @fail_fn: function to call if the original value was not 0
 *
 * Try to promote the count from 0 to 1. If it wasn't 0, call <fail_fn>.
 * In the failure case, this function is allowed to either set the value to
 * 1, or to set it to a value lower than 1.
 *
 * If the implementation sets it to a value of lower than 1, then the
 * __mutex_slowpath_needs_to_unlock() macro needs to return 1, it needs
 * to return 0 otherwise.
 */
#define __mutex_fastpath_unlock(count, fail_fn)				\
do {									\
	smp_mb();							\
	if (unlikely(atomic_inc_return(count) <= 0))			\
		fail_fn(count);						\
} while (0)

#define __mutex_slowpath_needs_to_unlock()		1

/**
 * __mutex_fastpath_trylock - try to acquire the mutex, without waiting
 *
 *  @count: pointer of type atomic_t
 *  @fail_fn: fallback function
 *
 * Change the count from 1 to a value lower than 1, and return 0 (failure)
 * if it wasn't 1 originally, or return 1 (success) otherwise. This function
 * MUST leave the value lower than 1 even when the "1" assertion wasn't true.
 * Additionally, if the value was < 0 originally, this function must not leave
 * it to 0 on failure.
 *
 * If the architecture has no effective trylock variant, it should call the
 * <fail_fn> spinlock-based trylock variant unconditionally.
 */
static inline int
__mutex_fastpath_trylock(atomic_t *count, int (*fail_fn)(atomic_t *))
{
	/*
	 * We have two variants here. The cmpxchg based one is the best one
	 * because it never induce a false contention state.  It is included
	 * here because architectures using the inc/dec algorithms over the
	 * xchg ones are much more likely to support cmpxchg natively.
	 *
	 * If not we fall back to the spinlock based variant - that is
	 * just as efficient (and simpler) as a 'destructive' probing of
	 * the mutex state would be.
	 */
#ifdef __HAVE_ARCH_CMPXCHG
	if (likely(atomic_cmpxchg(count, 1, 0) == 1)) {
		smp_mb();
		return 1;
	}
	return 0;
#else
	return fail_fn(count);
#endif
}

#endif
