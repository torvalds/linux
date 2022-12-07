/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_ERRCODE_H
#define _BCACHEFS_ERRCODE_H

#define BCH_ERRCODES()								\
	x(ENOSPC,			ENOSPC_disk_reservation)		\
	x(ENOSPC,			ENOSPC_bucket_alloc)			\
	x(ENOSPC,			ENOSPC_disk_label_add)			\
	x(ENOSPC,			ENOSPC_stripe_create)			\
	x(ENOSPC,			ENOSPC_stripe_reuse)			\
	x(ENOSPC,			ENOSPC_inode_create)			\
	x(ENOSPC,			ENOSPC_str_hash_create)			\
	x(ENOSPC,			ENOSPC_snapshot_create)			\
	x(ENOSPC,			ENOSPC_subvolume_create)		\
	x(ENOSPC,			ENOSPC_sb)				\
	x(ENOSPC,			ENOSPC_sb_journal)			\
	x(ENOSPC,			ENOSPC_sb_quota)			\
	x(ENOSPC,			ENOSPC_sb_replicas)			\
	x(ENOSPC,			ENOSPC_sb_members)			\
	x(0,				open_buckets_empty)			\
	x(0,				freelist_empty)				\
	x(BCH_ERR_freelist_empty,	no_buckets_found)			\
	x(0,				insufficient_devices)			\
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
	x(BCH_ERR_transaction_restart,	transaction_restart_nested)		\
	x(0,				no_btree_node)				\
	x(BCH_ERR_no_btree_node,	no_btree_node_relock)			\
	x(BCH_ERR_no_btree_node,	no_btree_node_upgrade)			\
	x(BCH_ERR_no_btree_node,	no_btree_node_drop)			\
	x(BCH_ERR_no_btree_node,	no_btree_node_lock_root)		\
	x(BCH_ERR_no_btree_node,	no_btree_node_up)			\
	x(BCH_ERR_no_btree_node,	no_btree_node_down)			\
	x(BCH_ERR_no_btree_node,	no_btree_node_init)			\
	x(BCH_ERR_no_btree_node,	no_btree_node_cached)			\
	x(0,				btree_insert_fail)			\
	x(BCH_ERR_btree_insert_fail,	btree_insert_btree_node_full)		\
	x(BCH_ERR_btree_insert_fail,	btree_insert_need_mark_replicas)	\
	x(BCH_ERR_btree_insert_fail,	btree_insert_need_journal_res)		\
	x(BCH_ERR_btree_insert_fail,	btree_insert_need_journal_reclaim)	\
	x(0,				lock_fail_root_changed)			\
	x(0,				journal_reclaim_would_deadlock)		\
	x(0,				fsck)					\
	x(BCH_ERR_fsck,			fsck_fix)				\
	x(BCH_ERR_fsck,			fsck_ignore)				\
	x(BCH_ERR_fsck,			fsck_errors_not_fixed)			\
	x(BCH_ERR_fsck,			fsck_repair_unimplemented)		\
	x(BCH_ERR_fsck,			fsck_repair_impossible)			\
	x(0,				need_snapshot_cleanup)			\
	x(0,				need_topology_repair)

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
	return err && __bch2_err_matches(err, class);
}

#define bch2_err_matches(_err, _class)			\
({							\
	BUILD_BUG_ON(!__builtin_constant_p(_class));	\
	_bch2_err_matches(_err, _class);		\
})

int __bch2_err_class(int);

static inline long bch2_err_class(long err)
{
	return err < 0 ? __bch2_err_class(err) : err;
}

#endif /* _BCACHFES_ERRCODE_H */
