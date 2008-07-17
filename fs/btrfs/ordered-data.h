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
	struct mutex mutex;
	struct rb_root tree;
	struct rb_node *last;
};

struct btrfs_sector_sum {
	u64 offset;
	u32 sum;
};

struct btrfs_ordered_sum {
	u64 file_offset;
	u64 len;
	struct list_head list;
	struct btrfs_sector_sum sums;
};

/* bits for the flags field */
#define BTRFS_ORDERED_IO_DONE 0 /* set when all the pages are written */
#define BTRFS_ORDERED_COMPLETE 1 /* set when removed from the tree */
#define BTRFS_ORDERED_START 2 /* set when tree setup */

struct btrfs_ordered_extent {
	u64 file_offset;
	u64 start;
	u64 len;
	unsigned long flags;
	atomic_t refs;
	struct list_head list;
	struct inode *inode;
	wait_queue_head_t wait;
	struct rb_node rb_node;
};


static inline int btrfs_ordered_sum_size(struct btrfs_root *root, u64 bytes)
{
	unsigned long num_sectors = (bytes + root->sectorsize - 1) /
		root->sectorsize;
	return sizeof(struct btrfs_ordered_sum) +
		num_sectors * sizeof(struct btrfs_sector_sum);
}

static inline void
btrfs_ordered_inode_tree_init(struct btrfs_ordered_inode_tree *t)
{
	mutex_init(&t->mutex);
	t->tree.rb_node = NULL;
	t->last = NULL;
}

int btrfs_put_ordered_extent(struct btrfs_ordered_extent *entry);
int btrfs_remove_ordered_extent(struct inode *inode,
				struct btrfs_ordered_extent *entry);
int btrfs_dec_test_ordered_pending(struct inode *inode,
				       u64 file_offset, u64 io_size);
int btrfs_add_ordered_extent(struct inode *inode, u64 file_offset,
			     u64 start, u64 len);
int btrfs_add_ordered_sum(struct inode *inode, struct btrfs_ordered_sum *sum);
struct btrfs_ordered_extent *btrfs_lookup_ordered_extent(struct inode *inode,
							 u64 file_offset);
void btrfs_wait_ordered_extent(struct inode *inode,
			       struct btrfs_ordered_extent *entry);
void btrfs_wait_ordered_range(struct inode *inode, u64 start, u64 len);
struct btrfs_ordered_extent *
btrfs_lookup_first_ordered_extent(struct inode * inode, u64 file_offset);
int btrfs_add_ordered_pending(struct inode *inode,
			      struct btrfs_ordered_extent *ordered,
			      u64 start, u64 len);
int btrfs_ordered_update_i_size(struct inode *inode,
				struct btrfs_ordered_extent *ordered);
int btrfs_find_ordered_sum(struct inode *inode, u64 offset, u32 *sum);
#endif
