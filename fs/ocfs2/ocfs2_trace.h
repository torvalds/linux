/* SPDX-License-Identifier: GPL-2.0 */
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

DECLARE_EVENT_CLASS(ocfs2__pointer,
	TP_PROTO(void *pointer),
	TP_ARGS(pointer),
	TP_STRUCT__entry(
		__field(void *, pointer)
	),
	TP_fast_assign(
		__entry->pointer = pointer;
	),
	TP_printk("%p", __entry->pointer)
);

#define DEFINE_OCFS2_POINTER_EVENT(name)	\
DEFINE_EVENT(ocfs2__pointer, name,	\
	TP_PROTO(void *pointer),	\
	TP_ARGS(pointer))

DECLARE_EVENT_CLASS(ocfs2__string,
	TP_PROTO(const char *name),
	TP_ARGS(name),
	TP_STRUCT__entry(
		__string(name,name)
	),
	TP_fast_assign(
		__assign_str(name, name);
	),
	TP_printk("%s", __get_str(name))
);

#define DEFINE_OCFS2_STRING_EVENT(name)	\
DEFINE_EVENT(ocfs2__string, name,	\
	TP_PROTO(const char *name),	\
	TP_ARGS(name))

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

DECLARE_EVENT_CLASS(ocfs2__uint_int,
	TP_PROTO(unsigned int value1, int value2),
	TP_ARGS(value1, value2),
	TP_STRUCT__entry(
		__field(unsigned int, value1)
		__field(int, value2)
	),
	TP_fast_assign(
		__entry->value1	= value1;
		__entry->value2	= value2;
	),
	TP_printk("%u %d", __entry->value1, __entry->value2)
);

#define DEFINE_OCFS2_UINT_INT_EVENT(name)	\
DEFINE_EVENT(ocfs2__uint_int, name,	\
	TP_PROTO(unsigned int val1, int val2),	\
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

DECLARE_EVENT_CLASS(ocfs2__ull_int,
	TP_PROTO(unsigned long long value1, int value2),
	TP_ARGS(value1, value2),
	TP_STRUCT__entry(
		__field(unsigned long long, value1)
		__field(int, value2)
	),
	TP_fast_assign(
		__entry->value1	= value1;
		__entry->value2	= value2;
	),
	TP_printk("%llu %d", __entry->value1, __entry->value2)
);

#define DEFINE_OCFS2_ULL_INT_EVENT(name)	\
DEFINE_EVENT(ocfs2__ull_int, name,	\
	TP_PROTO(unsigned long long val1, int val2),	\
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

DECLARE_EVENT_CLASS(ocfs2__ull_ull_uint_uint,
	TP_PROTO(unsigned long long value1, unsigned long long value2,
		 unsigned int value3, unsigned int value4),
	TP_ARGS(value1, value2, value3, value4),
	TP_STRUCT__entry(
		__field(unsigned long long, value1)
		__field(unsigned long long, value2)
		__field(unsigned int, value3)
		__field(unsigned int, value4)
	),
	TP_fast_assign(
		__entry->value1 = value1;
		__entry->value2 = value2;
		__entry->value3 = value3;
		__entry->value4 = value4;
	),
	TP_printk("%llu %llu %u %u",
		  __entry->value1, __entry->value2,
		  __entry->value3, __entry->value4)
);

#define DEFINE_OCFS2_ULL_ULL_UINT_UINT_EVENT(name)	\
DEFINE_EVENT(ocfs2__ull_ull_uint_uint, name,	\
	TP_PROTO(unsigned long long ull, unsigned long long ull1,	\
		 unsigned int value2, unsigned int value3),	\
	TP_ARGS(ull, ull1, value2, value3))

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

TRACE_EVENT(ocfs2_trim_extent,
	TP_PROTO(struct super_block *sb, unsigned long long blk,
		 unsigned long long count),
	TP_ARGS(sb, blk, count),
	TP_STRUCT__entry(
		__field(int, dev_major)
		__field(int, dev_minor)
		__field(unsigned long long, blk)
		__field(__u64,	count)
	),
	TP_fast_assign(
		__entry->dev_major = MAJOR(sb->s_dev);
		__entry->dev_minor = MINOR(sb->s_dev);
		__entry->blk = blk;
		__entry->count = count;
	),
	TP_printk("%d %d %llu %llu",
		  __entry->dev_major, __entry->dev_minor,
		  __entry->blk, __entry->count)
);

DEFINE_OCFS2_ULL_UINT_UINT_UINT_EVENT(ocfs2_trim_group);

DEFINE_OCFS2_ULL_ULL_ULL_EVENT(ocfs2_trim_mainbm);

DEFINE_OCFS2_ULL_ULL_ULL_EVENT(ocfs2_trim_fs);

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

DEFINE_OCFS2_FILE_OPS(ocfs2_file_write_iter);

DEFINE_OCFS2_FILE_OPS(ocfs2_file_splice_write);

DEFINE_OCFS2_FILE_OPS(ocfs2_file_read_iter);

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
		 unsigned long count, int wait),
	TP_ARGS(ino, saved_pos, count, wait),
	TP_STRUCT__entry(
		__field(unsigned long long, ino)
		__field(unsigned long long, saved_pos)
		__field(unsigned long, count)
		__field(int, wait)
	),
	TP_fast_assign(
		__entry->ino = ino;
		__entry->saved_pos = saved_pos;
		__entry->count = count;
		__entry->wait = wait;
	),
	TP_printk("%llu %llu %lu %d", __entry->ino,
		  __entry->saved_pos, __entry->count, __entry->wait)
);

DEFINE_OCFS2_INT_EVENT(generic_file_read_iter_ret);

/* End of trace events for fs/ocfs2/file.c. */

/* Trace events for fs/ocfs2/inode.c. */

TRACE_EVENT(ocfs2_iget_begin,
	TP_PROTO(unsigned long long ino, unsigned int flags, int sysfile_type),
	TP_ARGS(ino, flags, sysfile_type),
	TP_STRUCT__entry(
		__field(unsigned long long, ino)
		__field(unsigned int, flags)
		__field(int, sysfile_type)
	),
	TP_fast_assign(
		__entry->ino = ino;
		__entry->flags = flags;
		__entry->sysfile_type = sysfile_type;
	),
	TP_printk("%llu %u %d", __entry->ino,
		  __entry->flags, __entry->sysfile_type)
);

DEFINE_OCFS2_ULL_EVENT(ocfs2_iget5_locked);

TRACE_EVENT(ocfs2_iget_end,
	TP_PROTO(void *inode, unsigned long long ino),
	TP_ARGS(inode, ino),
	TP_STRUCT__entry(
		__field(void *, inode)
		__field(unsigned long long, ino)
	),
	TP_fast_assign(
		__entry->inode = inode;
		__entry->ino = ino;
	),
	TP_printk("%p %llu", __entry->inode, __entry->ino)
);

TRACE_EVENT(ocfs2_find_actor,
	TP_PROTO(void *inode, unsigned long long ino,
		 void *args,  unsigned long long fi_blkno),
	TP_ARGS(inode, ino, args, fi_blkno),
	TP_STRUCT__entry(
		__field(void *, inode)
		__field(unsigned long long, ino)
		__field(void *, args)
		__field(unsigned long long, fi_blkno)
	),
	TP_fast_assign(
		__entry->inode = inode;
		__entry->ino = ino;
		__entry->args = args;
		__entry->fi_blkno = fi_blkno;
	),
	TP_printk("%p %llu %p %llu", __entry->inode, __entry->ino,
		  __entry->args, __entry->fi_blkno)
);

DEFINE_OCFS2_ULL_UINT_EVENT(ocfs2_populate_inode);

DEFINE_OCFS2_ULL_INT_EVENT(ocfs2_read_locked_inode);

DEFINE_OCFS2_INT_INT_EVENT(ocfs2_check_orphan_recovery_state);

DEFINE_OCFS2_ULL_EVENT(ocfs2_validate_inode_block);
DEFINE_OCFS2_ULL_EVENT(ocfs2_filecheck_validate_inode_block);
DEFINE_OCFS2_ULL_EVENT(ocfs2_filecheck_repair_inode_block);

TRACE_EVENT(ocfs2_inode_is_valid_to_delete,
	TP_PROTO(void *task, void *dc_task, unsigned long long ino,
		 unsigned int flags),
	TP_ARGS(task, dc_task, ino, flags),
	TP_STRUCT__entry(
		__field(void *, task)
		__field(void *, dc_task)
		__field(unsigned long long, ino)
		__field(unsigned int, flags)
	),
	TP_fast_assign(
		__entry->task = task;
		__entry->dc_task = dc_task;
		__entry->ino = ino;
		__entry->flags = flags;
	),
	TP_printk("%p %p %llu %u", __entry->task, __entry->dc_task,
		  __entry->ino, __entry->flags)
);

DEFINE_OCFS2_ULL_UINT_EVENT(ocfs2_query_inode_wipe_begin);

DEFINE_OCFS2_UINT_EVENT(ocfs2_query_inode_wipe_succ);

DEFINE_OCFS2_INT_INT_EVENT(ocfs2_query_inode_wipe_end);

DEFINE_OCFS2_ULL_INT_EVENT(ocfs2_cleanup_delete_inode);

DEFINE_OCFS2_ULL_ULL_UINT_EVENT(ocfs2_delete_inode);

DEFINE_OCFS2_ULL_UINT_EVENT(ocfs2_clear_inode);

DEFINE_OCFS2_ULL_UINT_UINT_EVENT(ocfs2_drop_inode);

TRACE_EVENT(ocfs2_inode_revalidate,
	TP_PROTO(void *inode, unsigned long long ino,
		 unsigned int flags),
	TP_ARGS(inode, ino, flags),
	TP_STRUCT__entry(
		__field(void *, inode)
		__field(unsigned long long, ino)
		__field(unsigned int, flags)
	),
	TP_fast_assign(
		__entry->inode = inode;
		__entry->ino = ino;
		__entry->flags = flags;
	),
	TP_printk("%p %llu %u", __entry->inode, __entry->ino, __entry->flags)
);

DEFINE_OCFS2_ULL_EVENT(ocfs2_mark_inode_dirty);

/* End of trace events for fs/ocfs2/inode.c. */

/* Trace events for fs/ocfs2/extent_map.c. */

TRACE_EVENT(ocfs2_read_virt_blocks,
	TP_PROTO(void *inode, unsigned long long vblock, int nr,
		 void *bhs, unsigned int flags, void *validate),
	TP_ARGS(inode, vblock, nr, bhs, flags, validate),
	TP_STRUCT__entry(
		__field(void *, inode)
		__field(unsigned long long, vblock)
		__field(int, nr)
		__field(void *, bhs)
		__field(unsigned int, flags)
		__field(void *, validate)
	),
	TP_fast_assign(
		__entry->inode = inode;
		__entry->vblock = vblock;
		__entry->nr = nr;
		__entry->bhs = bhs;
		__entry->flags = flags;
		__entry->validate = validate;
	),
	TP_printk("%p %llu %d %p %x %p", __entry->inode, __entry->vblock,
		  __entry->nr, __entry->bhs, __entry->flags, __entry->validate)
);

/* End of trace events for fs/ocfs2/extent_map.c. */

/* Trace events for fs/ocfs2/slot_map.c. */

DEFINE_OCFS2_UINT_EVENT(ocfs2_refresh_slot_info);

DEFINE_OCFS2_ULL_UINT_EVENT(ocfs2_map_slot_buffers);

DEFINE_OCFS2_ULL_UINT_EVENT(ocfs2_map_slot_buffers_block);

DEFINE_OCFS2_INT_EVENT(ocfs2_find_slot);

/* End of trace events for fs/ocfs2/slot_map.c. */

/* Trace events for fs/ocfs2/heartbeat.c. */

DEFINE_OCFS2_INT_EVENT(ocfs2_do_node_down);

/* End of trace events for fs/ocfs2/heartbeat.c. */

/* Trace events for fs/ocfs2/super.c. */

TRACE_EVENT(ocfs2_remount,
	TP_PROTO(unsigned long s_flags, unsigned long osb_flags, int flags),
	TP_ARGS(s_flags, osb_flags, flags),
	TP_STRUCT__entry(
		__field(unsigned long, s_flags)
		__field(unsigned long, osb_flags)
		__field(int, flags)
	),
	TP_fast_assign(
		__entry->s_flags = s_flags;
		__entry->osb_flags = osb_flags;
		__entry->flags = flags;
	),
	TP_printk("%lu %lu %d", __entry->s_flags,
		  __entry->osb_flags, __entry->flags)
);

TRACE_EVENT(ocfs2_fill_super,
	TP_PROTO(void *sb, void *data, int silent),
	TP_ARGS(sb, data, silent),
	TP_STRUCT__entry(
		__field(void *, sb)
		__field(void *, data)
		__field(int, silent)
	),
	TP_fast_assign(
		__entry->sb = sb;
		__entry->data = data;
		__entry->silent = silent;
	),
	TP_printk("%p %p %d", __entry->sb,
		  __entry->data, __entry->silent)
);

TRACE_EVENT(ocfs2_parse_options,
	TP_PROTO(int is_remount, char *options),
	TP_ARGS(is_remount, options),
	TP_STRUCT__entry(
		__field(int, is_remount)
		__string(options, options)
	),
	TP_fast_assign(
		__entry->is_remount = is_remount;
		__assign_str(options, options);
	),
	TP_printk("%d %s", __entry->is_remount, __get_str(options))
);

DEFINE_OCFS2_POINTER_EVENT(ocfs2_put_super);

TRACE_EVENT(ocfs2_statfs,
	TP_PROTO(void *sb, void *buf),
	TP_ARGS(sb, buf),
	TP_STRUCT__entry(
		__field(void *, sb)
		__field(void *, buf)
	),
	TP_fast_assign(
		__entry->sb = sb;
		__entry->buf = buf;
	),
	TP_printk("%p %p", __entry->sb, __entry->buf)
);

DEFINE_OCFS2_POINTER_EVENT(ocfs2_dismount_volume);

TRACE_EVENT(ocfs2_initialize_super,
	TP_PROTO(char *label, char *uuid_str, unsigned long long root_dir,
		 unsigned long long system_dir, int cluster_bits),
	TP_ARGS(label, uuid_str, root_dir, system_dir, cluster_bits),
	TP_STRUCT__entry(
		__string(label, label)
		__string(uuid_str, uuid_str)
		__field(unsigned long long, root_dir)
		__field(unsigned long long, system_dir)
		__field(int, cluster_bits)
	),
	TP_fast_assign(
		__assign_str(label, label);
		__assign_str(uuid_str, uuid_str);
		__entry->root_dir = root_dir;
		__entry->system_dir = system_dir;
		__entry->cluster_bits = cluster_bits;
	),
	TP_printk("%s %s %llu %llu %d", __get_str(label), __get_str(uuid_str),
		  __entry->root_dir, __entry->system_dir, __entry->cluster_bits)
);

/* End of trace events for fs/ocfs2/super.c. */

/* Trace events for fs/ocfs2/xattr.c. */

DEFINE_OCFS2_ULL_EVENT(ocfs2_validate_xattr_block);

DEFINE_OCFS2_UINT_EVENT(ocfs2_xattr_extend_allocation);

TRACE_EVENT(ocfs2_init_xattr_set_ctxt,
	TP_PROTO(const char *name, int meta, int clusters, int credits),
	TP_ARGS(name, meta, clusters, credits),
	TP_STRUCT__entry(
		__string(name, name)
		__field(int, meta)
		__field(int, clusters)
		__field(int, credits)
	),
	TP_fast_assign(
		__assign_str(name, name);
		__entry->meta = meta;
		__entry->clusters = clusters;
		__entry->credits = credits;
	),
	TP_printk("%s %d %d %d", __get_str(name), __entry->meta,
		  __entry->clusters, __entry->credits)
);

DECLARE_EVENT_CLASS(ocfs2__xattr_find,
	TP_PROTO(unsigned long long ino, const char *name, int name_index,
		 unsigned int hash, unsigned long long location,
		 int xe_index),
	TP_ARGS(ino, name, name_index, hash, location, xe_index),
	TP_STRUCT__entry(
		__field(unsigned long long, ino)
		__string(name, name)
		__field(int, name_index)
		__field(unsigned int, hash)
		__field(unsigned long long, location)
		__field(int, xe_index)
	),
	TP_fast_assign(
		__entry->ino = ino;
		__assign_str(name, name);
		__entry->name_index = name_index;
		__entry->hash = hash;
		__entry->location = location;
		__entry->xe_index = xe_index;
	),
	TP_printk("%llu %s %d %u %llu %d", __entry->ino, __get_str(name),
		  __entry->name_index, __entry->hash, __entry->location,
		  __entry->xe_index)
);

#define DEFINE_OCFS2_XATTR_FIND_EVENT(name)					\
DEFINE_EVENT(ocfs2__xattr_find, name,					\
TP_PROTO(unsigned long long ino, const char *name, int name_index,	\
	 unsigned int hash, unsigned long long bucket,			\
	 int xe_index),							\
	TP_ARGS(ino, name, name_index, hash, bucket, xe_index))

DEFINE_OCFS2_XATTR_FIND_EVENT(ocfs2_xattr_bucket_find);

DEFINE_OCFS2_XATTR_FIND_EVENT(ocfs2_xattr_index_block_find);

DEFINE_OCFS2_XATTR_FIND_EVENT(ocfs2_xattr_index_block_find_rec);

DEFINE_OCFS2_ULL_ULL_UINT_EVENT(ocfs2_iterate_xattr_buckets);

DEFINE_OCFS2_ULL_UINT_EVENT(ocfs2_iterate_xattr_bucket);

DEFINE_OCFS2_ULL_ULL_EVENT(ocfs2_cp_xattr_block_to_bucket_begin);

DEFINE_OCFS2_UINT_UINT_UINT_EVENT(ocfs2_cp_xattr_block_to_bucket_end);

DEFINE_OCFS2_ULL_EVENT(ocfs2_xattr_create_index_block_begin);

DEFINE_OCFS2_ULL_EVENT(ocfs2_xattr_create_index_block);

DEFINE_OCFS2_ULL_UINT_UINT_UINT_EVENT(ocfs2_defrag_xattr_bucket);

DEFINE_OCFS2_ULL_ULL_EVENT(ocfs2_mv_xattr_bucket_cross_cluster);

DEFINE_OCFS2_ULL_ULL_EVENT(ocfs2_divide_xattr_bucket_begin);

DEFINE_OCFS2_UINT_UINT_UINT_EVENT(ocfs2_divide_xattr_bucket_move);

DEFINE_OCFS2_ULL_ULL_UINT_EVENT(ocfs2_cp_xattr_bucket);

DEFINE_OCFS2_ULL_ULL_EVENT(ocfs2_mv_xattr_buckets);

DEFINE_OCFS2_ULL_ULL_UINT_EVENT(ocfs2_adjust_xattr_cross_cluster);

DEFINE_OCFS2_ULL_ULL_UINT_UINT_EVENT(ocfs2_add_new_xattr_cluster_begin);

DEFINE_OCFS2_ULL_UINT_EVENT(ocfs2_add_new_xattr_cluster);

DEFINE_OCFS2_ULL_UINT_UINT_EVENT(ocfs2_add_new_xattr_cluster_insert);

DEFINE_OCFS2_ULL_ULL_UINT_UINT_EVENT(ocfs2_extend_xattr_bucket);

DEFINE_OCFS2_ULL_EVENT(ocfs2_add_new_xattr_bucket);

DEFINE_OCFS2_ULL_UINT_UINT_EVENT(ocfs2_xattr_bucket_value_truncate);

DEFINE_OCFS2_ULL_ULL_UINT_UINT_EVENT(ocfs2_rm_xattr_cluster);

DEFINE_OCFS2_ULL_UINT_EVENT(ocfs2_reflink_xattr_header);

DEFINE_OCFS2_ULL_INT_EVENT(ocfs2_create_empty_xattr_block);

DEFINE_OCFS2_STRING_EVENT(ocfs2_xattr_set_entry_bucket);

DEFINE_OCFS2_STRING_EVENT(ocfs2_xattr_set_entry_index_block);

DEFINE_OCFS2_ULL_UINT_EVENT(ocfs2_xattr_bucket_value_refcount);

DEFINE_OCFS2_ULL_UINT_UINT_EVENT(ocfs2_reflink_xattr_buckets);

DEFINE_OCFS2_ULL_UINT_EVENT(ocfs2_reflink_xattr_rec);

/* End of trace events for fs/ocfs2/xattr.c. */

/* Trace events for fs/ocfs2/reservations.c. */

DEFINE_OCFS2_UINT_UINT_EVENT(ocfs2_resv_insert);

DEFINE_OCFS2_ULL_UINT_UINT_UINT_EVENT(ocfs2_resmap_find_free_bits_begin);

DEFINE_OCFS2_UINT_UINT_EVENT(ocfs2_resmap_find_free_bits_end);

TRACE_EVENT(ocfs2_resv_find_window_begin,
	TP_PROTO(unsigned int r_start, unsigned int r_end, unsigned int goal,
		 unsigned int wanted, int empty_root),
	TP_ARGS(r_start, r_end, goal, wanted, empty_root),
	TP_STRUCT__entry(
		__field(unsigned int, r_start)
		__field(unsigned int, r_end)
		__field(unsigned int, goal)
		__field(unsigned int, wanted)
		__field(int, empty_root)
	),
	TP_fast_assign(
		__entry->r_start = r_start;
		__entry->r_end = r_end;
		__entry->goal = goal;
		__entry->wanted = wanted;
		__entry->empty_root = empty_root;
	),
	TP_printk("%u %u %u %u %d", __entry->r_start, __entry->r_end,
		  __entry->goal, __entry->wanted, __entry->empty_root)
);

DEFINE_OCFS2_UINT_UINT_EVENT(ocfs2_resv_find_window_prev);

DEFINE_OCFS2_INT_INT_EVENT(ocfs2_resv_find_window_next);

DEFINE_OCFS2_UINT_UINT_UINT_EVENT(ocfs2_cannibalize_resv_begin);

TRACE_EVENT(ocfs2_cannibalize_resv_end,
	TP_PROTO(unsigned int start, unsigned int end, unsigned int len,
		 unsigned int last_start, unsigned int last_len),
	TP_ARGS(start, end, len, last_start, last_len),
	TP_STRUCT__entry(
		__field(unsigned int, start)
		__field(unsigned int, end)
		__field(unsigned int, len)
		__field(unsigned int, last_start)
		__field(unsigned int, last_len)
	),
	TP_fast_assign(
		__entry->start = start;
		__entry->end = end;
		__entry->len = len;
		__entry->last_start = last_start;
		__entry->last_len = last_len;
	),
	TP_printk("%u %u %u %u %u", __entry->start, __entry->end,
		  __entry->len, __entry->last_start, __entry->last_len)
);

DEFINE_OCFS2_UINT_UINT_EVENT(ocfs2_resmap_resv_bits);

TRACE_EVENT(ocfs2_resmap_claimed_bits_begin,
	TP_PROTO(unsigned int cstart, unsigned int cend, unsigned int clen,
		 unsigned int r_start, unsigned int r_end, unsigned int r_len,
		 unsigned int last_start, unsigned int last_len),
	TP_ARGS(cstart, cend, clen, r_start, r_end,
		r_len, last_start, last_len),
	TP_STRUCT__entry(
		__field(unsigned int, cstart)
		__field(unsigned int, cend)
		__field(unsigned int, clen)
		__field(unsigned int, r_start)
		__field(unsigned int, r_end)
		__field(unsigned int, r_len)
		__field(unsigned int, last_start)
		__field(unsigned int, last_len)
	),
	TP_fast_assign(
		__entry->cstart = cstart;
		__entry->cend = cend;
		__entry->clen = clen;
		__entry->r_start = r_start;
		__entry->r_end = r_end;
		__entry->r_len = r_len;
		__entry->last_start = last_start;
		__entry->last_len = last_len;
	),
	TP_printk("%u %u %u %u %u %u %u %u",
		  __entry->cstart, __entry->cend, __entry->clen,
		  __entry->r_start, __entry->r_end, __entry->r_len,
		  __entry->last_start, __entry->last_len)
);

TRACE_EVENT(ocfs2_resmap_claimed_bits_end,
	TP_PROTO(unsigned int start, unsigned int end, unsigned int len,
		 unsigned int last_start, unsigned int last_len),
	TP_ARGS(start, end, len, last_start, last_len),
	TP_STRUCT__entry(
		__field(unsigned int, start)
		__field(unsigned int, end)
		__field(unsigned int, len)
		__field(unsigned int, last_start)
		__field(unsigned int, last_len)
	),
	TP_fast_assign(
		__entry->start = start;
		__entry->end = end;
		__entry->len = len;
		__entry->last_start = last_start;
		__entry->last_len = last_len;
	),
	TP_printk("%u %u %u %u %u", __entry->start, __entry->end,
		  __entry->len, __entry->last_start, __entry->last_len)
);

/* End of trace events for fs/ocfs2/reservations.c. */

/* Trace events for fs/ocfs2/quota_local.c. */

DEFINE_OCFS2_ULL_UINT_EVENT(ocfs2_recover_local_quota_file);

DEFINE_OCFS2_INT_EVENT(ocfs2_finish_quota_recovery);

DEFINE_OCFS2_ULL_ULL_UINT_EVENT(olq_set_dquot);

/* End of trace events for fs/ocfs2/quota_local.c. */

/* Trace events for fs/ocfs2/quota_global.c. */

DEFINE_OCFS2_ULL_EVENT(ocfs2_validate_quota_block);

TRACE_EVENT(ocfs2_sync_dquot,
	TP_PROTO(unsigned int dq_id, long long dqb_curspace,
		 long long spacechange, long long curinodes,
		 long long inodechange),
	TP_ARGS(dq_id, dqb_curspace, spacechange, curinodes, inodechange),
	TP_STRUCT__entry(
		__field(unsigned int, dq_id)
		__field(long long, dqb_curspace)
		__field(long long, spacechange)
		__field(long long, curinodes)
		__field(long long, inodechange)
	),
	TP_fast_assign(
		__entry->dq_id = dq_id;
		__entry->dqb_curspace = dqb_curspace;
		__entry->spacechange = spacechange;
		__entry->curinodes = curinodes;
		__entry->inodechange = inodechange;
	),
	TP_printk("%u %lld %lld %lld %lld", __entry->dq_id,
		  __entry->dqb_curspace, __entry->spacechange,
		  __entry->curinodes, __entry->inodechange)
);

TRACE_EVENT(ocfs2_sync_dquot_helper,
	TP_PROTO(unsigned int dq_id, unsigned int dq_type, unsigned long type,
		 const char *s_id),
	TP_ARGS(dq_id, dq_type, type, s_id),

	TP_STRUCT__entry(
		__field(unsigned int, dq_id)
		__field(unsigned int, dq_type)
		__field(unsigned long, type)
		__string(s_id, s_id)
	),
	TP_fast_assign(
		__entry->dq_id = dq_id;
		__entry->dq_type = dq_type;
		__entry->type = type;
		__assign_str(s_id, s_id);
	),
	TP_printk("%u %u %lu %s", __entry->dq_id, __entry->dq_type,
		  __entry->type, __get_str(s_id))
);

DEFINE_OCFS2_UINT_INT_EVENT(ocfs2_write_dquot);

DEFINE_OCFS2_UINT_INT_EVENT(ocfs2_release_dquot);

DEFINE_OCFS2_UINT_INT_EVENT(ocfs2_acquire_dquot);

DEFINE_OCFS2_UINT_INT_EVENT(ocfs2_get_next_id);

DEFINE_OCFS2_UINT_INT_EVENT(ocfs2_mark_dquot_dirty);

/* End of trace events for fs/ocfs2/quota_global.c. */

/* Trace events for fs/ocfs2/dir.c. */
DEFINE_OCFS2_INT_EVENT(ocfs2_search_dirblock);

DEFINE_OCFS2_ULL_EVENT(ocfs2_validate_dir_block);

DEFINE_OCFS2_POINTER_EVENT(ocfs2_find_entry_el);

TRACE_EVENT(ocfs2_dx_dir_search,
	TP_PROTO(unsigned long long ino, int namelen, const char *name,
		 unsigned int major_hash, unsigned int minor_hash,
		 unsigned long long blkno),
	TP_ARGS(ino, namelen, name, major_hash, minor_hash, blkno),
	TP_STRUCT__entry(
		__field(unsigned long long, ino)
		__field(int, namelen)
		__string(name, name)
		__field(unsigned int, major_hash)
		__field(unsigned int,minor_hash)
		__field(unsigned long long, blkno)
	),
	TP_fast_assign(
		__entry->ino = ino;
		__entry->namelen = namelen;
		__assign_str(name, name);
		__entry->major_hash = major_hash;
		__entry->minor_hash = minor_hash;
		__entry->blkno = blkno;
	),
	TP_printk("%llu %.*s %u %u %llu", __entry->ino,
		   __entry->namelen, __get_str(name),
		  __entry->major_hash, __entry->minor_hash, __entry->blkno)
);

DEFINE_OCFS2_UINT_UINT_EVENT(ocfs2_dx_dir_search_leaf_info);

DEFINE_OCFS2_ULL_INT_EVENT(ocfs2_delete_entry_dx);

DEFINE_OCFS2_ULL_EVENT(ocfs2_readdir);

TRACE_EVENT(ocfs2_find_files_on_disk,
	TP_PROTO(int namelen, const char *name, void *blkno,
		 unsigned long long dir),
	TP_ARGS(namelen, name, blkno, dir),
	TP_STRUCT__entry(
		__field(int, namelen)
		__string(name, name)
		__field(void *, blkno)
		__field(unsigned long long, dir)
	),
	TP_fast_assign(
		__entry->namelen = namelen;
		__assign_str(name, name);
		__entry->blkno = blkno;
		__entry->dir = dir;
	),
	TP_printk("%.*s %p %llu", __entry->namelen, __get_str(name),
		  __entry->blkno, __entry->dir)
);

TRACE_EVENT(ocfs2_check_dir_for_entry,
	TP_PROTO(unsigned long long dir, int namelen, const char *name),
	TP_ARGS(dir, namelen, name),
	TP_STRUCT__entry(
		__field(unsigned long long, dir)
		__field(int, namelen)
		__string(name, name)
	),
	TP_fast_assign(
		__entry->dir = dir;
		__entry->namelen = namelen;
		__assign_str(name, name);
	),
	TP_printk("%llu %.*s", __entry->dir,
		  __entry->namelen, __get_str(name))
);

DEFINE_OCFS2_ULL_ULL_EVENT(ocfs2_dx_dir_attach_index);

DEFINE_OCFS2_ULL_ULL_UINT_EVENT(ocfs2_dx_dir_format_cluster);

TRACE_EVENT(ocfs2_dx_dir_index_root_block,
	TP_PROTO(unsigned long long dir,
		 unsigned int major_hash, unsigned int minor_hash,
		 int namelen, const char *name, unsigned int num_used),
	TP_ARGS(dir, major_hash, minor_hash, namelen, name, num_used),
	TP_STRUCT__entry(
		__field(unsigned long long, dir)
		__field(unsigned int, major_hash)
		__field(unsigned int, minor_hash)
		__field(int, namelen)
		__string(name, name)
		__field(unsigned int, num_used)
	),
	TP_fast_assign(
		__entry->dir = dir;
		__entry->major_hash = major_hash;
		__entry->minor_hash = minor_hash;
		__entry->namelen = namelen;
		__assign_str(name, name);
		__entry->num_used = num_used;
	),
	TP_printk("%llu %x %x %.*s %u", __entry->dir,
		  __entry->major_hash, __entry->minor_hash,
		   __entry->namelen, __get_str(name), __entry->num_used)
);

DEFINE_OCFS2_ULL_ULL_EVENT(ocfs2_extend_dir);

DEFINE_OCFS2_ULL_ULL_UINT_EVENT(ocfs2_dx_dir_rebalance);

DEFINE_OCFS2_UINT_UINT_UINT_EVENT(ocfs2_dx_dir_rebalance_split);

DEFINE_OCFS2_ULL_INT_EVENT(ocfs2_prepare_dir_for_insert);

/* End of trace events for fs/ocfs2/dir.c. */

/* Trace events for fs/ocfs2/namei.c. */

DECLARE_EVENT_CLASS(ocfs2__dentry_ops,
	TP_PROTO(void *dir, void *dentry, int name_len, const char *name,
		 unsigned long long dir_blkno, unsigned long long extra),
	TP_ARGS(dir, dentry, name_len, name, dir_blkno, extra),
	TP_STRUCT__entry(
		__field(void *, dir)
		__field(void *, dentry)
		__field(int, name_len)
		__string(name, name)
		__field(unsigned long long, dir_blkno)
		__field(unsigned long long, extra)
	),
	TP_fast_assign(
		__entry->dir = dir;
		__entry->dentry = dentry;
		__entry->name_len = name_len;
		__assign_str(name, name);
		__entry->dir_blkno = dir_blkno;
		__entry->extra = extra;
	),
	TP_printk("%p %p %.*s %llu %llu", __entry->dir, __entry->dentry,
		  __entry->name_len, __get_str(name),
		  __entry->dir_blkno, __entry->extra)
);

#define DEFINE_OCFS2_DENTRY_OPS(name)					\
DEFINE_EVENT(ocfs2__dentry_ops, name,					\
TP_PROTO(void *dir, void *dentry, int name_len, const char *name,	\
	 unsigned long long dir_blkno, unsigned long long extra),	\
	TP_ARGS(dir, dentry, name_len, name, dir_blkno, extra))

DEFINE_OCFS2_DENTRY_OPS(ocfs2_lookup);

DEFINE_OCFS2_DENTRY_OPS(ocfs2_mkdir);

DEFINE_OCFS2_DENTRY_OPS(ocfs2_create);

DEFINE_OCFS2_DENTRY_OPS(ocfs2_unlink);

DEFINE_OCFS2_DENTRY_OPS(ocfs2_symlink_create);

DEFINE_OCFS2_DENTRY_OPS(ocfs2_mv_orphaned_inode_to_new);

DEFINE_OCFS2_POINTER_EVENT(ocfs2_lookup_ret);

TRACE_EVENT(ocfs2_mknod,
	TP_PROTO(void *dir, void *dentry, int name_len, const char *name,
		 unsigned long long dir_blkno, unsigned long dev, int mode),
	TP_ARGS(dir, dentry, name_len, name, dir_blkno, dev, mode),
	TP_STRUCT__entry(
		__field(void *, dir)
		__field(void *, dentry)
		__field(int, name_len)
		__string(name, name)
		__field(unsigned long long, dir_blkno)
		__field(unsigned long, dev)
		__field(int, mode)
	),
	TP_fast_assign(
		__entry->dir = dir;
		__entry->dentry = dentry;
		__entry->name_len = name_len;
		__assign_str(name, name);
		__entry->dir_blkno = dir_blkno;
		__entry->dev = dev;
		__entry->mode = mode;
	),
	TP_printk("%p %p %.*s %llu %lu %d", __entry->dir, __entry->dentry,
		  __entry->name_len, __get_str(name),
		  __entry->dir_blkno, __entry->dev, __entry->mode)
);

TRACE_EVENT(ocfs2_link,
	TP_PROTO(unsigned long long ino, int old_len, const char *old_name,
		 int name_len, const char *name),
	TP_ARGS(ino, old_len, old_name, name_len, name),
	TP_STRUCT__entry(
		__field(unsigned long long, ino)
		__field(int, old_len)
		__string(old_name, old_name)
		__field(int, name_len)
		__string(name, name)
	),
	TP_fast_assign(
		__entry->ino = ino;
		__entry->old_len = old_len;
		__assign_str(old_name, old_name);
		__entry->name_len = name_len;
		__assign_str(name, name);
	),
	TP_printk("%llu %.*s %.*s", __entry->ino,
		  __entry->old_len, __get_str(old_name),
		  __entry->name_len, __get_str(name))
);

DEFINE_OCFS2_ULL_ULL_UINT_EVENT(ocfs2_unlink_noent);

DEFINE_OCFS2_ULL_ULL_EVENT(ocfs2_double_lock);

DEFINE_OCFS2_ULL_ULL_EVENT(ocfs2_double_lock_end);

TRACE_EVENT(ocfs2_rename,
	TP_PROTO(void *old_dir, void *old_dentry,
		 void *new_dir, void *new_dentry,
		 int old_len, const char *old_name,
		 int new_len, const char *new_name),
	TP_ARGS(old_dir, old_dentry, new_dir, new_dentry,
		old_len, old_name, new_len, new_name),
	TP_STRUCT__entry(
		__field(void *, old_dir)
		__field(void *, old_dentry)
		__field(void *, new_dir)
		__field(void *, new_dentry)
		__field(int, old_len)
		__string(old_name, old_name)
		__field(int, new_len)
		__string(new_name, new_name)
	),
	TP_fast_assign(
		__entry->old_dir = old_dir;
		__entry->old_dentry = old_dentry;
		__entry->new_dir = new_dir;
		__entry->new_dentry = new_dentry;
		__entry->old_len = old_len;
		__assign_str(old_name, old_name);
		__entry->new_len = new_len;
		__assign_str(new_name, new_name);
	),
	TP_printk("%p %p %p %p %.*s %.*s",
		  __entry->old_dir, __entry->old_dentry,
		  __entry->new_dir, __entry->new_dentry,
		  __entry->old_len, __get_str(old_name),
		  __entry->new_len, __get_str(new_name))
);

DEFINE_OCFS2_ULL_ULL_EVENT(ocfs2_rename_not_permitted);

TRACE_EVENT(ocfs2_rename_target_exists,
	TP_PROTO(int new_len, const char *new_name),
	TP_ARGS(new_len, new_name),
	TP_STRUCT__entry(
		__field(int, new_len)
		__string(new_name, new_name)
	),
	TP_fast_assign(
		__entry->new_len = new_len;
		__assign_str(new_name, new_name);
	),
	TP_printk("%.*s", __entry->new_len, __get_str(new_name))
);

DEFINE_OCFS2_ULL_ULL_UINT_EVENT(ocfs2_rename_disagree);

TRACE_EVENT(ocfs2_rename_over_existing,
	TP_PROTO(unsigned long long new_blkno, void *new_bh,
		 unsigned long long newdi_blkno),
	TP_ARGS(new_blkno, new_bh, newdi_blkno),
	TP_STRUCT__entry(
		__field(unsigned long long, new_blkno)
		__field(void *, new_bh)
		__field(unsigned long long, newdi_blkno)
	),
	TP_fast_assign(
		__entry->new_blkno = new_blkno;
		__entry->new_bh = new_bh;
		__entry->newdi_blkno = newdi_blkno;
	),
	TP_printk("%llu %p %llu", __entry->new_blkno, __entry->new_bh,
		  __entry->newdi_blkno)
);

DEFINE_OCFS2_ULL_ULL_UINT_EVENT(ocfs2_create_symlink_data);

TRACE_EVENT(ocfs2_symlink_begin,
	TP_PROTO(void *dir, void *dentry, const char *symname,
		 int len, const char *name),
	TP_ARGS(dir, dentry, symname, len, name),
	TP_STRUCT__entry(
		__field(void *, dir)
		__field(void *, dentry)
		__field(const char *, symname)
		__field(int, len)
		__string(name, name)
	),
	TP_fast_assign(
		__entry->dir = dir;
		__entry->dentry = dentry;
		__entry->symname = symname;
		__entry->len = len;
		__assign_str(name, name);
	),
	TP_printk("%p %p %s %.*s", __entry->dir, __entry->dentry,
		  __entry->symname, __entry->len, __get_str(name))
);

TRACE_EVENT(ocfs2_blkno_stringify,
	TP_PROTO(unsigned long long blkno, const char *name, int namelen),
	TP_ARGS(blkno, name, namelen),
	TP_STRUCT__entry(
		__field(unsigned long long, blkno)
		__string(name, name)
		__field(int, namelen)
	),
	TP_fast_assign(
		__entry->blkno = blkno;
		__assign_str(name, name);
		__entry->namelen = namelen;
	),
	TP_printk("%llu %s %d", __entry->blkno, __get_str(name),
		  __entry->namelen)
);

DEFINE_OCFS2_ULL_EVENT(ocfs2_orphan_add_begin);

DEFINE_OCFS2_ULL_UINT_EVENT(ocfs2_orphan_add_end);

TRACE_EVENT(ocfs2_orphan_del,
	TP_PROTO(unsigned long long dir, const char *name, int namelen),
	TP_ARGS(dir, name, namelen),
	TP_STRUCT__entry(
		__field(unsigned long long, dir)
		__string(name, name)
		__field(int, namelen)
	),
	TP_fast_assign(
		__entry->dir = dir;
		__assign_str(name, name);
		__entry->namelen = namelen;
	),
	TP_printk("%llu %s %d", __entry->dir, __get_str(name),
		  __entry->namelen)
);

/* End of trace events for fs/ocfs2/namei.c. */

/* Trace events for fs/ocfs2/dcache.c. */

TRACE_EVENT(ocfs2_dentry_revalidate,
	TP_PROTO(void *dentry, int len, const char *name),
	TP_ARGS(dentry, len, name),
	TP_STRUCT__entry(
		__field(void *, dentry)
		__field(int, len)
		__string(name, name)
	),
	TP_fast_assign(
		__entry->dentry = dentry;
		__entry->len = len;
		__assign_str(name, name);
	),
	TP_printk("%p %.*s", __entry->dentry, __entry->len, __get_str(name))
);

TRACE_EVENT(ocfs2_dentry_revalidate_negative,
	TP_PROTO(int len, const char *name, unsigned long pgen,
		 unsigned long gen),
	TP_ARGS(len, name, pgen, gen),
	TP_STRUCT__entry(
		__field(int, len)
		__string(name, name)
		__field(unsigned long, pgen)
		__field(unsigned long, gen)
	),
	TP_fast_assign(
		__entry->len = len;
		__assign_str(name, name);
		__entry->pgen = pgen;
		__entry->gen = gen;
	),
	TP_printk("%.*s %lu %lu", __entry->len, __get_str(name),
		  __entry->pgen, __entry->gen)
);

DEFINE_OCFS2_ULL_EVENT(ocfs2_dentry_revalidate_delete);

DEFINE_OCFS2_ULL_INT_EVENT(ocfs2_dentry_revalidate_orphaned);

DEFINE_OCFS2_ULL_EVENT(ocfs2_dentry_revalidate_nofsdata);

DEFINE_OCFS2_INT_EVENT(ocfs2_dentry_revalidate_ret);

TRACE_EVENT(ocfs2_find_local_alias,
	TP_PROTO(int len, const char *name),
	TP_ARGS(len, name),
	TP_STRUCT__entry(
		__field(int, len)
		__string(name, name)
	),
	TP_fast_assign(
		__entry->len = len;
		__assign_str(name, name);
	),
	TP_printk("%.*s", __entry->len, __get_str(name))
);

TRACE_EVENT(ocfs2_dentry_attach_lock,
	TP_PROTO(int len, const char *name,
		 unsigned long long parent, void *fsdata),
	TP_ARGS(len, name, parent, fsdata),
	TP_STRUCT__entry(
		__field(int, len)
		__string(name, name)
		__field(unsigned long long, parent)
		__field(void *, fsdata)
	),
	TP_fast_assign(
		__entry->len = len;
		__assign_str(name, name);
		__entry->parent = parent;
		__entry->fsdata = fsdata;
	),
	TP_printk("%.*s %llu %p", __entry->len, __get_str(name),
		  __entry->parent, __entry->fsdata)
);

TRACE_EVENT(ocfs2_dentry_attach_lock_found,
	TP_PROTO(const char *name, unsigned long long parent,
		 unsigned long long ino),
	TP_ARGS(name, parent, ino),
	TP_STRUCT__entry(
		__string(name, name)
		__field(unsigned long long, parent)
		__field(unsigned long long, ino)
	),
	TP_fast_assign(
		__assign_str(name, name);
		__entry->parent = parent;
		__entry->ino = ino;
	),
	TP_printk("%s %llu %llu", __get_str(name), __entry->parent, __entry->ino)
);
/* End of trace events for fs/ocfs2/dcache.c. */

/* Trace events for fs/ocfs2/export.c. */

TRACE_EVENT(ocfs2_get_dentry_begin,
	TP_PROTO(void *sb, void *handle, unsigned long long blkno),
	TP_ARGS(sb, handle, blkno),
	TP_STRUCT__entry(
		__field(void *, sb)
		__field(void *, handle)
		__field(unsigned long long, blkno)
	),
	TP_fast_assign(
		__entry->sb = sb;
		__entry->handle = handle;
		__entry->blkno = blkno;
	),
	TP_printk("%p %p %llu", __entry->sb, __entry->handle, __entry->blkno)
);

DEFINE_OCFS2_INT_INT_EVENT(ocfs2_get_dentry_test_bit);

DEFINE_OCFS2_ULL_UINT_EVENT(ocfs2_get_dentry_stale);

DEFINE_OCFS2_ULL_UINT_UINT_EVENT(ocfs2_get_dentry_generation);

DEFINE_OCFS2_POINTER_EVENT(ocfs2_get_dentry_end);

TRACE_EVENT(ocfs2_get_parent,
	TP_PROTO(void *child, int len, const char *name,
		 unsigned long long ino),
	TP_ARGS(child, len, name, ino),
	TP_STRUCT__entry(
		__field(void *,	child)
		__field(int, len)
		__string(name, name)
		__field(unsigned long long, ino)
	),
	TP_fast_assign(
		__entry->child = child;
		__entry->len = len;
		__assign_str(name, name);
		__entry->ino = ino;
	),
	TP_printk("%p %.*s %llu", __entry->child, __entry->len,
		  __get_str(name), __entry->ino)
);

DEFINE_OCFS2_POINTER_EVENT(ocfs2_get_parent_end);

TRACE_EVENT(ocfs2_encode_fh_begin,
	TP_PROTO(void *dentry, int name_len, const char *name,
		 void *fh, int len, int connectable),
	TP_ARGS(dentry, name_len, name, fh, len, connectable),
	TP_STRUCT__entry(
		__field(void *, dentry)
		__field(int, name_len)
		__string(name, name)
		__field(void *, fh)
		__field(int, len)
		__field(int, connectable)
	),
	TP_fast_assign(
		__entry->dentry = dentry;
		__entry->name_len = name_len;
		__assign_str(name, name);
		__entry->fh = fh;
		__entry->len = len;
		__entry->connectable = connectable;
	),
	TP_printk("%p %.*s %p %d %d", __entry->dentry, __entry->name_len,
		  __get_str(name), __entry->fh, __entry->len,
		  __entry->connectable)
);

DEFINE_OCFS2_ULL_UINT_EVENT(ocfs2_encode_fh_self);

DEFINE_OCFS2_ULL_UINT_EVENT(ocfs2_encode_fh_parent);

DEFINE_OCFS2_INT_EVENT(ocfs2_encode_fh_type);

/* End of trace events for fs/ocfs2/export.c. */

/* Trace events for fs/ocfs2/journal.c. */

DEFINE_OCFS2_UINT_EVENT(ocfs2_commit_cache_begin);

DEFINE_OCFS2_ULL_UINT_EVENT(ocfs2_commit_cache_end);

DEFINE_OCFS2_INT_INT_EVENT(ocfs2_extend_trans);

DEFINE_OCFS2_INT_EVENT(ocfs2_extend_trans_restart);

DEFINE_OCFS2_INT_INT_EVENT(ocfs2_allocate_extend_trans);

DEFINE_OCFS2_ULL_ULL_UINT_UINT_EVENT(ocfs2_journal_access);

DEFINE_OCFS2_ULL_EVENT(ocfs2_journal_dirty);

DEFINE_OCFS2_ULL_ULL_UINT_EVENT(ocfs2_journal_init);

DEFINE_OCFS2_UINT_EVENT(ocfs2_journal_init_maxlen);

DEFINE_OCFS2_INT_EVENT(ocfs2_journal_shutdown);

DEFINE_OCFS2_POINTER_EVENT(ocfs2_journal_shutdown_wait);

DEFINE_OCFS2_ULL_EVENT(ocfs2_complete_recovery);

DEFINE_OCFS2_INT_EVENT(ocfs2_complete_recovery_end);

TRACE_EVENT(ocfs2_complete_recovery_slot,
	TP_PROTO(int slot, unsigned long long la_ino,
		 unsigned long long tl_ino, void *qrec),
	TP_ARGS(slot, la_ino, tl_ino, qrec),
	TP_STRUCT__entry(
		__field(int, slot)
		__field(unsigned long long, la_ino)
		__field(unsigned long long, tl_ino)
		__field(void *, qrec)
	),
	TP_fast_assign(
		__entry->slot = slot;
		__entry->la_ino = la_ino;
		__entry->tl_ino = tl_ino;
		__entry->qrec = qrec;
	),
	TP_printk("%d %llu %llu %p", __entry->slot, __entry->la_ino,
		  __entry->tl_ino, __entry->qrec)
);

DEFINE_OCFS2_INT_INT_EVENT(ocfs2_recovery_thread_node);

DEFINE_OCFS2_INT_EVENT(ocfs2_recovery_thread_end);

TRACE_EVENT(ocfs2_recovery_thread,
	TP_PROTO(int node_num, int osb_node_num, int disable,
		 void *recovery_thread, int map_set),
	TP_ARGS(node_num, osb_node_num, disable, recovery_thread, map_set),
	TP_STRUCT__entry(
		__field(int, node_num)
		__field(int, osb_node_num)
		__field(int,disable)
		__field(void *, recovery_thread)
		__field(int,map_set)
	),
	TP_fast_assign(
		__entry->node_num = node_num;
		__entry->osb_node_num = osb_node_num;
		__entry->disable = disable;
		__entry->recovery_thread = recovery_thread;
		__entry->map_set = map_set;
	),
	TP_printk("%d %d %d %p %d", __entry->node_num,
		   __entry->osb_node_num, __entry->disable,
		   __entry->recovery_thread, __entry->map_set)
);

DEFINE_OCFS2_UINT_UINT_UINT_EVENT(ocfs2_replay_journal_recovered);

DEFINE_OCFS2_INT_EVENT(ocfs2_replay_journal_lock_err);

DEFINE_OCFS2_INT_EVENT(ocfs2_replay_journal_skip);

DEFINE_OCFS2_UINT_UINT_UINT_EVENT(ocfs2_recover_node);

DEFINE_OCFS2_UINT_UINT_EVENT(ocfs2_recover_node_skip);

DEFINE_OCFS2_UINT_UINT_EVENT(ocfs2_mark_dead_nodes);

DEFINE_OCFS2_UINT_UINT_UINT_EVENT(ocfs2_queue_orphan_scan_begin);

DEFINE_OCFS2_UINT_UINT_UINT_EVENT(ocfs2_queue_orphan_scan_end);

DEFINE_OCFS2_ULL_EVENT(ocfs2_orphan_filldir);

DEFINE_OCFS2_INT_EVENT(ocfs2_recover_orphans);

DEFINE_OCFS2_ULL_EVENT(ocfs2_recover_orphans_iput);

DEFINE_OCFS2_INT_EVENT(ocfs2_wait_on_mount);

/* End of trace events for fs/ocfs2/journal.c. */

/* Trace events for fs/ocfs2/buffer_head_io.c. */

DEFINE_OCFS2_ULL_UINT_EVENT(ocfs2_read_blocks_sync);

DEFINE_OCFS2_ULL_EVENT(ocfs2_read_blocks_sync_jbd);

DEFINE_OCFS2_ULL_ULL_EVENT(ocfs2_read_blocks_from_disk);

DEFINE_OCFS2_ULL_INT_INT_INT_EVENT(ocfs2_read_blocks_bh);

DEFINE_OCFS2_ULL_INT_INT_INT_EVENT(ocfs2_read_blocks_end);

TRACE_EVENT(ocfs2_write_block,
	TP_PROTO(unsigned long long block, void *ci),
	TP_ARGS(block, ci),
	TP_STRUCT__entry(
		__field(unsigned long long, block)
		__field(void *, ci)
	),
	TP_fast_assign(
		__entry->block = block;
		__entry->ci = ci;
	),
	TP_printk("%llu %p", __entry->block, __entry->ci)
);

TRACE_EVENT(ocfs2_read_blocks_begin,
	TP_PROTO(void *ci, unsigned long long block,
		 unsigned int nr, int flags),
	TP_ARGS(ci, block, nr, flags),
	TP_STRUCT__entry(
		__field(void *, ci)
		__field(unsigned long long, block)
		__field(unsigned int, nr)
		__field(int, flags)
	),
	TP_fast_assign(
		__entry->ci = ci;
		__entry->block = block;
		__entry->nr = nr;
		__entry->flags = flags;
	),
	TP_printk("%p %llu %u %d", __entry->ci, __entry->block,
		  __entry->nr, __entry->flags)
);

/* End of trace events for fs/ocfs2/buffer_head_io.c. */

/* Trace events for fs/ocfs2/uptodate.c. */

DEFINE_OCFS2_ULL_EVENT(ocfs2_purge_copied_metadata_tree);

DEFINE_OCFS2_ULL_UINT_UINT_EVENT(ocfs2_metadata_cache_purge);

DEFINE_OCFS2_ULL_ULL_UINT_EVENT(ocfs2_buffer_cached_begin);

TRACE_EVENT(ocfs2_buffer_cached_end,
	TP_PROTO(int index, void *item),
	TP_ARGS(index, item),
	TP_STRUCT__entry(
		__field(int, index)
		__field(void *, item)
	),
	TP_fast_assign(
		__entry->index = index;
		__entry->item = item;
	),
	TP_printk("%d %p", __entry->index, __entry->item)
);

DEFINE_OCFS2_ULL_ULL_UINT_EVENT(ocfs2_append_cache_array);

DEFINE_OCFS2_ULL_ULL_UINT_EVENT(ocfs2_insert_cache_tree);

DEFINE_OCFS2_ULL_UINT_UINT_EVENT(ocfs2_expand_cache);

DEFINE_OCFS2_ULL_UINT_UINT_EVENT(ocfs2_set_buffer_uptodate);

DEFINE_OCFS2_ULL_ULL_EVENT(ocfs2_set_buffer_uptodate_begin);

DEFINE_OCFS2_ULL_UINT_UINT_EVENT(ocfs2_remove_metadata_array);

DEFINE_OCFS2_ULL_ULL_EVENT(ocfs2_remove_metadata_tree);

DEFINE_OCFS2_ULL_ULL_UINT_UINT_EVENT(ocfs2_remove_block_from_cache);

/* End of trace events for fs/ocfs2/uptodate.c. */
#endif /* _TRACE_OCFS2_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE ocfs2_trace
#include <trace/define_trace.h>
