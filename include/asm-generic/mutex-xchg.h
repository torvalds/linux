/*
 * include/asm-generic/mutex-xchg.h
 *
 * Generic implementation of the mutex fastpath, based on xchg().
 *
 * NOTE: An xchg based implementation might be less optimal than an atomic
 *       decrement/increment based implementation. If your architecture
 *       has a reasonable atomic dec/inc then you should probably use
 *	 asm-generic/mutex-dec.h instead, or you could open-code an
 *	 optimized version in asm/mutex.h.
 */
#ifndef _ASM_GENERIC_MUTEX_XCHG_H
#define _ASM_GENERIC_MUTEX_XCHG_H

/**
 *  __mutex_fastpath_lock - try to take the lock by moving the count
 *                          from 1 to a 0 value
 *  @count: pointer of type atomic_t
 *  @fail_fn: function to call if the original value was not 1
 *
 * Change the count from 1 to a value lower than 1, and call <fail_fn> if it
 * wasn't 1 originally. This function MUST leave the value lower than 1
 * even when the "1" assertion wasn't true.
 */
static inline void
__mutex_fastpath_lock(atomic_t *count, void (*fail_fn)(atomic_t *))
{
	if (unlikely(atomic_xchg(count, 0) != 1))
		/*
		 * We failed to acquire the lock, so mark it contended
		 * to ensure that any waiting tasks are woken up by the
		 * unlock slow path.
		 */
		if (likely(atomic_xchg(count, -1) != 1))
			fail_fn(count);
}

/**
 *  __mutex_fastpath_lock_retval - try to take the lock by moving the count
 *                                 from 1 to a 0 value
 *  @count: pointer of type atomic_t
 *  @fail_fn: function to call if the original value was not 1
 *
 * Change the count from 1 to a value lower than 1, and call <fail_fn> if it
 * wasn't 1 originally. This function returns 0 if the fastpath succeeds,
 * or anything the slow path function returns
 */
static inline int
__mutex_fastpath_lock_retval(atomic_t *count, int (*fail_fn)(atomic_t *))
{
	if (unlikely(atomic_xchg(count, 0) != 1))
		if (likely(atomic_xchg(count, -1) != 1))
			return fail_fn(count);
	return 0;
}

/**
 *  __mutex_fastpath_unlock - try to promote the mutex from 0 to 1
 *  @count: pointer of type atomic_t
 *  @fail_fn: function to call if the original value was not 0
 *
 * try to promote the mutex from 0 to 1. if it wasn't 0, call <function>
 * In the failure case, this function is allowed to either set the value to
 * 1, or to set it to a value lower than one.
 * If the implementation sets it to a value of lower than one, the
 * __mutex_slowpath_needs_to_unlock() macro needs to return 1, it needs
 * to return 0 otherwise.
 */
static inline void
__mutex_fastpath_unlock(atomic_t *count, void (*fail_fn)(atomic_t *))
{
	if (unlikely(atomic_xchg(count, 1) != 0))
		fail_fn(count);
}

#define __mutex_slowpath_needs_to_unlock()		0

/**
 * __mutex_fastpath_trylock - try to acquire the mutex, without waiting
 *
 *  @count: pointer of type atomic_t
 *  @fail_fn: spinlock based trylock implementation
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
	int prev = atomic_xchg(count, 0);

	if (unlikely(prev < 0)) {
		/*
		 * The lock was marked contended so we must restore that
		 * state. If while doing so we get back a prev value of 1
		 * then we just own it.
		 *
		 * [ In the rare case of the mutex going to 1, to 0, to -1
		 *   and then back to 0 in this few-instructions window,
		 *   this has the potential to trigger the slowpath for the
		 *   owner's unlock path needlessly, but that's not a problem
		 *   in practice. ]
		 */
		prev = atomic_xchg(count, prev);
		if (prev < 0)
			prev = 0;
	}

	return prev;
}

#endif
