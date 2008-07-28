/*
 * Copyright (C) 2008 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/sched.h>
#include "ctree.h"
#include "ref-cache.h"
#include "transaction.h"

struct btrfs_leaf_ref *btrfs_alloc_leaf_ref(int nr_extents)
{
	struct btrfs_leaf_ref *ref;

	ref = kmalloc(btrfs_leaf_ref_size(nr_extents), GFP_NOFS);
	if (ref) {
		memset(ref, 0, sizeof(*ref));
		atomic_set(&ref->usage, 1);
	}
	return ref;
}

void btrfs_free_leaf_ref(struct btrfs_leaf_ref *ref)
{
	if (!ref)
		return;
	WARN_ON(atomic_read(&ref->usage) == 0);
	if (atomic_dec_and_test(&ref->usage)) {
		BUG_ON(ref->in_tree);
		kfree(ref);
	}
}

static int comp_keys(struct btrfs_key *k1, struct btrfs_key *k2)
{
	if (k1->objectid > k2->objectid)
		return 1;
	if (k1->objectid < k2->objectid)
		return -1;
	if (k1->type > k2->type)
		return 1;
	if (k1->type < k2->type)
		return -1;
	if (k1->offset > k2->offset)
		return 1;
	if (k1->offset < k2->offset)
		return -1;
	return 0;
}

static struct rb_node *tree_insert(struct rb_root *root, struct btrfs_key *key,
				   struct rb_node *node)
{
	struct rb_node ** p = &root->rb_node;
	struct rb_node * parent = NULL;
	struct btrfs_leaf_ref *entry;
	int ret;

	while(*p) {
		parent = *p;
		entry = rb_entry(parent, struct btrfs_leaf_ref, rb_node);
		WARN_ON(!entry->in_tree);

		ret = comp_keys(key, &entry->key);
		if (ret < 0)
			p = &(*p)->rb_left;
		else if (ret > 0)
			p = &(*p)->rb_right;
		else
			return parent;
	}
	
	entry = rb_entry(node, struct btrfs_leaf_ref, rb_node);
	entry->in_tree = 1;
	rb_link_node(node, parent, p);
	rb_insert_color(node, root);
	return NULL;
}

static struct rb_node *tree_search(struct rb_root *root, struct btrfs_key *key)
{
	struct rb_node * n = root->rb_node;
	struct btrfs_leaf_ref *entry;
	int ret;

	while(n) {
		entry = rb_entry(n, struct btrfs_leaf_ref, rb_node);
		WARN_ON(!entry->in_tree);

		ret = comp_keys(key, &entry->key);
		if (ret < 0)
			n = n->rb_left;
		else if (ret > 0)
			n = n->rb_right;
		else
			return n;
	}
	return NULL;
}

int btrfs_remove_leaf_refs(struct btrfs_root *root)
{
	struct rb_node *rb;
	struct btrfs_leaf_ref *ref = NULL;
	struct btrfs_leaf_ref_tree *tree = root->ref_tree;

	if (!tree)
		return 0;

	spin_lock(&tree->lock);
	while(!btrfs_leaf_ref_tree_empty(tree)) {
		tree->last = NULL;
		rb = rb_first(&tree->root);
		ref = rb_entry(rb, struct btrfs_leaf_ref, rb_node);
		rb_erase(&ref->rb_node, &tree->root);
		ref->in_tree = 0;

		spin_unlock(&tree->lock);

		btrfs_free_leaf_ref(ref);

		cond_resched();
		spin_lock(&tree->lock);
	}
	spin_unlock(&tree->lock);
	return 0;
}

struct btrfs_leaf_ref *btrfs_lookup_leaf_ref(struct btrfs_root *root,
					     struct btrfs_key *key)
{
	struct rb_node *rb;
	struct btrfs_leaf_ref *ref = NULL;
	struct btrfs_leaf_ref_tree *tree = root->ref_tree;

	if (!tree)
		return NULL;

	spin_lock(&tree->lock);
	if (tree->last && comp_keys(key, &tree->last->key) == 0) {
		ref = tree->last;
	} else {
		rb = tree_search(&tree->root, key);
		if (rb) {
			ref = rb_entry(rb, struct btrfs_leaf_ref, rb_node);
			tree->last = ref;
		}
	}
	if (ref)
		atomic_inc(&ref->usage);
	spin_unlock(&tree->lock);
	return ref;
}

int btrfs_add_leaf_ref(struct btrfs_root *root, struct btrfs_leaf_ref *ref)
{
	int ret = 0;
	struct rb_node *rb;
	size_t size = btrfs_leaf_ref_size(ref->nritems);
	struct btrfs_leaf_ref_tree *tree = root->ref_tree;
	struct btrfs_transaction *trans = root->fs_info->running_transaction;

	spin_lock(&tree->lock);
	rb = tree_insert(&tree->root, &ref->key, &ref->rb_node);
	if (rb) {
		ret = -EEXIST;
	} else {
		spin_lock(&root->fs_info->ref_cache_lock);
		root->fs_info->total_ref_cache_size += size;
		if (trans && tree->generation == trans->transid)
			root->fs_info->running_ref_cache_size += size;
		spin_unlock(&root->fs_info->ref_cache_lock);

		tree->last = ref;
		atomic_inc(&ref->usage);
	}
	spin_unlock(&tree->lock);
	return ret;
}

int btrfs_remove_leaf_ref(struct btrfs_root *root, struct btrfs_leaf_ref *ref)
{
	size_t size = btrfs_leaf_ref_size(ref->nritems);
	struct btrfs_leaf_ref_tree *tree = root->ref_tree;
	struct btrfs_transaction *trans = root->fs_info->running_transaction;

	BUG_ON(!ref->in_tree);
	spin_lock(&tree->lock);
	
	spin_lock(&root->fs_info->ref_cache_lock);
	root->fs_info->total_ref_cache_size -= size;
	if (trans && tree->generation == trans->transid)
		root->fs_info->running_ref_cache_size -= size;
	spin_unlock(&root->fs_info->ref_cache_lock);

	if (tree->last == ref) {
		struct rb_node *next = rb_next(&ref->rb_node);
		if (next) {
			tree->last = rb_entry(next, struct btrfs_leaf_ref,
					      rb_node);
		} else
			tree->last = NULL;
	}

	rb_erase(&ref->rb_node, &tree->root);
	ref->in_tree = 0;

	spin_unlock(&tree->lock);

	btrfs_free_leaf_ref(ref);
	return 0;
}

