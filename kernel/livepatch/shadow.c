/*
 * shadow.c - Shadow Variables
 *
 * Copyright (C) 2014 Josh Poimboeuf <jpoimboe@redhat.com>
 * Copyright (C) 2014 Seth Jennings <sjenning@redhat.com>
 * Copyright (C) 2017 Joe Lawrence <joe.lawrence@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/**
 * DOC: Shadow variable API concurrency notes:
 *
 * The shadow variable API provides a simple relationship between an
 * <obj, id> pair and a pointer value.  It is the responsibility of the
 * caller to provide any mutual exclusion required of the shadow data.
 *
 * Once a shadow variable is attached to its parent object via the
 * klp_shadow_*alloc() API calls, it is considered live: any subsequent
 * call to klp_shadow_get() may then return the shadow variable's data
 * pointer.  Callers of klp_shadow_*alloc() should prepare shadow data
 * accordingly.
 *
 * The klp_shadow_*alloc() API calls may allocate memory for new shadow
 * variable structures.  Their implementation does not call kmalloc
 * inside any spinlocks, but API callers should pass GFP flags according
 * to their specific needs.
 *
 * The klp_shadow_hash is an RCU-enabled hashtable and is safe against
 * concurrent klp_shadow_free() and klp_shadow_get() operations.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/hashtable.h>
#include <linux/slab.h>
#include <linux/livepatch.h>

static DEFINE_HASHTABLE(klp_shadow_hash, 12);

/*
 * klp_shadow_lock provides exclusive access to the klp_shadow_hash and
 * the shadow variables it references.
 */
static DEFINE_SPINLOCK(klp_shadow_lock);

/**
 * struct klp_shadow - shadow variable structure
 * @node:	klp_shadow_hash hash table node
 * @rcu_head:	RCU is used to safely free this structure
 * @obj:	pointer to parent object
 * @id:		data identifier
 * @data:	data area
 */
struct klp_shadow {
	struct hlist_node node;
	struct rcu_head rcu_head;
	void *obj;
	unsigned long id;
	char data[];
};

/**
 * klp_shadow_match() - verify a shadow variable matches given <obj, id>
 * @shadow:	shadow variable to match
 * @obj:	pointer to parent object
 * @id:		data identifier
 *
 * Return: true if the shadow variable matches.
 */
static inline bool klp_shadow_match(struct klp_shadow *shadow, void *obj,
				unsigned long id)
{
	return shadow->obj == obj && shadow->id == id;
}

/**
 * klp_shadow_get() - retrieve a shadow variable data pointer
 * @obj:	pointer to parent object
 * @id:		data identifier
 *
 * Return: the shadow variable data element, NULL on failure.
 */
void *klp_shadow_get(void *obj, unsigned long id)
{
	struct klp_shadow *shadow;

	rcu_read_lock();

	hash_for_each_possible_rcu(klp_shadow_hash, shadow, node,
				   (unsigned long)obj) {

		if (klp_shadow_match(shadow, obj, id)) {
			rcu_read_unlock();
			return shadow->data;
		}
	}

	rcu_read_unlock();

	return NULL;
}
EXPORT_SYMBOL_GPL(klp_shadow_get);

static void *__klp_shadow_get_or_alloc(void *obj, unsigned long id,
				       size_t size, gfp_t gfp_flags,
				       klp_shadow_ctor_t ctor, void *ctor_data,
				       bool warn_on_exist)
{
	struct klp_shadow *new_shadow;
	void *shadow_data;
	unsigned long flags;

	/* Check if the shadow variable already exists */
	shadow_data = klp_shadow_get(obj, id);
	if (shadow_data)
		goto exists;

	/*
	 * Allocate a new shadow variable.  Fill it with zeroes by default.
	 * More complex setting can be done by @ctor function.  But it is
	 * called only when the buffer is really used (under klp_shadow_lock).
	 */
	new_shadow = kzalloc(size + sizeof(*new_shadow), gfp_flags);
	if (!new_shadow)
		return NULL;

	/* Look for <obj, id> again under the lock */
	spin_lock_irqsave(&klp_shadow_lock, flags);
	shadow_data = klp_shadow_get(obj, id);
	if (unlikely(shadow_data)) {
		/*
		 * Shadow variable was found, throw away speculative
		 * allocation.
		 */
		spin_unlock_irqrestore(&klp_shadow_lock, flags);
		kfree(new_shadow);
		goto exists;
	}

	new_shadow->obj = obj;
	new_shadow->id = id;

	if (ctor) {
		int err;

		err = ctor(obj, new_shadow->data, ctor_data);
		if (err) {
			spin_unlock_irqrestore(&klp_shadow_lock, flags);
			kfree(new_shadow);
			pr_err("Failed to construct shadow variable <%p, %lx> (%d)\n",
			       obj, id, err);
			return NULL;
		}
	}

	/* No <obj, id> found, so attach the newly allocated one */
	hash_add_rcu(klp_shadow_hash, &new_shadow->node,
		     (unsigned long)new_shadow->obj);
	spin_unlock_irqrestore(&klp_shadow_lock, flags);

	return new_shadow->data;

exists:
	if (warn_on_exist) {
		WARN(1, "Duplicate shadow variable <%p, %lx>\n", obj, id);
		return NULL;
	}

	return shadow_data;
}

/**
 * klp_shadow_alloc() - allocate and add a new shadow variable
 * @obj:	pointer to parent object
 * @id:		data identifier
 * @size:	size of attached data
 * @gfp_flags:	GFP mask for allocation
 * @ctor:	custom constructor to initialize the shadow data (optional)
 * @ctor_data:	pointer to any data needed by @ctor (optional)
 *
 * Allocates @size bytes for new shadow variable data using @gfp_flags.
 * The data are zeroed by default.  They are further initialized by @ctor
 * function if it is not NULL.  The new shadow variable is then added
 * to the global hashtable.
 *
 * If an existing <obj, id> shadow variable can be found, this routine will
 * issue a WARN, exit early and return NULL.
 *
 * This function guarantees that the constructor function is called only when
 * the variable did not exist before.  The cost is that @ctor is called
 * in atomic context under a spin lock.
 *
 * Return: the shadow variable data element, NULL on duplicate or
 * failure.
 */
void *klp_shadow_alloc(void *obj, unsigned long id,
		       size_t size, gfp_t gfp_flags,
		       klp_shadow_ctor_t ctor, void *ctor_data)
{
	return __klp_shadow_get_or_alloc(obj, id, size, gfp_flags,
					 ctor, ctor_data, true);
}
EXPORT_SYMBOL_GPL(klp_shadow_alloc);

/**
 * klp_shadow_get_or_alloc() - get existing or allocate a new shadow variable
 * @obj:	pointer to parent object
 * @id:		data identifier
 * @size:	size of attached data
 * @gfp_flags:	GFP mask for allocation
 * @ctor:	custom constructor to initialize the shadow data (optional)
 * @ctor_data:	pointer to any data needed by @ctor (optional)
 *
 * Returns a pointer to existing shadow data if an <obj, id> shadow
 * variable is already present.  Otherwise, it creates a new shadow
 * variable like klp_shadow_alloc().
 *
 * This function guarantees that only one shadow variable exists with the given
 * @id for the given @obj.  It also guarantees that the constructor function
 * will be called only when the variable did not exist before.  The cost is
 * that @ctor is called in atomic context under a spin lock.
 *
 * Return: the shadow variable data element, NULL on failure.
 */
void *klp_shadow_get_or_alloc(void *obj, unsigned long id,
			      size_t size, gfp_t gfp_flags,
			      klp_shadow_ctor_t ctor, void *ctor_data)
{
	return __klp_shadow_get_or_alloc(obj, id, size, gfp_flags,
					 ctor, ctor_data, false);
}
EXPORT_SYMBOL_GPL(klp_shadow_get_or_alloc);

static void klp_shadow_free_struct(struct klp_shadow *shadow,
				   klp_shadow_dtor_t dtor)
{
	hash_del_rcu(&shadow->node);
	if (dtor)
		dtor(shadow->obj, shadow->data);
	kfree_rcu(shadow, rcu_head);
}

/**
 * klp_shadow_free() - detach and free a <obj, id> shadow variable
 * @obj:	pointer to parent object
 * @id:		data identifier
 * @dtor:	custom callback that can be used to unregister the variable
 *		and/or free data that the shadow variable points to (optional)
 *
 * This function releases the memory for this <obj, id> shadow variable
 * instance, callers should stop referencing it accordingly.
 */
void klp_shadow_free(void *obj, unsigned long id, klp_shadow_dtor_t dtor)
{
	struct klp_shadow *shadow;
	unsigned long flags;

	spin_lock_irqsave(&klp_shadow_lock, flags);

	/* Delete <obj, id> from hash */
	hash_for_each_possible(klp_shadow_hash, shadow, node,
			       (unsigned long)obj) {

		if (klp_shadow_match(shadow, obj, id)) {
			klp_shadow_free_struct(shadow, dtor);
			break;
		}
	}

	spin_unlock_irqrestore(&klp_shadow_lock, flags);
}
EXPORT_SYMBOL_GPL(klp_shadow_free);

/**
 * klp_shadow_free_all() - detach and free all <*, id> shadow variables
 * @id:		data identifier
 * @dtor:	custom callback that can be used to unregister the variable
 *		and/or free data that the shadow variable points to (optional)
 *
 * This function releases the memory for all <*, id> shadow variable
 * instances, callers should stop referencing them accordingly.
 */
void klp_shadow_free_all(unsigned long id, klp_shadow_dtor_t dtor)
{
	struct klp_shadow *shadow;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&klp_shadow_lock, flags);

	/* Delete all <*, id> from hash */
	hash_for_each(klp_shadow_hash, i, shadow, node) {
		if (klp_shadow_match(shadow, shadow->obj, id))
			klp_shadow_free_struct(shadow, dtor);
	}

	spin_unlock_irqrestore(&klp_shadow_lock, flags);
}
EXPORT_SYMBOL_GPL(klp_shadow_free_all);
