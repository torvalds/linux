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
	rcu_read_lock();
	struct bch_dev *ca = bch2_dev_rcu_noerror(c, pos.inode);
	bool ret = ca && bucket_valid(ca, pos.offset);
	rcu_read_unlock();
	return ret;
}

static inline u64 bucket_to_u64(struct bpos bucket)
{
	return (bucket.inode << 48) | bucket.offset;
}

static inline struct bpos u64_to_bucket(u64 bucket)
{
	return POS(bucket >> 48, bucket & ~(~0ULL << 48));
}

static inline u8 alloc_gc_gen(struct bch_alloc_v4 a)
{
	return a.gen - a.oldest_gen;
}

static inline void alloc_to_bucket(struct bucket *dst, struct bch_alloc_v4 src)
{
	dst->gen		= src.gen;
	dst->data_type		= src.data_type;
	dst->stripe_sectors	= src.stripe_sectors;
	dst->dirty_sectors	= src.dirty_sectors;
	dst->cached_sectors	= src.cached_sectors;
	dst->stripe		= src.stripe;
}

static inline void __bucket_m_to_alloc(struct bch_alloc_v4 *dst, struct bucket src)
{
	dst->gen		= src.gen;
	dst->data_type		= src.data_type;
	dst->stripe_sectors	= src.stripe_sectors;
	dst->dirty_sectors	= src.dirty_sectors;
	dst->cached_sectors	= src.cached_sectors;
	dst->stripe		= src.stripe;
}

static inline struct bch_alloc_v4 bucket_m_to_alloc(struct bucket b)
{
	struct bch_alloc_v4 ret = {};
	__bucket_m_to_alloc(&ret, b);
	return ret;
}

static inline enum bch_data_type bucket_data_type(enum bch_data_type data_type)
{
	switch (data_type) {
	case BCH_DATA_cached:
	case BCH_DATA_stripe:
		return BCH_DATA_user;
	default:
		return data_type;
	}
}

static inline bool bucket_data_type_mismatch(enum bch_data_type bucket,
					     enum bch_data_type ptr)
{
	return !data_type_is_empty(bucket) &&
		bucket_data_type(bucket) != bucket_data_type(ptr);
}

/*
 * It is my general preference to use unsigned types for unsigned quantities -
 * however, these helpers are used in disk accounting calculations run by
 * triggers where the output will be negated and added to an s64. unsigned is
 * right out even though all these quantities will fit in 32 bits, since it
 * won't be sign extended correctly; u64 will negate "correctly", but s64 is the
 * simpler option here.
 */
static inline s64 bch2_bucket_sectors_total(struct bch_alloc_v4 a)
{
	return a.stripe_sectors + a.dirty_sectors + a.cached_sectors;
}

static inline s64 bch2_bucket_sectors_dirty(struct bch_alloc_v4 a)
{
	return a.stripe_sectors + a.dirty_sectors;
}

static inline s64 bch2_bucket_sectors(struct bch_alloc_v4 a)
{
	return a.data_type == BCH_DATA_cached
		? a.cached_sectors
		: bch2_bucket_sectors_dirty(a);
}

static inline s64 bch2_bucket_sectors_fragmented(struct bch_dev *ca,
						 struct bch_alloc_v4 a)
{
	int d = bch2_bucket_sectors(a);

	return d ? max(0, ca->mi.bucket_size - d) : 0;
}

static inline s64 bch2_gc_bucket_sectors_fragmented(struct bch_dev *ca, struct bucket a)
{
	int d = a.stripe_sectors + a.dirty_sectors;

	return d ? max(0, ca->mi.bucket_size - d) : 0;
}

static inline s64 bch2_bucket_sectors_unstriped(struct bch_alloc_v4 a)
{
	return a.data_type == BCH_DATA_stripe ? a.dirty_sectors : 0;
}

static inline enum bch_data_type alloc_data_type(struct bch_alloc_v4 a,
						 enum bch_data_type data_type)
{
	if (a.stripe)
		return data_type == BCH_DATA_parity ? data_type : BCH_DATA_stripe;
	if (bch2_bucket_sectors_dirty(a))
		return data_type;
	if (a.cached_sectors)
		return BCH_DATA_cached;
	if (BCH_ALLOC_V4_NEED_DISCARD(&a))
		return BCH_DATA_need_discard;
	if (alloc_gc_gen(a) >= BUCKET_GC_GEN_MAX)
		return BCH_DATA_need_gc_gens;
	return BCH_DATA_free;
}

static inline void alloc_data_type_set(struct bch_alloc_v4 *a, enum bch_data_type data_type)
{
	a->data_type = alloc_data_type(*a, data_type);
}

static inline u64 alloc_lru_idx_read(struct bch_alloc_v4 a)
{
	return a.data_type == BCH_DATA_cached
		? a.io_time[READ] & LRU_TIME_MAX
		: 0;
}

#define DATA_TYPES_MOVABLE		\
	((1U << BCH_DATA_btree)|	\
	 (1U << BCH_DATA_user)|		\
	 (1U << BCH_DATA_stripe))

static inline bool data_type_movable(enum bch_data_type type)
{
	return (1U << type) & DATA_TYPES_MOVABLE;
}

static inline u64 alloc_lru_idx_fragmentation(struct bch_alloc_v4 a,
					      struct bch_dev *ca)
{
	if (a.data_type >= BCH_DATA_NR)
		return 0;

	if (!data_type_movable(a.data_type) ||
	    !bch2_bucket_sectors_fragmented(ca, a))
		return 0;

	/*
	 * avoid overflowing LRU_TIME_BITS on a corrupted fs, when
	 * bucket_sectors_dirty is (much) bigger than bucket_size
	 */
	u64 d = min_t(s64, bch2_bucket_sectors_dirty(a),
		      ca->mi.bucket_size);

	return div_u64(d * (1ULL << 31), ca->mi.bucket_size);
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

static inline unsigned alloc_v4_u64s_noerror(const struct bch_alloc_v4 *a)
{
	return (BCH_ALLOC_V4_BACKPOINTERS_START(a) ?:
			BCH_ALLOC_V4_U64s_V0) +
		BCH_ALLOC_V4_NR_BACKPOINTERS(a) *
		(sizeof(struct bch_backpointer) / sizeof(u64));
}

static inline unsigned alloc_v4_u64s(const struct bch_alloc_v4 *a)
{
	unsigned ret = alloc_v4_u64s_noerror(a);
	BUG_ON(ret > U8_MAX - BKEY_U64s);
	return ret;
}

static inline void set_alloc_v4_u64s(struct bkey_i_alloc_v4 *a)
{
	set_bkey_val_u64s(&a->k, alloc_v4_u64s(&a->v));
}

struct bkey_i_alloc_v4 *
bch2_trans_start_alloc_update_noupdate(struct btree_trans *, struct btree_iter *, struct bpos);
struct bkey_i_alloc_v4 *
bch2_trans_start_alloc_update(struct btree_trans *, struct bpos,
			      enum btree_iter_update_trigger_flags);

void __bch2_alloc_to_v4(struct bkey_s_c, struct bch_alloc_v4 *);

static inline const struct bch_alloc_v4 *bch2_alloc_to_v4(struct bkey_s_c k, struct bch_alloc_v4 *convert)
{
	const struct bch_alloc_v4 *ret;

	if (unlikely(k.k->type != KEY_TYPE_alloc_v4))
		goto slowpath;

	ret = bkey_s_c_to_alloc_v4(k).v;
	if (BCH_ALLOC_V4_BACKPOINTERS_START(ret) != BCH_ALLOC_V4_U64s)
		goto slowpath;

	return ret;
slowpath:
	__bch2_alloc_to_v4(k, convert);
	return convert;
}

struct bkey_i_alloc_v4 *bch2_alloc_to_v4_mut(struct btree_trans *, struct bkey_s_c);

int bch2_bucket_io_time_reset(struct btree_trans *, unsigned, size_t, int);

int bch2_alloc_v1_validate(struct bch_fs *, struct bkey_s_c,
			   struct bkey_validate_context);
int bch2_alloc_v2_validate(struct bch_fs *, struct bkey_s_c,
			   struct bkey_validate_context);
int bch2_alloc_v3_validate(struct bch_fs *, struct bkey_s_c,
			   struct bkey_validate_context);
int bch2_alloc_v4_validate(struct bch_fs *, struct bkey_s_c,
			   struct bkey_validate_context);
void bch2_alloc_v4_swab(struct bkey_s);
void bch2_alloc_to_text(struct printbuf *, struct bch_fs *, struct bkey_s_c);

#define bch2_bkey_ops_alloc ((struct bkey_ops) {	\
	.key_validate	= bch2_alloc_v1_validate,	\
	.val_to_text	= bch2_alloc_to_text,		\
	.trigger	= bch2_trigger_alloc,		\
	.min_val_size	= 8,				\
})

#define bch2_bkey_ops_alloc_v2 ((struct bkey_ops) {	\
	.key_validate	= bch2_alloc_v2_validate,	\
	.val_to_text	= bch2_alloc_to_text,		\
	.trigger	= bch2_trigger_alloc,		\
	.min_val_size	= 8,				\
})

#define bch2_bkey_ops_alloc_v3 ((struct bkey_ops) {	\
	.key_validate	= bch2_alloc_v3_validate,	\
	.val_to_text	= bch2_alloc_to_text,		\
	.trigger	= bch2_trigger_alloc,		\
	.min_val_size	= 16,				\
})

#define bch2_bkey_ops_alloc_v4 ((struct bkey_ops) {	\
	.key_validate	= bch2_alloc_v4_validate,	\
	.val_to_text	= bch2_alloc_to_text,		\
	.swab		= bch2_alloc_v4_swab,		\
	.trigger	= bch2_trigger_alloc,		\
	.min_val_size	= 48,				\
})

int bch2_bucket_gens_validate(struct bch_fs *, struct bkey_s_c,
			      struct bkey_validate_context);
void bch2_bucket_gens_to_text(struct printbuf *, struct bch_fs *, struct bkey_s_c);

#define bch2_bkey_ops_bucket_gens ((struct bkey_ops) {	\
	.key_validate	= bch2_bucket_gens_validate,	\
	.val_to_text	= bch2_bucket_gens_to_text,	\
})

int bch2_bucket_gens_init(struct bch_fs *);

static inline bool bkey_is_alloc(const struct bkey *k)
{
	return  k->type == KEY_TYPE_alloc ||
		k->type == KEY_TYPE_alloc_v2 ||
		k->type == KEY_TYPE_alloc_v3;
}

int bch2_alloc_read(struct bch_fs *);

int bch2_alloc_key_to_dev_counters(struct btree_trans *, struct bch_dev *,
				   const struct bch_alloc_v4 *,
				   const struct bch_alloc_v4 *, unsigned);
int bch2_trigger_alloc(struct btree_trans *, enum btree_id, unsigned,
		       struct bkey_s_c, struct bkey_s,
		       enum btree_iter_update_trigger_flags);

int bch2_check_discard_freespace_key(struct btree_trans *, struct btree_iter *, u8 *, bool);
int bch2_check_alloc_info(struct bch_fs *);
int bch2_check_alloc_to_lru_refs(struct bch_fs *);
void bch2_dev_do_discards(struct bch_dev *);
void bch2_do_discards(struct bch_fs *);

static inline u64 should_invalidate_buckets(struct bch_dev *ca,
					    struct bch_dev_usage u)
{
	u64 want_free = ca->mi.nbuckets >> 7;
	u64 free = max_t(s64, 0,
			   u.d[BCH_DATA_free].buckets
			 + u.d[BCH_DATA_need_discard].buckets
			 - bch2_dev_buckets_reserved(ca, BCH_WATERMARK_stripe));

	return clamp_t(s64, want_free - free, 0, u.d[BCH_DATA_cached].buckets);
}

void bch2_dev_do_invalidates(struct bch_dev *);
void bch2_do_invalidates(struct bch_fs *);

static inline struct bch_backpointer *alloc_v4_backpointers(struct bch_alloc_v4 *a)
{
	return (void *) ((u64 *) &a->v +
			 (BCH_ALLOC_V4_BACKPOINTERS_START(a) ?:
			  BCH_ALLOC_V4_U64s_V0));
}

static inline const struct bch_backpointer *alloc_v4_backpointers_c(const struct bch_alloc_v4 *a)
{
	return (void *) ((u64 *) &a->v + BCH_ALLOC_V4_BACKPOINTERS_START(a));
}

int bch2_dev_freespace_init(struct bch_fs *, struct bch_dev *, u64, u64);
int bch2_fs_freespace_init(struct bch_fs *);
int bch2_dev_remove_alloc(struct bch_fs *, struct bch_dev *);

void bch2_recalc_capacity(struct bch_fs *);
u64 bch2_min_rw_member_capacity(struct bch_fs *);

void bch2_dev_allocator_remove(struct bch_fs *, struct bch_dev *);
void bch2_dev_allocator_add(struct bch_fs *, struct bch_dev *);

void bch2_dev_allocator_background_exit(struct bch_dev *);
void bch2_dev_allocator_background_init(struct bch_dev *);

void bch2_fs_allocator_background_init(struct bch_fs *);

#endif /* _BCACHEFS_ALLOC_BACKGROUND_H */
