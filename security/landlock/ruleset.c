// SPDX-License-Identifier: GPL-2.0-only
/*
 * Landlock LSM - Ruleset management
 *
 * Copyright © 2016-2020 Mickaël Salaün <mic@digikod.net>
 * Copyright © 2018-2020 ANSSI
 */

#include <linux/bits.h>
#include <linux/bug.h>
#include <linux/compiler_types.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/lockdep.h>
#include <linux/overflow.h>
#include <linux/rbtree.h>
#include <linux/refcount.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include "limits.h"
#include "object.h"
#include "ruleset.h"

static struct landlock_ruleset *create_ruleset(const u32 num_layers)
{
	struct landlock_ruleset *new_ruleset;

	new_ruleset =
		kzalloc(struct_size(new_ruleset, fs_access_masks, num_layers),
			GFP_KERNEL_ACCOUNT);
	if (!new_ruleset)
		return ERR_PTR(-ENOMEM);
	refcount_set(&new_ruleset->usage, 1);
	mutex_init(&new_ruleset->lock);
	new_ruleset->root = RB_ROOT;
	new_ruleset->num_layers = num_layers;
	/*
	 * hierarchy = NULL
	 * num_rules = 0
	 * fs_access_masks[] = 0
	 */
	return new_ruleset;
}

struct landlock_ruleset *landlock_create_ruleset(const u32 fs_access_mask)
{
	struct landlock_ruleset *new_ruleset;

	/* Informs about useless ruleset. */
	if (!fs_access_mask)
		return ERR_PTR(-ENOMSG);
	new_ruleset = create_ruleset(1);
	if (!IS_ERR(new_ruleset))
		new_ruleset->fs_access_masks[0] = fs_access_mask;
	return new_ruleset;
}

static void build_check_rule(void)
{
	const struct landlock_rule rule = {
		.num_layers = ~0,
	};

	BUILD_BUG_ON(rule.num_layers < LANDLOCK_MAX_NUM_LAYERS);
}

static struct landlock_rule *
create_rule(struct landlock_object *const object,
	    const struct landlock_layer (*const layers)[], const u32 num_layers,
	    const struct landlock_layer *const new_layer)
{
	struct landlock_rule *new_rule;
	u32 new_num_layers;

	build_check_rule();
	if (new_layer) {
		/* Should already be checked by landlock_merge_ruleset(). */
		if (WARN_ON_ONCE(num_layers >= LANDLOCK_MAX_NUM_LAYERS))
			return ERR_PTR(-E2BIG);
		new_num_layers = num_layers + 1;
	} else {
		new_num_layers = num_layers;
	}
	new_rule = kzalloc(struct_size(new_rule, layers, new_num_layers),
			   GFP_KERNEL_ACCOUNT);
	if (!new_rule)
		return ERR_PTR(-ENOMEM);
	RB_CLEAR_NODE(&new_rule->node);
	landlock_get_object(object);
	new_rule->object = object;
	new_rule->num_layers = new_num_layers;
	/* Copies the original layer stack. */
	memcpy(new_rule->layers, layers,
	       flex_array_size(new_rule, layers, num_layers));
	if (new_layer)
		/* Adds a copy of @new_layer on the layer stack. */
		new_rule->layers[new_rule->num_layers - 1] = *new_layer;
	return new_rule;
}

static void free_rule(struct landlock_rule *const rule)
{
	might_sleep();
	if (!rule)
		return;
	landlock_put_object(rule->object);
	kfree(rule);
}

static void build_check_ruleset(void)
{
	const struct landlock_ruleset ruleset = {
		.num_rules = ~0,
		.num_layers = ~0,
	};
	typeof(ruleset.fs_access_masks[0]) fs_access_mask = ~0;

	BUILD_BUG_ON(ruleset.num_rules < LANDLOCK_MAX_NUM_RULES);
	BUILD_BUG_ON(ruleset.num_layers < LANDLOCK_MAX_NUM_LAYERS);
	BUILD_BUG_ON(fs_access_mask < LANDLOCK_MASK_ACCESS_FS);
}

/**
 * insert_rule - Create and insert a rule in a ruleset
 *
 * @ruleset: The ruleset to be updated.
 * @object: The object to build the new rule with.  The underlying kernel
 *          object must be held by the caller.
 * @layers: One or multiple layers to be copied into the new rule.
 * @num_layers: The number of @layers entries.
 *
 * When user space requests to add a new rule to a ruleset, @layers only
 * contains one entry and this entry is not assigned to any level.  In this
 * case, the new rule will extend @ruleset, similarly to a boolean OR between
 * access rights.
 *
 * When merging a ruleset in a domain, or copying a domain, @layers will be
 * added to @ruleset as new constraints, similarly to a boolean AND between
 * access rights.
 */
static int insert_rule(struct landlock_ruleset *const ruleset,
		       struct landlock_object *const object,
		       const struct landlock_layer (*const layers)[],
		       size_t num_layers)
{
	struct rb_node **walker_node;
	struct rb_node *parent_node = NULL;
	struct landlock_rule *new_rule;

	might_sleep();
	lockdep_assert_held(&ruleset->lock);
	if (WARN_ON_ONCE(!object || !layers))
		return -ENOENT;
	walker_node = &(ruleset->root.rb_node);
	while (*walker_node) {
		struct landlock_rule *const this =
			rb_entry(*walker_node, struct landlock_rule, node);

		if (this->object != object) {
			parent_node = *walker_node;
			if (this->object < object)
				walker_node = &((*walker_node)->rb_right);
			else
				walker_node = &((*walker_node)->rb_left);
			continue;
		}

		/* Only a single-level layer should match an existing rule. */
		if (WARN_ON_ONCE(num_layers != 1))
			return -EINVAL;

		/* If there is a matching rule, updates it. */
		if ((*layers)[0].level == 0) {
			/*
			 * Extends access rights when the request comes from
			 * landlock_add_rule(2), i.e. @ruleset is not a domain.
			 */
			if (WARN_ON_ONCE(this->num_layers != 1))
				return -EINVAL;
			if (WARN_ON_ONCE(this->layers[0].level != 0))
				return -EINVAL;
			this->layers[0].access |= (*layers)[0].access;
			return 0;
		}

		if (WARN_ON_ONCE(this->layers[0].level == 0))
			return -EINVAL;

		/*
		 * Intersects access rights when it is a merge between a
		 * ruleset and a domain.
		 */
		new_rule = create_rule(object, &this->layers, this->num_layers,
				       &(*layers)[0]);
		if (IS_ERR(new_rule))
			return PTR_ERR(new_rule);
		rb_replace_node(&this->node, &new_rule->node, &ruleset->root);
		free_rule(this);
		return 0;
	}

	/* There is no match for @object. */
	build_check_ruleset();
	if (ruleset->num_rules >= LANDLOCK_MAX_NUM_RULES)
		return -E2BIG;
	new_rule = create_rule(object, layers, num_layers, NULL);
	if (IS_ERR(new_rule))
		return PTR_ERR(new_rule);
	rb_link_node(&new_rule->node, parent_node, walker_node);
	rb_insert_color(&new_rule->node, &ruleset->root);
	ruleset->num_rules++;
	return 0;
}

static void build_check_layer(void)
{
	const struct landlock_layer layer = {
		.level = ~0,
		.access = ~0,
	};

	BUILD_BUG_ON(layer.level < LANDLOCK_MAX_NUM_LAYERS);
	BUILD_BUG_ON(layer.access < LANDLOCK_MASK_ACCESS_FS);
}

/* @ruleset must be locked by the caller. */
int landlock_insert_rule(struct landlock_ruleset *const ruleset,
			 struct landlock_object *const object, const u32 access)
{
	struct landlock_layer layers[] = { {
		.access = access,
		/* When @level is zero, insert_rule() extends @ruleset. */
		.level = 0,
	} };

	build_check_layer();
	return insert_rule(ruleset, object, &layers, ARRAY_SIZE(layers));
}

static inline void get_hierarchy(struct landlock_hierarchy *const hierarchy)
{
	if (hierarchy)
		refcount_inc(&hierarchy->usage);
}

static void put_hierarchy(struct landlock_hierarchy *hierarchy)
{
	while (hierarchy && refcount_dec_and_test(&hierarchy->usage)) {
		const struct landlock_hierarchy *const freeme = hierarchy;

		hierarchy = hierarchy->parent;
		kfree(freeme);
	}
}

static int merge_ruleset(struct landlock_ruleset *const dst,
			 struct landlock_ruleset *const src)
{
	struct landlock_rule *walker_rule, *next_rule;
	int err = 0;

	might_sleep();
	/* Should already be checked by landlock_merge_ruleset() */
	if (WARN_ON_ONCE(!src))
		return 0;
	/* Only merge into a domain. */
	if (WARN_ON_ONCE(!dst || !dst->hierarchy))
		return -EINVAL;

	/* Locks @dst first because we are its only owner. */
	mutex_lock(&dst->lock);
	mutex_lock_nested(&src->lock, SINGLE_DEPTH_NESTING);

	/* Stacks the new layer. */
	if (WARN_ON_ONCE(src->num_layers != 1 || dst->num_layers < 1)) {
		err = -EINVAL;
		goto out_unlock;
	}
	dst->fs_access_masks[dst->num_layers - 1] = src->fs_access_masks[0];

	/* Merges the @src tree. */
	rbtree_postorder_for_each_entry_safe(walker_rule, next_rule, &src->root,
					     node) {
		struct landlock_layer layers[] = { {
			.level = dst->num_layers,
		} };

		if (WARN_ON_ONCE(walker_rule->num_layers != 1)) {
			err = -EINVAL;
			goto out_unlock;
		}
		if (WARN_ON_ONCE(walker_rule->layers[0].level != 0)) {
			err = -EINVAL;
			goto out_unlock;
		}
		layers[0].access = walker_rule->layers[0].access;
		err = insert_rule(dst, walker_rule->object, &layers,
				  ARRAY_SIZE(layers));
		if (err)
			goto out_unlock;
	}

out_unlock:
	mutex_unlock(&src->lock);
	mutex_unlock(&dst->lock);
	return err;
}

static int inherit_ruleset(struct landlock_ruleset *const parent,
			   struct landlock_ruleset *const child)
{
	struct landlock_rule *walker_rule, *next_rule;
	int err = 0;

	might_sleep();
	if (!parent)
		return 0;

	/* Locks @child first because we are its only owner. */
	mutex_lock(&child->lock);
	mutex_lock_nested(&parent->lock, SINGLE_DEPTH_NESTING);

	/* Copies the @parent tree. */
	rbtree_postorder_for_each_entry_safe(walker_rule, next_rule,
					     &parent->root, node) {
		err = insert_rule(child, walker_rule->object,
				  &walker_rule->layers,
				  walker_rule->num_layers);
		if (err)
			goto out_unlock;
	}

	if (WARN_ON_ONCE(child->num_layers <= parent->num_layers)) {
		err = -EINVAL;
		goto out_unlock;
	}
	/* Copies the parent layer stack and leaves a space for the new layer. */
	memcpy(child->fs_access_masks, parent->fs_access_masks,
	       flex_array_size(parent, fs_access_masks, parent->num_layers));

	if (WARN_ON_ONCE(!parent->hierarchy)) {
		err = -EINVAL;
		goto out_unlock;
	}
	get_hierarchy(parent->hierarchy);
	child->hierarchy->parent = parent->hierarchy;

out_unlock:
	mutex_unlock(&parent->lock);
	mutex_unlock(&child->lock);
	return err;
}

static void free_ruleset(struct landlock_ruleset *const ruleset)
{
	struct landlock_rule *freeme, *next;

	might_sleep();
	rbtree_postorder_for_each_entry_safe(freeme, next, &ruleset->root, node)
		free_rule(freeme);
	put_hierarchy(ruleset->hierarchy);
	kfree(ruleset);
}

void landlock_put_ruleset(struct landlock_ruleset *const ruleset)
{
	might_sleep();
	if (ruleset && refcount_dec_and_test(&ruleset->usage))
		free_ruleset(ruleset);
}

static void free_ruleset_work(struct work_struct *const work)
{
	struct landlock_ruleset *ruleset;

	ruleset = container_of(work, struct landlock_ruleset, work_free);
	free_ruleset(ruleset);
}

void landlock_put_ruleset_deferred(struct landlock_ruleset *const ruleset)
{
	if (ruleset && refcount_dec_and_test(&ruleset->usage)) {
		INIT_WORK(&ruleset->work_free, free_ruleset_work);
		schedule_work(&ruleset->work_free);
	}
}

/**
 * landlock_merge_ruleset - Merge a ruleset with a domain
 *
 * @parent: Parent domain.
 * @ruleset: New ruleset to be merged.
 *
 * Returns the intersection of @parent and @ruleset, or returns @parent if
 * @ruleset is empty, or returns a duplicate of @ruleset if @parent is empty.
 */
struct landlock_ruleset *
landlock_merge_ruleset(struct landlock_ruleset *const parent,
		       struct landlock_ruleset *const ruleset)
{
	struct landlock_ruleset *new_dom;
	u32 num_layers;
	int err;

	might_sleep();
	if (WARN_ON_ONCE(!ruleset || parent == ruleset))
		return ERR_PTR(-EINVAL);

	if (parent) {
		if (parent->num_layers >= LANDLOCK_MAX_NUM_LAYERS)
			return ERR_PTR(-E2BIG);
		num_layers = parent->num_layers + 1;
	} else {
		num_layers = 1;
	}

	/* Creates a new domain... */
	new_dom = create_ruleset(num_layers);
	if (IS_ERR(new_dom))
		return new_dom;
	new_dom->hierarchy =
		kzalloc(sizeof(*new_dom->hierarchy), GFP_KERNEL_ACCOUNT);
	if (!new_dom->hierarchy) {
		err = -ENOMEM;
		goto out_put_dom;
	}
	refcount_set(&new_dom->hierarchy->usage, 1);

	/* ...as a child of @parent... */
	err = inherit_ruleset(parent, new_dom);
	if (err)
		goto out_put_dom;

	/* ...and including @ruleset. */
	err = merge_ruleset(new_dom, ruleset);
	if (err)
		goto out_put_dom;

	return new_dom;

out_put_dom:
	landlock_put_ruleset(new_dom);
	return ERR_PTR(err);
}

/*
 * The returned access has the same lifetime as @ruleset.
 */
const struct landlock_rule *
landlock_find_rule(const struct landlock_ruleset *const ruleset,
		   const struct landlock_object *const object)
{
	const struct rb_node *node;

	if (!object)
		return NULL;
	node = ruleset->root.rb_node;
	while (node) {
		struct landlock_rule *this =
			rb_entry(node, struct landlock_rule, node);

		if (this->object == object)
			return this;
		if (this->object < object)
			node = node->rb_right;
		else
			node = node->rb_left;
	}
	return NULL;
}
