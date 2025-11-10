/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2025 Christian Brauner <brauner@kernel.org> */
#ifndef _LINUX_NSTREE_TYPES_H
#define _LINUX_NSTREE_TYPES_H

#include <linux/rbtree.h>
#include <linux/list.h>

/**
 * struct ns_tree_root - Root of a namespace tree
 * @ns_rb: Red-black tree root for efficient lookups
 * @ns_list_head: List head for sequential iteration
 *
 * Each namespace tree maintains both an rbtree (for O(log n) lookups)
 * and a list (for efficient sequential iteration). The list is kept in
 * the same sorted order as the rbtree.
 */
struct ns_tree_root {
	struct rb_root ns_rb;
	struct list_head ns_list_head;
};

/**
 * struct ns_tree_node - Node in a namespace tree
 * @ns_node: Red-black tree node
 * @ns_list_entry: List entry for sequential iteration
 *
 * Represents a namespace's position in a tree. Each namespace has
 * multiple tree nodes for different trees (unified, per-type, owner).
 */
struct ns_tree_node {
	struct rb_node ns_node;
	struct list_head ns_list_entry;
};

/**
 * struct ns_tree - Namespace tree nodes and active reference count
 * @ns_id: Unique namespace identifier
 * @__ns_ref_active: Active reference count (do not use directly)
 * @ns_unified_node: Node in the global namespace tree
 * @ns_tree_node: Node in the per-type namespace tree
 * @ns_owner_node: Node in the owner namespace's tree of owned namespaces
 * @ns_owner_root: Root of the tree of namespaces owned by this namespace
 *                 (only used when this namespace is an owner)
 */
struct ns_tree {
	u64 ns_id;
	atomic_t __ns_ref_active;
	struct ns_tree_node ns_unified_node;
	struct ns_tree_node ns_tree_node;
	struct ns_tree_node ns_owner_node;
	struct ns_tree_root ns_owner_root;
};

#endif /* _LINUX_NSTREE_TYPES_H */
