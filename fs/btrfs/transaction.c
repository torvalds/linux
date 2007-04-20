#include <linux/module.h>
#include <linux/fs.h>
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"

static int total_trans = 0;
extern struct kmem_cache *btrfs_trans_handle_cachep;
extern struct kmem_cache *btrfs_transaction_cachep;

#define BTRFS_ROOT_TRANS_TAG 0

#define TRANS_MAGIC 0xE1E10E
static void put_transaction(struct btrfs_transaction *transaction)
{
	WARN_ON(transaction->use_count == 0);
	transaction->use_count--;
	WARN_ON(transaction->magic != TRANS_MAGIC);
	if (transaction->use_count == 0) {
		WARN_ON(total_trans == 0);
		total_trans--;
		list_del_init(&transaction->list);
		memset(transaction, 0, sizeof(*transaction));
		kmem_cache_free(btrfs_transaction_cachep, transaction);
	}
}

static int join_transaction(struct btrfs_root *root)
{
	struct btrfs_transaction *cur_trans;
	cur_trans = root->fs_info->running_transaction;
	if (!cur_trans) {
		cur_trans = kmem_cache_alloc(btrfs_transaction_cachep,
					     GFP_NOFS);
		total_trans++;
		BUG_ON(!cur_trans);
		root->fs_info->generation++;
		root->fs_info->running_transaction = cur_trans;
		cur_trans->num_writers = 0;
		cur_trans->transid = root->fs_info->generation;
		init_waitqueue_head(&cur_trans->writer_wait);
		init_waitqueue_head(&cur_trans->commit_wait);
		cur_trans->magic = TRANS_MAGIC;
		cur_trans->in_commit = 0;
		cur_trans->use_count = 1;
		cur_trans->commit_done = 0;
		list_add_tail(&cur_trans->list, &root->fs_info->trans_list);
	}
	cur_trans->num_writers++;
	return 0;
}

struct btrfs_trans_handle *btrfs_start_transaction(struct btrfs_root *root,
						   int num_blocks)
{
	struct btrfs_trans_handle *h =
		kmem_cache_alloc(btrfs_trans_handle_cachep, GFP_NOFS);
	int ret;
	u64 running_trans_id;

	mutex_lock(&root->fs_info->trans_mutex);
	ret = join_transaction(root);
	BUG_ON(ret);
	running_trans_id = root->fs_info->running_transaction->transid;

	if (root != root->fs_info->tree_root && root->last_trans <
	    running_trans_id) {
		radix_tree_tag_set(&root->fs_info->fs_roots_radix,
				   (unsigned long)root->root_key.objectid,
				   BTRFS_ROOT_TRANS_TAG);
		root->commit_root = root->node;
		get_bh(root->node);
	}
	root->last_trans = running_trans_id;
	h->transid = running_trans_id;
	h->transaction = root->fs_info->running_transaction;
	h->blocks_reserved = num_blocks;
	h->blocks_used = 0;
	root->fs_info->running_transaction->use_count++;
	mutex_unlock(&root->fs_info->trans_mutex);
	h->magic = h->magic2 = TRANS_MAGIC;
	return h;
}

int btrfs_end_transaction(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root)
{
	struct btrfs_transaction *cur_trans;

	WARN_ON(trans->magic != TRANS_MAGIC);
	WARN_ON(trans->magic2 != TRANS_MAGIC);
	mutex_lock(&root->fs_info->trans_mutex);
	cur_trans = root->fs_info->running_transaction;
	WARN_ON(cur_trans->num_writers < 1);
	if (waitqueue_active(&cur_trans->writer_wait))
		wake_up(&cur_trans->writer_wait);
	cur_trans->num_writers--;
	put_transaction(cur_trans);
	mutex_unlock(&root->fs_info->trans_mutex);
	memset(trans, 0, sizeof(*trans));
	kmem_cache_free(btrfs_trans_handle_cachep, trans);
	return 0;
}


int btrfs_write_and_wait_transaction(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root)
{
	filemap_write_and_wait(root->fs_info->btree_inode->i_mapping);
	return 0;
}

int btrfs_commit_tree_roots(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root)
{
	int ret;
	u64 old_extent_block;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_root *tree_root = fs_info->tree_root;
	struct btrfs_root *extent_root = fs_info->extent_root;
	struct btrfs_root *dev_root = fs_info->dev_root;

	if (btrfs_super_device_root(fs_info->disk_super) !=
	    bh_blocknr(dev_root->node)) {
		btrfs_set_super_device_root(fs_info->disk_super,
					    bh_blocknr(dev_root->node));
	}
	while(1) {
		old_extent_block = btrfs_root_blocknr(&extent_root->root_item);
		if (old_extent_block == bh_blocknr(extent_root->node))
			break;
		btrfs_set_root_blocknr(&extent_root->root_item,
				       bh_blocknr(extent_root->node));
		ret = btrfs_update_root(trans, tree_root,
					&extent_root->root_key,
					&extent_root->root_item);
		BUG_ON(ret);
	}
	return 0;
}

static int wait_for_commit(struct btrfs_root *root,
			   struct btrfs_transaction *commit)
{
	DEFINE_WAIT(wait);
	while(!commit->commit_done) {
		prepare_to_wait(&commit->commit_wait, &wait,
				TASK_UNINTERRUPTIBLE);
		if (commit->commit_done)
			break;
		mutex_unlock(&root->fs_info->trans_mutex);
		schedule();
		mutex_lock(&root->fs_info->trans_mutex);
	}
	finish_wait(&commit->commit_wait, &wait);
	return 0;
}

struct dirty_root {
	struct list_head list;
	struct btrfs_key snap_key;
	struct buffer_head *commit_root;
	struct btrfs_root *root;
};

int add_dirty_roots(struct btrfs_trans_handle *trans,
		    struct radix_tree_root *radix, struct list_head *list)
{
	struct dirty_root *dirty;
	struct btrfs_root *gang[8];
	struct btrfs_root *root;
	int i;
	int ret;
	int err;
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
			if (root->commit_root == root->node) {
				WARN_ON(bh_blocknr(root->node) !=
					btrfs_root_blocknr(&root->root_item));
				brelse(root->commit_root);
				root->commit_root = NULL;
				continue;
			}
			dirty = kmalloc(sizeof(*dirty), GFP_NOFS);
			BUG_ON(!dirty);
			memcpy(&dirty->snap_key, &root->root_key,
			       sizeof(root->root_key));
			dirty->commit_root = root->commit_root;
			root->commit_root = NULL;
			dirty->root = root;
			root->root_key.offset = root->fs_info->generation;
			btrfs_set_root_blocknr(&root->root_item,
					       bh_blocknr(root->node));
			err = btrfs_insert_root(trans, root->fs_info->tree_root,
						&root->root_key,
						&root->root_item);
			BUG_ON(err);
			list_add(&dirty->list, list);
		}
	}
	return 0;
}

int drop_dirty_roots(struct btrfs_root *tree_root, struct list_head *list)
{
	struct dirty_root *dirty;
	struct btrfs_trans_handle *trans;
	int ret;

	while(!list_empty(list)) {
		dirty = list_entry(list->next, struct dirty_root, list);
		list_del_init(&dirty->list);
		trans = btrfs_start_transaction(tree_root, 1);
		ret = btrfs_drop_snapshot(trans, dirty->root,
					  dirty->commit_root);
		BUG_ON(ret);

		ret = btrfs_del_root(trans, tree_root, &dirty->snap_key);
		BUG_ON(ret);
		ret = btrfs_end_transaction(trans, tree_root);
		BUG_ON(ret);
		kfree(dirty);
	}
	return 0;
}

int btrfs_commit_transaction(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root)
{
	int ret = 0;
	struct btrfs_transaction *cur_trans;
	struct btrfs_transaction *prev_trans = NULL;
	struct list_head dirty_fs_roots;
	DEFINE_WAIT(wait);

	INIT_LIST_HEAD(&dirty_fs_roots);

	mutex_lock(&root->fs_info->trans_mutex);
	if (trans->transaction->in_commit) {
		cur_trans = trans->transaction;
		trans->transaction->use_count++;
		btrfs_end_transaction(trans, root);
		ret = wait_for_commit(root, cur_trans);
		BUG_ON(ret);
		put_transaction(cur_trans);
		mutex_unlock(&root->fs_info->trans_mutex);
		return 0;
	}
	cur_trans = trans->transaction;
	trans->transaction->in_commit = 1;
	while (trans->transaction->num_writers > 1) {
		WARN_ON(cur_trans != trans->transaction);
		prepare_to_wait(&trans->transaction->writer_wait, &wait,
				TASK_UNINTERRUPTIBLE);
		if (trans->transaction->num_writers <= 1)
			break;
		mutex_unlock(&root->fs_info->trans_mutex);
		schedule();
		mutex_lock(&root->fs_info->trans_mutex);
		finish_wait(&trans->transaction->writer_wait, &wait);
	}
	finish_wait(&trans->transaction->writer_wait, &wait);
	WARN_ON(cur_trans != trans->transaction);
	add_dirty_roots(trans, &root->fs_info->fs_roots_radix, &dirty_fs_roots);
	ret = btrfs_commit_tree_roots(trans, root);
	BUG_ON(ret);
	cur_trans = root->fs_info->running_transaction;
	root->fs_info->running_transaction = NULL;
	if (cur_trans->list.prev != &root->fs_info->trans_list) {
		prev_trans = list_entry(cur_trans->list.prev,
					struct btrfs_transaction, list);
		if (prev_trans->commit_done)
			prev_trans = NULL;
		else
			prev_trans->use_count++;
	}
	mutex_unlock(&root->fs_info->trans_mutex);
	mutex_unlock(&root->fs_info->fs_mutex);
	ret = btrfs_write_and_wait_transaction(trans, root);
	if (prev_trans) {
		mutex_lock(&root->fs_info->trans_mutex);
		wait_for_commit(root, prev_trans);
		put_transaction(prev_trans);
		mutex_unlock(&root->fs_info->trans_mutex);
	}
	btrfs_set_super_generation(root->fs_info->disk_super,
				   cur_trans->transid);
	BUG_ON(ret);
	write_ctree_super(trans, root);

	mutex_lock(&root->fs_info->fs_mutex);
	btrfs_finish_extent_commit(trans, root);
	mutex_lock(&root->fs_info->trans_mutex);
	cur_trans->commit_done = 1;
	wake_up(&cur_trans->commit_wait);
	put_transaction(cur_trans);
	put_transaction(cur_trans);
	mutex_unlock(&root->fs_info->trans_mutex);
	kmem_cache_free(btrfs_trans_handle_cachep, trans);

	drop_dirty_roots(root->fs_info->tree_root, &dirty_fs_roots);
	return ret;
}

