#undef TRACE_SYSTEM
#define TRACE_SYSTEM bcache

#if !defined(_TRACE_BCACHE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_BCACHE_H

#include <linux/tracepoint.h>

struct search;

DECLARE_EVENT_CLASS(bcache_request,

	TP_PROTO(struct search *s, struct bio *bio),

	TP_ARGS(s, bio),

	TP_STRUCT__entry(
		__field(dev_t,		dev			)
		__field(unsigned int,	orig_major		)
		__field(unsigned int,	orig_minor		)
		__field(sector_t,	sector			)
		__field(dev_t,		orig_sector		)
		__field(unsigned int,	nr_sector		)
		__array(char,		rwbs,	6		)
		__array(char,		comm,	TASK_COMM_LEN	)
	),

	TP_fast_assign(
		__entry->dev		= bio->bi_bdev->bd_dev;
		__entry->orig_major	= s->d->disk->major;
		__entry->orig_minor	= s->d->disk->first_minor;
		__entry->sector		= bio->bi_sector;
		__entry->orig_sector	= bio->bi_sector - 16;
		__entry->nr_sector	= bio->bi_size >> 9;
		blk_fill_rwbs(__entry->rwbs, bio->bi_rw, bio->bi_size);
		memcpy(__entry->comm, current->comm, TASK_COMM_LEN);
	),

	TP_printk("%d,%d %s %llu + %u [%s] (from %d,%d @ %llu)",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->rwbs,
		  (unsigned long long)__entry->sector,
		  __entry->nr_sector, __entry->comm,
		  __entry->orig_major, __entry->orig_minor,
		  (unsigned long long)__entry->orig_sector)
);

DEFINE_EVENT(bcache_request, bcache_request_start,

	TP_PROTO(struct search *s, struct bio *bio),

	TP_ARGS(s, bio)
);

DEFINE_EVENT(bcache_request, bcache_request_end,

	TP_PROTO(struct search *s, struct bio *bio),

	TP_ARGS(s, bio)
);

DECLARE_EVENT_CLASS(bcache_bio,

	TP_PROTO(struct bio *bio),

	TP_ARGS(bio),

	TP_STRUCT__entry(
		__field(dev_t,		dev			)
		__field(sector_t,	sector			)
		__field(unsigned int,	nr_sector		)
		__array(char,		rwbs,	6		)
		__array(char,		comm,	TASK_COMM_LEN	)
	),

	TP_fast_assign(
		__entry->dev		= bio->bi_bdev->bd_dev;
		__entry->sector		= bio->bi_sector;
		__entry->nr_sector	= bio->bi_size >> 9;
		blk_fill_rwbs(__entry->rwbs, bio->bi_rw, bio->bi_size);
		memcpy(__entry->comm, current->comm, TASK_COMM_LEN);
	),

	TP_printk("%d,%d  %s %llu + %u [%s]",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->rwbs,
		  (unsigned long long)__entry->sector,
		  __entry->nr_sector, __entry->comm)
);


DEFINE_EVENT(bcache_bio, bcache_passthrough,

	TP_PROTO(struct bio *bio),

	TP_ARGS(bio)
);

DEFINE_EVENT(bcache_bio, bcache_cache_hit,

	TP_PROTO(struct bio *bio),

	TP_ARGS(bio)
);

DEFINE_EVENT(bcache_bio, bcache_cache_miss,

	TP_PROTO(struct bio *bio),

	TP_ARGS(bio)
);

DEFINE_EVENT(bcache_bio, bcache_read_retry,

	TP_PROTO(struct bio *bio),

	TP_ARGS(bio)
);

DEFINE_EVENT(bcache_bio, bcache_writethrough,

	TP_PROTO(struct bio *bio),

	TP_ARGS(bio)
);

DEFINE_EVENT(bcache_bio, bcache_writeback,

	TP_PROTO(struct bio *bio),

	TP_ARGS(bio)
);

DEFINE_EVENT(bcache_bio, bcache_write_skip,

	TP_PROTO(struct bio *bio),

	TP_ARGS(bio)
);

DEFINE_EVENT(bcache_bio, bcache_btree_read,

	TP_PROTO(struct bio *bio),

	TP_ARGS(bio)
);

DEFINE_EVENT(bcache_bio, bcache_btree_write,

	TP_PROTO(struct bio *bio),

	TP_ARGS(bio)
);

DEFINE_EVENT(bcache_bio, bcache_write_dirty,

	TP_PROTO(struct bio *bio),

	TP_ARGS(bio)
);

DEFINE_EVENT(bcache_bio, bcache_read_dirty,

	TP_PROTO(struct bio *bio),

	TP_ARGS(bio)
);

DEFINE_EVENT(bcache_bio, bcache_write_moving,

	TP_PROTO(struct bio *bio),

	TP_ARGS(bio)
);

DEFINE_EVENT(bcache_bio, bcache_read_moving,

	TP_PROTO(struct bio *bio),

	TP_ARGS(bio)
);

DEFINE_EVENT(bcache_bio, bcache_journal_write,

	TP_PROTO(struct bio *bio),

	TP_ARGS(bio)
);

DECLARE_EVENT_CLASS(bcache_cache_bio,

	TP_PROTO(struct bio *bio,
		 sector_t orig_sector,
		 struct block_device* orig_bdev),

	TP_ARGS(bio, orig_sector, orig_bdev),

	TP_STRUCT__entry(
		__field(dev_t,		dev			)
		__field(dev_t,		orig_dev		)
		__field(sector_t,	sector			)
		__field(sector_t,	orig_sector		)
		__field(unsigned int,	nr_sector		)
		__array(char,		rwbs,	6		)
		__array(char,		comm,	TASK_COMM_LEN	)
	),

	TP_fast_assign(
		__entry->dev		= bio->bi_bdev->bd_dev;
		__entry->orig_dev	= orig_bdev->bd_dev;
		__entry->sector		= bio->bi_sector;
		__entry->orig_sector	= orig_sector;
		__entry->nr_sector	= bio->bi_size >> 9;
		blk_fill_rwbs(__entry->rwbs, bio->bi_rw, bio->bi_size);
		memcpy(__entry->comm, current->comm, TASK_COMM_LEN);
	),

	TP_printk("%d,%d  %s %llu + %u [%s] (from %d,%d %llu)",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->rwbs,
		  (unsigned long long)__entry->sector,
		  __entry->nr_sector, __entry->comm,
		  MAJOR(__entry->orig_dev), MINOR(__entry->orig_dev),
		  (unsigned long long)__entry->orig_sector)
);

DEFINE_EVENT(bcache_cache_bio, bcache_cache_insert,

	TP_PROTO(struct bio *bio,
		 sector_t orig_sector,
		 struct block_device *orig_bdev),

	TP_ARGS(bio, orig_sector, orig_bdev)
);

DECLARE_EVENT_CLASS(bcache_gc,

	TP_PROTO(uint8_t *uuid),

	TP_ARGS(uuid),

	TP_STRUCT__entry(
		__field(uint8_t *,	uuid)
	),

	TP_fast_assign(
		__entry->uuid		= uuid;
	),

	TP_printk("%pU", __entry->uuid)
);


DEFINE_EVENT(bcache_gc, bcache_gc_start,

	     TP_PROTO(uint8_t *uuid),

	     TP_ARGS(uuid)
);

DEFINE_EVENT(bcache_gc, bcache_gc_end,

	     TP_PROTO(uint8_t *uuid),

	     TP_ARGS(uuid)
);

#endif /* _TRACE_BCACHE_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
