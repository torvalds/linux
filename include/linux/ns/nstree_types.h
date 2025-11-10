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

#endif /* _LINUX_NSTREE_TYPES_H */
