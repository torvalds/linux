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

#ifndef __DISKIO__
#define __DISKIO__

#include <linux/buffer_head.h>

#define BTRFS_SUPER_INFO_OFFSET (16 * 1024)

enum btrfs_bh_state_bits {
	BH_Checked = BH_PrivateStart,
	BH_Defrag,
	BH_DefragDone,
};
BUFFER_FNS(Checked, checked);
BUFFER_FNS(Defrag, defrag);
BUFFER_FNS(DefragDone, defrag_done);

static inline struct btrfs_node *btrfs_buffer_node(struct buffer_head *bh)
{
	return (struct btrfs_node *)bh->b_data;
}

static inline struct btrfs_leaf *btrfs_buffer_leaf(struct buffer_head *bh)
{
	return (struct btrfs_leaf *)bh->b_data;
}

static inline struct btrfs_header *btrfs_buffer_header(struct buffer_head *bh)
{
	return &((struct btrfs_node *)bh->b_data)->header;
}

struct buffer_head *read_tree_block(struct btrfs_root *root, u64 blocknr);
int readahead_tree_block(struct btrfs_root *root, u64 blocknr);
struct buffer_head *btrfs_find_create_tree_block(struct btrfs_root *root,
						 u64 blocknr);
int write_tree_block(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		     struct buffer_head *buf);
int dirty_tree_block(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		     struct buffer_head *buf);
int clean_tree_block(struct btrfs_trans_handle *trans,
		     struct btrfs_root *root, struct buffer_head *buf);
int btrfs_commit_transaction(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root);
struct btrfs_root *open_ctree(struct super_block *sb);
int close_ctree(struct btrfs_root *root);
void btrfs_block_release(struct btrfs_root *root, struct buffer_head *buf);
int write_ctree_super(struct btrfs_trans_handle *trans,
		      struct btrfs_root *root);
struct buffer_head *btrfs_find_tree_block(struct btrfs_root *root, u64 blocknr);
int btrfs_csum_data(struct btrfs_root * root, char *data, size_t len,
		    char *result);
struct btrfs_root *btrfs_read_fs_root(struct btrfs_fs_info *fs_info,
				      struct btrfs_key *location,
				      const char *name, int namelen);
struct btrfs_root *btrfs_read_fs_root_no_radix(struct btrfs_fs_info *fs_info,
					       struct btrfs_key *location);
u64 bh_blocknr(struct buffer_head *bh);
int btrfs_insert_dev_radix(struct btrfs_root *root,
			   struct block_device *bdev,
			   u64 device_id,
			   u64 block_start,
			   u64 num_blocks);
int btrfs_map_bh_to_logical(struct btrfs_root *root, struct buffer_head *bh,
			     u64 logical);
void btrfs_btree_balance_dirty(struct btrfs_root *root, unsigned long nr);
int btrfs_free_fs_root(struct btrfs_fs_info *fs_info, struct btrfs_root *root);
void btrfs_mark_buffer_dirty(struct buffer_head *bh);
#endif
