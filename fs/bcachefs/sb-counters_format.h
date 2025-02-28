/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_SB_COUNTERS_FORMAT_H
#define _BCACHEFS_SB_COUNTERS_FORMAT_H

enum counters_flags {
	TYPE_COUNTER	= BIT(0),	/* event counters */
	TYPE_SECTORS	= BIT(1),	/* amount counters, the unit is sectors */
};

#define BCH_PERSISTENT_COUNTERS()					\
	x(io_read,					0,	TYPE_SECTORS)	\
	x(io_write,					1,	TYPE_SECTORS)	\
	x(io_move,					2,	TYPE_SECTORS)	\
	x(bucket_invalidate,				3,	TYPE_COUNTER)	\
	x(bucket_discard,				4,	TYPE_COUNTER)	\
	x(bucket_alloc,					5,	TYPE_COUNTER)	\
	x(bucket_alloc_fail,				6,	TYPE_COUNTER)	\
	x(btree_cache_scan,				7,	TYPE_COUNTER)	\
	x(btree_cache_reap,				8,	TYPE_COUNTER)	\
	x(btree_cache_cannibalize,			9,	TYPE_COUNTER)	\
	x(btree_cache_cannibalize_lock,			10,	TYPE_COUNTER)	\
	x(btree_cache_cannibalize_lock_fail,		11,	TYPE_COUNTER)	\
	x(btree_cache_cannibalize_unlock,		12,	TYPE_COUNTER)	\
	x(btree_node_write,				13,	TYPE_COUNTER)	\
	x(btree_node_read,				14,	TYPE_COUNTER)	\
	x(btree_node_compact,				15,	TYPE_COUNTER)	\
	x(btree_node_merge,				16,	TYPE_COUNTER)	\
	x(btree_node_split,				17,	TYPE_COUNTER)	\
	x(btree_node_rewrite,				18,	TYPE_COUNTER)	\
	x(btree_node_alloc,				19,	TYPE_COUNTER)	\
	x(btree_node_free,				20,	TYPE_COUNTER)	\
	x(btree_node_set_root,				21,	TYPE_COUNTER)	\
	x(btree_path_relock_fail,			22,	TYPE_COUNTER)	\
	x(btree_path_upgrade_fail,			23,	TYPE_COUNTER)	\
	x(btree_reserve_get_fail,			24,	TYPE_COUNTER)	\
	x(journal_entry_full,				25,	TYPE_COUNTER)	\
	x(journal_full,					26,	TYPE_COUNTER)	\
	x(journal_reclaim_finish,			27,	TYPE_COUNTER)	\
	x(journal_reclaim_start,			28,	TYPE_COUNTER)	\
	x(journal_write,				29,	TYPE_COUNTER)	\
	x(read_promote,					30,	TYPE_COUNTER)	\
	x(read_bounce,					31,	TYPE_COUNTER)	\
	x(read_split,					33,	TYPE_COUNTER)	\
	x(read_retry,					32,	TYPE_COUNTER)	\
	x(read_reuse_race,				34,	TYPE_COUNTER)	\
	x(move_extent_read,				35,	TYPE_SECTORS)	\
	x(move_extent_write,				36,	TYPE_SECTORS)	\
	x(move_extent_finish,				37,	TYPE_SECTORS)	\
	x(move_extent_fail,				38,	TYPE_COUNTER)	\
	x(move_extent_start_fail,			39,	TYPE_COUNTER)	\
	x(copygc,					40,	TYPE_COUNTER)	\
	x(copygc_wait,					41,	TYPE_COUNTER)	\
	x(gc_gens_end,					42,	TYPE_COUNTER)	\
	x(gc_gens_start,				43,	TYPE_COUNTER)	\
	x(trans_blocked_journal_reclaim,		44,	TYPE_COUNTER)	\
	x(trans_restart_btree_node_reused,		45,	TYPE_COUNTER)	\
	x(trans_restart_btree_node_split,		46,	TYPE_COUNTER)	\
	x(trans_restart_fault_inject,			47,	TYPE_COUNTER)	\
	x(trans_restart_iter_upgrade,			48,	TYPE_COUNTER)	\
	x(trans_restart_journal_preres_get,		49,	TYPE_COUNTER)	\
	x(trans_restart_journal_reclaim,		50,	TYPE_COUNTER)	\
	x(trans_restart_journal_res_get,		51,	TYPE_COUNTER)	\
	x(trans_restart_key_cache_key_realloced,	52,	TYPE_COUNTER)	\
	x(trans_restart_key_cache_raced,		53,	TYPE_COUNTER)	\
	x(trans_restart_mark_replicas,			54,	TYPE_COUNTER)	\
	x(trans_restart_mem_realloced,			55,	TYPE_COUNTER)	\
	x(trans_restart_memory_allocation_failure,	56,	TYPE_COUNTER)	\
	x(trans_restart_relock,				57,	TYPE_COUNTER)	\
	x(trans_restart_relock_after_fill,		58,	TYPE_COUNTER)	\
	x(trans_restart_relock_key_cache_fill,		59,	TYPE_COUNTER)	\
	x(trans_restart_relock_next_node,		60,	TYPE_COUNTER)	\
	x(trans_restart_relock_parent_for_fill,		61,	TYPE_COUNTER)	\
	x(trans_restart_relock_path,			62,	TYPE_COUNTER)	\
	x(trans_restart_relock_path_intent,		63,	TYPE_COUNTER)	\
	x(trans_restart_too_many_iters,			64,	TYPE_COUNTER)	\
	x(trans_restart_traverse,			65,	TYPE_COUNTER)	\
	x(trans_restart_upgrade,			66,	TYPE_COUNTER)	\
	x(trans_restart_would_deadlock,			67,	TYPE_COUNTER)	\
	x(trans_restart_would_deadlock_write,		68,	TYPE_COUNTER)	\
	x(trans_restart_injected,			69,	TYPE_COUNTER)	\
	x(trans_restart_key_cache_upgrade,		70,	TYPE_COUNTER)	\
	x(trans_traverse_all,				71,	TYPE_COUNTER)	\
	x(transaction_commit,				72,	TYPE_COUNTER)	\
	x(write_super,					73,	TYPE_COUNTER)	\
	x(trans_restart_would_deadlock_recursion_limit,	74,	TYPE_COUNTER)	\
	x(trans_restart_write_buffer_flush,		75,	TYPE_COUNTER)	\
	x(trans_restart_split_race,			76,	TYPE_COUNTER)	\
	x(write_buffer_flush_slowpath,			77,	TYPE_COUNTER)	\
	x(write_buffer_flush_sync,			78,	TYPE_COUNTER)

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
