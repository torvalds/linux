// SPDX-License-Identifier: GPL-2.0-only
/*
 * Landlock LSM - Ruleset management
 *
 * Copyright © 2016-2020 Mickaël Salaün <mic@digikod.net>
 * Copyright © 2018-2020 ANSSI
 * Copyright © 2026 Cloudflare, Inc.
 */

#include <linux/bits.h>
#include <linux/bug.h>
#include <linux/cleanup.h>
#include <linux/compiler_types.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/lockdep.h>
#include <linux/mutex.h>
#include <linux/overflow.h>
#include <linux/rbtree.h>
#include <linux/refcount.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <uapi/linux/landlock.h>

#include "access.h"
#include "id.h"
#include "limits.h"
#include "object.h"
#include "ruleset.h"

#include <trace/events/landlock.h>

struct landlock_ruleset *
landlock_create_ruleset(const access_mask_t fs_access_mask,
			const access_mask_t net_access_mask,
			const access_mask_t scope_mask)
{
	struct landlock_ruleset *new_ruleset;

	/* Informs about useless ruleset. */
	if (!fs_access_mask && !net_access_mask && !scope_mask)
		return ERR_PTR(-ENOMSG);

	new_ruleset = kzalloc_obj(*new_ruleset, GFP_KERNEL_ACCOUNT);
	if (!new_ruleset)
		return ERR_PTR(-ENOMEM);

	refcount_set(&new_ruleset->usage, 1);
	mutex_init(&new_ruleset->lock);
	new_ruleset->rules.root_inode = RB_ROOT;

#if IS_ENABLED(CONFIG_INET)
	new_ruleset->rules.root_net_port = RB_ROOT;
#endif /* IS_ENABLED(CONFIG_INET) */

#ifdef CONFIG_TRACEPOINTS
	new_ruleset->id = landlock_get_id_range(1);
#endif /* CONFIG_TRACEPOINTS */

	/* Should already be checked in landlock_create_ruleset(). */
	if (fs_access_mask) {
		const access_mask_t mask = fs_access_mask &
					   LANDLOCK_MASK_ACCESS_FS;

		WARN_ON_ONCE(fs_access_mask != mask);
		new_ruleset->handled_masks.fs |= mask;
	}
	if (net_access_mask) {
		const access_mask_t mask = net_access_mask &
					   LANDLOCK_MASK_ACCESS_NET;

		WARN_ON_ONCE(net_access_mask != mask);
		new_ruleset->handled_masks.net |= mask;
	}
	if (scope_mask) {
		const access_mask_t mask = scope_mask & LANDLOCK_MASK_SCOPE;

		WARN_ON_ONCE(scope_mask != mask);
		new_ruleset->handled_masks.scope |= mask;
	}
	return new_ruleset;
}

static void build_check_rule(void)
{
	const struct landlock_rule rule = {
		.num_layers = ~0,
	};

	/*
	 * Checks that .num_layers is large enough for at least
	 * LANDLOCK_MAX_NUM_LAYERS layers.
	 */
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
	    const struct landlock_layer (*layers)[], const u32 num_layers,
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
	new_rule = kzalloc_flex(*new_rule, layers, new_num_layers,
				GFP_KERNEL_ACCOUNT);
	if (!new_rule)
		return ERR_PTR(-ENOMEM);
	RB_CLEAR_NODE(&new_rule->node);
	if (is_object_pointer(id.type)) {
		/* This should have been caught by landlock_store_rule(). */
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
	const struct landlock_rules rules = {
		.num_rules = ~0,
	};

	BUILD_BUG_ON(rules.num_rules < LANDLOCK_MAX_NUM_RULES);
}

/**
 * landlock_store_rule - Create and insert a rule into the rule storage
 *
 * @rules: The rule storage to be updated.  The caller is responsible for
 *         any required locking.  For rulesets, this means holding
 *         &landlock_ruleset.lock.  For domains under construction, no lock is
 *         needed because the domain is not yet visible to other tasks.
 * @id: The ID to build the new rule with.  The underlying kernel object, if
 *      any, must be held by the caller.
 * @layers: One or multiple layers to be copied into the new rule.
 * @num_layers: The number of @layers entries.
 *
 * When user space requests to add a new rule to a ruleset, @layers only
 * contains one entry and this entry is not assigned to any level.  In this
 * case, the new rule will extend @rules, similarly to a boolean OR between
 * access rights.
 *
 * When merging a ruleset in a domain, or copying a domain, @layers will be
 * added to @rules as new constraints, similarly to a boolean AND between access
 * rights.
 *
 * Return: 0 on success, -errno on failure.
 */
int landlock_store_rule(struct landlock_rules *const rules,
			const struct landlock_id id,
			const struct landlock_layer (*layers)[],
			const size_t num_layers)
{
	struct rb_node **walker_node;
	struct rb_node *parent_node = NULL;
	struct landlock_rule *new_rule;
	struct rb_root *root;

	might_sleep();
	if (WARN_ON_ONCE(!layers))
		return -ENOENT;

	if (is_object_pointer(id.type) && WARN_ON_ONCE(!id.key.object))
		return -ENOENT;

	root = landlock_get_rule_root(rules, id.type);
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
			 * landlock_add_rule(2), i.e. @rules is not a domain.
			 */
			if (WARN_ON_ONCE(this->num_layers != 1))
				return -EINVAL;
			if (WARN_ON_ONCE(this->layers[0].level != 0))
				return -EINVAL;
			this->layers[0].access |= (*layers)[0].access;
			this->layers[0].flags.quiet |= (*layers)[0].flags.quiet;
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
	if (rules->num_rules >= LANDLOCK_MAX_NUM_RULES)
		return -E2BIG;
	new_rule = create_rule(id, layers, num_layers, NULL);
	if (IS_ERR(new_rule))
		return PTR_ERR(new_rule);
	rb_link_node(&new_rule->node, parent_node, walker_node);
	rb_insert_color(&new_rule->node, root);
	rules->num_rules++;
	return 0;
}

static void build_check_layer(void)
{
	const struct landlock_layer layer = {
		.level = ~0,
		.access = ~0,
	};

	/*
	 * Checks that .level and .access are large enough to contain their expected
	 * maximum values.
	 */
	BUILD_BUG_ON(layer.level < LANDLOCK_MAX_NUM_LAYERS);
	BUILD_BUG_ON(layer.access < LANDLOCK_MASK_ACCESS_FS);
}

/* @ruleset must be locked by the caller. */
int landlock_insert_rule(struct landlock_ruleset *const ruleset,
			 const struct landlock_id id,
			 const access_mask_t access, const u32 flags)
{
	struct landlock_layer layers[] = { {
		.access = access,
		/*
		 * When @level is zero, landlock_store_rule() extends @ruleset.
		 */
		.level = 0,
		.flags = {
			.quiet = !!(flags & LANDLOCK_ADD_RULE_QUIET),
		},
	} };
	int err;

	build_check_layer();
	lockdep_assert_held(&ruleset->lock);
	err = landlock_store_rule(&ruleset->rules, id, &layers,
				  ARRAY_SIZE(layers));

#ifdef CONFIG_TRACEPOINTS
	if (!err)
		ruleset->version++;
#endif /* CONFIG_TRACEPOINTS */

	return err;
}

void landlock_free_rules(struct landlock_rules *const rules)
{
	struct landlock_rule *freeme, *next;

	might_sleep();
	rbtree_postorder_for_each_entry_safe(freeme, next, &rules->root_inode,
					     node)
		free_rule(freeme, LANDLOCK_KEY_INODE);

#if IS_ENABLED(CONFIG_INET)
	rbtree_postorder_for_each_entry_safe(freeme, next,
					     &rules->root_net_port, node)
		free_rule(freeme, LANDLOCK_KEY_NET_PORT);
#endif /* IS_ENABLED(CONFIG_INET) */
}

static void free_ruleset(struct landlock_ruleset *const ruleset)
{
	might_sleep();
	trace_landlock_free_ruleset(ruleset);
	landlock_free_rules(&ruleset->rules);
	kfree(ruleset);
}

void landlock_put_ruleset(struct landlock_ruleset *const ruleset)
{
	might_sleep();
	if (ruleset && refcount_dec_and_test(&ruleset->usage))
		free_ruleset(ruleset);
}
