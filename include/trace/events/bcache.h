/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM bcache

#if !defined(_TRACE_BCACHE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_BCACHE_H

#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(bcache_request,
	TP_PROTO(struct bcache_device *d, struct bio *bio),
	TP_ARGS(d, bio),

	TP_STRUCT__entry(
		__field(dev_t,		dev			)
		__field(unsigned int,	orig_major		)
		__field(unsigned int,	orig_minor		)
		__field(sector_t,	sector			)
		__field(dev_t,		orig_sector		)
		__field(unsigned int,	nr_sector		)
		__array(char,		rwbs,	6		)
	),

	TP_fast_assign(
		__entry->dev		= bio_dev(bio);
		__entry->orig_major	= d->disk->major;
		__entry->orig_minor	= d->disk->first_minor;
		__entry->sector		= bio->bi_iter.bi_sector;
		__entry->orig_sector	= bio->bi_iter.bi_sector - 16;
		__entry->nr_sector	= bio->bi_iter.bi_size >> 9;
		blk_fill_rwbs(__entry->rwbs, bio->bi_opf, bio->bi_iter.bi_size);
	),

	TP_printk("%d,%d %s %llu + %u (from %d,%d @ %llu)",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->rwbs, (unsigned long long)__entry->sector,
		  __entry->nr_sector, __entry->orig_major, __entry->orig_minor,
		  (unsigned long long)__entry->orig_sector)
);

DECLARE_EVENT_CLASS(bkey,
	TP_PROTO(struct bkey *k),
	TP_ARGS(k),

	TP_STRUCT__entry(
		__field(u32,	size				)
		__field(u32,	inode				)
		__field(u64,	offset				)
		__field(bool,	dirty				)
	),

	TP_fast_assign(
		__entry->inode	= KEY_INODE(k);
		__entry->offset	= KEY_OFFSET(k);
		__entry->size	= KEY_SIZE(k);
		__entry->dirty	= KEY_DIRTY(k);
	),

	TP_printk("%u:%llu len %u dirty %u", __entry->inode,
		  __entry->offset, __entry->size, __entry->dirty)
);

DECLARE_EVENT_CLASS(btree_node,
	TP_PROTO(struct btree *b),
	TP_ARGS(b),

	TP_STRUCT__entry(
		__field(size_t,		bucket			)
	),

	TP_fast_assign(
		__entry->bucket	= PTR_BUCKET_NR(b->c, &b->key, 0);
	),

	TP_printk("bucket %zu", __entry->bucket)
);

/* request.c */

DEFINE_EVENT(bcache_request, bcache_request_start,
	TP_PROTO(struct bcache_device *d, struct bio *bio),
	TP_ARGS(d, bio)
);

DEFINE_EVENT(bcache_request, bcache_request_end,
	TP_PROTO(struct bcache_device *d, struct bio *bio),
	TP_ARGS(d, bio)
);

DECLARE_EVENT_CLASS(bcache_bio,
	TP_PROTO(struct bio *bio),
	TP_ARGS(bio),

	TP_STRUCT__entry(
		__field(dev_t,		dev			)
		__field(sector_t,	sector			)
		__field(unsigned int,	nr_sector		)
		__array(char,		rwbs,	6		)
	),

	TP_fast_assign(
		__entry->dev		= bio_dev(bio);
		__entry->sector		= bio->bi_iter.bi_sector;
		__entry->nr_sector	= bio->bi_iter.bi_size >> 9;
		blk_fill_rwbs(__entry->rwbs, bio->bi_opf, bio->bi_iter.bi_size);
	),

	TP_printk("%d,%d  %s %llu + %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev), __entry->rwbs,
		  (unsigned long long)__entry->sector, __entry->nr_sector)
);

DEFINE_EVENT(bcache_bio, bcache_bypass_sequential,
	TP_PROTO(struct bio *bio),
	TP_ARGS(bio)
);

DEFINE_EVENT(bcache_bio, bcache_bypass_congested,
	TP_PROTO(struct bio *bio),
	TP_ARGS(bio)
);

TRACE_EVENT(bcache_read,
	TP_PROTO(struct bio *bio, bool hit, bool bypass),
	TP_ARGS(bio, hit, bypass),

	TP_STRUCT__entry(
		__field(dev_t,		dev			)
		__field(sector_t,	sector			)
		__field(unsigned int,	nr_sector		)
		__array(char,		rwbs,	6		)
		__field(bool,		cache_hit		)
		__field(bool,		bypass			)
	),

	TP_fast_assign(
		__entry->dev		= bio_dev(bio);
		__entry->sector		= bio->bi_iter.bi_sector;
		__entry->nr_sector	= bio->bi_iter.bi_size >> 9;
		blk_fill_rwbs(__entry->rwbs, bio->bi_opf, bio->bi_iter.bi_size);
		__entry->cache_hit = hit;
		__entry->bypass = bypass;
	),

	TP_printk("%d,%d  %s %llu + %u hit %u bypass %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->rwbs, (unsigned long long)__entry->sector,
		  __entry->nr_sector, __entry->cache_hit, __entry->bypass)
);

TRACE_EVENT(bcache_write,
	TP_PROTO(struct cache_set *c, u64 inode, struct bio *bio,
		bool writeback, bool bypass),
	TP_ARGS(c, inode, bio, writeback, bypass),

	TP_STRUCT__entry(
		__array(char,		uuid,	16		)
		__field(u64,		inode			)
		__field(sector_t,	sector			)
		__field(unsigned int,	nr_sector		)
		__array(char,		rwbs,	6		)
		__field(bool,		writeback		)
		__field(bool,		bypass			)
	),

	TP_fast_assign(
		memcpy(__entry->uuid, c->sb.set_uuid, 16);
		__entry->inode		= inode;
		__entry->sector		= bio->bi_iter.bi_sector;
		__entry->nr_sector	= bio->bi_iter.bi_size >> 9;
		blk_fill_rwbs(__entry->rwbs, bio->bi_opf, bio->bi_iter.bi_size);
		__entry->writeback = writeback;
		__entry->bypass = bypass;
	),

	TP_printk("%pU inode %llu  %s %llu + %u hit %u bypass %u",
		  __entry->uuid, __entry->inode,
		  __entry->rwbs, (unsigned long long)__entry->sector,
		  __entry->nr_sector, __entry->writeback, __entry->bypass)
);

DEFINE_EVENT(bcache_bio, bcache_read_retry,
	TP_PROTO(struct bio *bio),
	TP_ARGS(bio)
);

DEFINE_EVENT(bkey, bcache_cache_insert,
	TP_PROTO(struct bkey *k),
	TP_ARGS(k)
);

/* Journal */

DECLARE_EVENT_CLASS(cache_set,
	TP_PROTO(struct cache_set *c),
	TP_ARGS(c),

	TP_STRUCT__entry(
		__array(char,		uuid,	16 )
	),

	TP_fast_assign(
		memcpy(__entry->uuid, c->sb.set_uuid, 16);
	),

	TP_printk("%pU", __entry->uuid)
);

DEFINE_EVENT(bkey, bcache_journal_replay_key,
	TP_PROTO(struct bkey *k),
	TP_ARGS(k)
);

DEFINE_EVENT(cache_set, bcache_journal_full,
	TP_PROTO(struct cache_set *c),
	TP_ARGS(c)
);

DEFINE_EVENT(cache_set, bcache_journal_entry_full,
	TP_PROTO(struct cache_set *c),
	TP_ARGS(c)
);

TRACE_EVENT(bcache_journal_write,
	TP_PROTO(struct bio *bio, u32 keys),
	TP_ARGS(bio, keys),

	TP_STRUCT__entry(
		__field(dev_t,		dev			)
		__field(sector_t,	sector			)
		__field(unsigned int,	nr_sector		)
		__array(char,		rwbs,	6		)
		__field(u32,		nr_keys			)
	),

	TP_fast_assign(
		__entry->dev		= bio_dev(bio);
		__entry->sector		= bio->bi_iter.bi_sector;
		__entry->nr_sector	= bio->bi_iter.bi_size >> 9;
		__entry->nr_keys	= keys;
		blk_fill_rwbs(__entry->rwbs, bio->bi_opf, bio->bi_iter.bi_size);
	),

	TP_printk("%d,%d  %s %llu + %u keys %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev), __entry->rwbs,
		  (unsigned long long)__entry->sector, __entry->nr_sector,
		  __entry->nr_keys)
);

/* Btree */

DEFINE_EVENT(cache_set, bcache_btree_cache_cannibalize,
	TP_PROTO(struct cache_set *c),
	TP_ARGS(c)
);

DEFINE_EVENT(btree_node, bcache_btree_read,
	TP_PROTO(struct btree *b),
	TP_ARGS(b)
);

TRACE_EVENT(bcache_btree_write,
	TP_PROTO(struct btree *b),
	TP_ARGS(b),

	TP_STRUCT__entry(
		__field(size_t,		bucket			)
		__field(unsigned,	block			)
		__field(unsigned,	keys			)
	),

	TP_fast_assign(
		__entry->bucket	= PTR_BUCKET_NR(b->c, &b->key, 0);
		__entry->block	= b->written;
		__entry->keys	= b->keys.set[b->keys.nsets].data->keys;
	),

	TP_printk("bucket %zu", __entry->bucket)
);

DEFINE_EVENT(btree_node, bcache_btree_node_alloc,
	TP_PROTO(struct btree *b),
	TP_ARGS(b)
);

DEFINE_EVENT(cache_set, bcache_btree_node_alloc_fail,
	TP_PROTO(struct cache_set *c),
	TP_ARGS(c)
);

DEFINE_EVENT(btree_node, bcache_btree_node_free,
	TP_PROTO(struct btree *b),
	TP_ARGS(b)
);

TRACE_EVENT(bcache_btree_gc_coalesce,
	TP_PROTO(unsigned nodes),
	TP_ARGS(nodes),

	TP_STRUCT__entry(
		__field(unsigned,	nodes			)
	),

	TP_fast_assign(
		__entry->nodes	= nodes;
	),

	TP_printk("coalesced %u nodes", __entry->nodes)
);

DEFINE_EVENT(cache_set, bcache_gc_start,
	TP_PROTO(struct cache_set *c),
	TP_ARGS(c)
);

DEFINE_EVENT(cache_set, bcache_gc_end,
	TP_PROTO(struct cache_set *c),
	TP_ARGS(c)
);

DEFINE_EVENT(bkey, bcache_gc_copy,
	TP_PROTO(struct bkey *k),
	TP_ARGS(k)
);

DEFINE_EVENT(bkey, bcache_gc_copy_collision,
	TP_PROTO(struct bkey *k),
	TP_ARGS(k)
);

TRACE_EVENT(bcache_btree_insert_key,
	TP_PROTO(struct btree *b, struct bkey *k, unsigned op, unsigned status),
	TP_ARGS(b, k, op, status),

	TP_STRUCT__entry(
		__field(u64,	btree_node			)
		__field(u32,	btree_level			)
		__field(u32,	inode				)
		__field(u64,	offset				)
		__field(u32,	size				)
		__field(u8,	dirty				)
		__field(u8,	op				)
		__field(u8,	status				)
	),

	TP_fast_assign(
		__entry->btree_node = PTR_BUCKET_NR(b->c, &b->key, 0);
		__entry->btree_level = b->level;
		__entry->inode	= KEY_INODE(k);
		__entry->offset	= KEY_OFFSET(k);
		__entry->size	= KEY_SIZE(k);
		__entry->dirty	= KEY_DIRTY(k);
		__entry->op = op;
		__entry->status = status;
	),

	TP_printk("%u for %u at %llu(%u): %u:%llu len %u dirty %u",
		  __entry->status, __entry->op,
		  __entry->btree_node, __entry->btree_level,
		  __entry->inode, __entry->offset,
		  __entry->size, __entry->dirty)
);

DECLARE_EVENT_CLASS(btree_split,
	TP_PROTO(struct btree *b, unsigned keys),
	TP_ARGS(b, keys),

	TP_STRUCT__entry(
		__field(size_t,		bucket			)
		__field(unsigned,	keys			)
	),

	TP_fast_assign(
		__entry->bucket	= PTR_BUCKET_NR(b->c, &b->key, 0);
		__entry->keys	= keys;
	),

	TP_printk("bucket %zu keys %u", __entry->bucket, __entry->keys)
);

DEFINE_EVENT(btree_split, bcache_btree_node_split,
	TP_PROTO(struct btree *b, unsigned keys),
	TP_ARGS(b, keys)
);

DEFINE_EVENT(btree_split, bcache_btree_node_compact,
	TP_PROTO(struct btree *b, unsigned keys),
	TP_ARGS(b, keys)
);

DEFINE_EVENT(btree_node, bcache_btree_set_root,
	TP_PROTO(struct btree *b),
	TP_ARGS(b)
);

TRACE_EVENT(bcache_keyscan,
	TP_PROTO(unsigned nr_found,
		 unsigned start_inode, uint64_t start_offset,
		 unsigned end_inode, uint64_t end_offset),
	TP_ARGS(nr_found,
		start_inode, start_offset,
		end_inode, end_offset),

	TP_STRUCT__entry(
		__field(__u32,	nr_found			)
		__field(__u32,	start_inode			)
		__field(__u64,	start_offset			)
		__field(__u32,	end_inode			)
		__field(__u64,	end_offset			)
	),

	TP_fast_assign(
		__entry->nr_found	= nr_found;
		__entry->start_inode	= start_inode;
		__entry->start_offset	= start_offset;
		__entry->end_inode	= end_inode;
		__entry->end_offset	= end_offset;
	),

	TP_printk("found %u keys from %u:%llu to %u:%llu", __entry->nr_found,
		  __entry->start_inode, __entry->start_offset,
		  __entry->end_inode, __entry->end_offset)
);

/* Allocator */

TRACE_EVENT(bcache_invalidate,
	TP_PROTO(struct cache *ca, size_t bucket),
	TP_ARGS(ca, bucket),

	TP_STRUCT__entry(
		__field(unsigned,	sectors			)
		__field(dev_t,		dev			)
		__field(__u64,		offset			)
	),

	TP_fast_assign(
		__entry->dev		= ca->bdev->bd_dev;
		__entry->offset		= bucket << ca->set->bucket_bits;
		__entry->sectors	= GC_SECTORS_USED(&ca->buckets[bucket]);
	),

	TP_printk("invalidated %u sectors at %d,%d sector=%llu",
		  __entry->sectors, MAJOR(__entry->dev),
		  MINOR(__entry->dev), __entry->offset)
);

TRACE_EVENT(bcache_alloc,
	TP_PROTO(struct cache *ca, size_t bucket),
	TP_ARGS(ca, bucket),

	TP_STRUCT__entry(
		__field(dev_t,		dev			)
		__field(__u64,		offset			)
	),

	TP_fast_assign(
		__entry->dev		= ca->bdev->bd_dev;
		__entry->offset		= bucket << ca->set->bucket_bits;
	),

	TP_printk("allocated %d,%d sector=%llu", MAJOR(__entry->dev),
		  MINOR(__entry->dev), __entry->offset)
);

TRACE_EVENT(bcache_alloc_fail,
	TP_PROTO(struct cache *ca, unsigned reserve),
	TP_ARGS(ca, reserve),

	TP_STRUCT__entry(
		__field(dev_t,		dev			)
		__field(unsigned,	free			)
		__field(unsigned,	free_inc		)
		__field(unsigned,	blocked			)
	),

	TP_fast_assign(
		__entry->dev		= ca->bdev->bd_dev;
		__entry->free		= fifo_used(&ca->free[reserve]);
		__entry->free_inc	= fifo_used(&ca->free_inc);
		__entry->blocked	= atomic_read(&ca->set->prio_blocked);
	),

	TP_printk("alloc fail %d,%d free %u free_inc %u blocked %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev), __entry->free,
		  __entry->free_inc, __entry->blocked)
);

/* Background writeback */

DEFINE_EVENT(bkey, bcache_writeback,
	TP_PROTO(struct bkey *k),
	TP_ARGS(k)
);

DEFINE_EVENT(bkey, bcache_writeback_collision,
	TP_PROTO(struct bkey *k),
	TP_ARGS(k)
);

#endif /* _TRACE_BCACHE_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
