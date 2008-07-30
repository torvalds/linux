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
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "locking.h"
#include "ref-cache.h"

static int total_trans = 0;
extern struct kmem_cache *btrfs_trans_handle_cachep;
extern struct kmem_cache *btrfs_transaction_cachep;

#define BTRFS_ROOT_TRANS_TAG 0

static noinline void put_transaction(struct btrfs_transaction *transaction)
{
	WARN_ON(transaction->use_count == 0);
	transaction->use_count--;
	if (transaction->use_count == 0) {
		WARN_ON(total_trans == 0);
		total_trans--;
		list_del_init(&transaction->list);
		memset(transaction, 0, sizeof(*transaction));
		kmem_cache_free(btrfs_transaction_cachep, transaction);
	}
}

static noinline int join_transaction(struct btrfs_root *root)
{
	struct btrfs_transaction *cur_trans;
	cur_trans = root->fs_info->running_transaction;
	if (!cur_trans) {
		cur_trans = kmem_cache_alloc(btrfs_transaction_cachep,
					     GFP_NOFS);
		total_trans++;
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

static noinline int record_root_in_trans(struct btrfs_root *root)
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

struct btrfs_trans_handle *start_transaction(struct btrfs_root *root,
					     int num_blocks, int join)
{
	struct btrfs_trans_handle *h =
		kmem_cache_alloc(btrfs_trans_handle_cachep, GFP_NOFS);
	struct btrfs_transaction *cur_trans;
	int ret;

	mutex_lock(&root->fs_info->trans_mutex);
	cur_trans = root->fs_info->running_transaction;
	if (cur_trans && cur_trans->blocked && !join) {
		DEFINE_WAIT(wait);
		cur_trans->use_count++;
		while(1) {
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
	ret = join_transaction(root);
	BUG_ON(ret);

	record_root_in_trans(root);
	h->transid = root->fs_info->running_transaction->transid;
	h->transaction = root->fs_info->running_transaction;
	h->blocks_reserved = num_blocks;
	h->blocks_used = 0;
	h->block_group = NULL;
	h->alloc_exclude_nr = 0;
	h->alloc_exclude_start = 0;
	root->fs_info->running_transaction->use_count++;
	mutex_unlock(&root->fs_info->trans_mutex);
	return h;
}

struct btrfs_trans_handle *btrfs_start_transaction(struct btrfs_root *root,
						   int num_blocks)
{
	return start_transaction(root, num_blocks, 0);
}
struct btrfs_trans_handle *btrfs_join_transaction(struct btrfs_root *root,
						   int num_blocks)
{
	return start_transaction(root, num_blocks, 1);
}

static noinline int wait_for_commit(struct btrfs_root *root,
				    struct btrfs_transaction *commit)
{
	DEFINE_WAIT(wait);
	mutex_lock(&root->fs_info->trans_mutex);
	while(!commit->commit_done) {
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

void btrfs_throttle(struct btrfs_root *root)
{
	struct btrfs_fs_info *info = root->fs_info;

harder:
	if (atomic_read(&info->throttles)) {
		DEFINE_WAIT(wait);
		int thr;
		int harder_count = 0;
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

		if (harder_count < 5 &&
		    info->total_ref_cache_size > 5 * 1024 * 1024) {
			harder_count++;
			goto harder;
		}

		if (harder_count < 10 &&
		    info->total_ref_cache_size > 10 * 1024 * 1024) {
			harder_count++;
			goto harder;
		}
	}
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
		btrfs_throttle(root);

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


int btrfs_write_and_wait_transaction(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root)
{
	int ret;
	int err;
	int werr = 0;
	struct extent_io_tree *dirty_pages;
	struct page *page;
	struct inode *btree_inode = root->fs_info->btree_inode;
	u64 start;
	u64 end;
	unsigned long index;

	if (!trans || !trans->transaction) {
		return filemap_write_and_wait(btree_inode->i_mapping);
	}
	dirty_pages = &trans->transaction->dirty_pages;
	while(1) {
		ret = find_first_extent_bit(dirty_pages, 0, &start, &end,
					    EXTENT_DIRTY);
		if (ret)
			break;
		clear_extent_dirty(dirty_pages, start, end, GFP_NOFS);
		while(start <= end) {
			index = start >> PAGE_CACHE_SHIFT;
			start = (u64)(index + 1) << PAGE_CACHE_SHIFT;
			page = find_lock_page(btree_inode->i_mapping, index);
			if (!page)
				continue;
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
	err = filemap_fdatawait(btree_inode->i_mapping);
	if (err)
		werr = err;
	return werr;
}

static int update_cowonly_root(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root)
{
	int ret;
	u64 old_root_bytenr;
	struct btrfs_root *tree_root = root->fs_info->tree_root;

	btrfs_write_dirty_block_groups(trans, root);
	while(1) {
		old_root_bytenr = btrfs_root_bytenr(&root->root_item);
		if (old_root_bytenr == root->node->start)
			break;
		btrfs_set_root_bytenr(&root->root_item,
				       root->node->start);
		btrfs_set_root_level(&root->root_item,
				     btrfs_header_level(root->node));
		ret = btrfs_update_root(trans, tree_root,
					&root->root_key,
					&root->root_item);
		BUG_ON(ret);
		btrfs_write_dirty_block_groups(trans, root);
	}
	return 0;
}

int btrfs_commit_tree_roots(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct list_head *next;

	while(!list_empty(&fs_info->dirty_cowonly_roots)) {
		next = fs_info->dirty_cowonly_roots.next;
		list_del_init(next);
		root = list_entry(next, struct btrfs_root, dirty_list);
		update_cowonly_root(trans, root);
	}
	return 0;
}

int btrfs_add_dead_root(struct btrfs_root *root,
			struct btrfs_root *latest,
			struct list_head *dead_list)
{
	struct btrfs_dirty_root *dirty;

	dirty = kmalloc(sizeof(*dirty), GFP_NOFS);
	if (!dirty)
		return -ENOMEM;
	dirty->root = root;
	dirty->latest_root = latest;
	list_add(&dirty->list, dead_list);
	return 0;
}

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

	while(1) {
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

			if (root->commit_root == root->node) {
				WARN_ON(root->node->start !=
					btrfs_root_bytenr(&root->root_item));

				free_extent_buffer(root->commit_root);
				root->commit_root = NULL;

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
			root->root_key.offset = root->fs_info->generation;
			btrfs_set_root_bytenr(&root->root_item,
					      root->node->start);
			btrfs_set_root_level(&root->root_item,
					     btrfs_header_level(root->node));
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

	while(!list_empty(list)) {
		struct btrfs_root *root;

		dirty = list_entry(list->prev, struct btrfs_dirty_root, list);
		list_del_init(&dirty->list);

		num_bytes = btrfs_root_used(&dirty->root->root_item);
		root = dirty->latest_root;
		atomic_inc(&root->fs_info->throttles);

		mutex_lock(&root->fs_info->drop_mutex);
		while(1) {
			trans = btrfs_start_transaction(tree_root, 1);
			ret = btrfs_drop_snapshot(trans, dirty->root);
			if (ret != -EAGAIN) {
				break;
			}

			err = btrfs_update_root(trans,
					tree_root,
					&dirty->root->root_key,
					&dirty->root->root_item);
			if (err)
				ret = err;
			nr = trans->blocks_used;
			ret = btrfs_end_transaction(trans, tree_root);
			BUG_ON(ret);

			mutex_unlock(&root->fs_info->drop_mutex);
			btrfs_btree_balance_dirty(tree_root, nr);
			cond_resched();
			mutex_lock(&root->fs_info->drop_mutex);
		}
		BUG_ON(ret);
		atomic_dec(&root->fs_info->throttles);
		wake_up(&root->fs_info->transaction_throttle);

		mutex_lock(&root->fs_info->alloc_mutex);
		num_bytes -= btrfs_root_used(&dirty->root->root_item);
		bytes_used = btrfs_root_used(&root->root_item);
		if (num_bytes) {
			record_root_in_trans(root);
			btrfs_set_root_used(&root->root_item,
					    bytes_used - num_bytes);
		}
		mutex_unlock(&root->fs_info->alloc_mutex);

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

		ret = btrfs_remove_leaf_refs(root, max_useless);
		BUG_ON(ret);

		free_extent_buffer(dirty->root->node);
		kfree(dirty->root);
		kfree(dirty);

		btrfs_btree_balance_dirty(tree_root, nr);
		cond_resched();
	}
	return ret;
}

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
	int namelen;
	u64 objectid;

	new_root_item = kmalloc(sizeof(*new_root_item), GFP_NOFS);
	if (!new_root_item) {
		ret = -ENOMEM;
		goto fail;
	}
	ret = btrfs_find_free_objectid(trans, tree_root, 0, &objectid);
	if (ret)
		goto fail;

	memcpy(new_root_item, &root->root_item, sizeof(*new_root_item));

	key.objectid = objectid;
	key.offset = 1;
	btrfs_set_key_type(&key, BTRFS_ROOT_ITEM_KEY);

	old = btrfs_lock_root_node(root);
	btrfs_cow_block(trans, root, old, NULL, 0, &old);

	btrfs_copy_root(trans, root, old, &tmp, objectid);
	btrfs_tree_unlock(old);
	free_extent_buffer(old);

	btrfs_set_root_bytenr(new_root_item, tmp->start);
	btrfs_set_root_level(new_root_item, btrfs_header_level(tmp));
	ret = btrfs_insert_root(trans, root->fs_info->tree_root, &key,
				new_root_item);
	btrfs_tree_unlock(tmp);
	free_extent_buffer(tmp);
	if (ret)
		goto fail;

	/*
	 * insert the directory item
	 */
	key.offset = (u64)-1;
	namelen = strlen(pending->name);
	ret = btrfs_insert_dir_item(trans, root->fs_info->tree_root,
				    pending->name, namelen,
				    root->fs_info->sb->s_root->d_inode->i_ino,
				    &key, BTRFS_FT_DIR, 0);

	if (ret)
		goto fail;

	ret = btrfs_insert_inode_ref(trans, root->fs_info->tree_root,
			     pending->name, strlen(pending->name), objectid,
			     root->fs_info->sb->s_root->d_inode->i_ino, 0);

	/* Invalidate existing dcache entry for new snapshot. */
	btrfs_invalidate_dcache_root(root, pending->name, namelen);

fail:
	kfree(new_root_item);
	return ret;
}

static noinline int create_pending_snapshots(struct btrfs_trans_handle *trans,
					     struct btrfs_fs_info *fs_info)
{
	struct btrfs_pending_snapshot *pending;
	struct list_head *head = &trans->transaction->pending_snapshots;
	int ret;

	while(!list_empty(head)) {
		pending = list_entry(head->next,
				     struct btrfs_pending_snapshot, list);
		ret = create_pending_snapshot(trans, fs_info, pending);
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
		joined = cur_trans->num_joined;
		WARN_ON(cur_trans != trans->transaction);
		prepare_to_wait(&cur_trans->writer_wait, &wait,
				TASK_UNINTERRUPTIBLE);

		if (cur_trans->num_writers > 1)
			timeout = MAX_SCHEDULE_TIMEOUT;
		else
			timeout = 1;

		mutex_unlock(&root->fs_info->trans_mutex);

		schedule_timeout(timeout);

		mutex_lock(&root->fs_info->trans_mutex);
		finish_wait(&cur_trans->writer_wait, &wait);
	} while (cur_trans->num_writers > 1 ||
		 (cur_trans->num_joined != joined));

	ret = create_pending_snapshots(trans, root->fs_info);
	BUG_ON(ret);

	WARN_ON(cur_trans != trans->transaction);

	ret = add_dirty_roots(trans, &root->fs_info->fs_roots_radix,
			      &dirty_fs_roots);
	BUG_ON(ret);

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
	memcpy(&root->fs_info->super_for_commit, &root->fs_info->super_copy,
	       sizeof(root->fs_info->super_copy));

	btrfs_copy_pinned(root, pinned_copy);

	trans->transaction->blocked = 0;
	wake_up(&root->fs_info->transaction_throttle);
	wake_up(&root->fs_info->transaction_wait);

	mutex_unlock(&root->fs_info->trans_mutex);
	ret = btrfs_write_and_wait_transaction(trans, root);
	BUG_ON(ret);
	write_ctree_super(trans, root);

	btrfs_finish_extent_commit(trans, root, pinned_copy);
	mutex_lock(&root->fs_info->trans_mutex);

	kfree(pinned_copy);

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

	if (root->fs_info->closing) {
		drop_dirty_roots(root->fs_info->tree_root, &dirty_fs_roots);
	}
	return ret;
}

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
