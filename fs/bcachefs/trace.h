/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM bcachefs

#if !defined(_TRACE_BCACHEFS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_BCACHEFS_H

#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(bpos,
	TP_PROTO(struct bpos *p),
	TP_ARGS(p),

	TP_STRUCT__entry(
		__field(u64,	inode				)
		__field(u64,	offset				)
	),

	TP_fast_assign(
		__entry->inode	= p->inode;
		__entry->offset	= p->offset;
	),

	TP_printk("%llu:%llu", __entry->inode, __entry->offset)
);

DECLARE_EVENT_CLASS(bkey,
	TP_PROTO(const struct bkey *k),
	TP_ARGS(k),

	TP_STRUCT__entry(
		__field(u64,	inode				)
		__field(u64,	offset				)
		__field(u32,	size				)
	),

	TP_fast_assign(
		__entry->inode	= k->p.inode;
		__entry->offset	= k->p.offset;
		__entry->size	= k->size;
	),

	TP_printk("%llu:%llu len %u", __entry->inode,
		  __entry->offset, __entry->size)
);

DECLARE_EVENT_CLASS(bch_fs,
	TP_PROTO(struct bch_fs *c),
	TP_ARGS(c),

	TP_STRUCT__entry(
		__array(char,		uuid,	16 )
	),

	TP_fast_assign(
		memcpy(__entry->uuid, c->sb.user_uuid.b, 16);
	),

	TP_printk("%pU", __entry->uuid)
);

DECLARE_EVENT_CLASS(bio,
	TP_PROTO(struct bio *bio),
	TP_ARGS(bio),

	TP_STRUCT__entry(
		__field(dev_t,		dev			)
		__field(sector_t,	sector			)
		__field(unsigned int,	nr_sector		)
		__array(char,		rwbs,	6		)
	),

	TP_fast_assign(
		__entry->dev		= bio->bi_bdev ? bio_dev(bio) : 0;
		__entry->sector		= bio->bi_iter.bi_sector;
		__entry->nr_sector	= bio->bi_iter.bi_size >> 9;
		blk_fill_rwbs(__entry->rwbs, bio->bi_opf);
	),

	TP_printk("%d,%d  %s %llu + %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev), __entry->rwbs,
		  (unsigned long long)__entry->sector, __entry->nr_sector)
);

/* io.c: */

DEFINE_EVENT(bio, read_split,
	TP_PROTO(struct bio *bio),
	TP_ARGS(bio)
);

DEFINE_EVENT(bio, read_bounce,
	TP_PROTO(struct bio *bio),
	TP_ARGS(bio)
);

DEFINE_EVENT(bio, read_retry,
	TP_PROTO(struct bio *bio),
	TP_ARGS(bio)
);

DEFINE_EVENT(bio, promote,
	TP_PROTO(struct bio *bio),
	TP_ARGS(bio)
);

/* Journal */

DEFINE_EVENT(bch_fs, journal_full,
	TP_PROTO(struct bch_fs *c),
	TP_ARGS(c)
);

DEFINE_EVENT(bch_fs, journal_entry_full,
	TP_PROTO(struct bch_fs *c),
	TP_ARGS(c)
);

DEFINE_EVENT(bio, journal_write,
	TP_PROTO(struct bio *bio),
	TP_ARGS(bio)
);

/* bset.c: */

DEFINE_EVENT(bpos, bkey_pack_pos_fail,
	TP_PROTO(struct bpos *p),
	TP_ARGS(p)
);

/* Btree */

DECLARE_EVENT_CLASS(btree_node,
	TP_PROTO(struct bch_fs *c, struct btree *b),
	TP_ARGS(c, b),

	TP_STRUCT__entry(
		__array(char,		uuid,		16	)
		__field(u8,		level			)
		__field(u8,		id			)
		__field(u64,		inode			)
		__field(u64,		offset			)
	),

	TP_fast_assign(
		memcpy(__entry->uuid, c->sb.user_uuid.b, 16);
		__entry->level		= b->c.level;
		__entry->id		= b->c.btree_id;
		__entry->inode		= b->key.k.p.inode;
		__entry->offset		= b->key.k.p.offset;
	),

	TP_printk("%pU  %u id %u %llu:%llu",
		  __entry->uuid, __entry->level, __entry->id,
		  __entry->inode, __entry->offset)
);

DEFINE_EVENT(btree_node, btree_read,
	TP_PROTO(struct bch_fs *c, struct btree *b),
	TP_ARGS(c, b)
);

TRACE_EVENT(btree_write,
	TP_PROTO(struct btree *b, unsigned bytes, unsigned sectors),
	TP_ARGS(b, bytes, sectors),

	TP_STRUCT__entry(
		__field(enum btree_node_type,	type)
		__field(unsigned,	bytes			)
		__field(unsigned,	sectors			)
	),

	TP_fast_assign(
		__entry->type	= btree_node_type(b);
		__entry->bytes	= bytes;
		__entry->sectors = sectors;
	),

	TP_printk("bkey type %u bytes %u sectors %u",
		  __entry->type , __entry->bytes, __entry->sectors)
);

DEFINE_EVENT(btree_node, btree_node_alloc,
	TP_PROTO(struct bch_fs *c, struct btree *b),
	TP_ARGS(c, b)
);

DEFINE_EVENT(btree_node, btree_node_free,
	TP_PROTO(struct bch_fs *c, struct btree *b),
	TP_ARGS(c, b)
);

DEFINE_EVENT(btree_node, btree_node_reap,
	TP_PROTO(struct bch_fs *c, struct btree *b),
	TP_ARGS(c, b)
);

DECLARE_EVENT_CLASS(btree_node_cannibalize_lock,
	TP_PROTO(struct bch_fs *c),
	TP_ARGS(c),

	TP_STRUCT__entry(
		__array(char,			uuid,	16	)
	),

	TP_fast_assign(
		memcpy(__entry->uuid, c->sb.user_uuid.b, 16);
	),

	TP_printk("%pU", __entry->uuid)
);

DEFINE_EVENT(btree_node_cannibalize_lock, btree_node_cannibalize_lock_fail,
	TP_PROTO(struct bch_fs *c),
	TP_ARGS(c)
);

DEFINE_EVENT(btree_node_cannibalize_lock, btree_node_cannibalize_lock,
	TP_PROTO(struct bch_fs *c),
	TP_ARGS(c)
);

DEFINE_EVENT(btree_node_cannibalize_lock, btree_node_cannibalize,
	TP_PROTO(struct bch_fs *c),
	TP_ARGS(c)
);

DEFINE_EVENT(bch_fs, btree_node_cannibalize_unlock,
	TP_PROTO(struct bch_fs *c),
	TP_ARGS(c)
);

TRACE_EVENT(btree_reserve_get_fail,
	TP_PROTO(struct bch_fs *c, size_t required, struct closure *cl),
	TP_ARGS(c, required, cl),

	TP_STRUCT__entry(
		__array(char,			uuid,	16	)
		__field(size_t,			required	)
		__field(struct closure *,	cl		)
	),

	TP_fast_assign(
		memcpy(__entry->uuid, c->sb.user_uuid.b, 16);
		__entry->required = required;
		__entry->cl = cl;
	),

	TP_printk("%pU required %zu by %p", __entry->uuid,
		  __entry->required, __entry->cl)
);

TRACE_EVENT(btree_insert_key,
	TP_PROTO(struct bch_fs *c, struct btree *b, struct bkey_i *k),
	TP_ARGS(c, b, k),

	TP_STRUCT__entry(
		__field(u8,		id			)
		__field(u64,		inode			)
		__field(u64,		offset			)
		__field(u32,		size			)
	),

	TP_fast_assign(
		__entry->id		= b->c.btree_id;
		__entry->inode		= k->k.p.inode;
		__entry->offset		= k->k.p.offset;
		__entry->size		= k->k.size;
	),

	TP_printk("btree %u: %llu:%llu len %u", __entry->id,
		  __entry->inode, __entry->offset, __entry->size)
);

DEFINE_EVENT(btree_node, btree_split,
	TP_PROTO(struct bch_fs *c, struct btree *b),
	TP_ARGS(c, b)
);

DEFINE_EVENT(btree_node, btree_compact,
	TP_PROTO(struct bch_fs *c, struct btree *b),
	TP_ARGS(c, b)
);

DEFINE_EVENT(btree_node, btree_merge,
	TP_PROTO(struct bch_fs *c, struct btree *b),
	TP_ARGS(c, b)
);

DEFINE_EVENT(btree_node, btree_set_root,
	TP_PROTO(struct bch_fs *c, struct btree *b),
	TP_ARGS(c, b)
);

/* Garbage collection */

DEFINE_EVENT(btree_node, btree_gc_coalesce,
	TP_PROTO(struct bch_fs *c, struct btree *b),
	TP_ARGS(c, b)
);

TRACE_EVENT(btree_gc_coalesce_fail,
	TP_PROTO(struct bch_fs *c, int reason),
	TP_ARGS(c, reason),

	TP_STRUCT__entry(
		__field(u8,		reason			)
		__array(char,		uuid,	16		)
	),

	TP_fast_assign(
		__entry->reason		= reason;
		memcpy(__entry->uuid, c->disk_sb.sb->user_uuid.b, 16);
	),

	TP_printk("%pU: %u", __entry->uuid, __entry->reason)
);

DEFINE_EVENT(btree_node, btree_gc_rewrite_node,
	TP_PROTO(struct bch_fs *c, struct btree *b),
	TP_ARGS(c, b)
);

DEFINE_EVENT(btree_node, btree_gc_rewrite_node_fail,
	TP_PROTO(struct bch_fs *c, struct btree *b),
	TP_ARGS(c, b)
);

DEFINE_EVENT(bch_fs, gc_start,
	TP_PROTO(struct bch_fs *c),
	TP_ARGS(c)
);

DEFINE_EVENT(bch_fs, gc_end,
	TP_PROTO(struct bch_fs *c),
	TP_ARGS(c)
);

DEFINE_EVENT(bch_fs, gc_coalesce_start,
	TP_PROTO(struct bch_fs *c),
	TP_ARGS(c)
);

DEFINE_EVENT(bch_fs, gc_coalesce_end,
	TP_PROTO(struct bch_fs *c),
	TP_ARGS(c)
);

DEFINE_EVENT(bch_fs, gc_cannot_inc_gens,
	TP_PROTO(struct bch_fs *c),
	TP_ARGS(c)
);

/* Allocator */

TRACE_EVENT(alloc_batch,
	TP_PROTO(struct bch_dev *ca, size_t free, size_t total),
	TP_ARGS(ca, free, total),

	TP_STRUCT__entry(
		__array(char,		uuid,	16	)
		__field(size_t,		free		)
		__field(size_t,		total		)
	),

	TP_fast_assign(
		memcpy(__entry->uuid, ca->uuid.b, 16);
		__entry->free = free;
		__entry->total = total;
	),

	TP_printk("%pU free %zu total %zu",
		__entry->uuid, __entry->free, __entry->total)
);

TRACE_EVENT(invalidate,
	TP_PROTO(struct bch_dev *ca, u64 offset, unsigned sectors),
	TP_ARGS(ca, offset, sectors),

	TP_STRUCT__entry(
		__field(unsigned,	sectors			)
		__field(dev_t,		dev			)
		__field(__u64,		offset			)
	),

	TP_fast_assign(
		__entry->dev		= ca->disk_sb.bdev->bd_dev;
		__entry->offset		= offset,
		__entry->sectors	= sectors;
	),

	TP_printk("invalidated %u sectors at %d,%d sector=%llu",
		  __entry->sectors, MAJOR(__entry->dev),
		  MINOR(__entry->dev), __entry->offset)
);

DEFINE_EVENT(bch_fs, rescale_prios,
	TP_PROTO(struct bch_fs *c),
	TP_ARGS(c)
);

DECLARE_EVENT_CLASS(bucket_alloc,
	TP_PROTO(struct bch_dev *ca, enum alloc_reserve reserve),
	TP_ARGS(ca, reserve),

	TP_STRUCT__entry(
		__array(char,			uuid,	16)
		__field(enum alloc_reserve,	reserve	  )
	),

	TP_fast_assign(
		memcpy(__entry->uuid, ca->uuid.b, 16);
		__entry->reserve = reserve;
	),

	TP_printk("%pU reserve %d", __entry->uuid, __entry->reserve)
);

DEFINE_EVENT(bucket_alloc, bucket_alloc,
	TP_PROTO(struct bch_dev *ca, enum alloc_reserve reserve),
	TP_ARGS(ca, reserve)
);

DEFINE_EVENT(bucket_alloc, bucket_alloc_fail,
	TP_PROTO(struct bch_dev *ca, enum alloc_reserve reserve),
	TP_ARGS(ca, reserve)
);

DEFINE_EVENT(bucket_alloc, open_bucket_alloc_fail,
	TP_PROTO(struct bch_dev *ca, enum alloc_reserve reserve),
	TP_ARGS(ca, reserve)
);

/* Moving IO */

DEFINE_EVENT(bkey, move_extent,
	TP_PROTO(const struct bkey *k),
	TP_ARGS(k)
);

DEFINE_EVENT(bkey, move_alloc_fail,
	TP_PROTO(const struct bkey *k),
	TP_ARGS(k)
);

DEFINE_EVENT(bkey, move_race,
	TP_PROTO(const struct bkey *k),
	TP_ARGS(k)
);

TRACE_EVENT(move_data,
	TP_PROTO(struct bch_fs *c, u64 sectors_moved,
		 u64 keys_moved),
	TP_ARGS(c, sectors_moved, keys_moved),

	TP_STRUCT__entry(
		__array(char,		uuid,	16	)
		__field(u64,		sectors_moved	)
		__field(u64,		keys_moved	)
	),

	TP_fast_assign(
		memcpy(__entry->uuid, c->sb.user_uuid.b, 16);
		__entry->sectors_moved = sectors_moved;
		__entry->keys_moved = keys_moved;
	),

	TP_printk("%pU sectors_moved %llu keys_moved %llu",
		__entry->uuid, __entry->sectors_moved, __entry->keys_moved)
);

TRACE_EVENT(copygc,
	TP_PROTO(struct bch_dev *ca,
		 u64 sectors_moved, u64 sectors_not_moved,
		 u64 buckets_moved, u64 buckets_not_moved),
	TP_ARGS(ca,
		sectors_moved, sectors_not_moved,
		buckets_moved, buckets_not_moved),

	TP_STRUCT__entry(
		__array(char,		uuid,	16		)
		__field(u64,		sectors_moved		)
		__field(u64,		sectors_not_moved	)
		__field(u64,		buckets_moved		)
		__field(u64,		buckets_not_moved	)
	),

	TP_fast_assign(
		memcpy(__entry->uuid, ca->uuid.b, 16);
		__entry->sectors_moved		= sectors_moved;
		__entry->sectors_not_moved	= sectors_not_moved;
		__entry->buckets_moved		= buckets_moved;
		__entry->buckets_not_moved = buckets_moved;
	),

	TP_printk("%pU sectors moved %llu remain %llu buckets moved %llu remain %llu",
		__entry->uuid,
		__entry->sectors_moved, __entry->sectors_not_moved,
		__entry->buckets_moved, __entry->buckets_not_moved)
);

DECLARE_EVENT_CLASS(transaction_restart,
	TP_PROTO(struct bch_fs *c, unsigned long ip),
	TP_ARGS(c, ip),

	TP_STRUCT__entry(
		__array(char,			name,	16)
		__field(unsigned long,		ip	)
	),

	TP_fast_assign(
		memcpy(__entry->name, c->name, 16);
		__entry->ip = ip;
	),

	TP_printk("%pS", (void *) __entry->ip)
);

DEFINE_EVENT(transaction_restart,	trans_restart_btree_node_reused,
	TP_PROTO(struct bch_fs *c, unsigned long ip),
	TP_ARGS(c, ip)
);

DEFINE_EVENT(transaction_restart,	trans_restart_would_deadlock,
	TP_PROTO(struct bch_fs *c, unsigned long ip),
	TP_ARGS(c, ip)
);

DEFINE_EVENT(transaction_restart,	trans_restart_iters_realloced,
	TP_PROTO(struct bch_fs *c, unsigned long ip),
	TP_ARGS(c, ip)
);

DEFINE_EVENT(transaction_restart,	trans_restart_mem_realloced,
	TP_PROTO(struct bch_fs *c, unsigned long ip),
	TP_ARGS(c, ip)
);

DEFINE_EVENT(transaction_restart,	trans_restart_journal_res_get,
	TP_PROTO(struct bch_fs *c, unsigned long ip),
	TP_ARGS(c, ip)
);

DEFINE_EVENT(transaction_restart,	trans_restart_journal_preres_get,
	TP_PROTO(struct bch_fs *c, unsigned long ip),
	TP_ARGS(c, ip)
);

DEFINE_EVENT(transaction_restart,	trans_restart_mark_replicas,
	TP_PROTO(struct bch_fs *c, unsigned long ip),
	TP_ARGS(c, ip)
);

DEFINE_EVENT(transaction_restart,	trans_restart_fault_inject,
	TP_PROTO(struct bch_fs *c, unsigned long ip),
	TP_ARGS(c, ip)
);

DEFINE_EVENT(transaction_restart,	trans_restart_btree_node_split,
	TP_PROTO(struct bch_fs *c, unsigned long ip),
	TP_ARGS(c, ip)
);

DEFINE_EVENT(transaction_restart,	trans_restart_traverse,
	TP_PROTO(struct bch_fs *c, unsigned long ip),
	TP_ARGS(c, ip)
);

DEFINE_EVENT(transaction_restart,	trans_restart_atomic,
	TP_PROTO(struct bch_fs *c, unsigned long ip),
	TP_ARGS(c, ip)
);

#endif /* _TRACE_BCACHEFS_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../fs/bcachefs

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

#include <trace/define_trace.h>
