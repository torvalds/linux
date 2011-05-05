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
#ifndef __REFCACHE__
#define __REFCACHE__

struct btrfs_extent_info {
	/* bytenr and num_bytes find the extent in the extent allocation tree */
	u64 bytenr;
	u64 num_bytes;

	/* objectid and offset find the back reference for the file */
	u64 objectid;
	u64 offset;
};

struct btrfs_leaf_ref {
	struct rb_node rb_node;
	struct btrfs_leaf_ref_tree *tree;
	int in_tree;
	atomic_t usage;

	u64 root_gen;
	u64 bytenr;
	u64 owner;
	u64 generation;
	int nritems;

	struct list_head list;
	struct btrfs_extent_info extents[];
};

static inline size_t btrfs_leaf_ref_size(int nr_extents)
{
	return sizeof(struct btrfs_leaf_ref) +
	       sizeof(struct btrfs_extent_info) * nr_extents;
}
#endif
