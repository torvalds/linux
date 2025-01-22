/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * kref.h - library routines for handling generic reference counted objects
 *
 * Copyright (C) 2004 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (C) 2004 IBM Corp.
 *
 * based on kobject.h which was:
 * Copyright (C) 2002-2003 Patrick Mochel <mochel@osdl.org>
 * Copyright (C) 2002-2003 Open Source Development Labs
 */

#ifndef _KREF_H_
#define _KREF_H_

#include <linux/spinlock.h>
#include <linux/refcount.h>

struct kref {
	refcount_t refcount;
};

#define KREF_INIT(n)	{ .refcount = REFCOUNT_INIT(n), }

/**
 * kref_init - initialize object.
 * @kref: object in question.
 */
static inline void kref_init(struct kref *kref)
{
	refcount_set(&kref->refcount, 1);
}

static inline unsigned int kref_read(const struct kref *kref)
{
	return refcount_read(&kref->refcount);
}

/**
 * kref_get - increment refcount for object.
 * @kref: object.
 */
static inline void kref_get(struct kref *kref)
{
	refcount_inc(&kref->refcount);
}

/**
 * kref_put - Decrement refcount for object
 * @kref: Object
 * @release: Pointer to the function that will clean up the object when the
 *	     last reference to the object is released.
 *
 * Decrement the refcount, and if 0, call @release.  The caller may not
 * pass NULL or kfree() as the release function.
 *
 * Return: 1 if this call removed the object, otherwise return 0.  Beware,
 * if this function returns 0, another caller may have removed the object
 * by the time this function returns.  The return value is only certain
 * if you want to see if the object is definitely released.
 */
static inline int kref_put(struct kref *kref, void (*release)(struct kref *kref))
{
	if (refcount_dec_and_test(&kref->refcount)) {
		release(kref);
		return 1;
	}
	return 0;
}

/**
 * kref_put_mutex - Decrement refcount for object
 * @kref: Object
 * @release: Pointer to the function that will clean up the object when the
 *	     last reference to the object is released.
 * @mutex: Mutex which protects the release function.
 *
 * This variant of kref_lock() calls the @release function with the @mutex
 * held.  The @release function will release the mutex.
 */
static inline int kref_put_mutex(struct kref *kref,
				 void (*release)(struct kref *kref),
				 struct mutex *mutex)
{
	if (refcount_dec_and_mutex_lock(&kref->refcount, mutex)) {
		release(kref);
		return 1;
	}
	return 0;
}

/**
 * kref_put_lock - Decrement refcount for object
 * @kref: Object
 * @release: Pointer to the function that will clean up the object when the
 *	     last reference to the object is released.
 * @lock: Spinlock which protects the release function.
 *
 * This variant of kref_lock() calls the @release function with the @lock
 * held.  The @release function will release the lock.
 */
static inline int kref_put_lock(struct kref *kref,
				void (*release)(struct kref *kref),
				spinlock_t *lock)
{
	if (refcount_dec_and_lock(&kref->refcount, lock)) {
		release(kref);
		return 1;
	}
	return 0;
}

/**
 * kref_get_unless_zero - Increment refcount for object unless it is zero.
 * @kref: object.
 *
 * This function is intended to simplify locking around refcounting for
 * objects that can be looked up from a lookup structure, and which are
 * removed from that lookup structure in the object destructor.
 * Operations on such objects require at least a read lock around
 * lookup + kref_get, and a write lock around kref_put + remove from lookup
 * structure. Furthermore, RCU implementations become extremely tricky.
 * With a lookup followed by a kref_get_unless_zero *with return value check*
 * locking in the kref_put path can be deferred to the actual removal from
 * the lookup structure and RCU lookups become trivial.
 *
 * Return: non-zero if the increment succeeded. Otherwise return 0.
 */
static inline int __must_check kref_get_unless_zero(struct kref *kref)
{
	return refcount_inc_not_zero(&kref->refcount);
}
#endif /* _KREF_H_ */
