/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_ALLOC_FOREGROUND_H
#define _BCACHEFS_ALLOC_FOREGROUND_H

#include "bcachefs.h"
#include "alloc_types.h"

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

int bch2_bucket_alloc(struct bch_fs *, struct bch_dev *, enum alloc_reserve, bool,
		      struct closure *);

#define __writepoint_for_each_ptr(_wp, _ob, _i, _start)			\
	for ((_i) = (_start);						\
	     (_i) < (_wp)->nr_ptrs && ((_ob) = (_wp)->ptrs[_i], true);	\
	     (_i)++)

#define writepoint_for_each_ptr_all(_wp, _ob, _i)			\
	__writepoint_for_each_ptr(_wp, _ob, _i, 0)

#define writepoint_for_each_ptr(_wp, _ob, _i)				\
	__writepoint_for_each_ptr(_wp, _ob, _i, wp->first_ptr)

void __bch2_open_bucket_put(struct bch_fs *, struct open_bucket *);

static inline void bch2_open_bucket_put(struct bch_fs *c, struct open_bucket *ob)
{
	if (atomic_dec_and_test(&ob->pin))
		__bch2_open_bucket_put(c, ob);
}

static inline void bch2_open_bucket_put_refs(struct bch_fs *c, u8 *nr, u8 *refs)
{
	unsigned i;

	for (i = 0; i < *nr; i++)
		bch2_open_bucket_put(c, c->open_buckets + refs[i]);

	*nr = 0;
}

static inline void bch2_open_bucket_get(struct bch_fs *c,
					struct write_point *wp,
					u8 *nr, u8 *refs)
{
	struct open_bucket *ob;
	unsigned i;

	writepoint_for_each_ptr(wp, ob, i) {
		atomic_inc(&ob->pin);
		refs[(*nr)++] = ob - c->open_buckets;
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

void bch2_writepoint_drop_ptrs(struct bch_fs *, struct write_point *,
			       u16, bool);

static inline struct hlist_head *writepoint_hash(struct bch_fs *c,
						 unsigned long write_point)
{
	unsigned hash =
		hash_long(write_point, ilog2(ARRAY_SIZE(c->write_points_hash)));

	return &c->write_points_hash[hash];
}

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

#endif /* _BCACHEFS_ALLOC_FOREGROUND_H */
