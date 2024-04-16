// SPDX-License-Identifier: GPL-2.0-only
/*
 * Landlock LSM - Object management
 *
 * Copyright © 2016-2020 Mickaël Salaün <mic@digikod.net>
 * Copyright © 2018-2020 ANSSI
 */

#include <linux/bug.h>
#include <linux/compiler_types.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/rcupdate.h>
#include <linux/refcount.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "object.h"

struct landlock_object *
landlock_create_object(const struct landlock_object_underops *const underops,
		       void *const underobj)
{
	struct landlock_object *new_object;

	if (WARN_ON_ONCE(!underops || !underobj))
		return ERR_PTR(-ENOENT);
	new_object = kzalloc(sizeof(*new_object), GFP_KERNEL_ACCOUNT);
	if (!new_object)
		return ERR_PTR(-ENOMEM);
	refcount_set(&new_object->usage, 1);
	spin_lock_init(&new_object->lock);
	new_object->underops = underops;
	new_object->underobj = underobj;
	return new_object;
}

/*
 * The caller must own the object (i.e. thanks to object->usage) to safely put
 * it.
 */
void landlock_put_object(struct landlock_object *const object)
{
	/*
	 * The call to @object->underops->release(object) might sleep, e.g.
	 * because of iput().
	 */
	might_sleep();
	if (!object)
		return;

	/*
	 * If the @object's refcount cannot drop to zero, we can just decrement
	 * the refcount without holding a lock. Otherwise, the decrement must
	 * happen under @object->lock for synchronization with things like
	 * get_inode_object().
	 */
	if (refcount_dec_and_lock(&object->usage, &object->lock)) {
		__acquire(&object->lock);
		/*
		 * With @object->lock initially held, remove the reference from
		 * @object->underobj to @object (if it still exists).
		 */
		object->underops->release(object);
		kfree_rcu(object, rcu_free);
	}
}
