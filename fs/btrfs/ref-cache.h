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

struct btrfs_extent_info {
	u64 bytenr;
	u64 num_bytes;
	u64 objectid;
	u64 offset;
};

struct btrfs_leaf_ref {
	struct rb_node rb_node;
	struct btrfs_key key;
	int in_tree;
	atomic_t usage;

	u64 bytenr;
	u64 owner;
	u64 generation;
	int nritems;
	struct btrfs_extent_info extents[];
};

struct btrfs_leaf_ref_tree {
	struct rb_root root;
	struct btrfs_leaf_ref *last;
	u64 generation;
	spinlock_t lock;
};

static inline size_t btrfs_leaf_ref_size(int nr_extents)
{
	return sizeof(struct btrfs_leaf_ref) + 
	       sizeof(struct btrfs_extent_info) * nr_extents;
}

static inline void btrfs_leaf_ref_tree_init(struct btrfs_leaf_ref_tree *tree)
{
	tree->root.rb_node = NULL;
	tree->last = NULL;
	tree->generation = 0;
	spin_lock_init(&tree->lock);
}

static inline int btrfs_leaf_ref_tree_empty(struct btrfs_leaf_ref_tree *tree)
{
	return RB_EMPTY_ROOT(&tree->root);
}

void btrfs_leaf_ref_tree_init(struct btrfs_leaf_ref_tree *tree);
struct btrfs_leaf_ref *btrfs_alloc_leaf_ref(int nr_extents);
void btrfs_free_leaf_ref(struct btrfs_leaf_ref *ref);
struct btrfs_leaf_ref *btrfs_lookup_leaf_ref(struct btrfs_root *root,
					     struct btrfs_key *key);
int btrfs_add_leaf_ref(struct btrfs_root *root, struct btrfs_leaf_ref *ref);
int btrfs_remove_leaf_refs(struct btrfs_root *root);
int btrfs_remove_leaf_ref(struct btrfs_root *root, struct btrfs_leaf_ref *ref);
