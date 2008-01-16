/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
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

#ifndef __BTRFS_ORDERED_DATA__
#define __BTRFS_ORDERED_DATA__

struct btrfs_ordered_inode_tree {
	rwlock_t lock;
	struct rb_root tree;
};

static inline void
btrfs_ordered_inode_tree_init(struct btrfs_ordered_inode_tree *t)
{
	rwlock_init(&t->lock);
	t->tree.rb_node = NULL;
}

int btrfs_add_ordered_inode(struct inode *inode);
int btrfs_find_del_first_ordered_inode(struct btrfs_ordered_inode_tree *tree,
				       u64 *root_objectid, u64 *objectid,
				       struct inode **inode);
int btrfs_find_first_ordered_inode(struct btrfs_ordered_inode_tree *tree,
				       u64 *root_objectid, u64 *objectid,
				       struct inode **inode);
int btrfs_del_ordered_inode(struct inode *inode);
#endif
