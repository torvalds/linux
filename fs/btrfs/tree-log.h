/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2008 Oracle.  All rights reserved.
 */

#ifndef BTRFS_TREE_LOG_H
#define BTRFS_TREE_LOG_H

#include "ctree.h"
#include "transaction.h"

/* return value for btrfs_log_dentry_safe that means we don't need to log it at all */
#define BTRFS_NO_LOG_SYNC 256

struct btrfs_log_ctx {
	int log_ret;
	int log_transid;
	bool log_new_dentries;
	bool logging_new_name;
	/* Indicate if the inode being logged was logged before. */
	bool logged_before;
	/* Tracks the last logged dir item/index key offset. */
	u64 last_dir_item_offset;
	struct inode *inode;
	struct list_head list;
	/* Only used for fast fsyncs. */
	struct list_head ordered_extents;
};

static inline void btrfs_init_log_ctx(struct btrfs_log_ctx *ctx,
				      struct inode *inode)
{
	ctx->log_ret = 0;
	ctx->log_transid = 0;
	ctx->log_new_dentries = false;
	ctx->logging_new_name = false;
	ctx->logged_before = false;
	ctx->inode = inode;
	INIT_LIST_HEAD(&ctx->list);
	INIT_LIST_HEAD(&ctx->ordered_extents);
}

static inline void btrfs_release_log_ctx_extents(struct btrfs_log_ctx *ctx)
{
	struct btrfs_ordered_extent *ordered;
	struct btrfs_ordered_extent *tmp;

	ASSERT(inode_is_locked(ctx->inode));

	list_for_each_entry_safe(ordered, tmp, &ctx->ordered_extents, log_list) {
		list_del_init(&ordered->log_list);
		btrfs_put_ordered_extent(ordered);
	}
}

static inline void btrfs_set_log_full_commit(struct btrfs_trans_handle *trans)
{
	WRITE_ONCE(trans->fs_info->last_trans_log_full_commit, trans->transid);
}

static inline int btrfs_need_log_full_commit(struct btrfs_trans_handle *trans)
{
	return READ_ONCE(trans->fs_info->last_trans_log_full_commit) ==
		trans->transid;
}

int btrfs_sync_log(struct btrfs_trans_handle *trans,
		   struct btrfs_root *root, struct btrfs_log_ctx *ctx);
int btrfs_free_log(struct btrfs_trans_handle *trans, struct btrfs_root *root);
int btrfs_free_log_root_tree(struct btrfs_trans_handle *trans,
			     struct btrfs_fs_info *fs_info);
int btrfs_recover_log_trees(struct btrfs_root *tree_root);
int btrfs_log_dentry_safe(struct btrfs_trans_handle *trans,
			  struct dentry *dentry,
			  struct btrfs_log_ctx *ctx);
void btrfs_del_dir_entries_in_log(struct btrfs_trans_handle *trans,
				  struct btrfs_root *root,
				  const char *name, int name_len,
				  struct btrfs_inode *dir, u64 index);
void btrfs_del_inode_ref_in_log(struct btrfs_trans_handle *trans,
				struct btrfs_root *root,
				const char *name, int name_len,
				struct btrfs_inode *inode, u64 dirid);
void btrfs_end_log_trans(struct btrfs_root *root);
void btrfs_pin_log_trans(struct btrfs_root *root);
void btrfs_record_unlink_dir(struct btrfs_trans_handle *trans,
			     struct btrfs_inode *dir, struct btrfs_inode *inode,
			     int for_rename);
void btrfs_record_snapshot_destroy(struct btrfs_trans_handle *trans,
				   struct btrfs_inode *dir);
void btrfs_log_new_name(struct btrfs_trans_handle *trans,
			struct dentry *old_dentry, struct btrfs_inode *old_dir,
			u64 old_dir_index, struct dentry *parent);

#endif
