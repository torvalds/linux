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

int btrfs_sync_log(struct btrfs_trans_handle *trans,
		   struct btrfs_root *root);
int btrfs_free_log(struct btrfs_trans_handle *trans, struct btrfs_root *root);
int btrfs_log_dentry(struct btrfs_trans_handle *trans,
		    struct btrfs_root *root, struct dentry *dentry);
int btrfs_recover_log_trees(struct btrfs_root *tree_root);
int btrfs_log_dentry_safe(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root, struct dentry *dentry);
int btrfs_log_inode(struct btrfs_trans_handle *trans,
		    struct btrfs_root *root, struct inode *inode,
		    int inode_only);
int btrfs_del_dir_entries_in_log(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 const char *name, int name_len,
				 struct inode *dir, u64 index);
int btrfs_del_inode_ref_in_log(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root,
			       const char *name, int name_len,
			       struct inode *inode, u64 dirid);
#endif
