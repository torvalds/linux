#undef TRACE_SYSTEM
#define TRACE_SYSTEM ocfs2

#if !defined(_TRACE_OCFS2_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_OCFS2_H

#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(ocfs2__int,
	TP_PROTO(int num),
	TP_ARGS(num),
	TP_STRUCT__entry(
		__field(int, num)
	),
	TP_fast_assign(
		__entry->num = num;
	),
	TP_printk("%d", __entry->num)
);

#define DEFINE_OCFS2_INT_EVENT(name)	\
DEFINE_EVENT(ocfs2__int, name,	\
	TP_PROTO(int num),	\
	TP_ARGS(num))

DECLARE_EVENT_CLASS(ocfs2__uint,
	TP_PROTO(unsigned int num),
	TP_ARGS(num),
	TP_STRUCT__entry(
		__field(	unsigned int,	num		)
	),
	TP_fast_assign(
		__entry->num	= 	num;
	),
	TP_printk("%u", __entry->num)
);

#define DEFINE_OCFS2_UINT_EVENT(name)	\
DEFINE_EVENT(ocfs2__uint, name,	\
	TP_PROTO(unsigned int num),	\
	TP_ARGS(num))

DECLARE_EVENT_CLASS(ocfs2__ull,
	TP_PROTO(unsigned long long blkno),
	TP_ARGS(blkno),
	TP_STRUCT__entry(
		__field(unsigned long long, blkno)
	),
	TP_fast_assign(
		__entry->blkno = blkno;
	),
	TP_printk("%llu", __entry->blkno)
);

#define DEFINE_OCFS2_ULL_EVENT(name)	\
DEFINE_EVENT(ocfs2__ull, name,	\
	TP_PROTO(unsigned long long num),	\
	TP_ARGS(num))

DECLARE_EVENT_CLASS(ocfs2__int_int,
	TP_PROTO(int value1, int value2),
	TP_ARGS(value1, value2),
	TP_STRUCT__entry(
		__field(int, value1)
		__field(int, value2)
	),
	TP_fast_assign(
		__entry->value1	= value1;
		__entry->value2	= value2;
	),
	TP_printk("%d %d", __entry->value1, __entry->value2)
);

#define DEFINE_OCFS2_INT_INT_EVENT(name)	\
DEFINE_EVENT(ocfs2__int_int, name,	\
	TP_PROTO(int val1, int val2),	\
	TP_ARGS(val1, val2))

DECLARE_EVENT_CLASS(ocfs2__uint_uint,
	TP_PROTO(unsigned int value1, unsigned int value2),
	TP_ARGS(value1, value2),
	TP_STRUCT__entry(
		__field(unsigned int, value1)
		__field(unsigned int, value2)
	),
	TP_fast_assign(
		__entry->value1 = value1;
		__entry->value2 = value2;
	),
	TP_printk("%u %u", __entry->value1, __entry->value2)
);

#define DEFINE_OCFS2_UINT_UINT_EVENT(name)	\
DEFINE_EVENT(ocfs2__uint_uint, name,	\
	TP_PROTO(unsigned int val1, unsigned int val2),	\
	TP_ARGS(val1, val2))

DECLARE_EVENT_CLASS(ocfs2__ull_uint,
	TP_PROTO(unsigned long long value1, unsigned int value2),
	TP_ARGS(value1, value2),
	TP_STRUCT__entry(
		__field(unsigned long long, value1)
		__field(unsigned int, value2)
	),
	TP_fast_assign(
		__entry->value1 = value1;
		__entry->value2 = value2;
	),
	TP_printk("%llu %u", __entry->value1, __entry->value2)
);

#define DEFINE_OCFS2_ULL_UINT_EVENT(name)	\
DEFINE_EVENT(ocfs2__ull_uint, name,	\
	TP_PROTO(unsigned long long val1, unsigned int val2),	\
	TP_ARGS(val1, val2))

DECLARE_EVENT_CLASS(ocfs2__ull_ull,
	TP_PROTO(unsigned long long value1, unsigned long long value2),
	TP_ARGS(value1, value2),
	TP_STRUCT__entry(
		__field(unsigned long long, value1)
		__field(unsigned long long, value2)
	),
	TP_fast_assign(
		__entry->value1 = value1;
		__entry->value2 = value2;
	),
	TP_printk("%llu %llu", __entry->value1, __entry->value2)
);

#define DEFINE_OCFS2_ULL_ULL_EVENT(name)	\
DEFINE_EVENT(ocfs2__ull_ull, name,	\
	TP_PROTO(unsigned long long val1, unsigned long long val2),	\
	TP_ARGS(val1, val2))

DECLARE_EVENT_CLASS(ocfs2__ull_ull_uint,
	TP_PROTO(unsigned long long value1,
		 unsigned long long value2, unsigned int value3),
	TP_ARGS(value1, value2, value3),
	TP_STRUCT__entry(
		__field(unsigned long long, value1)
		__field(unsigned long long, value2)
		__field(unsigned int, value3)
	),
	TP_fast_assign(
		__entry->value1 = value1;
		__entry->value2 = value2;
		__entry->value3 = value3;
	),
	TP_printk("%llu %llu %u",
		  __entry->value1, __entry->value2, __entry->value3)
);

#define DEFINE_OCFS2_ULL_ULL_UINT_EVENT(name)	\
DEFINE_EVENT(ocfs2__ull_ull_uint, name,	\
	TP_PROTO(unsigned long long val1,	\
		 unsigned long long val2, unsigned int val3),	\
	TP_ARGS(val1, val2, val3))

DECLARE_EVENT_CLASS(ocfs2__ull_uint_uint,
	TP_PROTO(unsigned long long value1,
		 unsigned int value2, unsigned int value3),
	TP_ARGS(value1, value2, value3),
	TP_STRUCT__entry(
		__field(unsigned long long, value1)
		__field(unsigned int, value2)
		__field(unsigned int, value3)
	),
	TP_fast_assign(
		__entry->value1 = value1;
		__entry->value2 = value2;
		__entry->value3	= value3;
	),
	TP_printk("%llu %u %u", __entry->value1,
		  __entry->value2, __entry->value3)
);

#define DEFINE_OCFS2_ULL_UINT_UINT_EVENT(name)	\
DEFINE_EVENT(ocfs2__ull_uint_uint, name,	\
	TP_PROTO(unsigned long long val1,	\
		 unsigned int val2, unsigned int val3),	\
	TP_ARGS(val1, val2, val3))

DECLARE_EVENT_CLASS(ocfs2__uint_uint_uint,
	TP_PROTO(unsigned int value1, unsigned int value2,
		 unsigned int value3),
	TP_ARGS(value1, value2, value3),
	TP_STRUCT__entry(
		__field(	unsigned int,	value1		)
		__field(	unsigned int,	value2		)
		__field(	unsigned int,	value3		)
	),
	TP_fast_assign(
		__entry->value1	= 	value1;
		__entry->value2	= 	value2;
		__entry->value3	= 	value3;
	),
	TP_printk("%u %u %u", __entry->value1, __entry->value2, __entry->value3)
);

#define DEFINE_OCFS2_UINT_UINT_UINT_EVENT(name)	\
DEFINE_EVENT(ocfs2__uint_uint_uint, name,	\
	TP_PROTO(unsigned int value1, unsigned int value2,	\
		 unsigned int value3),	\
	TP_ARGS(value1, value2, value3))

DECLARE_EVENT_CLASS(ocfs2__ull_ull_ull,
	TP_PROTO(unsigned long long value1,
		 unsigned long long value2, unsigned long long value3),
	TP_ARGS(value1, value2, value3),
	TP_STRUCT__entry(
		__field(unsigned long long, value1)
		__field(unsigned long long, value2)
		__field(unsigned long long, value3)
	),
	TP_fast_assign(
		__entry->value1 = value1;
		__entry->value2 = value2;
		__entry->value3 = value3;
	),
	TP_printk("%llu %llu %llu",
		  __entry->value1, __entry->value2, __entry->value3)
);

#define DEFINE_OCFS2_ULL_ULL_ULL_EVENT(name)	\
DEFINE_EVENT(ocfs2__ull_ull_ull, name,	\
	TP_PROTO(unsigned long long value1, unsigned long long value2,	\
		 unsigned long long value3),	\
	TP_ARGS(value1, value2, value3))

DECLARE_EVENT_CLASS(ocfs2__ull_int_int_int,
	TP_PROTO(unsigned long long ull, int value1, int value2, int value3),
	TP_ARGS(ull, value1, value2, value3),
	TP_STRUCT__entry(
		__field(	unsigned long long,	ull	)
		__field(	int,	value1			)
		__field(	int,	value2			)
		__field(	int,	value3			)
	),
	TP_fast_assign(
		__entry->ull		= ull;
		__entry->value1		= value1;
		__entry->value2		= value2;
		__entry->value3		= value3;
	),
	TP_printk("%llu %d %d %d",
		  __entry->ull, __entry->value1,
		  __entry->value2, __entry->value3)
);

#define DEFINE_OCFS2_ULL_INT_INT_INT_EVENT(name)	\
DEFINE_EVENT(ocfs2__ull_int_int_int, name,	\
	TP_PROTO(unsigned long long ull, int value1,	\
		 int value2, int value3),	\
	TP_ARGS(ull, value1, value2, value3))

DECLARE_EVENT_CLASS(ocfs2__ull_uint_uint_uint,
	TP_PROTO(unsigned long long ull, unsigned int value1,
		 unsigned int value2, unsigned int value3),
	TP_ARGS(ull, value1, value2, value3),
	TP_STRUCT__entry(
		__field(unsigned long long, ull)
		__field(unsigned int, value1)
		__field(unsigned int, value2)
		__field(unsigned int, value3)
	),
	TP_fast_assign(
		__entry->ull = ull;
		__entry->value1 = value1;
		__entry->value2	= value2;
		__entry->value3	= value3;
	),
	TP_printk("%llu %u %u %u",
		  __entry->ull, __entry->value1,
		  __entry->value2, __entry->value3)
);

#define DEFINE_OCFS2_ULL_UINT_UINT_UINT_EVENT(name)	\
DEFINE_EVENT(ocfs2__ull_uint_uint_uint, name,	\
	TP_PROTO(unsigned long long ull, unsigned int value1,	\
		 unsigned int value2, unsigned int value3),	\
	TP_ARGS(ull, value1, value2, value3))

/* Trace events for fs/ocfs2/alloc.c. */
DECLARE_EVENT_CLASS(ocfs2__btree_ops,
	TP_PROTO(unsigned long long owner,\
		 unsigned int value1, unsigned int value2),
	TP_ARGS(owner, value1, value2),
	TP_STRUCT__entry(
		__field(unsigned long long, owner)
		__field(unsigned int, value1)
		__field(unsigned int, value2)
	),
	TP_fast_assign(
		__entry->owner = owner;
		__entry->value1 = value1;
		__entry->value2	= value2;
	),
	TP_printk("%llu %u %u",
		  __entry->owner, __entry->value1, __entry->value2)
);

#define DEFINE_OCFS2_BTREE_EVENT(name)	\
DEFINE_EVENT(ocfs2__btree_ops, name,	\
	TP_PROTO(unsigned long long owner,	\
		 unsigned int value1, unsigned int value2),	\
	TP_ARGS(owner, value1, value2))

DEFINE_OCFS2_BTREE_EVENT(ocfs2_adjust_rightmost_branch);

DEFINE_OCFS2_BTREE_EVENT(ocfs2_rotate_tree_right);

DEFINE_OCFS2_BTREE_EVENT(ocfs2_append_rec_to_path);

DEFINE_OCFS2_BTREE_EVENT(ocfs2_insert_extent_start);

DEFINE_OCFS2_BTREE_EVENT(ocfs2_add_clusters_in_btree);

DEFINE_OCFS2_INT_EVENT(ocfs2_num_free_extents);

DEFINE_OCFS2_INT_EVENT(ocfs2_complete_edge_insert);

TRACE_EVENT(ocfs2_grow_tree,
	TP_PROTO(unsigned long long owner, int depth),
	TP_ARGS(owner, depth),
	TP_STRUCT__entry(
		__field(unsigned long long, owner)
		__field(int, depth)
	),
	TP_fast_assign(
		__entry->owner = owner;
		__entry->depth = depth;
	),
	TP_printk("%llu %d", __entry->owner, __entry->depth)
);

TRACE_EVENT(ocfs2_rotate_subtree,
	TP_PROTO(int subtree_root, unsigned long long blkno,
		 int depth),
	TP_ARGS(subtree_root, blkno, depth),
	TP_STRUCT__entry(
		__field(int, subtree_root)
		__field(unsigned long long, blkno)
		__field(int, depth)
	),
	TP_fast_assign(
		__entry->subtree_root = subtree_root;
		__entry->blkno = blkno;
		__entry->depth = depth;
	),
	TP_printk("%d %llu %d", __entry->subtree_root,
		  __entry->blkno, __entry->depth)
);

TRACE_EVENT(ocfs2_insert_extent,
	TP_PROTO(unsigned int ins_appending, unsigned int ins_contig,
		 int ins_contig_index, int free_records, int ins_tree_depth),
	TP_ARGS(ins_appending, ins_contig, ins_contig_index, free_records,
		ins_tree_depth),
	TP_STRUCT__entry(
		__field(unsigned int, ins_appending)
		__field(unsigned int, ins_contig)
		__field(int, ins_contig_index)
		__field(int, free_records)
		__field(int, ins_tree_depth)
	),
	TP_fast_assign(
		__entry->ins_appending = ins_appending;
		__entry->ins_contig = ins_contig;
		__entry->ins_contig_index = ins_contig_index;
		__entry->free_records = free_records;
		__entry->ins_tree_depth = ins_tree_depth;
	),
	TP_printk("%u %u %d %d %d",
		  __entry->ins_appending, __entry->ins_contig,
		  __entry->ins_contig_index, __entry->free_records,
		  __entry->ins_tree_depth)
);

TRACE_EVENT(ocfs2_split_extent,
	TP_PROTO(int split_index, unsigned int c_contig_type,
		 unsigned int c_has_empty_extent,
		 unsigned int c_split_covers_rec),
	TP_ARGS(split_index, c_contig_type,
		c_has_empty_extent, c_split_covers_rec),
	TP_STRUCT__entry(
		__field(int, split_index)
		__field(unsigned int, c_contig_type)
		__field(unsigned int, c_has_empty_extent)
		__field(unsigned int, c_split_covers_rec)
	),
	TP_fast_assign(
		__entry->split_index = split_index;
		__entry->c_contig_type = c_contig_type;
		__entry->c_has_empty_extent = c_has_empty_extent;
		__entry->c_split_covers_rec = c_split_covers_rec;
	),
	TP_printk("%d %u %u %u", __entry->split_index, __entry->c_contig_type,
		  __entry->c_has_empty_extent, __entry->c_split_covers_rec)
);

TRACE_EVENT(ocfs2_remove_extent,
	TP_PROTO(unsigned long long owner, unsigned int cpos,
		 unsigned int len, int index,
		 unsigned int e_cpos, unsigned int clusters),
	TP_ARGS(owner, cpos, len, index, e_cpos, clusters),
	TP_STRUCT__entry(
		__field(unsigned long long, owner)
		__field(unsigned int, cpos)
		__field(unsigned int, len)
		__field(int, index)
		__field(unsigned int, e_cpos)
		__field(unsigned int, clusters)
	),
	TP_fast_assign(
		__entry->owner = owner;
		__entry->cpos = cpos;
		__entry->len = len;
		__entry->index = index;
		__entry->e_cpos = e_cpos;
		__entry->clusters = clusters;
	),
	TP_printk("%llu %u %u %d %u %u",
		  __entry->owner, __entry->cpos, __entry->len, __entry->index,
		  __entry->e_cpos, __entry->clusters)
);

TRACE_EVENT(ocfs2_commit_truncate,
	TP_PROTO(unsigned long long ino, unsigned int new_cpos,
		 unsigned int clusters, unsigned int depth),
	TP_ARGS(ino, new_cpos, clusters, depth),
	TP_STRUCT__entry(
		__field(unsigned long long, ino)
		__field(unsigned int, new_cpos)
		__field(unsigned int, clusters)
		__field(unsigned int, depth)
	),
	TP_fast_assign(
		__entry->ino = ino;
		__entry->new_cpos = new_cpos;
		__entry->clusters = clusters;
		__entry->depth = depth;
	),
	TP_printk("%llu %u %u %u",
		  __entry->ino, __entry->new_cpos,
		  __entry->clusters, __entry->depth)
);

TRACE_EVENT(ocfs2_validate_extent_block,
	TP_PROTO(unsigned long long blkno),
	TP_ARGS(blkno),
	TP_STRUCT__entry(
		__field(unsigned long long, blkno)
	),
	TP_fast_assign(
		__entry->blkno = blkno;
	),
	TP_printk("%llu ", __entry->blkno)
);

TRACE_EVENT(ocfs2_rotate_leaf,
	TP_PROTO(unsigned int insert_cpos, int insert_index,
		 int has_empty, int next_free,
		 unsigned int l_count),
	TP_ARGS(insert_cpos, insert_index, has_empty,
		next_free, l_count),
	TP_STRUCT__entry(
		__field(unsigned int, insert_cpos)
		__field(int, insert_index)
		__field(int, has_empty)
		__field(int, next_free)
		__field(unsigned int, l_count)
	),
	TP_fast_assign(
		__entry->insert_cpos = insert_cpos;
		__entry->insert_index = insert_index;
		__entry->has_empty = has_empty;
		__entry->next_free = next_free;
		__entry->l_count = l_count;
	),
	TP_printk("%u %d %d %d %u", __entry->insert_cpos,
		  __entry->insert_index, __entry->has_empty,
		  __entry->next_free, __entry->l_count)
);

TRACE_EVENT(ocfs2_add_clusters_in_btree_ret,
	TP_PROTO(int status, int reason, int err),
	TP_ARGS(status, reason, err),
	TP_STRUCT__entry(
		__field(int, status)
		__field(int, reason)
		__field(int, err)
	),
	TP_fast_assign(
		__entry->status = status;
		__entry->reason = reason;
		__entry->err = err;
	),
	TP_printk("%d %d %d", __entry->status,
		  __entry->reason, __entry->err)
);

TRACE_EVENT(ocfs2_mark_extent_written,
	TP_PROTO(unsigned long long owner, unsigned int cpos,
		 unsigned int len, unsigned int phys),
	TP_ARGS(owner, cpos, len, phys),
	TP_STRUCT__entry(
		__field(unsigned long long, owner)
		__field(unsigned int, cpos)
		__field(unsigned int, len)
		__field(unsigned int, phys)
	),
	TP_fast_assign(
		__entry->owner = owner;
		__entry->cpos = cpos;
		__entry->len = len;
		__entry->phys = phys;
	),
	TP_printk("%llu %u %u %u",
		  __entry->owner, __entry->cpos,
		  __entry->len, __entry->phys)
);

DECLARE_EVENT_CLASS(ocfs2__truncate_log_ops,
	TP_PROTO(unsigned long long blkno, int index,
		 unsigned int start, unsigned int num),
	TP_ARGS(blkno, index, start, num),
	TP_STRUCT__entry(
		__field(unsigned long long, blkno)
		__field(int, index)
		__field(unsigned int, start)
		__field(unsigned int, num)
	),
	TP_fast_assign(
		__entry->blkno = blkno;
		__entry->index = index;
		__entry->start = start;
		__entry->num = num;
	),
	TP_printk("%llu %d %u %u",
		  __entry->blkno, __entry->index,
		  __entry->start, __entry->num)
);

#define DEFINE_OCFS2_TRUNCATE_LOG_OPS_EVENT(name)	\
DEFINE_EVENT(ocfs2__truncate_log_ops, name,	\
	TP_PROTO(unsigned long long blkno, int index,	\
		 unsigned int start, unsigned int num),	\
	TP_ARGS(blkno, index, start, num))

DEFINE_OCFS2_TRUNCATE_LOG_OPS_EVENT(ocfs2_truncate_log_append);

DEFINE_OCFS2_TRUNCATE_LOG_OPS_EVENT(ocfs2_replay_truncate_records);

DEFINE_OCFS2_ULL_UINT_EVENT(ocfs2_flush_truncate_log);

DEFINE_OCFS2_INT_EVENT(ocfs2_begin_truncate_log_recovery);

DEFINE_OCFS2_INT_EVENT(ocfs2_truncate_log_recovery_num);

DEFINE_OCFS2_ULL_UINT_EVENT(ocfs2_complete_truncate_log_recovery);

DEFINE_OCFS2_ULL_UINT_EVENT(ocfs2_free_cached_blocks);

DEFINE_OCFS2_ULL_UINT_EVENT(ocfs2_cache_cluster_dealloc);

DEFINE_OCFS2_INT_INT_EVENT(ocfs2_run_deallocs);

TRACE_EVENT(ocfs2_cache_block_dealloc,
	TP_PROTO(int type, int slot, unsigned long long suballoc,
		 unsigned long long blkno, unsigned int bit),
	TP_ARGS(type, slot, suballoc, blkno, bit),
	TP_STRUCT__entry(
		__field(int, type)
		__field(int, slot)
		__field(unsigned long long, suballoc)
		__field(unsigned long long, blkno)
		__field(unsigned int, bit)
	),
	TP_fast_assign(
		__entry->type = type;
		__entry->slot = slot;
		__entry->suballoc = suballoc;
		__entry->blkno = blkno;
		__entry->bit = bit;
	),
	TP_printk("%d %d %llu %llu %u",
		  __entry->type, __entry->slot, __entry->suballoc,
		  __entry->blkno, __entry->bit)
);

/* End of trace events for fs/ocfs2/alloc.c. */

/* Trace events for fs/ocfs2/localalloc.c. */

DEFINE_OCFS2_UINT_UINT_UINT_EVENT(ocfs2_la_set_sizes);

DEFINE_OCFS2_ULL_INT_INT_INT_EVENT(ocfs2_alloc_should_use_local);

DEFINE_OCFS2_INT_EVENT(ocfs2_load_local_alloc);

DEFINE_OCFS2_INT_EVENT(ocfs2_begin_local_alloc_recovery);

DEFINE_OCFS2_ULL_INT_INT_INT_EVENT(ocfs2_reserve_local_alloc_bits);

DEFINE_OCFS2_UINT_EVENT(ocfs2_local_alloc_count_bits);

DEFINE_OCFS2_INT_INT_EVENT(ocfs2_local_alloc_find_clear_bits_search_bitmap);

DEFINE_OCFS2_ULL_INT_INT_INT_EVENT(ocfs2_local_alloc_find_clear_bits);

DEFINE_OCFS2_INT_INT_EVENT(ocfs2_sync_local_to_main);

TRACE_EVENT(ocfs2_sync_local_to_main_free,
	TP_PROTO(int count, int bit, unsigned long long start_blk,
		 unsigned long long blkno),
	TP_ARGS(count, bit, start_blk, blkno),
	TP_STRUCT__entry(
		__field(int, count)
		__field(int, bit)
		__field(unsigned long long, start_blk)
		__field(unsigned long long, blkno)
	),
	TP_fast_assign(
		__entry->count = count;
		__entry->bit = bit;
		__entry->start_blk = start_blk;
		__entry->blkno = blkno;
	),
	TP_printk("%d %d %llu %llu",
		  __entry->count, __entry->bit, __entry->start_blk,
		  __entry->blkno)
);

DEFINE_OCFS2_INT_INT_EVENT(ocfs2_local_alloc_new_window);

DEFINE_OCFS2_ULL_UINT_EVENT(ocfs2_local_alloc_new_window_result);

/* End of trace events for fs/ocfs2/localalloc.c. */

/* Trace events for fs/ocfs2/resize.c. */

DEFINE_OCFS2_UINT_UINT_EVENT(ocfs2_update_last_group_and_inode);

DEFINE_OCFS2_ULL_UINT_EVENT(ocfs2_group_extend);

DEFINE_OCFS2_ULL_UINT_UINT_UINT_EVENT(ocfs2_group_add);

/* End of trace events for fs/ocfs2/resize.c. */

/* Trace events for fs/ocfs2/suballoc.c. */

DEFINE_OCFS2_ULL_EVENT(ocfs2_validate_group_descriptor);

DEFINE_OCFS2_ULL_UINT_EVENT(ocfs2_block_group_alloc_contig);

DEFINE_OCFS2_ULL_UINT_EVENT(ocfs2_block_group_alloc_discontig);

DEFINE_OCFS2_ULL_EVENT(ocfs2_block_group_alloc);

DEFINE_OCFS2_UINT_UINT_EVENT(ocfs2_reserve_suballoc_bits_nospc);

DEFINE_OCFS2_UINT_UINT_UINT_EVENT(ocfs2_reserve_suballoc_bits_no_new_group);

DEFINE_OCFS2_ULL_EVENT(ocfs2_reserve_new_inode_new_group);

DEFINE_OCFS2_UINT_UINT_EVENT(ocfs2_block_group_set_bits);

TRACE_EVENT(ocfs2_relink_block_group,
	TP_PROTO(unsigned long long i_blkno, unsigned int chain,
		 unsigned long long bg_blkno,
		 unsigned long long prev_blkno),
	TP_ARGS(i_blkno, chain, bg_blkno, prev_blkno),
	TP_STRUCT__entry(
		__field(unsigned long long, i_blkno)
		__field(unsigned int, chain)
		__field(unsigned long long, bg_blkno)
		__field(unsigned long long, prev_blkno)
	),
	TP_fast_assign(
		__entry->i_blkno = i_blkno;
		__entry->chain = chain;
		__entry->bg_blkno = bg_blkno;
		__entry->prev_blkno = prev_blkno;
	),
	TP_printk("%llu %u %llu %llu",
		  __entry->i_blkno, __entry->chain, __entry->bg_blkno,
		  __entry->prev_blkno)
);

DEFINE_OCFS2_ULL_UINT_UINT_UINT_EVENT(ocfs2_cluster_group_search_wrong_max_bits);

DEFINE_OCFS2_ULL_ULL_EVENT(ocfs2_cluster_group_search_max_block);

DEFINE_OCFS2_ULL_ULL_EVENT(ocfs2_block_group_search_max_block);

DEFINE_OCFS2_ULL_UINT_UINT_EVENT(ocfs2_search_chain_begin);

DEFINE_OCFS2_ULL_UINT_EVENT(ocfs2_search_chain_succ);

DEFINE_OCFS2_ULL_UINT_EVENT(ocfs2_search_chain_end);

DEFINE_OCFS2_UINT_EVENT(ocfs2_claim_suballoc_bits);

DEFINE_OCFS2_ULL_UINT_EVENT(ocfs2_claim_new_inode_at_loc);

DEFINE_OCFS2_UINT_UINT_EVENT(ocfs2_block_group_clear_bits);

TRACE_EVENT(ocfs2_free_suballoc_bits,
	TP_PROTO(unsigned long long inode, unsigned long long group,
		 unsigned int start_bit, unsigned int count),
	TP_ARGS(inode, group, start_bit, count),
	TP_STRUCT__entry(
		__field(unsigned long long, inode)
		__field(unsigned long long, group)
		__field(unsigned int, start_bit)
		__field(unsigned int, count)
	),
	TP_fast_assign(
		__entry->inode = inode;
		__entry->group = group;
		__entry->start_bit = start_bit;
		__entry->count = count;
	),
	TP_printk("%llu %llu %u %u", __entry->inode, __entry->group,
		  __entry->start_bit, __entry->count)
);

TRACE_EVENT(ocfs2_free_clusters,
	TP_PROTO(unsigned long long bg_blkno, unsigned long long start_blk,
		 unsigned int start_bit, unsigned int count),
	TP_ARGS(bg_blkno, start_blk, start_bit, count),
	TP_STRUCT__entry(
		__field(unsigned long long, bg_blkno)
		__field(unsigned long long, start_blk)
		__field(unsigned int, start_bit)
		__field(unsigned int, count)
	),
	TP_fast_assign(
		__entry->bg_blkno = bg_blkno;
		__entry->start_blk = start_blk;
		__entry->start_bit = start_bit;
		__entry->count = count;
	),
	TP_printk("%llu %llu %u %u", __entry->bg_blkno, __entry->start_blk,
		  __entry->start_bit, __entry->count)
);

DEFINE_OCFS2_ULL_EVENT(ocfs2_get_suballoc_slot_bit);

DEFINE_OCFS2_ULL_UINT_EVENT(ocfs2_test_suballoc_bit);

DEFINE_OCFS2_ULL_EVENT(ocfs2_test_inode_bit);

/* End of trace events for fs/ocfs2/suballoc.c. */

/* Trace events for fs/ocfs2/refcounttree.c. */

DEFINE_OCFS2_ULL_EVENT(ocfs2_validate_refcount_block);

DEFINE_OCFS2_ULL_EVENT(ocfs2_purge_refcount_trees);

DEFINE_OCFS2_ULL_EVENT(ocfs2_create_refcount_tree);

DEFINE_OCFS2_ULL_EVENT(ocfs2_create_refcount_tree_blkno);

DEFINE_OCFS2_ULL_INT_INT_INT_EVENT(ocfs2_change_refcount_rec);

DEFINE_OCFS2_ULL_UINT_EVENT(ocfs2_expand_inline_ref_root);

DEFINE_OCFS2_ULL_UINT_UINT_EVENT(ocfs2_divide_leaf_refcount_block);

DEFINE_OCFS2_ULL_UINT_EVENT(ocfs2_new_leaf_refcount_block);

DECLARE_EVENT_CLASS(ocfs2__refcount_tree_ops,
	TP_PROTO(unsigned long long blkno, int index,
		 unsigned long long cpos,
		 unsigned int clusters, unsigned int refcount),
	TP_ARGS(blkno, index, cpos, clusters, refcount),
	TP_STRUCT__entry(
		__field(unsigned long long, blkno)
		__field(int, index)
		__field(unsigned long long, cpos)
		__field(unsigned int, clusters)
		__field(unsigned int, refcount)
	),
	TP_fast_assign(
		__entry->blkno = blkno;
		__entry->index = index;
		__entry->cpos = cpos;
		__entry->clusters = clusters;
		__entry->refcount = refcount;
	),
	TP_printk("%llu %d %llu %u %u", __entry->blkno, __entry->index,
		  __entry->cpos, __entry->clusters, __entry->refcount)
);

#define DEFINE_OCFS2_REFCOUNT_TREE_OPS_EVENT(name)	\
DEFINE_EVENT(ocfs2__refcount_tree_ops, name,		\
	TP_PROTO(unsigned long long blkno, int index,	\
		 unsigned long long cpos,		\
		 unsigned int count, unsigned int refcount),	\
	TP_ARGS(blkno, index, cpos, count, refcount))

DEFINE_OCFS2_REFCOUNT_TREE_OPS_EVENT(ocfs2_insert_refcount_rec);

TRACE_EVENT(ocfs2_split_refcount_rec,
	TP_PROTO(unsigned long long cpos,
		 unsigned int clusters, unsigned int refcount,
		 unsigned long long split_cpos,
		 unsigned int split_clusters, unsigned int split_refcount),
	TP_ARGS(cpos, clusters, refcount,
		split_cpos, split_clusters, split_refcount),
	TP_STRUCT__entry(
		__field(unsigned long long, cpos)
		__field(unsigned int, clusters)
		__field(unsigned int, refcount)
		__field(unsigned long long, split_cpos)
		__field(unsigned int, split_clusters)
		__field(unsigned int, split_refcount)
	),
	TP_fast_assign(
		__entry->cpos = cpos;
		__entry->clusters = clusters;
		__entry->refcount = refcount;
		__entry->split_cpos = split_cpos;
		__entry->split_clusters = split_clusters;
		__entry->split_refcount	= split_refcount;
	),
	TP_printk("%llu %u %u %llu %u %u",
		  __entry->cpos, __entry->clusters, __entry->refcount,
		  __entry->split_cpos, __entry->split_clusters,
		  __entry->split_refcount)
);

DEFINE_OCFS2_REFCOUNT_TREE_OPS_EVENT(ocfs2_split_refcount_rec_insert);

DEFINE_OCFS2_ULL_ULL_UINT_EVENT(ocfs2_increase_refcount_begin);

DEFINE_OCFS2_ULL_UINT_UINT_EVENT(ocfs2_increase_refcount_change);

DEFINE_OCFS2_ULL_UINT_EVENT(ocfs2_increase_refcount_insert);

DEFINE_OCFS2_ULL_UINT_UINT_EVENT(ocfs2_increase_refcount_split);

DEFINE_OCFS2_ULL_ULL_UINT_EVENT(ocfs2_remove_refcount_extent);

DEFINE_OCFS2_ULL_EVENT(ocfs2_restore_refcount_block);

DEFINE_OCFS2_ULL_ULL_UINT_EVENT(ocfs2_decrease_refcount_rec);

TRACE_EVENT(ocfs2_decrease_refcount,
	TP_PROTO(unsigned long long owner,
		 unsigned long long cpos,
		 unsigned int len, int delete),
	TP_ARGS(owner, cpos, len, delete),
	TP_STRUCT__entry(
		__field(unsigned long long, owner)
		__field(unsigned long long, cpos)
		__field(unsigned int, len)
		__field(int, delete)
	),
	TP_fast_assign(
		__entry->owner = owner;
		__entry->cpos = cpos;
		__entry->len = len;
		__entry->delete = delete;
	),
	TP_printk("%llu %llu %u %d",
		  __entry->owner, __entry->cpos, __entry->len, __entry->delete)
);

DEFINE_OCFS2_ULL_UINT_UINT_UINT_EVENT(ocfs2_mark_extent_refcounted);

DEFINE_OCFS2_ULL_UINT_UINT_UINT_EVENT(ocfs2_calc_refcount_meta_credits);

TRACE_EVENT(ocfs2_calc_refcount_meta_credits_iterate,
	TP_PROTO(int recs_add, unsigned long long cpos,
		 unsigned int clusters, unsigned long long r_cpos,
		 unsigned int r_clusters, unsigned int refcount, int index),
	TP_ARGS(recs_add, cpos, clusters, r_cpos, r_clusters, refcount, index),
	TP_STRUCT__entry(
		__field(int, recs_add)
		__field(unsigned long long, cpos)
		__field(unsigned int, clusters)
		__field(unsigned long long, r_cpos)
		__field(unsigned int, r_clusters)
		__field(unsigned int, refcount)
		__field(int, index)
	),
	TP_fast_assign(
		__entry->recs_add = recs_add;
		__entry->cpos = cpos;
		__entry->clusters = clusters;
		__entry->r_cpos = r_cpos;
		__entry->r_clusters = r_clusters;
		__entry->refcount = refcount;
		__entry->index = index;
	),
	TP_printk("%d %llu %u %llu %u %u %d",
		  __entry->recs_add, __entry->cpos, __entry->clusters,
		  __entry->r_cpos, __entry->r_clusters,
		  __entry->refcount, __entry->index)
);

DEFINE_OCFS2_INT_INT_EVENT(ocfs2_add_refcount_flag);

DEFINE_OCFS2_INT_INT_EVENT(ocfs2_prepare_refcount_change_for_del);

DEFINE_OCFS2_INT_INT_EVENT(ocfs2_lock_refcount_allocators);

DEFINE_OCFS2_ULL_UINT_UINT_UINT_EVENT(ocfs2_duplicate_clusters_by_page);

DEFINE_OCFS2_ULL_UINT_UINT_UINT_EVENT(ocfs2_duplicate_clusters_by_jbd);

TRACE_EVENT(ocfs2_clear_ext_refcount,
	TP_PROTO(unsigned long long ino, unsigned int cpos,
		 unsigned int len, unsigned int p_cluster,
		 unsigned int ext_flags),
	TP_ARGS(ino, cpos, len, p_cluster, ext_flags),
	TP_STRUCT__entry(
		__field(unsigned long long, ino)
		__field(unsigned int, cpos)
		__field(unsigned int, len)
		__field(unsigned int, p_cluster)
		__field(unsigned int, ext_flags)
	),
	TP_fast_assign(
		__entry->ino = ino;
		__entry->cpos = cpos;
		__entry->len = len;
		__entry->p_cluster = p_cluster;
		__entry->ext_flags = ext_flags;
	),
	TP_printk("%llu %u %u %u %u",
		  __entry->ino, __entry->cpos, __entry->len,
		  __entry->p_cluster, __entry->ext_flags)
);

TRACE_EVENT(ocfs2_replace_clusters,
	TP_PROTO(unsigned long long ino, unsigned int cpos,
		 unsigned int old, unsigned int new, unsigned int len,
		 unsigned int ext_flags),
	TP_ARGS(ino, cpos, old, new, len, ext_flags),
	TP_STRUCT__entry(
		__field(unsigned long long, ino)
		__field(unsigned int, cpos)
		__field(unsigned int, old)
		__field(unsigned int, new)
		__field(unsigned int, len)
		__field(unsigned int, ext_flags)
	),
	TP_fast_assign(
		__entry->ino = ino;
		__entry->cpos = cpos;
		__entry->old = old;
		__entry->new = new;
		__entry->len = len;
		__entry->ext_flags = ext_flags;
	),
	TP_printk("%llu %u %u %u %u %u",
		  __entry->ino, __entry->cpos, __entry->old, __entry->new,
		  __entry->len, __entry->ext_flags)
);

DEFINE_OCFS2_ULL_UINT_UINT_UINT_EVENT(ocfs2_make_clusters_writable);

TRACE_EVENT(ocfs2_refcount_cow_hunk,
	TP_PROTO(unsigned long long ino, unsigned int cpos,
		 unsigned int write_len, unsigned int max_cpos,
		 unsigned int cow_start, unsigned int cow_len),
	TP_ARGS(ino, cpos, write_len, max_cpos, cow_start, cow_len),
	TP_STRUCT__entry(
		__field(unsigned long long, ino)
		__field(unsigned int, cpos)
		__field(unsigned int, write_len)
		__field(unsigned int, max_cpos)
		__field(unsigned int, cow_start)
		__field(unsigned int, cow_len)
	),
	TP_fast_assign(
		__entry->ino = ino;
		__entry->cpos = cpos;
		__entry->write_len = write_len;
		__entry->max_cpos = max_cpos;
		__entry->cow_start = cow_start;
		__entry->cow_len = cow_len;
	),
	TP_printk("%llu %u %u %u %u %u",
		  __entry->ino, __entry->cpos, __entry->write_len,
		  __entry->max_cpos, __entry->cow_start, __entry->cow_len)
);

/* End of trace events for fs/ocfs2/refcounttree.c. */

/* Trace events for fs/ocfs2/aops.c. */

DECLARE_EVENT_CLASS(ocfs2__get_block,
	TP_PROTO(unsigned long long ino, unsigned long long iblock,
		 void *bh_result, int create),
	TP_ARGS(ino, iblock, bh_result, create),
	TP_STRUCT__entry(
		__field(unsigned long long, ino)
		__field(unsigned long long, iblock)
		__field(void *, bh_result)
		__field(int, create)
	),
	TP_fast_assign(
		__entry->ino = ino;
		__entry->iblock = iblock;
		__entry->bh_result = bh_result;
		__entry->create = create;
	),
	TP_printk("%llu %llu %p %d",
		  __entry->ino, __entry->iblock,
		  __entry->bh_result, __entry->create)
);

#define DEFINE_OCFS2_GET_BLOCK_EVENT(name)	\
DEFINE_EVENT(ocfs2__get_block, name,	\
	TP_PROTO(unsigned long long ino, unsigned long long iblock,	\
		 void *bh_result, int create),	\
	TP_ARGS(ino, iblock, bh_result, create))

DEFINE_OCFS2_GET_BLOCK_EVENT(ocfs2_symlink_get_block);

DEFINE_OCFS2_GET_BLOCK_EVENT(ocfs2_get_block);

DEFINE_OCFS2_ULL_ULL_EVENT(ocfs2_get_block_end);

DEFINE_OCFS2_ULL_ULL_EVENT(ocfs2_readpage);

DEFINE_OCFS2_ULL_ULL_EVENT(ocfs2_writepage);

DEFINE_OCFS2_ULL_ULL_EVENT(ocfs2_bmap);

TRACE_EVENT(ocfs2_try_to_write_inline_data,
	TP_PROTO(unsigned long long ino, unsigned int len,
		 unsigned long long pos, unsigned int flags),
	TP_ARGS(ino, len, pos, flags),
	TP_STRUCT__entry(
		__field(unsigned long long, ino)
		__field(unsigned int, len)
		__field(unsigned long long, pos)
		__field(unsigned int, flags)
	),
	TP_fast_assign(
		__entry->ino = ino;
		__entry->len = len;
		__entry->pos = pos;
		__entry->flags = flags;
	),
	TP_printk("%llu %u %llu 0x%x",
		  __entry->ino, __entry->len, __entry->pos, __entry->flags)
);

TRACE_EVENT(ocfs2_write_begin_nolock,
	TP_PROTO(unsigned long long ino,
		 long long i_size, unsigned int i_clusters,
		 unsigned long long pos, unsigned int len,
		 unsigned int flags, void *page,
		 unsigned int clusters, unsigned int extents_to_split),
	TP_ARGS(ino, i_size, i_clusters, pos, len, flags,
		page, clusters, extents_to_split),
	TP_STRUCT__entry(
		__field(unsigned long long, ino)
		__field(long long, i_size)
		__field(unsigned int, i_clusters)
		__field(unsigned long long, pos)
		__field(unsigned int, len)
		__field(unsigned int, flags)
		__field(void *, page)
		__field(unsigned int, clusters)
		__field(unsigned int, extents_to_split)
	),
	TP_fast_assign(
		__entry->ino = ino;
		__entry->i_size = i_size;
		__entry->i_clusters = i_clusters;
		__entry->pos = pos;
		__entry->len = len;
		__entry->flags = flags;
		__entry->page = page;
		__entry->clusters = clusters;
		__entry->extents_to_split = extents_to_split;
	),
	TP_printk("%llu %lld %u %llu %u %u %p %u %u",
		  __entry->ino, __entry->i_size, __entry->i_clusters,
		  __entry->pos, __entry->len,
		  __entry->flags, __entry->page, __entry->clusters,
		  __entry->extents_to_split)
);

TRACE_EVENT(ocfs2_write_end_inline,
	TP_PROTO(unsigned long long ino,
		 unsigned long long pos, unsigned int copied,
		 unsigned int id_count, unsigned int features),
	TP_ARGS(ino, pos, copied, id_count, features),
	TP_STRUCT__entry(
		__field(unsigned long long, ino)
		__field(unsigned long long, pos)
		__field(unsigned int, copied)
		__field(unsigned int, id_count)
		__field(unsigned int, features)
	),
	TP_fast_assign(
		__entry->ino = ino;
		__entry->pos = pos;
		__entry->copied = copied;
		__entry->id_count = id_count;
		__entry->features = features;
	),
	TP_printk("%llu %llu %u %u %u",
		  __entry->ino, __entry->pos, __entry->copied,
		  __entry->id_count, __entry->features)
);

/* End of trace events for fs/ocfs2/aops.c. */

/* Trace events for fs/ocfs2/mmap.c. */

TRACE_EVENT(ocfs2_fault,
	TP_PROTO(unsigned long long ino,
		 void *area, void *page, unsigned long pgoff),
	TP_ARGS(ino, area, page, pgoff),
	TP_STRUCT__entry(
		__field(unsigned long long, ino)
		__field(void *, area)
		__field(void *, page)
		__field(unsigned long, pgoff)
	),
	TP_fast_assign(
		__entry->ino = ino;
		__entry->area = area;
		__entry->page = page;
		__entry->pgoff = pgoff;
	),
	TP_printk("%llu %p %p %lu",
		  __entry->ino, __entry->area, __entry->page, __entry->pgoff)
);

/* End of trace events for fs/ocfs2/mmap.c. */

/* Trace events for fs/ocfs2/file.c. */

DECLARE_EVENT_CLASS(ocfs2__file_ops,
	TP_PROTO(void *inode, void *file, void *dentry,
		 unsigned long long ino,
		 unsigned int d_len, const unsigned char *d_name,
		 unsigned long long para),
	TP_ARGS(inode, file, dentry, ino, d_len, d_name, para),
	TP_STRUCT__entry(
		__field(void *, inode)
		__field(void *, file)
		__field(void *, dentry)
		__field(unsigned long long, ino)
		__field(unsigned int, d_len)
		__string(d_name, d_name)
		__field(unsigned long long, para)
	),
	TP_fast_assign(
		__entry->inode = inode;
		__entry->file = file;
		__entry->dentry = dentry;
		__entry->ino = ino;
		__entry->d_len = d_len;
		__assign_str(d_name, d_name);
		__entry->para = para;
	),
	TP_printk("%p %p %p %llu %llu %.*s", __entry->inode, __entry->file,
		  __entry->dentry, __entry->ino, __entry->para,
		  __entry->d_len, __get_str(d_name))
);

#define DEFINE_OCFS2_FILE_OPS(name)				\
DEFINE_EVENT(ocfs2__file_ops, name,				\
TP_PROTO(void *inode, void *file, void *dentry,			\
	 unsigned long long ino,				\
	 unsigned int d_len, const unsigned char *d_name,	\
	 unsigned long long mode),				\
	TP_ARGS(inode, file, dentry, ino, d_len, d_name, mode))

DEFINE_OCFS2_FILE_OPS(ocfs2_file_open);

DEFINE_OCFS2_FILE_OPS(ocfs2_file_release);

DEFINE_OCFS2_FILE_OPS(ocfs2_sync_file);

DEFINE_OCFS2_FILE_OPS(ocfs2_file_aio_write);

DEFINE_OCFS2_FILE_OPS(ocfs2_file_splice_write);

DEFINE_OCFS2_FILE_OPS(ocfs2_file_splice_read);

DEFINE_OCFS2_FILE_OPS(ocfs2_file_aio_read);

DEFINE_OCFS2_ULL_ULL_ULL_EVENT(ocfs2_truncate_file);

DEFINE_OCFS2_ULL_ULL_EVENT(ocfs2_truncate_file_error);

TRACE_EVENT(ocfs2_extend_allocation,
	TP_PROTO(unsigned long long ip_blkno, unsigned long long size,
		 unsigned int clusters, unsigned int clusters_to_add,
		 int why, int restart_func),
	TP_ARGS(ip_blkno, size, clusters, clusters_to_add, why, restart_func),
	TP_STRUCT__entry(
		__field(unsigned long long, ip_blkno)
		__field(unsigned long long, size)
		__field(unsigned int, clusters)
		__field(unsigned int, clusters_to_add)
		__field(int, why)
		__field(int, restart_func)
	),
	TP_fast_assign(
		__entry->ip_blkno = ip_blkno;
		__entry->size = size;
		__entry->clusters = clusters;
		__entry->clusters_to_add = clusters_to_add;
		__entry->why = why;
		__entry->restart_func = restart_func;
	),
	TP_printk("%llu %llu %u %u %d %d",
		  __entry->ip_blkno, __entry->size, __entry->clusters,
		  __entry->clusters_to_add, __entry->why, __entry->restart_func)
);

TRACE_EVENT(ocfs2_extend_allocation_end,
	TP_PROTO(unsigned long long ino,
		 unsigned int di_clusters, unsigned long long di_size,
		 unsigned int ip_clusters, unsigned long long i_size),
	TP_ARGS(ino, di_clusters, di_size, ip_clusters, i_size),
	TP_STRUCT__entry(
		__field(unsigned long long, ino)
		__field(unsigned int, di_clusters)
		__field(unsigned long long, di_size)
		__field(unsigned int, ip_clusters)
		__field(unsigned long long, i_size)
	),
	TP_fast_assign(
		__entry->ino = ino;
		__entry->di_clusters = di_clusters;
		__entry->di_size = di_size;
		__entry->ip_clusters = ip_clusters;
		__entry->i_size = i_size;
	),
	TP_printk("%llu %u %llu %u %llu", __entry->ino, __entry->di_clusters,
		  __entry->di_size, __entry->ip_clusters, __entry->i_size)
);

TRACE_EVENT(ocfs2_write_zero_page,
	TP_PROTO(unsigned long long ino,
		 unsigned long long abs_from, unsigned long long abs_to,
		 unsigned long index, unsigned int zero_from,
		 unsigned int zero_to),
	TP_ARGS(ino, abs_from, abs_to, index, zero_from, zero_to),
	TP_STRUCT__entry(
		__field(unsigned long long, ino)
		__field(unsigned long long, abs_from)
		__field(unsigned long long, abs_to)
		__field(unsigned long, index)
		__field(unsigned int, zero_from)
		__field(unsigned int, zero_to)
	),
	TP_fast_assign(
		__entry->ino = ino;
		__entry->abs_from = abs_from;
		__entry->abs_to = abs_to;
		__entry->index = index;
		__entry->zero_from = zero_from;
		__entry->zero_to = zero_to;
	),
	TP_printk("%llu %llu %llu %lu %u %u", __entry->ino,
		  __entry->abs_from, __entry->abs_to,
		  __entry->index, __entry->zero_from, __entry->zero_to)
);

DEFINE_OCFS2_ULL_ULL_ULL_EVENT(ocfs2_zero_extend_range);

DEFINE_OCFS2_ULL_ULL_ULL_EVENT(ocfs2_zero_extend);

TRACE_EVENT(ocfs2_setattr,
	TP_PROTO(void *inode, void *dentry,
		 unsigned long long ino,
		 unsigned int d_len, const unsigned char *d_name,
		 unsigned int ia_valid, unsigned int ia_mode,
		 unsigned int ia_uid, unsigned int ia_gid),
	TP_ARGS(inode, dentry, ino, d_len, d_name,
		ia_valid, ia_mode, ia_uid, ia_gid),
	TP_STRUCT__entry(
		__field(void *, inode)
		__field(void *, dentry)
		__field(unsigned long long, ino)
		__field(unsigned int, d_len)
		__string(d_name, d_name)
		__field(unsigned int, ia_valid)
		__field(unsigned int, ia_mode)
		__field(unsigned int, ia_uid)
		__field(unsigned int, ia_gid)
	),
	TP_fast_assign(
		__entry->inode = inode;
		__entry->dentry = dentry;
		__entry->ino = ino;
		__entry->d_len = d_len;
		__assign_str(d_name, d_name);
		__entry->ia_valid = ia_valid;
		__entry->ia_mode = ia_mode;
		__entry->ia_uid = ia_uid;
		__entry->ia_gid = ia_gid;
	),
	TP_printk("%p %p %llu %.*s %u %u %u %u", __entry->inode,
		  __entry->dentry, __entry->ino, __entry->d_len,
		  __get_str(d_name), __entry->ia_valid, __entry->ia_mode,
		  __entry->ia_uid, __entry->ia_gid)
);

DEFINE_OCFS2_ULL_UINT_EVENT(ocfs2_write_remove_suid);

DEFINE_OCFS2_ULL_ULL_ULL_EVENT(ocfs2_zero_partial_clusters);

DEFINE_OCFS2_ULL_ULL_EVENT(ocfs2_zero_partial_clusters_range1);

DEFINE_OCFS2_ULL_ULL_EVENT(ocfs2_zero_partial_clusters_range2);

DEFINE_OCFS2_ULL_ULL_ULL_EVENT(ocfs2_remove_inode_range);

TRACE_EVENT(ocfs2_prepare_inode_for_write,
	TP_PROTO(unsigned long long ino, unsigned long long saved_pos,
		 int appending, unsigned long count,
		 int *direct_io, int *has_refcount),
	TP_ARGS(ino, saved_pos, appending, count, direct_io, has_refcount),
	TP_STRUCT__entry(
		__field(unsigned long long, ino)
		__field(unsigned long long, saved_pos)
		__field(int, appending)
		__field(unsigned long, count)
		__field(int, direct_io)
		__field(int, has_refcount)
	),
	TP_fast_assign(
		__entry->ino = ino;
		__entry->saved_pos = saved_pos;
		__entry->appending = appending;
		__entry->count = count;
		__entry->direct_io = direct_io ? *direct_io : -1;
		__entry->has_refcount = has_refcount ? *has_refcount : -1;
	),
	TP_printk("%llu %llu %d %lu %d %d", __entry->ino,
		  __entry->saved_pos, __entry->appending, __entry->count,
		  __entry->direct_io, __entry->has_refcount)
);

DEFINE_OCFS2_INT_EVENT(generic_file_aio_read_ret);

/* End of trace events for fs/ocfs2/file.c. */

#endif /* _TRACE_OCFS2_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE ocfs2_trace
#include <trace/define_trace.h>
