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
		__field(dev_t,		dev			)
	),

	TP_fast_assign(
		__entry->dev		= c->dev;
	),

	TP_printk("%d,%d", MAJOR(__entry->dev), MINOR(__entry->dev))
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

TRACE_EVENT(journal_reclaim_start,
	TP_PROTO(struct bch_fs *c, u64 min_nr,
		 u64 prereserved, u64 prereserved_total,
		 u64 btree_cache_dirty, u64 btree_cache_total,
		 u64 btree_key_cache_dirty, u64 btree_key_cache_total),
	TP_ARGS(c, min_nr, prereserved, prereserved_total,
		btree_cache_dirty, btree_cache_total,
		btree_key_cache_dirty, btree_key_cache_total),

	TP_STRUCT__entry(
		__field(dev_t,		dev			)
		__field(u64,		min_nr			)
		__field(u64,		prereserved		)
		__field(u64,		prereserved_total	)
		__field(u64,		btree_cache_dirty	)
		__field(u64,		btree_cache_total	)
		__field(u64,		btree_key_cache_dirty	)
		__field(u64,		btree_key_cache_total	)
	),

	TP_fast_assign(
		__entry->dev			= c->dev;
		__entry->min_nr			= min_nr;
		__entry->prereserved		= prereserved;
		__entry->prereserved_total	= prereserved_total;
		__entry->btree_cache_dirty	= btree_cache_dirty;
		__entry->btree_cache_total	= btree_cache_total;
		__entry->btree_key_cache_dirty	= btree_key_cache_dirty;
		__entry->btree_key_cache_total	= btree_key_cache_total;
	),

	TP_printk("%d,%d min %llu prereserved %llu/%llu btree cache %llu/%llu key cache %llu/%llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->min_nr,
		  __entry->prereserved,
		  __entry->prereserved_total,
		  __entry->btree_cache_dirty,
		  __entry->btree_cache_total,
		  __entry->btree_key_cache_dirty,
		  __entry->btree_key_cache_total)
);

TRACE_EVENT(journal_reclaim_finish,
	TP_PROTO(struct bch_fs *c, u64 nr_flushed),
	TP_ARGS(c, nr_flushed),

	TP_STRUCT__entry(
		__field(dev_t,		dev			)
		__field(u64,		nr_flushed		)
	),

	TP_fast_assign(
		__entry->dev		= c->dev;
		__entry->nr_flushed	= nr_flushed;
	),

	TP_printk("%d%d flushed %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->nr_flushed)
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
		__field(dev_t,		dev			)
		__field(u8,		level			)
		__field(u8,		id			)
		__field(u64,		inode			)
		__field(u64,		offset			)
	),

	TP_fast_assign(
		__entry->dev		= c->dev;
		__entry->level		= b->c.level;
		__entry->id		= b->c.btree_id;
		__entry->inode		= b->key.k.p.inode;
		__entry->offset		= b->key.k.p.offset;
	),

	TP_printk("%d,%d  %u id %u %llu:%llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->level, __entry->id,
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

DEFINE_EVENT(bch_fs, btree_node_cannibalize_lock_fail,
	TP_PROTO(struct bch_fs *c),
	TP_ARGS(c)
);

DEFINE_EVENT(bch_fs, btree_node_cannibalize_lock,
	TP_PROTO(struct bch_fs *c),
	TP_ARGS(c)
);

DEFINE_EVENT(bch_fs, btree_node_cannibalize,
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
		__field(dev_t,		dev			)
		__field(size_t,			required	)
		__field(struct closure *,	cl		)
	),

	TP_fast_assign(
		__entry->dev		= c->dev;
		__entry->required = required;
		__entry->cl = cl;
	),

	TP_printk("%d,%d required %zu by %p",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->required, __entry->cl)
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

TRACE_EVENT(btree_cache_scan,
	TP_PROTO(unsigned long nr_to_scan_pages,
		 unsigned long nr_to_scan_nodes,
		 unsigned long can_free_nodes,
		 long ret),
	TP_ARGS(nr_to_scan_pages, nr_to_scan_nodes, can_free_nodes, ret),

	TP_STRUCT__entry(
		__field(unsigned long,	nr_to_scan_pages	)
		__field(unsigned long,	nr_to_scan_nodes	)
		__field(unsigned long,	can_free_nodes		)
		__field(long,		ret			)
	),

	TP_fast_assign(
		__entry->nr_to_scan_pages	= nr_to_scan_pages;
		__entry->nr_to_scan_nodes	= nr_to_scan_nodes;
		__entry->can_free_nodes		= can_free_nodes;
		__entry->ret			= ret;
	),

	TP_printk("scanned for %lu pages, %lu nodes, can free %lu nodes, ret %li",
		  __entry->nr_to_scan_pages,
		  __entry->nr_to_scan_nodes,
		  __entry->can_free_nodes,
		  __entry->ret)
);

TRACE_EVENT(btree_node_relock_fail,
	TP_PROTO(const char *trans_fn,
		 unsigned long caller_ip,
		 enum btree_id btree_id,
		 struct bpos *pos,
		 unsigned long node,
		 u32 iter_lock_seq,
		 u32 node_lock_seq),
	TP_ARGS(trans_fn, caller_ip, btree_id, pos, node, iter_lock_seq, node_lock_seq),

	TP_STRUCT__entry(
		__array(char,			trans_fn, 24	)
		__field(unsigned long,		caller_ip	)
		__field(u8,			btree_id	)
		__field(u64,			pos_inode	)
		__field(u64,			pos_offset	)
		__field(u32,			pos_snapshot	)
		__field(unsigned long,		node		)
		__field(u32,			iter_lock_seq	)
		__field(u32,			node_lock_seq	)
	),

	TP_fast_assign(
		strncpy(__entry->trans_fn, trans_fn, sizeof(__entry->trans_fn));
		__entry->caller_ip		= caller_ip;
		__entry->btree_id		= btree_id;
		__entry->pos_inode		= pos->inode;
		__entry->pos_offset		= pos->offset;
		__entry->pos_snapshot		= pos->snapshot;
		__entry->node			= node;
		__entry->iter_lock_seq		= iter_lock_seq;
		__entry->node_lock_seq		= node_lock_seq;
	),

	TP_printk("%s %pS btree %u pos %llu:%llu:%u, node %lu iter seq %u lock seq %u",
		  __entry->trans_fn,
		  (void *) __entry->caller_ip,
		  __entry->btree_id,
		  __entry->pos_inode,
		  __entry->pos_offset,
		  __entry->pos_snapshot,
		  __entry->node,
		  __entry->iter_lock_seq,
		  __entry->node_lock_seq)
);

/* Garbage collection */

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

DEFINE_EVENT(bch_fs, gc_cannot_inc_gens,
	TP_PROTO(struct bch_fs *c),
	TP_ARGS(c)
);

/* Allocator */

TRACE_EVENT(alloc_scan,
	TP_PROTO(struct bch_dev *ca, u64 found, u64 inc_gen, u64 inc_gen_skipped),
	TP_ARGS(ca, found, inc_gen, inc_gen_skipped),

	TP_STRUCT__entry(
		__field(dev_t,		dev		)
		__field(u64,		found		)
		__field(u64,		inc_gen		)
		__field(u64,		inc_gen_skipped	)
	),

	TP_fast_assign(
		__entry->dev		= ca->dev;
		__entry->found		= found;
		__entry->inc_gen	= inc_gen;
		__entry->inc_gen_skipped = inc_gen_skipped;
	),

	TP_printk("%d,%d found %llu inc_gen %llu inc_gen_skipped %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->found, __entry->inc_gen, __entry->inc_gen_skipped)
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
		__entry->dev		= ca->dev;
		__entry->offset		= offset,
		__entry->sectors	= sectors;
	),

	TP_printk("invalidated %u sectors at %d,%d sector=%llu",
		  __entry->sectors,
		  MAJOR(__entry->dev),
		  MINOR(__entry->dev),
		  __entry->offset)
);

DECLARE_EVENT_CLASS(bucket_alloc,
	TP_PROTO(struct bch_dev *ca, enum alloc_reserve reserve),
	TP_ARGS(ca, reserve),

	TP_STRUCT__entry(
		__field(dev_t,			dev	)
		__field(enum alloc_reserve,	reserve	)
	),

	TP_fast_assign(
		__entry->dev		= ca->dev;
		__entry->reserve	= reserve;
	),

	TP_printk("%d,%d reserve %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->reserve)
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
		__field(dev_t,		dev			)
		__field(u64,		sectors_moved	)
		__field(u64,		keys_moved	)
	),

	TP_fast_assign(
		__entry->dev			= c->dev;
		__entry->sectors_moved = sectors_moved;
		__entry->keys_moved = keys_moved;
	),

	TP_printk("%d,%d sectors_moved %llu keys_moved %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->sectors_moved, __entry->keys_moved)
);

TRACE_EVENT(copygc,
	TP_PROTO(struct bch_fs *c,
		 u64 sectors_moved, u64 sectors_not_moved,
		 u64 buckets_moved, u64 buckets_not_moved),
	TP_ARGS(c,
		sectors_moved, sectors_not_moved,
		buckets_moved, buckets_not_moved),

	TP_STRUCT__entry(
		__field(dev_t,		dev			)
		__field(u64,		sectors_moved		)
		__field(u64,		sectors_not_moved	)
		__field(u64,		buckets_moved		)
		__field(u64,		buckets_not_moved	)
	),

	TP_fast_assign(
		__entry->dev			= c->dev;
		__entry->sectors_moved		= sectors_moved;
		__entry->sectors_not_moved	= sectors_not_moved;
		__entry->buckets_moved		= buckets_moved;
		__entry->buckets_not_moved = buckets_moved;
	),

	TP_printk("%d,%d sectors moved %llu remain %llu buckets moved %llu remain %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->sectors_moved, __entry->sectors_not_moved,
		  __entry->buckets_moved, __entry->buckets_not_moved)
);

TRACE_EVENT(copygc_wait,
	TP_PROTO(struct bch_fs *c,
		 u64 wait_amount, u64 until),
	TP_ARGS(c, wait_amount, until),

	TP_STRUCT__entry(
		__field(dev_t,		dev			)
		__field(u64,		wait_amount		)
		__field(u64,		until			)
	),

	TP_fast_assign(
		__entry->dev		= c->dev;
		__entry->wait_amount	= wait_amount;
		__entry->until		= until;
	),

	TP_printk("%d,%u waiting for %llu sectors until %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->wait_amount, __entry->until)
);

DECLARE_EVENT_CLASS(transaction_restart,
	TP_PROTO(const char *trans_fn,
		 unsigned long caller_ip),
	TP_ARGS(trans_fn, caller_ip),

	TP_STRUCT__entry(
		__array(char,			trans_fn, 24	)
		__field(unsigned long,		caller_ip	)
	),

	TP_fast_assign(
		strncpy(__entry->trans_fn, trans_fn, sizeof(__entry->trans_fn));
		__entry->caller_ip		= caller_ip;
	),

	TP_printk("%s %pS", __entry->trans_fn, (void *) __entry->caller_ip)
);

DEFINE_EVENT(transaction_restart,	transaction_restart_ip,
	TP_PROTO(const char *trans_fn,
		 unsigned long caller_ip),
	TP_ARGS(trans_fn, caller_ip)
);

DEFINE_EVENT(transaction_restart,	trans_blocked_journal_reclaim,
	TP_PROTO(const char *trans_fn,
		 unsigned long caller_ip),
	TP_ARGS(trans_fn, caller_ip)
);

DEFINE_EVENT(transaction_restart,	trans_restart_journal_res_get,
	TP_PROTO(const char *trans_fn,
		 unsigned long caller_ip),
	TP_ARGS(trans_fn, caller_ip)
);

DEFINE_EVENT(transaction_restart,	trans_restart_journal_preres_get,
	TP_PROTO(const char *trans_fn,
		 unsigned long caller_ip),
	TP_ARGS(trans_fn, caller_ip)
);

DEFINE_EVENT(transaction_restart,	trans_restart_journal_reclaim,
	TP_PROTO(const char *trans_fn,
		 unsigned long caller_ip),
	TP_ARGS(trans_fn, caller_ip)
);

DEFINE_EVENT(transaction_restart,	trans_restart_fault_inject,
	TP_PROTO(const char *trans_fn,
		 unsigned long caller_ip),
	TP_ARGS(trans_fn, caller_ip)
);

DEFINE_EVENT(transaction_restart,	trans_traverse_all,
	TP_PROTO(const char *trans_fn,
		 unsigned long caller_ip),
	TP_ARGS(trans_fn, caller_ip)
);

DEFINE_EVENT(transaction_restart,	trans_restart_mark_replicas,
	TP_PROTO(const char *trans_fn,
		 unsigned long caller_ip),
	TP_ARGS(trans_fn, caller_ip)
);

DEFINE_EVENT(transaction_restart,	trans_restart_key_cache_raced,
	TP_PROTO(const char *trans_fn,
		 unsigned long caller_ip),
	TP_ARGS(trans_fn, caller_ip)
);

DECLARE_EVENT_CLASS(transaction_restart_iter,
	TP_PROTO(const char *trans_fn,
		 unsigned long caller_ip,
		 enum btree_id btree_id,
		 struct bpos *pos),
	TP_ARGS(trans_fn, caller_ip, btree_id, pos),

	TP_STRUCT__entry(
		__array(char,			trans_fn, 24	)
		__field(unsigned long,		caller_ip	)
		__field(u8,			btree_id	)
		__field(u64,			pos_inode	)
		__field(u64,			pos_offset	)
		__field(u32,			pos_snapshot	)
	),

	TP_fast_assign(
		strncpy(__entry->trans_fn, trans_fn, sizeof(__entry->trans_fn));
		__entry->caller_ip		= caller_ip;
		__entry->btree_id		= btree_id;
		__entry->pos_inode		= pos->inode;
		__entry->pos_offset		= pos->offset;
		__entry->pos_snapshot		= pos->snapshot;
	),

	TP_printk("%s %pS btree %u pos %llu:%llu:%u",
		  __entry->trans_fn,
		  (void *) __entry->caller_ip,
		  __entry->btree_id,
		  __entry->pos_inode,
		  __entry->pos_offset,
		  __entry->pos_snapshot)
);

DEFINE_EVENT(transaction_restart_iter,	trans_restart_btree_node_reused,
	TP_PROTO(const char *trans_fn,
		 unsigned long caller_ip,
		 enum btree_id btree_id,
		 struct bpos *pos),
	TP_ARGS(trans_fn, caller_ip, btree_id, pos)
);

DEFINE_EVENT(transaction_restart_iter,	trans_restart_btree_node_split,
	TP_PROTO(const char *trans_fn,
		 unsigned long caller_ip,
		 enum btree_id btree_id,
		 struct bpos *pos),
	TP_ARGS(trans_fn, caller_ip, btree_id, pos)
);

DEFINE_EVENT(transaction_restart_iter,	trans_restart_mark,
	TP_PROTO(const char *trans_fn,
		 unsigned long caller_ip,
		 enum btree_id btree_id,
		 struct bpos *pos),
	TP_ARGS(trans_fn, caller_ip, btree_id, pos)
);

DEFINE_EVENT(transaction_restart_iter,	trans_restart_upgrade,
	TP_PROTO(const char *trans_fn,
		 unsigned long caller_ip,
		 enum btree_id btree_id,
		 struct bpos *pos),
	TP_ARGS(trans_fn, caller_ip, btree_id, pos)
);

DEFINE_EVENT(transaction_restart_iter,	trans_restart_iter_upgrade,
	TP_PROTO(const char *trans_fn,
		 unsigned long caller_ip,
		 enum btree_id btree_id,
		 struct bpos *pos),
	TP_ARGS(trans_fn, caller_ip, btree_id, pos)
);

DEFINE_EVENT(transaction_restart_iter,	trans_restart_relock,
	TP_PROTO(const char *trans_fn,
		 unsigned long caller_ip,
		 enum btree_id btree_id,
		 struct bpos *pos),
	TP_ARGS(trans_fn, caller_ip, btree_id, pos)
);

DEFINE_EVENT(transaction_restart_iter,	trans_restart_relock_next_node,
	TP_PROTO(const char *trans_fn,
		 unsigned long caller_ip,
		 enum btree_id btree_id,
		 struct bpos *pos),
	TP_ARGS(trans_fn, caller_ip, btree_id, pos)
);

DEFINE_EVENT(transaction_restart_iter,	trans_restart_relock_parent_for_fill,
	TP_PROTO(const char *trans_fn,
		 unsigned long caller_ip,
		 enum btree_id btree_id,
		 struct bpos *pos),
	TP_ARGS(trans_fn, caller_ip, btree_id, pos)
);

DEFINE_EVENT(transaction_restart_iter,	trans_restart_relock_after_fill,
	TP_PROTO(const char *trans_fn,
		 unsigned long caller_ip,
		 enum btree_id btree_id,
		 struct bpos *pos),
	TP_ARGS(trans_fn, caller_ip, btree_id, pos)
);

DEFINE_EVENT(transaction_restart_iter,	trans_restart_relock_key_cache_fill,
	TP_PROTO(const char *trans_fn,
		 unsigned long caller_ip,
		 enum btree_id btree_id,
		 struct bpos *pos),
	TP_ARGS(trans_fn, caller_ip, btree_id, pos)
);

DEFINE_EVENT(transaction_restart_iter,	trans_restart_relock_path,
	TP_PROTO(const char *trans_fn,
		 unsigned long caller_ip,
		 enum btree_id btree_id,
		 struct bpos *pos),
	TP_ARGS(trans_fn, caller_ip, btree_id, pos)
);

DEFINE_EVENT(transaction_restart_iter,	trans_restart_relock_path_intent,
	TP_PROTO(const char *trans_fn,
		 unsigned long caller_ip,
		 enum btree_id btree_id,
		 struct bpos *pos),
	TP_ARGS(trans_fn, caller_ip, btree_id, pos)
);

DEFINE_EVENT(transaction_restart_iter,	trans_restart_traverse,
	TP_PROTO(const char *trans_fn,
		 unsigned long caller_ip,
		 enum btree_id btree_id,
		 struct bpos *pos),
	TP_ARGS(trans_fn, caller_ip, btree_id, pos)
);

DEFINE_EVENT(transaction_restart_iter,	trans_restart_memory_allocation_failure,
	TP_PROTO(const char *trans_fn,
		 unsigned long caller_ip,
		 enum btree_id btree_id,
		 struct bpos *pos),
	TP_ARGS(trans_fn, caller_ip, btree_id, pos)
);

TRACE_EVENT(trans_restart_would_deadlock,
	TP_PROTO(const char *trans_fn,
		 unsigned long	caller_ip,
		 bool		in_traverse_all,
		 unsigned	reason,
		 enum btree_id	have_btree_id,
		 unsigned	have_iter_type,
		 struct bpos	*have_pos,
		 enum btree_id	want_btree_id,
		 unsigned	want_iter_type,
		 struct bpos	*want_pos),
	TP_ARGS(trans_fn, caller_ip, in_traverse_all, reason,
		have_btree_id, have_iter_type, have_pos,
		want_btree_id, want_iter_type, want_pos),

	TP_STRUCT__entry(
		__array(char,			trans_fn, 24	)
		__field(unsigned long,		caller_ip	)
		__field(u8,			in_traverse_all	)
		__field(u8,			reason		)
		__field(u8,			have_btree_id	)
		__field(u8,			have_iter_type	)
		__field(u8,			want_btree_id	)
		__field(u8,			want_iter_type	)

		__field(u64,			have_pos_inode	)
		__field(u64,			have_pos_offset	)
		__field(u32,			have_pos_snapshot)
		__field(u32,			want_pos_snapshot)
		__field(u64,			want_pos_inode	)
		__field(u64,			want_pos_offset	)
	),

	TP_fast_assign(
		strncpy(__entry->trans_fn, trans_fn, sizeof(__entry->trans_fn));
		__entry->caller_ip		= caller_ip;
		__entry->in_traverse_all	= in_traverse_all;
		__entry->reason			= reason;
		__entry->have_btree_id		= have_btree_id;
		__entry->have_iter_type		= have_iter_type;
		__entry->want_btree_id		= want_btree_id;
		__entry->want_iter_type		= want_iter_type;

		__entry->have_pos_inode		= have_pos->inode;
		__entry->have_pos_offset	= have_pos->offset;
		__entry->have_pos_snapshot	= have_pos->snapshot;

		__entry->want_pos_inode		= want_pos->inode;
		__entry->want_pos_offset	= want_pos->offset;
		__entry->want_pos_snapshot	= want_pos->snapshot;
	),

	TP_printk("%s %pS traverse_all %u because %u have %u:%u %llu:%llu:%u want %u:%u %llu:%llu:%u",
		  __entry->trans_fn,
		  (void *) __entry->caller_ip,
		  __entry->in_traverse_all,
		  __entry->reason,
		  __entry->have_btree_id,
		  __entry->have_iter_type,
		  __entry->have_pos_inode,
		  __entry->have_pos_offset,
		  __entry->have_pos_snapshot,
		  __entry->want_btree_id,
		  __entry->want_iter_type,
		  __entry->want_pos_inode,
		  __entry->want_pos_offset,
		  __entry->want_pos_snapshot)
);

TRACE_EVENT(trans_restart_would_deadlock_write,
	TP_PROTO(const char *trans_fn),
	TP_ARGS(trans_fn),

	TP_STRUCT__entry(
		__array(char,			trans_fn, 24	)
	),

	TP_fast_assign(
		strncpy(__entry->trans_fn, trans_fn, sizeof(__entry->trans_fn));
	),

	TP_printk("%s", __entry->trans_fn)
);

TRACE_EVENT(trans_restart_mem_realloced,
	TP_PROTO(const char *trans_fn,
		 unsigned long caller_ip,
		 unsigned long bytes),
	TP_ARGS(trans_fn, caller_ip, bytes),

	TP_STRUCT__entry(
		__array(char,			trans_fn, 24	)
		__field(unsigned long,		caller_ip	)
		__field(unsigned long,		bytes		)
	),

	TP_fast_assign(
		strncpy(__entry->trans_fn, trans_fn, sizeof(__entry->trans_fn));
		__entry->caller_ip	= caller_ip;
		__entry->bytes		= bytes;
	),

	TP_printk("%s %pS bytes %lu",
		  __entry->trans_fn,
		  (void *) __entry->caller_ip,
		  __entry->bytes)
);

#endif /* _TRACE_BCACHEFS_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../fs/bcachefs

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

#include <trace/define_trace.h>
