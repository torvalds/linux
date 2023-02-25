/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM bcachefs

#if !defined(_TRACE_BCACHEFS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_BCACHEFS_H

#include <linux/tracepoint.h>

#define TRACE_BPOS_entries(name)				\
	__field(u64,			name##_inode	)	\
	__field(u64,			name##_offset	)	\
	__field(u32,			name##_snapshot	)

#define TRACE_BPOS_assign(dst, src)				\
	__entry->dst##_inode		= (src).inode;		\
	__entry->dst##_offset		= (src).offset;		\
	__entry->dst##_snapshot		= (src).snapshot

DECLARE_EVENT_CLASS(bpos,
	TP_PROTO(const struct bpos *p),
	TP_ARGS(p),

	TP_STRUCT__entry(
		TRACE_BPOS_entries(p)
	),

	TP_fast_assign(
		TRACE_BPOS_assign(p, *p);
	),

	TP_printk("%llu:%llu:%u", __entry->p_inode, __entry->p_offset, __entry->p_snapshot)
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

DECLARE_EVENT_CLASS(btree_node,
	TP_PROTO(struct bch_fs *c, struct btree *b),
	TP_ARGS(c, b),

	TP_STRUCT__entry(
		__field(dev_t,		dev			)
		__field(u8,		level			)
		__field(u8,		btree_id		)
		TRACE_BPOS_entries(pos)
	),

	TP_fast_assign(
		__entry->dev		= c->dev;
		__entry->level		= b->c.level;
		__entry->btree_id	= b->c.btree_id;
		TRACE_BPOS_assign(pos, b->key.k.p);
	),

	TP_printk("%d,%d %u %s %llu:%llu:%u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->level,
		  bch2_btree_ids[__entry->btree_id],
		  __entry->pos_inode, __entry->pos_offset, __entry->pos_snapshot)
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

/* super-io.c: */
TRACE_EVENT(write_super,
	TP_PROTO(struct bch_fs *c, unsigned long ip),
	TP_ARGS(c, ip),

	TP_STRUCT__entry(
		__field(dev_t,		dev	)
		__field(unsigned long,	ip	)
	),

	TP_fast_assign(
		__entry->dev		= c->dev;
		__entry->ip		= ip;
	),

	TP_printk("%d,%d for %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (void *) __entry->ip)
);

/* io.c: */

DEFINE_EVENT(bio, read_promote,
	TP_PROTO(struct bio *bio),
	TP_ARGS(bio)
);

DEFINE_EVENT(bio, read_bounce,
	TP_PROTO(struct bio *bio),
	TP_ARGS(bio)
);

DEFINE_EVENT(bio, read_split,
	TP_PROTO(struct bio *bio),
	TP_ARGS(bio)
);

DEFINE_EVENT(bio, read_retry,
	TP_PROTO(struct bio *bio),
	TP_ARGS(bio)
);

DEFINE_EVENT(bio, read_reuse_race,
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
	TP_PROTO(struct bch_fs *c, bool direct, bool kicked,
		 u64 min_nr, u64 min_key_cache,
		 u64 prereserved, u64 prereserved_total,
		 u64 btree_cache_dirty, u64 btree_cache_total,
		 u64 btree_key_cache_dirty, u64 btree_key_cache_total),
	TP_ARGS(c, direct, kicked, min_nr, min_key_cache, prereserved, prereserved_total,
		btree_cache_dirty, btree_cache_total,
		btree_key_cache_dirty, btree_key_cache_total),

	TP_STRUCT__entry(
		__field(dev_t,		dev			)
		__field(bool,		direct			)
		__field(bool,		kicked			)
		__field(u64,		min_nr			)
		__field(u64,		min_key_cache		)
		__field(u64,		prereserved		)
		__field(u64,		prereserved_total	)
		__field(u64,		btree_cache_dirty	)
		__field(u64,		btree_cache_total	)
		__field(u64,		btree_key_cache_dirty	)
		__field(u64,		btree_key_cache_total	)
	),

	TP_fast_assign(
		__entry->dev			= c->dev;
		__entry->direct			= direct;
		__entry->kicked			= kicked;
		__entry->min_nr			= min_nr;
		__entry->min_key_cache		= min_key_cache;
		__entry->prereserved		= prereserved;
		__entry->prereserved_total	= prereserved_total;
		__entry->btree_cache_dirty	= btree_cache_dirty;
		__entry->btree_cache_total	= btree_cache_total;
		__entry->btree_key_cache_dirty	= btree_key_cache_dirty;
		__entry->btree_key_cache_total	= btree_key_cache_total;
	),

	TP_printk("%d,%d direct %u kicked %u min %llu key cache %llu prereserved %llu/%llu btree cache %llu/%llu key cache %llu/%llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->direct,
		  __entry->kicked,
		  __entry->min_nr,
		  __entry->min_key_cache,
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

	TP_printk("%d,%d flushed %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->nr_flushed)
);

/* bset.c: */

DEFINE_EVENT(bpos, bkey_pack_pos_fail,
	TP_PROTO(const struct bpos *p),
	TP_ARGS(p)
);

/* Btree cache: */

TRACE_EVENT(btree_cache_scan,
	TP_PROTO(long nr_to_scan, long can_free, long ret),
	TP_ARGS(nr_to_scan, can_free, ret),

	TP_STRUCT__entry(
		__field(long,	nr_to_scan		)
		__field(long,	can_free		)
		__field(long,	ret			)
	),

	TP_fast_assign(
		__entry->nr_to_scan	= nr_to_scan;
		__entry->can_free	= can_free;
		__entry->ret		= ret;
	),

	TP_printk("scanned for %li nodes, can free %li, ret %li",
		  __entry->nr_to_scan, __entry->can_free, __entry->ret)
);

DEFINE_EVENT(btree_node, btree_cache_reap,
	TP_PROTO(struct bch_fs *c, struct btree *b),
	TP_ARGS(c, b)
);

DEFINE_EVENT(bch_fs, btree_cache_cannibalize_lock_fail,
	TP_PROTO(struct bch_fs *c),
	TP_ARGS(c)
);

DEFINE_EVENT(bch_fs, btree_cache_cannibalize_lock,
	TP_PROTO(struct bch_fs *c),
	TP_ARGS(c)
);

DEFINE_EVENT(bch_fs, btree_cache_cannibalize,
	TP_PROTO(struct bch_fs *c),
	TP_ARGS(c)
);

DEFINE_EVENT(bch_fs, btree_cache_cannibalize_unlock,
	TP_PROTO(struct bch_fs *c),
	TP_ARGS(c)
);

/* Btree */

DEFINE_EVENT(btree_node, btree_node_read,
	TP_PROTO(struct bch_fs *c, struct btree *b),
	TP_ARGS(c, b)
);

TRACE_EVENT(btree_node_write,
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

TRACE_EVENT(btree_reserve_get_fail,
	TP_PROTO(const char *trans_fn,
		 unsigned long caller_ip,
		 size_t required,
		 int ret),
	TP_ARGS(trans_fn, caller_ip, required, ret),

	TP_STRUCT__entry(
		__array(char,			trans_fn, 32	)
		__field(unsigned long,		caller_ip	)
		__field(size_t,			required	)
		__array(char,			ret, 32		)
	),

	TP_fast_assign(
		strscpy(__entry->trans_fn, trans_fn, sizeof(__entry->trans_fn));
		__entry->caller_ip	= caller_ip;
		__entry->required	= required;
		strscpy(__entry->ret, bch2_err_str(ret), sizeof(__entry->ret));
	),

	TP_printk("%s %pS required %zu ret %s",
		  __entry->trans_fn,
		  (void *) __entry->caller_ip,
		  __entry->required,
		  __entry->ret)
);

DEFINE_EVENT(btree_node, btree_node_compact,
	TP_PROTO(struct bch_fs *c, struct btree *b),
	TP_ARGS(c, b)
);

DEFINE_EVENT(btree_node, btree_node_merge,
	TP_PROTO(struct bch_fs *c, struct btree *b),
	TP_ARGS(c, b)
);

DEFINE_EVENT(btree_node, btree_node_split,
	TP_PROTO(struct bch_fs *c, struct btree *b),
	TP_ARGS(c, b)
);

DEFINE_EVENT(btree_node, btree_node_rewrite,
	TP_PROTO(struct bch_fs *c, struct btree *b),
	TP_ARGS(c, b)
);

DEFINE_EVENT(btree_node, btree_node_set_root,
	TP_PROTO(struct bch_fs *c, struct btree *b),
	TP_ARGS(c, b)
);

TRACE_EVENT(btree_path_relock_fail,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 struct btree_path *path,
		 unsigned level),
	TP_ARGS(trans, caller_ip, path, level),

	TP_STRUCT__entry(
		__array(char,			trans_fn, 32	)
		__field(unsigned long,		caller_ip	)
		__field(u8,			btree_id	)
		__field(u8,			level		)
		TRACE_BPOS_entries(pos)
		__array(char,			node, 24	)
		__field(u32,			iter_lock_seq	)
		__field(u32,			node_lock_seq	)
	),

	TP_fast_assign(
		struct btree *b = btree_path_node(path, level);

		strscpy(__entry->trans_fn, trans->fn, sizeof(__entry->trans_fn));
		__entry->caller_ip		= caller_ip;
		__entry->btree_id		= path->btree_id;
		__entry->level			= path->level;
		TRACE_BPOS_assign(pos, path->pos);
		if (IS_ERR(b))
			strscpy(__entry->node, bch2_err_str(PTR_ERR(b)), sizeof(__entry->node));
		else
			scnprintf(__entry->node, sizeof(__entry->node), "%px", b);
		__entry->iter_lock_seq		= path->l[level].lock_seq;
		__entry->node_lock_seq		= is_btree_node(path, level) ? path->l[level].b->c.lock.state.seq : 0;
	),

	TP_printk("%s %pS btree %s pos %llu:%llu:%u level %u node %s iter seq %u lock seq %u",
		  __entry->trans_fn,
		  (void *) __entry->caller_ip,
		  bch2_btree_ids[__entry->btree_id],
		  __entry->pos_inode,
		  __entry->pos_offset,
		  __entry->pos_snapshot,
		  __entry->level,
		  __entry->node,
		  __entry->iter_lock_seq,
		  __entry->node_lock_seq)
);

TRACE_EVENT(btree_path_upgrade_fail,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 struct btree_path *path,
		 unsigned level),
	TP_ARGS(trans, caller_ip, path, level),

	TP_STRUCT__entry(
		__array(char,			trans_fn, 32	)
		__field(unsigned long,		caller_ip	)
		__field(u8,			btree_id	)
		__field(u8,			level		)
		TRACE_BPOS_entries(pos)
		__field(u8,			locked		)
		__field(u8,			self_read_count	)
		__field(u8,			self_intent_count)
		__field(u8,			read_count	)
		__field(u8,			intent_count	)
		__field(u32,			iter_lock_seq	)
		__field(u32,			node_lock_seq	)
	),

	TP_fast_assign(
		struct six_lock_count c;

		strscpy(__entry->trans_fn, trans->fn, sizeof(__entry->trans_fn));
		__entry->caller_ip		= caller_ip;
		__entry->btree_id		= path->btree_id;
		__entry->level			= level;
		TRACE_BPOS_assign(pos, path->pos);
		__entry->locked			= btree_node_locked(path, level);

		c = bch2_btree_node_lock_counts(trans, NULL, &path->l[level].b->c, level),
		__entry->self_read_count	= c.n[SIX_LOCK_read];
		__entry->self_intent_count	= c.n[SIX_LOCK_intent];
		c = six_lock_counts(&path->l[level].b->c.lock);
		__entry->read_count		= c.n[SIX_LOCK_read];
		__entry->intent_count		= c.n[SIX_LOCK_read];
		__entry->iter_lock_seq		= path->l[level].lock_seq;
		__entry->node_lock_seq		= is_btree_node(path, level) ? path->l[level].b->c.lock.state.seq : 0;
	),

	TP_printk("%s %pS btree %s pos %llu:%llu:%u level %u locked %u held %u:%u lock count %u:%u iter seq %u lock seq %u",
		  __entry->trans_fn,
		  (void *) __entry->caller_ip,
		  bch2_btree_ids[__entry->btree_id],
		  __entry->pos_inode,
		  __entry->pos_offset,
		  __entry->pos_snapshot,
		  __entry->level,
		  __entry->locked,
		  __entry->self_read_count,
		  __entry->self_intent_count,
		  __entry->read_count,
		  __entry->intent_count,
		  __entry->iter_lock_seq,
		  __entry->node_lock_seq)
);

/* Garbage collection */

DEFINE_EVENT(bch_fs, gc_gens_start,
	TP_PROTO(struct bch_fs *c),
	TP_ARGS(c)
);

DEFINE_EVENT(bch_fs, gc_gens_end,
	TP_PROTO(struct bch_fs *c),
	TP_ARGS(c)
);

/* Allocator */

DECLARE_EVENT_CLASS(bucket_alloc,
	TP_PROTO(struct bch_dev *ca, const char *alloc_reserve,
		 u64 bucket,
		 u64 free,
		 u64 avail,
		 u64 copygc_wait_amount,
		 s64 copygc_waiting_for,
		 struct bucket_alloc_state *s,
		 bool nonblocking,
		 const char *err),
	TP_ARGS(ca, alloc_reserve, bucket, free, avail,
		copygc_wait_amount, copygc_waiting_for,
		s, nonblocking, err),

	TP_STRUCT__entry(
		__field(u8,			dev			)
		__array(char,	reserve,	16			)
		__field(u64,			bucket	)
		__field(u64,			free			)
		__field(u64,			avail			)
		__field(u64,			copygc_wait_amount	)
		__field(s64,			copygc_waiting_for	)
		__field(u64,			seen			)
		__field(u64,			open			)
		__field(u64,			need_journal_commit	)
		__field(u64,			nouse			)
		__field(bool,			nonblocking		)
		__field(u64,			nocow			)
		__array(char,			err,	32		)
	),

	TP_fast_assign(
		__entry->dev		= ca->dev_idx;
		strscpy(__entry->reserve, alloc_reserve, sizeof(__entry->reserve));
		__entry->bucket		= bucket;
		__entry->free		= free;
		__entry->avail		= avail;
		__entry->copygc_wait_amount	= copygc_wait_amount;
		__entry->copygc_waiting_for	= copygc_waiting_for;
		__entry->seen		= s->buckets_seen;
		__entry->open		= s->skipped_open;
		__entry->need_journal_commit = s->skipped_need_journal_commit;
		__entry->nouse		= s->skipped_nouse;
		__entry->nonblocking	= nonblocking;
		__entry->nocow		= s->skipped_nocow;
		strscpy(__entry->err, err, sizeof(__entry->err));
	),

	TP_printk("reserve %s bucket %u:%llu free %llu avail %llu copygc_wait %llu/%lli seen %llu open %llu need_journal_commit %llu nouse %llu nocow %llu nonblocking %u err %s",
		  __entry->reserve,
		  __entry->dev,
		  __entry->bucket,
		  __entry->free,
		  __entry->avail,
		  __entry->copygc_wait_amount,
		  __entry->copygc_waiting_for,
		  __entry->seen,
		  __entry->open,
		  __entry->need_journal_commit,
		  __entry->nouse,
		  __entry->nocow,
		  __entry->nonblocking,
		  __entry->err)
);

DEFINE_EVENT(bucket_alloc, bucket_alloc,
	TP_PROTO(struct bch_dev *ca, const char *alloc_reserve,
		 u64 bucket,
		 u64 free,
		 u64 avail,
		 u64 copygc_wait_amount,
		 s64 copygc_waiting_for,
		 struct bucket_alloc_state *s,
		 bool nonblocking,
		 const char *err),
	TP_ARGS(ca, alloc_reserve, bucket, free, avail,
		copygc_wait_amount, copygc_waiting_for,
		s, nonblocking, err)
);

DEFINE_EVENT(bucket_alloc, bucket_alloc_fail,
	TP_PROTO(struct bch_dev *ca, const char *alloc_reserve,
		 u64 bucket,
		 u64 free,
		 u64 avail,
		 u64 copygc_wait_amount,
		 s64 copygc_waiting_for,
		 struct bucket_alloc_state *s,
		 bool nonblocking,
		 const char *err),
	TP_ARGS(ca, alloc_reserve, bucket, free, avail,
		copygc_wait_amount, copygc_waiting_for,
		s, nonblocking, err)
);

TRACE_EVENT(discard_buckets,
	TP_PROTO(struct bch_fs *c, u64 seen, u64 open,
		 u64 need_journal_commit, u64 discarded, const char *err),
	TP_ARGS(c, seen, open, need_journal_commit, discarded, err),

	TP_STRUCT__entry(
		__field(dev_t,		dev			)
		__field(u64,		seen			)
		__field(u64,		open			)
		__field(u64,		need_journal_commit	)
		__field(u64,		discarded		)
		__array(char,		err,	16		)
	),

	TP_fast_assign(
		__entry->dev			= c->dev;
		__entry->seen			= seen;
		__entry->open			= open;
		__entry->need_journal_commit	= need_journal_commit;
		__entry->discarded		= discarded;
		strscpy(__entry->err, err, sizeof(__entry->err));
	),

	TP_printk("%d%d seen %llu open %llu need_journal_commit %llu discarded %llu err %s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->seen,
		  __entry->open,
		  __entry->need_journal_commit,
		  __entry->discarded,
		  __entry->err)
);

TRACE_EVENT(bucket_invalidate,
	TP_PROTO(struct bch_fs *c, unsigned dev, u64 bucket, u32 sectors),
	TP_ARGS(c, dev, bucket, sectors),

	TP_STRUCT__entry(
		__field(dev_t,		dev			)
		__field(u32,		dev_idx			)
		__field(u32,		sectors			)
		__field(u64,		bucket			)
	),

	TP_fast_assign(
		__entry->dev		= c->dev;
		__entry->dev_idx	= dev;
		__entry->sectors	= sectors;
		__entry->bucket		= bucket;
	),

	TP_printk("%d:%d invalidated %u:%llu cached sectors %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->dev_idx, __entry->bucket,
		  __entry->sectors)
);

/* Moving IO */

DEFINE_EVENT(bkey, move_extent_read,
	TP_PROTO(const struct bkey *k),
	TP_ARGS(k)
);

DEFINE_EVENT(bkey, move_extent_write,
	TP_PROTO(const struct bkey *k),
	TP_ARGS(k)
);

DEFINE_EVENT(bkey, move_extent_finish,
	TP_PROTO(const struct bkey *k),
	TP_ARGS(k)
);

DEFINE_EVENT(bkey, move_extent_fail,
	TP_PROTO(const struct bkey *k),
	TP_ARGS(k)
);

DEFINE_EVENT(bkey, move_extent_alloc_mem_fail,
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

TRACE_EVENT(evacuate_bucket,
	TP_PROTO(struct bch_fs *c, struct bpos *bucket,
		 unsigned sectors, unsigned bucket_size,
		 u64 fragmentation, int ret),
	TP_ARGS(c, bucket, sectors, bucket_size, fragmentation, ret),

	TP_STRUCT__entry(
		__field(dev_t,		dev		)
		__field(u64,		member		)
		__field(u64,		bucket		)
		__field(u32,		sectors		)
		__field(u32,		bucket_size	)
		__field(u64,		fragmentation	)
		__field(int,		ret		)
	),

	TP_fast_assign(
		__entry->dev			= c->dev;
		__entry->member			= bucket->inode;
		__entry->bucket			= bucket->offset;
		__entry->sectors		= sectors;
		__entry->bucket_size		= bucket_size;
		__entry->fragmentation		= fragmentation;
		__entry->ret			= ret;
	),

	TP_printk("%d,%d %llu:%llu sectors %u/%u fragmentation %llu ret %i",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->member, __entry->bucket,
		  __entry->sectors, __entry->bucket_size,
		  __entry->fragmentation, __entry->ret)
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

/* btree transactions: */

DECLARE_EVENT_CLASS(transaction_event,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip),
	TP_ARGS(trans, caller_ip),

	TP_STRUCT__entry(
		__array(char,			trans_fn, 32	)
		__field(unsigned long,		caller_ip	)
	),

	TP_fast_assign(
		strscpy(__entry->trans_fn, trans->fn, sizeof(__entry->trans_fn));
		__entry->caller_ip		= caller_ip;
	),

	TP_printk("%s %pS", __entry->trans_fn, (void *) __entry->caller_ip)
);

DEFINE_EVENT(transaction_event,	transaction_commit,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip),
	TP_ARGS(trans, caller_ip)
);

DEFINE_EVENT(transaction_event,	trans_restart_injected,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip),
	TP_ARGS(trans, caller_ip)
);

DEFINE_EVENT(transaction_event,	trans_restart_split_race,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip),
	TP_ARGS(trans, caller_ip)
);

DEFINE_EVENT(transaction_event,	trans_blocked_journal_reclaim,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip),
	TP_ARGS(trans, caller_ip)
);

DEFINE_EVENT(transaction_event,	trans_restart_journal_res_get,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip),
	TP_ARGS(trans, caller_ip)
);


TRACE_EVENT(trans_restart_journal_preres_get,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 unsigned flags),
	TP_ARGS(trans, caller_ip, flags),

	TP_STRUCT__entry(
		__array(char,			trans_fn, 32	)
		__field(unsigned long,		caller_ip	)
		__field(unsigned,		flags		)
	),

	TP_fast_assign(
		strscpy(__entry->trans_fn, trans->fn, sizeof(__entry->trans_fn));
		__entry->caller_ip		= caller_ip;
		__entry->flags			= flags;
	),

	TP_printk("%s %pS %x", __entry->trans_fn,
		  (void *) __entry->caller_ip,
		  __entry->flags)
);

DEFINE_EVENT(transaction_event,	trans_restart_journal_reclaim,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip),
	TP_ARGS(trans, caller_ip)
);

DEFINE_EVENT(transaction_event,	trans_restart_fault_inject,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip),
	TP_ARGS(trans, caller_ip)
);

DEFINE_EVENT(transaction_event,	trans_traverse_all,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip),
	TP_ARGS(trans, caller_ip)
);

DEFINE_EVENT(transaction_event,	trans_restart_mark_replicas,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip),
	TP_ARGS(trans, caller_ip)
);

DEFINE_EVENT(transaction_event,	trans_restart_key_cache_raced,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip),
	TP_ARGS(trans, caller_ip)
);

DEFINE_EVENT(transaction_event,	trans_restart_too_many_iters,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip),
	TP_ARGS(trans, caller_ip)
);

DECLARE_EVENT_CLASS(transaction_restart_iter,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 struct btree_path *path),
	TP_ARGS(trans, caller_ip, path),

	TP_STRUCT__entry(
		__array(char,			trans_fn, 32	)
		__field(unsigned long,		caller_ip	)
		__field(u8,			btree_id	)
		TRACE_BPOS_entries(pos)
	),

	TP_fast_assign(
		strscpy(__entry->trans_fn, trans->fn, sizeof(__entry->trans_fn));
		__entry->caller_ip		= caller_ip;
		__entry->btree_id		= path->btree_id;
		TRACE_BPOS_assign(pos, path->pos)
	),

	TP_printk("%s %pS btree %s pos %llu:%llu:%u",
		  __entry->trans_fn,
		  (void *) __entry->caller_ip,
		  bch2_btree_ids[__entry->btree_id],
		  __entry->pos_inode,
		  __entry->pos_offset,
		  __entry->pos_snapshot)
);

DEFINE_EVENT(transaction_restart_iter,	trans_restart_btree_node_reused,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 struct btree_path *path),
	TP_ARGS(trans, caller_ip, path)
);

DEFINE_EVENT(transaction_restart_iter,	trans_restart_btree_node_split,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 struct btree_path *path),
	TP_ARGS(trans, caller_ip, path)
);

TRACE_EVENT(trans_restart_upgrade,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 struct btree_path *path,
		 unsigned old_locks_want,
		 unsigned new_locks_want),
	TP_ARGS(trans, caller_ip, path, old_locks_want, new_locks_want),

	TP_STRUCT__entry(
		__array(char,			trans_fn, 32	)
		__field(unsigned long,		caller_ip	)
		__field(u8,			btree_id	)
		__field(u8,			old_locks_want	)
		__field(u8,			new_locks_want	)
		TRACE_BPOS_entries(pos)
	),

	TP_fast_assign(
		strscpy(__entry->trans_fn, trans->fn, sizeof(__entry->trans_fn));
		__entry->caller_ip		= caller_ip;
		__entry->btree_id		= path->btree_id;
		__entry->old_locks_want		= old_locks_want;
		__entry->new_locks_want		= new_locks_want;
		TRACE_BPOS_assign(pos, path->pos)
	),

	TP_printk("%s %pS btree %s pos %llu:%llu:%u locks_want %u -> %u",
		  __entry->trans_fn,
		  (void *) __entry->caller_ip,
		  bch2_btree_ids[__entry->btree_id],
		  __entry->pos_inode,
		  __entry->pos_offset,
		  __entry->pos_snapshot,
		  __entry->old_locks_want,
		  __entry->new_locks_want)
);

DEFINE_EVENT(transaction_restart_iter,	trans_restart_relock,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 struct btree_path *path),
	TP_ARGS(trans, caller_ip, path)
);

DEFINE_EVENT(transaction_restart_iter,	trans_restart_relock_next_node,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 struct btree_path *path),
	TP_ARGS(trans, caller_ip, path)
);

DEFINE_EVENT(transaction_restart_iter,	trans_restart_relock_parent_for_fill,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 struct btree_path *path),
	TP_ARGS(trans, caller_ip, path)
);

DEFINE_EVENT(transaction_restart_iter,	trans_restart_relock_after_fill,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 struct btree_path *path),
	TP_ARGS(trans, caller_ip, path)
);

DEFINE_EVENT(transaction_event,	trans_restart_key_cache_upgrade,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip),
	TP_ARGS(trans, caller_ip)
);

DEFINE_EVENT(transaction_restart_iter,	trans_restart_relock_key_cache_fill,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 struct btree_path *path),
	TP_ARGS(trans, caller_ip, path)
);

DEFINE_EVENT(transaction_restart_iter,	trans_restart_relock_path,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 struct btree_path *path),
	TP_ARGS(trans, caller_ip, path)
);

DEFINE_EVENT(transaction_restart_iter,	trans_restart_relock_path_intent,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 struct btree_path *path),
	TP_ARGS(trans, caller_ip, path)
);

DEFINE_EVENT(transaction_restart_iter,	trans_restart_traverse,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 struct btree_path *path),
	TP_ARGS(trans, caller_ip, path)
);

DEFINE_EVENT(transaction_restart_iter,	trans_restart_memory_allocation_failure,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 struct btree_path *path),
	TP_ARGS(trans, caller_ip, path)
);

DEFINE_EVENT(transaction_event,	trans_restart_would_deadlock,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip),
	TP_ARGS(trans, caller_ip)
);

DEFINE_EVENT(transaction_event,	trans_restart_would_deadlock_recursion_limit,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip),
	TP_ARGS(trans, caller_ip)
);

TRACE_EVENT(trans_restart_would_deadlock_write,
	TP_PROTO(struct btree_trans *trans),
	TP_ARGS(trans),

	TP_STRUCT__entry(
		__array(char,			trans_fn, 32	)
	),

	TP_fast_assign(
		strscpy(__entry->trans_fn, trans->fn, sizeof(__entry->trans_fn));
	),

	TP_printk("%s", __entry->trans_fn)
);

TRACE_EVENT(trans_restart_mem_realloced,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 unsigned long bytes),
	TP_ARGS(trans, caller_ip, bytes),

	TP_STRUCT__entry(
		__array(char,			trans_fn, 32	)
		__field(unsigned long,		caller_ip	)
		__field(unsigned long,		bytes		)
	),

	TP_fast_assign(
		strscpy(__entry->trans_fn, trans->fn, sizeof(__entry->trans_fn));
		__entry->caller_ip	= caller_ip;
		__entry->bytes		= bytes;
	),

	TP_printk("%s %pS bytes %lu",
		  __entry->trans_fn,
		  (void *) __entry->caller_ip,
		  __entry->bytes)
);

TRACE_EVENT(trans_restart_key_cache_key_realloced,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 struct btree_path *path,
		 unsigned old_u64s,
		 unsigned new_u64s),
	TP_ARGS(trans, caller_ip, path, old_u64s, new_u64s),

	TP_STRUCT__entry(
		__array(char,			trans_fn, 32	)
		__field(unsigned long,		caller_ip	)
		__field(enum btree_id,		btree_id	)
		TRACE_BPOS_entries(pos)
		__field(u32,			old_u64s	)
		__field(u32,			new_u64s	)
	),

	TP_fast_assign(
		strscpy(__entry->trans_fn, trans->fn, sizeof(__entry->trans_fn));
		__entry->caller_ip		= caller_ip;

		__entry->btree_id	= path->btree_id;
		TRACE_BPOS_assign(pos, path->pos);
		__entry->old_u64s	= old_u64s;
		__entry->new_u64s	= new_u64s;
	),

	TP_printk("%s %pS btree %s pos %llu:%llu:%u old_u64s %u new_u64s %u",
		  __entry->trans_fn,
		  (void *) __entry->caller_ip,
		  bch2_btree_ids[__entry->btree_id],
		  __entry->pos_inode,
		  __entry->pos_offset,
		  __entry->pos_snapshot,
		  __entry->old_u64s,
		  __entry->new_u64s)
);

DEFINE_EVENT(transaction_event,	trans_restart_write_buffer_flush,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip),
	TP_ARGS(trans, caller_ip)
);

TRACE_EVENT(write_buffer_flush,
	TP_PROTO(struct btree_trans *trans, size_t nr, size_t skipped, size_t fast, size_t size),
	TP_ARGS(trans, nr, skipped, fast, size),

	TP_STRUCT__entry(
		__field(size_t,		nr		)
		__field(size_t,		skipped		)
		__field(size_t,		fast		)
		__field(size_t,		size		)
	),

	TP_fast_assign(
		__entry->nr	= nr;
		__entry->skipped = skipped;
		__entry->fast	= fast;
		__entry->size	= size;
	),

	TP_printk("%zu/%zu skipped %zu fast %zu",
		  __entry->nr, __entry->size, __entry->skipped, __entry->fast)
);

TRACE_EVENT(write_buffer_flush_slowpath,
	TP_PROTO(struct btree_trans *trans, size_t nr, size_t size),
	TP_ARGS(trans, nr, size),

	TP_STRUCT__entry(
		__field(size_t,		nr		)
		__field(size_t,		size		)
	),

	TP_fast_assign(
		__entry->nr	= nr;
		__entry->size	= size;
	),

	TP_printk("%zu/%zu", __entry->nr, __entry->size)
);

#endif /* _TRACE_BCACHEFS_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../fs/bcachefs

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

#include <trace/define_trace.h>
