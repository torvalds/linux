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
#include "delayed-ref.h"
#include "ctree.h"

struct btrfs_transaction {
	u64 transid;
	/*
	 * total writers in this transaction, it must be zero before the
	 * transaction can end
	 */
	atomic_t num_writers;
	atomic_t use_count;

	unsigned long num_joined;

	spinlock_t commit_lock;
	int in_commit;
	int commit_done;
	int blocked;
	struct list_head list;
	struct extent_io_tree dirty_pages;
	unsigned long start_time;
	wait_queue_head_t writer_wait;
	wait_queue_head_t commit_wait;
	struct list_head pending_snapshots;
	struct btrfs_delayed_ref_root delayed_refs;
	int aborted;
};

struct btrfs_trans_handle {
	u64 transid;
	u64 bytes_reserved;
	unsigned long use_count;
	unsigned long blocks_reserved;
	unsigned long blocks_used;
	unsigned long delayed_ref_updates;
	struct btrfs_transaction *transaction;
	struct btrfs_block_rsv *block_rsv;
	struct btrfs_block_rsv *orig_rsv;
	int aborted;
	/*
	 * this root is only needed to validate that the root passed to
	 * start_transaction is the same as the one passed to end_transaction.
	 * Subvolume quota depends on this
	 */
	struct btrfs_root *root;
	struct seq_list delayed_ref_elem;
	struct list_head qgroup_ref_list;
};

struct btrfs_pending_snapshot {
	struct dentry *dentry;
	struct btrfs_root *root;
	struct btrfs_root *snap;
	/* block reservation for the operation */
	struct btrfs_block_rsv block_rsv;
	/* extra metadata reseration for relocation */
	int error;
	bool readonly;
	struct list_head list;
};

static inline void btrfs_set_inode_last_trans(struct btrfs_trans_handle *trans,
					      struct inode *inode)
{
	BTRFS_I(inode)->last_trans = trans->transaction->transid;
	BTRFS_I(inode)->last_sub_trans = BTRFS_I(inode)->root->log_transid;
}

int btrfs_end_transaction(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root);
int btrfs_end_transaction_nolock(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root);
struct btrfs_trans_handle *btrfs_start_transaction(struct btrfs_root *root,
						   int num_items);
struct btrfs_trans_handle *btrfs_join_transaction(struct btrfs_root *root);
struct btrfs_trans_handle *btrfs_join_transaction_nolock(struct btrfs_root *root);
struct btrfs_trans_handle *btrfs_start_ioctl_transaction(struct btrfs_root *root);
int btrfs_wait_for_commit(struct btrfs_root *root, u64 transid);
int btrfs_write_and_wait_transaction(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root);

int btrfs_add_dead_root(struct btrfs_root *root);
int btrfs_defrag_root(struct btrfs_root *root, int cacheonly);
int btrfs_clean_old_snapshots(struct btrfs_root *root);
int btrfs_commit_transaction(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root);
int btrfs_commit_transaction_async(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root,
				   int wait_for_unblock);
int btrfs_end_transaction_throttle(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root);
int btrfs_end_transaction_dmeta(struct btrfs_trans_handle *trans,
				struct btrfs_root *root);
int btrfs_should_end_transaction(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root);
void btrfs_throttle(struct btrfs_root *root);
int btrfs_record_root_in_trans(struct btrfs_trans_handle *trans,
				struct btrfs_root *root);
int btrfs_write_and_wait_marked_extents(struct btrfs_root *root,
				struct extent_io_tree *dirty_pages, int mark);
int btrfs_write_marked_extents(struct btrfs_root *root,
				struct extent_io_tree *dirty_pages, int mark);
int btrfs_wait_marked_extents(struct btrfs_root *root,
				struct extent_io_tree *dirty_pages, int mark);
int btrfs_transaction_blocked(struct btrfs_fs_info *info);
int btrfs_transaction_in_commit(struct btrfs_fs_info *info);
void put_transaction(struct btrfs_transaction *transaction);
#endif
