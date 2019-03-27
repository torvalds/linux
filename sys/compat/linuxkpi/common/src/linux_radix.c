/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013-2018 Mellanox Technologies, Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/radix-tree.h>
#include <linux/err.h>

static MALLOC_DEFINE(M_RADIX, "radix", "Linux radix compat");

static inline unsigned long
radix_max(struct radix_tree_root *root)
{
	return ((1UL << (root->height * RADIX_TREE_MAP_SHIFT)) - 1UL);
}

static inline int
radix_pos(long id, int height)
{
	return (id >> (RADIX_TREE_MAP_SHIFT * height)) & RADIX_TREE_MAP_MASK;
}

void *
radix_tree_lookup(struct radix_tree_root *root, unsigned long index)
{
	struct radix_tree_node *node;
	void *item;
	int height;

	item = NULL;
	node = root->rnode;
	height = root->height - 1;
	if (index > radix_max(root))
		goto out;
	while (height && node)
		node = node->slots[radix_pos(index, height--)];
	if (node)
		item = node->slots[radix_pos(index, 0)];

out:
	return (item);
}

bool
radix_tree_iter_find(struct radix_tree_root *root, struct radix_tree_iter *iter,
    void ***pppslot)
{
	struct radix_tree_node *node;
	unsigned long index = iter->index;
	int height;

restart:
	node = root->rnode;
	if (node == NULL)
		return (false);
	height = root->height - 1;
	if (height == -1 || index > radix_max(root))
		return (false);
	do {
		unsigned long mask = RADIX_TREE_MAP_MASK << (RADIX_TREE_MAP_SHIFT * height);
		unsigned long step = 1UL << (RADIX_TREE_MAP_SHIFT * height);
		int pos = radix_pos(index, height);
		struct radix_tree_node *next;

		/* track last slot */
		*pppslot = node->slots + pos;

		next = node->slots[pos];
		if (next == NULL) {
			index += step;
			index &= -step;
			if ((index & mask) == 0)
				goto restart;
		} else {
			node = next;
			height--;
		}
	} while (height != -1);
	iter->index = index;
	return (true);
}

void *
radix_tree_delete(struct radix_tree_root *root, unsigned long index)
{
	struct radix_tree_node *stack[RADIX_TREE_MAX_HEIGHT];
	struct radix_tree_node *node;
	void *item;
	int height;
	int idx;

	item = NULL;
	node = root->rnode;
	height = root->height - 1;
	if (index > radix_max(root))
		goto out;
	/*
	 * Find the node and record the path in stack.
	 */
	while (height && node) {
		stack[height] = node;
		node = node->slots[radix_pos(index, height--)];
	}
	idx = radix_pos(index, 0);
	if (node)
		item = node->slots[idx];
	/*
	 * If we removed something reduce the height of the tree.
	 */
	if (item)
		for (;;) {
			node->slots[idx] = NULL;
			node->count--;
			if (node->count > 0)
				break;
			free(node, M_RADIX);
			if (node == root->rnode) {
				root->rnode = NULL;
				root->height = 0;
				break;
			}
			height++;
			node = stack[height];
			idx = radix_pos(index, height);
		}
out:
	return (item);
}

void
radix_tree_iter_delete(struct radix_tree_root *root,
    struct radix_tree_iter *iter, void **slot)
{
	radix_tree_delete(root, iter->index);
}

int
radix_tree_insert(struct radix_tree_root *root, unsigned long index, void *item)
{
	struct radix_tree_node *node;
	struct radix_tree_node *temp[RADIX_TREE_MAX_HEIGHT - 1];
	int height;
	int idx;

	/* bail out upon insertion of a NULL item */
	if (item == NULL)
		return (-EINVAL);

	/* get root node, if any */
	node = root->rnode;

	/* allocate root node, if any */
	if (node == NULL) {
		node = malloc(sizeof(*node), M_RADIX, root->gfp_mask | M_ZERO);
		if (node == NULL)
			return (-ENOMEM);
		root->rnode = node;
		root->height++;
	}

	/* expand radix tree as needed */
	while (radix_max(root) < index) {

		/* check if the radix tree is getting too big */
		if (root->height == RADIX_TREE_MAX_HEIGHT)
			return (-E2BIG);

		/*
		 * If the root radix level is not empty, we need to
		 * allocate a new radix level:
		 */
		if (node->count != 0) {
			node = malloc(sizeof(*node), M_RADIX, root->gfp_mask | M_ZERO);
			if (node == NULL)
				return (-ENOMEM);
			node->slots[0] = root->rnode;
			node->count++;
			root->rnode = node;
		}
		root->height++;
	}

	/* get radix tree height index */
	height = root->height - 1;

	/* walk down the tree until the first missing node, if any */
	for ( ; height != 0; height--) {
		idx = radix_pos(index, height);
		if (node->slots[idx] == NULL)
			break;
		node = node->slots[idx];
	}

	/* allocate the missing radix levels, if any */
	for (idx = 0; idx != height; idx++) {
		temp[idx] = malloc(sizeof(*node), M_RADIX,
		    root->gfp_mask | M_ZERO);
		if (temp[idx] == NULL) {
			while(idx--)
				free(temp[idx], M_RADIX);
			/* Check if we should free the root node as well. */
			if (root->rnode->count == 0) {
				free(root->rnode, M_RADIX);
				root->rnode = NULL;
				root->height = 0;
			}
			return (-ENOMEM);
		}
	}

	/* setup new radix levels, if any */
	for ( ; height != 0; height--) {
		idx = radix_pos(index, height);
		node->slots[idx] = temp[height - 1];
		node->count++;
		node = node->slots[idx];
	}

	/*
	 * Insert and adjust count if the item does not already exist.
	 */
	idx = radix_pos(index, 0);
	if (node->slots[idx])
		return (-EEXIST);
	node->slots[idx] = item;
	node->count++;

	return (0);
}
