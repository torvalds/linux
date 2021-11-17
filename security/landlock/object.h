/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Landlock LSM - Object management
 *
 * Copyright © 2016-2020 Mickaël Salaün <mic@digikod.net>
 * Copyright © 2018-2020 ANSSI
 */

#ifndef _SECURITY_LANDLOCK_OBJECT_H
#define _SECURITY_LANDLOCK_OBJECT_H

#include <linux/compiler_types.h>
#include <linux/refcount.h>
#include <linux/spinlock.h>

struct landlock_object;

/**
 * struct landlock_object_underops - Operations on an underlying object
 */
struct landlock_object_underops {
	/**
	 * @release: Releases the underlying object (e.g. iput() for an inode).
	 */
	void (*release)(struct landlock_object *const object)
		__releases(object->lock);
};

/**
 * struct landlock_object - Security blob tied to a kernel object
 *
 * The goal of this structure is to enable to tie a set of ephemeral access
 * rights (pertaining to different domains) to a kernel object (e.g an inode)
 * in a safe way.  This implies to handle concurrent use and modification.
 *
 * The lifetime of a &struct landlock_object depends on the rules referring to
 * it.
 */
struct landlock_object {
	/**
	 * @usage: This counter is used to tie an object to the rules matching
	 * it or to keep it alive while adding a new rule.  If this counter
	 * reaches zero, this struct must not be modified, but this counter can
	 * still be read from within an RCU read-side critical section.  When
	 * adding a new rule to an object with a usage counter of zero, we must
	 * wait until the pointer to this object is set to NULL (or recycled).
	 */
	refcount_t usage;
	/**
	 * @lock: Protects against concurrent modifications.  This lock must be
	 * held from the time @usage drops to zero until any weak references
	 * from @underobj to this object have been cleaned up.
	 *
	 * Lock ordering: inode->i_lock nests inside this.
	 */
	spinlock_t lock;
	/**
	 * @underobj: Used when cleaning up an object and to mark an object as
	 * tied to its underlying kernel structure.  This pointer is protected
	 * by @lock.  Cf. landlock_release_inodes() and release_inode().
	 */
	void *underobj;
	union {
		/**
		 * @rcu_free: Enables lockless use of @usage, @lock and
		 * @underobj from within an RCU read-side critical section.
		 * @rcu_free and @underops are only used by
		 * landlock_put_object().
		 */
		struct rcu_head rcu_free;
		/**
		 * @underops: Enables landlock_put_object() to release the
		 * underlying object (e.g. inode).
		 */
		const struct landlock_object_underops *underops;
	};
};

struct landlock_object *landlock_create_object(
		const struct landlock_object_underops *const underops,
		void *const underobj);

void landlock_put_object(struct landlock_object *const object);

static inline void landlock_get_object(struct landlock_object *const object)
{
	if (object)
		refcount_inc(&object->usage);
}

#endif /* _SECURITY_LANDLOCK_OBJECT_H */
