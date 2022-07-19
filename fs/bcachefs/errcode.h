/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_ERRCODE_H
#define _BCACHEFS_ERRCODE_H

#define BCH_ERRCODES()							\
	x(0,			open_buckets_empty)			\
	x(0,			freelist_empty)				\
	x(freelist_empty,	no_buckets_found)			\
	x(0,			insufficient_devices)			\
	x(0,			transaction_restart)			\
	x(transaction_restart,	transaction_restart_fault_inject)	\
	x(transaction_restart,	transaction_restart_relock)		\
	x(transaction_restart,	transaction_restart_relock_path)	\
	x(transaction_restart,	transaction_restart_relock_path_intent)	\
	x(transaction_restart,	transaction_restart_relock_after_fill)	\
	x(transaction_restart,	transaction_restart_too_many_iters)	\
	x(transaction_restart,	transaction_restart_lock_node_reused)	\
	x(transaction_restart,	transaction_restart_fill_relock)	\
	x(transaction_restart,	transaction_restart_fill_mem_alloc_fail)\
	x(transaction_restart,	transaction_restart_mem_realloced)	\
	x(transaction_restart,	transaction_restart_in_traverse_all)	\
	x(transaction_restart,	transaction_restart_would_deadlock)	\
	x(transaction_restart,	transaction_restart_would_deadlock_write)\
	x(transaction_restart,	transaction_restart_upgrade)		\
	x(transaction_restart,	transaction_restart_key_cache_fill)	\
	x(transaction_restart,	transaction_restart_key_cache_raced)	\
	x(transaction_restart,	transaction_restart_key_cache_realloced)\
	x(transaction_restart,	transaction_restart_journal_preres_get)	\
	x(transaction_restart,	transaction_restart_nested)		\
	x(0,			lock_fail_node_reused)			\
	x(0,			lock_fail_root_changed)			\
	x(0,			journal_reclaim_would_deadlock)		\
	x(0,			fsck)					\
	x(fsck,			fsck_fix)				\
	x(fsck,			fsck_ignore)				\
	x(fsck,			fsck_errors_not_fixed)			\
	x(fsck,			fsck_repair_unimplemented)		\
	x(fsck,			fsck_repair_impossible)			\
	x(0,			need_snapshot_cleanup)			\
	x(0,			need_topology_repair)

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

#endif /* _BCACHFES_ERRCODE_H */
