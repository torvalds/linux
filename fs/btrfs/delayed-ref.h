/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2008 Oracle.  All rights reserved.
 */

#ifndef BTRFS_DELAYED_REF_H
#define BTRFS_DELAYED_REF_H

#include <linux/types.h>
#include <linux/refcount.h>
#include <linux/list.h>
#include <linux/rbtree.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <uapi/linux/btrfs_tree.h>

struct btrfs_trans_handle;
struct btrfs_fs_info;

/* these are the possible values of struct btrfs_delayed_ref_node->action */
enum btrfs_delayed_ref_action {
	/* Add one backref to the tree */
	BTRFS_ADD_DELAYED_REF = 1,
	/* Delete one backref from the tree */
	BTRFS_DROP_DELAYED_REF,
	/* Record a full extent allocation */
	BTRFS_ADD_DELAYED_EXTENT,
	/* Not changing ref count on head ref */
	BTRFS_UPDATE_DELAYED_HEAD,
} __packed;

struct btrfs_data_ref {
	/* For EXTENT_DATA_REF */

	/* Inode which refers to this data extent */
	u64 objectid;

	/*
	 * file_offset - extent_offset
	 *
	 * file_offset is the key.offset of the EXTENT_DATA key.
	 * extent_offset is btrfs_file_extent_offset() of the EXTENT_DATA data.
	 */
	u64 offset;
};

struct btrfs_tree_ref {
	/*
	 * Level of this tree block.
	 *
	 * Shared for skinny (TREE_BLOCK_REF) and normal tree ref.
	 */
	int level;

	/* For non-skinny metadata, no special member needed */
};

struct btrfs_delayed_ref_node {
	struct rb_node ref_node;
	/*
	 * If action is BTRFS_ADD_DELAYED_REF, also link this node to
	 * ref_head->ref_add_list, then we do not need to iterate the
	 * whole ref_head->ref_list to find BTRFS_ADD_DELAYED_REF nodes.
	 */
	struct list_head add_list;

	/* the starting bytenr of the extent */
	u64 bytenr;

	/* the size of the extent */
	u64 num_bytes;

	/* seq number to keep track of insertion order */
	u64 seq;

	/* The ref_root for this ref */
	u64 ref_root;

	/*
	 * The parent for this ref, if this isn't set the ref_root is the
	 * reference owner.
	 */
	u64 parent;

	/* ref count on this data structure */
	refcount_t refs;

	/*
	 * how many refs is this entry adding or deleting.  For
	 * head refs, this may be a negative number because it is keeping
	 * track of the total mods done to the reference count.
	 * For individual refs, this will always be a positive number
	 *
	 * It may be more than one, since it is possible for a single
	 * parent to have more than one ref on an extent
	 */
	int ref_mod;

	unsigned int action:8;
	unsigned int type:8;

	union {
		struct btrfs_tree_ref tree_ref;
		struct btrfs_data_ref data_ref;
	};
};

struct btrfs_delayed_extent_op {
	struct btrfs_disk_key key;
	bool update_key;
	bool update_flags;
	u64 flags_to_set;
};

/*
 * the head refs are used to hold a lock on a given extent, which allows us
 * to make sure that only one process is running the delayed refs
 * at a time for a single extent.  They also store the sum of all the
 * reference count modifications we've queued up.
 */
struct btrfs_delayed_ref_head {
	u64 bytenr;
	u64 num_bytes;
	/*
	 * For insertion into struct btrfs_delayed_ref_root::href_root.
	 * Keep it in the same cache line as 'bytenr' for more efficient
	 * searches in the rbtree.
	 */
	struct rb_node href_node;
	/*
	 * the mutex is held while running the refs, and it is also
	 * held when checking the sum of reference modifications.
	 */
	struct mutex mutex;

	refcount_t refs;

	/* Protects 'ref_tree' and 'ref_add_list'. */
	spinlock_t lock;
	struct rb_root_cached ref_tree;
	/* accumulate add BTRFS_ADD_DELAYED_REF nodes to this ref_add_list. */
	struct list_head ref_add_list;

	struct btrfs_delayed_extent_op *extent_op;

	/*
	 * This is used to track the final ref_mod from all the refs associated
	 * with this head ref, this is not adjusted as delayed refs are run,
	 * this is meant to track if we need to do the csum accounting or not.
	 */
	int total_ref_mod;

	/*
	 * This is the current outstanding mod references for this bytenr.  This
	 * is used with lookup_extent_info to get an accurate reference count
	 * for a bytenr, so it is adjusted as delayed refs are run so that any
	 * on disk reference count + ref_mod is accurate.
	 */
	int ref_mod;

	/*
	 * The root that triggered the allocation when must_insert_reserved is
	 * set to true.
	 */
	u64 owning_root;

	/*
	 * Track reserved bytes when setting must_insert_reserved.  On success
	 * or cleanup, we will need to free the reservation.
	 */
	u64 reserved_bytes;

	/* Tree block level, for metadata only. */
	u8 level;

	/*
	 * when a new extent is allocated, it is just reserved in memory
	 * The actual extent isn't inserted into the extent allocation tree
	 * until the delayed ref is processed.  must_insert_reserved is
	 * used to flag a delayed ref so the accounting can be updated
	 * when a full insert is done.
	 *
	 * It is possible the extent will be freed before it is ever
	 * inserted into the extent allocation tree.  In this case
	 * we need to update the in ram accounting to properly reflect
	 * the free has happened.
	 */
	bool must_insert_reserved;

	bool is_data;
	bool is_system;
	bool processing;
};

enum btrfs_delayed_ref_flags {
	/* Indicate that we are flushing delayed refs for the commit */
	BTRFS_DELAYED_REFS_FLUSHING,
};

struct btrfs_delayed_ref_root {
	/* head ref rbtree */
	struct rb_root_cached href_root;

	/*
	 * Track dirty extent records.
	 * The keys correspond to the logical address of the extent ("bytenr")
	 * right shifted by fs_info->sectorsize_bits. This is both to get a more
	 * dense index space (optimizes xarray structure) and because indexes in
	 * xarrays are of "unsigned long" type, meaning they are 32 bits wide on
	 * 32 bits platforms, limiting the extent range to 4G which is too low
	 * and makes it unusable (truncated index values) on 32 bits platforms.
	 */
	struct xarray dirty_extents;

	/* this spin lock protects the rbtree and the entries inside */
	spinlock_t lock;

	/* how many delayed ref updates we've queued, used by the
	 * throttling code
	 */
	atomic_t num_entries;

	/* total number of head nodes in tree */
	unsigned long num_heads;

	/* total number of head nodes ready for processing */
	unsigned long num_heads_ready;

	u64 pending_csums;

	unsigned long flags;

	u64 run_delayed_start;

	/*
	 * To make qgroup to skip given root.
	 * This is for snapshot, as btrfs_qgroup_inherit() will manually
	 * modify counters for snapshot and its source, so we should skip
	 * the snapshot in new_root/old_roots or it will get calculated twice
	 */
	u64 qgroup_to_skip;
};

enum btrfs_ref_type {
	BTRFS_REF_NOT_SET,
	BTRFS_REF_DATA,
	BTRFS_REF_METADATA,
	BTRFS_REF_LAST,
} __packed;

struct btrfs_ref {
	enum btrfs_ref_type type;
	enum btrfs_delayed_ref_action action;

	/*
	 * Whether this extent should go through qgroup record.
	 *
	 * Normally false, but for certain cases like delayed subtree scan,
	 * setting this flag can hugely reduce qgroup overhead.
	 */
	bool skip_qgroup;

#ifdef CONFIG_BTRFS_FS_REF_VERIFY
	/* Through which root is this modification. */
	u64 real_root;
#endif
	u64 bytenr;
	u64 num_bytes;
	u64 owning_root;

	/*
	 * The root that owns the reference for this reference, this will be set
	 * or ->parent will be set, depending on what type of reference this is.
	 */
	u64 ref_root;

	/* Bytenr of the parent tree block */
	u64 parent;
	union {
		struct btrfs_data_ref data_ref;
		struct btrfs_tree_ref tree_ref;
	};
};

extern struct kmem_cache *btrfs_delayed_ref_head_cachep;
extern struct kmem_cache *btrfs_delayed_ref_node_cachep;
extern struct kmem_cache *btrfs_delayed_extent_op_cachep;

int __init btrfs_delayed_ref_init(void);
void __cold btrfs_delayed_ref_exit(void);

static inline u64 btrfs_calc_delayed_ref_bytes(const struct btrfs_fs_info *fs_info,
					       int num_delayed_refs)
{
	u64 num_bytes;

	num_bytes = btrfs_calc_insert_metadata_size(fs_info, num_delayed_refs);

	/*
	 * We have to check the mount option here because we could be enabling
	 * the free space tree for the first time and don't have the compat_ro
	 * option set yet.
	 *
	 * We need extra reservations if we have the free space tree because
	 * we'll have to modify that tree as well.
	 */
	if (btrfs_test_opt(fs_info, FREE_SPACE_TREE))
		num_bytes *= 2;

	return num_bytes;
}

static inline u64 btrfs_calc_delayed_ref_csum_bytes(const struct btrfs_fs_info *fs_info,
						    int num_csum_items)
{
	/*
	 * Deleting csum items does not result in new nodes/leaves and does not
	 * require changing the free space tree, only the csum tree, so this is
	 * all we need.
	 */
	return btrfs_calc_metadata_size(fs_info, num_csum_items);
}

void btrfs_init_tree_ref(struct btrfs_ref *generic_ref, int level, u64 mod_root,
			 bool skip_qgroup);
void btrfs_init_data_ref(struct btrfs_ref *generic_ref, u64 ino, u64 offset,
			 u64 mod_root, bool skip_qgroup);

static inline struct btrfs_delayed_extent_op *
btrfs_alloc_delayed_extent_op(void)
{
	return kmem_cache_alloc(btrfs_delayed_extent_op_cachep, GFP_NOFS);
}

static inline void
btrfs_free_delayed_extent_op(struct btrfs_delayed_extent_op *op)
{
	if (op)
		kmem_cache_free(btrfs_delayed_extent_op_cachep, op);
}

void btrfs_put_delayed_ref(struct btrfs_delayed_ref_node *ref);

static inline u64 btrfs_ref_head_to_space_flags(
				struct btrfs_delayed_ref_head *head_ref)
{
	if (head_ref->is_data)
		return BTRFS_BLOCK_GROUP_DATA;
	else if (head_ref->is_system)
		return BTRFS_BLOCK_GROUP_SYSTEM;
	return BTRFS_BLOCK_GROUP_METADATA;
}

static inline void btrfs_put_delayed_ref_head(struct btrfs_delayed_ref_head *head)
{
	if (refcount_dec_and_test(&head->refs))
		kmem_cache_free(btrfs_delayed_ref_head_cachep, head);
}

int btrfs_add_delayed_tree_ref(struct btrfs_trans_handle *trans,
			       struct btrfs_ref *generic_ref,
			       struct btrfs_delayed_extent_op *extent_op);
int btrfs_add_delayed_data_ref(struct btrfs_trans_handle *trans,
			       struct btrfs_ref *generic_ref,
			       u64 reserved);
int btrfs_add_delayed_extent_op(struct btrfs_trans_handle *trans,
				u64 bytenr, u64 num_bytes, u8 level,
				struct btrfs_delayed_extent_op *extent_op);
void btrfs_merge_delayed_refs(struct btrfs_fs_info *fs_info,
			      struct btrfs_delayed_ref_root *delayed_refs,
			      struct btrfs_delayed_ref_head *head);

struct btrfs_delayed_ref_head *
btrfs_find_delayed_ref_head(struct btrfs_delayed_ref_root *delayed_refs,
			    u64 bytenr);
int btrfs_delayed_ref_lock(struct btrfs_delayed_ref_root *delayed_refs,
			   struct btrfs_delayed_ref_head *head);
static inline void btrfs_delayed_ref_unlock(struct btrfs_delayed_ref_head *head)
{
	mutex_unlock(&head->mutex);
}
void btrfs_delete_ref_head(struct btrfs_delayed_ref_root *delayed_refs,
			   struct btrfs_delayed_ref_head *head);

struct btrfs_delayed_ref_head *btrfs_select_ref_head(
		struct btrfs_delayed_ref_root *delayed_refs);

int btrfs_check_delayed_seq(struct btrfs_fs_info *fs_info, u64 seq);

void btrfs_delayed_refs_rsv_release(struct btrfs_fs_info *fs_info, int nr_refs, int nr_csums);
void btrfs_update_delayed_refs_rsv(struct btrfs_trans_handle *trans);
void btrfs_inc_delayed_refs_rsv_bg_inserts(struct btrfs_fs_info *fs_info);
void btrfs_dec_delayed_refs_rsv_bg_inserts(struct btrfs_fs_info *fs_info);
void btrfs_inc_delayed_refs_rsv_bg_updates(struct btrfs_fs_info *fs_info);
void btrfs_dec_delayed_refs_rsv_bg_updates(struct btrfs_fs_info *fs_info);
int btrfs_delayed_refs_rsv_refill(struct btrfs_fs_info *fs_info,
				  enum btrfs_reserve_flush_enum flush);
bool btrfs_check_space_for_delayed_refs(struct btrfs_fs_info *fs_info);
bool btrfs_find_delayed_tree_ref(struct btrfs_delayed_ref_head *head,
				 u64 root, u64 parent);

static inline u64 btrfs_delayed_ref_owner(struct btrfs_delayed_ref_node *node)
{
	if (node->type == BTRFS_EXTENT_DATA_REF_KEY ||
	    node->type == BTRFS_SHARED_DATA_REF_KEY)
		return node->data_ref.objectid;
	return node->tree_ref.level;
}

static inline u64 btrfs_delayed_ref_offset(struct btrfs_delayed_ref_node *node)
{
	if (node->type == BTRFS_EXTENT_DATA_REF_KEY ||
	    node->type == BTRFS_SHARED_DATA_REF_KEY)
		return node->data_ref.offset;
	return 0;
}

static inline u8 btrfs_ref_type(struct btrfs_ref *ref)
{
	ASSERT(ref->type == BTRFS_REF_DATA || ref->type == BTRFS_REF_METADATA);

	if (ref->type == BTRFS_REF_DATA) {
		if (ref->parent)
			return BTRFS_SHARED_DATA_REF_KEY;
		else
			return BTRFS_EXTENT_DATA_REF_KEY;
	} else {
		if (ref->parent)
			return BTRFS_SHARED_BLOCK_REF_KEY;
		else
			return BTRFS_TREE_BLOCK_REF_KEY;
	}

	return 0;
}

#endif
