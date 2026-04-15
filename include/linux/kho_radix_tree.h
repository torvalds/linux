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
 * This is a radix tree implementation for tracking physical memory pages
 * across kexec transitions. It was developed for the KHO mechanism but is
 * designed for broader use by any subsystem that needs to preserve pages.
 *
 * The radix tree is a multi-level tree where leaf nodes are bitmaps
 * representing individual pages. To allow pages of different sizes (orders)
 * to be stored efficiently in a single tree, it uses a unique key encoding
 * scheme. Each key is an unsigned long that combines a page's physical
 * address and its order.
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

typedef int (*kho_radix_tree_walk_callback_t)(phys_addr_t phys,
					      unsigned int order);

#ifdef CONFIG_KEXEC_HANDOVER

int kho_radix_add_page(struct kho_radix_tree *tree, unsigned long pfn,
		       unsigned int order);

void kho_radix_del_page(struct kho_radix_tree *tree, unsigned long pfn,
			unsigned int order);

int kho_radix_walk_tree(struct kho_radix_tree *tree,
			kho_radix_tree_walk_callback_t cb);

#else  /* #ifdef CONFIG_KEXEC_HANDOVER */

static inline int kho_radix_add_page(struct kho_radix_tree *tree, long pfn,
				     unsigned int order)
{
	return -EOPNOTSUPP;
}

static inline void kho_radix_del_page(struct kho_radix_tree *tree,
				      unsigned long pfn, unsigned int order) { }

static inline int kho_radix_walk_tree(struct kho_radix_tree *tree,
				      kho_radix_tree_walk_callback_t cb)
{
	return -EOPNOTSUPP;
}

#endif /* #ifdef CONFIG_KEXEC_HANDOVER */

#endif	/* _LINUX_KHO_RADIX_TREE_H */
