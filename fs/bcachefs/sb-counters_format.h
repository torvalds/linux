/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_SB_COUNTERS_FORMAT_H
#define _BCACHEFS_SB_COUNTERS_FORMAT_H

#define BCH_PERSISTENT_COUNTERS()				\
	x(io_read,					0)	\
	x(io_write,					1)	\
	x(io_move,					2)	\
	x(bucket_invalidate,				3)	\
	x(bucket_discard,				4)	\
	x(bucket_alloc,					5)	\
	x(bucket_alloc_fail,				6)	\
	x(btree_cache_scan,				7)	\
	x(btree_cache_reap,				8)	\
	x(btree_cache_cannibalize,			9)	\
	x(btree_cache_cannibalize_lock,			10)	\
	x(btree_cache_cannibalize_lock_fail,		11)	\
	x(btree_cache_cannibalize_unlock,		12)	\
	x(btree_node_write,				13)	\
	x(btree_node_read,				14)	\
	x(btree_node_compact,				15)	\
	x(btree_node_merge,				16)	\
	x(btree_node_split,				17)	\
	x(btree_node_rewrite,				18)	\
	x(btree_node_alloc,				19)	\
	x(btree_node_free,				20)	\
	x(btree_node_set_root,				21)	\
	x(btree_path_relock_fail,			22)	\
	x(btree_path_upgrade_fail,			23)	\
	x(btree_reserve_get_fail,			24)	\
	x(journal_entry_full,				25)	\
	x(journal_full,					26)	\
	x(journal_reclaim_finish,			27)	\
	x(journal_reclaim_start,			28)	\
	x(journal_write,				29)	\
	x(read_promote,					30)	\
	x(read_bounce,					31)	\
	x(read_split,					33)	\
	x(read_retry,					32)	\
	x(read_reuse_race,				34)	\
	x(move_extent_read,				35)	\
	x(move_extent_write,				36)	\
	x(move_extent_finish,				37)	\
	x(move_extent_fail,				38)	\
	x(move_extent_start_fail,			39)	\
	x(copygc,					40)	\
	x(copygc_wait,					41)	\
	x(gc_gens_end,					42)	\
	x(gc_gens_start,				43)	\
	x(trans_blocked_journal_reclaim,		44)	\
	x(trans_restart_btree_node_reused,		45)	\
	x(trans_restart_btree_node_split,		46)	\
	x(trans_restart_fault_inject,			47)	\
	x(trans_restart_iter_upgrade,			48)	\
	x(trans_restart_journal_preres_get,		49)	\
	x(trans_restart_journal_reclaim,		50)	\
	x(trans_restart_journal_res_get,		51)	\
	x(trans_restart_key_cache_key_realloced,	52)	\
	x(trans_restart_key_cache_raced,		53)	\
	x(trans_restart_mark_replicas,			54)	\
	x(trans_restart_mem_realloced,			55)	\
	x(trans_restart_memory_allocation_failure,	56)	\
	x(trans_restart_relock,				57)	\
	x(trans_restart_relock_after_fill,		58)	\
	x(trans_restart_relock_key_cache_fill,		59)	\
	x(trans_restart_relock_next_node,		60)	\
	x(trans_restart_relock_parent_for_fill,		61)	\
	x(trans_restart_relock_path,			62)	\
	x(trans_restart_relock_path_intent,		63)	\
	x(trans_restart_too_many_iters,			64)	\
	x(trans_restart_traverse,			65)	\
	x(trans_restart_upgrade,			66)	\
	x(trans_restart_would_deadlock,			67)	\
	x(trans_restart_would_deadlock_write,		68)	\
	x(trans_restart_injected,			69)	\
	x(trans_restart_key_cache_upgrade,		70)	\
	x(trans_traverse_all,				71)	\
	x(transaction_commit,				72)	\
	x(write_super,					73)	\
	x(trans_restart_would_deadlock_recursion_limit,	74)	\
	x(trans_restart_write_buffer_flush,		75)	\
	x(trans_restart_split_race,			76)	\
	x(write_buffer_flush_slowpath,			77)	\
	x(write_buffer_flush_sync,			78)

enum bch_persistent_counters {
#define x(t, n, ...) BCH_COUNTER_##t,
	BCH_PERSISTENT_COUNTERS()
#undef x
	BCH_COUNTER_NR
};

struct bch_sb_field_counters {
	struct bch_sb_field	field;
	__le64			d[];
};

#endif /* _BCACHEFS_SB_COUNTERS_FORMAT_H */
