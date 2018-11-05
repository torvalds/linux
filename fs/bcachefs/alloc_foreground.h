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

struct dev_alloc_list {
	unsigned	nr;
	u8		devs[BCH_SB_MEMBERS_MAX];
};

struct dev_alloc_list bch2_wp_alloc_list(struct bch_fs *,
					 struct write_point *,
					 struct bch_devs_mask *);
void bch2_wp_rescale(struct bch_fs *, struct bch_dev *,
		     struct write_point *);

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
		atomic_inc(&ob->pin);
		ob_push(c, ptrs, ob);
	}
}

struct write_point *bch2_alloc_sectors_start(struct bch_fs *,
					     unsigned,
					     struct write_point_specifier,
					     struct bch_devs_list *,
					     unsigned, unsigned,
					     enum alloc_reserve,
					     unsigned,
					     struct closure *);

void bch2_alloc_sectors_append_ptrs(struct bch_fs *, struct write_point *,
				    struct bkey_i_extent *, unsigned);
void bch2_alloc_sectors_done(struct bch_fs *, struct write_point *);

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

static inline void writepoint_init(struct write_point *wp,
				   enum bch_data_type type)
{
	mutex_init(&wp->lock);
	wp->type = type;
}

void bch2_fs_allocator_foreground_init(struct bch_fs *);

#endif /* _BCACHEFS_ALLOC_FOREGROUND_H */
