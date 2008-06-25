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

#ifndef __BTRFS_TRANSACTION__
#define __BTRFS_TRANSACTION__
#include "btrfs_inode.h"
#include "ordered-data.h"

struct btrfs_transaction {
	u64 transid;
	unsigned long num_writers;
	unsigned long num_joined;
	int in_commit;
	int use_count;
	int commit_done;
	struct list_head list;
	struct extent_io_tree dirty_pages;
	unsigned long start_time;
	struct btrfs_ordered_inode_tree ordered_inode_tree;
	wait_queue_head_t writer_wait;
	wait_queue_head_t commit_wait;
	struct list_head pending_snapshots;
};

struct btrfs_trans_handle {
	u64 transid;
	unsigned long blocks_reserved;
	unsigned long blocks_used;
	struct btrfs_transaction *transaction;
	struct btrfs_block_group_cache *block_group;
	u64 alloc_exclude_start;
	u64 alloc_exclude_nr;
};

struct btrfs_pending_snapshot {
	struct btrfs_root *root;
	char *name;
	struct list_head list;
};


static inline void btrfs_set_trans_block_group(struct btrfs_trans_handle *trans,
					       struct inode *inode)
{
	trans->block_group = BTRFS_I(inode)->block_group;
}

static inline void btrfs_update_inode_block_group(struct
						  btrfs_trans_handle *trans,
						  struct inode *inode)
{
	BTRFS_I(inode)->block_group = trans->block_group;
}

static inline void btrfs_set_inode_last_trans(struct btrfs_trans_handle *trans,
					      struct inode *inode)
{
	BTRFS_I(inode)->last_trans = trans->transaction->transid;
}

int btrfs_end_transaction(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root);
struct btrfs_trans_handle *btrfs_start_transaction(struct btrfs_root *root,
						   int num_blocks);
int btrfs_write_and_wait_transaction(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root);
int btrfs_commit_tree_roots(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root);

int btrfs_add_dead_root(struct btrfs_root *root, struct btrfs_root *latest,
			struct list_head *dead_list);
int btrfs_defrag_root(struct btrfs_root *root, int cacheonly);
int btrfs_clean_old_snapshots(struct btrfs_root *root);
int btrfs_commit_transaction(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root);
int btrfs_write_ordered_inodes(struct btrfs_trans_handle *trans,
				struct btrfs_root *root);
int btrfs_end_transaction_throttle(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root);
#endif
