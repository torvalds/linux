/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Landlock LSM - Ruleset management
 *
 * Copyright © 2016-2020 Mickaël Salaün <mic@digikod.net>
 * Copyright © 2018-2020 ANSSI
 * Copyright © 2026 Cloudflare, Inc.
 */

#ifndef _SECURITY_LANDLOCK_RULESET_H
#define _SECURITY_LANDLOCK_RULESET_H

#include <linux/cleanup.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/rbtree.h>
#include <linux/refcount.h>

#include "access.h"
#include "limits.h"
#include "object.h"

/**
 * struct landlock_layer - Access rights for a given layer
 */
struct landlock_layer {
	/**
	 * @level: Position of this layer in the layer stack.  Starts from 1.
	 */
	u8 level;
	/**
	 * @flags: Bitfield for special flags attached to this rule.
	 */
	struct {
		/**
		 * @flags.quiet: Suppresses denial logs for the object covered by
		 * this rule in this domain.  For filesystem rules, this inherits
		 * down the file hierarchy.
		 */
		u8 quiet : 1;
	} flags;
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
	 * @LANDLOCK_KEY_INODE: Type of &landlock_rules.root_inode's node keys.
	 */
	LANDLOCK_KEY_INODE = 1,
	/**
	 * @LANDLOCK_KEY_NET_PORT: Type of &landlock_rules.root_net_port's node
	 * keys.
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
 * struct landlock_rules - Red-black tree storage for Landlock rules
 *
 * This structure holds the rule trees shared by both rulesets and domains.
 */
struct landlock_rules {
	/**
	 * @root_inode: Root of a red-black tree containing &struct
	 * landlock_rule nodes with inode object.  Immutable for domains.
	 */
	struct rb_root root_inode;

#if IS_ENABLED(CONFIG_INET)
	/**
	 * @root_net_port: Root of a red-black tree containing &struct
	 * landlock_rule nodes with network port.  Immutable for domains.
	 */
	struct rb_root root_net_port;
#endif /* IS_ENABLED(CONFIG_INET) */

	/**
	 * @num_rules: Number of non-overlapping (i.e. not for the same object)
	 * rules in this tree storage.
	 */
	u32 num_rules;
};

/**
 * struct landlock_ruleset - Landlock ruleset
 *
 * This data structure must contain unique entries, be updatable, and quick to
 * match an object.
 */
struct landlock_ruleset {
	/**
	 * @rules: Red-black tree storage for rules.
	 */
	struct landlock_rules rules;
	/**
	 * @lock: Protects against concurrent modifications of @rules, if @usage
	 * is greater than zero.
	 */
	struct mutex lock;
	/**
	 * @usage: Number of file descriptors referencing this ruleset.
	 */
	refcount_t usage;

#ifdef CONFIG_TRACEPOINTS
	/**
	 * @version: Counter incremented on each successful
	 * landlock_add_rule(2), including when it only extends an existing
	 * rule's access rights.  Used by tracepoints to correlate a domain with
	 * the exact ruleset state it was created from.  Protected by @lock.
	 */
	u32 version;
	/**
	 * @id: Unique identifier for this ruleset, used for tracing.
	 */
	u64 id;
#endif /* CONFIG_TRACEPOINTS */

	/**
	 * @quiet_masks: Stores the quiet flags for an unmerged ruleset.  For a
	 * merged domain, this is stored in each layer's struct
	 * landlock_hierarchy instead.
	 */
	struct access_masks quiet_masks;
	/**
	 * @handled_masks: Contains the subset of filesystem and network actions
	 * that are handled by this ruleset.
	 */
	struct access_masks handled_masks;
};

struct landlock_ruleset *
landlock_create_ruleset(const access_mask_t access_mask_fs,
			const access_mask_t access_mask_net,
			const access_mask_t scope_mask);

void landlock_put_ruleset(struct landlock_ruleset *const ruleset);

DEFINE_FREE(landlock_put_ruleset, struct landlock_ruleset *,
	    if (!IS_ERR_OR_NULL(_T)) landlock_put_ruleset(_T))

int landlock_insert_rule(struct landlock_ruleset *const ruleset,
			 const struct landlock_id id,
			 const access_mask_t access, const u32 flags);

int landlock_store_rule(struct landlock_rules *const rules,
			const struct landlock_id id,
			const struct landlock_layer (*layers)[],
			const size_t num_layers);

void landlock_free_rules(struct landlock_rules *const rules);

/**
 * landlock_get_rule_root - Get the root of a rule tree by key type
 *
 * @rules: The rules storage to look up.
 * @key_type: The type of key to select the tree for.
 *
 * Return: A pointer to the rb_root, or ERR_PTR(-EINVAL) on unknown type.
 */
static inline struct rb_root *
landlock_get_rule_root(struct landlock_rules *const rules,
		       const enum landlock_key_type key_type)
{
	switch (key_type) {
	case LANDLOCK_KEY_INODE:
		return &rules->root_inode;

#if IS_ENABLED(CONFIG_INET)
	case LANDLOCK_KEY_NET_PORT:
		return &rules->root_net_port;
#endif /* IS_ENABLED(CONFIG_INET) */

	default:
		WARN_ON_ONCE(1);
		return ERR_PTR(-EINVAL);
	}
}

static inline void landlock_get_ruleset(struct landlock_ruleset *const ruleset)
{
	if (ruleset)
		refcount_inc(&ruleset->usage);
}

#endif /* _SECURITY_LANDLOCK_RULESET_H */
