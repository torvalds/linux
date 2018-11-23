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
	u64 _v = atomic64_read(&(g)->_mark.v);			\
	struct bucket_mark _old;				\
								\
	do {							\
		(new).v.counter = _old.v.counter = _v;		\
		expr;						\
	} while ((_v = atomic64_cmpxchg(&(g)->_mark.v,		\
			       _old.v.counter,			\
			       (new).v.counter)) != _old.v.counter);\
	_old;							\
})

static inline struct bucket_array *__bucket_array(struct bch_dev *ca,
						  bool gc)
{
	return rcu_dereference_check(ca->buckets[gc],
				     !ca->fs ||
				     percpu_rwsem_is_held(&ca->fs->usage_lock) ||
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
	return bucket(ca, b)->mark.gen - ca->oldest_gens[b];
}

static inline size_t PTR_BUCKET_NR(const struct bch_dev *ca,
				   const struct bch_extent_ptr *ptr)
{
	return sector_to_bucket(ca, ptr->offset);
}

static inline struct bucket *PTR_BUCKET(struct bch_dev *ca,
					const struct bch_extent_ptr *ptr)
{
	return bucket(ca, PTR_BUCKET_NR(ca, ptr));
}

static inline struct bucket_mark ptr_bucket_mark(struct bch_dev *ca,
						 const struct bch_extent_ptr *ptr)
{
	struct bucket_mark m;

	rcu_read_lock();
	m = READ_ONCE(bucket(ca, PTR_BUCKET_NR(ca, ptr))->mark);
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

/* Device usage: */

struct bch_dev_usage __bch2_dev_usage_read(struct bch_dev *, bool);
struct bch_dev_usage bch2_dev_usage_read(struct bch_fs *, struct bch_dev *);

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

struct bch_fs_usage __bch2_fs_usage_read(struct bch_fs *, bool);
struct bch_fs_usage bch2_fs_usage_read(struct bch_fs *);
void bch2_fs_usage_apply(struct bch_fs *, struct bch_fs_usage *,
			 struct disk_reservation *, struct gc_pos);

u64 bch2_fs_sectors_used(struct bch_fs *, struct bch_fs_usage);

static inline u64 bch2_fs_sectors_free(struct bch_fs *c,
				       struct bch_fs_usage stats)
{
	return c->capacity - bch2_fs_sectors_used(c, stats);
}

static inline bool is_available_bucket(struct bucket_mark mark)
{
	return (!mark.owned_by_allocator &&
		!mark.dirty_sectors &&
		!mark.stripe &&
		!mark.nouse);
}

static inline bool bucket_needs_journal_commit(struct bucket_mark m,
					       u16 last_seq_ondisk)
{
	return m.journal_seq_valid &&
		((s16) m.journal_seq - (s16) last_seq_ondisk > 0);
}

void bch2_bucket_seq_cleanup(struct bch_fs *);

void bch2_invalidate_bucket(struct bch_fs *, struct bch_dev *,
			    size_t, struct bucket_mark *);
void bch2_mark_alloc_bucket(struct bch_fs *, struct bch_dev *,
			    size_t, bool, struct gc_pos, unsigned);
void bch2_mark_metadata_bucket(struct bch_fs *, struct bch_dev *,
			       size_t, enum bch_data_type, unsigned,
			       struct gc_pos, unsigned);

#define BCH_BUCKET_MARK_NOATOMIC		(1 << 0)
#define BCH_BUCKET_MARK_GC			(1 << 1)

int bch2_mark_key_locked(struct bch_fs *, enum bkey_type, struct bkey_s_c,
		  bool, s64, struct gc_pos,
		  struct bch_fs_usage *, u64, unsigned);
int bch2_mark_key(struct bch_fs *, enum bkey_type, struct bkey_s_c,
		  bool, s64, struct gc_pos,
		  struct bch_fs_usage *, u64, unsigned);
void bch2_mark_update(struct btree_insert *, struct btree_insert_entry *);

void __bch2_disk_reservation_put(struct bch_fs *, struct disk_reservation *);

static inline void bch2_disk_reservation_put(struct bch_fs *c,
					     struct disk_reservation *res)
{
	if (res->sectors)
		__bch2_disk_reservation_put(c, res);
}

#define BCH_DISK_RESERVATION_NOFAIL		(1 << 0)
#define BCH_DISK_RESERVATION_GC_LOCK_HELD	(1 << 1)
#define BCH_DISK_RESERVATION_BTREE_LOCKS_HELD	(1 << 2)

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
