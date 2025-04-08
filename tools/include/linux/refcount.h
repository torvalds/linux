/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TOOLS_LINUX_REFCOUNT_H
#define _TOOLS_LINUX_REFCOUNT_H

/*
 * Variant of atomic_t specialized for reference counts.
 *
 * The interface matches the atomic_t interface (to aid in porting) but only
 * provides the few functions one should use for reference counting.
 *
 * It differs in that the counter saturates at UINT_MAX and will not move once
 * there. This avoids wrapping the counter and causing 'spurious'
 * use-after-free issues.
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
 */

#include <linux/atomic.h>
#include <linux/kernel.h>

#ifdef NDEBUG
#define REFCOUNT_WARN(cond, str) (void)(cond)
#define __refcount_check
#else
#define REFCOUNT_WARN(cond, str) BUG_ON(cond)
#define __refcount_check	__must_check
#endif

typedef struct refcount_struct {
	atomic_t refs;
} refcount_t;

#define REFCOUNT_INIT(n)	{ .refs = ATOMIC_INIT(n), }

static inline void refcount_set(refcount_t *r, unsigned int n)
{
	atomic_set(&r->refs, n);
}

static inline void refcount_set_release(refcount_t *r, unsigned int n)
{
	atomic_set(&r->refs, n);
}

static inline unsigned int refcount_read(const refcount_t *r)
{
	return atomic_read(&r->refs);
}

/*
 * Similar to atomic_inc_not_zero(), will saturate at UINT_MAX and WARN.
 *
 * Provides no memory ordering, it is assumed the caller has guaranteed the
 * object memory to be stable (RCU, etc.). It does provide a control dependency
 * and thereby orders future stores. See the comment on top.
 */
static inline __refcount_check
bool refcount_inc_not_zero(refcount_t *r)
{
	unsigned int old, new, val = atomic_read(&r->refs);

	for (;;) {
		new = val + 1;

		if (!val)
			return false;

		if (unlikely(!new))
			return true;

		old = atomic_cmpxchg_relaxed(&r->refs, val, new);
		if (old == val)
			break;

		val = old;
	}

	REFCOUNT_WARN(new == UINT_MAX, "refcount_t: saturated; leaking memory.\n");

	return true;
}

/*
 * Similar to atomic_inc(), will saturate at UINT_MAX and WARN.
 *
 * Provides no memory ordering, it is assumed the caller already has a
 * reference on the object, will WARN when this is not so.
 */
static inline void refcount_inc(refcount_t *r)
{
	REFCOUNT_WARN(!refcount_inc_not_zero(r), "refcount_t: increment on 0; use-after-free.\n");
}

/*
 * Similar to atomic_dec_and_test(), it will WARN on underflow and fail to
 * decrement when saturated at UINT_MAX.
 *
 * Provides release memory ordering, such that prior loads and stores are done
 * before, and provides a control dependency such that free() must come after.
 * See the comment on top.
 */
static inline __refcount_check
bool refcount_sub_and_test(unsigned int i, refcount_t *r)
{
	unsigned int old, new, val = atomic_read(&r->refs);

	for (;;) {
		if (unlikely(val == UINT_MAX))
			return false;

		new = val - i;
		if (new > val) {
			REFCOUNT_WARN(new > val, "refcount_t: underflow; use-after-free.\n");
			return false;
		}

		old = atomic_cmpxchg_release(&r->refs, val, new);
		if (old == val)
			break;

		val = old;
	}

	return !new;
}

static inline __refcount_check
bool refcount_dec_and_test(refcount_t *r)
{
	return refcount_sub_and_test(1, r);
}


#endif /* _ATOMIC_LINUX_REFCOUNT_H */
