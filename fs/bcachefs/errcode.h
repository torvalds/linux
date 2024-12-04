/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_ERRCODE_H
#define _BCACHEFS_ERRCODE_H

#define BCH_ERRCODES()								\
	x(ERANGE,			ERANGE_option_too_small)		\
	x(ERANGE,			ERANGE_option_too_big)			\
	x(EINVAL,			mount_option)				\
	x(BCH_ERR_mount_option,		option_name)				\
	x(BCH_ERR_mount_option,		option_value)				\
	x(BCH_ERR_mount_option,         option_not_bool)                        \
	x(ENOMEM,			ENOMEM_stripe_buf)			\
	x(ENOMEM,			ENOMEM_replicas_table)			\
	x(ENOMEM,			ENOMEM_cpu_replicas)			\
	x(ENOMEM,			ENOMEM_replicas_gc)			\
	x(ENOMEM,			ENOMEM_disk_groups_validate)		\
	x(ENOMEM,			ENOMEM_disk_groups_to_cpu)		\
	x(ENOMEM,			ENOMEM_mark_snapshot)			\
	x(ENOMEM,			ENOMEM_mark_stripe)			\
	x(ENOMEM,			ENOMEM_mark_stripe_ptr)			\
	x(ENOMEM,			ENOMEM_btree_key_cache_create)		\
	x(ENOMEM,			ENOMEM_btree_key_cache_fill)		\
	x(ENOMEM,			ENOMEM_btree_key_cache_insert)		\
	x(ENOMEM,			ENOMEM_trans_kmalloc)			\
	x(ENOMEM,			ENOMEM_trans_log_msg)			\
	x(ENOMEM,			ENOMEM_do_encrypt)			\
	x(ENOMEM,			ENOMEM_ec_read_extent)			\
	x(ENOMEM,			ENOMEM_ec_stripe_mem_alloc)		\
	x(ENOMEM,			ENOMEM_ec_new_stripe_alloc)		\
	x(ENOMEM,			ENOMEM_fs_btree_cache_init)		\
	x(ENOMEM,			ENOMEM_fs_btree_key_cache_init)		\
	x(ENOMEM,			ENOMEM_fs_counters_init)		\
	x(ENOMEM,			ENOMEM_fs_btree_write_buffer_init)	\
	x(ENOMEM,			ENOMEM_io_clock_init)			\
	x(ENOMEM,			ENOMEM_blacklist_table_init)		\
	x(ENOMEM,			ENOMEM_sb_realloc_injected)		\
	x(ENOMEM,			ENOMEM_sb_bio_realloc)			\
	x(ENOMEM,			ENOMEM_sb_buf_realloc)			\
	x(ENOMEM,			ENOMEM_sb_journal_validate)		\
	x(ENOMEM,			ENOMEM_sb_journal_v2_validate)		\
	x(ENOMEM,			ENOMEM_journal_entry_add)		\
	x(ENOMEM,			ENOMEM_journal_read_buf_realloc)	\
	x(ENOMEM,			ENOMEM_btree_interior_update_worker_init)\
	x(ENOMEM,			ENOMEM_btree_interior_update_pool_init)	\
	x(ENOMEM,			ENOMEM_bio_read_init)			\
	x(ENOMEM,			ENOMEM_bio_read_split_init)		\
	x(ENOMEM,			ENOMEM_bio_write_init)			\
	x(ENOMEM,			ENOMEM_bio_bounce_pages_init)		\
	x(ENOMEM,			ENOMEM_writepage_bioset_init)		\
	x(ENOMEM,			ENOMEM_dio_read_bioset_init)		\
	x(ENOMEM,			ENOMEM_dio_write_bioset_init)		\
	x(ENOMEM,			ENOMEM_nocow_flush_bioset_init)		\
	x(ENOMEM,			ENOMEM_promote_table_init)		\
	x(ENOMEM,			ENOMEM_compression_bounce_read_init)	\
	x(ENOMEM,			ENOMEM_compression_bounce_write_init)	\
	x(ENOMEM,			ENOMEM_compression_workspace_init)	\
	x(EIO,				compression_workspace_not_initialized)	\
	x(ENOMEM,			ENOMEM_bucket_gens)			\
	x(ENOMEM,			ENOMEM_buckets_nouse)			\
	x(ENOMEM,			ENOMEM_usage_init)			\
	x(ENOMEM,			ENOMEM_btree_node_read_all_replicas)	\
	x(ENOMEM,			ENOMEM_btree_node_reclaim)		\
	x(ENOMEM,			ENOMEM_btree_node_mem_alloc)		\
	x(ENOMEM,			ENOMEM_btree_cache_cannibalize_lock)	\
	x(ENOMEM,			ENOMEM_buckets_waiting_for_journal_init)\
	x(ENOMEM,			ENOMEM_buckets_waiting_for_journal_set)	\
	x(ENOMEM,			ENOMEM_set_nr_journal_buckets)		\
	x(ENOMEM,			ENOMEM_dev_journal_init)		\
	x(ENOMEM,			ENOMEM_journal_pin_fifo)		\
	x(ENOMEM,			ENOMEM_journal_buf)			\
	x(ENOMEM,			ENOMEM_gc_start)			\
	x(ENOMEM,			ENOMEM_gc_alloc_start)			\
	x(ENOMEM,			ENOMEM_gc_reflink_start)		\
	x(ENOMEM,			ENOMEM_gc_gens)				\
	x(ENOMEM,			ENOMEM_gc_repair_key)			\
	x(ENOMEM,			ENOMEM_fsck_extent_ends_at)		\
	x(ENOMEM,			ENOMEM_fsck_add_nlink)			\
	x(ENOMEM,			ENOMEM_journal_key_insert)		\
	x(ENOMEM,			ENOMEM_journal_keys_sort)		\
	x(ENOMEM,			ENOMEM_read_superblock_clean)		\
	x(ENOMEM,			ENOMEM_fs_alloc)			\
	x(ENOMEM,			ENOMEM_fs_name_alloc)			\
	x(ENOMEM,			ENOMEM_fs_other_alloc)			\
	x(ENOMEM,			ENOMEM_dev_alloc)			\
	x(ENOMEM,			ENOMEM_disk_accounting)			\
	x(ENOMEM,			ENOMEM_stripe_head_alloc)		\
	x(ENOMEM,                       ENOMEM_journal_read_bucket)             \
	x(ENOSPC,			ENOSPC_disk_reservation)		\
	x(ENOSPC,			ENOSPC_bucket_alloc)			\
	x(ENOSPC,			ENOSPC_disk_label_add)			\
	x(ENOSPC,			ENOSPC_stripe_create)			\
	x(ENOSPC,			ENOSPC_inode_create)			\
	x(ENOSPC,			ENOSPC_str_hash_create)			\
	x(ENOSPC,			ENOSPC_snapshot_create)			\
	x(ENOSPC,			ENOSPC_subvolume_create)		\
	x(ENOSPC,			ENOSPC_sb)				\
	x(ENOSPC,			ENOSPC_sb_journal)			\
	x(ENOSPC,			ENOSPC_sb_journal_seq_blacklist)	\
	x(ENOSPC,			ENOSPC_sb_quota)			\
	x(ENOSPC,			ENOSPC_sb_replicas)			\
	x(ENOSPC,			ENOSPC_sb_members)			\
	x(ENOSPC,			ENOSPC_sb_members_v2)			\
	x(ENOSPC,			ENOSPC_sb_crypt)			\
	x(ENOSPC,			ENOSPC_sb_downgrade)			\
	x(ENOSPC,			ENOSPC_btree_slot)			\
	x(ENOSPC,			ENOSPC_snapshot_tree)			\
	x(ENOENT,			ENOENT_bkey_type_mismatch)		\
	x(ENOENT,			ENOENT_str_hash_lookup)			\
	x(ENOENT,			ENOENT_str_hash_set_must_replace)	\
	x(ENOENT,			ENOENT_inode)				\
	x(ENOENT,			ENOENT_not_subvol)			\
	x(ENOENT,			ENOENT_not_directory)			\
	x(ENOENT,			ENOENT_directory_dead)			\
	x(ENOENT,			ENOENT_subvolume)			\
	x(ENOENT,			ENOENT_snapshot_tree)			\
	x(ENOENT,			ENOENT_dirent_doesnt_match_inode)	\
	x(ENOENT,			ENOENT_dev_not_found)			\
	x(ENOENT,			ENOENT_dev_idx_not_found)		\
	x(ENOENT,			ENOENT_inode_no_backpointer)		\
	x(ENOTEMPTY,			ENOTEMPTY_dir_not_empty)		\
	x(ENOTEMPTY,			ENOTEMPTY_subvol_not_empty)		\
	x(EEXIST,			EEXIST_str_hash_set)			\
	x(EEXIST,			EEXIST_discard_in_flight_add)		\
	x(EEXIST,			EEXIST_subvolume_create)		\
	x(ENOSPC,			open_buckets_empty)			\
	x(ENOSPC,			freelist_empty)				\
	x(BCH_ERR_freelist_empty,	no_buckets_found)			\
	x(0,				transaction_restart)			\
	x(BCH_ERR_transaction_restart,	transaction_restart_fault_inject)	\
	x(BCH_ERR_transaction_restart,	transaction_restart_relock)		\
	x(BCH_ERR_transaction_restart,	transaction_restart_relock_path)	\
	x(BCH_ERR_transaction_restart,	transaction_restart_relock_path_intent)	\
	x(BCH_ERR_transaction_restart,	transaction_restart_relock_after_fill)	\
	x(BCH_ERR_transaction_restart,	transaction_restart_too_many_iters)	\
	x(BCH_ERR_transaction_restart,	transaction_restart_lock_node_reused)	\
	x(BCH_ERR_transaction_restart,	transaction_restart_fill_relock)	\
	x(BCH_ERR_transaction_restart,	transaction_restart_fill_mem_alloc_fail)\
	x(BCH_ERR_transaction_restart,	transaction_restart_mem_realloced)	\
	x(BCH_ERR_transaction_restart,	transaction_restart_in_traverse_all)	\
	x(BCH_ERR_transaction_restart,	transaction_restart_would_deadlock)	\
	x(BCH_ERR_transaction_restart,	transaction_restart_would_deadlock_write)\
	x(BCH_ERR_transaction_restart,	transaction_restart_deadlock_recursion_limit)\
	x(BCH_ERR_transaction_restart,	transaction_restart_upgrade)		\
	x(BCH_ERR_transaction_restart,	transaction_restart_key_cache_upgrade)	\
	x(BCH_ERR_transaction_restart,	transaction_restart_key_cache_fill)	\
	x(BCH_ERR_transaction_restart,	transaction_restart_key_cache_raced)	\
	x(BCH_ERR_transaction_restart,	transaction_restart_key_cache_realloced)\
	x(BCH_ERR_transaction_restart,	transaction_restart_journal_preres_get)	\
	x(BCH_ERR_transaction_restart,	transaction_restart_split_race)		\
	x(BCH_ERR_transaction_restart,	transaction_restart_write_buffer_flush)	\
	x(BCH_ERR_transaction_restart,	transaction_restart_nested)		\
	x(BCH_ERR_transaction_restart,	transaction_restart_commit)		\
	x(0,				no_btree_node)				\
	x(BCH_ERR_no_btree_node,	no_btree_node_relock)			\
	x(BCH_ERR_no_btree_node,	no_btree_node_upgrade)			\
	x(BCH_ERR_no_btree_node,	no_btree_node_drop)			\
	x(BCH_ERR_no_btree_node,	no_btree_node_lock_root)		\
	x(BCH_ERR_no_btree_node,	no_btree_node_up)			\
	x(BCH_ERR_no_btree_node,	no_btree_node_down)			\
	x(BCH_ERR_no_btree_node,	no_btree_node_init)			\
	x(BCH_ERR_no_btree_node,	no_btree_node_cached)			\
	x(BCH_ERR_no_btree_node,	no_btree_node_srcu_reset)		\
	x(0,				btree_insert_fail)			\
	x(BCH_ERR_btree_insert_fail,	btree_insert_btree_node_full)		\
	x(BCH_ERR_btree_insert_fail,	btree_insert_need_mark_replicas)	\
	x(BCH_ERR_btree_insert_fail,	btree_insert_need_journal_res)		\
	x(BCH_ERR_btree_insert_fail,	btree_insert_need_journal_reclaim)	\
	x(0,				backpointer_to_overwritten_btree_node)	\
	x(0,				journal_reclaim_would_deadlock)		\
	x(EINVAL,			fsck)					\
	x(BCH_ERR_fsck,			fsck_fix)				\
	x(BCH_ERR_fsck,			fsck_delete_bkey)			\
	x(BCH_ERR_fsck,			fsck_ignore)				\
	x(BCH_ERR_fsck,			fsck_errors_not_fixed)			\
	x(BCH_ERR_fsck,			fsck_repair_unimplemented)		\
	x(BCH_ERR_fsck,			fsck_repair_impossible)			\
	x(EINVAL,			restart_recovery)			\
	x(EINVAL,			not_in_recovery)			\
	x(EINVAL,			cannot_rewind_recovery)			\
	x(0,				data_update_done)			\
	x(EINVAL,			device_state_not_allowed)		\
	x(EINVAL,			member_info_missing)			\
	x(EINVAL,			mismatched_block_size)			\
	x(EINVAL,			block_size_too_small)			\
	x(EINVAL,			bucket_size_too_small)			\
	x(EINVAL,			device_size_too_small)			\
	x(EINVAL,			device_size_too_big)			\
	x(EINVAL,			device_not_a_member_of_filesystem)	\
	x(EINVAL,			device_has_been_removed)		\
	x(EINVAL,			device_splitbrain)			\
	x(EINVAL,			device_already_online)			\
	x(EINVAL,			insufficient_devices_to_start)		\
	x(EINVAL,			invalid)				\
	x(EINVAL,			internal_fsck_err)			\
	x(EINVAL,			opt_parse_error)			\
	x(EINVAL,			remove_with_metadata_missing_unimplemented)\
	x(EINVAL,			remove_would_lose_data)			\
	x(EROFS,			erofs_trans_commit)			\
	x(EROFS,			erofs_no_writes)			\
	x(EROFS,			erofs_journal_err)			\
	x(EROFS,			erofs_sb_err)				\
	x(EROFS,			erofs_unfixed_errors)			\
	x(EROFS,			erofs_norecovery)			\
	x(EROFS,			erofs_nochanges)			\
	x(EROFS,			insufficient_devices)			\
	x(0,				operation_blocked)			\
	x(BCH_ERR_operation_blocked,	btree_cache_cannibalize_lock_blocked)	\
	x(BCH_ERR_operation_blocked,	journal_res_get_blocked)		\
	x(BCH_ERR_operation_blocked,	journal_preres_get_blocked)		\
	x(BCH_ERR_operation_blocked,	bucket_alloc_blocked)			\
	x(BCH_ERR_operation_blocked,	stripe_alloc_blocked)			\
	x(BCH_ERR_invalid,		invalid_sb)				\
	x(BCH_ERR_invalid_sb,		invalid_sb_magic)			\
	x(BCH_ERR_invalid_sb,		invalid_sb_version)			\
	x(BCH_ERR_invalid_sb,		invalid_sb_features)			\
	x(BCH_ERR_invalid_sb,		invalid_sb_too_big)			\
	x(BCH_ERR_invalid_sb,		invalid_sb_csum_type)			\
	x(BCH_ERR_invalid_sb,		invalid_sb_csum)			\
	x(BCH_ERR_invalid_sb,		invalid_sb_block_size)			\
	x(BCH_ERR_invalid_sb,		invalid_sb_uuid)			\
	x(BCH_ERR_invalid_sb,		invalid_sb_too_many_members)		\
	x(BCH_ERR_invalid_sb,		invalid_sb_dev_idx)			\
	x(BCH_ERR_invalid_sb,		invalid_sb_time_precision)		\
	x(BCH_ERR_invalid_sb,		invalid_sb_field_size)			\
	x(BCH_ERR_invalid_sb,		invalid_sb_layout)			\
	x(BCH_ERR_invalid_sb_layout,	invalid_sb_layout_type)			\
	x(BCH_ERR_invalid_sb_layout,	invalid_sb_layout_nr_superblocks)	\
	x(BCH_ERR_invalid_sb_layout,	invalid_sb_layout_superblocks_overlap)	\
	x(BCH_ERR_invalid_sb_layout,    invalid_sb_layout_sb_max_size_bits)     \
	x(BCH_ERR_invalid_sb,		invalid_sb_members_missing)		\
	x(BCH_ERR_invalid_sb,		invalid_sb_members)			\
	x(BCH_ERR_invalid_sb,		invalid_sb_disk_groups)			\
	x(BCH_ERR_invalid_sb,		invalid_sb_replicas)			\
	x(BCH_ERR_invalid_sb,		invalid_replicas_entry)			\
	x(BCH_ERR_invalid_sb,		invalid_sb_journal)			\
	x(BCH_ERR_invalid_sb,		invalid_sb_journal_seq_blacklist)	\
	x(BCH_ERR_invalid_sb,		invalid_sb_crypt)			\
	x(BCH_ERR_invalid_sb,		invalid_sb_clean)			\
	x(BCH_ERR_invalid_sb,		invalid_sb_quota)			\
	x(BCH_ERR_invalid_sb,		invalid_sb_errors)			\
	x(BCH_ERR_invalid_sb,		invalid_sb_opt_compression)		\
	x(BCH_ERR_invalid_sb,		invalid_sb_ext)				\
	x(BCH_ERR_invalid_sb,		invalid_sb_downgrade)			\
	x(BCH_ERR_invalid,		invalid_bkey)				\
	x(BCH_ERR_operation_blocked,    nocow_lock_blocked)			\
	x(EIO,				journal_shutdown)			\
	x(EIO,				journal_flush_err)			\
	x(EIO,				btree_node_read_err)			\
	x(BCH_ERR_btree_node_read_err,	btree_node_read_err_cached)		\
	x(EIO,				sb_not_downgraded)			\
	x(EIO,				btree_node_write_all_failed)		\
	x(EIO,				btree_node_read_error)			\
	x(EIO,				btree_node_read_validate_error)		\
	x(EIO,				btree_need_topology_repair)		\
	x(EIO,				bucket_ref_update)			\
	x(EIO,				trigger_pointer)			\
	x(EIO,				trigger_stripe_pointer)			\
	x(EIO,				metadata_bucket_inconsistency)		\
	x(EIO,				mark_stripe)				\
	x(EIO,				stripe_reconstruct)			\
	x(EIO,				key_type_error)				\
	x(EIO,				no_device_to_read_from)			\
	x(EIO,				missing_indirect_extent)		\
	x(EIO,				invalidate_stripe_to_dev)		\
	x(EIO,				no_encryption_key)			\
	x(EIO,				insufficient_journal_devices)		\
	x(BCH_ERR_btree_node_read_err,	btree_node_read_err_fixable)		\
	x(BCH_ERR_btree_node_read_err,	btree_node_read_err_want_retry)		\
	x(BCH_ERR_btree_node_read_err,	btree_node_read_err_must_retry)		\
	x(BCH_ERR_btree_node_read_err,	btree_node_read_err_bad_node)		\
	x(BCH_ERR_btree_node_read_err,	btree_node_read_err_incompatible)	\
	x(0,				nopromote)				\
	x(BCH_ERR_nopromote,		nopromote_may_not)			\
	x(BCH_ERR_nopromote,		nopromote_already_promoted)		\
	x(BCH_ERR_nopromote,		nopromote_unwritten)			\
	x(BCH_ERR_nopromote,		nopromote_congested)			\
	x(BCH_ERR_nopromote,		nopromote_in_flight)			\
	x(BCH_ERR_nopromote,		nopromote_no_writes)			\
	x(BCH_ERR_nopromote,		nopromote_enomem)			\
	x(0,				invalid_snapshot_node)			\
	x(0,				option_needs_open_fs)			\
	x(0,				remove_disk_accounting_entry)

enum bch_errcode {
	BCH_ERR_START		= 2048,
#define x(class, err) BCH_ERR_##err,
	BCH_ERRCODES()
#undef x
	BCH_ERR_MAX
};

const char *bch2_err_str(int);
bool __bch2_err_matches(int, int);

static inline bool _bch2_err_matches(int err, int class)
{
	return err < 0 && __bch2_err_matches(err, class);
}

#define bch2_err_matches(_err, _class)			\
({							\
	BUILD_BUG_ON(!__builtin_constant_p(_class));	\
	unlikely(_bch2_err_matches(_err, _class));	\
})

int __bch2_err_class(int);

static inline long bch2_err_class(long err)
{
	return err < 0 ? __bch2_err_class(err) : err;
}

#define BLK_STS_REMOVED		((__force blk_status_t)128)

const char *bch2_blk_status_to_str(blk_status_t);

#endif /* _BCACHFES_ERRCODE_H */
