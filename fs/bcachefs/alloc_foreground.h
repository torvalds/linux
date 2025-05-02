/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_ALLOC_FOREGROUND_H
#define _BCACHEFS_ALLOC_FOREGROUND_H

#include "bcachefs.h"
#include "alloc_types.h"
#include "extents.h"
#include "sb-members.h"

#include <linux/hash.h>

struct bkey;
struct bch_dev;
struct bch_fs;
struct bch_devs_List;

extern const char * const bch2_watermarks[];

void bch2_reset_alloc_cursors(struct bch_fs *);

struct dev_alloc_list {
	unsigned	nr;
	u8		data[BCH_SB_MEMBERS_MAX];
};

struct dev_alloc_list bch2_dev_alloc_list(struct bch_fs *,
					  struct dev_stripe_state *,
					  struct bch_devs_mask *);
void bch2_dev_stripe_increment(struct bch_dev *, struct dev_stripe_state *);

static inline struct bch_dev *ob_dev(struct bch_fs *c, struct open_bucket *ob)
{
	return bch2_dev_have_ref(c, ob->dev);
}

static inline unsigned bch2_open_buckets_reserved(enum bch_watermark watermark)
{
	switch (watermark) {
	case BCH_WATERMARK_interior_updates:
		return 0;
	case BCH_WATERMARK_reclaim:
		return OPEN_BUCKETS_COUNT / 6;
	case BCH_WATERMARK_btree:
	case BCH_WATERMARK_btree_copygc:
		return OPEN_BUCKETS_COUNT / 4;
	case BCH_WATERMARK_copygc:
		return OPEN_BUCKETS_COUNT / 3;
	default:
		return OPEN_BUCKETS_COUNT / 2;
	}
}

struct open_bucket *bch2_bucket_alloc(struct bch_fs *, struct bch_dev *,
				      enum bch_watermark, enum bch_data_type,
				      struct closure *);

static inline void ob_push(struct bch_fs *c, struct open_buckets *obs,
			   struct open_bucket *ob)
{
	BUG_ON(obs->nr >= ARRAY_SIZE(obs->v));

	obs->v[obs->nr++] = ob - c->open_buckets;
}

#define open_bucket_for_each(_c, _obs, _ob, _i)				\
	for ((_i) = 0;							\
	     (_i) < (_obs)->nr &&					\
	     ((_ob) = (_c)->open_buckets + (_obs)->v[_i], true);	\
	     (_i)++)

static inline struct open_bucket *ec_open_bucket(struct bch_fs *c,
						 struct open_buckets *obs)
{
	struct open_bucket *ob;
	unsigned i;

	open_bucket_for_each(c, obs, ob, i)
		if (ob->ec)
			return ob;

	return NULL;
}

void bch2_open_bucket_write_error(struct bch_fs *,
			struct open_buckets *, unsigned, int);

void __bch2_open_bucket_put(struct bch_fs *, struct open_bucket *);

static inline void bch2_open_bucket_put(struct bch_fs *c, struct open_bucket *ob)
{
	if (atomic_dec_and_test(&ob->pin))
		__bch2_open_bucket_put(c, ob);
}

static inline void bch2_open_buckets_put(struct bch_fs *c,
					 struct open_buckets *ptrs)
{
	struct open_bucket *ob;
	unsigned i;

	open_bucket_for_each(c, ptrs, ob, i)
		bch2_open_bucket_put(c, ob);
	ptrs->nr = 0;
}

static inline void bch2_alloc_sectors_done_inlined(struct bch_fs *c, struct write_point *wp)
{
	struct open_buckets ptrs = { .nr = 0 }, keep = { .nr = 0 };
	struct open_bucket *ob;
	unsigned i;

	open_bucket_for_each(c, &wp->ptrs, ob, i)
		ob_push(c, ob->sectors_free < block_sectors(c)
			? &ptrs
			: &keep, ob);
	wp->ptrs = keep;

	mutex_unlock(&wp->lock);

	bch2_open_buckets_put(c, &ptrs);
}

static inline void bch2_open_bucket_get(struct bch_fs *c,
					struct write_point *wp,
					struct open_buckets *ptrs)
{
	struct open_bucket *ob;
	unsigned i;

	open_bucket_for_each(c, &wp->ptrs, ob, i) {
		ob->data_type = wp->data_type;
		atomic_inc(&ob->pin);
		ob_push(c, ptrs, ob);
	}
}

static inline open_bucket_idx_t *open_bucket_hashslot(struct bch_fs *c,
						  unsigned dev, u64 bucket)
{
	return c->open_buckets_hash +
		(jhash_3words(dev, bucket, bucket >> 32, 0) &
		 (OPEN_BUCKETS_COUNT - 1));
}

static inline bool bch2_bucket_is_open(struct bch_fs *c, unsigned dev, u64 bucket)
{
	open_bucket_idx_t slot = *open_bucket_hashslot(c, dev, bucket);

	while (slot) {
		struct open_bucket *ob = &c->open_buckets[slot];

		if (ob->dev == dev && ob->bucket == bucket)
			return true;

		slot = ob->hash;
	}

	return false;
}

static inline bool bch2_bucket_is_open_safe(struct bch_fs *c, unsigned dev, u64 bucket)
{
	bool ret;

	if (bch2_bucket_is_open(c, dev, bucket))
		return true;

	spin_lock(&c->freelist_lock);
	ret = bch2_bucket_is_open(c, dev, bucket);
	spin_unlock(&c->freelist_lock);

	return ret;
}

enum bch_write_flags;
int bch2_bucket_alloc_set_trans(struct btree_trans *, struct open_buckets *,
		      struct dev_stripe_state *, struct bch_devs_mask *,
		      unsigned, unsigned *, bool *, enum bch_write_flags,
		      enum bch_data_type, enum bch_watermark,
		      struct closure *);

int bch2_alloc_sectors_start_trans(struct btree_trans *,
				   unsigned, unsigned,
				   struct write_point_specifier,
				   struct bch_devs_list *,
				   unsigned, unsigned,
				   enum bch_watermark,
				   enum bch_write_flags,
				   struct closure *,
				   struct write_point **);

struct bch_extent_ptr bch2_ob_ptr(struct bch_fs *, struct open_bucket *);

/*
 * Append pointers to the space we just allocated to @k, and mark @sectors space
 * as allocated out of @ob
 */
static inline void
bch2_alloc_sectors_append_ptrs_inlined(struct bch_fs *c, struct write_point *wp,
				       struct bkey_i *k, unsigned sectors,
				       bool cached)
{
	struct open_bucket *ob;
	unsigned i;

	BUG_ON(sectors > wp->sectors_free);
	wp->sectors_free	-= sectors;
	wp->sectors_allocated	+= sectors;

	open_bucket_for_each(c, &wp->ptrs, ob, i) {
		struct bch_dev *ca = ob_dev(c, ob);
		struct bch_extent_ptr ptr = bch2_ob_ptr(c, ob);

		ptr.cached = cached ||
			(!ca->mi.durability &&
			 wp->data_type == BCH_DATA_user);

		bch2_bkey_append_ptr(k, ptr);

		BUG_ON(sectors > ob->sectors_free);
		ob->sectors_free -= sectors;
	}
}

void bch2_alloc_sectors_append_ptrs(struct bch_fs *, struct write_point *,
				    struct bkey_i *, unsigned, bool);
void bch2_alloc_sectors_done(struct bch_fs *, struct write_point *);

void bch2_open_buckets_stop(struct bch_fs *c, struct bch_dev *, bool);

static inline struct write_point_specifier writepoint_hashed(unsigned long v)
{
	return (struct write_point_specifier) { .v = v | 1 };
}

static inline struct write_point_specifier writepoint_ptr(struct write_point *wp)
{
	return (struct write_point_specifier) { .v = (unsigned long) wp };
}

void bch2_fs_allocator_foreground_init(struct bch_fs *);

void bch2_open_bucket_to_text(struct printbuf *, struct bch_fs *, struct open_bucket *);
void bch2_open_buckets_to_text(struct printbuf *, struct bch_fs *, struct bch_dev *);
void bch2_open_buckets_partial_to_text(struct printbuf *, struct bch_fs *);

void bch2_write_points_to_text(struct printbuf *, struct bch_fs *);

void bch2_fs_alloc_debug_to_text(struct printbuf *, struct bch_fs *);
void bch2_dev_alloc_debug_to_text(struct printbuf *, struct bch_dev *);

void __bch2_wait_on_allocator(struct bch_fs *, struct closure *);
static inline void bch2_wait_on_allocator(struct bch_fs *c, struct closure *cl)
{
	if (cl->closure_get_happened)
		__bch2_wait_on_allocator(c, cl);
}

#endif /* _BCACHEFS_ALLOC_FOREGROUND_H */
