/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2014 Facebook.  All rights reserved.
 */

#ifndef BTRFS_QGROUP_H
#define BTRFS_QGROUP_H

#include <linux/spinlock.h>
#include <linux/rbtree.h>
#include "ulist.h"
#include "delayed-ref.h"

/*
 * Btrfs qgroup overview
 *
 * Btrfs qgroup splits into 3 main part:
 * 1) Reserve
 *    Reserve metadata/data space for incoming operations
 *    Affect how qgroup limit works
 *
 * 2) Trace
 *    Tell btrfs qgroup to trace dirty extents.
 *
 *    Dirty extents including:
 *    - Newly allocated extents
 *    - Extents going to be deleted (in this trans)
 *    - Extents whose owner is going to be modified
 *
 *    This is the main part affects whether qgroup numbers will stay
 *    consistent.
 *    Btrfs qgroup can trace clean extents and won't cause any problem,
 *    but it will consume extra CPU time, it should be avoided if possible.
 *
 * 3) Account
 *    Btrfs qgroup will updates its numbers, based on dirty extents traced
 *    in previous step.
 *
 *    Normally at qgroup rescan and transaction commit time.
 */

/*
 * Special performance optimization for balance.
 *
 * For balance, we need to swap subtree of subvolume and reloc trees.
 * In theory, we need to trace all subtree blocks of both subvolume and reloc
 * trees, since their owner has changed during such swap.
 *
 * However since balance has ensured that both subtrees are containing the
 * same contents and have the same tree structures, such swap won't cause
 * qgroup number change.
 *
 * But there is a race window between subtree swap and transaction commit,
 * during that window, if we increase/decrease tree level or merge/split tree
 * blocks, we still need to trace the original subtrees.
 *
 * So for balance, we use a delayed subtree tracing, whose workflow is:
 *
 * 1) Record the subtree root block get swapped.
 *
 *    During subtree swap:
 *    O = Old tree blocks
 *    N = New tree blocks
 *          reloc tree                     subvolume tree X
 *             Root                               Root
 *            /    \                             /    \
 *          NA     OB                          OA      OB
 *        /  |     |  \                      /  |      |  \
 *      NC  ND     OE  OF                   OC  OD     OE  OF
 *
 *   In this case, NA and OA are going to be swapped, record (NA, OA) into
 *   subvolume tree X.
 *
 * 2) After subtree swap.
 *          reloc tree                     subvolume tree X
 *             Root                               Root
 *            /    \                             /    \
 *          OA     OB                          NA      OB
 *        /  |     |  \                      /  |      |  \
 *      OC  OD     OE  OF                   NC  ND     OE  OF
 *
 * 3a) COW happens for OB
 *     If we are going to COW tree block OB, we check OB's bytenr against
 *     tree X's swapped_blocks structure.
 *     If it doesn't fit any, nothing will happen.
 *
 * 3b) COW happens for NA
 *     Check NA's bytenr against tree X's swapped_blocks, and get a hit.
 *     Then we do subtree scan on both subtrees OA and NA.
 *     Resulting 6 tree blocks to be scanned (OA, OC, OD, NA, NC, ND).
 *
 *     Then no matter what we do to subvolume tree X, qgroup numbers will
 *     still be correct.
 *     Then NA's record gets removed from X's swapped_blocks.
 *
 * 4)  Transaction commit
 *     Any record in X's swapped_blocks gets removed, since there is no
 *     modification to the swapped subtrees, no need to trigger heavy qgroup
 *     subtree rescan for them.
 */

/*
 * Record a dirty extent, and info qgroup to update quota on it
 * TODO: Use kmem cache to alloc it.
 */
struct btrfs_qgroup_extent_record {
	struct rb_node node;
	u64 bytenr;
	u64 num_bytes;

	/*
	 * For qgroup reserved data space freeing.
	 *
	 * @data_rsv_refroot and @data_rsv will be recorded after
	 * BTRFS_ADD_DELAYED_EXTENT is called.
	 * And will be used to free reserved qgroup space at
	 * transaction commit time.
	 */
	u32 data_rsv;		/* reserved data space needs to be freed */
	u64 data_rsv_refroot;	/* which root the reserved data belongs to */
	struct ulist *old_roots;
};

struct btrfs_qgroup_swapped_block {
	struct rb_node node;

	int level;
	bool trace_leaf;

	/* bytenr/generation of the tree block in subvolume tree after swap */
	u64 subvol_bytenr;
	u64 subvol_generation;

	/* bytenr/generation of the tree block in reloc tree after swap */
	u64 reloc_bytenr;
	u64 reloc_generation;

	u64 last_snapshot;
	struct btrfs_key first_key;
};

/*
 * Qgroup reservation types:
 *
 * DATA:
 *	space reserved for data
 *
 * META_PERTRANS:
 * 	Space reserved for metadata (per-transaction)
 * 	Due to the fact that qgroup data is only updated at transaction commit
 * 	time, reserved space for metadata must be kept until transaction
 * 	commits.
 * 	Any metadata reserved that are used in btrfs_start_transaction() should
 * 	be of this type.
 *
 * META_PREALLOC:
 *	There are cases where metadata space is reserved before starting
 *	transaction, and then btrfs_join_transaction() to get a trans handle.
 *	Any metadata reserved for such usage should be of this type.
 *	And after join_transaction() part (or all) of such reservation should
 *	be converted into META_PERTRANS.
 */
enum btrfs_qgroup_rsv_type {
	BTRFS_QGROUP_RSV_DATA,
	BTRFS_QGROUP_RSV_META_PERTRANS,
	BTRFS_QGROUP_RSV_META_PREALLOC,
	BTRFS_QGROUP_RSV_LAST,
};

/*
 * Represents how many bytes we have reserved for this qgroup.
 *
 * Each type should have different reservation behavior.
 * E.g, data follows its io_tree flag modification, while
 * *currently* meta is just reserve-and-clear during transaction.
 *
 * TODO: Add new type for reservation which can survive transaction commit.
 * Current metadata reservation behavior is not suitable for such case.
 */
struct btrfs_qgroup_rsv {
	u64 values[BTRFS_QGROUP_RSV_LAST];
};

/*
 * one struct for each qgroup, organized in fs_info->qgroup_tree.
 */
struct btrfs_qgroup {
	u64 qgroupid;

	/*
	 * state
	 */
	u64 rfer;	/* referenced */
	u64 rfer_cmpr;	/* referenced compressed */
	u64 excl;	/* exclusive */
	u64 excl_cmpr;	/* exclusive compressed */

	/*
	 * limits
	 */
	u64 lim_flags;	/* which limits are set */
	u64 max_rfer;
	u64 max_excl;
	u64 rsv_rfer;
	u64 rsv_excl;

	/*
	 * reservation tracking
	 */
	struct btrfs_qgroup_rsv rsv;

	/*
	 * lists
	 */
	struct list_head groups;  /* groups this group is member of */
	struct list_head members; /* groups that are members of this group */
	struct list_head dirty;   /* dirty groups */
	struct rb_node node;	  /* tree of qgroups */

	/*
	 * temp variables for accounting operations
	 * Refer to qgroup_shared_accounting() for details.
	 */
	u64 old_refcnt;
	u64 new_refcnt;
};

/*
 * For qgroup event trace points only
 */
#define QGROUP_RESERVE		(1<<0)
#define QGROUP_RELEASE		(1<<1)
#define QGROUP_FREE		(1<<2)

int btrfs_quota_enable(struct btrfs_fs_info *fs_info);
int btrfs_quota_disable(struct btrfs_fs_info *fs_info);
int btrfs_qgroup_rescan(struct btrfs_fs_info *fs_info);
void btrfs_qgroup_rescan_resume(struct btrfs_fs_info *fs_info);
int btrfs_qgroup_wait_for_completion(struct btrfs_fs_info *fs_info,
				     bool interruptible);
int btrfs_add_qgroup_relation(struct btrfs_trans_handle *trans, u64 src,
			      u64 dst);
int btrfs_del_qgroup_relation(struct btrfs_trans_handle *trans, u64 src,
			      u64 dst);
int btrfs_create_qgroup(struct btrfs_trans_handle *trans, u64 qgroupid);
int btrfs_remove_qgroup(struct btrfs_trans_handle *trans, u64 qgroupid);
int btrfs_limit_qgroup(struct btrfs_trans_handle *trans, u64 qgroupid,
		       struct btrfs_qgroup_limit *limit);
int btrfs_read_qgroup_config(struct btrfs_fs_info *fs_info);
void btrfs_free_qgroup_config(struct btrfs_fs_info *fs_info);
struct btrfs_delayed_extent_op;

/*
 * Inform qgroup to trace one dirty extent, its info is recorded in @record.
 * So qgroup can account it at transaction committing time.
 *
 * No lock version, caller must acquire delayed ref lock and allocated memory,
 * then call btrfs_qgroup_trace_extent_post() after exiting lock context.
 *
 * Return 0 for success insert
 * Return >0 for existing record, caller can free @record safely.
 * Error is not possible
 */
int btrfs_qgroup_trace_extent_nolock(
		struct btrfs_fs_info *fs_info,
		struct btrfs_delayed_ref_root *delayed_refs,
		struct btrfs_qgroup_extent_record *record);

/*
 * Post handler after qgroup_trace_extent_nolock().
 *
 * NOTE: Current qgroup does the expensive backref walk at transaction
 * committing time with TRANS_STATE_COMMIT_DOING, this blocks incoming
 * new transaction.
 * This is designed to allow btrfs_find_all_roots() to get correct new_roots
 * result.
 *
 * However for old_roots there is no need to do backref walk at that time,
 * since we search commit roots to walk backref and result will always be
 * correct.
 *
 * Due to the nature of no lock version, we can't do backref there.
 * So we must call btrfs_qgroup_trace_extent_post() after exiting
 * spinlock context.
 *
 * TODO: If we can fix and prove btrfs_find_all_roots() can get correct result
 * using current root, then we can move all expensive backref walk out of
 * transaction committing, but not now as qgroup accounting will be wrong again.
 */
int btrfs_qgroup_trace_extent_post(struct btrfs_fs_info *fs_info,
				   struct btrfs_qgroup_extent_record *qrecord);

/*
 * Inform qgroup to trace one dirty extent, specified by @bytenr and
 * @num_bytes.
 * So qgroup can account it at commit trans time.
 *
 * Better encapsulated version, with memory allocation and backref walk for
 * commit roots.
 * So this can sleep.
 *
 * Return 0 if the operation is done.
 * Return <0 for error, like memory allocation failure or invalid parameter
 * (NULL trans)
 */
int btrfs_qgroup_trace_extent(struct btrfs_trans_handle *trans, u64 bytenr,
			      u64 num_bytes, gfp_t gfp_flag);

/*
 * Inform qgroup to trace all leaf items of data
 *
 * Return 0 for success
 * Return <0 for error(ENOMEM)
 */
int btrfs_qgroup_trace_leaf_items(struct btrfs_trans_handle *trans,
				  struct extent_buffer *eb);
/*
 * Inform qgroup to trace a whole subtree, including all its child tree
 * blocks and data.
 * The root tree block is specified by @root_eb.
 *
 * Normally used by relocation(tree block swap) and subvolume deletion.
 *
 * Return 0 for success
 * Return <0 for error(ENOMEM or tree search error)
 */
int btrfs_qgroup_trace_subtree(struct btrfs_trans_handle *trans,
			       struct extent_buffer *root_eb,
			       u64 root_gen, int root_level);
int btrfs_qgroup_account_extent(struct btrfs_trans_handle *trans, u64 bytenr,
				u64 num_bytes, struct ulist *old_roots,
				struct ulist *new_roots);
int btrfs_qgroup_account_extents(struct btrfs_trans_handle *trans);
int btrfs_run_qgroups(struct btrfs_trans_handle *trans);
int btrfs_qgroup_inherit(struct btrfs_trans_handle *trans, u64 srcid,
			 u64 objectid, struct btrfs_qgroup_inherit *inherit);
void btrfs_qgroup_free_refroot(struct btrfs_fs_info *fs_info,
			       u64 ref_root, u64 num_bytes,
			       enum btrfs_qgroup_rsv_type type);

#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
int btrfs_verify_qgroup_counts(struct btrfs_fs_info *fs_info, u64 qgroupid,
			       u64 rfer, u64 excl);
#endif

/* New io_tree based accurate qgroup reserve API */
int btrfs_qgroup_reserve_data(struct inode *inode,
			struct extent_changeset **reserved, u64 start, u64 len);
int btrfs_qgroup_release_data(struct inode *inode, u64 start, u64 len);
int btrfs_qgroup_free_data(struct inode *inode,
			struct extent_changeset *reserved, u64 start, u64 len);

int __btrfs_qgroup_reserve_meta(struct btrfs_root *root, int num_bytes,
				enum btrfs_qgroup_rsv_type type, bool enforce);
/* Reserve metadata space for pertrans and prealloc type */
static inline int btrfs_qgroup_reserve_meta_pertrans(struct btrfs_root *root,
				int num_bytes, bool enforce)
{
	return __btrfs_qgroup_reserve_meta(root, num_bytes,
			BTRFS_QGROUP_RSV_META_PERTRANS, enforce);
}
static inline int btrfs_qgroup_reserve_meta_prealloc(struct btrfs_root *root,
				int num_bytes, bool enforce)
{
	return __btrfs_qgroup_reserve_meta(root, num_bytes,
			BTRFS_QGROUP_RSV_META_PREALLOC, enforce);
}

void __btrfs_qgroup_free_meta(struct btrfs_root *root, int num_bytes,
			     enum btrfs_qgroup_rsv_type type);

/* Free per-transaction meta reservation for error handling */
static inline void btrfs_qgroup_free_meta_pertrans(struct btrfs_root *root,
						   int num_bytes)
{
	__btrfs_qgroup_free_meta(root, num_bytes,
			BTRFS_QGROUP_RSV_META_PERTRANS);
}

/* Pre-allocated meta reservation can be freed at need */
static inline void btrfs_qgroup_free_meta_prealloc(struct btrfs_root *root,
						   int num_bytes)
{
	__btrfs_qgroup_free_meta(root, num_bytes,
			BTRFS_QGROUP_RSV_META_PREALLOC);
}

/*
 * Per-transaction meta reservation should be all freed at transaction commit
 * time
 */
void btrfs_qgroup_free_meta_all_pertrans(struct btrfs_root *root);

/*
 * Convert @num_bytes of META_PREALLOCATED reservation to META_PERTRANS.
 *
 * This is called when preallocated meta reservation needs to be used.
 * Normally after btrfs_join_transaction() call.
 */
void btrfs_qgroup_convert_reserved_meta(struct btrfs_root *root, int num_bytes);

void btrfs_qgroup_check_reserved_leak(struct inode *inode);

/* btrfs_qgroup_swapped_blocks related functions */
void btrfs_qgroup_init_swapped_blocks(
	struct btrfs_qgroup_swapped_blocks *swapped_blocks);

void btrfs_qgroup_clean_swapped_blocks(struct btrfs_root *root);
int btrfs_qgroup_add_swapped_blocks(struct btrfs_trans_handle *trans,
		struct btrfs_root *subvol_root,
		struct btrfs_block_group_cache *bg,
		struct extent_buffer *subvol_parent, int subvol_slot,
		struct extent_buffer *reloc_parent, int reloc_slot,
		u64 last_snapshot);
int btrfs_qgroup_trace_subtree_after_cow(struct btrfs_trans_handle *trans,
		struct btrfs_root *root, struct extent_buffer *eb);

#endif
