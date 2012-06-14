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

#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/writeback.h>
#include <linux/pagevec.h>
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

/* returns NULL if the insertion worked, or it returns the node it did find
 * in the tree
 */
static struct rb_node *tree_insert(struct rb_root *root, u64 file_offset,
				   struct rb_node *node)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	struct btrfs_ordered_extent *entry;

	while (*p) {
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

static void ordered_data_tree_panic(struct inode *inode, int errno,
					       u64 offset)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	btrfs_panic(fs_info, errno, "Inconsistency in ordered tree at offset "
		    "%llu\n", (unsigned long long)offset);
}

/*
 * look for a given offset in the tree, and if it can't be found return the
 * first lesser offset
 */
static struct rb_node *__tree_search(struct rb_root *root, u64 file_offset,
				     struct rb_node **prev_ret)
{
	struct rb_node *n = root->rb_node;
	struct rb_node *prev = NULL;
	struct rb_node *test;
	struct btrfs_ordered_extent *entry;
	struct btrfs_ordered_extent *prev_entry = NULL;

	while (n) {
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

	while (prev && file_offset >= entry_end(prev_entry)) {
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
	while (prev && file_offset < entry_end(prev_entry)) {
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

/*
 * helper to check if a given offset is inside a given entry
 */
static int offset_in_entry(struct btrfs_ordered_extent *entry, u64 file_offset)
{
	if (file_offset < entry->file_offset ||
	    entry->file_offset + entry->len <= file_offset)
		return 0;
	return 1;
}

static int range_overlaps(struct btrfs_ordered_extent *entry, u64 file_offset,
			  u64 len)
{
	if (file_offset + len <= entry->file_offset ||
	    entry->file_offset + entry->len <= file_offset)
		return 0;
	return 1;
}

/*
 * look find the first ordered struct that has this offset, otherwise
 * the first one less than this offset
 */
static inline struct rb_node *tree_search(struct btrfs_ordered_inode_tree *tree,
					  u64 file_offset)
{
	struct rb_root *root = &tree->tree;
	struct rb_node *prev = NULL;
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

/* allocate and add a new ordered_extent into the per-inode tree.
 * file_offset is the logical offset in the file
 *
 * start is the disk block number of an extent already reserved in the
 * extent allocation tree
 *
 * len is the length of the extent
 *
 * The tree is given a single reference on the ordered extent that was
 * inserted.
 */
static int __btrfs_add_ordered_extent(struct inode *inode, u64 file_offset,
				      u64 start, u64 len, u64 disk_len,
				      int type, int dio, int compress_type)
{
	struct btrfs_ordered_inode_tree *tree;
	struct rb_node *node;
	struct btrfs_ordered_extent *entry;

	tree = &BTRFS_I(inode)->ordered_tree;
	entry = kzalloc(sizeof(*entry), GFP_NOFS);
	if (!entry)
		return -ENOMEM;

	entry->file_offset = file_offset;
	entry->start = start;
	entry->len = len;
	entry->disk_len = disk_len;
	entry->bytes_left = len;
	entry->inode = igrab(inode);
	entry->compress_type = compress_type;
	if (type != BTRFS_ORDERED_IO_DONE && type != BTRFS_ORDERED_COMPLETE)
		set_bit(type, &entry->flags);

	if (dio)
		set_bit(BTRFS_ORDERED_DIRECT, &entry->flags);

	/* one ref for the tree */
	atomic_set(&entry->refs, 1);
	init_waitqueue_head(&entry->wait);
	INIT_LIST_HEAD(&entry->list);
	INIT_LIST_HEAD(&entry->root_extent_list);

	trace_btrfs_ordered_extent_add(inode, entry);

	spin_lock_irq(&tree->lock);
	node = tree_insert(&tree->tree, file_offset,
			   &entry->rb_node);
	if (node)
		ordered_data_tree_panic(inode, -EEXIST, file_offset);
	spin_unlock_irq(&tree->lock);

	spin_lock(&BTRFS_I(inode)->root->fs_info->ordered_extent_lock);
	list_add_tail(&entry->root_extent_list,
		      &BTRFS_I(inode)->root->fs_info->ordered_extents);
	spin_unlock(&BTRFS_I(inode)->root->fs_info->ordered_extent_lock);

	return 0;
}

int btrfs_add_ordered_extent(struct inode *inode, u64 file_offset,
			     u64 start, u64 len, u64 disk_len, int type)
{
	return __btrfs_add_ordered_extent(inode, file_offset, start, len,
					  disk_len, type, 0,
					  BTRFS_COMPRESS_NONE);
}

int btrfs_add_ordered_extent_dio(struct inode *inode, u64 file_offset,
				 u64 start, u64 len, u64 disk_len, int type)
{
	return __btrfs_add_ordered_extent(inode, file_offset, start, len,
					  disk_len, type, 1,
					  BTRFS_COMPRESS_NONE);
}

int btrfs_add_ordered_extent_compress(struct inode *inode, u64 file_offset,
				      u64 start, u64 len, u64 disk_len,
				      int type, int compress_type)
{
	return __btrfs_add_ordered_extent(inode, file_offset, start, len,
					  disk_len, type, 0,
					  compress_type);
}

/*
 * Add a struct btrfs_ordered_sum into the list of checksums to be inserted
 * when an ordered extent is finished.  If the list covers more than one
 * ordered extent, it is split across multiples.
 */
void btrfs_add_ordered_sum(struct inode *inode,
			   struct btrfs_ordered_extent *entry,
			   struct btrfs_ordered_sum *sum)
{
	struct btrfs_ordered_inode_tree *tree;

	tree = &BTRFS_I(inode)->ordered_tree;
	spin_lock_irq(&tree->lock);
	list_add_tail(&sum->list, &entry->list);
	spin_unlock_irq(&tree->lock);
}

/*
 * this is used to account for finished IO across a given range
 * of the file.  The IO may span ordered extents.  If
 * a given ordered_extent is completely done, 1 is returned, otherwise
 * 0.
 *
 * test_and_set_bit on a flag in the struct btrfs_ordered_extent is used
 * to make sure this function only returns 1 once for a given ordered extent.
 *
 * file_offset is updated to one byte past the range that is recorded as
 * complete.  This allows you to walk forward in the file.
 */
int btrfs_dec_test_first_ordered_pending(struct inode *inode,
				   struct btrfs_ordered_extent **cached,
				   u64 *file_offset, u64 io_size, int uptodate)
{
	struct btrfs_ordered_inode_tree *tree;
	struct rb_node *node;
	struct btrfs_ordered_extent *entry = NULL;
	int ret;
	unsigned long flags;
	u64 dec_end;
	u64 dec_start;
	u64 to_dec;

	tree = &BTRFS_I(inode)->ordered_tree;
	spin_lock_irqsave(&tree->lock, flags);
	node = tree_search(tree, *file_offset);
	if (!node) {
		ret = 1;
		goto out;
	}

	entry = rb_entry(node, struct btrfs_ordered_extent, rb_node);
	if (!offset_in_entry(entry, *file_offset)) {
		ret = 1;
		goto out;
	}

	dec_start = max(*file_offset, entry->file_offset);
	dec_end = min(*file_offset + io_size, entry->file_offset +
		      entry->len);
	*file_offset = dec_end;
	if (dec_start > dec_end) {
		printk(KERN_CRIT "bad ordering dec_start %llu end %llu\n",
		       (unsigned long long)dec_start,
		       (unsigned long long)dec_end);
	}
	to_dec = dec_end - dec_start;
	if (to_dec > entry->bytes_left) {
		printk(KERN_CRIT "bad ordered accounting left %llu size %llu\n",
		       (unsigned long long)entry->bytes_left,
		       (unsigned long long)to_dec);
	}
	entry->bytes_left -= to_dec;
	if (!uptodate)
		set_bit(BTRFS_ORDERED_IOERR, &entry->flags);

	if (entry->bytes_left == 0)
		ret = test_and_set_bit(BTRFS_ORDERED_IO_DONE, &entry->flags);
	else
		ret = 1;
out:
	if (!ret && cached && entry) {
		*cached = entry;
		atomic_inc(&entry->refs);
	}
	spin_unlock_irqrestore(&tree->lock, flags);
	return ret == 0;
}

/*
 * this is used to account for finished IO across a given range
 * of the file.  The IO should not span ordered extents.  If
 * a given ordered_extent is completely done, 1 is returned, otherwise
 * 0.
 *
 * test_and_set_bit on a flag in the struct btrfs_ordered_extent is used
 * to make sure this function only returns 1 once for a given ordered extent.
 */
int btrfs_dec_test_ordered_pending(struct inode *inode,
				   struct btrfs_ordered_extent **cached,
				   u64 file_offset, u64 io_size, int uptodate)
{
	struct btrfs_ordered_inode_tree *tree;
	struct rb_node *node;
	struct btrfs_ordered_extent *entry = NULL;
	unsigned long flags;
	int ret;

	tree = &BTRFS_I(inode)->ordered_tree;
	spin_lock_irqsave(&tree->lock, flags);
	if (cached && *cached) {
		entry = *cached;
		goto have_entry;
	}

	node = tree_search(tree, file_offset);
	if (!node) {
		ret = 1;
		goto out;
	}

	entry = rb_entry(node, struct btrfs_ordered_extent, rb_node);
have_entry:
	if (!offset_in_entry(entry, file_offset)) {
		ret = 1;
		goto out;
	}

	if (io_size > entry->bytes_left) {
		printk(KERN_CRIT "bad ordered accounting left %llu size %llu\n",
		       (unsigned long long)entry->bytes_left,
		       (unsigned long long)io_size);
	}
	entry->bytes_left -= io_size;
	if (!uptodate)
		set_bit(BTRFS_ORDERED_IOERR, &entry->flags);

	if (entry->bytes_left == 0)
		ret = test_and_set_bit(BTRFS_ORDERED_IO_DONE, &entry->flags);
	else
		ret = 1;
out:
	if (!ret && cached && entry) {
		*cached = entry;
		atomic_inc(&entry->refs);
	}
	spin_unlock_irqrestore(&tree->lock, flags);
	return ret == 0;
}

/*
 * used to drop a reference on an ordered extent.  This will free
 * the extent if the last reference is dropped
 */
void btrfs_put_ordered_extent(struct btrfs_ordered_extent *entry)
{
	struct list_head *cur;
	struct btrfs_ordered_sum *sum;

	trace_btrfs_ordered_extent_put(entry->inode, entry);

	if (atomic_dec_and_test(&entry->refs)) {
		if (entry->inode)
			btrfs_add_delayed_iput(entry->inode);
		while (!list_empty(&entry->list)) {
			cur = entry->list.next;
			sum = list_entry(cur, struct btrfs_ordered_sum, list);
			list_del(&sum->list);
			kfree(sum);
		}
		kfree(entry);
	}
}

/*
 * remove an ordered extent from the tree.  No references are dropped
 * and waiters are woken up.
 */
void btrfs_remove_ordered_extent(struct inode *inode,
				 struct btrfs_ordered_extent *entry)
{
	struct btrfs_ordered_inode_tree *tree;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct rb_node *node;

	tree = &BTRFS_I(inode)->ordered_tree;
	spin_lock_irq(&tree->lock);
	node = &entry->rb_node;
	rb_erase(node, &tree->tree);
	tree->last = NULL;
	set_bit(BTRFS_ORDERED_COMPLETE, &entry->flags);
	spin_unlock_irq(&tree->lock);

	spin_lock(&root->fs_info->ordered_extent_lock);
	list_del_init(&entry->root_extent_list);

	trace_btrfs_ordered_extent_remove(inode, entry);

	/*
	 * we have no more ordered extents for this inode and
	 * no dirty pages.  We can safely remove it from the
	 * list of ordered extents
	 */
	if (RB_EMPTY_ROOT(&tree->tree) &&
	    !mapping_tagged(inode->i_mapping, PAGECACHE_TAG_DIRTY)) {
		list_del_init(&BTRFS_I(inode)->ordered_operations);
	}
	spin_unlock(&root->fs_info->ordered_extent_lock);
	wake_up(&entry->wait);
}

/*
 * wait for all the ordered extents in a root.  This is done when balancing
 * space between drives.
 */
void btrfs_wait_ordered_extents(struct btrfs_root *root,
				int nocow_only, int delay_iput)
{
	struct list_head splice;
	struct list_head *cur;
	struct btrfs_ordered_extent *ordered;
	struct inode *inode;

	INIT_LIST_HEAD(&splice);

	spin_lock(&root->fs_info->ordered_extent_lock);
	list_splice_init(&root->fs_info->ordered_extents, &splice);
	while (!list_empty(&splice)) {
		cur = splice.next;
		ordered = list_entry(cur, struct btrfs_ordered_extent,
				     root_extent_list);
		if (nocow_only &&
		    !test_bit(BTRFS_ORDERED_NOCOW, &ordered->flags) &&
		    !test_bit(BTRFS_ORDERED_PREALLOC, &ordered->flags)) {
			list_move(&ordered->root_extent_list,
				  &root->fs_info->ordered_extents);
			cond_resched_lock(&root->fs_info->ordered_extent_lock);
			continue;
		}

		list_del_init(&ordered->root_extent_list);
		atomic_inc(&ordered->refs);

		/*
		 * the inode may be getting freed (in sys_unlink path).
		 */
		inode = igrab(ordered->inode);

		spin_unlock(&root->fs_info->ordered_extent_lock);

		if (inode) {
			btrfs_start_ordered_extent(inode, ordered, 1);
			btrfs_put_ordered_extent(ordered);
			if (delay_iput)
				btrfs_add_delayed_iput(inode);
			else
				iput(inode);
		} else {
			btrfs_put_ordered_extent(ordered);
		}

		spin_lock(&root->fs_info->ordered_extent_lock);
	}
	spin_unlock(&root->fs_info->ordered_extent_lock);
}

/*
 * this is used during transaction commit to write all the inodes
 * added to the ordered operation list.  These files must be fully on
 * disk before the transaction commits.
 *
 * we have two modes here, one is to just start the IO via filemap_flush
 * and the other is to wait for all the io.  When we wait, we have an
 * extra check to make sure the ordered operation list really is empty
 * before we return
 */
void btrfs_run_ordered_operations(struct btrfs_root *root, int wait)
{
	struct btrfs_inode *btrfs_inode;
	struct inode *inode;
	struct list_head splice;

	INIT_LIST_HEAD(&splice);

	mutex_lock(&root->fs_info->ordered_operations_mutex);
	spin_lock(&root->fs_info->ordered_extent_lock);
again:
	list_splice_init(&root->fs_info->ordered_operations, &splice);

	while (!list_empty(&splice)) {
		btrfs_inode = list_entry(splice.next, struct btrfs_inode,
				   ordered_operations);

		inode = &btrfs_inode->vfs_inode;

		list_del_init(&btrfs_inode->ordered_operations);

		/*
		 * the inode may be getting freed (in sys_unlink path).
		 */
		inode = igrab(inode);

		if (!wait && inode) {
			list_add_tail(&BTRFS_I(inode)->ordered_operations,
			      &root->fs_info->ordered_operations);
		}
		spin_unlock(&root->fs_info->ordered_extent_lock);

		if (inode) {
			if (wait)
				btrfs_wait_ordered_range(inode, 0, (u64)-1);
			else
				filemap_flush(inode->i_mapping);
			btrfs_add_delayed_iput(inode);
		}

		cond_resched();
		spin_lock(&root->fs_info->ordered_extent_lock);
	}
	if (wait && !list_empty(&root->fs_info->ordered_operations))
		goto again;

	spin_unlock(&root->fs_info->ordered_extent_lock);
	mutex_unlock(&root->fs_info->ordered_operations_mutex);
}

/*
 * Used to start IO or wait for a given ordered extent to finish.
 *
 * If wait is one, this effectively waits on page writeback for all the pages
 * in the extent, and it waits on the io completion code to insert
 * metadata into the btree corresponding to the extent
 */
void btrfs_start_ordered_extent(struct inode *inode,
				       struct btrfs_ordered_extent *entry,
				       int wait)
{
	u64 start = entry->file_offset;
	u64 end = start + entry->len - 1;

	trace_btrfs_ordered_extent_start(inode, entry);

	/*
	 * pages in the range can be dirty, clean or writeback.  We
	 * start IO on any dirty ones so the wait doesn't stall waiting
	 * for pdflush to find them
	 */
	if (!test_bit(BTRFS_ORDERED_DIRECT, &entry->flags))
		filemap_fdatawrite_range(inode->i_mapping, start, end);
	if (wait) {
		wait_event(entry->wait, test_bit(BTRFS_ORDERED_COMPLETE,
						 &entry->flags));
	}
}

/*
 * Used to wait on ordered extents across a large range of bytes.
 */
void btrfs_wait_ordered_range(struct inode *inode, u64 start, u64 len)
{
	u64 end;
	u64 orig_end;
	struct btrfs_ordered_extent *ordered;
	int found;

	if (start + len < start) {
		orig_end = INT_LIMIT(loff_t);
	} else {
		orig_end = start + len - 1;
		if (orig_end > INT_LIMIT(loff_t))
			orig_end = INT_LIMIT(loff_t);
	}

	/* start IO across the range first to instantiate any delalloc
	 * extents
	 */
	filemap_write_and_wait_range(inode->i_mapping, start, orig_end);

	end = orig_end;
	found = 0;
	while (1) {
		ordered = btrfs_lookup_first_ordered_extent(inode, end);
		if (!ordered)
			break;
		if (ordered->file_offset > orig_end) {
			btrfs_put_ordered_extent(ordered);
			break;
		}
		if (ordered->file_offset + ordered->len < start) {
			btrfs_put_ordered_extent(ordered);
			break;
		}
		found++;
		btrfs_start_ordered_extent(inode, ordered, 1);
		end = ordered->file_offset;
		btrfs_put_ordered_extent(ordered);
		if (end == 0 || end == start)
			break;
		end--;
	}
}

/*
 * find an ordered extent corresponding to file_offset.  return NULL if
 * nothing is found, otherwise take a reference on the extent and return it
 */
struct btrfs_ordered_extent *btrfs_lookup_ordered_extent(struct inode *inode,
							 u64 file_offset)
{
	struct btrfs_ordered_inode_tree *tree;
	struct rb_node *node;
	struct btrfs_ordered_extent *entry = NULL;

	tree = &BTRFS_I(inode)->ordered_tree;
	spin_lock_irq(&tree->lock);
	node = tree_search(tree, file_offset);
	if (!node)
		goto out;

	entry = rb_entry(node, struct btrfs_ordered_extent, rb_node);
	if (!offset_in_entry(entry, file_offset))
		entry = NULL;
	if (entry)
		atomic_inc(&entry->refs);
out:
	spin_unlock_irq(&tree->lock);
	return entry;
}

/* Since the DIO code tries to lock a wide area we need to look for any ordered
 * extents that exist in the range, rather than just the start of the range.
 */
struct btrfs_ordered_extent *btrfs_lookup_ordered_range(struct inode *inode,
							u64 file_offset,
							u64 len)
{
	struct btrfs_ordered_inode_tree *tree;
	struct rb_node *node;
	struct btrfs_ordered_extent *entry = NULL;

	tree = &BTRFS_I(inode)->ordered_tree;
	spin_lock_irq(&tree->lock);
	node = tree_search(tree, file_offset);
	if (!node) {
		node = tree_search(tree, file_offset + len);
		if (!node)
			goto out;
	}

	while (1) {
		entry = rb_entry(node, struct btrfs_ordered_extent, rb_node);
		if (range_overlaps(entry, file_offset, len))
			break;

		if (entry->file_offset >= file_offset + len) {
			entry = NULL;
			break;
		}
		entry = NULL;
		node = rb_next(node);
		if (!node)
			break;
	}
out:
	if (entry)
		atomic_inc(&entry->refs);
	spin_unlock_irq(&tree->lock);
	return entry;
}

/*
 * lookup and return any extent before 'file_offset'.  NULL is returned
 * if none is found
 */
struct btrfs_ordered_extent *
btrfs_lookup_first_ordered_extent(struct inode *inode, u64 file_offset)
{
	struct btrfs_ordered_inode_tree *tree;
	struct rb_node *node;
	struct btrfs_ordered_extent *entry = NULL;

	tree = &BTRFS_I(inode)->ordered_tree;
	spin_lock_irq(&tree->lock);
	node = tree_search(tree, file_offset);
	if (!node)
		goto out;

	entry = rb_entry(node, struct btrfs_ordered_extent, rb_node);
	atomic_inc(&entry->refs);
out:
	spin_unlock_irq(&tree->lock);
	return entry;
}

/*
 * After an extent is done, call this to conditionally update the on disk
 * i_size.  i_size is updated to cover any fully written part of the file.
 */
int btrfs_ordered_update_i_size(struct inode *inode, u64 offset,
				struct btrfs_ordered_extent *ordered)
{
	struct btrfs_ordered_inode_tree *tree = &BTRFS_I(inode)->ordered_tree;
	u64 disk_i_size;
	u64 new_i_size;
	u64 i_size_test;
	u64 i_size = i_size_read(inode);
	struct rb_node *node;
	struct rb_node *prev = NULL;
	struct btrfs_ordered_extent *test;
	int ret = 1;

	if (ordered)
		offset = entry_end(ordered);
	else
		offset = ALIGN(offset, BTRFS_I(inode)->root->sectorsize);

	spin_lock_irq(&tree->lock);
	disk_i_size = BTRFS_I(inode)->disk_i_size;

	/* truncate file */
	if (disk_i_size > i_size) {
		BTRFS_I(inode)->disk_i_size = i_size;
		ret = 0;
		goto out;
	}

	/*
	 * if the disk i_size is already at the inode->i_size, or
	 * this ordered extent is inside the disk i_size, we're done
	 */
	if (disk_i_size == i_size || offset <= disk_i_size) {
		goto out;
	}

	/*
	 * walk backward from this ordered extent to disk_i_size.
	 * if we find an ordered extent then we can't update disk i_size
	 * yet
	 */
	if (ordered) {
		node = rb_prev(&ordered->rb_node);
	} else {
		prev = tree_search(tree, offset);
		/*
		 * we insert file extents without involving ordered struct,
		 * so there should be no ordered struct cover this offset
		 */
		if (prev) {
			test = rb_entry(prev, struct btrfs_ordered_extent,
					rb_node);
			BUG_ON(offset_in_entry(test, offset));
		}
		node = prev;
	}
	for (; node; node = rb_prev(node)) {
		test = rb_entry(node, struct btrfs_ordered_extent, rb_node);

		/* We treat this entry as if it doesnt exist */
		if (test_bit(BTRFS_ORDERED_UPDATED_ISIZE, &test->flags))
			continue;
		if (test->file_offset + test->len <= disk_i_size)
			break;
		if (test->file_offset >= i_size)
			break;
		if (test->file_offset >= disk_i_size)
			goto out;
	}
	new_i_size = min_t(u64, offset, i_size);

	/*
	 * at this point, we know we can safely update i_size to at least
	 * the offset from this ordered extent.  But, we need to
	 * walk forward and see if ios from higher up in the file have
	 * finished.
	 */
	if (ordered) {
		node = rb_next(&ordered->rb_node);
	} else {
		if (prev)
			node = rb_next(prev);
		else
			node = rb_first(&tree->tree);
	}

	/*
	 * We are looking for an area between our current extent and the next
	 * ordered extent to update the i_size to.  There are 3 cases here
	 *
	 * 1) We don't actually have anything and we can update to i_size.
	 * 2) We have stuff but they already did their i_size update so again we
	 * can just update to i_size.
	 * 3) We have an outstanding ordered extent so the most we can update
	 * our disk_i_size to is the start of the next offset.
	 */
	i_size_test = i_size;
	for (; node; node = rb_next(node)) {
		test = rb_entry(node, struct btrfs_ordered_extent, rb_node);

		if (test_bit(BTRFS_ORDERED_UPDATED_ISIZE, &test->flags))
			continue;
		if (test->file_offset > offset) {
			i_size_test = test->file_offset;
			break;
		}
	}

	/*
	 * i_size_test is the end of a region after this ordered
	 * extent where there are no ordered extents, we can safely set
	 * disk_i_size to this.
	 */
	if (i_size_test > offset)
		new_i_size = min_t(u64, i_size_test, i_size);
	BTRFS_I(inode)->disk_i_size = new_i_size;
	ret = 0;
out:
	/*
	 * We need to do this because we can't remove ordered extents until
	 * after the i_disk_size has been updated and then the inode has been
	 * updated to reflect the change, so we need to tell anybody who finds
	 * this ordered extent that we've already done all the real work, we
	 * just haven't completed all the other work.
	 */
	if (ordered)
		set_bit(BTRFS_ORDERED_UPDATED_ISIZE, &ordered->flags);
	spin_unlock_irq(&tree->lock);
	return ret;
}

/*
 * search the ordered extents for one corresponding to 'offset' and
 * try to find a checksum.  This is used because we allow pages to
 * be reclaimed before their checksum is actually put into the btree
 */
int btrfs_find_ordered_sum(struct inode *inode, u64 offset, u64 disk_bytenr,
			   u32 *sum)
{
	struct btrfs_ordered_sum *ordered_sum;
	struct btrfs_sector_sum *sector_sums;
	struct btrfs_ordered_extent *ordered;
	struct btrfs_ordered_inode_tree *tree = &BTRFS_I(inode)->ordered_tree;
	unsigned long num_sectors;
	unsigned long i;
	u32 sectorsize = BTRFS_I(inode)->root->sectorsize;
	int ret = 1;

	ordered = btrfs_lookup_ordered_extent(inode, offset);
	if (!ordered)
		return 1;

	spin_lock_irq(&tree->lock);
	list_for_each_entry_reverse(ordered_sum, &ordered->list, list) {
		if (disk_bytenr >= ordered_sum->bytenr) {
			num_sectors = ordered_sum->len / sectorsize;
			sector_sums = ordered_sum->sums;
			for (i = 0; i < num_sectors; i++) {
				if (sector_sums[i].bytenr == disk_bytenr) {
					*sum = sector_sums[i].sum;
					ret = 0;
					goto out;
				}
			}
		}
	}
out:
	spin_unlock_irq(&tree->lock);
	btrfs_put_ordered_extent(ordered);
	return ret;
}


/*
 * add a given inode to the list of inodes that must be fully on
 * disk before a transaction commit finishes.
 *
 * This basically gives us the ext3 style data=ordered mode, and it is mostly
 * used to make sure renamed files are fully on disk.
 *
 * It is a noop if the inode is already fully on disk.
 *
 * If trans is not null, we'll do a friendly check for a transaction that
 * is already flushing things and force the IO down ourselves.
 */
void btrfs_add_ordered_operation(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root, struct inode *inode)
{
	u64 last_mod;

	last_mod = max(BTRFS_I(inode)->generation, BTRFS_I(inode)->last_trans);

	/*
	 * if this file hasn't been changed since the last transaction
	 * commit, we can safely return without doing anything
	 */
	if (last_mod < root->fs_info->last_trans_committed)
		return;

	/*
	 * the transaction is already committing.  Just start the IO and
	 * don't bother with all of this list nonsense
	 */
	if (trans && root->fs_info->running_transaction->blocked) {
		btrfs_wait_ordered_range(inode, 0, (u64)-1);
		return;
	}

	spin_lock(&root->fs_info->ordered_extent_lock);
	if (list_empty(&BTRFS_I(inode)->ordered_operations)) {
		list_add_tail(&BTRFS_I(inode)->ordered_operations,
			      &root->fs_info->ordered_operations);
	}
	spin_unlock(&root->fs_info->ordered_extent_lock);
}
