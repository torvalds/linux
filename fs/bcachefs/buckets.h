/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Code for manipulating bucket marks for garbage collection.
 *
 * Copyright 2014 Datera, Inc.
 */

#ifndef _BUCKETS_H
#define _BUCKETS_H

#include "buckets_types.h"
#include "super.h"

#define for_each_bucket(_b, _buckets)				\
	for (_b = (_buckets)->b + (_buckets)->first_bucket;	\
	     _b < (_buckets)->b + (_buckets)->nbuckets; _b++)

#define bucket_cmpxchg(g, new, expr)				\
({								\
	struct bucket *_g = g;					\
	u64 _v = atomic64_read(&(g)->_mark.v);			\
	struct bucket_mark _old;				\
								\
	do {							\
		(new).v.counter = _old.v.counter = _v;		\
		expr;						\
	} while ((_v = atomic64_cmpxchg(&(_g)->_mark.v,		\
			       _old.v.counter,			\
			       (new).v.counter)) != _old.v.counter);\
	_old;							\
})

static inline struct bucket_array *__bucket_array(struct bch_dev *ca,
						  bool gc)
{
	return rcu_dereference_check(ca->buckets[gc],
				     !ca->fs ||
				     percpu_rwsem_is_held(&ca->fs->mark_lock) ||
				     lockdep_is_held(&ca->fs->gc_lock) ||
				     lockdep_is_held(&ca->bucket_lock));
}

static inline struct bucket_array *bucket_array(struct bch_dev *ca)
{
	return __bucket_array(ca, false);
}

static inline struct bucket *__bucket(struct bch_dev *ca, size_t b, bool gc)
{
	struct bucket_array *buckets = __bucket_array(ca, gc);

	BUG_ON(b < buckets->first_bucket || b >= buckets->nbuckets);
	return buckets->b + b;
}

static inline struct bucket *bucket(struct bch_dev *ca, size_t b)
{
	return __bucket(ca, b, false);
}

static inline void bucket_io_clock_reset(struct bch_fs *c, struct bch_dev *ca,
					 size_t b, int rw)
{
	bucket(ca, b)->io_time[rw] = c->bucket_clock[rw].hand;
}

static inline u16 bucket_last_io(struct bch_fs *c, struct bucket *g, int rw)
{
	return c->bucket_clock[rw].hand - g->io_time[rw];
}

/*
 * bucket_gc_gen() returns the difference between the bucket's current gen and
 * the oldest gen of any pointer into that bucket in the btree.
 */

static inline u8 bucket_gc_gen(struct bch_dev *ca, size_t b)
{
	struct bucket *g = bucket(ca, b);

	return g->mark.gen - g->oldest_gen;
}

static inline size_t PTR_BUCKET_NR(const struct bch_dev *ca,
				   const struct bch_extent_ptr *ptr)
{
	return sector_to_bucket(ca, ptr->offset);
}

static inline struct bucket *PTR_BUCKET(struct bch_dev *ca,
					const struct bch_extent_ptr *ptr,
					bool gc)
{
	return __bucket(ca, PTR_BUCKET_NR(ca, ptr), gc);
}

static inline enum bch_data_type ptr_data_type(const struct bkey *k,
					       const struct bch_extent_ptr *ptr)
{
	if (k->type == KEY_TYPE_btree_ptr ||
	    k->type == KEY_TYPE_btree_ptr_v2)
		return BCH_DATA_BTREE;

	return ptr->cached ? BCH_DATA_CACHED : BCH_DATA_USER;
}

static inline struct bucket_mark ptr_bucket_mark(struct bch_dev *ca,
						 const struct bch_extent_ptr *ptr)
{
	struct bucket_mark m;

	rcu_read_lock();
	m = READ_ONCE(PTR_BUCKET(ca, ptr, 0)->mark);
	rcu_read_unlock();

	return m;
}

static inline int gen_cmp(u8 a, u8 b)
{
	return (s8) (a - b);
}

static inline int gen_after(u8 a, u8 b)
{
	int r = gen_cmp(a, b);

	return r > 0 ? r : 0;
}

/**
 * ptr_stale() - check if a pointer points into a bucket that has been
 * invalidated.
 */
static inline u8 ptr_stale(struct bch_dev *ca,
			   const struct bch_extent_ptr *ptr)
{
	return gen_after(ptr_bucket_mark(ca, ptr).gen, ptr->gen);
}

static inline s64 __ptr_disk_sectors(struct extent_ptr_decoded p,
				     unsigned live_size)
{
	return live_size && p.crc.compression_type
		? max(1U, DIV_ROUND_UP(live_size * p.crc.compressed_size,
				       p.crc.uncompressed_size))
		: live_size;
}

static inline s64 ptr_disk_sectors(struct extent_ptr_decoded p)
{
	return __ptr_disk_sectors(p, p.crc.live_size);
}

/* bucket gc marks */

static inline unsigned bucket_sectors_used(struct bucket_mark mark)
{
	return mark.dirty_sectors + mark.cached_sectors;
}

static inline bool bucket_unused(struct bucket_mark mark)
{
	return !mark.owned_by_allocator &&
		!mark.data_type &&
		!bucket_sectors_used(mark);
}

static inline bool is_available_bucket(struct bucket_mark mark)
{
	return (!mark.owned_by_allocator &&
		!mark.dirty_sectors &&
		!mark.stripe);
}

static inline bool bucket_needs_journal_commit(struct bucket_mark m,
					       u16 last_seq_ondisk)
{
	return m.journal_seq_valid &&
		((s16) m.journal_seq - (s16) last_seq_ondisk > 0);
}

/* Device usage: */

struct bch_dev_usage bch2_dev_usage_read(struct bch_fs *, struct bch_dev *);

void bch2_dev_usage_from_buckets(struct bch_fs *);

static inline u64 __dev_buckets_available(struct bch_dev *ca,
					  struct bch_dev_usage stats)
{
	u64 total = ca->mi.nbuckets - ca->mi.first_bucket;

	if (WARN_ONCE(stats.buckets_unavailable > total,
		      "buckets_unavailable overflow (%llu > %llu)\n",
		      stats.buckets_unavailable, total))
		return 0;

	return total - stats.buckets_unavailable;
}

/*
 * Number of reclaimable buckets - only for use by the allocator thread:
 */
static inline u64 dev_buckets_available(struct bch_fs *c, struct bch_dev *ca)
{
	return __dev_buckets_available(ca, bch2_dev_usage_read(c, ca));
}

static inline u64 __dev_buckets_free(struct bch_dev *ca,
				     struct bch_dev_usage stats)
{
	return __dev_buckets_available(ca, stats) +
		fifo_used(&ca->free[RESERVE_NONE]) +
		fifo_used(&ca->free_inc);
}

static inline u64 dev_buckets_free(struct bch_fs *c, struct bch_dev *ca)
{
	return __dev_buckets_free(ca, bch2_dev_usage_read(c, ca));
}

/* Filesystem usage: */

static inline unsigned fs_usage_u64s(struct bch_fs *c)
{

	return sizeof(struct bch_fs_usage) / sizeof(u64) +
		READ_ONCE(c->replicas.nr);
}

void bch2_fs_usage_scratch_put(struct bch_fs *, struct bch_fs_usage_online *);
struct bch_fs_usage_online *bch2_fs_usage_scratch_get(struct bch_fs *);

u64 bch2_fs_usage_read_one(struct bch_fs *, u64 *);

struct bch_fs_usage_online *bch2_fs_usage_read(struct bch_fs *);

void bch2_fs_usage_acc_to_base(struct bch_fs *, unsigned);

void bch2_fs_usage_to_text(struct printbuf *,
			   struct bch_fs *, struct bch_fs_usage_online *);

u64 bch2_fs_sectors_used(struct bch_fs *, struct bch_fs_usage_online *);

struct bch_fs_usage_short
bch2_fs_usage_read_short(struct bch_fs *);

/* key/bucket marking: */

void bch2_bucket_seq_cleanup(struct bch_fs *);
void bch2_fs_usage_initialize(struct bch_fs *);

void bch2_invalidate_bucket(struct bch_fs *, struct bch_dev *,
			    size_t, struct bucket_mark *);
void bch2_mark_alloc_bucket(struct bch_fs *, struct bch_dev *,
			    size_t, bool, struct gc_pos, unsigned);
void bch2_mark_metadata_bucket(struct bch_fs *, struct bch_dev *,
			       size_t, enum bch_data_type, unsigned,
			       struct gc_pos, unsigned);

int bch2_mark_key(struct bch_fs *, struct bkey_s_c, unsigned, s64,
		  struct bch_fs_usage *, u64, unsigned);
int bch2_fs_usage_apply(struct bch_fs *, struct bch_fs_usage_online *,
			struct disk_reservation *, unsigned);

int bch2_mark_update(struct btree_trans *, struct btree_iter *,
		     struct bkey_i *, struct bch_fs_usage *, unsigned);

int bch2_replicas_delta_list_apply(struct bch_fs *,
				   struct bch_fs_usage *,
				   struct replicas_delta_list *);
int bch2_trans_mark_key(struct btree_trans *, struct bkey_s_c,
			unsigned, s64, unsigned);
int bch2_trans_mark_update(struct btree_trans *, struct btree_iter *iter,
			   struct bkey_i *insert, unsigned);
void bch2_trans_fs_usage_apply(struct btree_trans *, struct bch_fs_usage_online *);

/* disk reservations: */

static inline void bch2_disk_reservation_put(struct bch_fs *c,
					     struct disk_reservation *res)
{
	this_cpu_sub(*c->online_reserved, res->sectors);
	res->sectors = 0;
}

#define BCH_DISK_RESERVATION_NOFAIL		(1 << 0)

int bch2_disk_reservation_add(struct bch_fs *,
			     struct disk_reservation *,
			     unsigned, int);

static inline struct disk_reservation
bch2_disk_reservation_init(struct bch_fs *c, unsigned nr_replicas)
{
	return (struct disk_reservation) {
		.sectors	= 0,
#if 0
		/* not used yet: */
		.gen		= c->capacity_gen,
#endif
		.nr_replicas	= nr_replicas,
	};
}

static inline int bch2_disk_reservation_get(struct bch_fs *c,
					    struct disk_reservation *res,
					    unsigned sectors,
					    unsigned nr_replicas,
					    int flags)
{
	*res = bch2_disk_reservation_init(c, nr_replicas);

	return bch2_disk_reservation_add(c, res, sectors * nr_replicas, flags);
}

int bch2_dev_buckets_resize(struct bch_fs *, struct bch_dev *, u64);
void bch2_dev_buckets_free(struct bch_dev *);
int bch2_dev_buckets_alloc(struct bch_fs *, struct bch_dev *);

#endif /* _BUCKETS_H */
