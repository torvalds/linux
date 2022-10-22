/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_ALLOC_BACKGROUND_H
#define _BCACHEFS_ALLOC_BACKGROUND_H

#include "bcachefs.h"
#include "alloc_types.h"
#include "buckets.h"
#include "debug.h"
#include "super.h"

/* How out of date a pointer gen is allowed to be: */
#define BUCKET_GC_GEN_MAX	96U

static inline bool bch2_dev_bucket_exists(struct bch_fs *c, struct bpos pos)
{
	struct bch_dev *ca;

	if (!bch2_dev_exists2(c, pos.inode))
		return false;

	ca = bch_dev_bkey_exists(c, pos.inode);
	return pos.offset >= ca->mi.first_bucket &&
		pos.offset < ca->mi.nbuckets;
}

static inline u8 alloc_gc_gen(struct bch_alloc_v4 a)
{
	return a.gen - a.oldest_gen;
}

static inline enum bch_data_type __alloc_data_type(u32 dirty_sectors,
						   u32 cached_sectors,
						   u32 stripe,
						   struct bch_alloc_v4 a,
						   enum bch_data_type data_type)
{
	if (dirty_sectors)
		return data_type;
	if (stripe)
		return BCH_DATA_stripe;
	if (cached_sectors)
		return BCH_DATA_cached;
	if (BCH_ALLOC_V4_NEED_DISCARD(&a))
		return BCH_DATA_need_discard;
	if (alloc_gc_gen(a) >= BUCKET_GC_GEN_MAX)
		return BCH_DATA_need_gc_gens;
	return BCH_DATA_free;
}

static inline enum bch_data_type alloc_data_type(struct bch_alloc_v4 a,
						 enum bch_data_type data_type)
{
	return __alloc_data_type(a.dirty_sectors, a.cached_sectors,
				 a.stripe, a, data_type);
}

static inline u64 alloc_lru_idx(struct bch_alloc_v4 a)
{
	return a.data_type == BCH_DATA_cached ? a.io_time[READ] : 0;
}

static inline u64 alloc_freespace_genbits(struct bch_alloc_v4 a)
{
	return ((u64) alloc_gc_gen(a) >> 4) << 56;
}

static inline struct bpos alloc_freespace_pos(struct bpos pos, struct bch_alloc_v4 a)
{
	pos.offset |= alloc_freespace_genbits(a);
	return pos;
}

struct bkey_i_alloc_v4 *
bch2_trans_start_alloc_update(struct btree_trans *, struct btree_iter *, struct bpos);

void bch2_alloc_to_v4(struct bkey_s_c, struct bch_alloc_v4 *);
struct bkey_i_alloc_v4 *bch2_alloc_to_v4_mut(struct btree_trans *, struct bkey_s_c);

int bch2_bucket_io_time_reset(struct btree_trans *, unsigned, size_t, int);

#define ALLOC_SCAN_BATCH(ca)		max_t(size_t, 1, (ca)->mi.nbuckets >> 9)

int bch2_alloc_v1_invalid(const struct bch_fs *, struct bkey_s_c, int, struct printbuf *);
int bch2_alloc_v2_invalid(const struct bch_fs *, struct bkey_s_c, int, struct printbuf *);
int bch2_alloc_v3_invalid(const struct bch_fs *, struct bkey_s_c, int, struct printbuf *);
int bch2_alloc_v4_invalid(const struct bch_fs *, struct bkey_s_c, int, struct printbuf *);
void bch2_alloc_v4_swab(struct bkey_s);
void bch2_alloc_to_text(struct printbuf *, struct bch_fs *, struct bkey_s_c);

#define bch2_bkey_ops_alloc ((struct bkey_ops) {	\
	.key_invalid	= bch2_alloc_v1_invalid,	\
	.val_to_text	= bch2_alloc_to_text,		\
	.trans_trigger	= bch2_trans_mark_alloc,	\
	.atomic_trigger	= bch2_mark_alloc,		\
})

#define bch2_bkey_ops_alloc_v2 ((struct bkey_ops) {	\
	.key_invalid	= bch2_alloc_v2_invalid,	\
	.val_to_text	= bch2_alloc_to_text,		\
	.trans_trigger	= bch2_trans_mark_alloc,	\
	.atomic_trigger	= bch2_mark_alloc,		\
})

#define bch2_bkey_ops_alloc_v3 ((struct bkey_ops) {	\
	.key_invalid	= bch2_alloc_v3_invalid,	\
	.val_to_text	= bch2_alloc_to_text,		\
	.trans_trigger	= bch2_trans_mark_alloc,	\
	.atomic_trigger	= bch2_mark_alloc,		\
})

#define bch2_bkey_ops_alloc_v4 ((struct bkey_ops) {	\
	.key_invalid	= bch2_alloc_v4_invalid,	\
	.val_to_text	= bch2_alloc_to_text,		\
	.swab		= bch2_alloc_v4_swab,		\
	.trans_trigger	= bch2_trans_mark_alloc,	\
	.atomic_trigger	= bch2_mark_alloc,		\
})

static inline bool bkey_is_alloc(const struct bkey *k)
{
	return  k->type == KEY_TYPE_alloc ||
		k->type == KEY_TYPE_alloc_v2 ||
		k->type == KEY_TYPE_alloc_v3;
}

int bch2_alloc_read(struct bch_fs *);

int bch2_trans_mark_alloc(struct btree_trans *, enum btree_id, unsigned,
			  struct bkey_s_c, struct bkey_i *, unsigned);
int bch2_check_alloc_info(struct bch_fs *);
int bch2_check_alloc_to_lru_refs(struct bch_fs *);
void bch2_do_discards(struct bch_fs *);

static inline u64 should_invalidate_buckets(struct bch_dev *ca,
					    struct bch_dev_usage u)
{
	u64 want_free = ca->mi.nbuckets >> 7;
	u64 free = max_t(s64, 0,
			   u.d[BCH_DATA_free].buckets
			 + u.d[BCH_DATA_need_discard].buckets
			 - bch2_dev_buckets_reserved(ca, RESERVE_none));

	return clamp_t(s64, want_free - free, 0, u.d[BCH_DATA_cached].buckets);
}

void bch2_do_invalidates(struct bch_fs *);

int bch2_fs_freespace_init(struct bch_fs *);

void bch2_recalc_capacity(struct bch_fs *);

void bch2_dev_allocator_remove(struct bch_fs *, struct bch_dev *);
void bch2_dev_allocator_add(struct bch_fs *, struct bch_dev *);

void bch2_fs_allocator_background_init(struct bch_fs *);

#endif /* _BCACHEFS_ALLOC_BACKGROUND_H */
