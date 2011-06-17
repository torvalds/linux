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

#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/writeback.h>
#include <linux/pagemap.h>
#include <linux/blkdev.h>
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "locking.h"
#include "tree-log.h"
#include "inode-map.h"

#define BTRFS_ROOT_TRANS_TAG 0

static noinline void put_transaction(struct btrfs_transaction *transaction)
{
	WARN_ON(atomic_read(&transaction->use_count) == 0);
	if (atomic_dec_and_test(&transaction->use_count)) {
		BUG_ON(!list_empty(&transaction->list));
		memset(transaction, 0, sizeof(*transaction));
		kmem_cache_free(btrfs_transaction_cachep, transaction);
	}
}

static noinline void switch_commit_root(struct btrfs_root *root)
{
	free_extent_buffer(root->commit_root);
	root->commit_root = btrfs_root_node(root);
}

/*
 * either allocate a new transaction or hop into the existing one
 */
static noinline int join_transaction(struct btrfs_root *root, int nofail)
{
	struct btrfs_transaction *cur_trans;

	spin_lock(&root->fs_info->trans_lock);
	if (root->fs_info->trans_no_join) {
		if (!nofail) {
			spin_unlock(&root->fs_info->trans_lock);
			return -EBUSY;
		}
	}

	cur_trans = root->fs_info->running_transaction;
	if (cur_trans) {
		atomic_inc(&cur_trans->use_count);
		atomic_inc(&cur_trans->num_writers);
		cur_trans->num_joined++;
		spin_unlock(&root->fs_info->trans_lock);
		return 0;
	}
	spin_unlock(&root->fs_info->trans_lock);

	cur_trans = kmem_cache_alloc(btrfs_transaction_cachep, GFP_NOFS);
	if (!cur_trans)
		return -ENOMEM;
	spin_lock(&root->fs_info->trans_lock);
	if (root->fs_info->running_transaction) {
		kmem_cache_free(btrfs_transaction_cachep, cur_trans);
		cur_trans = root->fs_info->running_transaction;
		atomic_inc(&cur_trans->use_count);
		atomic_inc(&cur_trans->num_writers);
		cur_trans->num_joined++;
		spin_unlock(&root->fs_info->trans_lock);
		return 0;
	}
	atomic_set(&cur_trans->num_writers, 1);
	cur_trans->num_joined = 0;
	init_waitqueue_head(&cur_trans->writer_wait);
	init_waitqueue_head(&cur_trans->commit_wait);
	cur_trans->in_commit = 0;
	cur_trans->blocked = 0;
	/*
	 * One for this trans handle, one so it will live on until we
	 * commit the transaction.
	 */
	atomic_set(&cur_trans->use_count, 2);
	cur_trans->commit_done = 0;
	cur_trans->start_time = get_seconds();

	cur_trans->delayed_refs.root = RB_ROOT;
	cur_trans->delayed_refs.num_entries = 0;
	cur_trans->delayed_refs.num_heads_ready = 0;
	cur_trans->delayed_refs.num_heads = 0;
	cur_trans->delayed_refs.flushing = 0;
	cur_trans->delayed_refs.run_delayed_start = 0;
	spin_lock_init(&cur_trans->commit_lock);
	spin_lock_init(&cur_trans->delayed_refs.lock);

	INIT_LIST_HEAD(&cur_trans->pending_snapshots);
	list_add_tail(&cur_trans->list, &root->fs_info->trans_list);
	extent_io_tree_init(&cur_trans->dirty_pages,
			     root->fs_info->btree_inode->i_mapping);
	root->fs_info->generation++;
	cur_trans->transid = root->fs_info->generation;
	root->fs_info->running_transaction = cur_trans;
	spin_unlock(&root->fs_info->trans_lock);

	return 0;
}

/*
 * this does all the record keeping required to make sure that a reference
 * counted root is properly recorded in a given transaction.  This is required
 * to make sure the old root from before we joined the transaction is deleted
 * when the transaction commits
 */
static int record_root_in_trans(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root)
{
	if (root->ref_cows && root->last_trans < trans->transid) {
		WARN_ON(root == root->fs_info->extent_root);
		WARN_ON(root->commit_root != root->node);

		/*
		 * see below for in_trans_setup usage rules
		 * we have the reloc mutex held now, so there
		 * is only one writer in this function
		 */
		root->in_trans_setup = 1;

		/* make sure readers find in_trans_setup before
		 * they find our root->last_trans update
		 */
		smp_wmb();

		spin_lock(&root->fs_info->fs_roots_radix_lock);
		if (root->last_trans == trans->transid) {
			spin_unlock(&root->fs_info->fs_roots_radix_lock);
			return 0;
		}
		radix_tree_tag_set(&root->fs_info->fs_roots_radix,
			   (unsigned long)root->root_key.objectid,
			   BTRFS_ROOT_TRANS_TAG);
		spin_unlock(&root->fs_info->fs_roots_radix_lock);
		root->last_trans = trans->transid;

		/* this is pretty tricky.  We don't want to
		 * take the relocation lock in btrfs_record_root_in_trans
		 * unless we're really doing the first setup for this root in
		 * this transaction.
		 *
		 * Normally we'd use root->last_trans as a flag to decide
		 * if we want to take the expensive mutex.
		 *
		 * But, we have to set root->last_trans before we
		 * init the relocation root, otherwise, we trip over warnings
		 * in ctree.c.  The solution used here is to flag ourselves
		 * with root->in_trans_setup.  When this is 1, we're still
		 * fixing up the reloc trees and everyone must wait.
		 *
		 * When this is zero, they can trust root->last_trans and fly
		 * through btrfs_record_root_in_trans without having to take the
		 * lock.  smp_wmb() makes sure that all the writes above are
		 * done before we pop in the zero below
		 */
		btrfs_init_reloc_root(trans, root);
		smp_wmb();
		root->in_trans_setup = 0;
	}
	return 0;
}


int btrfs_record_root_in_trans(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root)
{
	if (!root->ref_cows)
		return 0;

	/*
	 * see record_root_in_trans for comments about in_trans_setup usage
	 * and barriers
	 */
	smp_rmb();
	if (root->last_trans == trans->transid &&
	    !root->in_trans_setup)
		return 0;

	mutex_lock(&root->fs_info->reloc_mutex);
	record_root_in_trans(trans, root);
	mutex_unlock(&root->fs_info->reloc_mutex);

	return 0;
}

/* wait for commit against the current transaction to become unblocked
 * when this is done, it is safe to start a new transaction, but the current
 * transaction might not be fully on disk.
 */
static void wait_current_trans(struct btrfs_root *root)
{
	struct btrfs_transaction *cur_trans;

	spin_lock(&root->fs_info->trans_lock);
	cur_trans = root->fs_info->running_transaction;
	if (cur_trans && cur_trans->blocked) {
		DEFINE_WAIT(wait);
		atomic_inc(&cur_trans->use_count);
		spin_unlock(&root->fs_info->trans_lock);
		while (1) {
			prepare_to_wait(&root->fs_info->transaction_wait, &wait,
					TASK_UNINTERRUPTIBLE);
			if (!cur_trans->blocked)
				break;
			schedule();
		}
		finish_wait(&root->fs_info->transaction_wait, &wait);
		put_transaction(cur_trans);
	} else {
		spin_unlock(&root->fs_info->trans_lock);
	}
}

enum btrfs_trans_type {
	TRANS_START,
	TRANS_JOIN,
	TRANS_USERSPACE,
	TRANS_JOIN_NOLOCK,
};

static int may_wait_transaction(struct btrfs_root *root, int type)
{
	if (root->fs_info->log_root_recovering)
		return 0;

	if (type == TRANS_USERSPACE)
		return 1;

	if (type == TRANS_START &&
	    !atomic_read(&root->fs_info->open_ioctl_trans))
		return 1;

	return 0;
}

static struct btrfs_trans_handle *start_transaction(struct btrfs_root *root,
						    u64 num_items, int type)
{
	struct btrfs_trans_handle *h;
	struct btrfs_transaction *cur_trans;
	int retries = 0;
	int ret;

	if (root->fs_info->fs_state & BTRFS_SUPER_FLAG_ERROR)
		return ERR_PTR(-EROFS);

	if (current->journal_info) {
		WARN_ON(type != TRANS_JOIN && type != TRANS_JOIN_NOLOCK);
		h = current->journal_info;
		h->use_count++;
		h->orig_rsv = h->block_rsv;
		h->block_rsv = NULL;
		goto got_it;
	}
again:
	h = kmem_cache_alloc(btrfs_trans_handle_cachep, GFP_NOFS);
	if (!h)
		return ERR_PTR(-ENOMEM);

	if (may_wait_transaction(root, type))
		wait_current_trans(root);

	do {
		ret = join_transaction(root, type == TRANS_JOIN_NOLOCK);
		if (ret == -EBUSY)
			wait_current_trans(root);
	} while (ret == -EBUSY);

	if (ret < 0) {
		kmem_cache_free(btrfs_trans_handle_cachep, h);
		return ERR_PTR(ret);
	}

	cur_trans = root->fs_info->running_transaction;

	h->transid = cur_trans->transid;
	h->transaction = cur_trans;
	h->blocks_used = 0;
	h->bytes_reserved = 0;
	h->delayed_ref_updates = 0;
	h->use_count = 1;
	h->block_rsv = NULL;
	h->orig_rsv = NULL;

	smp_mb();
	if (cur_trans->blocked && may_wait_transaction(root, type)) {
		btrfs_commit_transaction(h, root);
		goto again;
	}

	if (num_items > 0) {
		ret = btrfs_trans_reserve_metadata(h, root, num_items);
		if (ret == -EAGAIN && !retries) {
			retries++;
			btrfs_commit_transaction(h, root);
			goto again;
		} else if (ret == -EAGAIN) {
			/*
			 * We have already retried and got EAGAIN, so really we
			 * don't have space, so set ret to -ENOSPC.
			 */
			ret = -ENOSPC;
		}

		if (ret < 0) {
			btrfs_end_transaction(h, root);
			return ERR_PTR(ret);
		}
	}

got_it:
	btrfs_record_root_in_trans(h, root);

	if (!current->journal_info && type != TRANS_USERSPACE)
		current->journal_info = h;
	return h;
}

struct btrfs_trans_handle *btrfs_start_transaction(struct btrfs_root *root,
						   int num_items)
{
	return start_transaction(root, num_items, TRANS_START);
}
struct btrfs_trans_handle *btrfs_join_transaction(struct btrfs_root *root)
{
	return start_transaction(root, 0, TRANS_JOIN);
}

struct btrfs_trans_handle *btrfs_join_transaction_nolock(struct btrfs_root *root)
{
	return start_transaction(root, 0, TRANS_JOIN_NOLOCK);
}

struct btrfs_trans_handle *btrfs_start_ioctl_transaction(struct btrfs_root *root)
{
	return start_transaction(root, 0, TRANS_USERSPACE);
}

/* wait for a transaction commit to be fully complete */
static noinline int wait_for_commit(struct btrfs_root *root,
				    struct btrfs_transaction *commit)
{
	DEFINE_WAIT(wait);
	while (!commit->commit_done) {
		prepare_to_wait(&commit->commit_wait, &wait,
				TASK_UNINTERRUPTIBLE);
		if (commit->commit_done)
			break;
		schedule();
	}
	finish_wait(&commit->commit_wait, &wait);
	return 0;
}

int btrfs_wait_for_commit(struct btrfs_root *root, u64 transid)
{
	struct btrfs_transaction *cur_trans = NULL, *t;
	int ret;

	ret = 0;
	if (transid) {
		if (transid <= root->fs_info->last_trans_committed)
			goto out;

		/* find specified transaction */
		spin_lock(&root->fs_info->trans_lock);
		list_for_each_entry(t, &root->fs_info->trans_list, list) {
			if (t->transid == transid) {
				cur_trans = t;
				atomic_inc(&cur_trans->use_count);
				break;
			}
			if (t->transid > transid)
				break;
		}
		spin_unlock(&root->fs_info->trans_lock);
		ret = -EINVAL;
		if (!cur_trans)
			goto out;  /* bad transid */
	} else {
		/* find newest transaction that is committing | committed */
		spin_lock(&root->fs_info->trans_lock);
		list_for_each_entry_reverse(t, &root->fs_info->trans_list,
					    list) {
			if (t->in_commit) {
				if (t->commit_done)
					break;
				cur_trans = t;
				atomic_inc(&cur_trans->use_count);
				break;
			}
		}
		spin_unlock(&root->fs_info->trans_lock);
		if (!cur_trans)
			goto out;  /* nothing committing|committed */
	}

	wait_for_commit(root, cur_trans);

	put_transaction(cur_trans);
	ret = 0;
out:
	return ret;
}

void btrfs_throttle(struct btrfs_root *root)
{
	if (!atomic_read(&root->fs_info->open_ioctl_trans))
		wait_current_trans(root);
}

static int should_end_transaction(struct btrfs_trans_handle *trans,
				  struct btrfs_root *root)
{
	int ret;
	ret = btrfs_block_rsv_check(trans, root,
				    &root->fs_info->global_block_rsv, 0, 5);
	return ret ? 1 : 0;
}

int btrfs_should_end_transaction(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root)
{
	struct btrfs_transaction *cur_trans = trans->transaction;
	int updates;

	smp_mb();
	if (cur_trans->blocked || cur_trans->delayed_refs.flushing)
		return 1;

	updates = trans->delayed_ref_updates;
	trans->delayed_ref_updates = 0;
	if (updates)
		btrfs_run_delayed_refs(trans, root, updates);

	return should_end_transaction(trans, root);
}

static int __btrfs_end_transaction(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root, int throttle, int lock)
{
	struct btrfs_transaction *cur_trans = trans->transaction;
	struct btrfs_fs_info *info = root->fs_info;
	int count = 0;

	if (--trans->use_count) {
		trans->block_rsv = trans->orig_rsv;
		return 0;
	}

	while (count < 4) {
		unsigned long cur = trans->delayed_ref_updates;
		trans->delayed_ref_updates = 0;
		if (cur &&
		    trans->transaction->delayed_refs.num_heads_ready > 64) {
			trans->delayed_ref_updates = 0;

			/*
			 * do a full flush if the transaction is trying
			 * to close
			 */
			if (trans->transaction->delayed_refs.flushing)
				cur = 0;
			btrfs_run_delayed_refs(trans, root, cur);
		} else {
			break;
		}
		count++;
	}

	btrfs_trans_release_metadata(trans, root);

	if (lock && !atomic_read(&root->fs_info->open_ioctl_trans) &&
	    should_end_transaction(trans, root)) {
		trans->transaction->blocked = 1;
		smp_wmb();
	}

	if (lock && cur_trans->blocked && !cur_trans->in_commit) {
		if (throttle)
			return btrfs_commit_transaction(trans, root);
		else
			wake_up_process(info->transaction_kthread);
	}

	WARN_ON(cur_trans != info->running_transaction);
	WARN_ON(atomic_read(&cur_trans->num_writers) < 1);
	atomic_dec(&cur_trans->num_writers);

	smp_mb();
	if (waitqueue_active(&cur_trans->writer_wait))
		wake_up(&cur_trans->writer_wait);
	put_transaction(cur_trans);

	if (current->journal_info == trans)
		current->journal_info = NULL;
	memset(trans, 0, sizeof(*trans));
	kmem_cache_free(btrfs_trans_handle_cachep, trans);

	if (throttle)
		btrfs_run_delayed_iputs(root);

	return 0;
}

int btrfs_end_transaction(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root)
{
	int ret;

	ret = __btrfs_end_transaction(trans, root, 0, 1);
	if (ret)
		return ret;
	return 0;
}

int btrfs_end_transaction_throttle(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root)
{
	int ret;

	ret = __btrfs_end_transaction(trans, root, 1, 1);
	if (ret)
		return ret;
	return 0;
}

int btrfs_end_transaction_nolock(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root)
{
	int ret;

	ret = __btrfs_end_transaction(trans, root, 0, 0);
	if (ret)
		return ret;
	return 0;
}

int btrfs_end_transaction_dmeta(struct btrfs_trans_handle *trans,
				struct btrfs_root *root)
{
	return __btrfs_end_transaction(trans, root, 1, 1);
}

/*
 * when btree blocks are allocated, they have some corresponding bits set for
 * them in one of two extent_io trees.  This is used to make sure all of
 * those extents are sent to disk but does not wait on them
 */
int btrfs_write_marked_extents(struct btrfs_root *root,
			       struct extent_io_tree *dirty_pages, int mark)
{
	int ret;
	int err = 0;
	int werr = 0;
	struct page *page;
	struct inode *btree_inode = root->fs_info->btree_inode;
	u64 start = 0;
	u64 end;
	unsigned long index;

	while (1) {
		ret = find_first_extent_bit(dirty_pages, start, &start, &end,
					    mark);
		if (ret)
			break;
		while (start <= end) {
			cond_resched();

			index = start >> PAGE_CACHE_SHIFT;
			start = (u64)(index + 1) << PAGE_CACHE_SHIFT;
			page = find_get_page(btree_inode->i_mapping, index);
			if (!page)
				continue;

			btree_lock_page_hook(page);
			if (!page->mapping) {
				unlock_page(page);
				page_cache_release(page);
				continue;
			}

			if (PageWriteback(page)) {
				if (PageDirty(page))
					wait_on_page_writeback(page);
				else {
					unlock_page(page);
					page_cache_release(page);
					continue;
				}
			}
			err = write_one_page(page, 0);
			if (err)
				werr = err;
			page_cache_release(page);
		}
	}
	if (err)
		werr = err;
	return werr;
}

/*
 * when btree blocks are allocated, they have some corresponding bits set for
 * them in one of two extent_io trees.  This is used to make sure all of
 * those extents are on disk for transaction or log commit.  We wait
 * on all the pages and clear them from the dirty pages state tree
 */
int btrfs_wait_marked_extents(struct btrfs_root *root,
			      struct extent_io_tree *dirty_pages, int mark)
{
	int ret;
	int err = 0;
	int werr = 0;
	struct page *page;
	struct inode *btree_inode = root->fs_info->btree_inode;
	u64 start = 0;
	u64 end;
	unsigned long index;

	while (1) {
		ret = find_first_extent_bit(dirty_pages, start, &start, &end,
					    mark);
		if (ret)
			break;

		clear_extent_bits(dirty_pages, start, end, mark, GFP_NOFS);
		while (start <= end) {
			index = start >> PAGE_CACHE_SHIFT;
			start = (u64)(index + 1) << PAGE_CACHE_SHIFT;
			page = find_get_page(btree_inode->i_mapping, index);
			if (!page)
				continue;
			if (PageDirty(page)) {
				btree_lock_page_hook(page);
				wait_on_page_writeback(page);
				err = write_one_page(page, 0);
				if (err)
					werr = err;
			}
			wait_on_page_writeback(page);
			page_cache_release(page);
			cond_resched();
		}
	}
	if (err)
		werr = err;
	return werr;
}

/*
 * when btree blocks are allocated, they have some corresponding bits set for
 * them in one of two extent_io trees.  This is used to make sure all of
 * those extents are on disk for transaction or log commit
 */
int btrfs_write_and_wait_marked_extents(struct btrfs_root *root,
				struct extent_io_tree *dirty_pages, int mark)
{
	int ret;
	int ret2;

	ret = btrfs_write_marked_extents(root, dirty_pages, mark);
	ret2 = btrfs_wait_marked_extents(root, dirty_pages, mark);
	return ret || ret2;
}

int btrfs_write_and_wait_transaction(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root)
{
	if (!trans || !trans->transaction) {
		struct inode *btree_inode;
		btree_inode = root->fs_info->btree_inode;
		return filemap_write_and_wait(btree_inode->i_mapping);
	}
	return btrfs_write_and_wait_marked_extents(root,
					   &trans->transaction->dirty_pages,
					   EXTENT_DIRTY);
}

/*
 * this is used to update the root pointer in the tree of tree roots.
 *
 * But, in the case of the extent allocation tree, updating the root
 * pointer may allocate blocks which may change the root of the extent
 * allocation tree.
 *
 * So, this loops and repeats and makes sure the cowonly root didn't
 * change while the root pointer was being updated in the metadata.
 */
static int update_cowonly_root(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root)
{
	int ret;
	u64 old_root_bytenr;
	u64 old_root_used;
	struct btrfs_root *tree_root = root->fs_info->tree_root;

	old_root_used = btrfs_root_used(&root->root_item);
	btrfs_write_dirty_block_groups(trans, root);

	while (1) {
		old_root_bytenr = btrfs_root_bytenr(&root->root_item);
		if (old_root_bytenr == root->node->start &&
		    old_root_used == btrfs_root_used(&root->root_item))
			break;

		btrfs_set_root_node(&root->root_item, root->node);
		ret = btrfs_update_root(trans, tree_root,
					&root->root_key,
					&root->root_item);
		BUG_ON(ret);

		old_root_used = btrfs_root_used(&root->root_item);
		ret = btrfs_write_dirty_block_groups(trans, root);
		BUG_ON(ret);
	}

	if (root != root->fs_info->extent_root)
		switch_commit_root(root);

	return 0;
}

/*
 * update all the cowonly tree roots on disk
 */
static noinline int commit_cowonly_roots(struct btrfs_trans_handle *trans,
					 struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct list_head *next;
	struct extent_buffer *eb;
	int ret;

	ret = btrfs_run_delayed_refs(trans, root, (unsigned long)-1);
	BUG_ON(ret);

	eb = btrfs_lock_root_node(fs_info->tree_root);
	btrfs_cow_block(trans, fs_info->tree_root, eb, NULL, 0, &eb);
	btrfs_tree_unlock(eb);
	free_extent_buffer(eb);

	ret = btrfs_run_delayed_refs(trans, root, (unsigned long)-1);
	BUG_ON(ret);

	while (!list_empty(&fs_info->dirty_cowonly_roots)) {
		next = fs_info->dirty_cowonly_roots.next;
		list_del_init(next);
		root = list_entry(next, struct btrfs_root, dirty_list);

		update_cowonly_root(trans, root);
	}

	down_write(&fs_info->extent_commit_sem);
	switch_commit_root(fs_info->extent_root);
	up_write(&fs_info->extent_commit_sem);

	return 0;
}

/*
 * dead roots are old snapshots that need to be deleted.  This allocates
 * a dirty root struct and adds it into the list of dead roots that need to
 * be deleted
 */
int btrfs_add_dead_root(struct btrfs_root *root)
{
	spin_lock(&root->fs_info->trans_lock);
	list_add(&root->root_list, &root->fs_info->dead_roots);
	spin_unlock(&root->fs_info->trans_lock);
	return 0;
}

/*
 * update all the cowonly tree roots on disk
 */
static noinline int commit_fs_roots(struct btrfs_trans_handle *trans,
				    struct btrfs_root *root)
{
	struct btrfs_root *gang[8];
	struct btrfs_fs_info *fs_info = root->fs_info;
	int i;
	int ret;
	int err = 0;

	spin_lock(&fs_info->fs_roots_radix_lock);
	while (1) {
		ret = radix_tree_gang_lookup_tag(&fs_info->fs_roots_radix,
						 (void **)gang, 0,
						 ARRAY_SIZE(gang),
						 BTRFS_ROOT_TRANS_TAG);
		if (ret == 0)
			break;
		for (i = 0; i < ret; i++) {
			root = gang[i];
			radix_tree_tag_clear(&fs_info->fs_roots_radix,
					(unsigned long)root->root_key.objectid,
					BTRFS_ROOT_TRANS_TAG);
			spin_unlock(&fs_info->fs_roots_radix_lock);

			btrfs_free_log(trans, root);
			btrfs_update_reloc_root(trans, root);
			btrfs_orphan_commit_root(trans, root);

			btrfs_save_ino_cache(root, trans);

			if (root->commit_root != root->node) {
				mutex_lock(&root->fs_commit_mutex);
				switch_commit_root(root);
				btrfs_unpin_free_ino(root);
				mutex_unlock(&root->fs_commit_mutex);

				btrfs_set_root_node(&root->root_item,
						    root->node);
			}

			err = btrfs_update_root(trans, fs_info->tree_root,
						&root->root_key,
						&root->root_item);
			spin_lock(&fs_info->fs_roots_radix_lock);
			if (err)
				break;
		}
	}
	spin_unlock(&fs_info->fs_roots_radix_lock);
	return err;
}

/*
 * defrag a given btree.  If cacheonly == 1, this won't read from the disk,
 * otherwise every leaf in the btree is read and defragged.
 */
int btrfs_defrag_root(struct btrfs_root *root, int cacheonly)
{
	struct btrfs_fs_info *info = root->fs_info;
	struct btrfs_trans_handle *trans;
	int ret;
	unsigned long nr;

	if (xchg(&root->defrag_running, 1))
		return 0;

	while (1) {
		trans = btrfs_start_transaction(root, 0);
		if (IS_ERR(trans))
			return PTR_ERR(trans);

		ret = btrfs_defrag_leaves(trans, root, cacheonly);

		nr = trans->blocks_used;
		btrfs_end_transaction(trans, root);
		btrfs_btree_balance_dirty(info->tree_root, nr);
		cond_resched();

		if (btrfs_fs_closing(root->fs_info) || ret != -EAGAIN)
			break;
	}
	root->defrag_running = 0;
	return ret;
}

/*
 * new snapshots need to be created at a very specific time in the
 * transaction commit.  This does the actual creation
 */
static noinline int create_pending_snapshot(struct btrfs_trans_handle *trans,
				   struct btrfs_fs_info *fs_info,
				   struct btrfs_pending_snapshot *pending)
{
	struct btrfs_key key;
	struct btrfs_root_item *new_root_item;
	struct btrfs_root *tree_root = fs_info->tree_root;
	struct btrfs_root *root = pending->root;
	struct btrfs_root *parent_root;
	struct inode *parent_inode;
	struct dentry *parent;
	struct dentry *dentry;
	struct extent_buffer *tmp;
	struct extent_buffer *old;
	int ret;
	u64 to_reserve = 0;
	u64 index = 0;
	u64 objectid;
	u64 root_flags;

	new_root_item = kmalloc(sizeof(*new_root_item), GFP_NOFS);
	if (!new_root_item) {
		pending->error = -ENOMEM;
		goto fail;
	}

	ret = btrfs_find_free_objectid(tree_root, &objectid);
	if (ret) {
		pending->error = ret;
		goto fail;
	}

	btrfs_reloc_pre_snapshot(trans, pending, &to_reserve);
	btrfs_orphan_pre_snapshot(trans, pending, &to_reserve);

	if (to_reserve > 0) {
		ret = btrfs_block_rsv_add(trans, root, &pending->block_rsv,
					  to_reserve);
		if (ret) {
			pending->error = ret;
			goto fail;
		}
	}

	key.objectid = objectid;
	key.offset = (u64)-1;
	key.type = BTRFS_ROOT_ITEM_KEY;

	trans->block_rsv = &pending->block_rsv;

	dentry = pending->dentry;
	parent = dget_parent(dentry);
	parent_inode = parent->d_inode;
	parent_root = BTRFS_I(parent_inode)->root;
	record_root_in_trans(trans, parent_root);

	/*
	 * insert the directory item
	 */
	ret = btrfs_set_inode_index(parent_inode, &index);
	BUG_ON(ret);
	ret = btrfs_insert_dir_item(trans, parent_root,
				dentry->d_name.name, dentry->d_name.len,
				parent_inode, &key,
				BTRFS_FT_DIR, index);
	BUG_ON(ret);

	btrfs_i_size_write(parent_inode, parent_inode->i_size +
					 dentry->d_name.len * 2);
	ret = btrfs_update_inode(trans, parent_root, parent_inode);
	BUG_ON(ret);

	/*
	 * pull in the delayed directory update
	 * and the delayed inode item
	 * otherwise we corrupt the FS during
	 * snapshot
	 */
	ret = btrfs_run_delayed_items(trans, root);
	BUG_ON(ret);

	record_root_in_trans(trans, root);
	btrfs_set_root_last_snapshot(&root->root_item, trans->transid);
	memcpy(new_root_item, &root->root_item, sizeof(*new_root_item));
	btrfs_check_and_init_root_item(new_root_item);

	root_flags = btrfs_root_flags(new_root_item);
	if (pending->readonly)
		root_flags |= BTRFS_ROOT_SUBVOL_RDONLY;
	else
		root_flags &= ~BTRFS_ROOT_SUBVOL_RDONLY;
	btrfs_set_root_flags(new_root_item, root_flags);

	old = btrfs_lock_root_node(root);
	btrfs_cow_block(trans, root, old, NULL, 0, &old);
	btrfs_set_lock_blocking(old);

	btrfs_copy_root(trans, root, old, &tmp, objectid);
	btrfs_tree_unlock(old);
	free_extent_buffer(old);

	btrfs_set_root_node(new_root_item, tmp);
	/* record when the snapshot was created in key.offset */
	key.offset = trans->transid;
	ret = btrfs_insert_root(trans, tree_root, &key, new_root_item);
	btrfs_tree_unlock(tmp);
	free_extent_buffer(tmp);
	BUG_ON(ret);

	/*
	 * insert root back/forward references
	 */
	ret = btrfs_add_root_ref(trans, tree_root, objectid,
				 parent_root->root_key.objectid,
				 btrfs_ino(parent_inode), index,
				 dentry->d_name.name, dentry->d_name.len);
	BUG_ON(ret);
	dput(parent);

	key.offset = (u64)-1;
	pending->snap = btrfs_read_fs_root_no_name(root->fs_info, &key);
	BUG_ON(IS_ERR(pending->snap));

	btrfs_reloc_post_snapshot(trans, pending);
	btrfs_orphan_post_snapshot(trans, pending);
fail:
	kfree(new_root_item);
	btrfs_block_rsv_release(root, &pending->block_rsv, (u64)-1);
	return 0;
}

/*
 * create all the snapshots we've scheduled for creation
 */
static noinline int create_pending_snapshots(struct btrfs_trans_handle *trans,
					     struct btrfs_fs_info *fs_info)
{
	struct btrfs_pending_snapshot *pending;
	struct list_head *head = &trans->transaction->pending_snapshots;
	int ret;

	list_for_each_entry(pending, head, list) {
		ret = create_pending_snapshot(trans, fs_info, pending);
		BUG_ON(ret);
	}
	return 0;
}

static void update_super_roots(struct btrfs_root *root)
{
	struct btrfs_root_item *root_item;
	struct btrfs_super_block *super;

	super = &root->fs_info->super_copy;

	root_item = &root->fs_info->chunk_root->root_item;
	super->chunk_root = root_item->bytenr;
	super->chunk_root_generation = root_item->generation;
	super->chunk_root_level = root_item->level;

	root_item = &root->fs_info->tree_root->root_item;
	super->root = root_item->bytenr;
	super->generation = root_item->generation;
	super->root_level = root_item->level;
	if (super->cache_generation != 0 || btrfs_test_opt(root, SPACE_CACHE))
		super->cache_generation = root_item->generation;
}

int btrfs_transaction_in_commit(struct btrfs_fs_info *info)
{
	int ret = 0;
	spin_lock(&info->trans_lock);
	if (info->running_transaction)
		ret = info->running_transaction->in_commit;
	spin_unlock(&info->trans_lock);
	return ret;
}

int btrfs_transaction_blocked(struct btrfs_fs_info *info)
{
	int ret = 0;
	spin_lock(&info->trans_lock);
	if (info->running_transaction)
		ret = info->running_transaction->blocked;
	spin_unlock(&info->trans_lock);
	return ret;
}

/*
 * wait for the current transaction commit to start and block subsequent
 * transaction joins
 */
static void wait_current_trans_commit_start(struct btrfs_root *root,
					    struct btrfs_transaction *trans)
{
	DEFINE_WAIT(wait);

	if (trans->in_commit)
		return;

	while (1) {
		prepare_to_wait(&root->fs_info->transaction_blocked_wait, &wait,
				TASK_UNINTERRUPTIBLE);
		if (trans->in_commit) {
			finish_wait(&root->fs_info->transaction_blocked_wait,
				    &wait);
			break;
		}
		schedule();
		finish_wait(&root->fs_info->transaction_blocked_wait, &wait);
	}
}

/*
 * wait for the current transaction to start and then become unblocked.
 * caller holds ref.
 */
static void wait_current_trans_commit_start_and_unblock(struct btrfs_root *root,
					 struct btrfs_transaction *trans)
{
	DEFINE_WAIT(wait);

	if (trans->commit_done || (trans->in_commit && !trans->blocked))
		return;

	while (1) {
		prepare_to_wait(&root->fs_info->transaction_wait, &wait,
				TASK_UNINTERRUPTIBLE);
		if (trans->commit_done ||
		    (trans->in_commit && !trans->blocked)) {
			finish_wait(&root->fs_info->transaction_wait,
				    &wait);
			break;
		}
		schedule();
		finish_wait(&root->fs_info->transaction_wait,
			    &wait);
	}
}

/*
 * commit transactions asynchronously. once btrfs_commit_transaction_async
 * returns, any subsequent transaction will not be allowed to join.
 */
struct btrfs_async_commit {
	struct btrfs_trans_handle *newtrans;
	struct btrfs_root *root;
	struct delayed_work work;
};

static void do_async_commit(struct work_struct *work)
{
	struct btrfs_async_commit *ac =
		container_of(work, struct btrfs_async_commit, work.work);

	btrfs_commit_transaction(ac->newtrans, ac->root);
	kfree(ac);
}

int btrfs_commit_transaction_async(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root,
				   int wait_for_unblock)
{
	struct btrfs_async_commit *ac;
	struct btrfs_transaction *cur_trans;

	ac = kmalloc(sizeof(*ac), GFP_NOFS);
	if (!ac)
		return -ENOMEM;

	INIT_DELAYED_WORK(&ac->work, do_async_commit);
	ac->root = root;
	ac->newtrans = btrfs_join_transaction(root);
	if (IS_ERR(ac->newtrans)) {
		int err = PTR_ERR(ac->newtrans);
		kfree(ac);
		return err;
	}

	/* take transaction reference */
	cur_trans = trans->transaction;
	atomic_inc(&cur_trans->use_count);

	btrfs_end_transaction(trans, root);
	schedule_delayed_work(&ac->work, 0);

	/* wait for transaction to start and unblock */
	if (wait_for_unblock)
		wait_current_trans_commit_start_and_unblock(root, cur_trans);
	else
		wait_current_trans_commit_start(root, cur_trans);

	if (current->journal_info == trans)
		current->journal_info = NULL;

	put_transaction(cur_trans);
	return 0;
}

/*
 * btrfs_transaction state sequence:
 *    in_commit = 0, blocked = 0  (initial)
 *    in_commit = 1, blocked = 1
 *    blocked = 0
 *    commit_done = 1
 */
int btrfs_commit_transaction(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root)
{
	unsigned long joined = 0;
	struct btrfs_transaction *cur_trans;
	struct btrfs_transaction *prev_trans = NULL;
	DEFINE_WAIT(wait);
	int ret;
	int should_grow = 0;
	unsigned long now = get_seconds();
	int flush_on_commit = btrfs_test_opt(root, FLUSHONCOMMIT);

	btrfs_run_ordered_operations(root, 0);

	/* make a pass through all the delayed refs we have so far
	 * any runnings procs may add more while we are here
	 */
	ret = btrfs_run_delayed_refs(trans, root, 0);
	BUG_ON(ret);

	btrfs_trans_release_metadata(trans, root);

	cur_trans = trans->transaction;
	/*
	 * set the flushing flag so procs in this transaction have to
	 * start sending their work down.
	 */
	cur_trans->delayed_refs.flushing = 1;

	ret = btrfs_run_delayed_refs(trans, root, 0);
	BUG_ON(ret);

	spin_lock(&cur_trans->commit_lock);
	if (cur_trans->in_commit) {
		spin_unlock(&cur_trans->commit_lock);
		atomic_inc(&cur_trans->use_count);
		btrfs_end_transaction(trans, root);

		ret = wait_for_commit(root, cur_trans);
		BUG_ON(ret);

		put_transaction(cur_trans);

		return 0;
	}

	trans->transaction->in_commit = 1;
	trans->transaction->blocked = 1;
	spin_unlock(&cur_trans->commit_lock);
	wake_up(&root->fs_info->transaction_blocked_wait);

	spin_lock(&root->fs_info->trans_lock);
	if (cur_trans->list.prev != &root->fs_info->trans_list) {
		prev_trans = list_entry(cur_trans->list.prev,
					struct btrfs_transaction, list);
		if (!prev_trans->commit_done) {
			atomic_inc(&prev_trans->use_count);
			spin_unlock(&root->fs_info->trans_lock);

			wait_for_commit(root, prev_trans);

			put_transaction(prev_trans);
		} else {
			spin_unlock(&root->fs_info->trans_lock);
		}
	} else {
		spin_unlock(&root->fs_info->trans_lock);
	}

	if (now < cur_trans->start_time || now - cur_trans->start_time < 1)
		should_grow = 1;

	do {
		int snap_pending = 0;

		joined = cur_trans->num_joined;
		if (!list_empty(&trans->transaction->pending_snapshots))
			snap_pending = 1;

		WARN_ON(cur_trans != trans->transaction);

		if (flush_on_commit || snap_pending) {
			btrfs_start_delalloc_inodes(root, 1);
			ret = btrfs_wait_ordered_extents(root, 0, 1);
			BUG_ON(ret);
		}

		ret = btrfs_run_delayed_items(trans, root);
		BUG_ON(ret);

		/*
		 * rename don't use btrfs_join_transaction, so, once we
		 * set the transaction to blocked above, we aren't going
		 * to get any new ordered operations.  We can safely run
		 * it here and no for sure that nothing new will be added
		 * to the list
		 */
		btrfs_run_ordered_operations(root, 1);

		prepare_to_wait(&cur_trans->writer_wait, &wait,
				TASK_UNINTERRUPTIBLE);

		if (atomic_read(&cur_trans->num_writers) > 1)
			schedule_timeout(MAX_SCHEDULE_TIMEOUT);
		else if (should_grow)
			schedule_timeout(1);

		finish_wait(&cur_trans->writer_wait, &wait);
	} while (atomic_read(&cur_trans->num_writers) > 1 ||
		 (should_grow && cur_trans->num_joined != joined));

	/*
	 * Ok now we need to make sure to block out any other joins while we
	 * commit the transaction.  We could have started a join before setting
	 * no_join so make sure to wait for num_writers to == 1 again.
	 */
	spin_lock(&root->fs_info->trans_lock);
	root->fs_info->trans_no_join = 1;
	spin_unlock(&root->fs_info->trans_lock);
	wait_event(cur_trans->writer_wait,
		   atomic_read(&cur_trans->num_writers) == 1);

	/*
	 * the reloc mutex makes sure that we stop
	 * the balancing code from coming in and moving
	 * extents around in the middle of the commit
	 */
	mutex_lock(&root->fs_info->reloc_mutex);

	ret = btrfs_run_delayed_items(trans, root);
	BUG_ON(ret);

	ret = create_pending_snapshots(trans, root->fs_info);
	BUG_ON(ret);

	ret = btrfs_run_delayed_refs(trans, root, (unsigned long)-1);
	BUG_ON(ret);

	/*
	 * make sure none of the code above managed to slip in a
	 * delayed item
	 */
	btrfs_assert_delayed_root_empty(root);

	WARN_ON(cur_trans != trans->transaction);

	btrfs_scrub_pause(root);
	/* btrfs_commit_tree_roots is responsible for getting the
	 * various roots consistent with each other.  Every pointer
	 * in the tree of tree roots has to point to the most up to date
	 * root for every subvolume and other tree.  So, we have to keep
	 * the tree logging code from jumping in and changing any
	 * of the trees.
	 *
	 * At this point in the commit, there can't be any tree-log
	 * writers, but a little lower down we drop the trans mutex
	 * and let new people in.  By holding the tree_log_mutex
	 * from now until after the super is written, we avoid races
	 * with the tree-log code.
	 */
	mutex_lock(&root->fs_info->tree_log_mutex);

	ret = commit_fs_roots(trans, root);
	BUG_ON(ret);

	/* commit_fs_roots gets rid of all the tree log roots, it is now
	 * safe to free the root of tree log roots
	 */
	btrfs_free_log_root_tree(trans, root->fs_info);

	ret = commit_cowonly_roots(trans, root);
	BUG_ON(ret);

	btrfs_prepare_extent_commit(trans, root);

	cur_trans = root->fs_info->running_transaction;

	btrfs_set_root_node(&root->fs_info->tree_root->root_item,
			    root->fs_info->tree_root->node);
	switch_commit_root(root->fs_info->tree_root);

	btrfs_set_root_node(&root->fs_info->chunk_root->root_item,
			    root->fs_info->chunk_root->node);
	switch_commit_root(root->fs_info->chunk_root);

	update_super_roots(root);

	if (!root->fs_info->log_root_recovering) {
		btrfs_set_super_log_root(&root->fs_info->super_copy, 0);
		btrfs_set_super_log_root_level(&root->fs_info->super_copy, 0);
	}

	memcpy(&root->fs_info->super_for_commit, &root->fs_info->super_copy,
	       sizeof(root->fs_info->super_copy));

	trans->transaction->blocked = 0;
	spin_lock(&root->fs_info->trans_lock);
	root->fs_info->running_transaction = NULL;
	root->fs_info->trans_no_join = 0;
	spin_unlock(&root->fs_info->trans_lock);
	mutex_unlock(&root->fs_info->reloc_mutex);

	wake_up(&root->fs_info->transaction_wait);

	ret = btrfs_write_and_wait_transaction(trans, root);
	BUG_ON(ret);
	write_ctree_super(trans, root, 0);

	/*
	 * the super is written, we can safely allow the tree-loggers
	 * to go about their business
	 */
	mutex_unlock(&root->fs_info->tree_log_mutex);

	btrfs_finish_extent_commit(trans, root);

	cur_trans->commit_done = 1;

	root->fs_info->last_trans_committed = cur_trans->transid;

	wake_up(&cur_trans->commit_wait);

	spin_lock(&root->fs_info->trans_lock);
	list_del_init(&cur_trans->list);
	spin_unlock(&root->fs_info->trans_lock);

	put_transaction(cur_trans);
	put_transaction(cur_trans);

	trace_btrfs_transaction_commit(root);

	btrfs_scrub_continue(root);

	if (current->journal_info == trans)
		current->journal_info = NULL;

	kmem_cache_free(btrfs_trans_handle_cachep, trans);

	if (current != root->fs_info->transaction_kthread)
		btrfs_run_delayed_iputs(root);

	return ret;
}

/*
 * interface function to delete all the snapshots we have scheduled for deletion
 */
int btrfs_clean_old_snapshots(struct btrfs_root *root)
{
	LIST_HEAD(list);
	struct btrfs_fs_info *fs_info = root->fs_info;

	spin_lock(&fs_info->trans_lock);
	list_splice_init(&fs_info->dead_roots, &list);
	spin_unlock(&fs_info->trans_lock);

	while (!list_empty(&list)) {
		root = list_entry(list.next, struct btrfs_root, root_list);
		list_del(&root->root_list);

		btrfs_kill_all_delayed_nodes(root);

		if (btrfs_header_backref_rev(root->node) <
		    BTRFS_MIXED_BACKREF_REV)
			btrfs_drop_snapshot(root, NULL, 0);
		else
			btrfs_drop_snapshot(root, NULL, 1);
	}
	return 0;
}
