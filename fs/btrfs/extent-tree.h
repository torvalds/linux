/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_EXTENT_TREE_H
#define BTRFS_EXTENT_TREE_H

#include "misc.h"
#include "block-group.h"

struct btrfs_free_cluster;

enum btrfs_extent_allocation_policy {
	BTRFS_EXTENT_ALLOC_CLUSTERED,
	BTRFS_EXTENT_ALLOC_ZONED,
};

struct find_free_extent_ctl {
	/* Basic allocation info */
	u64 ram_bytes;
	u64 num_bytes;
	u64 min_alloc_size;
	u64 empty_size;
	u64 flags;
	int delalloc;

	/* Where to start the search inside the bg */
	u64 search_start;

	/* For clustered allocation */
	u64 empty_cluster;
	struct btrfs_free_cluster *last_ptr;
	bool use_cluster;

	bool have_caching_bg;
	bool orig_have_caching_bg;

	/* Allocation is called for tree-log */
	bool for_treelog;

	/* Allocation is called for data relocation */
	bool for_data_reloc;

	/* RAID index, converted from flags */
	int index;

	/*
	 * Current loop number, check find_free_extent_update_loop() for details
	 */
	int loop;

	/*
	 * Whether we're refilling a cluster, if true we need to re-search
	 * current block group but don't try to refill the cluster again.
	 */
	bool retry_clustered;

	/*
	 * Whether we're updating free space cache, if true we need to re-search
	 * current block group but don't try updating free space cache again.
	 */
	bool retry_unclustered;

	/* If current block group is cached */
	int cached;

	/* Max contiguous hole found */
	u64 max_extent_size;

	/* Total free space from free space cache, not always contiguous */
	u64 total_free_space;

	/* Found result */
	u64 found_offset;

	/* Hint where to start looking for an empty space */
	u64 hint_byte;

	/* Allocation policy */
	enum btrfs_extent_allocation_policy policy;

	/* Whether or not the allocator is currently following a hint */
	bool hinted;

	/* Size class of block groups to prefer in early loops */
	enum btrfs_block_group_size_class size_class;
};

enum btrfs_inline_ref_type {
	BTRFS_REF_TYPE_INVALID,
	BTRFS_REF_TYPE_BLOCK,
	BTRFS_REF_TYPE_DATA,
	BTRFS_REF_TYPE_ANY,
};

int btrfs_get_extent_inline_ref_type(const struct extent_buffer *eb,
				     struct btrfs_extent_inline_ref *iref,
				     enum btrfs_inline_ref_type is_data);
u64 hash_extent_data_ref(u64 root_objectid, u64 owner, u64 offset);

int btrfs_add_excluded_extent(struct btrfs_fs_info *fs_info,
			      u64 start, u64 num_bytes);
void btrfs_free_excluded_extents(struct btrfs_block_group *cache);
int btrfs_run_delayed_refs(struct btrfs_trans_handle *trans, unsigned long count);
void btrfs_cleanup_ref_head_accounting(struct btrfs_fs_info *fs_info,
				  struct btrfs_delayed_ref_root *delayed_refs,
				  struct btrfs_delayed_ref_head *head);
int btrfs_lookup_data_extent(struct btrfs_fs_info *fs_info, u64 start, u64 len);
int btrfs_lookup_extent_info(struct btrfs_trans_handle *trans,
			     struct btrfs_fs_info *fs_info, u64 bytenr,
			     u64 offset, int metadata, u64 *refs, u64 *flags);
int btrfs_pin_extent(struct btrfs_trans_handle *trans, u64 bytenr, u64 num,
		     int reserved);
int btrfs_pin_extent_for_log_replay(struct btrfs_trans_handle *trans,
				    u64 bytenr, u64 num_bytes);
int btrfs_exclude_logged_extents(struct extent_buffer *eb);
int btrfs_cross_ref_exist(struct btrfs_root *root,
			  u64 objectid, u64 offset, u64 bytenr, bool strict,
			  struct btrfs_path *path);
struct extent_buffer *btrfs_alloc_tree_block(struct btrfs_trans_handle *trans,
					     struct btrfs_root *root,
					     u64 parent, u64 root_objectid,
					     const struct btrfs_disk_key *key,
					     int level, u64 hint,
					     u64 empty_size,
					     enum btrfs_lock_nesting nest);
void btrfs_free_tree_block(struct btrfs_trans_handle *trans,
			   u64 root_id,
			   struct extent_buffer *buf,
			   u64 parent, int last_ref);
int btrfs_alloc_reserved_file_extent(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root, u64 owner,
				     u64 offset, u64 ram_bytes,
				     struct btrfs_key *ins);
int btrfs_alloc_logged_file_extent(struct btrfs_trans_handle *trans,
				   u64 root_objectid, u64 owner, u64 offset,
				   struct btrfs_key *ins);
int btrfs_reserve_extent(struct btrfs_root *root, u64 ram_bytes, u64 num_bytes,
			 u64 min_alloc_size, u64 empty_size, u64 hint_byte,
			 struct btrfs_key *ins, int is_data, int delalloc);
int btrfs_inc_ref(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		  struct extent_buffer *buf, int full_backref);
int btrfs_dec_ref(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		  struct extent_buffer *buf, int full_backref);
int btrfs_set_disk_extent_flags(struct btrfs_trans_handle *trans,
				struct extent_buffer *eb, u64 flags, int level);
int btrfs_free_extent(struct btrfs_trans_handle *trans, struct btrfs_ref *ref);

int btrfs_free_reserved_extent(struct btrfs_fs_info *fs_info,
			       u64 start, u64 len, int delalloc);
int btrfs_pin_reserved_extent(struct btrfs_trans_handle *trans, u64 start, u64 len);
int btrfs_finish_extent_commit(struct btrfs_trans_handle *trans);
int btrfs_inc_extent_ref(struct btrfs_trans_handle *trans, struct btrfs_ref *generic_ref);
int __must_check btrfs_drop_snapshot(struct btrfs_root *root, int update_ref,
				     int for_reloc);
int btrfs_drop_subtree(struct btrfs_trans_handle *trans,
			struct btrfs_root *root,
			struct extent_buffer *node,
			struct extent_buffer *parent);

#endif
