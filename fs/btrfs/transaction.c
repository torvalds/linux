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
#include <linux/sched.h>
#include <linux/writeback.h>
#include <linux/pagemap.h>
#include <linux/blkdev.h>
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "locking.h"
#include "ref-cache.h"
#include "tree-log.h"

#define BTRFS_ROOT_TRANS_TAG 0

static noinline void put_transaction(struct btrfs_transaction *transaction)
{
	WARN_ON(transaction->use_count == 0);
	transaction->use_count--;
	if (transaction->use_count == 0) {
		list_del_init(&transaction->list);
		memset(transaction, 0, sizeof(*transaction));
		kmem_cache_free(btrfs_transaction_cachep, transaction);
	}
}

/*
 * either allocate a new transaction or hop into the existing one
 */
static noinline int join_transaction(struct btrfs_root *root)
{
	struct btrfs_transaction *cur_trans;
	cur_trans = root->fs_info->running_transaction;
	if (!cur_trans) {
		cur_trans = kmem_cache_alloc(btrfs_transaction_cachep,
					     GFP_NOFS);
		BUG_ON(!cur_trans);
		root->fs_info->generation++;
		root->fs_info->last_alloc = 0;
		root->fs_info->last_data_alloc = 0;
		cur_trans->num_writers = 1;
		cur_trans->num_joined = 0;
		cur_trans->transid = root->fs_info->generation;
		init_waitqueue_head(&cur_trans->writer_wait);
		init_waitqueue_head(&cur_trans->commit_wait);
		cur_trans->in_commit = 0;
		cur_trans->blocked = 0;
		cur_trans->use_count = 1;
		cur_trans->commit_done = 0;
		cur_trans->start_time = get_seconds();
		INIT_LIST_HEAD(&cur_trans->pending_snapshots);
		list_add_tail(&cur_trans->list, &root->fs_info->trans_list);
		extent_io_tree_init(&cur_trans->dirty_pages,
				     root->fs_info->btree_inode->i_mapping,
				     GFP_NOFS);
		spin_lock(&root->fs_info->new_trans_lock);
		root->fs_info->running_transaction = cur_trans;
		spin_unlock(&root->fs_info->new_trans_lock);
	} else {
		cur_trans->num_writers++;
		cur_trans->num_joined++;
	}

	return 0;
}

/*
 * this does all the record keeping required to make sure that a reference
 * counted root is properly recorded in a given transaction.  This is required
 * to make sure the old root from before we joined the transaction is deleted
 * when the transaction commits
 */
noinline int btrfs_record_root_in_trans(struct btrfs_root *root)
{
	struct btrfs_dirty_root *dirty;
	u64 running_trans_id = root->fs_info->running_transaction->transid;
	if (root->ref_cows && root->last_trans < running_trans_id) {
		WARN_ON(root == root->fs_info->extent_root);
		if (root->root_item.refs != 0) {
			radix_tree_tag_set(&root->fs_info->fs_roots_radix,
				   (unsigned long)root->root_key.objectid,
				   BTRFS_ROOT_TRANS_TAG);

			dirty = kmalloc(sizeof(*dirty), GFP_NOFS);
			BUG_ON(!dirty);
			dirty->root = kmalloc(sizeof(*dirty->root), GFP_NOFS);
			BUG_ON(!dirty->root);
			dirty->latest_root = root;
			INIT_LIST_HEAD(&dirty->list);

			root->commit_root = btrfs_root_node(root);

			memcpy(dirty->root, root, sizeof(*root));
			spin_lock_init(&dirty->root->node_lock);
			spin_lock_init(&dirty->root->list_lock);
			mutex_init(&dirty->root->objectid_mutex);
			mutex_init(&dirty->root->log_mutex);
			INIT_LIST_HEAD(&dirty->root->dead_list);
			dirty->root->node = root->commit_root;
			dirty->root->commit_root = NULL;

			spin_lock(&root->list_lock);
			list_add(&dirty->root->dead_list, &root->dead_list);
			spin_unlock(&root->list_lock);

			root->dirty_root = dirty;
		} else {
			WARN_ON(1);
		}
		root->last_trans = running_trans_id;
	}
	return 0;
}

/* wait for commit against the current transaction to become unblocked
 * when this is done, it is safe to start a new transaction, but the current
 * transaction might not be fully on disk.
 */
static void wait_current_trans(struct btrfs_root *root)
{
	struct btrfs_transaction *cur_trans;

	cur_trans = root->fs_info->running_transaction;
	if (cur_trans && cur_trans->blocked) {
		DEFINE_WAIT(wait);
		cur_trans->use_count++;
		while (1) {
			prepare_to_wait(&root->fs_info->transaction_wait, &wait,
					TASK_UNINTERRUPTIBLE);
			if (cur_trans->blocked) {
				mutex_unlock(&root->fs_info->trans_mutex);
				schedule();
				mutex_lock(&root->fs_info->trans_mutex);
				finish_wait(&root->fs_info->transaction_wait,
					    &wait);
			} else {
				finish_wait(&root->fs_info->transaction_wait,
					    &wait);
				break;
			}
		}
		put_transaction(cur_trans);
	}
}

static struct btrfs_trans_handle *start_transaction(struct btrfs_root *root,
					     int num_blocks, int wait)
{
	struct btrfs_trans_handle *h =
		kmem_cache_alloc(btrfs_trans_handle_cachep, GFP_NOFS);
	int ret;

	mutex_lock(&root->fs_info->trans_mutex);
	if (!root->fs_info->log_root_recovering &&
	    ((wait == 1 && !root->fs_info->open_ioctl_trans) || wait == 2))
		wait_current_trans(root);
	ret = join_transaction(root);
	BUG_ON(ret);

	btrfs_record_root_in_trans(root);
	h->transid = root->fs_info->running_transaction->transid;
	h->transaction = root->fs_info->running_transaction;
	h->blocks_reserved = num_blocks;
	h->blocks_used = 0;
	h->block_group = 0;
	h->alloc_exclude_nr = 0;
	h->alloc_exclude_start = 0;
	root->fs_info->running_transaction->use_count++;
	mutex_unlock(&root->fs_info->trans_mutex);
	return h;
}

struct btrfs_trans_handle *btrfs_start_transaction(struct btrfs_root *root,
						   int num_blocks)
{
	return start_transaction(root, num_blocks, 1);
}
struct btrfs_trans_handle *btrfs_join_transaction(struct btrfs_root *root,
						   int num_blocks)
{
	return start_transaction(root, num_blocks, 0);
}

struct btrfs_trans_handle *btrfs_start_ioctl_transaction(struct btrfs_root *r,
							 int num_blocks)
{
	return start_transaction(r, num_blocks, 2);
}

/* wait for a transaction commit to be fully complete */
static noinline int wait_for_commit(struct btrfs_root *root,
				    struct btrfs_transaction *commit)
{
	DEFINE_WAIT(wait);
	mutex_lock(&root->fs_info->trans_mutex);
	while (!commit->commit_done) {
		prepare_to_wait(&commit->commit_wait, &wait,
				TASK_UNINTERRUPTIBLE);
		if (commit->commit_done)
			break;
		mutex_unlock(&root->fs_info->trans_mutex);
		schedule();
		mutex_lock(&root->fs_info->trans_mutex);
	}
	mutex_unlock(&root->fs_info->trans_mutex);
	finish_wait(&commit->commit_wait, &wait);
	return 0;
}

/*
 * rate limit against the drop_snapshot code.  This helps to slow down new
 * operations if the drop_snapshot code isn't able to keep up.
 */
static void throttle_on_drops(struct btrfs_root *root)
{
	struct btrfs_fs_info *info = root->fs_info;
	int harder_count = 0;

harder:
	if (atomic_read(&info->throttles)) {
		DEFINE_WAIT(wait);
		int thr;
		thr = atomic_read(&info->throttle_gen);

		do {
			prepare_to_wait(&info->transaction_throttle,
					&wait, TASK_UNINTERRUPTIBLE);
			if (!atomic_read(&info->throttles)) {
				finish_wait(&info->transaction_throttle, &wait);
				break;
			}
			schedule();
			finish_wait(&info->transaction_throttle, &wait);
		} while (thr == atomic_read(&info->throttle_gen));
		harder_count++;

		if (root->fs_info->total_ref_cache_size > 1 * 1024 * 1024 &&
		    harder_count < 2)
			goto harder;

		if (root->fs_info->total_ref_cache_size > 5 * 1024 * 1024 &&
		    harder_count < 10)
			goto harder;

		if (root->fs_info->total_ref_cache_size > 10 * 1024 * 1024 &&
		    harder_count < 20)
			goto harder;
	}
}

void btrfs_throttle(struct btrfs_root *root)
{
	mutex_lock(&root->fs_info->trans_mutex);
	if (!root->fs_info->open_ioctl_trans)
		wait_current_trans(root);
	mutex_unlock(&root->fs_info->trans_mutex);

	throttle_on_drops(root);
}

static int __btrfs_end_transaction(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root, int throttle)
{
	struct btrfs_transaction *cur_trans;
	struct btrfs_fs_info *info = root->fs_info;

	mutex_lock(&info->trans_mutex);
	cur_trans = info->running_transaction;
	WARN_ON(cur_trans != trans->transaction);
	WARN_ON(cur_trans->num_writers < 1);
	cur_trans->num_writers--;

	if (waitqueue_active(&cur_trans->writer_wait))
		wake_up(&cur_trans->writer_wait);
	put_transaction(cur_trans);
	mutex_unlock(&info->trans_mutex);
	memset(trans, 0, sizeof(*trans));
	kmem_cache_free(btrfs_trans_handle_cachep, trans);

	if (throttle)
		throttle_on_drops(root);

	return 0;
}

int btrfs_end_transaction(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root)
{
	return __btrfs_end_transaction(trans, root, 0);
}

int btrfs_end_transaction_throttle(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root)
{
	return __btrfs_end_transaction(trans, root, 1);
}

/*
 * when btree blocks are allocated, they have some corresponding bits set for
 * them in one of two extent_io trees.  This is used to make sure all of
 * those extents are on disk for transaction or log commit
 */
int btrfs_write_and_wait_marked_extents(struct btrfs_root *root,
					struct extent_io_tree *dirty_pages)
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
					    EXTENT_DIRTY);
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
	while (1) {
		ret = find_first_extent_bit(dirty_pages, 0, &start, &end,
					    EXTENT_DIRTY);
		if (ret)
			break;

		clear_extent_dirty(dirty_pages, start, end, GFP_NOFS);
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

int btrfs_write_and_wait_transaction(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root)
{
	if (!trans || !trans->transaction) {
		struct inode *btree_inode;
		btree_inode = root->fs_info->btree_inode;
		return filemap_write_and_wait(btree_inode->i_mapping);
	}
	return btrfs_write_and_wait_marked_extents(root,
					   &trans->transaction->dirty_pages);
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
	struct btrfs_root *tree_root = root->fs_info->tree_root;

	btrfs_extent_post_op(trans, root);
	btrfs_write_dirty_block_groups(trans, root);
	btrfs_extent_post_op(trans, root);

	while (1) {
		old_root_bytenr = btrfs_root_bytenr(&root->root_item);
		if (old_root_bytenr == root->node->start)
			break;
		btrfs_set_root_bytenr(&root->root_item,
				       root->node->start);
		btrfs_set_root_level(&root->root_item,
				     btrfs_header_level(root->node));
		btrfs_set_root_generation(&root->root_item, trans->transid);

		btrfs_extent_post_op(trans, root);

		ret = btrfs_update_root(trans, tree_root,
					&root->root_key,
					&root->root_item);
		BUG_ON(ret);
		btrfs_write_dirty_block_groups(trans, root);
		btrfs_extent_post_op(trans, root);
	}
	return 0;
}

/*
 * update all the cowonly tree roots on disk
 */
int btrfs_commit_tree_roots(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct list_head *next;
	struct extent_buffer *eb;

	btrfs_extent_post_op(trans, fs_info->tree_root);

	eb = btrfs_lock_root_node(fs_info->tree_root);
	btrfs_cow_block(trans, fs_info->tree_root, eb, NULL, 0, &eb, 0);
	btrfs_tree_unlock(eb);
	free_extent_buffer(eb);

	btrfs_extent_post_op(trans, fs_info->tree_root);

	while (!list_empty(&fs_info->dirty_cowonly_roots)) {
		next = fs_info->dirty_cowonly_roots.next;
		list_del_init(next);
		root = list_entry(next, struct btrfs_root, dirty_list);

		update_cowonly_root(trans, root);
	}
	return 0;
}

/*
 * dead roots are old snapshots that need to be deleted.  This allocates
 * a dirty root struct and adds it into the list of dead roots that need to
 * be deleted
 */
int btrfs_add_dead_root(struct btrfs_root *root, struct btrfs_root *latest)
{
	struct btrfs_dirty_root *dirty;

	dirty = kmalloc(sizeof(*dirty), GFP_NOFS);
	if (!dirty)
		return -ENOMEM;
	dirty->root = root;
	dirty->latest_root = latest;

	mutex_lock(&root->fs_info->trans_mutex);
	list_add(&dirty->list, &latest->fs_info->dead_roots);
	mutex_unlock(&root->fs_info->trans_mutex);
	return 0;
}

/*
 * at transaction commit time we need to schedule the old roots for
 * deletion via btrfs_drop_snapshot.  This runs through all the
 * reference counted roots that were modified in the current
 * transaction and puts them into the drop list
 */
static noinline int add_dirty_roots(struct btrfs_trans_handle *trans,
				    struct radix_tree_root *radix,
				    struct list_head *list)
{
	struct btrfs_dirty_root *dirty;
	struct btrfs_root *gang[8];
	struct btrfs_root *root;
	int i;
	int ret;
	int err = 0;
	u32 refs;

	while (1) {
		ret = radix_tree_gang_lookup_tag(radix, (void **)gang, 0,
						 ARRAY_SIZE(gang),
						 BTRFS_ROOT_TRANS_TAG);
		if (ret == 0)
			break;
		for (i = 0; i < ret; i++) {
			root = gang[i];
			radix_tree_tag_clear(radix,
				     (unsigned long)root->root_key.objectid,
				     BTRFS_ROOT_TRANS_TAG);

			BUG_ON(!root->ref_tree);
			dirty = root->dirty_root;

			btrfs_free_log(trans, root);
			btrfs_free_reloc_root(trans, root);

			if (root->commit_root == root->node) {
				WARN_ON(root->node->start !=
					btrfs_root_bytenr(&root->root_item));

				free_extent_buffer(root->commit_root);
				root->commit_root = NULL;
				root->dirty_root = NULL;

				spin_lock(&root->list_lock);
				list_del_init(&dirty->root->dead_list);
				spin_unlock(&root->list_lock);

				kfree(dirty->root);
				kfree(dirty);

				/* make sure to update the root on disk
				 * so we get any updates to the block used
				 * counts
				 */
				err = btrfs_update_root(trans,
						root->fs_info->tree_root,
						&root->root_key,
						&root->root_item);
				continue;
			}

			memset(&root->root_item.drop_progress, 0,
			       sizeof(struct btrfs_disk_key));
			root->root_item.drop_level = 0;
			root->commit_root = NULL;
			root->dirty_root = NULL;
			root->root_key.offset = root->fs_info->generation;
			btrfs_set_root_bytenr(&root->root_item,
					      root->node->start);
			btrfs_set_root_level(&root->root_item,
					     btrfs_header_level(root->node));
			btrfs_set_root_generation(&root->root_item,
						  root->root_key.offset);

			err = btrfs_insert_root(trans, root->fs_info->tree_root,
						&root->root_key,
						&root->root_item);
			if (err)
				break;

			refs = btrfs_root_refs(&dirty->root->root_item);
			btrfs_set_root_refs(&dirty->root->root_item, refs - 1);
			err = btrfs_update_root(trans, root->fs_info->tree_root,
						&dirty->root->root_key,
						&dirty->root->root_item);

			BUG_ON(err);
			if (refs == 1) {
				list_add(&dirty->list, list);
			} else {
				WARN_ON(1);
				free_extent_buffer(dirty->root->node);
				kfree(dirty->root);
				kfree(dirty);
			}
		}
	}
	return err;
}

/*
 * defrag a given btree.  If cacheonly == 1, this won't read from the disk,
 * otherwise every leaf in the btree is read and defragged.
 */
int btrfs_defrag_root(struct btrfs_root *root, int cacheonly)
{
	struct btrfs_fs_info *info = root->fs_info;
	int ret;
	struct btrfs_trans_handle *trans;
	unsigned long nr;

	smp_mb();
	if (root->defrag_running)
		return 0;
	trans = btrfs_start_transaction(root, 1);
	while (1) {
		root->defrag_running = 1;
		ret = btrfs_defrag_leaves(trans, root, cacheonly);
		nr = trans->blocks_used;
		btrfs_end_transaction(trans, root);
		btrfs_btree_balance_dirty(info->tree_root, nr);
		cond_resched();

		trans = btrfs_start_transaction(root, 1);
		if (root->fs_info->closing || ret != -EAGAIN)
			break;
	}
	root->defrag_running = 0;
	smp_mb();
	btrfs_end_transaction(trans, root);
	return 0;
}

/*
 * Given a list of roots that need to be deleted, call btrfs_drop_snapshot on
 * all of them
 */
static noinline int drop_dirty_roots(struct btrfs_root *tree_root,
				     struct list_head *list)
{
	struct btrfs_dirty_root *dirty;
	struct btrfs_trans_handle *trans;
	unsigned long nr;
	u64 num_bytes;
	u64 bytes_used;
	u64 max_useless;
	int ret = 0;
	int err;

	while (!list_empty(list)) {
		struct btrfs_root *root;

		dirty = list_entry(list->prev, struct btrfs_dirty_root, list);
		list_del_init(&dirty->list);

		num_bytes = btrfs_root_used(&dirty->root->root_item);
		root = dirty->latest_root;
		atomic_inc(&root->fs_info->throttles);

		while (1) {
			trans = btrfs_start_transaction(tree_root, 1);
			mutex_lock(&root->fs_info->drop_mutex);
			ret = btrfs_drop_snapshot(trans, dirty->root);
			if (ret != -EAGAIN)
				break;
			mutex_unlock(&root->fs_info->drop_mutex);

			err = btrfs_update_root(trans,
					tree_root,
					&dirty->root->root_key,
					&dirty->root->root_item);
			if (err)
				ret = err;
			nr = trans->blocks_used;
			ret = btrfs_end_transaction(trans, tree_root);
			BUG_ON(ret);

			btrfs_btree_balance_dirty(tree_root, nr);
			cond_resched();
		}
		BUG_ON(ret);
		atomic_dec(&root->fs_info->throttles);
		wake_up(&root->fs_info->transaction_throttle);

		num_bytes -= btrfs_root_used(&dirty->root->root_item);
		bytes_used = btrfs_root_used(&root->root_item);
		if (num_bytes) {
			mutex_lock(&root->fs_info->trans_mutex);
			btrfs_record_root_in_trans(root);
			mutex_unlock(&root->fs_info->trans_mutex);
			btrfs_set_root_used(&root->root_item,
					    bytes_used - num_bytes);
		}

		ret = btrfs_del_root(trans, tree_root, &dirty->root->root_key);
		if (ret) {
			BUG();
			break;
		}
		mutex_unlock(&root->fs_info->drop_mutex);

		spin_lock(&root->list_lock);
		list_del_init(&dirty->root->dead_list);
		if (!list_empty(&root->dead_list)) {
			struct btrfs_root *oldest;
			oldest = list_entry(root->dead_list.prev,
					    struct btrfs_root, dead_list);
			max_useless = oldest->root_key.offset - 1;
		} else {
			max_useless = root->root_key.offset - 1;
		}
		spin_unlock(&root->list_lock);

		nr = trans->blocks_used;
		ret = btrfs_end_transaction(trans, tree_root);
		BUG_ON(ret);

		ret = btrfs_remove_leaf_refs(root, max_useless, 0);
		BUG_ON(ret);

		free_extent_buffer(dirty->root->node);
		kfree(dirty->root);
		kfree(dirty);

		btrfs_btree_balance_dirty(tree_root, nr);
		cond_resched();
	}
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
	struct extent_buffer *tmp;
	struct extent_buffer *old;
	int ret;
	u64 objectid;

	new_root_item = kmalloc(sizeof(*new_root_item), GFP_NOFS);
	if (!new_root_item) {
		ret = -ENOMEM;
		goto fail;
	}
	ret = btrfs_find_free_objectid(trans, tree_root, 0, &objectid);
	if (ret)
		goto fail;

	btrfs_record_root_in_trans(root);
	btrfs_set_root_last_snapshot(&root->root_item, trans->transid);
	memcpy(new_root_item, &root->root_item, sizeof(*new_root_item));

	key.objectid = objectid;
	key.offset = trans->transid;
	btrfs_set_key_type(&key, BTRFS_ROOT_ITEM_KEY);

	old = btrfs_lock_root_node(root);
	btrfs_cow_block(trans, root, old, NULL, 0, &old, 0);

	btrfs_copy_root(trans, root, old, &tmp, objectid);
	btrfs_tree_unlock(old);
	free_extent_buffer(old);

	btrfs_set_root_bytenr(new_root_item, tmp->start);
	btrfs_set_root_level(new_root_item, btrfs_header_level(tmp));
	btrfs_set_root_generation(new_root_item, trans->transid);
	ret = btrfs_insert_root(trans, root->fs_info->tree_root, &key,
				new_root_item);
	btrfs_tree_unlock(tmp);
	free_extent_buffer(tmp);
	if (ret)
		goto fail;

	key.offset = (u64)-1;
	memcpy(&pending->root_key, &key, sizeof(key));
fail:
	kfree(new_root_item);
	return ret;
}

static noinline int finish_pending_snapshot(struct btrfs_fs_info *fs_info,
				   struct btrfs_pending_snapshot *pending)
{
	int ret;
	int namelen;
	u64 index = 0;
	struct btrfs_trans_handle *trans;
	struct inode *parent_inode;
	struct inode *inode;
	struct btrfs_root *parent_root;

	parent_inode = pending->dentry->d_parent->d_inode;
	parent_root = BTRFS_I(parent_inode)->root;
	trans = btrfs_join_transaction(parent_root, 1);

	/*
	 * insert the directory item
	 */
	namelen = strlen(pending->name);
	ret = btrfs_set_inode_index(parent_inode, &index);
	ret = btrfs_insert_dir_item(trans, parent_root,
			    pending->name, namelen,
			    parent_inode->i_ino,
			    &pending->root_key, BTRFS_FT_DIR, index);

	if (ret)
		goto fail;

	btrfs_i_size_write(parent_inode, parent_inode->i_size + namelen * 2);
	ret = btrfs_update_inode(trans, parent_root, parent_inode);
	BUG_ON(ret);

	/* add the backref first */
	ret = btrfs_add_root_ref(trans, parent_root->fs_info->tree_root,
				 pending->root_key.objectid,
				 BTRFS_ROOT_BACKREF_KEY,
				 parent_root->root_key.objectid,
				 parent_inode->i_ino, index, pending->name,
				 namelen);

	BUG_ON(ret);

	/* now add the forward ref */
	ret = btrfs_add_root_ref(trans, parent_root->fs_info->tree_root,
				 parent_root->root_key.objectid,
				 BTRFS_ROOT_REF_KEY,
				 pending->root_key.objectid,
				 parent_inode->i_ino, index, pending->name,
				 namelen);

	inode = btrfs_lookup_dentry(parent_inode, pending->dentry);
	d_instantiate(pending->dentry, inode);
fail:
	btrfs_end_transaction(trans, fs_info->fs_root);
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
	int ret;

	list_for_each_entry(pending, head, list) {
		ret = create_pending_snapshot(trans, fs_info, pending);
		BUG_ON(ret);
	}
	return 0;
}

static noinline int finish_pending_snapshots(struct btrfs_trans_handle *trans,
					     struct btrfs_fs_info *fs_info)
{
	struct btrfs_pending_snapshot *pending;
	struct list_head *head = &trans->transaction->pending_snapshots;
	int ret;

	while (!list_empty(head)) {
		pending = list_entry(head->next,
				     struct btrfs_pending_snapshot, list);
		ret = finish_pending_snapshot(fs_info, pending);
		BUG_ON(ret);
		list_del(&pending->list);
		kfree(pending->name);
		kfree(pending);
	}
	return 0;
}

int btrfs_commit_transaction(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root)
{
	unsigned long joined = 0;
	unsigned long timeout = 1;
	struct btrfs_transaction *cur_trans;
	struct btrfs_transaction *prev_trans = NULL;
	struct btrfs_root *chunk_root = root->fs_info->chunk_root;
	struct list_head dirty_fs_roots;
	struct extent_io_tree *pinned_copy;
	DEFINE_WAIT(wait);
	int ret;

	INIT_LIST_HEAD(&dirty_fs_roots);
	mutex_lock(&root->fs_info->trans_mutex);
	if (trans->transaction->in_commit) {
		cur_trans = trans->transaction;
		trans->transaction->use_count++;
		mutex_unlock(&root->fs_info->trans_mutex);
		btrfs_end_transaction(trans, root);

		ret = wait_for_commit(root, cur_trans);
		BUG_ON(ret);

		mutex_lock(&root->fs_info->trans_mutex);
		put_transaction(cur_trans);
		mutex_unlock(&root->fs_info->trans_mutex);

		return 0;
	}

	pinned_copy = kmalloc(sizeof(*pinned_copy), GFP_NOFS);
	if (!pinned_copy)
		return -ENOMEM;

	extent_io_tree_init(pinned_copy,
			     root->fs_info->btree_inode->i_mapping, GFP_NOFS);

	trans->transaction->in_commit = 1;
	trans->transaction->blocked = 1;
	cur_trans = trans->transaction;
	if (cur_trans->list.prev != &root->fs_info->trans_list) {
		prev_trans = list_entry(cur_trans->list.prev,
					struct btrfs_transaction, list);
		if (!prev_trans->commit_done) {
			prev_trans->use_count++;
			mutex_unlock(&root->fs_info->trans_mutex);

			wait_for_commit(root, prev_trans);

			mutex_lock(&root->fs_info->trans_mutex);
			put_transaction(prev_trans);
		}
	}

	do {
		int snap_pending = 0;
		joined = cur_trans->num_joined;
		if (!list_empty(&trans->transaction->pending_snapshots))
			snap_pending = 1;

		WARN_ON(cur_trans != trans->transaction);
		prepare_to_wait(&cur_trans->writer_wait, &wait,
				TASK_UNINTERRUPTIBLE);

		if (cur_trans->num_writers > 1)
			timeout = MAX_SCHEDULE_TIMEOUT;
		else
			timeout = 1;

		mutex_unlock(&root->fs_info->trans_mutex);

		if (snap_pending) {
			ret = btrfs_wait_ordered_extents(root, 1);
			BUG_ON(ret);
		}

		schedule_timeout(timeout);

		mutex_lock(&root->fs_info->trans_mutex);
		finish_wait(&cur_trans->writer_wait, &wait);
	} while (cur_trans->num_writers > 1 ||
		 (cur_trans->num_joined != joined));

	ret = create_pending_snapshots(trans, root->fs_info);
	BUG_ON(ret);

	WARN_ON(cur_trans != trans->transaction);

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
	/*
	 * keep tree reloc code from adding new reloc trees
	 */
	mutex_lock(&root->fs_info->tree_reloc_mutex);


	ret = add_dirty_roots(trans, &root->fs_info->fs_roots_radix,
			      &dirty_fs_roots);
	BUG_ON(ret);

	/* add_dirty_roots gets rid of all the tree log roots, it is now
	 * safe to free the root of tree log roots
	 */
	btrfs_free_log_root_tree(trans, root->fs_info);

	ret = btrfs_commit_tree_roots(trans, root);
	BUG_ON(ret);

	cur_trans = root->fs_info->running_transaction;
	spin_lock(&root->fs_info->new_trans_lock);
	root->fs_info->running_transaction = NULL;
	spin_unlock(&root->fs_info->new_trans_lock);
	btrfs_set_super_generation(&root->fs_info->super_copy,
				   cur_trans->transid);
	btrfs_set_super_root(&root->fs_info->super_copy,
			     root->fs_info->tree_root->node->start);
	btrfs_set_super_root_level(&root->fs_info->super_copy,
			   btrfs_header_level(root->fs_info->tree_root->node));

	btrfs_set_super_chunk_root(&root->fs_info->super_copy,
				   chunk_root->node->start);
	btrfs_set_super_chunk_root_level(&root->fs_info->super_copy,
					 btrfs_header_level(chunk_root->node));
	btrfs_set_super_chunk_root_generation(&root->fs_info->super_copy,
				btrfs_header_generation(chunk_root->node));

	if (!root->fs_info->log_root_recovering) {
		btrfs_set_super_log_root(&root->fs_info->super_copy, 0);
		btrfs_set_super_log_root_level(&root->fs_info->super_copy, 0);
	}

	memcpy(&root->fs_info->super_for_commit, &root->fs_info->super_copy,
	       sizeof(root->fs_info->super_copy));

	btrfs_copy_pinned(root, pinned_copy);

	trans->transaction->blocked = 0;
	wake_up(&root->fs_info->transaction_throttle);
	wake_up(&root->fs_info->transaction_wait);

	mutex_unlock(&root->fs_info->trans_mutex);
	ret = btrfs_write_and_wait_transaction(trans, root);
	BUG_ON(ret);
	write_ctree_super(trans, root, 0);

	/*
	 * the super is written, we can safely allow the tree-loggers
	 * to go about their business
	 */
	mutex_unlock(&root->fs_info->tree_log_mutex);

	btrfs_finish_extent_commit(trans, root, pinned_copy);
	kfree(pinned_copy);

	btrfs_drop_dead_reloc_roots(root);
	mutex_unlock(&root->fs_info->tree_reloc_mutex);

	/* do the directory inserts of any pending snapshot creations */
	finish_pending_snapshots(trans, root->fs_info);

	mutex_lock(&root->fs_info->trans_mutex);

	cur_trans->commit_done = 1;
	root->fs_info->last_trans_committed = cur_trans->transid;
	wake_up(&cur_trans->commit_wait);

	put_transaction(cur_trans);
	put_transaction(cur_trans);

	list_splice_init(&dirty_fs_roots, &root->fs_info->dead_roots);
	if (root->fs_info->closing)
		list_splice_init(&root->fs_info->dead_roots, &dirty_fs_roots);

	mutex_unlock(&root->fs_info->trans_mutex);

	kmem_cache_free(btrfs_trans_handle_cachep, trans);

	if (root->fs_info->closing)
		drop_dirty_roots(root->fs_info->tree_root, &dirty_fs_roots);
	return ret;
}

/*
 * interface function to delete all the snapshots we have scheduled for deletion
 */
int btrfs_clean_old_snapshots(struct btrfs_root *root)
{
	struct list_head dirty_roots;
	INIT_LIST_HEAD(&dirty_roots);
again:
	mutex_lock(&root->fs_info->trans_mutex);
	list_splice_init(&root->fs_info->dead_roots, &dirty_roots);
	mutex_unlock(&root->fs_info->trans_mutex);

	if (!list_empty(&dirty_roots)) {
		drop_dirty_roots(root, &dirty_roots);
		goto again;
	}
	return 0;
}
