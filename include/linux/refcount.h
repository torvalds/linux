/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Variant of atomic_t specialized for reference counts.
 *
 * The interface matches the atomic_t interface (to aid in porting) but only
 * provides the few functions one should use for reference counting.
 *
 * Saturation semantics
 * ====================
 *
 * refcount_t differs from atomic_t in that the counter saturates at
 * REFCOUNT_SATURATED and will not move once there. This avoids wrapping the
 * counter and causing 'spurious' use-after-free issues. In order to avoid the
 * cost associated with introducing cmpxchg() loops into all of the saturating
 * operations, we temporarily allow the counter to take on an unchecked value
 * and then explicitly set it to REFCOUNT_SATURATED on detecting that underflow
 * or overflow has occurred. Although this is racy when multiple threads
 * access the refcount concurrently, by placing REFCOUNT_SATURATED roughly
 * equidistant from 0 and INT_MAX we minimise the scope for error:
 *
 * 	                           INT_MAX     REFCOUNT_SATURATED   UINT_MAX
 *   0                          (0x7fff_ffff)    (0xc000_0000)    (0xffff_ffff)
 *   +--------------------------------+----------------+----------------+
 *                                     <---------- bad value! ---------->
 *
 * (in a signed view of the world, the "bad value" range corresponds to
 * a negative counter value).
 *
 * As an example, consider a refcount_inc() operation that causes the counter
 * to overflow:
 *
 * 	int old = atomic_fetch_add_relaxed(r);
 *	// old is INT_MAX, refcount now INT_MIN (0x8000_0000)
 *	if (old < 0)
 *		atomic_set(r, REFCOUNT_SATURATED);
 *
 * If another thread also performs a refcount_inc() operation between the two
 * atomic operations, then the count will continue to edge closer to 0. If it
 * reaches a value of 1 before /any/ of the threads reset it to the saturated
 * value, then a concurrent refcount_dec_and_test() may erroneously free the
 * underlying object.
 * Linux limits the maximum number of tasks to PID_MAX_LIMIT, which is currently
 * 0x400000 (and can't easily be raised in the future beyond FUTEX_TID_MASK).
 * With the current PID limit, if no batched refcounting operations are used and
 * the attacker can't repeatedly trigger kernel oopses in the middle of refcount
 * operations, this makes it impossible for a saturated refcount to leave the
 * saturation range, even if it is possible for multiple uses of the same
 * refcount to nest in the context of a single task:
 *
 *     (UINT_MAX+1-REFCOUNT_SATURATED) / PID_MAX_LIMIT =
 *     0x40000000 / 0x400000 = 0x100 = 256
 *
 * If hundreds of references are added/removed with a single refcounting
 * operation, it may potentially be possible to leave the saturation range; but
 * given the precise timing details involved with the round-robin scheduling of
 * each thread manipulating the refcount and the need to hit the race multiple
 * times in succession, there doesn't appear to be a practical avenue of attack
 * even if using refcount_add() operations with larger increments.
 *
 * Memory ordering
 * ===============
 *
 * Memory ordering rules are slightly relaxed wrt regular atomic_t functions
 * and provide only what is strictly required for refcounts.
 *
 * The increments are fully relaxed; these will not provide ordering. The
 * rationale is that whatever is used to obtain the object we're increasing the
 * reference count on will provide the ordering. For locked data structures,
 * its the lock acquire, for RCU/lockless data structures its the dependent
 * load.
 *
 * Do note that inc_not_zero() provides a control dependency which will order
 * future stores against the inc, this ensures we'll never modify the object
 * if we did not in fact acquire a reference.
 *
 * The decrements will provide release order, such that all the prior loads and
 * stores will be issued before, it also provides a control dependency, which
 * will order us against the subsequent free().
 *
 * The control dependency is against the load of the cmpxchg (ll/sc) that
 * succeeded. This means the stores aren't fully ordered, but this is fine
 * because the 1->0 transition indicates no concurrency.
 *
 * Note that the allocator is responsible for ordering things between free()
 * and alloc().
 *
 * The decrements dec_and_test() and sub_and_test() also provide acquire
 * ordering on success.
 *
 * refcount_{add|inc}_not_zero_acquire() and refcount_set_release() provide
 * acquire and release ordering for cases when the memory occupied by the
 * object might be reused to store another object. This is important for the
 * cases where secondary validation is required to detect such reuse, e.g.
 * SLAB_TYPESAFE_BY_RCU. The secondary validation checks have to happen after
 * the refcount is taken, hence acquire order is necessary. Similarly, when the
 * object is initialized, all stores to its attributes should be visible before
 * the refcount is set, otherwise a stale attribute value might be used by
 * another task which succeeds in taking a refcount to the new object.
 */

#ifndef _LINUX_REFCOUNT_H
#define _LINUX_REFCOUNT_H

#include <linux/atomic.h>
#include <linux/bug.h>
#include <linux/compiler.h>
#include <linux/limits.h>
#include <linux/refcount_types.h>
#include <linux/spinlock_types.h>

struct mutex;

#define REFCOUNT_INIT(n)	{ .refs = ATOMIC_INIT(n), }
#define REFCOUNT_MAX		INT_MAX
#define REFCOUNT_SATURATED	(INT_MIN / 2)

enum refcount_saturation_type {
	REFCOUNT_ADD_NOT_ZERO_OVF,
	REFCOUNT_ADD_OVF,
	REFCOUNT_ADD_UAF,
	REFCOUNT_SUB_UAF,
	REFCOUNT_DEC_LEAK,
};

void refcount_warn_saturate(refcount_t *r, enum refcount_saturation_type t);

/**
 * refcount_set - set a refcount's value
 * @r: the refcount
 * @n: value to which the refcount will be set
 */
static inline void refcount_set(refcount_t *r, int n)
{
	atomic_set(&r->refs, n);
}

/**
 * refcount_set_release - set a refcount's value with release ordering
 * @r: the refcount
 * @n: value to which the refcount will be set
 *
 * This function should be used when memory occupied by the object might be
 * reused to store another object -- consider SLAB_TYPESAFE_BY_RCU.
 *
 * Provides release memory ordering which will order previous memory operations
 * against this store. This ensures all updates to this object are visible
 * once the refcount is set and stale values from the object previously
 * occupying this memory are overwritten with new ones.
 *
 * This function should be called only after new object is fully initialized.
 * After this call the object should be considered visible to other tasks even
 * if it was not yet added into an object collection normally used to discover
 * it. This is because other tasks might have discovered the object previously
 * occupying the same memory and after memory reuse they can succeed in taking
 * refcount to the new object and start using it.
 */
static inline void refcount_set_release(refcount_t *r, int n)
{
	atomic_set_release(&r->refs, n);
}

/**
 * refcount_read - get a refcount's value
 * @r: the refcount
 *
 * Return: the refcount's value
 */
static inline unsigned int refcount_read(const refcount_t *r)
{
	return atomic_read(&r->refs);
}

static inline __must_check __signed_wrap
bool __refcount_add_not_zero(int i, refcount_t *r, int *oldp)
{
	int old = refcount_read(r);

	do {
		if (!old)
			break;
	} while (!atomic_try_cmpxchg_relaxed(&r->refs, &old, old + i));

	if (oldp)
		*oldp = old;

	if (unlikely(old < 0 || old + i < 0))
		refcount_warn_saturate(r, REFCOUNT_ADD_NOT_ZERO_OVF);

	return old;
}

/**
 * refcount_add_not_zero - add a value to a refcount unless it is 0
 * @i: the value to add to the refcount
 * @r: the refcount
 *
 * Will saturate at REFCOUNT_SATURATED and WARN.
 *
 * Provides no memory ordering, it is assumed the caller has guaranteed the
 * object memory to be stable (RCU, etc.). It does provide a control dependency
 * and thereby orders future stores. See the comment on top.
 *
 * Use of this function is not recommended for the normal reference counting
 * use case in which references are taken and released one at a time.  In these
 * cases, refcount_inc(), or one of its variants, should instead be used to
 * increment a reference count.
 *
 * Return: false if the passed refcount is 0, true otherwise
 */
static inline __must_check bool refcount_add_not_zero(int i, refcount_t *r)
{
	return __refcount_add_not_zero(i, r, NULL);
}

static inline __must_check __signed_wrap
bool __refcount_add_not_zero_acquire(int i, refcount_t *r, int *oldp)
{
	int old = refcount_read(r);

	do {
		if (!old)
			break;
	} while (!atomic_try_cmpxchg_acquire(&r->refs, &old, old + i));

	if (oldp)
		*oldp = old;

	if (unlikely(old < 0 || old + i < 0))
		refcount_warn_saturate(r, REFCOUNT_ADD_NOT_ZERO_OVF);

	return old;
}

/**
 * refcount_add_not_zero_acquire - add a value to a refcount with acquire ordering unless it is 0
 *
 * @i: the value to add to the refcount
 * @r: the refcount
 *
 * Will saturate at REFCOUNT_SATURATED and WARN.
 *
 * This function should be used when memory occupied by the object might be
 * reused to store another object -- consider SLAB_TYPESAFE_BY_RCU.
 *
 * Provides acquire memory ordering on success, it is assumed the caller has
 * guaranteed the object memory to be stable (RCU, etc.). It does provide a
 * control dependency and thereby orders future stores. See the comment on top.
 *
 * Use of this function is not recommended for the normal reference counting
 * use case in which references are taken and released one at a time.  In these
 * cases, refcount_inc_not_zero_acquire() should instead be used to increment a
 * reference count.
 *
 * Return: false if the passed refcount is 0, true otherwise
 */
static inline __must_check bool refcount_add_not_zero_acquire(int i, refcount_t *r)
{
	return __refcount_add_not_zero_acquire(i, r, NULL);
}

static inline __signed_wrap
void __refcount_add(int i, refcount_t *r, int *oldp)
{
	int old = atomic_fetch_add_relaxed(i, &r->refs);

	if (oldp)
		*oldp = old;

	if (unlikely(!old))
		refcount_warn_saturate(r, REFCOUNT_ADD_UAF);
	else if (unlikely(old < 0 || old + i < 0))
		refcount_warn_saturate(r, REFCOUNT_ADD_OVF);
}

/**
 * refcount_add - add a value to a refcount
 * @i: the value to add to the refcount
 * @r: the refcount
 *
 * Similar to atomic_add(), but will saturate at REFCOUNT_SATURATED and WARN.
 *
 * Provides no memory ordering, it is assumed the caller has guaranteed the
 * object memory to be stable (RCU, etc.). It does provide a control dependency
 * and thereby orders future stores. See the comment on top.
 *
 * Use of this function is not recommended for the normal reference counting
 * use case in which references are taken and released one at a time.  In these
 * cases, refcount_inc(), or one of its variants, should instead be used to
 * increment a reference count.
 */
static inline void refcount_add(int i, refcount_t *r)
{
	__refcount_add(i, r, NULL);
}

static inline __must_check bool __refcount_inc_not_zero(refcount_t *r, int *oldp)
{
	return __refcount_add_not_zero(1, r, oldp);
}

/**
 * refcount_inc_not_zero - increment a refcount unless it is 0
 * @r: the refcount to increment
 *
 * Similar to atomic_inc_not_zero(), but will saturate at REFCOUNT_SATURATED
 * and WARN.
 *
 * Provides no memory ordering, it is assumed the caller has guaranteed the
 * object memory to be stable (RCU, etc.). It does provide a control dependency
 * and thereby orders future stores. See the comment on top.
 *
 * Return: true if the increment was successful, false otherwise
 */
static inline __must_check bool refcount_inc_not_zero(refcount_t *r)
{
	return __refcount_inc_not_zero(r, NULL);
}

static inline __must_check bool __refcount_inc_not_zero_acquire(refcount_t *r, int *oldp)
{
	return __refcount_add_not_zero_acquire(1, r, oldp);
}

/**
 * refcount_inc_not_zero_acquire - increment a refcount with acquire ordering unless it is 0
 * @r: the refcount to increment
 *
 * Similar to refcount_inc_not_zero(), but provides acquire memory ordering on
 * success.
 *
 * This function should be used when memory occupied by the object might be
 * reused to store another object -- consider SLAB_TYPESAFE_BY_RCU.
 *
 * Provides acquire memory ordering on success, it is assumed the caller has
 * guaranteed the object memory to be stable (RCU, etc.). It does provide a
 * control dependency and thereby orders future stores. See the comment on top.
 *
 * Return: true if the increment was successful, false otherwise
 */
static inline __must_check bool refcount_inc_not_zero_acquire(refcount_t *r)
{
	return __refcount_inc_not_zero_acquire(r, NULL);
}

static inline void __refcount_inc(refcount_t *r, int *oldp)
{
	__refcount_add(1, r, oldp);
}

/**
 * refcount_inc - increment a refcount
 * @r: the refcount to increment
 *
 * Similar to atomic_inc(), but will saturate at REFCOUNT_SATURATED and WARN.
 *
 * Provides no memory ordering, it is assumed the caller already has a
 * reference on the object.
 *
 * Will WARN if the refcount is 0, as this represents a possible use-after-free
 * condition.
 */
static inline void refcount_inc(refcount_t *r)
{
	__refcount_inc(r, NULL);
}

static inline __must_check __signed_wrap
bool __refcount_sub_and_test(int i, refcount_t *r, int *oldp)
{
	int old = atomic_fetch_sub_release(i, &r->refs);

	if (oldp)
		*oldp = old;

	if (old > 0 && old == i) {
		smp_acquire__after_ctrl_dep();
		return true;
	}

	if (unlikely(old <= 0 || old - i < 0))
		refcount_warn_saturate(r, REFCOUNT_SUB_UAF);

	return false;
}

/**
 * refcount_sub_and_test - subtract from a refcount and test if it is 0
 * @i: amount to subtract from the refcount
 * @r: the refcount
 *
 * Similar to atomic_dec_and_test(), but it will WARN, return false and
 * ultimately leak on underflow and will fail to decrement when saturated
 * at REFCOUNT_SATURATED.
 *
 * Provides release memory ordering, such that prior loads and stores are done
 * before, and provides an acquire ordering on success such that free()
 * must come after.
 *
 * Use of this function is not recommended for the normal reference counting
 * use case in which references are taken and released one at a time.  In these
 * cases, refcount_dec(), or one of its variants, should instead be used to
 * decrement a reference count.
 *
 * Return: true if the resulting refcount is 0, false otherwise
 */
static inline __must_check bool refcount_sub_and_test(int i, refcount_t *r)
{
	return __refcount_sub_and_test(i, r, NULL);
}

static inline __must_check bool __refcount_dec_and_test(refcount_t *r, int *oldp)
{
	return __refcount_sub_and_test(1, r, oldp);
}

/**
 * refcount_dec_and_test - decrement a refcount and test if it is 0
 * @r: the refcount
 *
 * Similar to atomic_dec_and_test(), it will WARN on underflow and fail to
 * decrement when saturated at REFCOUNT_SATURATED.
 *
 * Provides release memory ordering, such that prior loads and stores are done
 * before, and provides an acquire ordering on success such that free()
 * must come after.
 *
 * Return: true if the resulting refcount is 0, false otherwise
 */
static inline __must_check bool refcount_dec_and_test(refcount_t *r)
{
	return __refcount_dec_and_test(r, NULL);
}

static inline void __refcount_dec(refcount_t *r, int *oldp)
{
	int old = atomic_fetch_sub_release(1, &r->refs);

	if (oldp)
		*oldp = old;

	if (unlikely(old <= 1))
		refcount_warn_saturate(r, REFCOUNT_DEC_LEAK);
}

/**
 * refcount_dec - decrement a refcount
 * @r: the refcount
 *
 * Similar to atomic_dec(), it will WARN on underflow and fail to decrement
 * when saturated at REFCOUNT_SATURATED.
 *
 * Provides release memory ordering, such that prior loads and stores are done
 * before.
 */
static inline void refcount_dec(refcount_t *r)
{
	__refcount_dec(r, NULL);
}

extern __must_check bool refcount_dec_if_one(refcount_t *r);
extern __must_check bool refcount_dec_not_one(refcount_t *r);
extern __must_check bool refcount_dec_and_mutex_lock(refcount_t *r, struct mutex *lock) __cond_acquires(lock);
extern __must_check bool refcount_dec_and_lock(refcount_t *r, spinlock_t *lock) __cond_acquires(lock);
extern __must_check bool refcount_dec_and_lock_irqsave(refcount_t *r,
						       spinlock_t *lock,
						       unsigned long *flags) __cond_acquires(lock);
#endif /* _LINUX_REFCOUNT_H */
