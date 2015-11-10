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

enum btrfs_trans_state {
	TRANS_STATE_RUNNING		= 0,
	TRANS_STATE_BLOCKED		= 1,
	TRANS_STATE_COMMIT_START	= 2,
	TRANS_STATE_COMMIT_DOING	= 3,
	TRANS_STATE_UNBLOCKED		= 4,
	TRANS_STATE_COMPLETED		= 5,
	TRANS_STATE_MAX			= 6,
};

#define BTRFS_TRANS_HAVE_FREE_BGS	0
#define BTRFS_TRANS_DIRTY_BG_RUN	1
#define BTRFS_TRANS_CACHE_ENOSPC	2

struct btrfs_transaction {
	u64 transid;
	/*
	 * total external writers(USERSPACE/START/ATTACH) in this
	 * transaction, it must be zero before the transaction is
	 * being committed
	 */
	atomic_t num_extwriters;
	/*
	 * total writers in this transaction, it must be zero before the
	 * transaction can end
	 */
	atomic_t num_writers;
	atomic_t use_count;
	atomic_t pending_ordered;

	unsigned long flags;

	/* Be protected by fs_info->trans_lock when we want to change it. */
	enum btrfs_trans_state state;
	struct list_head list;
	struct extent_io_tree dirty_pages;
	unsigned long start_time;
	wait_queue_head_t writer_wait;
	wait_queue_head_t commit_wait;
	wait_queue_head_t pending_wait;
	struct list_head pending_snapshots;
	struct list_head pending_chunks;
	struct list_head switch_commits;
	struct list_head dirty_bgs;
	struct list_head io_bgs;
	struct list_head dropped_roots;
	u64 num_dirty_bgs;

	/*
	 * we need to make sure block group deletion doesn't race with
	 * free space cache writeout.  This mutex keeps them from stomping
	 * on each other
	 */
	struct mutex cache_write_mutex;
	spinlock_t dirty_bgs_lock;
	/* Protected by spin lock fs_info->unused_bgs_lock. */
	struct list_head deleted_bgs;
	spinlock_t dropped_roots_lock;
	struct btrfs_delayed_ref_root delayed_refs;
	int aborted;
};

#define __TRANS_FREEZABLE	(1U << 0)

#define __TRANS_USERSPACE	(1U << 8)
#define __TRANS_START		(1U << 9)
#define __TRANS_ATTACH		(1U << 10)
#define __TRANS_JOIN		(1U << 11)
#define __TRANS_JOIN_NOLOCK	(1U << 12)
#define __TRANS_DUMMY		(1U << 13)

#define TRANS_USERSPACE		(__TRANS_USERSPACE | __TRANS_FREEZABLE)
#define TRANS_START		(__TRANS_START | __TRANS_FREEZABLE)
#define TRANS_ATTACH		(__TRANS_ATTACH)
#define TRANS_JOIN		(__TRANS_JOIN | __TRANS_FREEZABLE)
#define TRANS_JOIN_NOLOCK	(__TRANS_JOIN_NOLOCK)

#define TRANS_EXTWRITERS	(__TRANS_USERSPACE | __TRANS_START |	\
				 __TRANS_ATTACH)

#define BTRFS_SEND_TRANS_STUB	((void *)1)

struct btrfs_trans_handle {
	u64 transid;
	u64 bytes_reserved;
	u64 chunk_bytes_reserved;
	unsigned long use_count;
	unsigned long blocks_reserved;
	unsigned long blocks_used;
	unsigned long delayed_ref_updates;
	struct btrfs_transaction *transaction;
	struct btrfs_block_rsv *block_rsv;
	struct btrfs_block_rsv *orig_rsv;
	short aborted;
	short adding_csums;
	bool allocating_chunk;
	bool can_flush_pending_bgs;
	bool reloc_reserved;
	bool sync;
	unsigned int type;
	/*
	 * this root is only needed to validate that the root passed to
	 * start_transaction is the same as the one passed to end_transaction.
	 * Subvolume quota depends on this
	 */
	struct btrfs_root *root;
	struct seq_list delayed_ref_elem;
	struct list_head qgroup_ref_list;
	struct list_head new_bgs;
};

struct btrfs_pending_snapshot {
	struct dentry *dentry;
	struct inode *dir;
	struct btrfs_root *root;
	struct btrfs_root_item *root_item;
	struct btrfs_root *snap;
	struct btrfs_qgroup_inherit *inherit;
	struct btrfs_path *path;
	/* block reservation for the operation */
	struct btrfs_block_rsv block_rsv;
	u64 qgroup_reserved;
	/* extra metadata reseration for relocation */
	int error;
	bool readonly;
	struct list_head list;
};

static inline void btrfs_set_inode_last_trans(struct btrfs_trans_handle *trans,
					      struct inode *inode)
{
	spin_lock(&BTRFS_I(inode)->lock);
	BTRFS_I(inode)->last_trans = trans->transaction->transid;
	BTRFS_I(inode)->last_sub_trans = BTRFS_I(inode)->root->log_transid;
	BTRFS_I(inode)->last_log_commit = BTRFS_I(inode)->root->last_log_commit;
	spin_unlock(&BTRFS_I(inode)->lock);
}

/*
 * Make qgroup codes to skip given qgroupid, means the old/new_roots for
 * qgroup won't contain the qgroupid in it.
 */
static inline void btrfs_set_skip_qgroup(struct btrfs_trans_handle *trans,
					 u64 qgroupid)
{
	struct btrfs_delayed_ref_root *delayed_refs;

	delayed_refs = &trans->transaction->delayed_refs;
	WARN_ON(delayed_refs->qgroup_to_skip);
	delayed_refs->qgroup_to_skip = qgroupid;
}

static inline void btrfs_clear_skip_qgroup(struct btrfs_trans_handle *trans)
{
	struct btrfs_delayed_ref_root *delayed_refs;

	delayed_refs = &trans->transaction->delayed_refs;
	WARN_ON(!delayed_refs->qgroup_to_skip);
	delayed_refs->qgroup_to_skip = 0;
}

int btrfs_end_transaction(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root);
struct btrfs_trans_handle *btrfs_start_transaction(struct btrfs_root *root,
						   unsigned int num_items);
struct btrfs_trans_handle *btrfs_start_transaction_fallback_global_rsv(
					struct btrfs_root *root,
					unsigned int num_items,
					int min_factor);
struct btrfs_trans_handle *btrfs_start_transaction_lflush(
					struct btrfs_root *root,
					unsigned int num_items);
struct btrfs_trans_handle *btrfs_join_transaction(struct btrfs_root *root);
struct btrfs_trans_handle *btrfs_join_transaction_nolock(struct btrfs_root *root);
struct btrfs_trans_handle *btrfs_attach_transaction(struct btrfs_root *root);
struct btrfs_trans_handle *btrfs_attach_transaction_barrier(
					struct btrfs_root *root);
struct btrfs_trans_handle *btrfs_start_ioctl_transaction(struct btrfs_root *root);
int btrfs_wait_for_commit(struct btrfs_root *root, u64 transid);

void btrfs_add_dead_root(struct btrfs_root *root);
int btrfs_defrag_root(struct btrfs_root *root);
int btrfs_clean_one_deleted_snapshot(struct btrfs_root *root);
int btrfs_commit_transaction(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root);
int btrfs_commit_transaction_async(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root,
				   int wait_for_unblock);
int btrfs_end_transaction_throttle(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root);
int btrfs_should_end_transaction(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root);
void btrfs_throttle(struct btrfs_root *root);
int btrfs_record_root_in_trans(struct btrfs_trans_handle *trans,
				struct btrfs_root *root);
int btrfs_write_marked_extents(struct btrfs_root *root,
				struct extent_io_tree *dirty_pages, int mark);
int btrfs_wait_marked_extents(struct btrfs_root *root,
				struct extent_io_tree *dirty_pages, int mark);
int btrfs_transaction_blocked(struct btrfs_fs_info *info);
int btrfs_transaction_in_commit(struct btrfs_fs_info *info);
void btrfs_put_transaction(struct btrfs_transaction *transaction);
void btrfs_apply_pending_changes(struct btrfs_fs_info *fs_info);
void btrfs_add_dropped_root(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root);
#endif
