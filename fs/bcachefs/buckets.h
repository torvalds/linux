/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Code for manipulating bucket marks for garbage collection.
 *
 * Copyright 2014 Datera, Inc.
 */

#ifndef _BUCKETS_H
#define _BUCKETS_H

#include "buckets_types.h"
#include "extents.h"
#include "sb-members.h"

static inline size_t sector_to_bucket(const struct bch_dev *ca, sector_t s)
{
	return div_u64(s, ca->mi.bucket_size);
}

static inline sector_t bucket_to_sector(const struct bch_dev *ca, size_t b)
{
	return ((sector_t) b) * ca->mi.bucket_size;
}

static inline sector_t bucket_remainder(const struct bch_dev *ca, sector_t s)
{
	u32 remainder;

	div_u64_rem(s, ca->mi.bucket_size, &remainder);
	return remainder;
}

static inline size_t sector_to_bucket_and_offset(const struct bch_dev *ca, sector_t s,
						 u32 *offset)
{
	return div_u64_rem(s, ca->mi.bucket_size, offset);
}

#define for_each_bucket(_b, _buckets)				\
	for (_b = (_buckets)->b + (_buckets)->first_bucket;	\
	     _b < (_buckets)->b + (_buckets)->nbuckets; _b++)

/*
 * Ugly hack alert:
 *
 * We need to cram a spinlock in a single byte, because that's what we have left
 * in struct bucket, and we care about the size of these - during fsck, we need
 * in memory state for every single bucket on every device.
 *
 * We used to do
 *   while (xchg(&b->lock, 1) cpu_relax();
 * but, it turns out not all architectures support xchg on a single byte.
 *
 * So now we use bit_spin_lock(), with fun games since we can't burn a whole
 * ulong for this - we just need to make sure the lock bit always ends up in the
 * first byte.
 */

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define BUCKET_LOCK_BITNR	0
#else
#define BUCKET_LOCK_BITNR	(BITS_PER_LONG - 1)
#endif

union ulong_byte_assert {
	ulong	ulong;
	u8	byte;
};

static inline void bucket_unlock(struct bucket *b)
{
	BUILD_BUG_ON(!((union ulong_byte_assert) { .ulong = 1UL << BUCKET_LOCK_BITNR }).byte);

	clear_bit_unlock(BUCKET_LOCK_BITNR, (void *) &b->lock);
	wake_up_bit((void *) &b->lock, BUCKET_LOCK_BITNR);
}

static inline void bucket_lock(struct bucket *b)
{
	wait_on_bit_lock((void *) &b->lock, BUCKET_LOCK_BITNR,
			 TASK_UNINTERRUPTIBLE);
}

static inline struct bucket_array *gc_bucket_array(struct bch_dev *ca)
{
	return rcu_dereference_check(ca->buckets_gc,
				     !ca->fs ||
				     percpu_rwsem_is_held(&ca->fs->mark_lock) ||
				     lockdep_is_held(&ca->fs->gc_lock) ||
				     lockdep_is_held(&ca->bucket_lock));
}

static inline struct bucket *gc_bucket(struct bch_dev *ca, size_t b)
{
	struct bucket_array *buckets = gc_bucket_array(ca);

	BUG_ON(b < buckets->first_bucket || b >= buckets->nbuckets);
	return buckets->b + b;
}

static inline struct bucket_gens *bucket_gens(struct bch_dev *ca)
{
	return rcu_dereference_check(ca->bucket_gens,
				     !ca->fs ||
				     percpu_rwsem_is_held(&ca->fs->mark_lock) ||
				     lockdep_is_held(&ca->fs->gc_lock) ||
				     lockdep_is_held(&ca->bucket_lock));
}

static inline u8 *bucket_gen(struct bch_dev *ca, size_t b)
{
	struct bucket_gens *gens = bucket_gens(ca);

	BUG_ON(b < gens->first_bucket || b >= gens->nbuckets);
	return gens->b + b;
}

static inline size_t PTR_BUCKET_NR(const struct bch_dev *ca,
				   const struct bch_extent_ptr *ptr)
{
	return sector_to_bucket(ca, ptr->offset);
}

static inline struct bpos PTR_BUCKET_POS(const struct bch_fs *c,
				   const struct bch_extent_ptr *ptr)
{
	struct bch_dev *ca = bch_dev_bkey_exists(c, ptr->dev);

	return POS(ptr->dev, PTR_BUCKET_NR(ca, ptr));
}

static inline struct bpos PTR_BUCKET_POS_OFFSET(const struct bch_fs *c,
						const struct bch_extent_ptr *ptr,
						u32 *bucket_offset)
{
	struct bch_dev *ca = bch_dev_bkey_exists(c, ptr->dev);

	return POS(ptr->dev, sector_to_bucket_and_offset(ca, ptr->offset, bucket_offset));
}

static inline struct bucket *PTR_GC_BUCKET(struct bch_dev *ca,
					   const struct bch_extent_ptr *ptr)
{
	return gc_bucket(ca, PTR_BUCKET_NR(ca, ptr));
}

static inline enum bch_data_type ptr_data_type(const struct bkey *k,
					       const struct bch_extent_ptr *ptr)
{
	if (bkey_is_btree_ptr(k))
		return BCH_DATA_btree;

	return ptr->cached ? BCH_DATA_cached : BCH_DATA_user;
}

static inline s64 ptr_disk_sectors(s64 sectors, struct extent_ptr_decoded p)
{
	EBUG_ON(sectors < 0);

	return crc_is_compressed(p.crc)
		? DIV_ROUND_UP_ULL(sectors * p.crc.compressed_size,
				   p.crc.uncompressed_size)
		: sectors;
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
	u8 ret;

	rcu_read_lock();
	ret = gen_after(*bucket_gen(ca, PTR_BUCKET_NR(ca, ptr)), ptr->gen);
	rcu_read_unlock();

	return ret;
}

/* Device usage: */

void bch2_dev_usage_read_fast(struct bch_dev *, struct bch_dev_usage *);
static inline struct bch_dev_usage bch2_dev_usage_read(struct bch_dev *ca)
{
	struct bch_dev_usage ret;

	bch2_dev_usage_read_fast(ca, &ret);
	return ret;
}

void bch2_dev_usage_init(struct bch_dev *);
void bch2_dev_usage_to_text(struct printbuf *, struct bch_dev_usage *);

static inline u64 bch2_dev_buckets_reserved(struct bch_dev *ca, enum bch_watermark watermark)
{
	s64 reserved = 0;

	switch (watermark) {
	case BCH_WATERMARK_NR:
		BUG();
	case BCH_WATERMARK_stripe:
		reserved += ca->mi.nbuckets >> 6;
		fallthrough;
	case BCH_WATERMARK_normal:
		reserved += ca->mi.nbuckets >> 6;
		fallthrough;
	case BCH_WATERMARK_copygc:
		reserved += ca->nr_btree_reserve;
		fallthrough;
	case BCH_WATERMARK_btree:
		reserved += ca->nr_btree_reserve;
		fallthrough;
	case BCH_WATERMARK_btree_copygc:
	case BCH_WATERMARK_reclaim:
		break;
	}

	return reserved;
}

static inline u64 dev_buckets_free(struct bch_dev *ca,
				   struct bch_dev_usage usage,
				   enum bch_watermark watermark)
{
	return max_t(s64, 0,
		     usage.d[BCH_DATA_free].buckets -
		     ca->nr_open_buckets -
		     bch2_dev_buckets_reserved(ca, watermark));
}

static inline u64 __dev_buckets_available(struct bch_dev *ca,
					  struct bch_dev_usage usage,
					  enum bch_watermark watermark)
{
	return max_t(s64, 0,
		       usage.d[BCH_DATA_free].buckets
		     + usage.d[BCH_DATA_cached].buckets
		     + usage.d[BCH_DATA_need_gc_gens].buckets
		     + usage.d[BCH_DATA_need_discard].buckets
		     - ca->nr_open_buckets
		     - bch2_dev_buckets_reserved(ca, watermark));
}

static inline u64 dev_buckets_available(struct bch_dev *ca,
					enum bch_watermark watermark)
{
	return __dev_buckets_available(ca, bch2_dev_usage_read(ca), watermark);
}

/* Filesystem usage: */

static inline unsigned __fs_usage_u64s(unsigned nr_replicas)
{
	return sizeof(struct bch_fs_usage) / sizeof(u64) + nr_replicas;
}

static inline unsigned fs_usage_u64s(struct bch_fs *c)
{
	return __fs_usage_u64s(READ_ONCE(c->replicas.nr));
}

static inline unsigned __fs_usage_online_u64s(unsigned nr_replicas)
{
	return sizeof(struct bch_fs_usage_online) / sizeof(u64) + nr_replicas;
}

static inline unsigned fs_usage_online_u64s(struct bch_fs *c)
{
	return __fs_usage_online_u64s(READ_ONCE(c->replicas.nr));
}

static inline unsigned dev_usage_u64s(void)
{
	return sizeof(struct bch_dev_usage) / sizeof(u64);
}

u64 bch2_fs_usage_read_one(struct bch_fs *, u64 *);

struct bch_fs_usage_online *bch2_fs_usage_read(struct bch_fs *);

void bch2_fs_usage_acc_to_base(struct bch_fs *, unsigned);

void bch2_fs_usage_to_text(struct printbuf *,
			   struct bch_fs *, struct bch_fs_usage_online *);

u64 bch2_fs_sectors_used(struct bch_fs *, struct bch_fs_usage_online *);

struct bch_fs_usage_short
bch2_fs_usage_read_short(struct bch_fs *);

void bch2_dev_usage_update(struct bch_fs *, struct bch_dev *,
			   const struct bch_alloc_v4 *,
			   const struct bch_alloc_v4 *, u64, bool);
void bch2_dev_usage_update_m(struct bch_fs *, struct bch_dev *,
			     struct bucket *, struct bucket *);

/* key/bucket marking: */

static inline struct bch_fs_usage *fs_usage_ptr(struct bch_fs *c,
						unsigned journal_seq,
						bool gc)
{
	percpu_rwsem_assert_held(&c->mark_lock);
	BUG_ON(!gc && !journal_seq);

	return this_cpu_ptr(gc
			    ? c->usage_gc
			    : c->usage[journal_seq & JOURNAL_BUF_MASK]);
}

int bch2_update_replicas(struct bch_fs *, struct bkey_s_c,
			 struct bch_replicas_entry_v1 *, s64,
			 unsigned, bool);
int bch2_update_replicas_list(struct btree_trans *,
			 struct bch_replicas_entry_v1 *, s64);
int bch2_update_cached_sectors_list(struct btree_trans *, unsigned, s64);
int bch2_replicas_deltas_realloc(struct btree_trans *, unsigned);

void bch2_fs_usage_initialize(struct bch_fs *);

int bch2_check_bucket_ref(struct btree_trans *, struct bkey_s_c,
			  const struct bch_extent_ptr *,
			  s64, enum bch_data_type, u8, u8, u32);

int bch2_mark_metadata_bucket(struct bch_fs *, struct bch_dev *,
			      size_t, enum bch_data_type, unsigned,
			      struct gc_pos, unsigned);

int bch2_trigger_extent(struct btree_trans *, enum btree_id, unsigned,
			struct bkey_s_c, struct bkey_s, unsigned);
int bch2_trigger_reservation(struct btree_trans *, enum btree_id, unsigned,
			  struct bkey_s_c, struct bkey_s, unsigned);

#define trigger_run_overwrite_then_insert(_fn, _trans, _btree_id, _level, _old, _new, _flags)\
({												\
	int ret = 0;										\
												\
	if (_old.k->type)									\
		ret = _fn(_trans, _btree_id, _level, _old, _flags & ~BTREE_TRIGGER_INSERT);	\
	if (!ret && _new.k->type)								\
		ret = _fn(_trans, _btree_id, _level, _new.s_c, _flags & ~BTREE_TRIGGER_OVERWRITE);\
	ret;											\
})

void bch2_trans_fs_usage_revert(struct btree_trans *, struct replicas_delta_list *);
int bch2_trans_fs_usage_apply(struct btree_trans *, struct replicas_delta_list *);

int bch2_trans_mark_metadata_bucket(struct btree_trans *, struct bch_dev *,
				    size_t, enum bch_data_type, unsigned);
int bch2_trans_mark_dev_sb(struct bch_fs *, struct bch_dev *);
int bch2_trans_mark_dev_sbs(struct bch_fs *);

static inline bool is_superblock_bucket(struct bch_dev *ca, u64 b)
{
	struct bch_sb_layout *layout = &ca->disk_sb.sb->layout;
	u64 b_offset	= bucket_to_sector(ca, b);
	u64 b_end	= bucket_to_sector(ca, b + 1);
	unsigned i;

	if (!b)
		return true;

	for (i = 0; i < layout->nr_superblocks; i++) {
		u64 offset = le64_to_cpu(layout->sb_offset[i]);
		u64 end = offset + (1 << layout->sb_max_size_bits);

		if (!(offset >= b_end || end <= b_offset))
			return true;
	}

	return false;
}

/* disk reservations: */

static inline void bch2_disk_reservation_put(struct bch_fs *c,
					     struct disk_reservation *res)
{
	if (res->sectors) {
		this_cpu_sub(*c->online_reserved, res->sectors);
		res->sectors = 0;
	}
}

#define BCH_DISK_RESERVATION_NOFAIL		(1 << 0)

int __bch2_disk_reservation_add(struct bch_fs *,
				struct disk_reservation *,
				u64, int);

static inline int bch2_disk_reservation_add(struct bch_fs *c, struct disk_reservation *res,
					    u64 sectors, int flags)
{
#ifdef __KERNEL__
	u64 old, new;

	do {
		old = this_cpu_read(c->pcpu->sectors_available);
		if (sectors > old)
			return __bch2_disk_reservation_add(c, res, sectors, flags);

		new = old - sectors;
	} while (this_cpu_cmpxchg(c->pcpu->sectors_available, old, new) != old);

	this_cpu_add(*c->online_reserved, sectors);
	res->sectors			+= sectors;
	return 0;
#else
	return __bch2_disk_reservation_add(c, res, sectors, flags);
#endif
}

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
					    u64 sectors, unsigned nr_replicas,
					    int flags)
{
	*res = bch2_disk_reservation_init(c, nr_replicas);

	return bch2_disk_reservation_add(c, res, sectors * nr_replicas, flags);
}

#define RESERVE_FACTOR	6

static inline u64 avail_factor(u64 r)
{
	return div_u64(r << RESERVE_FACTOR, (1 << RESERVE_FACTOR) + 1);
}

int bch2_dev_buckets_resize(struct bch_fs *, struct bch_dev *, u64);
void bch2_dev_buckets_free(struct bch_dev *);
int bch2_dev_buckets_alloc(struct bch_fs *, struct bch_dev *);

#endif /* _BUCKETS_H */
