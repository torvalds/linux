// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/writeback.h>
#include <linux/sched/mm.h>
#include "misc.h"
#include "ctree.h"
#include "transaction.h"
#include "btrfs_inode.h"
#include "extent_io.h"
#include "disk-io.h"
#include "compression.h"
#include "delalloc-space.h"
#include "qgroup.h"

static struct kmem_cache *btrfs_ordered_extent_cache;

static u64 entry_end(struct btrfs_ordered_extent *entry)
{
	if (entry->file_offset + entry->num_bytes < entry->file_offset)
		return (u64)-1;
	return entry->file_offset + entry->num_bytes;
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
	    entry->file_offset + entry->num_bytes <= file_offset)
		return 0;
	return 1;
}

static int range_overlaps(struct btrfs_ordered_extent *entry, u64 file_offset,
			  u64 len)
{
	if (file_offset + len <= entry->file_offset ||
	    entry->file_offset + entry->num_bytes <= file_offset)
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

/*
 * Allocate and add a new ordered_extent into the per-inode tree.
 *
 * The tree is given a single reference on the ordered extent that was
 * inserted.
 */
static int __btrfs_add_ordered_extent(struct btrfs_inode *inode, u64 file_offset,
				      u64 disk_bytenr, u64 num_bytes,
				      u64 disk_num_bytes, int type, int dio,
				      int compress_type)
{
	struct btrfs_root *root = inode->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_ordered_inode_tree *tree = &inode->ordered_tree;
	struct rb_node *node;
	struct btrfs_ordered_extent *entry;
	int ret;

	if (type == BTRFS_ORDERED_NOCOW || type == BTRFS_ORDERED_PREALLOC) {
		/* For nocow write, we can release the qgroup rsv right now */
		ret = btrfs_qgroup_free_data(inode, NULL, file_offset, num_bytes);
		if (ret < 0)
			return ret;
		ret = 0;
	} else {
		/*
		 * The ordered extent has reserved qgroup space, release now
		 * and pass the reserved number for qgroup_record to free.
		 */
		ret = btrfs_qgroup_release_data(inode, file_offset, num_bytes);
		if (ret < 0)
			return ret;
	}
	entry = kmem_cache_zalloc(btrfs_ordered_extent_cache, GFP_NOFS);
	if (!entry)
		return -ENOMEM;

	entry->file_offset = file_offset;
	entry->disk_bytenr = disk_bytenr;
	entry->num_bytes = num_bytes;
	entry->disk_num_bytes = disk_num_bytes;
	entry->bytes_left = num_bytes;
	entry->inode = igrab(&inode->vfs_inode);
	entry->compress_type = compress_type;
	entry->truncated_len = (u64)-1;
	entry->qgroup_rsv = ret;
	if (type != BTRFS_ORDERED_IO_DONE && type != BTRFS_ORDERED_COMPLETE)
		set_bit(type, &entry->flags);

	if (dio) {
		percpu_counter_add_batch(&fs_info->dio_bytes, num_bytes,
					 fs_info->delalloc_batch);
		set_bit(BTRFS_ORDERED_DIRECT, &entry->flags);
	}

	/* one ref for the tree */
	refcount_set(&entry->refs, 1);
	init_waitqueue_head(&entry->wait);
	INIT_LIST_HEAD(&entry->list);
	INIT_LIST_HEAD(&entry->log_list);
	INIT_LIST_HEAD(&entry->root_extent_list);
	INIT_LIST_HEAD(&entry->work_list);
	init_completion(&entry->completion);

	trace_btrfs_ordered_extent_add(inode, entry);

	spin_lock_irq(&tree->lock);
	node = tree_insert(&tree->tree, file_offset,
			   &entry->rb_node);
	if (node)
		btrfs_panic(fs_info, -EEXIST,
				"inconsistency in ordered tree at offset %llu",
				file_offset);
	spin_unlock_irq(&tree->lock);

	spin_lock(&root->ordered_extent_lock);
	list_add_tail(&entry->root_extent_list,
		      &root->ordered_extents);
	root->nr_ordered_extents++;
	if (root->nr_ordered_extents == 1) {
		spin_lock(&fs_info->ordered_root_lock);
		BUG_ON(!list_empty(&root->ordered_root));
		list_add_tail(&root->ordered_root, &fs_info->ordered_roots);
		spin_unlock(&fs_info->ordered_root_lock);
	}
	spin_unlock(&root->ordered_extent_lock);

	/*
	 * We don't need the count_max_extents here, we can assume that all of
	 * that work has been done at higher layers, so this is truly the
	 * smallest the extent is going to get.
	 */
	spin_lock(&inode->lock);
	btrfs_mod_outstanding_extents(inode, 1);
	spin_unlock(&inode->lock);

	return 0;
}

int btrfs_add_ordered_extent(struct btrfs_inode *inode, u64 file_offset,
			     u64 disk_bytenr, u64 num_bytes, u64 disk_num_bytes,
			     int type)
{
	return __btrfs_add_ordered_extent(inode, file_offset, disk_bytenr,
					  num_bytes, disk_num_bytes, type, 0,
					  BTRFS_COMPRESS_NONE);
}

int btrfs_add_ordered_extent_dio(struct btrfs_inode *inode, u64 file_offset,
				 u64 disk_bytenr, u64 num_bytes,
				 u64 disk_num_bytes, int type)
{
	return __btrfs_add_ordered_extent(inode, file_offset, disk_bytenr,
					  num_bytes, disk_num_bytes, type, 1,
					  BTRFS_COMPRESS_NONE);
}

int btrfs_add_ordered_extent_compress(struct btrfs_inode *inode, u64 file_offset,
				      u64 disk_bytenr, u64 num_bytes,
				      u64 disk_num_bytes, int type,
				      int compress_type)
{
	return __btrfs_add_ordered_extent(inode, file_offset, disk_bytenr,
					  num_bytes, disk_num_bytes, type, 0,
					  compress_type);
}

/*
 * Add a struct btrfs_ordered_sum into the list of checksums to be inserted
 * when an ordered extent is finished.  If the list covers more than one
 * ordered extent, it is split across multiples.
 */
void btrfs_add_ordered_sum(struct btrfs_ordered_extent *entry,
			   struct btrfs_ordered_sum *sum)
{
	struct btrfs_ordered_inode_tree *tree;

	tree = &BTRFS_I(entry->inode)->ordered_tree;
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
int btrfs_dec_test_first_ordered_pending(struct btrfs_inode *inode,
				   struct btrfs_ordered_extent **cached,
				   u64 *file_offset, u64 io_size, int uptodate)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	struct btrfs_ordered_inode_tree *tree = &inode->ordered_tree;
	struct rb_node *node;
	struct btrfs_ordered_extent *entry = NULL;
	int ret;
	unsigned long flags;
	u64 dec_end;
	u64 dec_start;
	u64 to_dec;

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
	dec_end = min(*file_offset + io_size,
		      entry->file_offset + entry->num_bytes);
	*file_offset = dec_end;
	if (dec_start > dec_end) {
		btrfs_crit(fs_info, "bad ordering dec_start %llu end %llu",
			   dec_start, dec_end);
	}
	to_dec = dec_end - dec_start;
	if (to_dec > entry->bytes_left) {
		btrfs_crit(fs_info,
			   "bad ordered accounting left %llu size %llu",
			   entry->bytes_left, to_dec);
	}
	entry->bytes_left -= to_dec;
	if (!uptodate)
		set_bit(BTRFS_ORDERED_IOERR, &entry->flags);

	if (entry->bytes_left == 0) {
		ret = test_and_set_bit(BTRFS_ORDERED_IO_DONE, &entry->flags);
		/* test_and_set_bit implies a barrier */
		cond_wake_up_nomb(&entry->wait);
	} else {
		ret = 1;
	}
out:
	if (!ret && cached && entry) {
		*cached = entry;
		refcount_inc(&entry->refs);
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
int btrfs_dec_test_ordered_pending(struct btrfs_inode *inode,
				   struct btrfs_ordered_extent **cached,
				   u64 file_offset, u64 io_size, int uptodate)
{
	struct btrfs_ordered_inode_tree *tree = &inode->ordered_tree;
	struct rb_node *node;
	struct btrfs_ordered_extent *entry = NULL;
	unsigned long flags;
	int ret;

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
		btrfs_crit(inode->root->fs_info,
			   "bad ordered accounting left %llu size %llu",
		       entry->bytes_left, io_size);
	}
	entry->bytes_left -= io_size;
	if (!uptodate)
		set_bit(BTRFS_ORDERED_IOERR, &entry->flags);

	if (entry->bytes_left == 0) {
		ret = test_and_set_bit(BTRFS_ORDERED_IO_DONE, &entry->flags);
		/* test_and_set_bit implies a barrier */
		cond_wake_up_nomb(&entry->wait);
	} else {
		ret = 1;
	}
out:
	if (!ret && cached && entry) {
		*cached = entry;
		refcount_inc(&entry->refs);
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

	trace_btrfs_ordered_extent_put(BTRFS_I(entry->inode), entry);

	if (refcount_dec_and_test(&entry->refs)) {
		ASSERT(list_empty(&entry->root_extent_list));
		ASSERT(list_empty(&entry->log_list));
		ASSERT(RB_EMPTY_NODE(&entry->rb_node));
		if (entry->inode)
			btrfs_add_delayed_iput(entry->inode);
		while (!list_empty(&entry->list)) {
			cur = entry->list.next;
			sum = list_entry(cur, struct btrfs_ordered_sum, list);
			list_del(&sum->list);
			kvfree(sum);
		}
		kmem_cache_free(btrfs_ordered_extent_cache, entry);
	}
}

/*
 * remove an ordered extent from the tree.  No references are dropped
 * and waiters are woken up.
 */
void btrfs_remove_ordered_extent(struct btrfs_inode *btrfs_inode,
				 struct btrfs_ordered_extent *entry)
{
	struct btrfs_ordered_inode_tree *tree;
	struct btrfs_root *root = btrfs_inode->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct rb_node *node;
	bool pending;

	/* This is paired with btrfs_add_ordered_extent. */
	spin_lock(&btrfs_inode->lock);
	btrfs_mod_outstanding_extents(btrfs_inode, -1);
	spin_unlock(&btrfs_inode->lock);
	if (root != fs_info->tree_root)
		btrfs_delalloc_release_metadata(btrfs_inode, entry->num_bytes,
						false);

	if (test_bit(BTRFS_ORDERED_DIRECT, &entry->flags))
		percpu_counter_add_batch(&fs_info->dio_bytes, -entry->num_bytes,
					 fs_info->delalloc_batch);

	tree = &btrfs_inode->ordered_tree;
	spin_lock_irq(&tree->lock);
	node = &entry->rb_node;
	rb_erase(node, &tree->tree);
	RB_CLEAR_NODE(node);
	if (tree->last == node)
		tree->last = NULL;
	set_bit(BTRFS_ORDERED_COMPLETE, &entry->flags);
	pending = test_and_clear_bit(BTRFS_ORDERED_PENDING, &entry->flags);
	spin_unlock_irq(&tree->lock);

	/*
	 * The current running transaction is waiting on us, we need to let it
	 * know that we're complete and wake it up.
	 */
	if (pending) {
		struct btrfs_transaction *trans;

		/*
		 * The checks for trans are just a formality, it should be set,
		 * but if it isn't we don't want to deref/assert under the spin
		 * lock, so be nice and check if trans is set, but ASSERT() so
		 * if it isn't set a developer will notice.
		 */
		spin_lock(&fs_info->trans_lock);
		trans = fs_info->running_transaction;
		if (trans)
			refcount_inc(&trans->use_count);
		spin_unlock(&fs_info->trans_lock);

		ASSERT(trans);
		if (trans) {
			if (atomic_dec_and_test(&trans->pending_ordered))
				wake_up(&trans->pending_wait);
			btrfs_put_transaction(trans);
		}
	}

	spin_lock(&root->ordered_extent_lock);
	list_del_init(&entry->root_extent_list);
	root->nr_ordered_extents--;

	trace_btrfs_ordered_extent_remove(btrfs_inode, entry);

	if (!root->nr_ordered_extents) {
		spin_lock(&fs_info->ordered_root_lock);
		BUG_ON(list_empty(&root->ordered_root));
		list_del_init(&root->ordered_root);
		spin_unlock(&fs_info->ordered_root_lock);
	}
	spin_unlock(&root->ordered_extent_lock);
	wake_up(&entry->wait);
}

static void btrfs_run_ordered_extent_work(struct btrfs_work *work)
{
	struct btrfs_ordered_extent *ordered;

	ordered = container_of(work, struct btrfs_ordered_extent, flush_work);
	btrfs_start_ordered_extent(ordered, 1);
	complete(&ordered->completion);
}

/*
 * wait for all the ordered extents in a root.  This is done when balancing
 * space between drives.
 */
u64 btrfs_wait_ordered_extents(struct btrfs_root *root, u64 nr,
			       const u64 range_start, const u64 range_len)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	LIST_HEAD(splice);
	LIST_HEAD(skipped);
	LIST_HEAD(works);
	struct btrfs_ordered_extent *ordered, *next;
	u64 count = 0;
	const u64 range_end = range_start + range_len;

	mutex_lock(&root->ordered_extent_mutex);
	spin_lock(&root->ordered_extent_lock);
	list_splice_init(&root->ordered_extents, &splice);
	while (!list_empty(&splice) && nr) {
		ordered = list_first_entry(&splice, struct btrfs_ordered_extent,
					   root_extent_list);

		if (range_end <= ordered->disk_bytenr ||
		    ordered->disk_bytenr + ordered->disk_num_bytes <= range_start) {
			list_move_tail(&ordered->root_extent_list, &skipped);
			cond_resched_lock(&root->ordered_extent_lock);
			continue;
		}

		list_move_tail(&ordered->root_extent_list,
			       &root->ordered_extents);
		refcount_inc(&ordered->refs);
		spin_unlock(&root->ordered_extent_lock);

		btrfs_init_work(&ordered->flush_work,
				btrfs_run_ordered_extent_work, NULL, NULL);
		list_add_tail(&ordered->work_list, &works);
		btrfs_queue_work(fs_info->flush_workers, &ordered->flush_work);

		cond_resched();
		spin_lock(&root->ordered_extent_lock);
		if (nr != U64_MAX)
			nr--;
		count++;
	}
	list_splice_tail(&skipped, &root->ordered_extents);
	list_splice_tail(&splice, &root->ordered_extents);
	spin_unlock(&root->ordered_extent_lock);

	list_for_each_entry_safe(ordered, next, &works, work_list) {
		list_del_init(&ordered->work_list);
		wait_for_completion(&ordered->completion);
		btrfs_put_ordered_extent(ordered);
		cond_resched();
	}
	mutex_unlock(&root->ordered_extent_mutex);

	return count;
}

void btrfs_wait_ordered_roots(struct btrfs_fs_info *fs_info, u64 nr,
			     const u64 range_start, const u64 range_len)
{
	struct btrfs_root *root;
	struct list_head splice;
	u64 done;

	INIT_LIST_HEAD(&splice);

	mutex_lock(&fs_info->ordered_operations_mutex);
	spin_lock(&fs_info->ordered_root_lock);
	list_splice_init(&fs_info->ordered_roots, &splice);
	while (!list_empty(&splice) && nr) {
		root = list_first_entry(&splice, struct btrfs_root,
					ordered_root);
		root = btrfs_grab_root(root);
		BUG_ON(!root);
		list_move_tail(&root->ordered_root,
			       &fs_info->ordered_roots);
		spin_unlock(&fs_info->ordered_root_lock);

		done = btrfs_wait_ordered_extents(root, nr,
						  range_start, range_len);
		btrfs_put_root(root);

		spin_lock(&fs_info->ordered_root_lock);
		if (nr != U64_MAX) {
			nr -= done;
		}
	}
	list_splice_tail(&splice, &fs_info->ordered_roots);
	spin_unlock(&fs_info->ordered_root_lock);
	mutex_unlock(&fs_info->ordered_operations_mutex);
}

/*
 * Used to start IO or wait for a given ordered extent to finish.
 *
 * If wait is one, this effectively waits on page writeback for all the pages
 * in the extent, and it waits on the io completion code to insert
 * metadata into the btree corresponding to the extent
 */
void btrfs_start_ordered_extent(struct btrfs_ordered_extent *entry, int wait)
{
	u64 start = entry->file_offset;
	u64 end = start + entry->num_bytes - 1;
	struct btrfs_inode *inode = BTRFS_I(entry->inode);

	trace_btrfs_ordered_extent_start(inode, entry);

	/*
	 * pages in the range can be dirty, clean or writeback.  We
	 * start IO on any dirty ones so the wait doesn't stall waiting
	 * for the flusher thread to find them
	 */
	if (!test_bit(BTRFS_ORDERED_DIRECT, &entry->flags))
		filemap_fdatawrite_range(inode->vfs_inode.i_mapping, start, end);
	if (wait) {
		wait_event(entry->wait, test_bit(BTRFS_ORDERED_COMPLETE,
						 &entry->flags));
	}
}

/*
 * Used to wait on ordered extents across a large range of bytes.
 */
int btrfs_wait_ordered_range(struct inode *inode, u64 start, u64 len)
{
	int ret = 0;
	int ret_wb = 0;
	u64 end;
	u64 orig_end;
	struct btrfs_ordered_extent *ordered;

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
	ret = btrfs_fdatawrite_range(inode, start, orig_end);
	if (ret)
		return ret;

	/*
	 * If we have a writeback error don't return immediately. Wait first
	 * for any ordered extents that haven't completed yet. This is to make
	 * sure no one can dirty the same page ranges and call writepages()
	 * before the ordered extents complete - to avoid failures (-EEXIST)
	 * when adding the new ordered extents to the ordered tree.
	 */
	ret_wb = filemap_fdatawait_range(inode->i_mapping, start, orig_end);

	end = orig_end;
	while (1) {
		ordered = btrfs_lookup_first_ordered_extent(BTRFS_I(inode), end);
		if (!ordered)
			break;
		if (ordered->file_offset > orig_end) {
			btrfs_put_ordered_extent(ordered);
			break;
		}
		if (ordered->file_offset + ordered->num_bytes <= start) {
			btrfs_put_ordered_extent(ordered);
			break;
		}
		btrfs_start_ordered_extent(ordered, 1);
		end = ordered->file_offset;
		/*
		 * If the ordered extent had an error save the error but don't
		 * exit without waiting first for all other ordered extents in
		 * the range to complete.
		 */
		if (test_bit(BTRFS_ORDERED_IOERR, &ordered->flags))
			ret = -EIO;
		btrfs_put_ordered_extent(ordered);
		if (end == 0 || end == start)
			break;
		end--;
	}
	return ret_wb ? ret_wb : ret;
}

/*
 * find an ordered extent corresponding to file_offset.  return NULL if
 * nothing is found, otherwise take a reference on the extent and return it
 */
struct btrfs_ordered_extent *btrfs_lookup_ordered_extent(struct btrfs_inode *inode,
							 u64 file_offset)
{
	struct btrfs_ordered_inode_tree *tree;
	struct rb_node *node;
	struct btrfs_ordered_extent *entry = NULL;

	tree = &inode->ordered_tree;
	spin_lock_irq(&tree->lock);
	node = tree_search(tree, file_offset);
	if (!node)
		goto out;

	entry = rb_entry(node, struct btrfs_ordered_extent, rb_node);
	if (!offset_in_entry(entry, file_offset))
		entry = NULL;
	if (entry)
		refcount_inc(&entry->refs);
out:
	spin_unlock_irq(&tree->lock);
	return entry;
}

/* Since the DIO code tries to lock a wide area we need to look for any ordered
 * extents that exist in the range, rather than just the start of the range.
 */
struct btrfs_ordered_extent *btrfs_lookup_ordered_range(
		struct btrfs_inode *inode, u64 file_offset, u64 len)
{
	struct btrfs_ordered_inode_tree *tree;
	struct rb_node *node;
	struct btrfs_ordered_extent *entry = NULL;

	tree = &inode->ordered_tree;
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
		refcount_inc(&entry->refs);
	spin_unlock_irq(&tree->lock);
	return entry;
}

/*
 * Adds all ordered extents to the given list. The list ends up sorted by the
 * file_offset of the ordered extents.
 */
void btrfs_get_ordered_extents_for_logging(struct btrfs_inode *inode,
					   struct list_head *list)
{
	struct btrfs_ordered_inode_tree *tree = &inode->ordered_tree;
	struct rb_node *n;

	ASSERT(inode_is_locked(&inode->vfs_inode));

	spin_lock_irq(&tree->lock);
	for (n = rb_first(&tree->tree); n; n = rb_next(n)) {
		struct btrfs_ordered_extent *ordered;

		ordered = rb_entry(n, struct btrfs_ordered_extent, rb_node);

		if (test_bit(BTRFS_ORDERED_LOGGED, &ordered->flags))
			continue;

		ASSERT(list_empty(&ordered->log_list));
		list_add_tail(&ordered->log_list, list);
		refcount_inc(&ordered->refs);
	}
	spin_unlock_irq(&tree->lock);
}

/*
 * lookup and return any extent before 'file_offset'.  NULL is returned
 * if none is found
 */
struct btrfs_ordered_extent *
btrfs_lookup_first_ordered_extent(struct btrfs_inode *inode, u64 file_offset)
{
	struct btrfs_ordered_inode_tree *tree;
	struct rb_node *node;
	struct btrfs_ordered_extent *entry = NULL;

	tree = &inode->ordered_tree;
	spin_lock_irq(&tree->lock);
	node = tree_search(tree, file_offset);
	if (!node)
		goto out;

	entry = rb_entry(node, struct btrfs_ordered_extent, rb_node);
	refcount_inc(&entry->refs);
out:
	spin_unlock_irq(&tree->lock);
	return entry;
}

/*
 * search the ordered extents for one corresponding to 'offset' and
 * try to find a checksum.  This is used because we allow pages to
 * be reclaimed before their checksum is actually put into the btree
 */
int btrfs_find_ordered_sum(struct btrfs_inode *inode, u64 offset,
			   u64 disk_bytenr, u8 *sum, int len)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	struct btrfs_ordered_sum *ordered_sum;
	struct btrfs_ordered_extent *ordered;
	struct btrfs_ordered_inode_tree *tree = &inode->ordered_tree;
	unsigned long num_sectors;
	unsigned long i;
	u32 sectorsize = btrfs_inode_sectorsize(inode);
	const u8 blocksize_bits = inode->vfs_inode.i_sb->s_blocksize_bits;
	const u16 csum_size = btrfs_super_csum_size(fs_info->super_copy);
	int index = 0;

	ordered = btrfs_lookup_ordered_extent(inode, offset);
	if (!ordered)
		return 0;

	spin_lock_irq(&tree->lock);
	list_for_each_entry_reverse(ordered_sum, &ordered->list, list) {
		if (disk_bytenr >= ordered_sum->bytenr &&
		    disk_bytenr < ordered_sum->bytenr + ordered_sum->len) {
			i = (disk_bytenr - ordered_sum->bytenr) >> blocksize_bits;
			num_sectors = ordered_sum->len >> blocksize_bits;
			num_sectors = min_t(int, len - index, num_sectors - i);
			memcpy(sum + index, ordered_sum->sums + i * csum_size,
			       num_sectors * csum_size);

			index += (int)num_sectors * csum_size;
			if (index == len)
				goto out;
			disk_bytenr += num_sectors * sectorsize;
		}
	}
out:
	spin_unlock_irq(&tree->lock);
	btrfs_put_ordered_extent(ordered);
	return index;
}

/*
 * btrfs_flush_ordered_range - Lock the passed range and ensures all pending
 * ordered extents in it are run to completion.
 *
 * @inode:        Inode whose ordered tree is to be searched
 * @start:        Beginning of range to flush
 * @end:          Last byte of range to lock
 * @cached_state: If passed, will return the extent state responsible for the
 * locked range. It's the caller's responsibility to free the cached state.
 *
 * This function always returns with the given range locked, ensuring after it's
 * called no order extent can be pending.
 */
void btrfs_lock_and_flush_ordered_range(struct btrfs_inode *inode, u64 start,
					u64 end,
					struct extent_state **cached_state)
{
	struct btrfs_ordered_extent *ordered;
	struct extent_state *cache = NULL;
	struct extent_state **cachedp = &cache;

	if (cached_state)
		cachedp = cached_state;

	while (1) {
		lock_extent_bits(&inode->io_tree, start, end, cachedp);
		ordered = btrfs_lookup_ordered_range(inode, start,
						     end - start + 1);
		if (!ordered) {
			/*
			 * If no external cached_state has been passed then
			 * decrement the extra ref taken for cachedp since we
			 * aren't exposing it outside of this function
			 */
			if (!cached_state)
				refcount_dec(&cache->refs);
			break;
		}
		unlock_extent_cached(&inode->io_tree, start, end, cachedp);
		btrfs_start_ordered_extent(ordered, 1);
		btrfs_put_ordered_extent(ordered);
	}
}

int __init ordered_data_init(void)
{
	btrfs_ordered_extent_cache = kmem_cache_create("btrfs_ordered_extent",
				     sizeof(struct btrfs_ordered_extent), 0,
				     SLAB_MEM_SPREAD,
				     NULL);
	if (!btrfs_ordered_extent_cache)
		return -ENOMEM;

	return 0;
}

void __cold ordered_data_exit(void)
{
	kmem_cache_destroy(btrfs_ordered_extent_cache);
}
