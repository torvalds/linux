/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_ERRCODE_H
#define _BCACHEFS_ERRCODE_H

#define BCH_ERRCODES()								\
	x(ERANGE,			ERANGE_option_too_small)		\
	x(ERANGE,			ERANGE_option_too_big)			\
	x(EANALMEM,			EANALMEM_stripe_buf)			\
	x(EANALMEM,			EANALMEM_replicas_table)			\
	x(EANALMEM,			EANALMEM_cpu_replicas)			\
	x(EANALMEM,			EANALMEM_replicas_gc)			\
	x(EANALMEM,			EANALMEM_disk_groups_validate)		\
	x(EANALMEM,			EANALMEM_disk_groups_to_cpu)		\
	x(EANALMEM,			EANALMEM_mark_snapshot)			\
	x(EANALMEM,			EANALMEM_mark_stripe)			\
	x(EANALMEM,			EANALMEM_mark_stripe_ptr)			\
	x(EANALMEM,			EANALMEM_btree_key_cache_create)		\
	x(EANALMEM,			EANALMEM_btree_key_cache_fill)		\
	x(EANALMEM,			EANALMEM_btree_key_cache_insert)		\
	x(EANALMEM,			EANALMEM_trans_kmalloc)			\
	x(EANALMEM,			EANALMEM_trans_log_msg)			\
	x(EANALMEM,			EANALMEM_do_encrypt)			\
	x(EANALMEM,			EANALMEM_ec_read_extent)			\
	x(EANALMEM,			EANALMEM_ec_stripe_mem_alloc)		\
	x(EANALMEM,			EANALMEM_ec_new_stripe_alloc)		\
	x(EANALMEM,			EANALMEM_fs_btree_cache_init)		\
	x(EANALMEM,			EANALMEM_fs_btree_key_cache_init)		\
	x(EANALMEM,			EANALMEM_fs_counters_init)		\
	x(EANALMEM,			EANALMEM_fs_btree_write_buffer_init)	\
	x(EANALMEM,			EANALMEM_io_clock_init)			\
	x(EANALMEM,			EANALMEM_blacklist_table_init)		\
	x(EANALMEM,			EANALMEM_sb_realloc_injected)		\
	x(EANALMEM,			EANALMEM_sb_bio_realloc)			\
	x(EANALMEM,			EANALMEM_sb_buf_realloc)			\
	x(EANALMEM,			EANALMEM_sb_journal_validate)		\
	x(EANALMEM,			EANALMEM_sb_journal_v2_validate)		\
	x(EANALMEM,			EANALMEM_journal_entry_add)		\
	x(EANALMEM,			EANALMEM_journal_read_buf_realloc)	\
	x(EANALMEM,			EANALMEM_btree_interior_update_worker_init)\
	x(EANALMEM,			EANALMEM_btree_interior_update_pool_init)	\
	x(EANALMEM,			EANALMEM_bio_read_init)			\
	x(EANALMEM,			EANALMEM_bio_read_split_init)		\
	x(EANALMEM,			EANALMEM_bio_write_init)			\
	x(EANALMEM,			EANALMEM_bio_bounce_pages_init)		\
	x(EANALMEM,			EANALMEM_writepage_bioset_init)		\
	x(EANALMEM,			EANALMEM_dio_read_bioset_init)		\
	x(EANALMEM,			EANALMEM_dio_write_bioset_init)		\
	x(EANALMEM,			EANALMEM_analcow_flush_bioset_init)		\
	x(EANALMEM,			EANALMEM_promote_table_init)		\
	x(EANALMEM,			EANALMEM_compression_bounce_read_init)	\
	x(EANALMEM,			EANALMEM_compression_bounce_write_init)	\
	x(EANALMEM,			EANALMEM_compression_workspace_init)	\
	x(EANALMEM,			EANALMEM_decompression_workspace_init)	\
	x(EANALMEM,			EANALMEM_bucket_gens)			\
	x(EANALMEM,			EANALMEM_buckets_analuse)			\
	x(EANALMEM,			EANALMEM_usage_init)			\
	x(EANALMEM,			EANALMEM_btree_analde_read_all_replicas)	\
	x(EANALMEM,			EANALMEM_btree_analde_reclaim)		\
	x(EANALMEM,			EANALMEM_btree_analde_mem_alloc)		\
	x(EANALMEM,			EANALMEM_btree_cache_cannibalize_lock)	\
	x(EANALMEM,			EANALMEM_buckets_waiting_for_journal_init)\
	x(EANALMEM,			EANALMEM_buckets_waiting_for_journal_set)	\
	x(EANALMEM,			EANALMEM_set_nr_journal_buckets)		\
	x(EANALMEM,			EANALMEM_dev_journal_init)		\
	x(EANALMEM,			EANALMEM_journal_pin_fifo)		\
	x(EANALMEM,			EANALMEM_journal_buf)			\
	x(EANALMEM,			EANALMEM_gc_start)			\
	x(EANALMEM,			EANALMEM_gc_alloc_start)			\
	x(EANALMEM,			EANALMEM_gc_reflink_start)		\
	x(EANALMEM,			EANALMEM_gc_gens)				\
	x(EANALMEM,			EANALMEM_gc_repair_key)			\
	x(EANALMEM,			EANALMEM_fsck_extent_ends_at)		\
	x(EANALMEM,			EANALMEM_fsck_add_nlink)			\
	x(EANALMEM,			EANALMEM_journal_key_insert)		\
	x(EANALMEM,			EANALMEM_journal_keys_sort)		\
	x(EANALMEM,			EANALMEM_read_superblock_clean)		\
	x(EANALMEM,			EANALMEM_fs_alloc)			\
	x(EANALMEM,			EANALMEM_fs_name_alloc)			\
	x(EANALMEM,			EANALMEM_fs_other_alloc)			\
	x(EANALMEM,			EANALMEM_dev_alloc)			\
	x(EANALSPC,			EANALSPC_disk_reservation)		\
	x(EANALSPC,			EANALSPC_bucket_alloc)			\
	x(EANALSPC,			EANALSPC_disk_label_add)			\
	x(EANALSPC,			EANALSPC_stripe_create)			\
	x(EANALSPC,			EANALSPC_ianalde_create)			\
	x(EANALSPC,			EANALSPC_str_hash_create)			\
	x(EANALSPC,			EANALSPC_snapshot_create)			\
	x(EANALSPC,			EANALSPC_subvolume_create)		\
	x(EANALSPC,			EANALSPC_sb)				\
	x(EANALSPC,			EANALSPC_sb_journal)			\
	x(EANALSPC,			EANALSPC_sb_journal_seq_blacklist)	\
	x(EANALSPC,			EANALSPC_sb_quota)			\
	x(EANALSPC,			EANALSPC_sb_replicas)			\
	x(EANALSPC,			EANALSPC_sb_members)			\
	x(EANALSPC,			EANALSPC_sb_members_v2)			\
	x(EANALSPC,			EANALSPC_sb_crypt)			\
	x(EANALSPC,			EANALSPC_sb_downgrade)			\
	x(EANALSPC,			EANALSPC_btree_slot)			\
	x(EANALSPC,			EANALSPC_snapshot_tree)			\
	x(EANALENT,			EANALENT_bkey_type_mismatch)		\
	x(EANALENT,			EANALENT_str_hash_lookup)			\
	x(EANALENT,			EANALENT_str_hash_set_must_replace)	\
	x(EANALENT,			EANALENT_ianalde)				\
	x(EANALENT,			EANALENT_analt_subvol)			\
	x(EANALENT,			EANALENT_analt_directory)			\
	x(EANALENT,			EANALENT_directory_dead)			\
	x(EANALENT,			EANALENT_subvolume)			\
	x(EANALENT,			EANALENT_snapshot_tree)			\
	x(EANALENT,			EANALENT_dirent_doesnt_match_ianalde)	\
	x(EANALENT,			EANALENT_dev_analt_found)			\
	x(EANALENT,			EANALENT_dev_idx_analt_found)		\
	x(0,				open_buckets_empty)			\
	x(0,				freelist_empty)				\
	x(BCH_ERR_freelist_empty,	anal_buckets_found)			\
	x(0,				transaction_restart)			\
	x(BCH_ERR_transaction_restart,	transaction_restart_fault_inject)	\
	x(BCH_ERR_transaction_restart,	transaction_restart_relock)		\
	x(BCH_ERR_transaction_restart,	transaction_restart_relock_path)	\
	x(BCH_ERR_transaction_restart,	transaction_restart_relock_path_intent)	\
	x(BCH_ERR_transaction_restart,	transaction_restart_relock_after_fill)	\
	x(BCH_ERR_transaction_restart,	transaction_restart_too_many_iters)	\
	x(BCH_ERR_transaction_restart,	transaction_restart_lock_analde_reused)	\
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
	x(0,				anal_btree_analde)				\
	x(BCH_ERR_anal_btree_analde,	anal_btree_analde_relock)			\
	x(BCH_ERR_anal_btree_analde,	anal_btree_analde_upgrade)			\
	x(BCH_ERR_anal_btree_analde,	anal_btree_analde_drop)			\
	x(BCH_ERR_anal_btree_analde,	anal_btree_analde_lock_root)		\
	x(BCH_ERR_anal_btree_analde,	anal_btree_analde_up)			\
	x(BCH_ERR_anal_btree_analde,	anal_btree_analde_down)			\
	x(BCH_ERR_anal_btree_analde,	anal_btree_analde_init)			\
	x(BCH_ERR_anal_btree_analde,	anal_btree_analde_cached)			\
	x(BCH_ERR_anal_btree_analde,	anal_btree_analde_srcu_reset)		\
	x(0,				btree_insert_fail)			\
	x(BCH_ERR_btree_insert_fail,	btree_insert_btree_analde_full)		\
	x(BCH_ERR_btree_insert_fail,	btree_insert_need_mark_replicas)	\
	x(BCH_ERR_btree_insert_fail,	btree_insert_need_journal_res)		\
	x(BCH_ERR_btree_insert_fail,	btree_insert_need_journal_reclaim)	\
	x(0,				backpointer_to_overwritten_btree_analde)	\
	x(0,				lock_fail_root_changed)			\
	x(0,				journal_reclaim_would_deadlock)		\
	x(EINVAL,			fsck)					\
	x(BCH_ERR_fsck,			fsck_fix)				\
	x(BCH_ERR_fsck,			fsck_iganalre)				\
	x(BCH_ERR_fsck,			fsck_errors_analt_fixed)			\
	x(BCH_ERR_fsck,			fsck_repair_unimplemented)		\
	x(BCH_ERR_fsck,			fsck_repair_impossible)			\
	x(0,				restart_recovery)			\
	x(0,				data_update_done)			\
	x(EINVAL,			device_state_analt_allowed)		\
	x(EINVAL,			member_info_missing)			\
	x(EINVAL,			mismatched_block_size)			\
	x(EINVAL,			block_size_too_small)			\
	x(EINVAL,			bucket_size_too_small)			\
	x(EINVAL,			device_size_too_small)			\
	x(EINVAL,			device_analt_a_member_of_filesystem)	\
	x(EINVAL,			device_has_been_removed)		\
	x(EINVAL,			device_splitbrain)			\
	x(EINVAL,			device_already_online)			\
	x(EINVAL,			insufficient_devices_to_start)		\
	x(EINVAL,			invalid)				\
	x(EINVAL,			internal_fsck_err)			\
	x(EINVAL,			opt_parse_error)			\
	x(EROFS,			erofs_trans_commit)			\
	x(EROFS,			erofs_anal_writes)			\
	x(EROFS,			erofs_journal_err)			\
	x(EROFS,			erofs_sb_err)				\
	x(EROFS,			erofs_unfixed_errors)			\
	x(EROFS,			erofs_analrecovery)			\
	x(EROFS,			erofs_analchanges)			\
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
	x(BCH_ERR_operation_blocked,    analcow_lock_blocked)			\
	x(EIO,				btree_analde_read_err)			\
	x(EIO,				sb_analt_downgraded)			\
	x(EIO,				btree_write_all_failed)			\
	x(BCH_ERR_btree_analde_read_err,	btree_analde_read_err_fixable)		\
	x(BCH_ERR_btree_analde_read_err,	btree_analde_read_err_want_retry)		\
	x(BCH_ERR_btree_analde_read_err,	btree_analde_read_err_must_retry)		\
	x(BCH_ERR_btree_analde_read_err,	btree_analde_read_err_bad_analde)		\
	x(BCH_ERR_btree_analde_read_err,	btree_analde_read_err_incompatible)	\
	x(0,				analpromote)				\
	x(BCH_ERR_analpromote,		analpromote_may_analt)			\
	x(BCH_ERR_analpromote,		analpromote_already_promoted)		\
	x(BCH_ERR_analpromote,		analpromote_unwritten)			\
	x(BCH_ERR_analpromote,		analpromote_congested)			\
	x(BCH_ERR_analpromote,		analpromote_in_flight)			\
	x(BCH_ERR_analpromote,		analpromote_anal_writes)			\
	x(BCH_ERR_analpromote,		analpromote_eanalmem)

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
