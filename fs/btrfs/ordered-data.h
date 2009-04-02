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

/* one of these per inode */
struct btrfs_ordered_inode_tree {
	struct mutex mutex;
	struct rb_root tree;
	struct rb_node *last;
};

/*
 * these are used to collect checksums done just before bios submission.
 * They are attached via a list into the ordered extent, and
 * checksum items are inserted into the tree after all the blocks in
 * the ordered extent are on disk
 */
struct btrfs_sector_sum {
	/* bytenr on disk */
	u64 bytenr;
	u32 sum;
};

struct btrfs_ordered_sum {
	/* bytenr is the start of this extent on disk */
	u64 bytenr;

	/*
	 * this is the length in bytes covered by the sums array below.
	 */
	unsigned long len;
	struct list_head list;
	/* last field is a variable length array of btrfs_sector_sums */
	struct btrfs_sector_sum sums[];
};

/*
 * bits for the flags field:
 *
 * BTRFS_ORDERED_IO_DONE is set when all of the blocks are written.
 * It is used to make sure metadata is inserted into the tree only once
 * per extent.
 *
 * BTRFS_ORDERED_COMPLETE is set when the extent is removed from the
 * rbtree, just before waking any waiters.  It is used to indicate the
 * IO is done and any metadata is inserted into the tree.
 */
#define BTRFS_ORDERED_IO_DONE 0 /* set when all the pages are written */

#define BTRFS_ORDERED_COMPLETE 1 /* set when removed from the tree */

#define BTRFS_ORDERED_NOCOW 2 /* set when we want to write in place */

#define BTRFS_ORDERED_COMPRESSED 3 /* writing a compressed extent */

#define BTRFS_ORDERED_PREALLOC 4 /* set when writing to prealloced extent */

struct btrfs_ordered_extent {
	/* logical offset in the file */
	u64 file_offset;

	/* disk byte number */
	u64 start;

	/* ram length of the extent in bytes */
	u64 len;

	/* extent length on disk */
	u64 disk_len;

	/* flags (described above) */
	unsigned long flags;

	/* reference count */
	atomic_t refs;

	/* the inode we belong to */
	struct inode *inode;

	/* list of checksums for insertion when the extent io is done */
	struct list_head list;

	/* used to wait for the BTRFS_ORDERED_COMPLETE bit */
	wait_queue_head_t wait;

	/* our friendly rbtree entry */
	struct rb_node rb_node;

	/* a per root list of all the pending ordered extents */
	struct list_head root_extent_list;
};


/*
 * calculates the total size you need to allocate for an ordered sum
 * structure spanning 'bytes' in the file
 */
static inline int btrfs_ordered_sum_size(struct btrfs_root *root,
					 unsigned long bytes)
{
	unsigned long num_sectors = (bytes + root->sectorsize - 1) /
		root->sectorsize;
	num_sectors++;
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
			     u64 start, u64 len, u64 disk_len, int tyep);
int btrfs_add_ordered_sum(struct inode *inode,
			  struct btrfs_ordered_extent *entry,
			  struct btrfs_ordered_sum *sum);
struct btrfs_ordered_extent *btrfs_lookup_ordered_extent(struct inode *inode,
							 u64 file_offset);
void btrfs_start_ordered_extent(struct inode *inode,
				struct btrfs_ordered_extent *entry, int wait);
int btrfs_wait_ordered_range(struct inode *inode, u64 start, u64 len);
struct btrfs_ordered_extent *
btrfs_lookup_first_ordered_extent(struct inode * inode, u64 file_offset);
int btrfs_ordered_update_i_size(struct inode *inode,
				struct btrfs_ordered_extent *ordered);
int btrfs_find_ordered_sum(struct inode *inode, u64 offset, u64 disk_bytenr, u32 *sum);
int btrfs_wait_on_page_writeback_range(struct address_space *mapping,
				       pgoff_t start, pgoff_t end);
int btrfs_fdatawrite_range(struct address_space *mapping, loff_t start,
			   loff_t end, int sync_mode);
int btrfs_wait_ordered_extents(struct btrfs_root *root, int nocow_only);
int btrfs_run_ordered_operations(struct btrfs_root *root, int wait);
int btrfs_add_ordered_operation(struct btrfs_trans_handle *trans,
				struct btrfs_root *root,
				struct inode *inode);
#endif
