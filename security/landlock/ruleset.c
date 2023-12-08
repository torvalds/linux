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
		kzalloc(struct_size(new_ruleset, access_masks, num_layers),
			GFP_KERNEL_ACCOUNT);
	if (!new_ruleset)
		return ERR_PTR(-ENOMEM);
	refcount_set(&new_ruleset->usage, 1);
	mutex_init(&new_ruleset->lock);
	new_ruleset->root_inode = RB_ROOT;

#if IS_ENABLED(CONFIG_INET)
	new_ruleset->root_net_port = RB_ROOT;
#endif /* IS_ENABLED(CONFIG_INET) */

	new_ruleset->num_layers = num_layers;
	/*
	 * hierarchy = NULL
	 * num_rules = 0
	 * access_masks[] = 0
	 */
	return new_ruleset;
}

struct landlock_ruleset *
landlock_create_ruleset(const access_mask_t fs_access_mask,
			const access_mask_t net_access_mask)
{
	struct landlock_ruleset *new_ruleset;

	/* Informs about useless ruleset. */
	if (!fs_access_mask && !net_access_mask)
		return ERR_PTR(-ENOMSG);
	new_ruleset = create_ruleset(1);
	if (IS_ERR(new_ruleset))
		return new_ruleset;
	if (fs_access_mask)
		landlock_add_fs_access_mask(new_ruleset, fs_access_mask, 0);
	if (net_access_mask)
		landlock_add_net_access_mask(new_ruleset, net_access_mask, 0);
	return new_ruleset;
}

static void build_check_rule(void)
{
	const struct landlock_rule rule = {
		.num_layers = ~0,
	};

	BUILD_BUG_ON(rule.num_layers < LANDLOCK_MAX_NUM_LAYERS);
}

static bool is_object_pointer(const enum landlock_key_type key_type)
{
	switch (key_type) {
	case LANDLOCK_KEY_INODE:
		return true;

#if IS_ENABLED(CONFIG_INET)
	case LANDLOCK_KEY_NET_PORT:
		return false;
#endif /* IS_ENABLED(CONFIG_INET) */

	default:
		WARN_ON_ONCE(1);
		return false;
	}
}

static struct landlock_rule *
create_rule(const struct landlock_id id,
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
	if (is_object_pointer(id.type)) {
		/* This should be catched by insert_rule(). */
		WARN_ON_ONCE(!id.key.object);
		landlock_get_object(id.key.object);
	}

	new_rule->key = id.key;
	new_rule->num_layers = new_num_layers;
	/* Copies the original layer stack. */
	memcpy(new_rule->layers, layers,
	       flex_array_size(new_rule, layers, num_layers));
	if (new_layer)
		/* Adds a copy of @new_layer on the layer stack. */
		new_rule->layers[new_rule->num_layers - 1] = *new_layer;
	return new_rule;
}

static struct rb_root *get_root(struct landlock_ruleset *const ruleset,
				const enum landlock_key_type key_type)
{
	switch (key_type) {
	case LANDLOCK_KEY_INODE:
		return &ruleset->root_inode;

#if IS_ENABLED(CONFIG_INET)
	case LANDLOCK_KEY_NET_PORT:
		return &ruleset->root_net_port;
#endif /* IS_ENABLED(CONFIG_INET) */

	default:
		WARN_ON_ONCE(1);
		return ERR_PTR(-EINVAL);
	}
}

static void free_rule(struct landlock_rule *const rule,
		      const enum landlock_key_type key_type)
{
	might_sleep();
	if (!rule)
		return;
	if (is_object_pointer(key_type))
		landlock_put_object(rule->key.object);
	kfree(rule);
}

static void build_check_ruleset(void)
{
	const struct landlock_ruleset ruleset = {
		.num_rules = ~0,
		.num_layers = ~0,
	};
	typeof(ruleset.access_masks[0]) access_masks = ~0;

	BUILD_BUG_ON(ruleset.num_rules < LANDLOCK_MAX_NUM_RULES);
	BUILD_BUG_ON(ruleset.num_layers < LANDLOCK_MAX_NUM_LAYERS);
	BUILD_BUG_ON(access_masks <
		     ((LANDLOCK_MASK_ACCESS_FS << LANDLOCK_SHIFT_ACCESS_FS) |
		      (LANDLOCK_MASK_ACCESS_NET << LANDLOCK_SHIFT_ACCESS_NET)));
}

/**
 * insert_rule - Create and insert a rule in a ruleset
 *
 * @ruleset: The ruleset to be updated.
 * @id: The ID to build the new rule with.  The underlying kernel object, if
 *      any, must be held by the caller.
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
		       const struct landlock_id id,
		       const struct landlock_layer (*const layers)[],
		       const size_t num_layers)
{
	struct rb_node **walker_node;
	struct rb_node *parent_node = NULL;
	struct landlock_rule *new_rule;
	struct rb_root *root;

	might_sleep();
	lockdep_assert_held(&ruleset->lock);
	if (WARN_ON_ONCE(!layers))
		return -ENOENT;

	if (is_object_pointer(id.type) && WARN_ON_ONCE(!id.key.object))
		return -ENOENT;

	root = get_root(ruleset, id.type);
	if (IS_ERR(root))
		return PTR_ERR(root);

	walker_node = &root->rb_node;
	while (*walker_node) {
		struct landlock_rule *const this =
			rb_entry(*walker_node, struct landlock_rule, node);

		if (this->key.data != id.key.data) {
			parent_node = *walker_node;
			if (this->key.data < id.key.data)
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
		new_rule = create_rule(id, &this->layers, this->num_layers,
				       &(*layers)[0]);
		if (IS_ERR(new_rule))
			return PTR_ERR(new_rule);
		rb_replace_node(&this->node, &new_rule->node, root);
		free_rule(this, id.type);
		return 0;
	}

	/* There is no match for @id. */
	build_check_ruleset();
	if (ruleset->num_rules >= LANDLOCK_MAX_NUM_RULES)
		return -E2BIG;
	new_rule = create_rule(id, layers, num_layers, NULL);
	if (IS_ERR(new_rule))
		return PTR_ERR(new_rule);
	rb_link_node(&new_rule->node, parent_node, walker_node);
	rb_insert_color(&new_rule->node, root);
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
			 const struct landlock_id id,
			 const access_mask_t access)
{
	struct landlock_layer layers[] = { {
		.access = access,
		/* When @level is zero, insert_rule() extends @ruleset. */
		.level = 0,
	} };

	build_check_layer();
	return insert_rule(ruleset, id, &layers, ARRAY_SIZE(layers));
}

static void get_hierarchy(struct landlock_hierarchy *const hierarchy)
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

static int merge_tree(struct landlock_ruleset *const dst,
		      struct landlock_ruleset *const src,
		      const enum landlock_key_type key_type)
{
	struct landlock_rule *walker_rule, *next_rule;
	struct rb_root *src_root;
	int err = 0;

	might_sleep();
	lockdep_assert_held(&dst->lock);
	lockdep_assert_held(&src->lock);

	src_root = get_root(src, key_type);
	if (IS_ERR(src_root))
		return PTR_ERR(src_root);

	/* Merges the @src tree. */
	rbtree_postorder_for_each_entry_safe(walker_rule, next_rule, src_root,
					     node) {
		struct landlock_layer layers[] = { {
			.level = dst->num_layers,
		} };
		const struct landlock_id id = {
			.key = walker_rule->key,
			.type = key_type,
		};

		if (WARN_ON_ONCE(walker_rule->num_layers != 1))
			return -EINVAL;

		if (WARN_ON_ONCE(walker_rule->layers[0].level != 0))
			return -EINVAL;

		layers[0].access = walker_rule->layers[0].access;

		err = insert_rule(dst, id, &layers, ARRAY_SIZE(layers));
		if (err)
			return err;
	}
	return err;
}

static int merge_ruleset(struct landlock_ruleset *const dst,
			 struct landlock_ruleset *const src)
{
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
	dst->access_masks[dst->num_layers - 1] = src->access_masks[0];

	/* Merges the @src inode tree. */
	err = merge_tree(dst, src, LANDLOCK_KEY_INODE);
	if (err)
		goto out_unlock;

#if IS_ENABLED(CONFIG_INET)
	/* Merges the @src network port tree. */
	err = merge_tree(dst, src, LANDLOCK_KEY_NET_PORT);
	if (err)
		goto out_unlock;
#endif /* IS_ENABLED(CONFIG_INET) */

out_unlock:
	mutex_unlock(&src->lock);
	mutex_unlock(&dst->lock);
	return err;
}

static int inherit_tree(struct landlock_ruleset *const parent,
			struct landlock_ruleset *const child,
			const enum landlock_key_type key_type)
{
	struct landlock_rule *walker_rule, *next_rule;
	struct rb_root *parent_root;
	int err = 0;

	might_sleep();
	lockdep_assert_held(&parent->lock);
	lockdep_assert_held(&child->lock);

	parent_root = get_root(parent, key_type);
	if (IS_ERR(parent_root))
		return PTR_ERR(parent_root);

	/* Copies the @parent inode or network tree. */
	rbtree_postorder_for_each_entry_safe(walker_rule, next_rule,
					     parent_root, node) {
		const struct landlock_id id = {
			.key = walker_rule->key,
			.type = key_type,
		};

		err = insert_rule(child, id, &walker_rule->layers,
				  walker_rule->num_layers);
		if (err)
			return err;
	}
	return err;
}

static int inherit_ruleset(struct landlock_ruleset *const parent,
			   struct landlock_ruleset *const child)
{
	int err = 0;

	might_sleep();
	if (!parent)
		return 0;

	/* Locks @child first because we are its only owner. */
	mutex_lock(&child->lock);
	mutex_lock_nested(&parent->lock, SINGLE_DEPTH_NESTING);

	/* Copies the @parent inode tree. */
	err = inherit_tree(parent, child, LANDLOCK_KEY_INODE);
	if (err)
		goto out_unlock;

#if IS_ENABLED(CONFIG_INET)
	/* Copies the @parent network port tree. */
	err = inherit_tree(parent, child, LANDLOCK_KEY_NET_PORT);
	if (err)
		goto out_unlock;
#endif /* IS_ENABLED(CONFIG_INET) */

	if (WARN_ON_ONCE(child->num_layers <= parent->num_layers)) {
		err = -EINVAL;
		goto out_unlock;
	}
	/* Copies the parent layer stack and leaves a space for the new layer. */
	memcpy(child->access_masks, parent->access_masks,
	       flex_array_size(parent, access_masks, parent->num_layers));

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
	rbtree_postorder_for_each_entry_safe(freeme, next, &ruleset->root_inode,
					     node)
		free_rule(freeme, LANDLOCK_KEY_INODE);

#if IS_ENABLED(CONFIG_INET)
	rbtree_postorder_for_each_entry_safe(freeme, next,
					     &ruleset->root_net_port, node)
		free_rule(freeme, LANDLOCK_KEY_NET_PORT);
#endif /* IS_ENABLED(CONFIG_INET) */

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
		   const struct landlock_id id)
{
	const struct rb_root *root;
	const struct rb_node *node;

	root = get_root((struct landlock_ruleset *)ruleset, id.type);
	if (IS_ERR(root))
		return NULL;
	node = root->rb_node;

	while (node) {
		struct landlock_rule *this =
			rb_entry(node, struct landlock_rule, node);

		if (this->key.data == id.key.data)
			return this;
		if (this->key.data < id.key.data)
			node = node->rb_right;
		else
			node = node->rb_left;
	}
	return NULL;
}

/*
 * @layer_masks is read and may be updated according to the access request and
 * the matching rule.
 * @masks_array_size must be equal to ARRAY_SIZE(*layer_masks).
 *
 * Returns true if the request is allowed (i.e. relevant layer masks for the
 * request are empty).
 */
bool landlock_unmask_layers(const struct landlock_rule *const rule,
			    const access_mask_t access_request,
			    layer_mask_t (*const layer_masks)[],
			    const size_t masks_array_size)
{
	size_t layer_level;

	if (!access_request || !layer_masks)
		return true;
	if (!rule)
		return false;

	/*
	 * An access is granted if, for each policy layer, at least one rule
	 * encountered on the pathwalk grants the requested access,
	 * regardless of its position in the layer stack.  We must then check
	 * the remaining layers for each inode, from the first added layer to
	 * the last one.  When there is multiple requested accesses, for each
	 * policy layer, the full set of requested accesses may not be granted
	 * by only one rule, but by the union (binary OR) of multiple rules.
	 * E.g. /a/b <execute> + /a <read> => /a/b <execute + read>
	 */
	for (layer_level = 0; layer_level < rule->num_layers; layer_level++) {
		const struct landlock_layer *const layer =
			&rule->layers[layer_level];
		const layer_mask_t layer_bit = BIT_ULL(layer->level - 1);
		const unsigned long access_req = access_request;
		unsigned long access_bit;
		bool is_empty;

		/*
		 * Records in @layer_masks which layer grants access to each
		 * requested access.
		 */
		is_empty = true;
		for_each_set_bit(access_bit, &access_req, masks_array_size) {
			if (layer->access & BIT_ULL(access_bit))
				(*layer_masks)[access_bit] &= ~layer_bit;
			is_empty = is_empty && !(*layer_masks)[access_bit];
		}
		if (is_empty)
			return true;
	}
	return false;
}

typedef access_mask_t
get_access_mask_t(const struct landlock_ruleset *const ruleset,
		  const u16 layer_level);

/**
 * landlock_init_layer_masks - Initialize layer masks from an access request
 *
 * Populates @layer_masks such that for each access right in @access_request,
 * the bits for all the layers are set where this access right is handled.
 *
 * @domain: The domain that defines the current restrictions.
 * @access_request: The requested access rights to check.
 * @layer_masks: It must contain %LANDLOCK_NUM_ACCESS_FS or
 * %LANDLOCK_NUM_ACCESS_NET elements according to @key_type.
 * @key_type: The key type to switch between access masks of different types.
 *
 * Returns: An access mask where each access right bit is set which is handled
 * in any of the active layers in @domain.
 */
access_mask_t
landlock_init_layer_masks(const struct landlock_ruleset *const domain,
			  const access_mask_t access_request,
			  layer_mask_t (*const layer_masks)[],
			  const enum landlock_key_type key_type)
{
	access_mask_t handled_accesses = 0;
	size_t layer_level, num_access;
	get_access_mask_t *get_access_mask;

	switch (key_type) {
	case LANDLOCK_KEY_INODE:
		get_access_mask = landlock_get_fs_access_mask;
		num_access = LANDLOCK_NUM_ACCESS_FS;
		break;

#if IS_ENABLED(CONFIG_INET)
	case LANDLOCK_KEY_NET_PORT:
		get_access_mask = landlock_get_net_access_mask;
		num_access = LANDLOCK_NUM_ACCESS_NET;
		break;
#endif /* IS_ENABLED(CONFIG_INET) */

	default:
		WARN_ON_ONCE(1);
		return 0;
	}

	memset(layer_masks, 0,
	       array_size(sizeof((*layer_masks)[0]), num_access));

	/* An empty access request can happen because of O_WRONLY | O_RDWR. */
	if (!access_request)
		return 0;

	/* Saves all handled accesses per layer. */
	for (layer_level = 0; layer_level < domain->num_layers; layer_level++) {
		const unsigned long access_req = access_request;
		unsigned long access_bit;

		for_each_set_bit(access_bit, &access_req, num_access) {
			if (BIT_ULL(access_bit) &
			    get_access_mask(domain, layer_level)) {
				(*layer_masks)[access_bit] |=
					BIT_ULL(layer_level);
				handled_accesses |= BIT_ULL(access_bit);
			}
		}
	}
	return handled_accesses;
}
