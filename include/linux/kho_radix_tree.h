/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_KHO_RADIX_TREE_H
#define _LINUX_KHO_RADIX_TREE_H

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/mutex_types.h>
#include <linux/types.h>

/**
 * DOC: Kexec Handover Radix Tree
 *
 * This is a radix tree implementation for tracking numeric keys across kexec
 * transitions. It was developed for the KHO preserved memory map but is
 * designed for broader use by any subsystem that needs to track keys.
 * Conceptually speaking, the data structure is similar to a set. It tracks the
 * presence or absence of numeric keys.
 *
 * The radix tree is a multi-level tree where leaf nodes are bitmaps
 * representing individual keys.
 *
 * For the KHO preserved memory map, to allow pages of different sizes (orders)
 * to be stored efficiently in a single tree, it uses a unique key encoding
 * scheme. Each key is an unsigned long that combines a page's physical address
 * and its order.
 *
 * Client code is responsible for allocating the root node of the tree,
 * initializing the mutex lock, and managing its lifecycle. It must use the
 * tree data structures defined in the KHO ABI,
 * `include/linux/kho/abi/kexec_handover.h`.
 */

struct kho_radix_node;

struct kho_radix_tree {
	struct kho_radix_node *root;
	struct mutex lock; /* protects the tree's structure and root pointer */
};

/**
 * struct kho_radix_walk_cb - Callbacks for KHO radix tree walk.
 * @leaf:      Called on each present key in the radix tree.
 * @node:      Called on each node of the radix tree itself. Receives the
 *             physical address of the page containing the node.
 *
 * For each callback, a return value of 0 continues the walk and a non-zero
 * return value is directly returned to the caller.
 */
struct kho_radix_walk_cb {
	int (*leaf)(unsigned long key, void *data);
	int (*node)(phys_addr_t phys, void *data);
};

#ifdef CONFIG_KEXEC_HANDOVER

int kho_radix_add_key(struct kho_radix_tree *tree, unsigned long key);
void kho_radix_del_key(struct kho_radix_tree *tree, unsigned long key);
int kho_radix_walk_tree(struct kho_radix_tree *tree,
			const struct kho_radix_walk_cb *cb, void *data);
int kho_radix_init_tree(struct kho_radix_tree *tree, struct kho_radix_node *root);
void kho_radix_destroy_tree(struct kho_radix_tree *tree);

#else  /* #ifdef CONFIG_KEXEC_HANDOVER */

static inline int kho_radix_add_key(struct kho_radix_tree *tree, unsigned long key)
{
	return -EOPNOTSUPP;
}

static inline void kho_radix_del_key(struct kho_radix_tree *tree,
				     unsigned long key) { }

static inline int kho_radix_walk_tree(struct kho_radix_tree *tree,
				      const struct kho_radix_walk_cb *cb, void *data)
{
	return -EOPNOTSUPP;
}

static inline int kho_radix_init_tree(struct kho_radix_tree *tree,
				      struct kho_radix_node *root)
{
	return 0;
}

static inline void kho_radix_destroy_tree(struct kho_radix_tree *tree) { }

#endif /* #ifdef CONFIG_KEXEC_HANDOVER */

#endif	/* _LINUX_KHO_RADIX_TREE_H */
