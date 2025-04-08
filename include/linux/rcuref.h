/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_RCUREF_H
#define _LINUX_RCUREF_H

#include <linux/atomic.h>
#include <linux/bug.h>
#include <linux/limits.h>
#include <linux/lockdep.h>
#include <linux/preempt.h>
#include <linux/rcupdate.h>

#define RCUREF_ONEREF		0x00000000U
#define RCUREF_MAXREF		0x7FFFFFFFU
#define RCUREF_SATURATED	0xA0000000U
#define RCUREF_RELEASED		0xC0000000U
#define RCUREF_DEAD		0xE0000000U
#define RCUREF_NOREF		0xFFFFFFFFU

/**
 * rcuref_init - Initialize a rcuref reference count with the given reference count
 * @ref:	Pointer to the reference count
 * @cnt:	The initial reference count typically '1'
 */
static inline void rcuref_init(rcuref_t *ref, unsigned int cnt)
{
	atomic_set(&ref->refcnt, cnt - 1);
}

/**
 * rcuref_read - Read the number of held reference counts of a rcuref
 * @ref:	Pointer to the reference count
 *
 * Return: The number of held references (0 ... N)
 */
static inline unsigned int rcuref_read(rcuref_t *ref)
{
	unsigned int c = atomic_read(&ref->refcnt);

	/* Return 0 if within the DEAD zone. */
	return c >= RCUREF_RELEASED ? 0 : c + 1;
}

extern __must_check bool rcuref_get_slowpath(rcuref_t *ref);

/**
 * rcuref_get - Acquire one reference on a rcuref reference count
 * @ref:	Pointer to the reference count
 *
 * Similar to atomic_inc_not_zero() but saturates at RCUREF_MAXREF.
 *
 * Provides no memory ordering, it is assumed the caller has guaranteed the
 * object memory to be stable (RCU, etc.). It does provide a control dependency
 * and thereby orders future stores. See documentation in lib/rcuref.c
 *
 * Return:
 *	False if the attempt to acquire a reference failed. This happens
 *	when the last reference has been put already
 *
 *	True if a reference was successfully acquired
 */
static inline __must_check bool rcuref_get(rcuref_t *ref)
{
	/*
	 * Unconditionally increase the reference count. The saturation and
	 * dead zones provide enough tolerance for this.
	 */
	if (likely(!atomic_add_negative_relaxed(1, &ref->refcnt)))
		return true;

	/* Handle the cases inside the saturation and dead zones */
	return rcuref_get_slowpath(ref);
}

extern __must_check bool rcuref_put_slowpath(rcuref_t *ref, unsigned int cnt);

/*
 * Internal helper. Do not invoke directly.
 */
static __always_inline __must_check bool __rcuref_put(rcuref_t *ref)
{
	int cnt;

	RCU_LOCKDEP_WARN(!rcu_read_lock_held() && preemptible(),
			 "suspicious rcuref_put_rcusafe() usage");
	/*
	 * Unconditionally decrease the reference count. The saturation and
	 * dead zones provide enough tolerance for this.
	 */
	cnt = atomic_sub_return_release(1, &ref->refcnt);
	if (likely(cnt >= 0))
		return false;

	/*
	 * Handle the last reference drop and cases inside the saturation
	 * and dead zones.
	 */
	return rcuref_put_slowpath(ref, cnt);
}

/**
 * rcuref_put_rcusafe -- Release one reference for a rcuref reference count RCU safe
 * @ref:	Pointer to the reference count
 *
 * Provides release memory ordering, such that prior loads and stores are done
 * before, and provides an acquire ordering on success such that free()
 * must come after.
 *
 * Can be invoked from contexts, which guarantee that no grace period can
 * happen which would free the object concurrently if the decrement drops
 * the last reference and the slowpath races against a concurrent get() and
 * put() pair. rcu_read_lock()'ed and atomic contexts qualify.
 *
 * Return:
 *	True if this was the last reference with no future references
 *	possible. This signals the caller that it can safely release the
 *	object which is protected by the reference counter.
 *
 *	False if there are still active references or the put() raced
 *	with a concurrent get()/put() pair. Caller is not allowed to
 *	release the protected object.
 */
static inline __must_check bool rcuref_put_rcusafe(rcuref_t *ref)
{
	return __rcuref_put(ref);
}

/**
 * rcuref_put -- Release one reference for a rcuref reference count
 * @ref:	Pointer to the reference count
 *
 * Can be invoked from any context.
 *
 * Provides release memory ordering, such that prior loads and stores are done
 * before, and provides an acquire ordering on success such that free()
 * must come after.
 *
 * Return:
 *
 *	True if this was the last reference with no future references
 *	possible. This signals the caller that it can safely schedule the
 *	object, which is protected by the reference counter, for
 *	deconstruction.
 *
 *	False if there are still active references or the put() raced
 *	with a concurrent get()/put() pair. Caller is not allowed to
 *	deconstruct the protected object.
 */
static inline __must_check bool rcuref_put(rcuref_t *ref)
{
	bool released;

	preempt_disable();
	released = __rcuref_put(ref);
	preempt_enable();
	return released;
}

#endif
