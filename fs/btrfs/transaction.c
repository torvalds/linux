#include <linux/module.h>
#include <linux/fs.h>
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"

static int total_trans = 0;
static void put_transaction(struct btrfs_transaction *transaction)
{
	transaction->use_count--;
	if (transaction->use_count == 0) {
		WARN_ON(total_trans == 0);
		total_trans--;
		kfree(transaction);
	}
}

static int join_transaction(struct btrfs_root *root)
{
	struct btrfs_transaction *cur_trans;
	cur_trans = root->fs_info->running_transaction;
	if (!cur_trans) {
		cur_trans = kmalloc(sizeof(*cur_trans), GFP_NOFS);
		total_trans++;
		BUG_ON(!cur_trans);
		root->fs_info->running_transaction = cur_trans;
		cur_trans->num_writers = 0;
		cur_trans->transid = root->root_key.offset + 1;
		init_waitqueue_head(&cur_trans->writer_wait);
		init_waitqueue_head(&cur_trans->commit_wait);
		cur_trans->in_commit = 0;
		cur_trans->use_count = 1;
		cur_trans->commit_done = 0;
	}
	cur_trans->num_writers++;
	return 0;
}

struct btrfs_trans_handle *btrfs_start_transaction(struct btrfs_root *root,
						   int num_blocks)
{
	struct btrfs_trans_handle *h = kmalloc(sizeof(*h), GFP_NOFS);
	int ret;

	mutex_lock(&root->fs_info->trans_mutex);
	ret = join_transaction(root);
	BUG_ON(ret);
	h->transid = root->fs_info->running_transaction->transid;
	h->transaction = root->fs_info->running_transaction;
	h->blocks_reserved = num_blocks;
	h->blocks_used = 0;
	root->fs_info->running_transaction->use_count++;
	mutex_unlock(&root->fs_info->trans_mutex);
	return h;
}

int btrfs_end_transaction(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root)
{
	struct btrfs_transaction *cur_trans;
	mutex_lock(&root->fs_info->trans_mutex);
	cur_trans = root->fs_info->running_transaction;
	WARN_ON(cur_trans->num_writers < 1);
	if (waitqueue_active(&cur_trans->writer_wait))
		wake_up(&cur_trans->writer_wait);
	cur_trans->num_writers--;
	put_transaction(cur_trans);
	mutex_unlock(&root->fs_info->trans_mutex);
	kfree(trans);
	return 0;
}


int btrfs_write_and_wait_transaction(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root)
{
	filemap_write_and_wait(root->fs_info->sb->s_bdev->bd_inode->i_mapping);
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
	struct btrfs_root *inode_root = fs_info->inode_root;

	btrfs_set_root_blocknr(&inode_root->root_item,
			       inode_root->node->b_blocknr);
	ret = btrfs_update_root(trans, tree_root,
				&inode_root->root_key,
				&inode_root->root_item);
	BUG_ON(ret);
	while(1) {
		old_extent_block = btrfs_root_blocknr(&extent_root->root_item);
		if (old_extent_block == extent_root->node->b_blocknr)
			break;
		btrfs_set_root_blocknr(&extent_root->root_item,
				       extent_root->node->b_blocknr);
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

int btrfs_commit_transaction(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root)
{
	int ret = 0;
	struct buffer_head *snap;
	struct btrfs_key snap_key;
	struct btrfs_transaction *cur_trans;
	DEFINE_WAIT(wait);

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
	while (trans->transaction->num_writers > 1) {
		prepare_to_wait(&trans->transaction->writer_wait, &wait,
				TASK_UNINTERRUPTIBLE);
		if (trans->transaction->num_writers <= 1)
			break;
		mutex_unlock(&root->fs_info->trans_mutex);
		schedule();
		mutex_lock(&root->fs_info->trans_mutex);
	}
	finish_wait(&trans->transaction->writer_wait, &wait);

	if (root->node != root->commit_root) {
		memcpy(&snap_key, &root->root_key, sizeof(snap_key));
		root->root_key.offset++;
	}


	if (btrfs_root_blocknr(&root->root_item) != root->node->b_blocknr) {
		btrfs_set_root_blocknr(&root->root_item, root->node->b_blocknr);
		ret = btrfs_insert_root(trans, root->fs_info->tree_root,
					&root->root_key, &root->root_item);
		BUG_ON(ret);
	}

	ret = btrfs_commit_tree_roots(trans, root);
	BUG_ON(ret);

	cur_trans = root->fs_info->running_transaction;
	root->fs_info->running_transaction = NULL;
	mutex_unlock(&root->fs_info->trans_mutex);

	ret = btrfs_write_and_wait_transaction(trans, root);
	BUG_ON(ret);

	write_ctree_super(trans, root);
	btrfs_finish_extent_commit(trans, root);
	mutex_lock(&root->fs_info->trans_mutex);
	put_transaction(cur_trans);
	put_transaction(cur_trans);
	mutex_unlock(&root->fs_info->trans_mutex);
	kfree(trans);

	if (root->node != root->commit_root) {
		trans = btrfs_start_transaction(root, 1);
		snap = root->commit_root;
		root->commit_root = root->node;
		get_bh(root->node);
		ret = btrfs_drop_snapshot(trans, root, snap);
		BUG_ON(ret);

		ret = btrfs_del_root(trans, root->fs_info->tree_root,
				     &snap_key);
		BUG_ON(ret); root->fs_info->generation = root->root_key.offset + 1; ret = btrfs_end_transaction(trans, root); BUG_ON(ret);
		printk("at free, total trans %d\n", total_trans);
	}

	return ret;
}

