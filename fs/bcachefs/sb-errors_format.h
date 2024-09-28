/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_SB_ERRORS_FORMAT_H
#define _BCACHEFS_SB_ERRORS_FORMAT_H

enum bch_fsck_flags {
	FSCK_CAN_FIX		= 1 << 0,
	FSCK_CAN_IGNORE		= 1 << 1,
	FSCK_NEED_FSCK		= 1 << 2,
	FSCK_NO_RATELIMIT	= 1 << 3,
	FSCK_AUTOFIX		= 1 << 4,
};

#define BCH_SB_ERRS()									\
	x(clean_but_journal_not_empty,				  0,	0)		\
	x(dirty_but_no_journal_entries,				  1,	0)		\
	x(dirty_but_no_journal_entries_post_drop_nonflushes,	  2,	0)		\
	x(sb_clean_journal_seq_mismatch,			  3,	0)		\
	x(sb_clean_btree_root_mismatch,				  4,	0)		\
	x(sb_clean_missing,					  5,	0)		\
	x(jset_unsupported_version,				  6,	0)		\
	x(jset_unknown_csum,					  7,	0)		\
	x(jset_last_seq_newer_than_seq,				  8,	0)		\
	x(jset_past_bucket_end,					  9,	0)		\
	x(jset_seq_blacklisted,					 10,	0)		\
	x(journal_entries_missing,				 11,	0)		\
	x(journal_entry_replicas_not_marked,			 12,	FSCK_AUTOFIX)	\
	x(journal_entry_past_jset_end,				 13,	0)		\
	x(journal_entry_replicas_data_mismatch,			 14,	0)		\
	x(journal_entry_bkey_u64s_0,				 15,	0)		\
	x(journal_entry_bkey_past_end,				 16,	0)		\
	x(journal_entry_bkey_bad_format,			 17,	0)		\
	x(journal_entry_bkey_invalid,				 18,	0)		\
	x(journal_entry_btree_root_bad_size,			 19,	0)		\
	x(journal_entry_blacklist_bad_size,			 20,	0)		\
	x(journal_entry_blacklist_v2_bad_size,			 21,	0)		\
	x(journal_entry_blacklist_v2_start_past_end,		 22,	0)		\
	x(journal_entry_usage_bad_size,				 23,	0)		\
	x(journal_entry_data_usage_bad_size,			 24,	0)		\
	x(journal_entry_clock_bad_size,				 25,	0)		\
	x(journal_entry_clock_bad_rw,				 26,	0)		\
	x(journal_entry_dev_usage_bad_size,			 27,	0)		\
	x(journal_entry_dev_usage_bad_dev,			 28,	0)		\
	x(journal_entry_dev_usage_bad_pad,			 29,	0)		\
	x(btree_node_unreadable,				 30,	0)		\
	x(btree_node_fault_injected,				 31,	0)		\
	x(btree_node_bad_magic,					 32,	0)		\
	x(btree_node_bad_seq,					 33,	0)		\
	x(btree_node_unsupported_version,			 34,	0)		\
	x(btree_node_bset_older_than_sb_min,			 35,	0)		\
	x(btree_node_bset_newer_than_sb,			 36,	0)		\
	x(btree_node_data_missing,				 37,	0)		\
	x(btree_node_bset_after_end,				 38,	0)		\
	x(btree_node_replicas_sectors_written_mismatch,		 39,	0)		\
	x(btree_node_replicas_data_mismatch,			 40,	0)		\
	x(bset_unknown_csum,					 41,	0)		\
	x(bset_bad_csum,					 42,	0)		\
	x(bset_past_end_of_btree_node,				 43,	0)		\
	x(bset_wrong_sector_offset,				 44,	0)		\
	x(bset_empty,						 45,	0)		\
	x(bset_bad_seq,						 46,	0)		\
	x(bset_blacklisted_journal_seq,				 47,	0)		\
	x(first_bset_blacklisted_journal_seq,			 48,	0)		\
	x(btree_node_bad_btree,					 49,	0)		\
	x(btree_node_bad_level,					 50,	0)		\
	x(btree_node_bad_min_key,				 51,	0)		\
	x(btree_node_bad_max_key,				 52,	0)		\
	x(btree_node_bad_format,				 53,	0)		\
	x(btree_node_bkey_past_bset_end,			 54,	0)		\
	x(btree_node_bkey_bad_format,				 55,	0)		\
	x(btree_node_bad_bkey,					 56,	0)		\
	x(btree_node_bkey_out_of_order,				 57,	0)		\
	x(btree_root_bkey_invalid,				 58,	0)		\
	x(btree_root_read_error,				 59,	0)		\
	x(btree_root_bad_min_key,				 60,	0)		\
	x(btree_root_bad_max_key,				 61,	0)		\
	x(btree_node_read_error,				 62,	0)		\
	x(btree_node_topology_bad_min_key,			 63,	0)		\
	x(btree_node_topology_bad_max_key,			 64,	0)		\
	x(btree_node_topology_overwritten_by_prev_node,		 65,	0)		\
	x(btree_node_topology_overwritten_by_next_node,		 66,	0)		\
	x(btree_node_topology_interior_node_empty,		 67,	0)		\
	x(fs_usage_hidden_wrong,				 68,	FSCK_AUTOFIX)	\
	x(fs_usage_btree_wrong,					 69,	FSCK_AUTOFIX)	\
	x(fs_usage_data_wrong,					 70,	FSCK_AUTOFIX)	\
	x(fs_usage_cached_wrong,				 71,	FSCK_AUTOFIX)	\
	x(fs_usage_reserved_wrong,				 72,	FSCK_AUTOFIX)	\
	x(fs_usage_persistent_reserved_wrong,			 73,	FSCK_AUTOFIX)	\
	x(fs_usage_nr_inodes_wrong,				 74,	FSCK_AUTOFIX)	\
	x(fs_usage_replicas_wrong,				 75,	FSCK_AUTOFIX)	\
	x(dev_usage_buckets_wrong,				 76,	FSCK_AUTOFIX)	\
	x(dev_usage_sectors_wrong,				 77,	FSCK_AUTOFIX)	\
	x(dev_usage_fragmented_wrong,				 78,	FSCK_AUTOFIX)	\
	x(dev_usage_buckets_ec_wrong,				 79,	FSCK_AUTOFIX)	\
	x(bkey_version_in_future,				 80,	0)		\
	x(bkey_u64s_too_small,					 81,	0)		\
	x(bkey_invalid_type_for_btree,				 82,	0)		\
	x(bkey_extent_size_zero,				 83,	0)		\
	x(bkey_extent_size_greater_than_offset,			 84,	0)		\
	x(bkey_size_nonzero,					 85,	0)		\
	x(bkey_snapshot_nonzero,				 86,	0)		\
	x(bkey_snapshot_zero,					 87,	0)		\
	x(bkey_at_pos_max,					 88,	0)		\
	x(bkey_before_start_of_btree_node,			 89,	0)		\
	x(bkey_after_end_of_btree_node,				 90,	0)		\
	x(bkey_val_size_nonzero,				 91,	0)		\
	x(bkey_val_size_too_small,				 92,	0)		\
	x(alloc_v1_val_size_bad,				 93,	0)		\
	x(alloc_v2_unpack_error,				 94,	0)		\
	x(alloc_v3_unpack_error,				 95,	0)		\
	x(alloc_v4_val_size_bad,				 96,	0)		\
	x(alloc_v4_backpointers_start_bad,			 97,	0)		\
	x(alloc_key_data_type_bad,				 98,	0)		\
	x(alloc_key_empty_but_have_data,			 99,	0)		\
	x(alloc_key_dirty_sectors_0,				100,	0)		\
	x(alloc_key_data_type_inconsistency,			101,	0)		\
	x(alloc_key_to_missing_dev_bucket,			102,	0)		\
	x(alloc_key_cached_inconsistency,			103,	0)		\
	x(alloc_key_cached_but_read_time_zero,			104,	FSCK_AUTOFIX)	\
	x(alloc_key_to_missing_lru_entry,			105,	FSCK_AUTOFIX)	\
	x(alloc_key_data_type_wrong,				106,	FSCK_AUTOFIX)	\
	x(alloc_key_gen_wrong,					107,	FSCK_AUTOFIX)	\
	x(alloc_key_dirty_sectors_wrong,			108,	FSCK_AUTOFIX)	\
	x(alloc_key_cached_sectors_wrong,			109,	FSCK_AUTOFIX)	\
	x(alloc_key_stripe_wrong,				110,	FSCK_AUTOFIX)	\
	x(alloc_key_stripe_redundancy_wrong,			111,	FSCK_AUTOFIX)	\
	x(bucket_sector_count_overflow,				112,	0)		\
	x(bucket_metadata_type_mismatch,			113,	0)		\
	x(need_discard_key_wrong,				114,	0)		\
	x(freespace_key_wrong,					115,	0)		\
	x(freespace_hole_missing,				116,	0)		\
	x(bucket_gens_val_size_bad,				117,	0)		\
	x(bucket_gens_key_wrong,				118,	FSCK_AUTOFIX)	\
	x(bucket_gens_hole_wrong,				119,	FSCK_AUTOFIX)	\
	x(bucket_gens_to_invalid_dev,				120,	FSCK_AUTOFIX)	\
	x(bucket_gens_to_invalid_buckets,			121,	FSCK_AUTOFIX)	\
	x(bucket_gens_nonzero_for_invalid_buckets,		122,	FSCK_AUTOFIX)	\
	x(need_discard_freespace_key_to_invalid_dev_bucket,	123,	0)		\
	x(need_discard_freespace_key_bad,			124,	0)		\
	x(backpointer_bucket_offset_wrong,			125,	0)		\
	x(backpointer_to_missing_device,			126,	0)		\
	x(backpointer_to_missing_alloc,				127,	0)		\
	x(backpointer_to_missing_ptr,				128,	0)		\
	x(lru_entry_at_time_0,					129,	FSCK_AUTOFIX)	\
	x(lru_entry_to_invalid_bucket,				130,	FSCK_AUTOFIX)	\
	x(lru_entry_bad,					131,	FSCK_AUTOFIX)	\
	x(btree_ptr_val_too_big,				132,	0)		\
	x(btree_ptr_v2_val_too_big,				133,	0)		\
	x(btree_ptr_has_non_ptr,				134,	0)		\
	x(extent_ptrs_invalid_entry,				135,	0)		\
	x(extent_ptrs_no_ptrs,					136,	0)		\
	x(extent_ptrs_too_many_ptrs,				137,	0)		\
	x(extent_ptrs_redundant_crc,				138,	0)		\
	x(extent_ptrs_redundant_stripe,				139,	0)		\
	x(extent_ptrs_unwritten,				140,	0)		\
	x(extent_ptrs_written_and_unwritten,			141,	0)		\
	x(ptr_to_invalid_device,				142,	0)		\
	x(ptr_to_duplicate_device,				143,	0)		\
	x(ptr_after_last_bucket,				144,	0)		\
	x(ptr_before_first_bucket,				145,	0)		\
	x(ptr_spans_multiple_buckets,				146,	0)		\
	x(ptr_to_missing_backpointer,				147,	FSCK_AUTOFIX)	\
	x(ptr_to_missing_alloc_key,				148,	FSCK_AUTOFIX)	\
	x(ptr_to_missing_replicas_entry,			149,	FSCK_AUTOFIX)	\
	x(ptr_to_missing_stripe,				150,	0)		\
	x(ptr_to_incorrect_stripe,				151,	0)		\
	x(ptr_gen_newer_than_bucket_gen,			152,	0)		\
	x(ptr_too_stale,					153,	0)		\
	x(stale_dirty_ptr,					154,	0)		\
	x(ptr_bucket_data_type_mismatch,			155,	0)		\
	x(ptr_cached_and_erasure_coded,				156,	0)		\
	x(ptr_crc_uncompressed_size_too_small,			157,	0)		\
	x(ptr_crc_csum_type_unknown,				158,	0)		\
	x(ptr_crc_compression_type_unknown,			159,	0)		\
	x(ptr_crc_redundant,					160,	0)		\
	x(ptr_crc_uncompressed_size_too_big,			161,	0)		\
	x(ptr_crc_nonce_mismatch,				162,	0)		\
	x(ptr_stripe_redundant,					163,	0)		\
	x(reservation_key_nr_replicas_invalid,			164,	0)		\
	x(reflink_v_refcount_wrong,				165,	0)		\
	x(reflink_p_to_missing_reflink_v,			166,	0)		\
	x(stripe_pos_bad,					167,	0)		\
	x(stripe_val_size_bad,					168,	0)		\
	x(stripe_sector_count_wrong,				169,	0)		\
	x(snapshot_tree_pos_bad,				170,	0)		\
	x(snapshot_tree_to_missing_snapshot,			171,	0)		\
	x(snapshot_tree_to_missing_subvol,			172,	0)		\
	x(snapshot_tree_to_wrong_subvol,			173,	0)		\
	x(snapshot_tree_to_snapshot_subvol,			174,	0)		\
	x(snapshot_pos_bad,					175,	0)		\
	x(snapshot_parent_bad,					176,	0)		\
	x(snapshot_children_not_normalized,			177,	0)		\
	x(snapshot_child_duplicate,				178,	0)		\
	x(snapshot_child_bad,					179,	0)		\
	x(snapshot_skiplist_not_normalized,			180,	0)		\
	x(snapshot_skiplist_bad,				181,	0)		\
	x(snapshot_should_not_have_subvol,			182,	0)		\
	x(snapshot_to_bad_snapshot_tree,			183,	FSCK_AUTOFIX)	\
	x(snapshot_bad_depth,					184,	0)		\
	x(snapshot_bad_skiplist,				185,	0)		\
	x(subvol_pos_bad,					186,	0)		\
	x(subvol_not_master_and_not_snapshot,			187,	0)		\
	x(subvol_to_missing_root,				188,	0)		\
	x(subvol_root_wrong_bi_subvol,				189,	0)		\
	x(bkey_in_missing_snapshot,				190,	0)		\
	x(inode_pos_inode_nonzero,				191,	0)		\
	x(inode_pos_blockdev_range,				192,	0)		\
	x(inode_unpack_error,					193,	0)		\
	x(inode_str_hash_invalid,				194,	0)		\
	x(inode_v3_fields_start_bad,				195,	0)		\
	x(inode_snapshot_mismatch,				196,	0)		\
	x(inode_unlinked_but_clean,				197,	0)		\
	x(inode_unlinked_but_nlink_nonzero,			198,	0)		\
	x(inode_unlinked_and_not_open,				281,	0)		\
	x(inode_checksum_type_invalid,				199,	0)		\
	x(inode_compression_type_invalid,			200,	0)		\
	x(inode_subvol_root_but_not_dir,			201,	0)		\
	x(inode_i_size_dirty_but_clean,				202,	FSCK_AUTOFIX)	\
	x(inode_i_sectors_dirty_but_clean,			203,	FSCK_AUTOFIX)	\
	x(inode_i_sectors_wrong,				204,	FSCK_AUTOFIX)	\
	x(inode_dir_wrong_nlink,				205,	FSCK_AUTOFIX)	\
	x(inode_dir_multiple_links,				206,	FSCK_AUTOFIX)	\
	x(inode_dir_missing_backpointer,			284,	FSCK_AUTOFIX)	\
	x(inode_multiple_links_but_nlink_0,			207,	FSCK_AUTOFIX)	\
	x(inode_wrong_backpointer,				208,	FSCK_AUTOFIX)	\
	x(inode_wrong_nlink,					209,	FSCK_AUTOFIX)	\
	x(inode_unreachable,					210,	FSCK_AUTOFIX)	\
	x(deleted_inode_but_clean,				211,	FSCK_AUTOFIX)	\
	x(deleted_inode_missing,				212,	FSCK_AUTOFIX)	\
	x(deleted_inode_is_dir,					213,	FSCK_AUTOFIX)	\
	x(deleted_inode_not_unlinked,				214,	FSCK_AUTOFIX)	\
	x(extent_overlapping,					215,	0)		\
	x(key_in_missing_inode,					216,	0)		\
	x(key_in_wrong_inode_type,				217,	0)		\
	x(extent_past_end_of_inode,				218,	0)		\
	x(dirent_empty_name,					219,	0)		\
	x(dirent_val_too_big,					220,	0)		\
	x(dirent_name_too_long,					221,	0)		\
	x(dirent_name_embedded_nul,				222,	0)		\
	x(dirent_name_dot_or_dotdot,				223,	0)		\
	x(dirent_name_has_slash,				224,	0)		\
	x(dirent_d_type_wrong,					225,	0)		\
	x(inode_bi_parent_wrong,				226,	0)		\
	x(dirent_in_missing_dir_inode,				227,	0)		\
	x(dirent_in_non_dir_inode,				228,	0)		\
	x(dirent_to_missing_inode,				229,	0)		\
	x(dirent_to_missing_subvol,				230,	0)		\
	x(dirent_to_itself,					231,	0)		\
	x(quota_type_invalid,					232,	0)		\
	x(xattr_val_size_too_small,				233,	0)		\
	x(xattr_val_size_too_big,				234,	0)		\
	x(xattr_invalid_type,					235,	0)		\
	x(xattr_name_invalid_chars,				236,	0)		\
	x(xattr_in_missing_inode,				237,	0)		\
	x(root_subvol_missing,					238,	0)		\
	x(root_dir_missing,					239,	0)		\
	x(root_inode_not_dir,					240,	0)		\
	x(dir_loop,						241,	0)		\
	x(hash_table_key_duplicate,				242,	0)		\
	x(hash_table_key_wrong_offset,				243,	0)		\
	x(unlinked_inode_not_on_deleted_list,			244,	FSCK_AUTOFIX)	\
	x(reflink_p_front_pad_bad,				245,	0)		\
	x(journal_entry_dup_same_device,			246,	0)		\
	x(inode_bi_subvol_missing,				247,	0)		\
	x(inode_bi_subvol_wrong,				248,	0)		\
	x(inode_points_to_missing_dirent,			249,	0)		\
	x(inode_points_to_wrong_dirent,				250,	0)		\
	x(inode_bi_parent_nonzero,				251,	0)		\
	x(dirent_to_missing_parent_subvol,			252,	0)		\
	x(dirent_not_visible_in_parent_subvol,			253,	0)		\
	x(subvol_fs_path_parent_wrong,				254,	0)		\
	x(subvol_root_fs_path_parent_nonzero,			255,	0)		\
	x(subvol_children_not_set,				256,	0)		\
	x(subvol_children_bad,					257,	0)		\
	x(subvol_loop,						258,	0)		\
	x(subvol_unreachable,					259,	FSCK_AUTOFIX)	\
	x(btree_node_bkey_bad_u64s,				260,	0)		\
	x(btree_node_topology_empty_interior_node,		261,	0)		\
	x(btree_ptr_v2_min_key_bad,				262,	0)		\
	x(btree_root_unreadable_and_scan_found_nothing,		263,	0)		\
	x(snapshot_node_missing,				264,	0)		\
	x(dup_backpointer_to_bad_csum_extent,			265,	0)		\
	x(btree_bitmap_not_marked,				266,	0)		\
	x(sb_clean_entry_overrun,				267,	0)		\
	x(btree_ptr_v2_written_0,				268,	0)		\
	x(subvol_snapshot_bad,					269,	0)		\
	x(subvol_inode_bad,					270,	0)		\
	x(alloc_key_stripe_sectors_wrong,			271,	FSCK_AUTOFIX)	\
	x(accounting_mismatch,					272,	FSCK_AUTOFIX)	\
	x(accounting_replicas_not_marked,			273,	0)		\
	x(invalid_btree_id,					274,	0)		\
	x(alloc_key_io_time_bad,				275,	0)		\
	x(alloc_key_fragmentation_lru_wrong,			276,	FSCK_AUTOFIX)	\
	x(accounting_key_junk_at_end,				277,	FSCK_AUTOFIX)	\
	x(accounting_key_replicas_nr_devs_0,			278,	FSCK_AUTOFIX)	\
	x(accounting_key_replicas_nr_required_bad,		279,	FSCK_AUTOFIX)	\
	x(accounting_key_replicas_devs_unsorted,		280,	FSCK_AUTOFIX)	\
	x(accounting_key_version_0,				282,	FSCK_AUTOFIX)	\
	x(logged_op_but_clean,					283,	FSCK_AUTOFIX)	\
	x(MAX,							285,	0)

enum bch_sb_error_id {
#define x(t, n, ...) BCH_FSCK_ERR_##t = n,
	BCH_SB_ERRS()
#undef x
};

struct bch_sb_field_errors {
	struct bch_sb_field	field;
	struct bch_sb_field_error_entry {
		__le64		v;
		__le64		last_error_time;
	}			entries[];
};

LE64_BITMASK(BCH_SB_ERROR_ENTRY_ID,	struct bch_sb_field_error_entry, v,  0, 16);
LE64_BITMASK(BCH_SB_ERROR_ENTRY_NR,	struct bch_sb_field_error_entry, v, 16, 64);

#endif /* _BCACHEFS_SB_ERRORS_FORMAT_H */
