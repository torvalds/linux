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

#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include "ctree.h"
#include "transaction.h"
#include "btrfs_inode.h"
#include "extent_io.h"


static u64 entry_end(struct btrfs_ordered_extent *entry)
{
	if (entry->file_offset + entry->len < entry->file_offset)
		return (u64)-1;
	return entry->file_offset + entry->len;
}

static struct rb_node *tree_insert(struct rb_root *root, u64 file_offset,
				   struct rb_node *node)
{
	struct rb_node ** p = &root->rb_node;
	struct rb_node * parent = NULL;
	struct btrfs_ordered_extent *entry;

	while(*p) {
		parent = *p;
		entry = rb_entry(parent, struct btrfs_ordered_extent, rb_node);

		if (file_offset < entry->file_offset)
			p = &(*p)->rb_left;
		else if (file_offset >= entry_end(entry))
			p = &(*p)->rb_right;
		else
			return parent;
	}

	rb_link_node(node, parent, p);
	rb_insert_color(node, root);
	return NULL;
}

static struct rb_node *__tree_search(struct rb_root *root, u64 file_offset,
				     struct rb_node **prev_ret)
{
	struct rb_node * n = root->rb_node;
	struct rb_node *prev = NULL;
	struct rb_node *test;
	struct btrfs_ordered_extent *entry;
	struct btrfs_ordered_extent *prev_entry = NULL;

	while(n) {
		entry = rb_entry(n, struct btrfs_ordered_extent, rb_node);
		prev = n;
		prev_entry = entry;

		if (file_offset < entry->file_offset)
			n = n->rb_left;
		else if (file_offset >= entry_end(entry))
			n = n->rb_right;
		else
			return n;
	}
	if (!prev_ret)
		return NULL;

	while(prev && file_offset >= entry_end(prev_entry)) {
		test = rb_next(prev);
		if (!test)
			break;
		prev_entry = rb_entry(test, struct btrfs_ordered_extent,
				      rb_node);
		if (file_offset < entry_end(prev_entry))
			break;

		prev = test;
	}
	if (prev)
		prev_entry = rb_entry(prev, struct btrfs_ordered_extent,
				      rb_node);
	while(prev && file_offset < entry_end(prev_entry)) {
		test = rb_prev(prev);
		if (!test)
			break;
		prev_entry = rb_entry(test, struct btrfs_ordered_extent,
				      rb_node);
		prev = test;
	}
	*prev_ret = prev;
	return NULL;
}

static int offset_in_entry(struct btrfs_ordered_extent *entry, u64 file_offset)
{
	if (file_offset < entry->file_offset ||
	    entry->file_offset + entry->len <= file_offset)
		return 0;
	return 1;
}

static inline struct rb_node *tree_search(struct btrfs_ordered_inode_tree *tree,
					  u64 file_offset)
{
	struct rb_root *root = &tree->tree;
	struct rb_node *prev;
	struct rb_node *ret;
	struct btrfs_ordered_extent *entry;

	if (tree->last) {
		entry = rb_entry(tree->last, struct btrfs_ordered_extent,
				 rb_node);
		if (offset_in_entry(entry, file_offset))
			return tree->last;
	}
	ret = __tree_search(root, file_offset, &prev);
	if (!ret)
		ret = prev;
	if (ret)
		tree->last = ret;
	return ret;
}

int btrfs_add_ordered_extent(struct inode *inode, u64 file_offset,
			     u64 start, u64 len)
{
	struct btrfs_ordered_inode_tree *tree;
	struct rb_node *node;
	struct btrfs_ordered_extent *entry;

	tree = &BTRFS_I(inode)->ordered_tree;
	entry = kzalloc(sizeof(*entry), GFP_NOFS);
	if (!entry)
		return -ENOMEM;

	mutex_lock(&tree->mutex);
	entry->file_offset = file_offset;
	entry->start = start;
	entry->len = len;
	entry->inode = inode;
	/* one ref for the tree */
	atomic_set(&entry->refs, 1);
	init_waitqueue_head(&entry->wait);
	INIT_LIST_HEAD(&entry->list);

	node = tree_insert(&tree->tree, file_offset,
			   &entry->rb_node);
	if (node) {
		entry = rb_entry(node, struct btrfs_ordered_extent, rb_node);
		atomic_inc(&entry->refs);
	}
	set_extent_ordered(&BTRFS_I(inode)->io_tree, file_offset,
			   entry_end(entry) - 1, GFP_NOFS);

	set_bit(BTRFS_ORDERED_START, &entry->flags);
	mutex_unlock(&tree->mutex);
	BUG_ON(node);
	return 0;
}

int btrfs_add_ordered_sum(struct inode *inode, struct btrfs_ordered_sum *sum)
{
	struct btrfs_ordered_inode_tree *tree;
	struct rb_node *node;
	struct btrfs_ordered_extent *entry;

	tree = &BTRFS_I(inode)->ordered_tree;
	mutex_lock(&tree->mutex);
	node = tree_search(tree, sum->file_offset);
	if (!node) {
search_fail:
printk("add ordered sum failed to find a node for inode %lu offset %Lu\n", inode->i_ino, sum->file_offset);
		node = rb_first(&tree->tree);
		while(node) {
			entry = rb_entry(node, struct btrfs_ordered_extent, rb_node);
			printk("entry %Lu %Lu %Lu\n", entry->file_offset, entry->file_offset + entry->len, entry->start);
			node = rb_next(node);
		}
		BUG();
	}
	BUG_ON(!node);

	entry = rb_entry(node, struct btrfs_ordered_extent, rb_node);
	if (!offset_in_entry(entry, sum->file_offset)) {
		goto search_fail;
	}

	list_add_tail(&sum->list, &entry->list);
	mutex_unlock(&tree->mutex);
	return 0;
}

int btrfs_dec_test_ordered_pending(struct inode *inode,
				   u64 file_offset, u64 io_size)
{
	struct btrfs_ordered_inode_tree *tree;
	struct rb_node *node;
	struct btrfs_ordered_extent *entry;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	int ret;

	tree = &BTRFS_I(inode)->ordered_tree;
	mutex_lock(&tree->mutex);
	clear_extent_ordered(io_tree, file_offset, file_offset + io_size - 1,
			     GFP_NOFS);
	node = tree_search(tree, file_offset);
	if (!node) {
		ret = 1;
		goto out;
	}

	entry = rb_entry(node, struct btrfs_ordered_extent, rb_node);
	if (!offset_in_entry(entry, file_offset)) {
		ret = 1;
		goto out;
	}

	ret = test_range_bit(io_tree, entry->file_offset,
			     entry->file_offset + entry->len - 1,
			     EXTENT_ORDERED, 0);
	if (!test_bit(BTRFS_ORDERED_START, &entry->flags)) {
printk("inode %lu not ready yet for extent %Lu %Lu\n", inode->i_ino, entry->file_offset, entry_end(entry));
	}
	if (ret == 0)
		ret = test_and_set_bit(BTRFS_ORDERED_IO_DONE, &entry->flags);
out:
	mutex_unlock(&tree->mutex);
	return ret == 0;
}

int btrfs_put_ordered_extent(struct btrfs_ordered_extent *entry)
{
	if (atomic_dec_and_test(&entry->refs))
		kfree(entry);
	return 0;
}

int btrfs_remove_ordered_extent(struct inode *inode,
				struct btrfs_ordered_extent *entry)
{
	struct btrfs_ordered_inode_tree *tree;
	struct rb_node *node;

	tree = &BTRFS_I(inode)->ordered_tree;
	mutex_lock(&tree->mutex);
	node = &entry->rb_node;
	rb_erase(node, &tree->tree);
	tree->last = NULL;
	set_bit(BTRFS_ORDERED_COMPLETE, &entry->flags);
	mutex_unlock(&tree->mutex);
	wake_up(&entry->wait);
	return 0;
}

void btrfs_wait_ordered_extent(struct inode *inode,
			       struct btrfs_ordered_extent *entry)
{
	u64 start = entry->file_offset;
	u64 end = start + entry->len - 1;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22)
	do_sync_file_range(file, start, end, SYNC_FILE_RANGE_WRITE);
#else
	do_sync_mapping_range(inode->i_mapping, start, end,
			      SYNC_FILE_RANGE_WRITE);
#endif
	wait_event(entry->wait,
		   test_bit(BTRFS_ORDERED_COMPLETE, &entry->flags));
}

static void btrfs_start_ordered_extent(struct inode *inode,
			       struct btrfs_ordered_extent *entry, int wait)
{
	u64 start = entry->file_offset;
	u64 end = start + entry->len - 1;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22)
	do_sync_file_range(file, start, end, SYNC_FILE_RANGE_WRITE);
#else
	do_sync_mapping_range(inode->i_mapping, start, end,
			      SYNC_FILE_RANGE_WRITE);
#endif
	if (wait)
		wait_event(entry->wait, test_bit(BTRFS_ORDERED_COMPLETE,
						 &entry->flags));
}

void btrfs_wait_ordered_range(struct inode *inode, u64 start, u64 len)
{
	u64 end;
	struct btrfs_ordered_extent *ordered;
	int found;
	int should_wait = 0;

again:
	if (start + len < start)
		end = (u64)-1;
	else
		end = start + len - 1;
	found = 0;
	while(1) {
		ordered = btrfs_lookup_first_ordered_extent(inode, end);
		if (!ordered) {
			break;
		}
		if (ordered->file_offset >= start + len) {
			btrfs_put_ordered_extent(ordered);
			break;
		}
		if (ordered->file_offset + ordered->len < start) {
			btrfs_put_ordered_extent(ordered);
			break;
		}
		btrfs_start_ordered_extent(inode, ordered, should_wait);
		found++;
		end = ordered->file_offset;
		btrfs_put_ordered_extent(ordered);
		if (end == 0)
			break;
		end--;
	}
	if (should_wait && found) {
		should_wait = 0;
		goto again;
	}
}

int btrfs_add_ordered_pending(struct inode *inode,
			      struct btrfs_ordered_extent *ordered,
			      u64 start, u64 len)
{
	WARN_ON(1);
	return 0;
#if 0
	int ret;
	struct btrfs_ordered_inode_tree *tree;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;

	tree = &BTRFS_I(inode)->ordered_tree;
	mutex_lock(&tree->mutex);
	if (test_bit(BTRFS_ORDERED_IO_DONE, &ordered->flags)) {
		ret = -EAGAIN;
		goto out;
	}
	set_extent_ordered(io_tree, start, start + len - 1, GFP_NOFS);
	ret = 0;
out:
	mutex_unlock(&tree->mutex);
	return ret;
#endif
}

struct btrfs_ordered_extent *btrfs_lookup_ordered_extent(struct inode *inode,
							 u64 file_offset)
{
	struct btrfs_ordered_inode_tree *tree;
	struct rb_node *node;
	struct btrfs_ordered_extent *entry = NULL;

	tree = &BTRFS_I(inode)->ordered_tree;
	mutex_lock(&tree->mutex);
	node = tree_search(tree, file_offset);
	if (!node)
		goto out;

	entry = rb_entry(node, struct btrfs_ordered_extent, rb_node);
	if (!offset_in_entry(entry, file_offset))
		entry = NULL;
	if (entry)
		atomic_inc(&entry->refs);
out:
	mutex_unlock(&tree->mutex);
	return entry;
}

struct btrfs_ordered_extent *
btrfs_lookup_first_ordered_extent(struct inode * inode, u64 file_offset)
{
	struct btrfs_ordered_inode_tree *tree;
	struct rb_node *node;
	struct btrfs_ordered_extent *entry = NULL;

	tree = &BTRFS_I(inode)->ordered_tree;
	mutex_lock(&tree->mutex);
	node = tree_search(tree, file_offset);
	if (!node)
		goto out;

	entry = rb_entry(node, struct btrfs_ordered_extent, rb_node);
	atomic_inc(&entry->refs);
out:
	mutex_unlock(&tree->mutex);
	return entry;
}

int btrfs_ordered_update_i_size(struct inode *inode,
				struct btrfs_ordered_extent *ordered)
{
	struct btrfs_ordered_inode_tree *tree = &BTRFS_I(inode)->ordered_tree;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	u64 disk_i_size;
	u64 new_i_size;
	u64 i_size_test;
	struct rb_node *node;
	struct btrfs_ordered_extent *test;

	mutex_lock(&tree->mutex);
	disk_i_size = BTRFS_I(inode)->disk_i_size;

	/*
	 * if the disk i_size is already at the inode->i_size, or
	 * this ordered extent is inside the disk i_size, we're done
	 */
	if (disk_i_size >= inode->i_size ||
	    ordered->file_offset + ordered->len <= disk_i_size) {
		goto out;
	}

	/*
	 * we can't update the disk_isize if there are delalloc bytes
	 * between disk_i_size and  this ordered extent
	 */
	if (test_range_bit(io_tree, disk_i_size,
			   ordered->file_offset + ordered->len - 1,
			   EXTENT_DELALLOC, 0)) {
		goto out;
	}
	/*
	 * walk backward from this ordered extent to disk_i_size.
	 * if we find an ordered extent then we can't update disk i_size
	 * yet
	 */
	while(1) {
		node = rb_prev(&ordered->rb_node);
		if (!node)
			break;
		test = rb_entry(node, struct btrfs_ordered_extent, rb_node);
		if (test->file_offset + test->len <= disk_i_size)
			break;
		if (test->file_offset >= inode->i_size)
			break;
		if (test->file_offset >= disk_i_size)
			goto out;
	}
	new_i_size = min_t(u64, entry_end(ordered), i_size_read(inode));

	/*
	 * at this point, we know we can safely update i_size to at least
	 * the offset from this ordered extent.  But, we need to
	 * walk forward and see if ios from higher up in the file have
	 * finished.
	 */
	node = rb_next(&ordered->rb_node);
	i_size_test = 0;
	if (node) {
		/*
		 * do we have an area where IO might have finished
		 * between our ordered extent and the next one.
		 */
		test = rb_entry(node, struct btrfs_ordered_extent, rb_node);
		if (test->file_offset > entry_end(ordered)) {
			i_size_test = test->file_offset - 1;
		}
	} else {
		i_size_test = i_size_read(inode);
	}

	/*
	 * i_size_test is the end of a region after this ordered
	 * extent where there are no ordered extents.  As long as there
	 * are no delalloc bytes in this area, it is safe to update
	 * disk_i_size to the end of the region.
	 */
	if (i_size_test > entry_end(ordered) &&
	    !test_range_bit(io_tree, entry_end(ordered), i_size_test,
			   EXTENT_DELALLOC, 0)) {
		new_i_size = min_t(u64, i_size_test, i_size_read(inode));
	}
	BTRFS_I(inode)->disk_i_size = new_i_size;
out:
	mutex_unlock(&tree->mutex);
	return 0;
}
