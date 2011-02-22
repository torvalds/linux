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
#endif /* _TRACE_OCFS2_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE ocfs2_trace
#include <trace/define_trace.h>
