/*
 * kref.h - library routines for handling generic reference counted objects
 *
 * Copyright (C) 2004 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (C) 2004 IBM Corp.
 *
 * based on kobject.h which was:
 * Copyright (C) 2002-2003 Patrick Mochel <mochel@osdl.org>
 * Copyright (C) 2002-2003 Open Source Development Labs
 *
 * This file is released under the GPLv2.
 *
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
 * kref_put - decrement refcount for object.
 * @kref: object.
 * @release: pointer to the function that will clean up the object when the
 *	     last reference to the object is released.
 *	     This pointer is required, and it is not acceptable to pass kfree
 *	     in as this function.  If the caller does pass kfree to this
 *	     function, you will be publicly mocked mercilessly by the kref
 *	     maintainer, and anyone else who happens to notice it.  You have
 *	     been warned.
 *
 * Decrement the refcount, and if 0, call release().
 * Return 1 if the object was removed, otherwise return 0.  Beware, if this
 * function returns 0, you still can not count on the kref from remaining in
 * memory.  Only use the return value if you want to see if the kref is now
 * gone, not present.
 */
static inline int kref_put(struct kref *kref, void (*release)(struct kref *kref))
{
	WARN_ON(release == NULL);

	if (refcount_dec_and_test(&kref->refcount)) {
		release(kref);
		return 1;
	}
	return 0;
}

static inline int kref_put_mutex(struct kref *kref,
				 void (*release)(struct kref *kref),
				 struct mutex *lock)
{
	WARN_ON(release == NULL);

	if (refcount_dec_and_mutex_lock(&kref->refcount, lock)) {
		release(kref);
		return 1;
	}
	return 0;
}

static inline int kref_put_lock(struct kref *kref,
				void (*release)(struct kref *kref),
				spinlock_t *lock)
{
	WARN_ON(release == NULL);

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
 * Return non-zero if the increment succeeded. Otherwise return 0.
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
 */
static inline int __must_check kref_get_unless_zero(struct kref *kref)
{
	return refcount_inc_not_zero(&kref->refcount);
}
#endif /* _KREF_H_ */
