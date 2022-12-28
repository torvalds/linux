// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/writeback.h>
#include <linux/sched/mm.h>
#include "messages.h"
#include "misc.h"
#include "ctree.h"
#include "transaction.h"
#include "btrfs_inode.h"
#include "extent_io.h"
#include "disk-io.h"
#include "compression.h"
#include "delalloc-space.h"
#include "qgroup.h"
#include "subpage.h"
#include "file.h"
#include "super.h"

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
		if (in_range(file_offset, entry->file_offset, entry->num_bytes))
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
 * Add an ordered extent to the per-inode tree.
 *
 * @inode:           Inode that this extent is for.
 * @file_offset:     Logical offset in file where the extent starts.
 * @num_bytes:       Logical length of extent in file.
 * @ram_bytes:       Full length of unencoded data.
 * @disk_bytenr:     Offset of extent on disk.
 * @disk_num_bytes:  Size of extent on disk.
 * @offset:          Offset into unencoded data where file data starts.
 * @flags:           Flags specifying type of extent (1 << BTRFS_ORDERED_*).
 * @compress_type:   Compression algorithm used for data.
 *
 * Most of these parameters correspond to &struct btrfs_file_extent_item. The
 * tree is given a single reference on the ordered extent that was inserted.
 *
 * Return: 0 or -ENOMEM.
 */
int btrfs_add_ordered_extent(struct btrfs_inode *inode, u64 file_offset,
			     u64 num_bytes, u64 ram_bytes, u64 disk_bytenr,
			     u64 disk_num_bytes, u64 offset, unsigned flags,
			     int compress_type)
{
	struct btrfs_root *root = inode->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_ordered_inode_tree *tree = &inode->ordered_tree;
	struct rb_node *node;
	struct btrfs_ordered_extent *entry;
	int ret;

	if (flags &
	    ((1 << BTRFS_ORDERED_NOCOW) | (1 << BTRFS_ORDERED_PREALLOC))) {
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
	entry->num_bytes = num_bytes;
	entry->ram_bytes = ram_bytes;
	entry->disk_bytenr = disk_bytenr;
	entry->disk_num_bytes = disk_num_bytes;
	entry->offset = offset;
	entry->bytes_left = num_bytes;
	entry->inode = igrab(&inode->vfs_inode);
	entry->compress_type = compress_type;
	entry->truncated_len = (u64)-1;
	entry->qgroup_rsv = ret;
	entry->physical = (u64)-1;

	ASSERT((flags & ~BTRFS_ORDERED_TYPE_FLAGS) == 0);
	entry->flags = flags;

	percpu_counter_add_batch(&fs_info->ordered_bytes, num_bytes,
				 fs_info->delalloc_batch);

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

static void finish_ordered_fn(struct btrfs_work *work)
{
	struct btrfs_ordered_extent *ordered_extent;

	ordered_extent = container_of(work, struct btrfs_ordered_extent, work);
	btrfs_finish_ordered_io(ordered_extent);
}

/*
 * Mark all ordered extents io inside the specified range finished.
 *
 * @page:	 The involved page for the operation.
 *		 For uncompressed buffered IO, the page status also needs to be
 *		 updated to indicate whether the pending ordered io is finished.
 *		 Can be NULL for direct IO and compressed write.
 *		 For these cases, callers are ensured they won't execute the
 *		 endio function twice.
 *
 * This function is called for endio, thus the range must have ordered
 * extent(s) covering it.
 */
void btrfs_mark_ordered_io_finished(struct btrfs_inode *inode,
				    struct page *page, u64 file_offset,
				    u64 num_bytes, bool uptodate)
{
	struct btrfs_ordered_inode_tree *tree = &inode->ordered_tree;
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	struct btrfs_workqueue *wq;
	struct rb_node *node;
	struct btrfs_ordered_extent *entry = NULL;
	unsigned long flags;
	u64 cur = file_offset;

	if (btrfs_is_free_space_inode(inode))
		wq = fs_info->endio_freespace_worker;
	else
		wq = fs_info->endio_write_workers;

	if (page)
		ASSERT(page->mapping && page_offset(page) <= file_offset &&
		       file_offset + num_bytes <= page_offset(page) + PAGE_SIZE);

	spin_lock_irqsave(&tree->lock, flags);
	while (cur < file_offset + num_bytes) {
		u64 entry_end;
		u64 end;
		u32 len;

		node = tree_search(tree, cur);
		/* No ordered extents at all */
		if (!node)
			break;

		entry = rb_entry(node, struct btrfs_ordered_extent, rb_node);
		entry_end = entry->file_offset + entry->num_bytes;
		/*
		 * |<-- OE --->|  |
		 *		  cur
		 * Go to next OE.
		 */
		if (cur >= entry_end) {
			node = rb_next(node);
			/* No more ordered extents, exit */
			if (!node)
				break;
			entry = rb_entry(node, struct btrfs_ordered_extent,
					 rb_node);

			/* Go to next ordered extent and continue */
			cur = entry->file_offset;
			continue;
		}
		/*
		 * |	|<--- OE --->|
		 * cur
		 * Go to the start of OE.
		 */
		if (cur < entry->file_offset) {
			cur = entry->file_offset;
			continue;
		}

		/*
		 * Now we are definitely inside one ordered extent.
		 *
		 * |<--- OE --->|
		 *	|
		 *	cur
		 */
		end = min(entry->file_offset + entry->num_bytes,
			  file_offset + num_bytes) - 1;
		ASSERT(end + 1 - cur < U32_MAX);
		len = end + 1 - cur;

		if (page) {
			/*
			 * Ordered (Private2) bit indicates whether we still
			 * have pending io unfinished for the ordered extent.
			 *
			 * If there's no such bit, we need to skip to next range.
			 */
			if (!btrfs_page_test_ordered(fs_info, page, cur, len)) {
				cur += len;
				continue;
			}
			btrfs_page_clear_ordered(fs_info, page, cur, len);
		}

		/* Now we're fine to update the accounting */
		if (unlikely(len > entry->bytes_left)) {
			WARN_ON(1);
			btrfs_crit(fs_info,
"bad ordered extent accounting, root=%llu ino=%llu OE offset=%llu OE len=%llu to_dec=%u left=%llu",
				   inode->root->root_key.objectid,
				   btrfs_ino(inode),
				   entry->file_offset,
				   entry->num_bytes,
				   len, entry->bytes_left);
			entry->bytes_left = 0;
		} else {
			entry->bytes_left -= len;
		}

		if (!uptodate)
			set_bit(BTRFS_ORDERED_IOERR, &entry->flags);

		/*
		 * All the IO of the ordered extent is finished, we need to queue
		 * the finish_func to be executed.
		 */
		if (entry->bytes_left == 0) {
			set_bit(BTRFS_ORDERED_IO_DONE, &entry->flags);
			cond_wake_up(&entry->wait);
			refcount_inc(&entry->refs);
			trace_btrfs_ordered_extent_mark_finished(inode, entry);
			spin_unlock_irqrestore(&tree->lock, flags);
			btrfs_init_work(&entry->work, finish_ordered_fn, NULL, NULL);
			btrfs_queue_work(wq, &entry->work);
			spin_lock_irqsave(&tree->lock, flags);
		}
		cur += len;
	}
	spin_unlock_irqrestore(&tree->lock, flags);
}

/*
 * Finish IO for one ordered extent across a given range.  The range can only
 * contain one ordered extent.
 *
 * @cached:	 The cached ordered extent. If not NULL, we can skip the tree
 *               search and use the ordered extent directly.
 * 		 Will be also used to store the finished ordered extent.
 * @file_offset: File offset for the finished IO
 * @io_size:	 Length of the finish IO range
 *
 * Return true if the ordered extent is finished in the range, and update
 * @cached.
 * Return false otherwise.
 *
 * NOTE: The range can NOT cross multiple ordered extents.
 * Thus caller should ensure the range doesn't cross ordered extents.
 */
bool btrfs_dec_test_ordered_pending(struct btrfs_inode *inode,
				    struct btrfs_ordered_extent **cached,
				    u64 file_offset, u64 io_size)
{
	struct btrfs_ordered_inode_tree *tree = &inode->ordered_tree;
	struct rb_node *node;
	struct btrfs_ordered_extent *entry = NULL;
	unsigned long flags;
	bool finished = false;

	spin_lock_irqsave(&tree->lock, flags);
	if (cached && *cached) {
		entry = *cached;
		goto have_entry;
	}

	node = tree_search(tree, file_offset);
	if (!node)
		goto out;

	entry = rb_entry(node, struct btrfs_ordered_extent, rb_node);
have_entry:
	if (!in_range(file_offset, entry->file_offset, entry->num_bytes))
		goto out;

	if (io_size > entry->bytes_left)
		btrfs_crit(inode->root->fs_info,
			   "bad ordered accounting left %llu size %llu",
		       entry->bytes_left, io_size);

	entry->bytes_left -= io_size;

	if (entry->bytes_left == 0) {
		/*
		 * Ensure only one caller can set the flag and finished_ret
		 * accordingly
		 */
		finished = !test_and_set_bit(BTRFS_ORDERED_IO_DONE, &entry->flags);
		/* test_and_set_bit implies a barrier */
		cond_wake_up_nomb(&entry->wait);
	}
out:
	if (finished && cached && entry) {
		*cached = entry;
		refcount_inc(&entry->refs);
		trace_btrfs_ordered_extent_dec_test_pending(inode, entry);
	}
	spin_unlock_irqrestore(&tree->lock, flags);
	return finished;
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
			btrfs_add_delayed_iput(BTRFS_I(entry->inode));
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
	bool freespace_inode;

	/*
	 * If this is a free space inode the thread has not acquired the ordered
	 * extents lockdep map.
	 */
	freespace_inode = btrfs_is_free_space_inode(btrfs_inode);

	btrfs_lockdep_acquire(fs_info, btrfs_trans_pending_ordered);
	/* This is paired with btrfs_add_ordered_extent. */
	spin_lock(&btrfs_inode->lock);
	btrfs_mod_outstanding_extents(btrfs_inode, -1);
	spin_unlock(&btrfs_inode->lock);
	if (root != fs_info->tree_root) {
		u64 release;

		if (test_bit(BTRFS_ORDERED_ENCODED, &entry->flags))
			release = entry->disk_num_bytes;
		else
			release = entry->num_bytes;
		btrfs_delalloc_release_metadata(btrfs_inode, release, false);
	}

	percpu_counter_add_batch(&fs_info->ordered_bytes, -entry->num_bytes,
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

	btrfs_lockdep_release(fs_info, btrfs_trans_pending_ordered);

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
	if (!freespace_inode)
		btrfs_lockdep_release(fs_info, btrfs_ordered_extent);
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
	bool freespace_inode;

	trace_btrfs_ordered_extent_start(inode, entry);

	/*
	 * If this is a free space inode do not take the ordered extents lockdep
	 * map.
	 */
	freespace_inode = btrfs_is_free_space_inode(inode);

	/*
	 * pages in the range can be dirty, clean or writeback.  We
	 * start IO on any dirty ones so the wait doesn't stall waiting
	 * for the flusher thread to find them
	 */
	if (!test_bit(BTRFS_ORDERED_DIRECT, &entry->flags))
		filemap_fdatawrite_range(inode->vfs_inode.i_mapping, start, end);
	if (wait) {
		if (!freespace_inode)
			btrfs_might_wait_for_event(inode->root->fs_info, btrfs_ordered_extent);
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
		orig_end = OFFSET_MAX;
	} else {
		orig_end = start + len - 1;
		if (orig_end > OFFSET_MAX)
			orig_end = OFFSET_MAX;
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
	unsigned long flags;

	tree = &inode->ordered_tree;
	spin_lock_irqsave(&tree->lock, flags);
	node = tree_search(tree, file_offset);
	if (!node)
		goto out;

	entry = rb_entry(node, struct btrfs_ordered_extent, rb_node);
	if (!in_range(file_offset, entry->file_offset, entry->num_bytes))
		entry = NULL;
	if (entry) {
		refcount_inc(&entry->refs);
		trace_btrfs_ordered_extent_lookup(inode, entry);
	}
out:
	spin_unlock_irqrestore(&tree->lock, flags);
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
	if (entry) {
		refcount_inc(&entry->refs);
		trace_btrfs_ordered_extent_lookup_range(inode, entry);
	}
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
		trace_btrfs_ordered_extent_lookup_for_logging(inode, ordered);
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
	trace_btrfs_ordered_extent_lookup_first(inode, entry);
out:
	spin_unlock_irq(&tree->lock);
	return entry;
}

/*
 * Lookup the first ordered extent that overlaps the range
 * [@file_offset, @file_offset + @len).
 *
 * The difference between this and btrfs_lookup_first_ordered_extent() is
 * that this one won't return any ordered extent that does not overlap the range.
 * And the difference against btrfs_lookup_ordered_extent() is, this function
 * ensures the first ordered extent gets returned.
 */
struct btrfs_ordered_extent *btrfs_lookup_first_ordered_range(
			struct btrfs_inode *inode, u64 file_offset, u64 len)
{
	struct btrfs_ordered_inode_tree *tree = &inode->ordered_tree;
	struct rb_node *node;
	struct rb_node *cur;
	struct rb_node *prev;
	struct rb_node *next;
	struct btrfs_ordered_extent *entry = NULL;

	spin_lock_irq(&tree->lock);
	node = tree->tree.rb_node;
	/*
	 * Here we don't want to use tree_search() which will use tree->last
	 * and screw up the search order.
	 * And __tree_search() can't return the adjacent ordered extents
	 * either, thus here we do our own search.
	 */
	while (node) {
		entry = rb_entry(node, struct btrfs_ordered_extent, rb_node);

		if (file_offset < entry->file_offset) {
			node = node->rb_left;
		} else if (file_offset >= entry_end(entry)) {
			node = node->rb_right;
		} else {
			/*
			 * Direct hit, got an ordered extent that starts at
			 * @file_offset
			 */
			goto out;
		}
	}
	if (!entry) {
		/* Empty tree */
		goto out;
	}

	cur = &entry->rb_node;
	/* We got an entry around @file_offset, check adjacent entries */
	if (entry->file_offset < file_offset) {
		prev = cur;
		next = rb_next(cur);
	} else {
		prev = rb_prev(cur);
		next = cur;
	}
	if (prev) {
		entry = rb_entry(prev, struct btrfs_ordered_extent, rb_node);
		if (range_overlaps(entry, file_offset, len))
			goto out;
	}
	if (next) {
		entry = rb_entry(next, struct btrfs_ordered_extent, rb_node);
		if (range_overlaps(entry, file_offset, len))
			goto out;
	}
	/* No ordered extent in the range */
	entry = NULL;
out:
	if (entry) {
		refcount_inc(&entry->refs);
		trace_btrfs_ordered_extent_lookup_first_range(inode, entry);
	}

	spin_unlock_irq(&tree->lock);
	return entry;
}

/*
 * Lock the passed range and ensures all pending ordered extents in it are run
 * to completion.
 *
 * @inode:        Inode whose ordered tree is to be searched
 * @start:        Beginning of range to flush
 * @end:          Last byte of range to lock
 * @cached_state: If passed, will return the extent state responsible for the
 *                locked range. It's the caller's responsibility to free the
 *                cached state.
 *
 * Always return with the given range locked, ensuring after it's called no
 * order extent can be pending.
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
		lock_extent(&inode->io_tree, start, end, cachedp);
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
		unlock_extent(&inode->io_tree, start, end, cachedp);
		btrfs_start_ordered_extent(ordered, 1);
		btrfs_put_ordered_extent(ordered);
	}
}

/*
 * Lock the passed range and ensure all pending ordered extents in it are run
 * to completion in nowait mode.
 *
 * Return true if btrfs_lock_ordered_range does not return any extents,
 * otherwise false.
 */
bool btrfs_try_lock_ordered_range(struct btrfs_inode *inode, u64 start, u64 end,
				  struct extent_state **cached_state)
{
	struct btrfs_ordered_extent *ordered;

	if (!try_lock_extent(&inode->io_tree, start, end, cached_state))
		return false;

	ordered = btrfs_lookup_ordered_range(inode, start, end - start + 1);
	if (!ordered)
		return true;

	btrfs_put_ordered_extent(ordered);
	unlock_extent(&inode->io_tree, start, end, cached_state);

	return false;
}


static int clone_ordered_extent(struct btrfs_ordered_extent *ordered, u64 pos,
				u64 len)
{
	struct inode *inode = ordered->inode;
	struct btrfs_fs_info *fs_info = BTRFS_I(inode)->root->fs_info;
	u64 file_offset = ordered->file_offset + pos;
	u64 disk_bytenr = ordered->disk_bytenr + pos;
	unsigned long flags = ordered->flags & BTRFS_ORDERED_TYPE_FLAGS;

	/*
	 * The splitting extent is already counted and will be added again in
	 * btrfs_add_ordered_extent_*(). Subtract len to avoid double counting.
	 */
	percpu_counter_add_batch(&fs_info->ordered_bytes, -len,
				 fs_info->delalloc_batch);
	WARN_ON_ONCE(flags & (1 << BTRFS_ORDERED_COMPRESSED));
	return btrfs_add_ordered_extent(BTRFS_I(inode), file_offset, len, len,
					disk_bytenr, len, 0, flags,
					ordered->compress_type);
}

int btrfs_split_ordered_extent(struct btrfs_ordered_extent *ordered, u64 pre,
				u64 post)
{
	struct inode *inode = ordered->inode;
	struct btrfs_ordered_inode_tree *tree = &BTRFS_I(inode)->ordered_tree;
	struct rb_node *node;
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	int ret = 0;

	trace_btrfs_ordered_extent_split(BTRFS_I(inode), ordered);

	spin_lock_irq(&tree->lock);
	/* Remove from tree once */
	node = &ordered->rb_node;
	rb_erase(node, &tree->tree);
	RB_CLEAR_NODE(node);
	if (tree->last == node)
		tree->last = NULL;

	ordered->file_offset += pre;
	ordered->disk_bytenr += pre;
	ordered->num_bytes -= (pre + post);
	ordered->disk_num_bytes -= (pre + post);
	ordered->bytes_left -= (pre + post);

	/* Re-insert the node */
	node = tree_insert(&tree->tree, ordered->file_offset, &ordered->rb_node);
	if (node)
		btrfs_panic(fs_info, -EEXIST,
			"zoned: inconsistency in ordered tree at offset %llu",
			    ordered->file_offset);

	spin_unlock_irq(&tree->lock);

	if (pre)
		ret = clone_ordered_extent(ordered, 0, pre);
	if (ret == 0 && post)
		ret = clone_ordered_extent(ordered, pre + ordered->disk_num_bytes,
					   post);

	return ret;
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
