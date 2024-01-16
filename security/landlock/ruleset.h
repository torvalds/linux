/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Landlock LSM - Ruleset management
 *
 * Copyright © 2016-2020 Mickaël Salaün <mic@digikod.net>
 * Copyright © 2018-2020 ANSSI
 */

#ifndef _SECURITY_LANDLOCK_RULESET_H
#define _SECURITY_LANDLOCK_RULESET_H

#include <linux/bitops.h>
#include <linux/build_bug.h>
#include <linux/mutex.h>
#include <linux/rbtree.h>
#include <linux/refcount.h>
#include <linux/workqueue.h>

#include "limits.h"
#include "object.h"

typedef u16 access_mask_t;
/* Makes sure all filesystem access rights can be stored. */
static_assert(BITS_PER_TYPE(access_mask_t) >= LANDLOCK_NUM_ACCESS_FS);
/* Makes sure for_each_set_bit() and for_each_clear_bit() calls are OK. */
static_assert(sizeof(unsigned long) >= sizeof(access_mask_t));

typedef u16 layer_mask_t;
/* Makes sure all layers can be checked. */
static_assert(BITS_PER_TYPE(layer_mask_t) >= LANDLOCK_MAX_NUM_LAYERS);

/**
 * struct landlock_layer - Access rights for a given layer
 */
struct landlock_layer {
	/**
	 * @level: Position of this layer in the layer stack.
	 */
	u16 level;
	/**
	 * @access: Bitfield of allowed actions on the kernel object.  They are
	 * relative to the object type (e.g. %LANDLOCK_ACTION_FS_READ).
	 */
	access_mask_t access;
};

/**
 * struct landlock_rule - Access rights tied to an object
 */
struct landlock_rule {
	/**
	 * @node: Node in the ruleset's red-black tree.
	 */
	struct rb_node node;
	/**
	 * @object: Pointer to identify a kernel object (e.g. an inode).  This
	 * is used as a key for this ruleset element.  This pointer is set once
	 * and never modified.  It always points to an allocated object because
	 * each rule increments the refcount of its object.
	 */
	struct landlock_object *object;
	/**
	 * @num_layers: Number of entries in @layers.
	 */
	u32 num_layers;
	/**
	 * @layers: Stack of layers, from the latest to the newest, implemented
	 * as a flexible array member (FAM).
	 */
	struct landlock_layer layers[];
};

/**
 * struct landlock_hierarchy - Node in a ruleset hierarchy
 */
struct landlock_hierarchy {
	/**
	 * @parent: Pointer to the parent node, or NULL if it is a root
	 * Landlock domain.
	 */
	struct landlock_hierarchy *parent;
	/**
	 * @usage: Number of potential children domains plus their parent
	 * domain.
	 */
	refcount_t usage;
};

/**
 * struct landlock_ruleset - Landlock ruleset
 *
 * This data structure must contain unique entries, be updatable, and quick to
 * match an object.
 */
struct landlock_ruleset {
	/**
	 * @root: Root of a red-black tree containing &struct landlock_rule
	 * nodes.  Once a ruleset is tied to a process (i.e. as a domain), this
	 * tree is immutable until @usage reaches zero.
	 */
	struct rb_root root;
	/**
	 * @hierarchy: Enables hierarchy identification even when a parent
	 * domain vanishes.  This is needed for the ptrace protection.
	 */
	struct landlock_hierarchy *hierarchy;
	union {
		/**
		 * @work_free: Enables to free a ruleset within a lockless
		 * section.  This is only used by
		 * landlock_put_ruleset_deferred() when @usage reaches zero.
		 * The fields @lock, @usage, @num_rules, @num_layers and
		 * @fs_access_masks are then unused.
		 */
		struct work_struct work_free;
		struct {
			/**
			 * @lock: Protects against concurrent modifications of
			 * @root, if @usage is greater than zero.
			 */
			struct mutex lock;
			/**
			 * @usage: Number of processes (i.e. domains) or file
			 * descriptors referencing this ruleset.
			 */
			refcount_t usage;
			/**
			 * @num_rules: Number of non-overlapping (i.e. not for
			 * the same object) rules in this ruleset.
			 */
			u32 num_rules;
			/**
			 * @num_layers: Number of layers that are used in this
			 * ruleset.  This enables to check that all the layers
			 * allow an access request.  A value of 0 identifies a
			 * non-merged ruleset (i.e. not a domain).
			 */
			u32 num_layers;
			/**
			 * @fs_access_masks: Contains the subset of filesystem
			 * actions that are restricted by a ruleset.  A domain
			 * saves all layers of merged rulesets in a stack
			 * (FAM), starting from the first layer to the last
			 * one.  These layers are used when merging rulesets,
			 * for user space backward compatibility (i.e.
			 * future-proof), and to properly handle merged
			 * rulesets without overlapping access rights.  These
			 * layers are set once and never changed for the
			 * lifetime of the ruleset.
			 */
			access_mask_t fs_access_masks[];
		};
	};
};

struct landlock_ruleset *
landlock_create_ruleset(const access_mask_t fs_access_mask);

void landlock_put_ruleset(struct landlock_ruleset *const ruleset);
void landlock_put_ruleset_deferred(struct landlock_ruleset *const ruleset);

int landlock_insert_rule(struct landlock_ruleset *const ruleset,
			 struct landlock_object *const object,
			 const access_mask_t access);

struct landlock_ruleset *
landlock_merge_ruleset(struct landlock_ruleset *const parent,
		       struct landlock_ruleset *const ruleset);

const struct landlock_rule *
landlock_find_rule(const struct landlock_ruleset *const ruleset,
		   const struct landlock_object *const object);

static inline void landlock_get_ruleset(struct landlock_ruleset *const ruleset)
{
	if (ruleset)
		refcount_inc(&ruleset->usage);
}

#endif /* _SECURITY_LANDLOCK_RULESET_H */
