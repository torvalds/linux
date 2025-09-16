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
#include "block-group.h"

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

static int btrfs_range_overlaps(struct btrfs_ordered_extent *entry, u64 file_offset,
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
static inline struct rb_node *ordered_tree_search(struct btrfs_inode *inode,
						  u64 file_offset)
{
	struct rb_node *prev = NULL;
	struct rb_node *ret;
	struct btrfs_ordered_extent *entry;

	if (inode->ordered_tree_last) {
		entry = rb_entry(inode->ordered_tree_last, struct btrfs_ordered_extent,
				 rb_node);
		if (in_range(file_offset, entry->file_offset, entry->num_bytes))
			return inode->ordered_tree_last;
	}
	ret = __tree_search(&inode->ordered_tree, file_offset, &prev);
	if (!ret)
		ret = prev;
	if (ret)
		inode->ordered_tree_last = ret;
	return ret;
}

static struct btrfs_ordered_extent *alloc_ordered_extent(
			struct btrfs_inode *inode, u64 file_offset, u64 num_bytes,
			u64 ram_bytes, u64 disk_bytenr, u64 disk_num_bytes,
			u64 offset, unsigned long flags, int compress_type)
{
	struct btrfs_ordered_extent *entry;
	int ret;
	u64 qgroup_rsv = 0;
	const bool is_nocow = (flags &
	       ((1U << BTRFS_ORDERED_NOCOW) | (1U << BTRFS_ORDERED_PREALLOC)));

	/*
	 * For a NOCOW write we can free the qgroup reserve right now. For a COW
	 * one we transfer the reserved space from the inode's iotree into the
	 * ordered extent by calling btrfs_qgroup_release_data() and tracking
	 * the qgroup reserved amount in the ordered extent, so that later after
	 * completing the ordered extent, when running the data delayed ref it
	 * creates, we free the reserved data with btrfs_qgroup_free_refroot().
	 */
	if (is_nocow)
		ret = btrfs_qgroup_free_data(inode, NULL, file_offset, num_bytes, &qgroup_rsv);
	else
		ret = btrfs_qgroup_release_data(inode, file_offset, num_bytes, &qgroup_rsv);

	if (ret < 0)
		return ERR_PTR(ret);

	entry = kmem_cache_zalloc(btrfs_ordered_extent_cache, GFP_NOFS);
	if (!entry) {
		entry = ERR_PTR(-ENOMEM);
		goto out;
	}

	entry->file_offset = file_offset;
	entry->num_bytes = num_bytes;
	entry->ram_bytes = ram_bytes;
	entry->disk_bytenr = disk_bytenr;
	entry->disk_num_bytes = disk_num_bytes;
	entry->offset = offset;
	entry->bytes_left = num_bytes;
	if (WARN_ON_ONCE(!igrab(&inode->vfs_inode))) {
		kmem_cache_free(btrfs_ordered_extent_cache, entry);
		entry = ERR_PTR(-ESTALE);
		goto out;
	}
	entry->inode = inode;
	entry->compress_type = compress_type;
	entry->truncated_len = (u64)-1;
	entry->qgroup_rsv = qgroup_rsv;
	entry->flags = flags;
	refcount_set(&entry->refs, 1);
	init_waitqueue_head(&entry->wait);
	INIT_LIST_HEAD(&entry->list);
	INIT_LIST_HEAD(&entry->log_list);
	INIT_LIST_HEAD(&entry->root_extent_list);
	INIT_LIST_HEAD(&entry->work_list);
	INIT_LIST_HEAD(&entry->bioc_list);
	init_completion(&entry->completion);

	/*
	 * We don't need the count_max_extents here, we can assume that all of
	 * that work has been done at higher layers, so this is truly the
	 * smallest the extent is going to get.
	 */
	spin_lock(&inode->lock);
	btrfs_mod_outstanding_extents(inode, 1);
	spin_unlock(&inode->lock);

out:
	if (IS_ERR(entry) && !is_nocow)
		btrfs_qgroup_free_refroot(inode->root->fs_info,
					  btrfs_root_id(inode->root),
					  qgroup_rsv, BTRFS_QGROUP_RSV_DATA);

	return entry;
}

static void insert_ordered_extent(struct btrfs_ordered_extent *entry)
{
	struct btrfs_inode *inode = entry->inode;
	struct btrfs_root *root = inode->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct rb_node *node;

	trace_btrfs_ordered_extent_add(inode, entry);

	percpu_counter_add_batch(&fs_info->ordered_bytes, entry->num_bytes,
				 fs_info->delalloc_batch);

	/* One ref for the tree. */
	refcount_inc(&entry->refs);

	spin_lock_irq(&inode->ordered_tree_lock);
	node = tree_insert(&inode->ordered_tree, entry->file_offset,
			   &entry->rb_node);
	if (unlikely(node))
		btrfs_panic(fs_info, -EEXIST,
				"inconsistency in ordered tree at offset %llu",
				entry->file_offset);
	spin_unlock_irq(&inode->ordered_tree_lock);

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
 * @flags:           Flags specifying type of extent (1U << BTRFS_ORDERED_*).
 * @compress_type:   Compression algorithm used for data.
 *
 * Most of these parameters correspond to &struct btrfs_file_extent_item. The
 * tree is given a single reference on the ordered extent that was inserted, and
 * the returned pointer is given a second reference.
 *
 * Return: the new ordered extent or error pointer.
 */
struct btrfs_ordered_extent *btrfs_alloc_ordered_extent(
			struct btrfs_inode *inode, u64 file_offset,
			const struct btrfs_file_extent *file_extent, unsigned long flags)
{
	struct btrfs_ordered_extent *entry;

	ASSERT((flags & ~BTRFS_ORDERED_TYPE_FLAGS) == 0);

	/*
	 * For regular writes, we just use the members in @file_extent.
	 *
	 * For NOCOW, we don't really care about the numbers except @start and
	 * file_extent->num_bytes, as we won't insert a file extent item at all.
	 *
	 * For PREALLOC, we do not use ordered extent members, but
	 * btrfs_mark_extent_written() handles everything.
	 *
	 * So here we always pass 0 as offset for NOCOW/PREALLOC ordered extents,
	 * or btrfs_split_ordered_extent() cannot handle it correctly.
	 */
	if (flags & ((1U << BTRFS_ORDERED_NOCOW) | (1U << BTRFS_ORDERED_PREALLOC)))
		entry = alloc_ordered_extent(inode, file_offset,
					     file_extent->num_bytes,
					     file_extent->num_bytes,
					     file_extent->disk_bytenr + file_extent->offset,
					     file_extent->num_bytes, 0, flags,
					     file_extent->compression);
	else
		entry = alloc_ordered_extent(inode, file_offset,
					     file_extent->num_bytes,
					     file_extent->ram_bytes,
					     file_extent->disk_bytenr,
					     file_extent->disk_num_bytes,
					     file_extent->offset, flags,
					     file_extent->compression);
	if (!IS_ERR(entry))
		insert_ordered_extent(entry);
	return entry;
}

/*
 * Add a struct btrfs_ordered_sum into the list of checksums to be inserted
 * when an ordered extent is finished.  If the list covers more than one
 * ordered extent, it is split across multiples.
 */
void btrfs_add_ordered_sum(struct btrfs_ordered_extent *entry,
			   struct btrfs_ordered_sum *sum)
{
	struct btrfs_inode *inode = entry->inode;

	spin_lock_irq(&inode->ordered_tree_lock);
	list_add_tail(&sum->list, &entry->list);
	spin_unlock_irq(&inode->ordered_tree_lock);
}

void btrfs_mark_ordered_extent_error(struct btrfs_ordered_extent *ordered)
{
	if (!test_and_set_bit(BTRFS_ORDERED_IOERR, &ordered->flags))
		mapping_set_error(ordered->inode->vfs_inode.i_mapping, -EIO);
}

static void finish_ordered_fn(struct btrfs_work *work)
{
	struct btrfs_ordered_extent *ordered_extent;

	ordered_extent = container_of(work, struct btrfs_ordered_extent, work);
	btrfs_finish_ordered_io(ordered_extent);
}

static bool can_finish_ordered_extent(struct btrfs_ordered_extent *ordered,
				      struct folio *folio, u64 file_offset,
				      u64 len, bool uptodate)
{
	struct btrfs_inode *inode = ordered->inode;
	struct btrfs_fs_info *fs_info = inode->root->fs_info;

	lockdep_assert_held(&inode->ordered_tree_lock);

	if (folio) {
		ASSERT(folio->mapping);
		ASSERT(folio_pos(folio) <= file_offset);
		ASSERT(file_offset + len <= folio_end(folio));

		/*
		 * Ordered flag indicates whether we still have
		 * pending io unfinished for the ordered extent.
		 *
		 * If it's not set, we need to skip to next range.
		 */
		if (!btrfs_folio_test_ordered(fs_info, folio, file_offset, len))
			return false;
		btrfs_folio_clear_ordered(fs_info, folio, file_offset, len);
	}

	/* Now we're fine to update the accounting. */
	if (WARN_ON_ONCE(len > ordered->bytes_left)) {
		btrfs_crit(fs_info,
"bad ordered extent accounting, root=%llu ino=%llu OE offset=%llu OE len=%llu to_dec=%llu left=%llu",
			   btrfs_root_id(inode->root), btrfs_ino(inode),
			   ordered->file_offset, ordered->num_bytes,
			   len, ordered->bytes_left);
		ordered->bytes_left = 0;
	} else {
		ordered->bytes_left -= len;
	}

	if (!uptodate)
		set_bit(BTRFS_ORDERED_IOERR, &ordered->flags);

	if (ordered->bytes_left)
		return false;

	/*
	 * All the IO of the ordered extent is finished, we need to queue
	 * the finish_func to be executed.
	 */
	set_bit(BTRFS_ORDERED_IO_DONE, &ordered->flags);
	cond_wake_up(&ordered->wait);
	refcount_inc(&ordered->refs);
	trace_btrfs_ordered_extent_mark_finished(inode, ordered);
	return true;
}

static void btrfs_queue_ordered_fn(struct btrfs_ordered_extent *ordered)
{
	struct btrfs_inode *inode = ordered->inode;
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	struct btrfs_workqueue *wq = btrfs_is_free_space_inode(inode) ?
		fs_info->endio_freespace_worker : fs_info->endio_write_workers;

	btrfs_init_work(&ordered->work, finish_ordered_fn, NULL);
	btrfs_queue_work(wq, &ordered->work);
}

void btrfs_finish_ordered_extent(struct btrfs_ordered_extent *ordered,
				 struct folio *folio, u64 file_offset, u64 len,
				 bool uptodate)
{
	struct btrfs_inode *inode = ordered->inode;
	unsigned long flags;
	bool ret;

	trace_btrfs_finish_ordered_extent(inode, file_offset, len, uptodate);

	spin_lock_irqsave(&inode->ordered_tree_lock, flags);
	ret = can_finish_ordered_extent(ordered, folio, file_offset, len,
					uptodate);
	spin_unlock_irqrestore(&inode->ordered_tree_lock, flags);

	/*
	 * If this is a COW write it means we created new extent maps for the
	 * range and they point to unwritten locations if we got an error either
	 * before submitting a bio or during IO.
	 *
	 * We have marked the ordered extent with BTRFS_ORDERED_IOERR, and we
	 * are queuing its completion below. During completion, at
	 * btrfs_finish_one_ordered(), we will drop the extent maps for the
	 * unwritten extents.
	 *
	 * However because completion runs in a work queue we can end up having
	 * a fast fsync running before that. In the case of direct IO, once we
	 * unlock the inode the fsync might start, and we queue the completion
	 * before unlocking the inode. In the case of buffered IO when writeback
	 * finishes (end_bbio_data_write()) we queue the completion, so if the
	 * writeback was triggered by a fast fsync, the fsync might start
	 * logging before ordered extent completion runs in the work queue.
	 *
	 * The fast fsync will log file extent items based on the extent maps it
	 * finds, so if by the time it collects extent maps the ordered extent
	 * completion didn't happen yet, it will log file extent items that
	 * point to unwritten extents, resulting in a corruption if a crash
	 * happens and the log tree is replayed. Note that a fast fsync does not
	 * wait for completion of ordered extents in order to reduce latency.
	 *
	 * Set a flag in the inode so that the next fast fsync will wait for
	 * ordered extents to complete before starting to log.
	 */
	if (!uptodate && !test_bit(BTRFS_ORDERED_NOCOW, &ordered->flags))
		set_bit(BTRFS_INODE_COW_WRITE_ERROR, &inode->runtime_flags);

	if (ret)
		btrfs_queue_ordered_fn(ordered);
}

/*
 * Mark all ordered extents io inside the specified range finished.
 *
 * @folio:	 The involved folio for the operation.
 *		 For uncompressed buffered IO, the folio status also needs to be
 *		 updated to indicate whether the pending ordered io is finished.
 *		 Can be NULL for direct IO and compressed write.
 *		 For these cases, callers are ensured they won't execute the
 *		 endio function twice.
 *
 * This function is called for endio, thus the range must have ordered
 * extent(s) covering it.
 */
void btrfs_mark_ordered_io_finished(struct btrfs_inode *inode,
				    struct folio *folio, u64 file_offset,
				    u64 num_bytes, bool uptodate)
{
	struct rb_node *node;
	struct btrfs_ordered_extent *entry = NULL;
	unsigned long flags;
	u64 cur = file_offset;

	trace_btrfs_writepage_end_io_hook(inode, file_offset,
					  file_offset + num_bytes - 1,
					  uptodate);

	spin_lock_irqsave(&inode->ordered_tree_lock, flags);
	while (cur < file_offset + num_bytes) {
		u64 entry_end;
		u64 end;
		u32 len;

		node = ordered_tree_search(inode, cur);
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

		if (can_finish_ordered_extent(entry, folio, cur, len, uptodate)) {
			spin_unlock_irqrestore(&inode->ordered_tree_lock, flags);
			btrfs_queue_ordered_fn(entry);
			spin_lock_irqsave(&inode->ordered_tree_lock, flags);
		}
		cur += len;
	}
	spin_unlock_irqrestore(&inode->ordered_tree_lock, flags);
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
	struct rb_node *node;
	struct btrfs_ordered_extent *entry = NULL;
	unsigned long flags;
	bool finished = false;

	spin_lock_irqsave(&inode->ordered_tree_lock, flags);
	if (cached && *cached) {
		entry = *cached;
		goto have_entry;
	}

	node = ordered_tree_search(inode, file_offset);
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
	spin_unlock_irqrestore(&inode->ordered_tree_lock, flags);
	return finished;
}

/*
 * used to drop a reference on an ordered extent.  This will free
 * the extent if the last reference is dropped
 */
void btrfs_put_ordered_extent(struct btrfs_ordered_extent *entry)
{
	trace_btrfs_ordered_extent_put(entry->inode, entry);

	if (refcount_dec_and_test(&entry->refs)) {
		struct btrfs_ordered_sum *sum;
		struct btrfs_ordered_sum *tmp;

		ASSERT(list_empty(&entry->root_extent_list));
		ASSERT(list_empty(&entry->log_list));
		ASSERT(RB_EMPTY_NODE(&entry->rb_node));
		btrfs_add_delayed_iput(entry->inode);
		list_for_each_entry_safe(sum, tmp, &entry->list, list)
			kvfree(sum);
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
	/* This is paired with alloc_ordered_extent(). */
	spin_lock(&btrfs_inode->lock);
	btrfs_mod_outstanding_extents(btrfs_inode, -1);
	spin_unlock(&btrfs_inode->lock);
	if (root != fs_info->tree_root) {
		u64 release;

		if (test_bit(BTRFS_ORDERED_ENCODED, &entry->flags))
			release = entry->disk_num_bytes;
		else
			release = entry->num_bytes;
		btrfs_delalloc_release_metadata(btrfs_inode, release,
						test_bit(BTRFS_ORDERED_IOERR,
							 &entry->flags));
	}

	percpu_counter_add_batch(&fs_info->ordered_bytes, -entry->num_bytes,
				 fs_info->delalloc_batch);

	spin_lock_irq(&btrfs_inode->ordered_tree_lock);
	node = &entry->rb_node;
	rb_erase(node, &btrfs_inode->ordered_tree);
	RB_CLEAR_NODE(node);
	if (btrfs_inode->ordered_tree_last == node)
		btrfs_inode->ordered_tree_last = NULL;
	set_bit(BTRFS_ORDERED_COMPLETE, &entry->flags);
	pending = test_and_clear_bit(BTRFS_ORDERED_PENDING, &entry->flags);
	spin_unlock_irq(&btrfs_inode->ordered_tree_lock);

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

		ASSERT(trans || BTRFS_FS_ERROR(fs_info));
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
	btrfs_start_ordered_extent(ordered);
	complete(&ordered->completion);
}

/*
 * Wait for all the ordered extents in a root. Use @bg as range or do whole
 * range if it's NULL.
 */
u64 btrfs_wait_ordered_extents(struct btrfs_root *root, u64 nr,
			       const struct btrfs_block_group *bg)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	LIST_HEAD(splice);
	LIST_HEAD(skipped);
	LIST_HEAD(works);
	struct btrfs_ordered_extent *ordered, *next;
	u64 count = 0;
	u64 range_start, range_len;
	u64 range_end;

	if (bg) {
		range_start = bg->start;
		range_len = bg->length;
	} else {
		range_start = 0;
		range_len = U64_MAX;
	}
	range_end = range_start + range_len;

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
				btrfs_run_ordered_extent_work, NULL);
		list_add_tail(&ordered->work_list, &works);
		btrfs_queue_work(fs_info->flush_workers, &ordered->flush_work);

		cond_resched();
		if (nr != U64_MAX)
			nr--;
		count++;
		spin_lock(&root->ordered_extent_lock);
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

/*
 * Wait for @nr ordered extents that intersect the @bg, or the whole range of
 * the filesystem if @bg is NULL.
 */
void btrfs_wait_ordered_roots(struct btrfs_fs_info *fs_info, u64 nr,
			      const struct btrfs_block_group *bg)
{
	struct btrfs_root *root;
	LIST_HEAD(splice);
	u64 done;

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

		done = btrfs_wait_ordered_extents(root, nr, bg);
		btrfs_put_root(root);

		if (nr != U64_MAX)
			nr -= done;

		spin_lock(&fs_info->ordered_root_lock);
	}
	list_splice_tail(&splice, &fs_info->ordered_roots);
	spin_unlock(&fs_info->ordered_root_lock);
	mutex_unlock(&fs_info->ordered_operations_mutex);
}

/*
 * Start IO and wait for a given ordered extent to finish.
 *
 * Wait on page writeback for all the pages in the extent but not in
 * [@nowriteback_start, @nowriteback_start + @nowriteback_len) and the
 * IO completion code to insert metadata into the btree corresponding to the extent.
 */
void btrfs_start_ordered_extent_nowriteback(struct btrfs_ordered_extent *entry,
					    u64 nowriteback_start, u32 nowriteback_len)
{
	u64 start = entry->file_offset;
	u64 end = start + entry->num_bytes - 1;
	struct btrfs_inode *inode = entry->inode;
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
	if (!test_bit(BTRFS_ORDERED_DIRECT, &entry->flags)) {
		if (!nowriteback_len) {
			filemap_fdatawrite_range(inode->vfs_inode.i_mapping, start, end);
		} else {
			if (start < nowriteback_start)
				filemap_fdatawrite_range(inode->vfs_inode.i_mapping, start,
							 nowriteback_start - 1);
			if (nowriteback_start + nowriteback_len < end)
				filemap_fdatawrite_range(inode->vfs_inode.i_mapping,
							 nowriteback_start + nowriteback_len,
							 end);
		}
	}

	if (!freespace_inode)
		btrfs_might_wait_for_event(inode->root->fs_info, btrfs_ordered_extent);
	wait_event(entry->wait, test_bit(BTRFS_ORDERED_COMPLETE, &entry->flags));
}

/*
 * Used to wait on ordered extents across a large range of bytes.
 */
int btrfs_wait_ordered_range(struct btrfs_inode *inode, u64 start, u64 len)
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
	ret_wb = filemap_fdatawait_range(inode->vfs_inode.i_mapping, start, orig_end);

	end = orig_end;
	while (1) {
		ordered = btrfs_lookup_first_ordered_extent(inode, end);
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
		btrfs_start_ordered_extent(ordered);
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
	struct rb_node *node;
	struct btrfs_ordered_extent *entry = NULL;
	unsigned long flags;

	spin_lock_irqsave(&inode->ordered_tree_lock, flags);
	node = ordered_tree_search(inode, file_offset);
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
	spin_unlock_irqrestore(&inode->ordered_tree_lock, flags);
	return entry;
}

/* Since the DIO code tries to lock a wide area we need to look for any ordered
 * extents that exist in the range, rather than just the start of the range.
 */
struct btrfs_ordered_extent *btrfs_lookup_ordered_range(
		struct btrfs_inode *inode, u64 file_offset, u64 len)
{
	struct rb_node *node;
	struct btrfs_ordered_extent *entry = NULL;

	spin_lock_irq(&inode->ordered_tree_lock);
	node = ordered_tree_search(inode, file_offset);
	if (!node) {
		node = ordered_tree_search(inode, file_offset + len);
		if (!node)
			goto out;
	}

	while (1) {
		entry = rb_entry(node, struct btrfs_ordered_extent, rb_node);
		if (btrfs_range_overlaps(entry, file_offset, len))
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
	spin_unlock_irq(&inode->ordered_tree_lock);
	return entry;
}

/*
 * Adds all ordered extents to the given list. The list ends up sorted by the
 * file_offset of the ordered extents.
 */
void btrfs_get_ordered_extents_for_logging(struct btrfs_inode *inode,
					   struct list_head *list)
{
	struct rb_node *n;

	btrfs_assert_inode_locked(inode);

	spin_lock_irq(&inode->ordered_tree_lock);
	for (n = rb_first(&inode->ordered_tree); n; n = rb_next(n)) {
		struct btrfs_ordered_extent *ordered;

		ordered = rb_entry(n, struct btrfs_ordered_extent, rb_node);

		if (test_bit(BTRFS_ORDERED_LOGGED, &ordered->flags))
			continue;

		ASSERT(list_empty(&ordered->log_list));
		list_add_tail(&ordered->log_list, list);
		refcount_inc(&ordered->refs);
		trace_btrfs_ordered_extent_lookup_for_logging(inode, ordered);
	}
	spin_unlock_irq(&inode->ordered_tree_lock);
}

/*
 * lookup and return any extent before 'file_offset'.  NULL is returned
 * if none is found
 */
struct btrfs_ordered_extent *
btrfs_lookup_first_ordered_extent(struct btrfs_inode *inode, u64 file_offset)
{
	struct rb_node *node;
	struct btrfs_ordered_extent *entry = NULL;

	spin_lock_irq(&inode->ordered_tree_lock);
	node = ordered_tree_search(inode, file_offset);
	if (!node)
		goto out;

	entry = rb_entry(node, struct btrfs_ordered_extent, rb_node);
	refcount_inc(&entry->refs);
	trace_btrfs_ordered_extent_lookup_first(inode, entry);
out:
	spin_unlock_irq(&inode->ordered_tree_lock);
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
	struct rb_node *node;
	struct rb_node *cur;
	struct rb_node *prev;
	struct rb_node *next;
	struct btrfs_ordered_extent *entry = NULL;

	spin_lock_irq(&inode->ordered_tree_lock);
	node = inode->ordered_tree.rb_node;
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
		if (btrfs_range_overlaps(entry, file_offset, len))
			goto out;
	}
	if (next) {
		entry = rb_entry(next, struct btrfs_ordered_extent, rb_node);
		if (btrfs_range_overlaps(entry, file_offset, len))
			goto out;
	}
	/* No ordered extent in the range */
	entry = NULL;
out:
	if (entry) {
		refcount_inc(&entry->refs);
		trace_btrfs_ordered_extent_lookup_first_range(inode, entry);
	}

	spin_unlock_irq(&inode->ordered_tree_lock);
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
		btrfs_lock_extent(&inode->io_tree, start, end, cachedp);
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
		btrfs_unlock_extent(&inode->io_tree, start, end, cachedp);
		btrfs_start_ordered_extent(ordered);
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

	if (!btrfs_try_lock_extent(&inode->io_tree, start, end, cached_state))
		return false;

	ordered = btrfs_lookup_ordered_range(inode, start, end - start + 1);
	if (!ordered)
		return true;

	btrfs_put_ordered_extent(ordered);
	btrfs_unlock_extent(&inode->io_tree, start, end, cached_state);

	return false;
}

/* Split out a new ordered extent for this first @len bytes of @ordered. */
struct btrfs_ordered_extent *btrfs_split_ordered_extent(
			struct btrfs_ordered_extent *ordered, u64 len)
{
	struct btrfs_inode *inode = ordered->inode;
	struct btrfs_root *root = inode->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	u64 file_offset = ordered->file_offset;
	u64 disk_bytenr = ordered->disk_bytenr;
	unsigned long flags = ordered->flags;
	struct btrfs_ordered_sum *sum, *tmpsum;
	struct btrfs_ordered_extent *new;
	struct rb_node *node;
	u64 offset = 0;

	trace_btrfs_ordered_extent_split(inode, ordered);

	ASSERT(!(flags & (1U << BTRFS_ORDERED_COMPRESSED)));

	/*
	 * The entire bio must be covered by the ordered extent, but we can't
	 * reduce the original extent to a zero length either.
	 */
	if (WARN_ON_ONCE(len >= ordered->num_bytes))
		return ERR_PTR(-EINVAL);
	/*
	 * If our ordered extent had an error there's no point in continuing.
	 * The error may have come from a transaction abort done either by this
	 * task or some other concurrent task, and the transaction abort path
	 * iterates over all existing ordered extents and sets the flag
	 * BTRFS_ORDERED_IOERR on them.
	 */
	if (unlikely(flags & (1U << BTRFS_ORDERED_IOERR))) {
		const int fs_error = BTRFS_FS_ERROR(fs_info);

		return fs_error ? ERR_PTR(fs_error) : ERR_PTR(-EIO);
	}
	/* We cannot split partially completed ordered extents. */
	if (ordered->bytes_left) {
		ASSERT(!(flags & ~BTRFS_ORDERED_TYPE_FLAGS));
		if (WARN_ON_ONCE(ordered->bytes_left != ordered->disk_num_bytes))
			return ERR_PTR(-EINVAL);
	}
	/* We cannot split a compressed ordered extent. */
	if (WARN_ON_ONCE(ordered->disk_num_bytes != ordered->num_bytes))
		return ERR_PTR(-EINVAL);

	new = alloc_ordered_extent(inode, file_offset, len, len, disk_bytenr,
				   len, 0, flags, ordered->compress_type);
	if (IS_ERR(new))
		return new;

	/* One ref for the tree. */
	refcount_inc(&new->refs);

	/*
	 * Take the root's ordered_extent_lock to avoid a race with
	 * btrfs_wait_ordered_extents() when updating the disk_bytenr and
	 * disk_num_bytes fields of the ordered extent below. And we disable
	 * IRQs because the inode's ordered_tree_lock is used in IRQ context
	 * elsewhere.
	 *
	 * There's no concern about a previous caller of
	 * btrfs_wait_ordered_extents() getting the trimmed ordered extent
	 * before we insert the new one, because even if it gets the ordered
	 * extent before it's trimmed and the new one inserted, right before it
	 * uses it or during its use, the ordered extent might have been
	 * trimmed in the meanwhile, and it missed the new ordered extent.
	 * There's no way around this and it's harmless for current use cases,
	 * so we take the root's ordered_extent_lock to fix that race during
	 * trimming and silence tools like KCSAN.
	 */
	spin_lock_irq(&root->ordered_extent_lock);
	spin_lock(&inode->ordered_tree_lock);

	/*
	 * We don't have overlapping ordered extents (that would imply double
	 * allocation of extents) and we checked above that the split length
	 * does not cross the ordered extent's num_bytes field, so there's
	 * no need to remove it and re-insert it in the tree.
	 */
	ordered->file_offset += len;
	ordered->disk_bytenr += len;
	ordered->num_bytes -= len;
	ordered->disk_num_bytes -= len;
	ordered->ram_bytes -= len;

	if (test_bit(BTRFS_ORDERED_IO_DONE, &ordered->flags)) {
		ASSERT(ordered->bytes_left == 0);
		new->bytes_left = 0;
	} else {
		ordered->bytes_left -= len;
	}

	if (test_bit(BTRFS_ORDERED_TRUNCATED, &ordered->flags)) {
		if (ordered->truncated_len > len) {
			ordered->truncated_len -= len;
		} else {
			new->truncated_len = ordered->truncated_len;
			ordered->truncated_len = 0;
		}
	}

	list_for_each_entry_safe(sum, tmpsum, &ordered->list, list) {
		if (offset == len)
			break;
		list_move_tail(&sum->list, &new->list);
		offset += sum->len;
	}

	node = tree_insert(&inode->ordered_tree, new->file_offset, &new->rb_node);
	if (unlikely(node))
		btrfs_panic(fs_info, -EEXIST,
			"inconsistency in ordered tree at offset %llu after split",
			new->file_offset);
	spin_unlock(&inode->ordered_tree_lock);

	list_add_tail(&new->root_extent_list, &root->ordered_extents);
	root->nr_ordered_extents++;
	spin_unlock_irq(&root->ordered_extent_lock);
	return new;
}

int __init ordered_data_init(void)
{
	btrfs_ordered_extent_cache = KMEM_CACHE(btrfs_ordered_extent, 0);
	if (!btrfs_ordered_extent_cache)
		return -ENOMEM;

	return 0;
}

void __cold ordered_data_exit(void)
{
	kmem_cache_destroy(btrfs_ordered_extent_cache);
}
