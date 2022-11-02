/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_ALLOC_FOREGROUND_H
#define _BCACHEFS_ALLOC_FOREGROUND_H

#include "bcachefs.h"
#include "alloc_types.h"

#include <linux/hash.h>

struct bkey;
struct bch_dev;
struct bch_fs;
struct bch_devs_List;

extern const char * const bch2_alloc_reserves[];

void bch2_reset_alloc_cursors(struct bch_fs *);

struct dev_alloc_list {
	unsigned	nr;
	u8		devs[BCH_SB_MEMBERS_MAX];
};

struct dev_alloc_list bch2_dev_alloc_list(struct bch_fs *,
					  struct dev_stripe_state *,
					  struct bch_devs_mask *);
void bch2_dev_stripe_increment(struct bch_dev *, struct dev_stripe_state *);

long bch2_bucket_alloc_new_fs(struct bch_dev *);

struct open_bucket *bch2_bucket_alloc(struct bch_fs *, struct bch_dev *,
				      enum alloc_reserve, bool,
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
			struct open_buckets *, unsigned);

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

int bch2_bucket_alloc_set(struct bch_fs *, struct open_buckets *,
		      struct dev_stripe_state *, struct bch_devs_mask *,
		      unsigned, unsigned *, bool *, enum alloc_reserve,
		      unsigned, struct closure *);

int bch2_alloc_sectors_start_trans(struct btree_trans *,
				   unsigned, unsigned,
				   struct write_point_specifier,
				   struct bch_devs_list *,
				   unsigned, unsigned,
				   enum alloc_reserve,
				   unsigned,
				   struct closure *,
				   struct write_point **);

struct bch_extent_ptr bch2_ob_ptr(struct bch_fs *, struct open_bucket *);
void bch2_alloc_sectors_append_ptrs(struct bch_fs *, struct write_point *,
				    struct bkey_i *, unsigned, bool);
void bch2_alloc_sectors_done(struct bch_fs *, struct write_point *);

void bch2_open_buckets_stop_dev(struct bch_fs *, struct bch_dev *,
				struct open_buckets *);

void bch2_writepoint_stop(struct bch_fs *, struct bch_dev *,
			  struct write_point *);

static inline struct write_point_specifier writepoint_hashed(unsigned long v)
{
	return (struct write_point_specifier) { .v = v | 1 };
}

static inline struct write_point_specifier writepoint_ptr(struct write_point *wp)
{
	return (struct write_point_specifier) { .v = (unsigned long) wp };
}

void bch2_fs_allocator_foreground_init(struct bch_fs *);

void bch2_open_buckets_to_text(struct printbuf *, struct bch_fs *);

void bch2_write_points_to_text(struct printbuf *, struct bch_fs *);

#endif /* _BCACHEFS_ALLOC_FOREGROUND_H */
