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
#include <linux/uuid.h>
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "locking.h"
#include "tree-log.h"
#include "inode-map.h"
#include "volumes.h"
#include "dev-replace.h"

#define BTRFS_ROOT_TRANS_TAG 0

void put_transaction(struct btrfs_transaction *transaction)
{
	WARN_ON(atomic_read(&transaction->use_count) == 0);
	if (atomic_dec_and_test(&transaction->use_count)) {
		BUG_ON(!list_empty(&transaction->list));
		WARN_ON(transaction->delayed_refs.root.rb_node);
		kmem_cache_free(btrfs_transaction_cachep, transaction);
	}
}

static noinline void switch_commit_root(struct btrfs_root *root)
{
	free_extent_buffer(root->commit_root);
	root->commit_root = btrfs_root_node(root);
}

static inline int can_join_transaction(struct btrfs_transaction *trans,
				       int type)
{
	return !(trans->in_commit &&
		 type != TRANS_JOIN &&
		 type != TRANS_JOIN_NOLOCK);
}

/*
 * either allocate a new transaction or hop into the existing one
 */
static noinline int join_transaction(struct btrfs_root *root, int type)
{
	struct btrfs_transaction *cur_trans;
	struct btrfs_fs_info *fs_info = root->fs_info;

	spin_lock(&fs_info->trans_lock);
loop:
	/* The file system has been taken offline. No new transactions. */
	if (test_bit(BTRFS_FS_STATE_ERROR, &fs_info->fs_state)) {
		spin_unlock(&fs_info->trans_lock);
		return -EROFS;
	}

	if (fs_info->trans_no_join) {
		/* 
		 * If we are JOIN_NOLOCK we're already committing a current
		 * transaction, we just need a handle to deal with something
		 * when committing the transaction, such as inode cache and
		 * space cache. It is a special case.
		 */
		if (type != TRANS_JOIN_NOLOCK) {
			spin_unlock(&fs_info->trans_lock);
			return -EBUSY;
		}
	}

	cur_trans = fs_info->running_transaction;
	if (cur_trans) {
		if (cur_trans->aborted) {
			spin_unlock(&fs_info->trans_lock);
			return cur_trans->aborted;
		}
		if (!can_join_transaction(cur_trans, type)) {
			spin_unlock(&fs_info->trans_lock);
			return -EBUSY;
		}
		atomic_inc(&cur_trans->use_count);
		atomic_inc(&cur_trans->num_writers);
		cur_trans->num_joined++;
		spin_unlock(&fs_info->trans_lock);
		return 0;
	}
	spin_unlock(&fs_info->trans_lock);

	/*
	 * If we are ATTACH, we just want to catch the current transaction,
	 * and commit it. If there is no transaction, just return ENOENT.
	 */
	if (type == TRANS_ATTACH)
		return -ENOENT;

	cur_trans = kmem_cache_alloc(btrfs_transaction_cachep, GFP_NOFS);
	if (!cur_trans)
		return -ENOMEM;

	spin_lock(&fs_info->trans_lock);
	if (fs_info->running_transaction) {
		/*
		 * someone started a transaction after we unlocked.  Make sure
		 * to redo the trans_no_join checks above
		 */
		kmem_cache_free(btrfs_transaction_cachep, cur_trans);
		goto loop;
	} else if (test_bit(BTRFS_FS_STATE_ERROR, &fs_info->fs_state)) {
		spin_unlock(&fs_info->trans_lock);
		kmem_cache_free(btrfs_transaction_cachep, cur_trans);
		return -EROFS;
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

	/*
	 * although the tree mod log is per file system and not per transaction,
	 * the log must never go across transaction boundaries.
	 */
	smp_mb();
	if (!list_empty(&fs_info->tree_mod_seq_list))
		WARN(1, KERN_ERR "btrfs: tree_mod_seq_list not empty when "
			"creating a fresh transaction\n");
	if (!RB_EMPTY_ROOT(&fs_info->tree_mod_log))
		WARN(1, KERN_ERR "btrfs: tree_mod_log rb tree not empty when "
			"creating a fresh transaction\n");
	atomic_set(&fs_info->tree_mod_seq, 0);

	spin_lock_init(&cur_trans->commit_lock);
	spin_lock_init(&cur_trans->delayed_refs.lock);
	atomic_set(&cur_trans->delayed_refs.procs_running_refs, 0);
	atomic_set(&cur_trans->delayed_refs.ref_seq, 0);
	init_waitqueue_head(&cur_trans->delayed_refs.wait);

	INIT_LIST_HEAD(&cur_trans->pending_snapshots);
	INIT_LIST_HEAD(&cur_trans->ordered_operations);
	list_add_tail(&cur_trans->list, &fs_info->trans_list);
	extent_io_tree_init(&cur_trans->dirty_pages,
			     fs_info->btree_inode->i_mapping);
	fs_info->generation++;
	cur_trans->transid = fs_info->generation;
	fs_info->running_transaction = cur_trans;
	cur_trans->aborted = 0;
	spin_unlock(&fs_info->trans_lock);

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
		atomic_inc(&cur_trans->use_count);
		spin_unlock(&root->fs_info->trans_lock);

		wait_event(root->fs_info->transaction_wait,
			   !cur_trans->blocked);
		put_transaction(cur_trans);
	} else {
		spin_unlock(&root->fs_info->trans_lock);
	}
}

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

static struct btrfs_trans_handle *
start_transaction(struct btrfs_root *root, u64 num_items, int type,
		  enum btrfs_reserve_flush_enum flush)
{
	struct btrfs_trans_handle *h;
	struct btrfs_transaction *cur_trans;
	u64 num_bytes = 0;
	int ret;
	u64 qgroup_reserved = 0;

	if (test_bit(BTRFS_FS_STATE_ERROR, &root->fs_info->fs_state))
		return ERR_PTR(-EROFS);

	if (current->journal_info) {
		WARN_ON(type != TRANS_JOIN && type != TRANS_JOIN_NOLOCK);
		h = current->journal_info;
		h->use_count++;
		WARN_ON(h->use_count > 2);
		h->orig_rsv = h->block_rsv;
		h->block_rsv = NULL;
		goto got_it;
	}

	/*
	 * Do the reservation before we join the transaction so we can do all
	 * the appropriate flushing if need be.
	 */
	if (num_items > 0 && root != root->fs_info->chunk_root) {
		if (root->fs_info->quota_enabled &&
		    is_fstree(root->root_key.objectid)) {
			qgroup_reserved = num_items * root->leafsize;
			ret = btrfs_qgroup_reserve(root, qgroup_reserved);
			if (ret)
				return ERR_PTR(ret);
		}

		num_bytes = btrfs_calc_trans_metadata_size(root, num_items);
		ret = btrfs_block_rsv_add(root,
					  &root->fs_info->trans_block_rsv,
					  num_bytes, flush);
		if (ret)
			goto reserve_fail;
	}
again:
	h = kmem_cache_alloc(btrfs_trans_handle_cachep, GFP_NOFS);
	if (!h) {
		ret = -ENOMEM;
		goto alloc_fail;
	}

	/*
	 * If we are JOIN_NOLOCK we're already committing a transaction and
	 * waiting on this guy, so we don't need to do the sb_start_intwrite
	 * because we're already holding a ref.  We need this because we could
	 * have raced in and did an fsync() on a file which can kick a commit
	 * and then we deadlock with somebody doing a freeze.
	 *
	 * If we are ATTACH, it means we just want to catch the current
	 * transaction and commit it, so we needn't do sb_start_intwrite(). 
	 */
	if (type < TRANS_JOIN_NOLOCK)
		sb_start_intwrite(root->fs_info->sb);

	if (may_wait_transaction(root, type))
		wait_current_trans(root);

	do {
		ret = join_transaction(root, type);
		if (ret == -EBUSY) {
			wait_current_trans(root);
			if (unlikely(type == TRANS_ATTACH))
				ret = -ENOENT;
		}
	} while (ret == -EBUSY);

	if (ret < 0) {
		/* We must get the transaction if we are JOIN_NOLOCK. */
		BUG_ON(type == TRANS_JOIN_NOLOCK);
		goto join_fail;
	}

	cur_trans = root->fs_info->running_transaction;

	h->transid = cur_trans->transid;
	h->transaction = cur_trans;
	h->blocks_used = 0;
	h->bytes_reserved = 0;
	h->root = root;
	h->delayed_ref_updates = 0;
	h->use_count = 1;
	h->adding_csums = 0;
	h->block_rsv = NULL;
	h->orig_rsv = NULL;
	h->aborted = 0;
	h->qgroup_reserved = 0;
	h->delayed_ref_elem.seq = 0;
	h->type = type;
	h->allocating_chunk = false;
	INIT_LIST_HEAD(&h->qgroup_ref_list);
	INIT_LIST_HEAD(&h->new_bgs);

	smp_mb();
	if (cur_trans->blocked && may_wait_transaction(root, type)) {
		btrfs_commit_transaction(h, root);
		goto again;
	}

	if (num_bytes) {
		trace_btrfs_space_reservation(root->fs_info, "transaction",
					      h->transid, num_bytes, 1);
		h->block_rsv = &root->fs_info->trans_block_rsv;
		h->bytes_reserved = num_bytes;
	}
	h->qgroup_reserved = qgroup_reserved;

got_it:
	btrfs_record_root_in_trans(h, root);

	if (!current->journal_info && type != TRANS_USERSPACE)
		current->journal_info = h;
	return h;

join_fail:
	if (type < TRANS_JOIN_NOLOCK)
		sb_end_intwrite(root->fs_info->sb);
	kmem_cache_free(btrfs_trans_handle_cachep, h);
alloc_fail:
	if (num_bytes)
		btrfs_block_rsv_release(root, &root->fs_info->trans_block_rsv,
					num_bytes);
reserve_fail:
	if (qgroup_reserved)
		btrfs_qgroup_free(root, qgroup_reserved);
	return ERR_PTR(ret);
}

struct btrfs_trans_handle *btrfs_start_transaction(struct btrfs_root *root,
						   int num_items)
{
	return start_transaction(root, num_items, TRANS_START,
				 BTRFS_RESERVE_FLUSH_ALL);
}

struct btrfs_trans_handle *btrfs_start_transaction_lflush(
					struct btrfs_root *root, int num_items)
{
	return start_transaction(root, num_items, TRANS_START,
				 BTRFS_RESERVE_FLUSH_LIMIT);
}

struct btrfs_trans_handle *btrfs_join_transaction(struct btrfs_root *root)
{
	return start_transaction(root, 0, TRANS_JOIN, 0);
}

struct btrfs_trans_handle *btrfs_join_transaction_nolock(struct btrfs_root *root)
{
	return start_transaction(root, 0, TRANS_JOIN_NOLOCK, 0);
}

struct btrfs_trans_handle *btrfs_start_ioctl_transaction(struct btrfs_root *root)
{
	return start_transaction(root, 0, TRANS_USERSPACE, 0);
}

/*
 * btrfs_attach_transaction() - catch the running transaction
 *
 * It is used when we want to commit the current the transaction, but
 * don't want to start a new one.
 *
 * Note: If this function return -ENOENT, it just means there is no
 * running transaction. But it is possible that the inactive transaction
 * is still in the memory, not fully on disk. If you hope there is no
 * inactive transaction in the fs when -ENOENT is returned, you should
 * invoke
 *     btrfs_attach_transaction_barrier()
 */
struct btrfs_trans_handle *btrfs_attach_transaction(struct btrfs_root *root)
{
	return start_transaction(root, 0, TRANS_ATTACH, 0);
}

/*
 * btrfs_attach_transaction() - catch the running transaction
 *
 * It is similar to the above function, the differentia is this one
 * will wait for all the inactive transactions until they fully
 * complete.
 */
struct btrfs_trans_handle *
btrfs_attach_transaction_barrier(struct btrfs_root *root)
{
	struct btrfs_trans_handle *trans;

	trans = start_transaction(root, 0, TRANS_ATTACH, 0);
	if (IS_ERR(trans) && PTR_ERR(trans) == -ENOENT)
		btrfs_wait_for_commit(root, 0);

	return trans;
}

/* wait for a transaction commit to be fully complete */
static noinline void wait_for_commit(struct btrfs_root *root,
				    struct btrfs_transaction *commit)
{
	wait_event(commit->commit_wait, commit->commit_done);
}

int btrfs_wait_for_commit(struct btrfs_root *root, u64 transid)
{
	struct btrfs_transaction *cur_trans = NULL, *t;
	int ret = 0;

	if (transid) {
		if (transid <= root->fs_info->last_trans_committed)
			goto out;

		ret = -EINVAL;
		/* find specified transaction */
		spin_lock(&root->fs_info->trans_lock);
		list_for_each_entry(t, &root->fs_info->trans_list, list) {
			if (t->transid == transid) {
				cur_trans = t;
				atomic_inc(&cur_trans->use_count);
				ret = 0;
				break;
			}
			if (t->transid > transid) {
				ret = 0;
				break;
			}
		}
		spin_unlock(&root->fs_info->trans_lock);
		/* The specified transaction doesn't exist */
		if (!cur_trans)
			goto out;
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

	ret = btrfs_block_rsv_check(root, &root->fs_info->global_block_rsv, 5);
	return ret ? 1 : 0;
}

int btrfs_should_end_transaction(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root)
{
	struct btrfs_transaction *cur_trans = trans->transaction;
	int updates;
	int err;

	smp_mb();
	if (cur_trans->blocked || cur_trans->delayed_refs.flushing)
		return 1;

	updates = trans->delayed_ref_updates;
	trans->delayed_ref_updates = 0;
	if (updates) {
		err = btrfs_run_delayed_refs(trans, root, updates);
		if (err) /* Error code will also eval true */
			return err;
	}

	return should_end_transaction(trans, root);
}

static int __btrfs_end_transaction(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root, int throttle)
{
	struct btrfs_transaction *cur_trans = trans->transaction;
	struct btrfs_fs_info *info = root->fs_info;
	int count = 0;
	int lock = (trans->type != TRANS_JOIN_NOLOCK);
	int err = 0;

	if (--trans->use_count) {
		trans->block_rsv = trans->orig_rsv;
		return 0;
	}

	/*
	 * do the qgroup accounting as early as possible
	 */
	err = btrfs_delayed_refs_qgroup_accounting(trans, info);

	btrfs_trans_release_metadata(trans, root);
	trans->block_rsv = NULL;
	/*
	 * the same root has to be passed to start_transaction and
	 * end_transaction. Subvolume quota depends on this.
	 */
	WARN_ON(trans->root != root);

	if (trans->qgroup_reserved) {
		btrfs_qgroup_free(root, trans->qgroup_reserved);
		trans->qgroup_reserved = 0;
	}

	if (!list_empty(&trans->new_bgs))
		btrfs_create_pending_block_groups(trans, root);

	while (count < 1) {
		unsigned long cur = trans->delayed_ref_updates;
		trans->delayed_ref_updates = 0;
		if (cur &&
		    trans->transaction->delayed_refs.num_heads_ready > 64) {
			trans->delayed_ref_updates = 0;
			btrfs_run_delayed_refs(trans, root, cur);
		} else {
			break;
		}
		count++;
	}

	btrfs_trans_release_metadata(trans, root);
	trans->block_rsv = NULL;

	if (!list_empty(&trans->new_bgs))
		btrfs_create_pending_block_groups(trans, root);

	if (lock && !atomic_read(&root->fs_info->open_ioctl_trans) &&
	    should_end_transaction(trans, root)) {
		trans->transaction->blocked = 1;
		smp_wmb();
	}

	if (lock && cur_trans->blocked && !cur_trans->in_commit) {
		if (throttle) {
			/*
			 * We may race with somebody else here so end up having
			 * to call end_transaction on ourselves again, so inc
			 * our use_count.
			 */
			trans->use_count++;
			return btrfs_commit_transaction(trans, root);
		} else {
			wake_up_process(info->transaction_kthread);
		}
	}

	if (trans->type < TRANS_JOIN_NOLOCK)
		sb_end_intwrite(root->fs_info->sb);

	WARN_ON(cur_trans != info->running_transaction);
	WARN_ON(atomic_read(&cur_trans->num_writers) < 1);
	atomic_dec(&cur_trans->num_writers);

	smp_mb();
	if (waitqueue_active(&cur_trans->writer_wait))
		wake_up(&cur_trans->writer_wait);
	put_transaction(cur_trans);

	if (current->journal_info == trans)
		current->journal_info = NULL;

	if (throttle)
		btrfs_run_delayed_iputs(root);

	if (trans->aborted ||
	    test_bit(BTRFS_FS_STATE_ERROR, &root->fs_info->fs_state))
		err = -EIO;
	assert_qgroups_uptodate(trans);

	kmem_cache_free(btrfs_trans_handle_cachep, trans);
	return err;
}

int btrfs_end_transaction(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root)
{
	int ret;

	ret = __btrfs_end_transaction(trans, root, 0);
	if (ret)
		return ret;
	return 0;
}

int btrfs_end_transaction_throttle(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root)
{
	int ret;

	ret = __btrfs_end_transaction(trans, root, 1);
	if (ret)
		return ret;
	return 0;
}

int btrfs_end_transaction_dmeta(struct btrfs_trans_handle *trans,
				struct btrfs_root *root)
{
	return __btrfs_end_transaction(trans, root, 1);
}

/*
 * when btree blocks are allocated, they have some corresponding bits set for
 * them in one of two extent_io trees.  This is used to make sure all of
 * those extents are sent to disk but does not wait on them
 */
int btrfs_write_marked_extents(struct btrfs_root *root,
			       struct extent_io_tree *dirty_pages, int mark)
{
	int err = 0;
	int werr = 0;
	struct address_space *mapping = root->fs_info->btree_inode->i_mapping;
	struct extent_state *cached_state = NULL;
	u64 start = 0;
	u64 end;
	struct blk_plug plug;

	blk_start_plug(&plug);
	while (!find_first_extent_bit(dirty_pages, start, &start, &end,
				      mark, &cached_state)) {
		convert_extent_bit(dirty_pages, start, end, EXTENT_NEED_WAIT,
				   mark, &cached_state, GFP_NOFS);
		cached_state = NULL;
		err = filemap_fdatawrite_range(mapping, start, end);
		if (err)
			werr = err;
		cond_resched();
		start = end + 1;
	}
	if (err)
		werr = err;
	blk_finish_plug(&plug);
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
	int err = 0;
	int werr = 0;
	struct address_space *mapping = root->fs_info->btree_inode->i_mapping;
	struct extent_state *cached_state = NULL;
	u64 start = 0;
	u64 end;

	while (!find_first_extent_bit(dirty_pages, start, &start, &end,
				      EXTENT_NEED_WAIT, &cached_state)) {
		clear_extent_bit(dirty_pages, start, end, EXTENT_NEED_WAIT,
				 0, 0, &cached_state, GFP_NOFS);
		err = filemap_fdatawait_range(mapping, start, end);
		if (err)
			werr = err;
		cond_resched();
		start = end + 1;
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

	if (ret)
		return ret;
	if (ret2)
		return ret2;
	return 0;
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
		if (ret)
			return ret;

		old_root_used = btrfs_root_used(&root->root_item);
		ret = btrfs_write_dirty_block_groups(trans, root);
		if (ret)
			return ret;
	}

	if (root != root->fs_info->extent_root)
		switch_commit_root(root);

	return 0;
}

/*
 * update all the cowonly tree roots on disk
 *
 * The error handling in this function may not be obvious. Any of the
 * failures will cause the file system to go offline. We still need
 * to clean up the delayed refs.
 */
static noinline int commit_cowonly_roots(struct btrfs_trans_handle *trans,
					 struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct list_head *next;
	struct extent_buffer *eb;
	int ret;

	ret = btrfs_run_delayed_refs(trans, root, (unsigned long)-1);
	if (ret)
		return ret;

	eb = btrfs_lock_root_node(fs_info->tree_root);
	ret = btrfs_cow_block(trans, fs_info->tree_root, eb, NULL,
			      0, &eb);
	btrfs_tree_unlock(eb);
	free_extent_buffer(eb);

	if (ret)
		return ret;

	ret = btrfs_run_delayed_refs(trans, root, (unsigned long)-1);
	if (ret)
		return ret;

	ret = btrfs_run_dev_stats(trans, root->fs_info);
	WARN_ON(ret);
	ret = btrfs_run_dev_replace(trans, root->fs_info);
	WARN_ON(ret);

	ret = btrfs_run_qgroups(trans, root->fs_info);
	BUG_ON(ret);

	/* run_qgroups might have added some more refs */
	ret = btrfs_run_delayed_refs(trans, root, (unsigned long)-1);
	BUG_ON(ret);

	while (!list_empty(&fs_info->dirty_cowonly_roots)) {
		next = fs_info->dirty_cowonly_roots.next;
		list_del_init(next);
		root = list_entry(next, struct btrfs_root, dirty_list);

		ret = update_cowonly_root(trans, root);
		if (ret)
			return ret;
	}

	down_write(&fs_info->extent_commit_sem);
	switch_commit_root(fs_info->extent_root);
	up_write(&fs_info->extent_commit_sem);

	btrfs_after_dev_replace_commit(fs_info);

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

			/* see comments in should_cow_block() */
			root->force_cow = 0;
			smp_wmb();

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
 * defrag a given btree.
 * Every leaf in the btree is read and defragged.
 */
int btrfs_defrag_root(struct btrfs_root *root)
{
	struct btrfs_fs_info *info = root->fs_info;
	struct btrfs_trans_handle *trans;
	int ret;

	if (xchg(&root->defrag_running, 1))
		return 0;

	while (1) {
		trans = btrfs_start_transaction(root, 0);
		if (IS_ERR(trans))
			return PTR_ERR(trans);

		ret = btrfs_defrag_leaves(trans, root);

		btrfs_end_transaction(trans, root);
		btrfs_btree_balance_dirty(info->tree_root);
		cond_resched();

		if (btrfs_fs_closing(root->fs_info) || ret != -EAGAIN)
			break;

		if (btrfs_defrag_cancelled(root->fs_info)) {
			printk(KERN_DEBUG "btrfs: defrag_root cancelled\n");
			ret = -EAGAIN;
			break;
		}
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
	struct btrfs_block_rsv *rsv;
	struct inode *parent_inode;
	struct btrfs_path *path;
	struct btrfs_dir_item *dir_item;
	struct dentry *dentry;
	struct extent_buffer *tmp;
	struct extent_buffer *old;
	struct timespec cur_time = CURRENT_TIME;
	int ret;
	u64 to_reserve = 0;
	u64 index = 0;
	u64 objectid;
	u64 root_flags;
	uuid_le new_uuid;

	path = btrfs_alloc_path();
	if (!path) {
		ret = pending->error = -ENOMEM;
		return ret;
	}

	new_root_item = kmalloc(sizeof(*new_root_item), GFP_NOFS);
	if (!new_root_item) {
		ret = pending->error = -ENOMEM;
		goto root_item_alloc_fail;
	}

	ret = btrfs_find_free_objectid(tree_root, &objectid);
	if (ret) {
		pending->error = ret;
		goto no_free_objectid;
	}

	btrfs_reloc_pre_snapshot(trans, pending, &to_reserve);

	if (to_reserve > 0) {
		ret = btrfs_block_rsv_add(root, &pending->block_rsv,
					  to_reserve,
					  BTRFS_RESERVE_NO_FLUSH);
		if (ret) {
			pending->error = ret;
			goto no_free_objectid;
		}
	}

	ret = btrfs_qgroup_inherit(trans, fs_info, root->root_key.objectid,
				   objectid, pending->inherit);
	if (ret) {
		pending->error = ret;
		goto no_free_objectid;
	}

	key.objectid = objectid;
	key.offset = (u64)-1;
	key.type = BTRFS_ROOT_ITEM_KEY;

	rsv = trans->block_rsv;
	trans->block_rsv = &pending->block_rsv;
	trans->bytes_reserved = trans->block_rsv->reserved;

	dentry = pending->dentry;
	parent_inode = pending->dir;
	parent_root = BTRFS_I(parent_inode)->root;
	record_root_in_trans(trans, parent_root);

	/*
	 * insert the directory item
	 */
	ret = btrfs_set_inode_index(parent_inode, &index);
	BUG_ON(ret); /* -ENOMEM */

	/* check if there is a file/dir which has the same name. */
	dir_item = btrfs_lookup_dir_item(NULL, parent_root, path,
					 btrfs_ino(parent_inode),
					 dentry->d_name.name,
					 dentry->d_name.len, 0);
	if (dir_item != NULL && !IS_ERR(dir_item)) {
		pending->error = -EEXIST;
		goto fail;
	} else if (IS_ERR(dir_item)) {
		ret = PTR_ERR(dir_item);
		btrfs_abort_transaction(trans, root, ret);
		goto fail;
	}
	btrfs_release_path(path);

	/*
	 * pull in the delayed directory update
	 * and the delayed inode item
	 * otherwise we corrupt the FS during
	 * snapshot
	 */
	ret = btrfs_run_delayed_items(trans, root);
	if (ret) {	/* Transaction aborted */
		btrfs_abort_transaction(trans, root, ret);
		goto fail;
	}

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

	btrfs_set_root_generation_v2(new_root_item,
			trans->transid);
	uuid_le_gen(&new_uuid);
	memcpy(new_root_item->uuid, new_uuid.b, BTRFS_UUID_SIZE);
	memcpy(new_root_item->parent_uuid, root->root_item.uuid,
			BTRFS_UUID_SIZE);
	new_root_item->otime.sec = cpu_to_le64(cur_time.tv_sec);
	new_root_item->otime.nsec = cpu_to_le32(cur_time.tv_nsec);
	btrfs_set_root_otransid(new_root_item, trans->transid);
	memset(&new_root_item->stime, 0, sizeof(new_root_item->stime));
	memset(&new_root_item->rtime, 0, sizeof(new_root_item->rtime));
	btrfs_set_root_stransid(new_root_item, 0);
	btrfs_set_root_rtransid(new_root_item, 0);

	old = btrfs_lock_root_node(root);
	ret = btrfs_cow_block(trans, root, old, NULL, 0, &old);
	if (ret) {
		btrfs_tree_unlock(old);
		free_extent_buffer(old);
		btrfs_abort_transaction(trans, root, ret);
		goto fail;
	}

	btrfs_set_lock_blocking(old);

	ret = btrfs_copy_root(trans, root, old, &tmp, objectid);
	/* clean up in any case */
	btrfs_tree_unlock(old);
	free_extent_buffer(old);
	if (ret) {
		btrfs_abort_transaction(trans, root, ret);
		goto fail;
	}

	/* see comments in should_cow_block() */
	root->force_cow = 1;
	smp_wmb();

	btrfs_set_root_node(new_root_item, tmp);
	/* record when the snapshot was created in key.offset */
	key.offset = trans->transid;
	ret = btrfs_insert_root(trans, tree_root, &key, new_root_item);
	btrfs_tree_unlock(tmp);
	free_extent_buffer(tmp);
	if (ret) {
		btrfs_abort_transaction(trans, root, ret);
		goto fail;
	}

	/*
	 * insert root back/forward references
	 */
	ret = btrfs_add_root_ref(trans, tree_root, objectid,
				 parent_root->root_key.objectid,
				 btrfs_ino(parent_inode), index,
				 dentry->d_name.name, dentry->d_name.len);
	if (ret) {
		btrfs_abort_transaction(trans, root, ret);
		goto fail;
	}

	key.offset = (u64)-1;
	pending->snap = btrfs_read_fs_root_no_name(root->fs_info, &key);
	if (IS_ERR(pending->snap)) {
		ret = PTR_ERR(pending->snap);
		btrfs_abort_transaction(trans, root, ret);
		goto fail;
	}

	ret = btrfs_reloc_post_snapshot(trans, pending);
	if (ret) {
		btrfs_abort_transaction(trans, root, ret);
		goto fail;
	}

	ret = btrfs_run_delayed_refs(trans, root, (unsigned long)-1);
	if (ret) {
		btrfs_abort_transaction(trans, root, ret);
		goto fail;
	}

	ret = btrfs_insert_dir_item(trans, parent_root,
				    dentry->d_name.name, dentry->d_name.len,
				    parent_inode, &key,
				    BTRFS_FT_DIR, index);
	/* We have check then name at the beginning, so it is impossible. */
	BUG_ON(ret == -EEXIST || ret == -EOVERFLOW);
	if (ret) {
		btrfs_abort_transaction(trans, root, ret);
		goto fail;
	}

	btrfs_i_size_write(parent_inode, parent_inode->i_size +
					 dentry->d_name.len * 2);
	parent_inode->i_mtime = parent_inode->i_ctime = CURRENT_TIME;
	ret = btrfs_update_inode_fallback(trans, parent_root, parent_inode);
	if (ret)
		btrfs_abort_transaction(trans, root, ret);
fail:
	trans->block_rsv = rsv;
	trans->bytes_reserved = 0;
no_free_objectid:
	kfree(new_root_item);
root_item_alloc_fail:
	btrfs_free_path(path);
	return ret;
}

/*
 * create all the snapshots we've scheduled for creation
 */
static noinline int create_pending_snapshots(struct btrfs_trans_handle *trans,
					     struct btrfs_fs_info *fs_info)
{
	struct btrfs_pending_snapshot *pending;
	struct list_head *head = &trans->transaction->pending_snapshots;

	list_for_each_entry(pending, head, list)
		create_pending_snapshot(trans, fs_info, pending);
	return 0;
}

static void update_super_roots(struct btrfs_root *root)
{
	struct btrfs_root_item *root_item;
	struct btrfs_super_block *super;

	super = root->fs_info->super_copy;

	root_item = &root->fs_info->chunk_root->root_item;
	super->chunk_root = root_item->bytenr;
	super->chunk_root_generation = root_item->generation;
	super->chunk_root_level = root_item->level;

	root_item = &root->fs_info->tree_root->root_item;
	super->root = root_item->bytenr;
	super->generation = root_item->generation;
	super->root_level = root_item->level;
	if (btrfs_test_opt(root, SPACE_CACHE))
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
	wait_event(root->fs_info->transaction_blocked_wait, trans->in_commit);
}

/*
 * wait for the current transaction to start and then become unblocked.
 * caller holds ref.
 */
static void wait_current_trans_commit_start_and_unblock(struct btrfs_root *root,
					 struct btrfs_transaction *trans)
{
	wait_event(root->fs_info->transaction_wait,
		   trans->commit_done || (trans->in_commit && !trans->blocked));
}

/*
 * commit transactions asynchronously. once btrfs_commit_transaction_async
 * returns, any subsequent transaction will not be allowed to join.
 */
struct btrfs_async_commit {
	struct btrfs_trans_handle *newtrans;
	struct btrfs_root *root;
	struct work_struct work;
};

static void do_async_commit(struct work_struct *work)
{
	struct btrfs_async_commit *ac =
		container_of(work, struct btrfs_async_commit, work);

	/*
	 * We've got freeze protection passed with the transaction.
	 * Tell lockdep about it.
	 */
	if (ac->newtrans->type < TRANS_JOIN_NOLOCK)
		rwsem_acquire_read(
		     &ac->root->fs_info->sb->s_writers.lock_map[SB_FREEZE_FS-1],
		     0, 1, _THIS_IP_);

	current->journal_info = ac->newtrans;

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

	INIT_WORK(&ac->work, do_async_commit);
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

	/*
	 * Tell lockdep we've released the freeze rwsem, since the
	 * async commit thread will be the one to unlock it.
	 */
	if (trans->type < TRANS_JOIN_NOLOCK)
		rwsem_release(
			&root->fs_info->sb->s_writers.lock_map[SB_FREEZE_FS-1],
			1, _THIS_IP_);

	schedule_work(&ac->work);

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


static void cleanup_transaction(struct btrfs_trans_handle *trans,
				struct btrfs_root *root, int err)
{
	struct btrfs_transaction *cur_trans = trans->transaction;
	DEFINE_WAIT(wait);

	WARN_ON(trans->use_count > 1);

	btrfs_abort_transaction(trans, root, err);

	spin_lock(&root->fs_info->trans_lock);
	list_del_init(&cur_trans->list);
	if (cur_trans == root->fs_info->running_transaction) {
		root->fs_info->trans_no_join = 1;
		spin_unlock(&root->fs_info->trans_lock);
		wait_event(cur_trans->writer_wait,
			   atomic_read(&cur_trans->num_writers) == 1);

		spin_lock(&root->fs_info->trans_lock);
		root->fs_info->running_transaction = NULL;
	}
	spin_unlock(&root->fs_info->trans_lock);

	btrfs_cleanup_one_transaction(trans->transaction, root);

	put_transaction(cur_trans);
	put_transaction(cur_trans);

	trace_btrfs_transaction_commit(root);

	btrfs_scrub_continue(root);

	if (current->journal_info == trans)
		current->journal_info = NULL;

	kmem_cache_free(btrfs_trans_handle_cachep, trans);
}

static int btrfs_flush_all_pending_stuffs(struct btrfs_trans_handle *trans,
					  struct btrfs_root *root)
{
	int flush_on_commit = btrfs_test_opt(root, FLUSHONCOMMIT);
	int snap_pending = 0;
	int ret;

	if (!flush_on_commit) {
		spin_lock(&root->fs_info->trans_lock);
		if (!list_empty(&trans->transaction->pending_snapshots))
			snap_pending = 1;
		spin_unlock(&root->fs_info->trans_lock);
	}

	if (flush_on_commit || snap_pending) {
		ret = btrfs_start_delalloc_inodes(root, 1);
		if (ret)
			return ret;
		btrfs_wait_ordered_extents(root, 1);
	}

	ret = btrfs_run_delayed_items(trans, root);
	if (ret)
		return ret;

	/*
	 * running the delayed items may have added new refs. account
	 * them now so that they hinder processing of more delayed refs
	 * as little as possible.
	 */
	btrfs_delayed_refs_qgroup_accounting(trans, root->fs_info);

	/*
	 * rename don't use btrfs_join_transaction, so, once we
	 * set the transaction to blocked above, we aren't going
	 * to get any new ordered operations.  We can safely run
	 * it here and no for sure that nothing new will be added
	 * to the list
	 */
	ret = btrfs_run_ordered_operations(trans, root, 1);

	return ret;
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
	struct btrfs_transaction *cur_trans = trans->transaction;
	struct btrfs_transaction *prev_trans = NULL;
	DEFINE_WAIT(wait);
	int ret;
	int should_grow = 0;
	unsigned long now = get_seconds();

	ret = btrfs_run_ordered_operations(trans, root, 0);
	if (ret) {
		btrfs_abort_transaction(trans, root, ret);
		btrfs_end_transaction(trans, root);
		return ret;
	}

	/* Stop the commit early if ->aborted is set */
	if (unlikely(ACCESS_ONCE(cur_trans->aborted))) {
		ret = cur_trans->aborted;
		btrfs_end_transaction(trans, root);
		return ret;
	}

	/* make a pass through all the delayed refs we have so far
	 * any runnings procs may add more while we are here
	 */
	ret = btrfs_run_delayed_refs(trans, root, 0);
	if (ret) {
		btrfs_end_transaction(trans, root);
		return ret;
	}

	btrfs_trans_release_metadata(trans, root);
	trans->block_rsv = NULL;
	if (trans->qgroup_reserved) {
		btrfs_qgroup_free(root, trans->qgroup_reserved);
		trans->qgroup_reserved = 0;
	}

	cur_trans = trans->transaction;

	/*
	 * set the flushing flag so procs in this transaction have to
	 * start sending their work down.
	 */
	cur_trans->delayed_refs.flushing = 1;

	if (!list_empty(&trans->new_bgs))
		btrfs_create_pending_block_groups(trans, root);

	ret = btrfs_run_delayed_refs(trans, root, 0);
	if (ret) {
		btrfs_end_transaction(trans, root);
		return ret;
	}

	spin_lock(&cur_trans->commit_lock);
	if (cur_trans->in_commit) {
		spin_unlock(&cur_trans->commit_lock);
		atomic_inc(&cur_trans->use_count);
		ret = btrfs_end_transaction(trans, root);

		wait_for_commit(root, cur_trans);

		put_transaction(cur_trans);

		return ret;
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

	if (!btrfs_test_opt(root, SSD) &&
	    (now < cur_trans->start_time || now - cur_trans->start_time < 1))
		should_grow = 1;

	do {
		joined = cur_trans->num_joined;

		WARN_ON(cur_trans != trans->transaction);

		ret = btrfs_flush_all_pending_stuffs(trans, root);
		if (ret)
			goto cleanup_transaction;

		prepare_to_wait(&cur_trans->writer_wait, &wait,
				TASK_UNINTERRUPTIBLE);

		if (atomic_read(&cur_trans->num_writers) > 1)
			schedule_timeout(MAX_SCHEDULE_TIMEOUT);
		else if (should_grow)
			schedule_timeout(1);

		finish_wait(&cur_trans->writer_wait, &wait);
	} while (atomic_read(&cur_trans->num_writers) > 1 ||
		 (should_grow && cur_trans->num_joined != joined));

	ret = btrfs_flush_all_pending_stuffs(trans, root);
	if (ret)
		goto cleanup_transaction;

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

	/* ->aborted might be set after the previous check, so check it */
	if (unlikely(ACCESS_ONCE(cur_trans->aborted))) {
		ret = cur_trans->aborted;
		goto cleanup_transaction;
	}
	/*
	 * the reloc mutex makes sure that we stop
	 * the balancing code from coming in and moving
	 * extents around in the middle of the commit
	 */
	mutex_lock(&root->fs_info->reloc_mutex);

	/*
	 * We needn't worry about the delayed items because we will
	 * deal with them in create_pending_snapshot(), which is the
	 * core function of the snapshot creation.
	 */
	ret = create_pending_snapshots(trans, root->fs_info);
	if (ret) {
		mutex_unlock(&root->fs_info->reloc_mutex);
		goto cleanup_transaction;
	}

	/*
	 * We insert the dir indexes of the snapshots and update the inode
	 * of the snapshots' parents after the snapshot creation, so there
	 * are some delayed items which are not dealt with. Now deal with
	 * them.
	 *
	 * We needn't worry that this operation will corrupt the snapshots,
	 * because all the tree which are snapshoted will be forced to COW
	 * the nodes and leaves.
	 */
	ret = btrfs_run_delayed_items(trans, root);
	if (ret) {
		mutex_unlock(&root->fs_info->reloc_mutex);
		goto cleanup_transaction;
	}

	ret = btrfs_run_delayed_refs(trans, root, (unsigned long)-1);
	if (ret) {
		mutex_unlock(&root->fs_info->reloc_mutex);
		goto cleanup_transaction;
	}

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
	if (ret) {
		mutex_unlock(&root->fs_info->tree_log_mutex);
		mutex_unlock(&root->fs_info->reloc_mutex);
		goto cleanup_transaction;
	}

	/* commit_fs_roots gets rid of all the tree log roots, it is now
	 * safe to free the root of tree log roots
	 */
	btrfs_free_log_root_tree(trans, root->fs_info);

	ret = commit_cowonly_roots(trans, root);
	if (ret) {
		mutex_unlock(&root->fs_info->tree_log_mutex);
		mutex_unlock(&root->fs_info->reloc_mutex);
		goto cleanup_transaction;
	}

	/*
	 * The tasks which save the space cache and inode cache may also
	 * update ->aborted, check it.
	 */
	if (unlikely(ACCESS_ONCE(cur_trans->aborted))) {
		ret = cur_trans->aborted;
		mutex_unlock(&root->fs_info->tree_log_mutex);
		mutex_unlock(&root->fs_info->reloc_mutex);
		goto cleanup_transaction;
	}

	btrfs_prepare_extent_commit(trans, root);

	cur_trans = root->fs_info->running_transaction;

	btrfs_set_root_node(&root->fs_info->tree_root->root_item,
			    root->fs_info->tree_root->node);
	switch_commit_root(root->fs_info->tree_root);

	btrfs_set_root_node(&root->fs_info->chunk_root->root_item,
			    root->fs_info->chunk_root->node);
	switch_commit_root(root->fs_info->chunk_root);

	assert_qgroups_uptodate(trans);
	update_super_roots(root);

	if (!root->fs_info->log_root_recovering) {
		btrfs_set_super_log_root(root->fs_info->super_copy, 0);
		btrfs_set_super_log_root_level(root->fs_info->super_copy, 0);
	}

	memcpy(root->fs_info->super_for_commit, root->fs_info->super_copy,
	       sizeof(*root->fs_info->super_copy));

	trans->transaction->blocked = 0;
	spin_lock(&root->fs_info->trans_lock);
	root->fs_info->running_transaction = NULL;
	root->fs_info->trans_no_join = 0;
	spin_unlock(&root->fs_info->trans_lock);
	mutex_unlock(&root->fs_info->reloc_mutex);

	wake_up(&root->fs_info->transaction_wait);

	ret = btrfs_write_and_wait_transaction(trans, root);
	if (ret) {
		btrfs_error(root->fs_info, ret,
			    "Error while writing out transaction.");
		mutex_unlock(&root->fs_info->tree_log_mutex);
		goto cleanup_transaction;
	}

	ret = write_ctree_super(trans, root, 0);
	if (ret) {
		mutex_unlock(&root->fs_info->tree_log_mutex);
		goto cleanup_transaction;
	}

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

	if (trans->type < TRANS_JOIN_NOLOCK)
		sb_end_intwrite(root->fs_info->sb);

	trace_btrfs_transaction_commit(root);

	btrfs_scrub_continue(root);

	if (current->journal_info == trans)
		current->journal_info = NULL;

	kmem_cache_free(btrfs_trans_handle_cachep, trans);

	if (current != root->fs_info->transaction_kthread)
		btrfs_run_delayed_iputs(root);

	return ret;

cleanup_transaction:
	btrfs_trans_release_metadata(trans, root);
	trans->block_rsv = NULL;
	if (trans->qgroup_reserved) {
		btrfs_qgroup_free(root, trans->qgroup_reserved);
		trans->qgroup_reserved = 0;
	}
	btrfs_printk(root->fs_info, "Skipping commit of aborted transaction.\n");
//	WARN_ON(1);
	if (current->journal_info == trans)
		current->journal_info = NULL;
	cleanup_transaction(trans, root, ret);

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
		int ret;

		root = list_entry(list.next, struct btrfs_root, root_list);
		list_del(&root->root_list);

		btrfs_kill_all_delayed_nodes(root);

		if (btrfs_header_backref_rev(root->node) <
		    BTRFS_MIXED_BACKREF_REV)
			ret = btrfs_drop_snapshot(root, NULL, 0, 0);
		else
			ret =btrfs_drop_snapshot(root, NULL, 1, 0);
		BUG_ON(ret < 0);
	}
	return 0;
}
