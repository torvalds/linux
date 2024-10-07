/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_FILE_REF_H
#define _LINUX_FILE_REF_H

#include <linux/atomic.h>
#include <linux/preempt.h>
#include <linux/types.h>

/*
 * file_ref is a reference count implementation specifically for use by
 * files. It takes inspiration from rcuref but differs in key aspects
 * such as support for SLAB_TYPESAFE_BY_RCU type caches.
 *
 * FILE_REF_ONEREF                FILE_REF_MAXREF
 * 0x0000000000000000UL      0x7FFFFFFFFFFFFFFFUL
 * <-------------------valid ------------------->
 *
 *                       FILE_REF_SATURATED
 * 0x8000000000000000UL 0xA000000000000000UL 0xBFFFFFFFFFFFFFFFUL
 * <-----------------------saturation zone---------------------->
 *
 * FILE_REF_RELEASED                   FILE_REF_DEAD
 * 0xC000000000000000UL         0xE000000000000000UL
 * <-------------------dead zone------------------->
 *
 * FILE_REF_NOREF
 * 0xFFFFFFFFFFFFFFFFUL
 */

#ifdef CONFIG_64BIT
#define FILE_REF_ONEREF		0x0000000000000000UL
#define FILE_REF_MAXREF		0x7FFFFFFFFFFFFFFFUL
#define FILE_REF_SATURATED	0xA000000000000000UL
#define FILE_REF_RELEASED	0xC000000000000000UL
#define FILE_REF_DEAD		0xE000000000000000UL
#define FILE_REF_NOREF		0xFFFFFFFFFFFFFFFFUL
#else
#define FILE_REF_ONEREF		0x00000000U
#define FILE_REF_MAXREF		0x7FFFFFFFU
#define FILE_REF_SATURATED	0xA0000000U
#define FILE_REF_RELEASED	0xC0000000U
#define FILE_REF_DEAD		0xE0000000U
#define FILE_REF_NOREF		0xFFFFFFFFU
#endif

typedef struct {
#ifdef CONFIG_64BIT
	atomic64_t refcnt;
#else
	atomic_t refcnt;
#endif
} file_ref_t;

/**
 * file_ref_init - Initialize a file reference count
 * @ref: Pointer to the reference count
 * @cnt: The initial reference count typically '1'
 */
static inline void file_ref_init(file_ref_t *ref, unsigned long cnt)
{
	atomic_long_set(&ref->refcnt, cnt - 1);
}

bool __file_ref_put(file_ref_t *ref, unsigned long cnt);

/**
 * file_ref_get - Acquire one reference on a file
 * @ref: Pointer to the reference count
 *
 * Similar to atomic_inc_not_zero() but saturates at FILE_REF_MAXREF.
 *
 * Provides full memory ordering.
 *
 * Return: False if the attempt to acquire a reference failed. This happens
 *         when the last reference has been put already. True if a reference
 *         was successfully acquired
 */
static __always_inline __must_check bool file_ref_get(file_ref_t *ref)
{
	/*
	 * Unconditionally increase the reference count with full
	 * ordering. The saturation and dead zones provide enough
	 * tolerance for this.
	 *
	 * If this indicates negative the file in question the fail can
	 * be freed and immediately reused due to SLAB_TYPSAFE_BY_RCU.
	 * Hence, unconditionally altering the file reference count to
	 * e.g., reset the file reference count back to the middle of
	 * the deadzone risk end up marking someone else's file as dead
	 * behind their back.
	 *
	 * It would be possible to do a careful:
	 *
	 * cnt = atomic_long_inc_return();
	 * if (likely(cnt >= 0))
	 *	return true;
	 *
	 * and then something like:
	 *
	 * if (cnt >= FILE_REF_RELEASE)
	 *	atomic_long_try_cmpxchg(&ref->refcnt, &cnt, FILE_REF_DEAD),
	 *
	 * to set the value back to the middle of the deadzone. But it's
	 * practically impossible to go from FILE_REF_DEAD to
	 * FILE_REF_ONEREF. It would need 2305843009213693952/2^61
	 * file_ref_get()s to resurrect such a dead file.
	 */
	return !atomic_long_add_negative(1, &ref->refcnt);
}

/**
 * file_ref_inc - Acquire one reference on a file
 * @ref: Pointer to the reference count
 *
 * Acquire an additional reference on a file. Warns if the caller didn't
 * already hold a reference.
 */
static __always_inline void file_ref_inc(file_ref_t *ref)
{
	long prior = atomic_long_fetch_inc_relaxed(&ref->refcnt);
	WARN_ONCE(prior < 0, "file_ref_inc() on a released file reference");
}

/**
 * file_ref_put -- Release a file reference
 * @ref:	Pointer to the reference count
 *
 * Provides release memory ordering, such that prior loads and stores
 * are done before, and provides an acquire ordering on success such
 * that free() must come after.
 *
 * Return: True if this was the last reference with no future references
 *         possible. This signals the caller that it can safely release
 *         the object which is protected by the reference counter.
 *         False if there are still active references or the put() raced
 *         with a concurrent get()/put() pair. Caller is not allowed to
 *         release the protected object.
 */
static __always_inline __must_check bool file_ref_put(file_ref_t *ref)
{
	long cnt;

	/*
	 * While files are SLAB_TYPESAFE_BY_RCU and thus file_ref_put()
	 * calls don't risk UAFs when a file is recyclyed, it is still
	 * vulnerable to UAFs caused by freeing the whole slab page once
	 * it becomes unused. Prevent file_ref_put() from being
	 * preempted protects against this.
	 */
	guard(preempt)();
	/*
	 * Unconditionally decrease the reference count. The saturation
	 * and dead zones provide enough tolerance for this. If this
	 * fails then we need to handle the last reference drop and
	 * cases inside the saturation and dead zones.
	 */
	cnt = atomic_long_dec_return(&ref->refcnt);
	if (cnt >= 0)
		return false;
	return __file_ref_put(ref, cnt);
}

/**
 * file_ref_read - Read the number of file references
 * @ref: Pointer to the reference count
 *
 * Return: The number of held references (0 ... N)
 */
static inline unsigned long file_ref_read(file_ref_t *ref)
{
	unsigned long c = atomic_long_read(&ref->refcnt);

	/* Return 0 if within the DEAD zone. */
	return c >= FILE_REF_RELEASED ? 0 : c + 1;
}

#endif
