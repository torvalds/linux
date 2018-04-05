/*
 * Copyright (C) 2008 Oracle.  All rights reserved.
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

#ifndef __TREE_LOG_
#define __TREE_LOG_

#include "ctree.h"
#include "transaction.h"

/* return value for btrfs_log_dentry_safe that means we don't need to log it at all */
#define BTRFS_NO_LOG_SYNC 256

struct btrfs_log_ctx {
	int log_ret;
	int log_transid;
	int io_err;
	bool log_new_dentries;
	struct inode *inode;
	struct list_head list;
};

static inline void btrfs_init_log_ctx(struct btrfs_log_ctx *ctx,
				      struct inode *inode)
{
	ctx->log_ret = 0;
	ctx->log_transid = 0;
	ctx->io_err = 0;
	ctx->log_new_dentries = false;
	ctx->inode = inode;
	INIT_LIST_HEAD(&ctx->list);
}

static inline void btrfs_set_log_full_commit(struct btrfs_fs_info *fs_info,
					     struct btrfs_trans_handle *trans)
{
	WRITE_ONCE(fs_info->last_trans_log_full_commit, trans->transid);
}

static inline int btrfs_need_log_full_commit(struct btrfs_fs_info *fs_info,
					     struct btrfs_trans_handle *trans)
{
	return READ_ONCE(fs_info->last_trans_log_full_commit) ==
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
			  const loff_t start,
			  const loff_t end,
			  struct btrfs_log_ctx *ctx);
int btrfs_del_dir_entries_in_log(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 const char *name, int name_len,
				 struct btrfs_inode *dir, u64 index);
int btrfs_del_inode_ref_in_log(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root,
			       const char *name, int name_len,
			       struct btrfs_inode *inode, u64 dirid);
void btrfs_end_log_trans(struct btrfs_root *root);
int btrfs_pin_log_trans(struct btrfs_root *root);
void btrfs_record_unlink_dir(struct btrfs_trans_handle *trans,
			     struct btrfs_inode *dir, struct btrfs_inode *inode,
			     int for_rename);
void btrfs_record_snapshot_destroy(struct btrfs_trans_handle *trans,
				   struct btrfs_inode *dir);
int btrfs_log_new_name(struct btrfs_trans_handle *trans,
			struct btrfs_inode *inode, struct btrfs_inode *old_dir,
			struct dentry *parent);
#endif
