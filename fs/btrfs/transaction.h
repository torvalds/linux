/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#ifndef BTRFS_TRANSACTION_H
#define BTRFS_TRANSACTION_H

#include <linux/refcount.h>
#include "btrfs_inode.h"
#include "delayed-ref.h"
#include "ctree.h"

enum btrfs_trans_state {
	TRANS_STATE_RUNNING,
	TRANS_STATE_BLOCKED,
	TRANS_STATE_COMMIT_START,
	TRANS_STATE_COMMIT_DOING,
	TRANS_STATE_UNBLOCKED,
	TRANS_STATE_COMPLETED,
	TRANS_STATE_MAX,
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
	refcount_t use_count;

	unsigned long flags;

	/* Be protected by fs_info->trans_lock when we want to change it. */
	enum btrfs_trans_state state;
	int aborted;
	struct list_head list;
	struct extent_io_tree dirty_pages;
	time64_t start_time;
	wait_queue_head_t writer_wait;
	wait_queue_head_t commit_wait;
	struct list_head pending_snapshots;
	struct list_head pending_chunks;
	struct list_head switch_commits;
	struct list_head dirty_bgs;

	/*
	 * There is no explicit lock which protects io_bgs, rather its
	 * consistency is implied by the fact that all the sites which modify
	 * it do so under some form of transaction critical section, namely:
	 *
	 * - btrfs_start_dirty_block_groups - This function can only ever be
	 *   run by one of the transaction committers. Refer to
	 *   BTRFS_TRANS_DIRTY_BG_RUN usage in btrfs_commit_transaction
	 *
	 * - btrfs_write_dirty_blockgroups - this is called by
	 *   commit_cowonly_roots from transaction critical section
	 *   (TRANS_STATE_COMMIT_DOING)
	 *
	 * - btrfs_cleanup_dirty_bgs - called on transaction abort
	 */
	struct list_head io_bgs;
	struct list_head dropped_roots;

	/*
	 * we need to make sure block group deletion doesn't race with
	 * free space cache writeout.  This mutex keeps them from stomping
	 * on each other
	 */
	struct mutex cache_write_mutex;
	spinlock_t dirty_bgs_lock;
	unsigned int num_dirty_bgs;
	/* Protected by spin lock fs_info->unused_bgs_lock. */
	struct list_head deleted_bgs;
	spinlock_t dropped_roots_lock;
	struct btrfs_delayed_ref_root delayed_refs;
	struct btrfs_fs_info *fs_info;
};

#define __TRANS_FREEZABLE	(1U << 0)

#define __TRANS_START		(1U << 9)
#define __TRANS_ATTACH		(1U << 10)
#define __TRANS_JOIN		(1U << 11)
#define __TRANS_JOIN_NOLOCK	(1U << 12)
#define __TRANS_DUMMY		(1U << 13)

#define TRANS_START		(__TRANS_START | __TRANS_FREEZABLE)
#define TRANS_ATTACH		(__TRANS_ATTACH)
#define TRANS_JOIN		(__TRANS_JOIN | __TRANS_FREEZABLE)
#define TRANS_JOIN_NOLOCK	(__TRANS_JOIN_NOLOCK)

#define TRANS_EXTWRITERS	(__TRANS_START | __TRANS_ATTACH)

#define BTRFS_SEND_TRANS_STUB	((void *)1)

struct btrfs_trans_handle {
	u64 transid;
	u64 bytes_reserved;
	u64 chunk_bytes_reserved;
	unsigned long delayed_ref_updates;
	struct btrfs_transaction *transaction;
	struct btrfs_block_rsv *block_rsv;
	struct btrfs_block_rsv *orig_rsv;
	refcount_t use_count;
	unsigned int type;
	short aborted;
	bool adding_csums;
	bool allocating_chunk;
	bool can_flush_pending_bgs;
	bool reloc_reserved;
	bool sync;
	bool dirty;
	struct btrfs_root *root;
	struct btrfs_fs_info *fs_info;
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
	/* extra metadata reservation for relocation */
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

int btrfs_end_transaction(struct btrfs_trans_handle *trans);
struct btrfs_trans_handle *btrfs_start_transaction(struct btrfs_root *root,
						   unsigned int num_items);
struct btrfs_trans_handle *btrfs_start_transaction_fallback_global_rsv(
					struct btrfs_root *root,
					unsigned int num_items,
					int min_factor);
struct btrfs_trans_handle *btrfs_join_transaction(struct btrfs_root *root);
struct btrfs_trans_handle *btrfs_join_transaction_nolock(struct btrfs_root *root);
struct btrfs_trans_handle *btrfs_attach_transaction(struct btrfs_root *root);
struct btrfs_trans_handle *btrfs_attach_transaction_barrier(
					struct btrfs_root *root);
int btrfs_wait_for_commit(struct btrfs_fs_info *fs_info, u64 transid);

void btrfs_add_dead_root(struct btrfs_root *root);
int btrfs_defrag_root(struct btrfs_root *root);
int btrfs_clean_one_deleted_snapshot(struct btrfs_root *root);
int btrfs_commit_transaction(struct btrfs_trans_handle *trans);
int btrfs_commit_transaction_async(struct btrfs_trans_handle *trans,
				   int wait_for_unblock);

/*
 * Try to commit transaction asynchronously, so this is safe to call
 * even holding a spinlock.
 *
 * It's done by informing transaction_kthread to commit transaction without
 * waiting for commit interval.
 */
static inline void btrfs_commit_transaction_locksafe(
		struct btrfs_fs_info *fs_info)
{
	set_bit(BTRFS_FS_NEED_ASYNC_COMMIT, &fs_info->flags);
	wake_up_process(fs_info->transaction_kthread);
}
int btrfs_end_transaction_throttle(struct btrfs_trans_handle *trans);
int btrfs_should_end_transaction(struct btrfs_trans_handle *trans);
void btrfs_throttle(struct btrfs_fs_info *fs_info);
int btrfs_record_root_in_trans(struct btrfs_trans_handle *trans,
				struct btrfs_root *root);
int btrfs_write_marked_extents(struct btrfs_fs_info *fs_info,
				struct extent_io_tree *dirty_pages, int mark);
int btrfs_wait_extents(struct btrfs_fs_info *fs_info,
		       struct extent_io_tree *dirty_pages);
int btrfs_wait_tree_log_extents(struct btrfs_root *root, int mark);
int btrfs_transaction_blocked(struct btrfs_fs_info *info);
int btrfs_transaction_in_commit(struct btrfs_fs_info *info);
void btrfs_put_transaction(struct btrfs_transaction *transaction);
void btrfs_apply_pending_changes(struct btrfs_fs_info *fs_info);
void btrfs_add_dropped_root(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root);

#endif
