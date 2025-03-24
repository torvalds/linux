// SPDX-License-Identifier: GPL-2.0-only

/*
 * rcuref - A scalable reference count implementation for RCU managed objects
 *
 * rcuref is provided to replace open coded reference count implementations
 * based on atomic_t. It protects explicitely RCU managed objects which can
 * be visible even after the last reference has been dropped and the object
 * is heading towards destruction.
 *
 * A common usage pattern is:
 *
 * get()
 *	rcu_read_lock();
 *	p = get_ptr();
 *	if (p && !atomic_inc_not_zero(&p->refcnt))
 *		p = NULL;
 *	rcu_read_unlock();
 *	return p;
 *
 * put()
 *	if (!atomic_dec_return(&->refcnt)) {
 *		remove_ptr(p);
 *		kfree_rcu((p, rcu);
 *	}
 *
 * atomic_inc_not_zero() is implemented with a try_cmpxchg() loop which has
 * O(N^2) behaviour under contention with N concurrent operations.
 *
 * rcuref uses atomic_add_negative_relaxed() for the fast path, which scales
 * better under contention.
 *
 * Why not refcount?
 * =================
 *
 * In principle it should be possible to make refcount use the rcuref
 * scheme, but the destruction race described below cannot be prevented
 * unless the protected object is RCU managed.
 *
 * Theory of operation
 * ===================
 *
 * rcuref uses an unsigned integer reference counter. As long as the
 * counter value is greater than or equal to RCUREF_ONEREF and not larger
 * than RCUREF_MAXREF the reference is alive:
 *
 * ONEREF   MAXREF               SATURATED             RELEASED      DEAD    NOREF
 * 0        0x7FFFFFFF 0x8000000 0xA0000000 0xBFFFFFFF 0xC0000000 0xE0000000 0xFFFFFFFF
 * <---valid --------> <-------saturation zone-------> <-----dead zone----->
 *
 * The get() and put() operations do unconditional increments and
 * decrements. The result is checked after the operation. This optimizes
 * for the fast path.
 *
 * If the reference count is saturated or dead, then the increments and
 * decrements are not harmful as the reference count still stays in the
 * respective zones and is always set back to STATURATED resp. DEAD. The
 * zones have room for 2^28 racing operations in each direction, which
 * makes it practically impossible to escape the zones.
 *
 * Once the last reference is dropped the reference count becomes
 * RCUREF_NOREF which forces rcuref_put() into the slowpath operation. The
 * slowpath then tries to set the reference count from RCUREF_NOREF to
 * RCUREF_DEAD via a cmpxchg(). This opens a small window where a
 * concurrent rcuref_get() can acquire the reference count and bring it
 * back to RCUREF_ONEREF or even drop the reference again and mark it DEAD.
 *
 * If the cmpxchg() succeeds then a concurrent rcuref_get() will result in
 * DEAD + 1, which is inside the dead zone. If that happens the reference
 * count is put back to DEAD.
 *
 * The actual race is possible due to the unconditional increment and
 * decrements in rcuref_get() and rcuref_put():
 *
 *	T1				T2
 *	get()				put()
 *					if (atomic_add_negative(-1, &ref->refcnt))
 *		succeeds->			atomic_cmpxchg(&ref->refcnt, NOREF, DEAD);
 *
 *	atomic_add_negative(1, &ref->refcnt);	<- Elevates refcount to DEAD + 1
 *
 * As the result of T1's add is negative, the get() goes into the slow path
 * and observes refcnt being in the dead zone which makes the operation fail.
 *
 * Possible critical states:
 *
 *	Context Counter	References	Operation
 *	T1	0	1		init()
 *	T2	1	2		get()
 *	T1	0	1		put()
 *	T2     -1	0		put() tries to mark dead
 *	T1	0	1		get()
 *	T2	0	1		put() mark dead fails
 *	T1     -1	0		put() tries to mark dead
 *	T1    DEAD	0		put() mark dead succeeds
 *	T2    DEAD+1	0		get() fails and puts it back to DEAD
 *
 * Of course there are more complex scenarios, but the above illustrates
 * the working principle. The rest is left to the imagination of the
 * reader.
 *
 * Deconstruction race
 * ===================
 *
 * The release operation must be protected by prohibiting a grace period in
 * order to prevent a possible use after free:
 *
 *	T1				T2
 *	put()				get()
 *	// ref->refcnt = ONEREF
 *	if (!atomic_add_negative(-1, &ref->refcnt))
 *		return false;				<- Not taken
 *
 *	// ref->refcnt == NOREF
 *	--> preemption
 *					// Elevates ref->refcnt to ONEREF
 *					if (!atomic_add_negative(1, &ref->refcnt))
 *						return true;			<- taken
 *
 *					if (put(&p->ref)) { <-- Succeeds
 *						remove_pointer(p);
 *						kfree_rcu(p, rcu);
 *					}
 *
 *		RCU grace period ends, object is freed
 *
 *	atomic_cmpxchg(&ref->refcnt, NOREF, DEAD);	<- UAF
 *
 * This is prevented by disabling preemption around the put() operation as
 * that's in most kernel configurations cheaper than a rcu_read_lock() /
 * rcu_read_unlock() pair and in many cases even a NOOP. In any case it
 * prevents the grace period which keeps the object alive until all put()
 * operations complete.
 *
 * Saturation protection
 * =====================
 *
 * The reference count has a saturation limit RCUREF_MAXREF (INT_MAX).
 * Once this is exceedded the reference count becomes stale by setting it
 * to RCUREF_SATURATED, which will cause a memory leak, but it prevents
 * wrap arounds which obviously cause worse problems than a memory
 * leak. When saturation is reached a warning is emitted.
 *
 * Race conditions
 * ===============
 *
 * All reference count increment/decrement operations are unconditional and
 * only verified after the fact. This optimizes for the good case and takes
 * the occasional race vs. a dead or already saturated refcount into
 * account. The saturation and dead zones are large enough to accomodate
 * for that.
 *
 * Memory ordering
 * ===============
 *
 * Memory ordering rules are slightly relaxed wrt regular atomic_t functions
 * and provide only what is strictly required for refcounts.
 *
 * The increments are fully relaxed; these will not provide ordering. The
 * rationale is that whatever is used to obtain the object to increase the
 * reference count on will provide the ordering. For locked data
 * structures, its the lock acquire, for RCU/lockless data structures its
 * the dependent load.
 *
 * rcuref_get() provides a control dependency ordering future stores which
 * ensures that the object is not modified when acquiring a reference
 * fails.
 *
 * rcuref_put() provides release order, i.e. all prior loads and stores
 * will be issued before. It also provides a control dependency ordering
 * against the subsequent destruction of the object.
 *
 * If rcuref_put() successfully dropped the last reference and marked the
 * object DEAD it also provides acquire ordering.
 */

#include <linux/export.h>
#include <linux/rcuref.h>

/**
 * rcuref_get_slowpath - Slowpath of rcuref_get()
 * @ref:	Pointer to the reference count
 *
 * Invoked when the reference count is outside of the valid zone.
 *
 * Return:
 *	False if the reference count was already marked dead
 *
 *	True if the reference count is saturated, which prevents the
 *	object from being deconstructed ever.
 */
bool rcuref_get_slowpath(rcuref_t *ref)
{
	unsigned int cnt = atomic_read(&ref->refcnt);

	/*
	 * If the reference count was already marked dead, undo the
	 * increment so it stays in the middle of the dead zone and return
	 * fail.
	 */
	if (cnt >= RCUREF_RELEASED) {
		atomic_set(&ref->refcnt, RCUREF_DEAD);
		return false;
	}

	/*
	 * If it was saturated, warn and mark it so. In case the increment
	 * was already on a saturated value restore the saturation
	 * marker. This keeps it in the middle of the saturation zone and
	 * prevents the reference count from overflowing. This leaks the
	 * object memory, but prevents the obvious reference count overflow
	 * damage.
	 */
	if (WARN_ONCE(cnt > RCUREF_MAXREF, "rcuref saturated - leaking memory"))
		atomic_set(&ref->refcnt, RCUREF_SATURATED);
	return true;
}
EXPORT_SYMBOL_GPL(rcuref_get_slowpath);

/**
 * rcuref_put_slowpath - Slowpath of __rcuref_put()
 * @ref:	Pointer to the reference count
 * @cnt:	The resulting value of the fastpath decrement
 *
 * Invoked when the reference count is outside of the valid zone.
 *
 * Return:
 *	True if this was the last reference with no future references
 *	possible. This signals the caller that it can safely schedule the
 *	object, which is protected by the reference counter, for
 *	deconstruction.
 *
 *	False if there are still active references or the put() raced
 *	with a concurrent get()/put() pair. Caller is not allowed to
 *	deconstruct the protected object.
 */
bool rcuref_put_slowpath(rcuref_t *ref, unsigned int cnt)
{
	/* Did this drop the last reference? */
	if (likely(cnt == RCUREF_NOREF)) {
		/*
		 * Carefully try to set the reference count to RCUREF_DEAD.
		 *
		 * This can fail if a concurrent get() operation has
		 * elevated it again or the corresponding put() even marked
		 * it dead already. Both are valid situations and do not
		 * require a retry. If this fails the caller is not
		 * allowed to deconstruct the object.
		 */
		if (!atomic_try_cmpxchg_release(&ref->refcnt, &cnt, RCUREF_DEAD))
			return false;

		/*
		 * The caller can safely schedule the object for
		 * deconstruction. Provide acquire ordering.
		 */
		smp_acquire__after_ctrl_dep();
		return true;
	}

	/*
	 * If the reference count was already in the dead zone, then this
	 * put() operation is imbalanced. Warn, put the reference count back to
	 * DEAD and tell the caller to not deconstruct the object.
	 */
	if (WARN_ONCE(cnt >= RCUREF_RELEASED, "rcuref - imbalanced put()")) {
		atomic_set(&ref->refcnt, RCUREF_DEAD);
		return false;
	}

	/*
	 * This is a put() operation on a saturated refcount. Restore the
	 * mean saturation value and tell the caller to not deconstruct the
	 * object.
	 */
	if (cnt > RCUREF_MAXREF)
		atomic_set(&ref->refcnt, RCUREF_SATURATED);
	return false;
}
EXPORT_SYMBOL_GPL(rcuref_put_slowpath);
