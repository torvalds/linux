/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Landlock LSM - Ruleset management
 *
 * Copyright © 2016-2020 Mickaël Salaün <mic@digikod.net>
 * Copyright © 2018-2020 ANSSI
 */

#ifndef _SECURITY_LANDLOCK_RULESET_H
#define _SECURITY_LANDLOCK_RULESET_H

#include <linux/cleanup.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/rbtree.h>
#include <linux/refcount.h>
#include <linux/workqueue.h>

#include "access.h"
#include "limits.h"
#include "object.h"

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
 * union landlock_key - Key of a ruleset's red-black tree
 */
union landlock_key {
	/**
	 * @object: Pointer to identify a kernel object (e.g. an inode).
	 */
	struct landlock_object *object;
	/**
	 * @data: Raw data to identify an arbitrary 32-bit value
	 * (e.g. a TCP port).
	 */
	uintptr_t data;
};

/**
 * enum landlock_key_type - Type of &union landlock_key
 */
enum landlock_key_type {
	/**
	 * @LANDLOCK_KEY_INODE: Type of &landlock_ruleset.root_inode's node
	 * keys.
	 */
	LANDLOCK_KEY_INODE = 1,
	/**
	 * @LANDLOCK_KEY_NET_PORT: Type of &landlock_ruleset.root_net_port's
	 * node keys.
	 */
	LANDLOCK_KEY_NET_PORT,
};

/**
 * struct landlock_id - Unique rule identifier for a ruleset
 */
struct landlock_id {
	/**
	 * @key: Identifies either a kernel object (e.g. an inode) or
	 * a raw value (e.g. a TCP port).
	 */
	union landlock_key key;
	/**
	 * @type: Type of a landlock_ruleset's root tree.
	 */
	const enum landlock_key_type type;
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
	 * @key: A union to identify either a kernel object (e.g. an inode) or
	 * a raw data value (e.g. a network socket port). This is used as a key
	 * for this ruleset element.  The pointer is set once and never
	 * modified.  It always points to an allocated object because each rule
	 * increments the refcount of its object.
	 */
	union landlock_key key;
	/**
	 * @num_layers: Number of entries in @layers.
	 */
	u32 num_layers;
	/**
	 * @layers: Stack of layers, from the latest to the newest, implemented
	 * as a flexible array member (FAM).
	 */
	struct landlock_layer layers[] __counted_by(num_layers);
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
	 * @root_inode: Root of a red-black tree containing &struct
	 * landlock_rule nodes with inode object.  Once a ruleset is tied to a
	 * process (i.e. as a domain), this tree is immutable until @usage
	 * reaches zero.
	 */
	struct rb_root root_inode;

#if IS_ENABLED(CONFIG_INET)
	/**
	 * @root_net_port: Root of a red-black tree containing &struct
	 * landlock_rule nodes with network port. Once a ruleset is tied to a
	 * process (i.e. as a domain), this tree is immutable until @usage
	 * reaches zero.
	 */
	struct rb_root root_net_port;
#endif /* IS_ENABLED(CONFIG_INET) */

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
		 * @access_masks are then unused.
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
			 * @access_masks: Contains the subset of filesystem and
			 * network actions that are restricted by a ruleset.
			 * A domain saves all layers of merged rulesets in a
			 * stack (FAM), starting from the first layer to the
			 * last one.  These layers are used when merging
			 * rulesets, for user space backward compatibility
			 * (i.e. future-proof), and to properly handle merged
			 * rulesets without overlapping access rights.  These
			 * layers are set once and never changed for the
			 * lifetime of the ruleset.
			 */
			struct access_masks access_masks[];
		};
	};
};

struct landlock_ruleset *
landlock_create_ruleset(const access_mask_t access_mask_fs,
			const access_mask_t access_mask_net,
			const access_mask_t scope_mask);

void landlock_put_ruleset(struct landlock_ruleset *const ruleset);
void landlock_put_ruleset_deferred(struct landlock_ruleset *const ruleset);

DEFINE_FREE(landlock_put_ruleset, struct landlock_ruleset *,
	    if (!IS_ERR_OR_NULL(_T)) landlock_put_ruleset(_T))

int landlock_insert_rule(struct landlock_ruleset *const ruleset,
			 const struct landlock_id id,
			 const access_mask_t access);

struct landlock_ruleset *
landlock_merge_ruleset(struct landlock_ruleset *const parent,
		       struct landlock_ruleset *const ruleset);

const struct landlock_rule *
landlock_find_rule(const struct landlock_ruleset *const ruleset,
		   const struct landlock_id id);

static inline void landlock_get_ruleset(struct landlock_ruleset *const ruleset)
{
	if (ruleset)
		refcount_inc(&ruleset->usage);
}

/**
 * landlock_union_access_masks - Return all access rights handled in the
 *				 domain
 *
 * @domain: Landlock ruleset (used as a domain)
 *
 * Returns: an access_masks result of the OR of all the domain's access masks.
 */
static inline struct access_masks
landlock_union_access_masks(const struct landlock_ruleset *const domain)
{
	union access_masks_all matches = {};
	size_t layer_level;

	for (layer_level = 0; layer_level < domain->num_layers; layer_level++) {
		union access_masks_all layer = {
			.masks = domain->access_masks[layer_level],
		};

		matches.all |= layer.all;
	}

	return matches.masks;
}

/**
 * landlock_get_applicable_domain - Return @domain if it applies to (handles)
 *				    at least one of the access rights specified
 *				    in @masks
 *
 * @domain: Landlock ruleset (used as a domain)
 * @masks: access masks
 *
 * Returns: @domain if any access rights specified in @masks is handled, or
 * NULL otherwise.
 */
static inline const struct landlock_ruleset *
landlock_get_applicable_domain(const struct landlock_ruleset *const domain,
			       const struct access_masks masks)
{
	const union access_masks_all masks_all = {
		.masks = masks,
	};
	union access_masks_all merge = {};

	if (!domain)
		return NULL;

	merge.masks = landlock_union_access_masks(domain);
	if (merge.all & masks_all.all)
		return domain;

	return NULL;
}

static inline void
landlock_add_fs_access_mask(struct landlock_ruleset *const ruleset,
			    const access_mask_t fs_access_mask,
			    const u16 layer_level)
{
	access_mask_t fs_mask = fs_access_mask & LANDLOCK_MASK_ACCESS_FS;

	/* Should already be checked in sys_landlock_create_ruleset(). */
	WARN_ON_ONCE(fs_access_mask != fs_mask);
	ruleset->access_masks[layer_level].fs |= fs_mask;
}

static inline void
landlock_add_net_access_mask(struct landlock_ruleset *const ruleset,
			     const access_mask_t net_access_mask,
			     const u16 layer_level)
{
	access_mask_t net_mask = net_access_mask & LANDLOCK_MASK_ACCESS_NET;

	/* Should already be checked in sys_landlock_create_ruleset(). */
	WARN_ON_ONCE(net_access_mask != net_mask);
	ruleset->access_masks[layer_level].net |= net_mask;
}

static inline void
landlock_add_scope_mask(struct landlock_ruleset *const ruleset,
			const access_mask_t scope_mask, const u16 layer_level)
{
	access_mask_t mask = scope_mask & LANDLOCK_MASK_SCOPE;

	/* Should already be checked in sys_landlock_create_ruleset(). */
	WARN_ON_ONCE(scope_mask != mask);
	ruleset->access_masks[layer_level].scope |= mask;
}

static inline access_mask_t
landlock_get_fs_access_mask(const struct landlock_ruleset *const ruleset,
			    const u16 layer_level)
{
	/* Handles all initially denied by default access rights. */
	return ruleset->access_masks[layer_level].fs |
	       _LANDLOCK_ACCESS_FS_INITIALLY_DENIED;
}

static inline access_mask_t
landlock_get_net_access_mask(const struct landlock_ruleset *const ruleset,
			     const u16 layer_level)
{
	return ruleset->access_masks[layer_level].net;
}

static inline access_mask_t
landlock_get_scope_mask(const struct landlock_ruleset *const ruleset,
			const u16 layer_level)
{
	return ruleset->access_masks[layer_level].scope;
}

bool landlock_unmask_layers(const struct landlock_rule *const rule,
			    const access_mask_t access_request,
			    layer_mask_t (*const layer_masks)[],
			    const size_t masks_array_size);

access_mask_t
landlock_init_layer_masks(const struct landlock_ruleset *const domain,
			  const access_mask_t access_request,
			  layer_mask_t (*const layer_masks)[],
			  const enum landlock_key_type key_type);

#endif /* _SECURITY_LANDLOCK_RULESET_H */
