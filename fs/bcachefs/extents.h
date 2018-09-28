/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_EXTENTS_H
#define _BCACHEFS_EXTENTS_H

#include "bcachefs.h"
#include "bkey.h"
#include "extents_types.h"

struct bch_fs;
struct journal_res;
struct btree_node_iter;
struct btree_node_iter_large;
struct btree_insert;
struct btree_insert_entry;
struct bch_devs_mask;
union bch_extent_crc;

const char *bch2_btree_ptr_invalid(const struct bch_fs *, struct bkey_s_c);
void bch2_btree_ptr_debugcheck(struct bch_fs *, struct btree *,
			       struct bkey_s_c);
int bch2_btree_ptr_to_text(struct bch_fs *, char *, size_t, struct bkey_s_c);
void bch2_ptr_swab(const struct bkey_format *, struct bkey_packed *);

#define bch2_bkey_btree_ops (struct bkey_ops) {			\
	.key_invalid	= bch2_btree_ptr_invalid,		\
	.key_debugcheck	= bch2_btree_ptr_debugcheck,		\
	.val_to_text	= bch2_btree_ptr_to_text,		\
	.swab		= bch2_ptr_swab,			\
}

const char *bch2_extent_invalid(const struct bch_fs *, struct bkey_s_c);
void bch2_extent_debugcheck(struct bch_fs *, struct btree *, struct bkey_s_c);
int bch2_extent_to_text(struct bch_fs *, char *, size_t, struct bkey_s_c);
bool bch2_ptr_normalize(struct bch_fs *, struct btree *, struct bkey_s);
enum merge_result bch2_extent_merge(struct bch_fs *, struct btree *,
				    struct bkey_i *, struct bkey_i *);

#define bch2_bkey_extent_ops (struct bkey_ops) {		\
	.key_invalid	= bch2_extent_invalid,			\
	.key_debugcheck	= bch2_extent_debugcheck,		\
	.val_to_text	= bch2_extent_to_text,			\
	.swab		= bch2_ptr_swab,			\
	.key_normalize	= bch2_ptr_normalize,			\
	.key_merge	= bch2_extent_merge,			\
	.is_extents	= true,					\
}

struct btree_nr_keys bch2_key_sort_fix_overlapping(struct bset *,
						  struct btree *,
						  struct btree_node_iter_large *);
struct btree_nr_keys bch2_extent_sort_fix_overlapping(struct bch_fs *c,
						     struct bset *,
						     struct btree *,
						     struct btree_node_iter_large *);

int bch2_btree_pick_ptr(struct bch_fs *, const struct btree *,
			struct bch_devs_mask *avoid,
			struct extent_ptr_decoded *);

int bch2_extent_pick_ptr(struct bch_fs *, struct bkey_s_c,
			 struct bch_devs_mask *,
			 struct extent_ptr_decoded *);

void bch2_extent_trim_atomic(struct bkey_i *, struct btree_iter *);

static inline bool bch2_extent_is_atomic(struct bkey *k,
					 struct btree_iter *iter)
{
	struct btree *b = iter->l[0].b;

	return bkey_cmp(k->p, b->key.k.p) <= 0 &&
		bkey_cmp(bkey_start_pos(k), b->data->min_key) >= 0;
}

enum btree_insert_ret
bch2_extent_can_insert(struct btree_insert *, struct btree_insert_entry *,
		       unsigned *);
enum btree_insert_ret
bch2_insert_fixup_extent(struct btree_insert *, struct btree_insert_entry *);

bool bch2_extent_normalize(struct bch_fs *, struct bkey_s);
void bch2_extent_mark_replicas_cached(struct bch_fs *, struct bkey_s_extent,
				      unsigned, unsigned);

const struct bch_extent_ptr *
bch2_extent_has_device(struct bkey_s_c_extent, unsigned);
bool bch2_extent_drop_device(struct bkey_s_extent, unsigned);
const struct bch_extent_ptr *
bch2_extent_has_group(struct bch_fs *, struct bkey_s_c_extent, unsigned);
const struct bch_extent_ptr *
bch2_extent_has_target(struct bch_fs *, struct bkey_s_c_extent, unsigned);

unsigned bch2_extent_nr_ptrs(struct bkey_s_c_extent);
unsigned bch2_extent_nr_dirty_ptrs(struct bkey_s_c);
unsigned bch2_extent_is_compressed(struct bkey_s_c);

unsigned bch2_extent_ptr_durability(struct bch_fs *,
				    const struct bch_extent_ptr *);
unsigned bch2_extent_durability(struct bch_fs *, struct bkey_s_c_extent);

bool bch2_extent_matches_ptr(struct bch_fs *, struct bkey_s_c_extent,
			     struct bch_extent_ptr, u64);

static inline bool bkey_extent_is_data(const struct bkey *k)
{
	switch (k->type) {
	case BCH_EXTENT:
	case BCH_EXTENT_CACHED:
		return true;
	default:
		return false;
	}
}

static inline bool bkey_extent_is_allocation(const struct bkey *k)
{
	switch (k->type) {
	case BCH_EXTENT:
	case BCH_EXTENT_CACHED:
	case BCH_RESERVATION:
		return true;
	default:
		return false;
	}
}

static inline bool bch2_extent_is_fully_allocated(struct bkey_s_c k)
{
	return bkey_extent_is_allocation(k.k) &&
		!bch2_extent_is_compressed(k);
}

static inline bool bkey_extent_is_cached(const struct bkey *k)
{
	return k->type == BCH_EXTENT_CACHED;
}

static inline void bkey_extent_set_cached(struct bkey *k, bool cached)
{
	EBUG_ON(k->type != BCH_EXTENT &&
		k->type != BCH_EXTENT_CACHED);

	k->type = cached ? BCH_EXTENT_CACHED : BCH_EXTENT;
}

static inline unsigned
__extent_entry_type(const union bch_extent_entry *e)
{
	return e->type ? __ffs(e->type) : BCH_EXTENT_ENTRY_MAX;
}

static inline enum bch_extent_entry_type
extent_entry_type(const union bch_extent_entry *e)
{
	int ret = __ffs(e->type);

	EBUG_ON(ret < 0 || ret >= BCH_EXTENT_ENTRY_MAX);

	return ret;
}

static inline size_t extent_entry_bytes(const union bch_extent_entry *entry)
{
	switch (extent_entry_type(entry)) {
	case BCH_EXTENT_ENTRY_crc32:
		return sizeof(struct bch_extent_crc32);
	case BCH_EXTENT_ENTRY_crc64:
		return sizeof(struct bch_extent_crc64);
	case BCH_EXTENT_ENTRY_crc128:
		return sizeof(struct bch_extent_crc128);
	case BCH_EXTENT_ENTRY_ptr:
		return sizeof(struct bch_extent_ptr);
	default:
		BUG();
	}
}

static inline size_t extent_entry_u64s(const union bch_extent_entry *entry)
{
	return extent_entry_bytes(entry) / sizeof(u64);
}

static inline bool extent_entry_is_ptr(const union bch_extent_entry *e)
{
	switch (extent_entry_type(e)) {
	case BCH_EXTENT_ENTRY_ptr:
		return true;
	default:
		return false;
	}
}

static inline bool extent_entry_is_crc(const union bch_extent_entry *e)
{
	switch (extent_entry_type(e)) {
	case BCH_EXTENT_ENTRY_crc32:
	case BCH_EXTENT_ENTRY_crc64:
	case BCH_EXTENT_ENTRY_crc128:
		return true;
	default:
		return false;
	}
}

union bch_extent_crc {
	u8				type;
	struct bch_extent_crc32		crc32;
	struct bch_extent_crc64		crc64;
	struct bch_extent_crc128	crc128;
};

/* downcast, preserves const */
#define to_entry(_entry)						\
({									\
	BUILD_BUG_ON(!type_is(_entry, union bch_extent_crc *) &&	\
		     !type_is(_entry, struct bch_extent_ptr *));	\
									\
	__builtin_choose_expr(						\
		(type_is_exact(_entry, const union bch_extent_crc *) ||	\
		 type_is_exact(_entry, const struct bch_extent_ptr *)),	\
		(const union bch_extent_entry *) (_entry),		\
		(union bch_extent_entry *) (_entry));			\
})

#define __entry_to_crc(_entry)						\
	__builtin_choose_expr(						\
		type_is_exact(_entry, const union bch_extent_entry *),	\
		(const union bch_extent_crc *) (_entry),		\
		(union bch_extent_crc *) (_entry))

#define entry_to_crc(_entry)						\
({									\
	EBUG_ON((_entry) && !extent_entry_is_crc(_entry));		\
									\
	__entry_to_crc(_entry);						\
})

#define entry_to_ptr(_entry)						\
({									\
	EBUG_ON((_entry) && !extent_entry_is_ptr(_entry));		\
									\
	__builtin_choose_expr(						\
		type_is_exact(_entry, const union bch_extent_entry *),	\
		(const struct bch_extent_ptr *) (_entry),		\
		(struct bch_extent_ptr *) (_entry));			\
})

/* checksum entries: */

static inline struct bch_extent_crc_unpacked
bch2_extent_crc_unpack(const struct bkey *k, const union bch_extent_crc *crc)
{
#define common_fields(_crc)						\
		.csum_type		= _crc.csum_type,		\
		.compression_type	= _crc.compression_type,	\
		.compressed_size	= _crc._compressed_size + 1,	\
		.uncompressed_size	= _crc._uncompressed_size + 1,	\
		.offset			= _crc.offset,			\
		.live_size		= k->size

	if (!crc)
		return (struct bch_extent_crc_unpacked) {
			.compressed_size	= k->size,
			.uncompressed_size	= k->size,
			.live_size		= k->size,
		};

	switch (extent_entry_type(to_entry(crc))) {
	case BCH_EXTENT_ENTRY_crc32: {
		struct bch_extent_crc_unpacked ret = (struct bch_extent_crc_unpacked) {
			common_fields(crc->crc32),
		};

		*((__le32 *) &ret.csum.lo) = crc->crc32.csum;

		memcpy(&ret.csum.lo, &crc->crc32.csum,
		       sizeof(crc->crc32.csum));

		return ret;
	}
	case BCH_EXTENT_ENTRY_crc64: {
		struct bch_extent_crc_unpacked ret = (struct bch_extent_crc_unpacked) {
			common_fields(crc->crc64),
			.nonce			= crc->crc64.nonce,
			.csum.lo		= (__force __le64) crc->crc64.csum_lo,
		};

		*((__le16 *) &ret.csum.hi) = crc->crc64.csum_hi;

		return ret;
	}
	case BCH_EXTENT_ENTRY_crc128: {
		struct bch_extent_crc_unpacked ret = (struct bch_extent_crc_unpacked) {
			common_fields(crc->crc128),
			.nonce			= crc->crc128.nonce,
			.csum			= crc->crc128.csum,
		};

		return ret;
	}
	default:
		BUG();
	}
#undef common_fields
}

/* Extent entry iteration: */

#define extent_entry_next(_entry)					\
	((typeof(_entry)) ((void *) (_entry) + extent_entry_bytes(_entry)))

#define extent_entry_last(_e)						\
	vstruct_idx((_e).v, bkey_val_u64s((_e).k))

/* Iterate over all entries: */

#define extent_for_each_entry_from(_e, _entry, _start)			\
	for ((_entry) = _start;						\
	     (_entry) < extent_entry_last(_e);				\
	     (_entry) = extent_entry_next(_entry))

#define extent_for_each_entry(_e, _entry)				\
	extent_for_each_entry_from(_e, _entry, (_e).v->start)

/* Iterate over pointers only: */

#define extent_ptr_next(_e, _ptr)					\
({									\
	typeof(&(_e).v->start[0]) _entry;				\
									\
	extent_for_each_entry_from(_e, _entry, to_entry(_ptr))		\
		if (extent_entry_is_ptr(_entry))			\
			break;						\
									\
	_entry < extent_entry_last(_e) ? entry_to_ptr(_entry) : NULL;	\
})

#define extent_for_each_ptr(_e, _ptr)					\
	for ((_ptr) = &(_e).v->start->ptr;				\
	     ((_ptr) = extent_ptr_next(_e, _ptr));			\
	     (_ptr)++)

/* Iterate over crcs only: */

#define extent_crc_next(_e, _crc, _iter)				\
({									\
	extent_for_each_entry_from(_e, _iter, _iter)			\
		if (extent_entry_is_crc(_iter)) {			\
			(_crc) = bch2_extent_crc_unpack((_e).k, entry_to_crc(_iter));\
			break;						\
		}							\
									\
	(_iter) < extent_entry_last(_e);				\
})

#define extent_for_each_crc(_e, _crc, _iter)				\
	for ((_crc) = bch2_extent_crc_unpack((_e).k, NULL),		\
	     (_iter) = (_e).v->start;					\
	     extent_crc_next(_e, _crc, _iter);				\
	     (_iter) = extent_entry_next(_iter))

/* Iterate over pointers, with crcs: */

static inline struct extent_ptr_decoded
__extent_ptr_decoded_init(const struct bkey *k)
{
	return (struct extent_ptr_decoded) {
		.crc		= bch2_extent_crc_unpack(k, NULL),
	};
}

#define EXTENT_ITERATE_EC		(1 << 0)

#define __extent_ptr_next_decode(_e, _ptr, _entry)			\
({									\
	__label__ out;							\
									\
	extent_for_each_entry_from(_e, _entry, _entry)			\
		switch (extent_entry_type(_entry)) {			\
		case BCH_EXTENT_ENTRY_ptr:				\
			(_ptr).ptr		= _entry->ptr;		\
			goto out;					\
		case BCH_EXTENT_ENTRY_crc32:				\
		case BCH_EXTENT_ENTRY_crc64:				\
		case BCH_EXTENT_ENTRY_crc128:				\
			(_ptr).crc = bch2_extent_crc_unpack((_e).k,	\
					entry_to_crc(_entry));		\
			break;						\
		}							\
									\
out:									\
	_entry < extent_entry_last(_e);					\
})

#define extent_for_each_ptr_decode(_e, _ptr, _entry)			\
	for ((_ptr) = __extent_ptr_decoded_init((_e).k),		\
	     (_entry) = (_e).v->start;					\
	     __extent_ptr_next_decode(_e, _ptr, _entry);		\
	     (_entry) = extent_entry_next(_entry))

/* Iterate over pointers backwards: */

#define extent_ptr_prev(_e, _ptr)					\
({									\
	typeof(&(_e).v->start->ptr) _p;					\
	typeof(&(_e).v->start->ptr) _prev = NULL;			\
									\
	extent_for_each_ptr(_e, _p) {					\
		if (_p == (_ptr))					\
			break;						\
		_prev = _p;						\
	}								\
									\
	_prev;								\
})

/*
 * Use this when you'll be dropping pointers as you iterate. Quadratic,
 * unfortunately:
 */
#define extent_for_each_ptr_backwards(_e, _ptr)				\
	for ((_ptr) = extent_ptr_prev(_e, NULL);			\
	     (_ptr);							\
	     (_ptr) = extent_ptr_prev(_e, _ptr))

void bch2_extent_crc_append(struct bkey_i_extent *,
			    struct bch_extent_crc_unpacked);

static inline void __extent_entry_push(struct bkey_i_extent *e)
{
	union bch_extent_entry *entry = extent_entry_last(extent_i_to_s(e));

	EBUG_ON(bkey_val_u64s(&e->k) + extent_entry_u64s(entry) >
		BKEY_EXTENT_VAL_U64s_MAX);

	e->k.u64s += extent_entry_u64s(entry);
}

static inline void extent_ptr_append(struct bkey_i_extent *e,
				     struct bch_extent_ptr ptr)
{
	ptr.type = 1 << BCH_EXTENT_ENTRY_ptr;
	extent_entry_last(extent_i_to_s(e))->ptr = ptr;
	__extent_entry_push(e);
}

static inline struct bch_devs_list bch2_extent_devs(struct bkey_s_c_extent e)
{
	struct bch_devs_list ret = (struct bch_devs_list) { 0 };
	const struct bch_extent_ptr *ptr;

	extent_for_each_ptr(e, ptr)
		ret.devs[ret.nr++] = ptr->dev;

	return ret;
}

static inline struct bch_devs_list bch2_extent_dirty_devs(struct bkey_s_c_extent e)
{
	struct bch_devs_list ret = (struct bch_devs_list) { 0 };
	const struct bch_extent_ptr *ptr;

	extent_for_each_ptr(e, ptr)
		if (!ptr->cached)
			ret.devs[ret.nr++] = ptr->dev;

	return ret;
}

static inline struct bch_devs_list bch2_extent_cached_devs(struct bkey_s_c_extent e)
{
	struct bch_devs_list ret = (struct bch_devs_list) { 0 };
	const struct bch_extent_ptr *ptr;

	extent_for_each_ptr(e, ptr)
		if (ptr->cached)
			ret.devs[ret.nr++] = ptr->dev;

	return ret;
}

static inline struct bch_devs_list bch2_bkey_devs(struct bkey_s_c k)
{
	switch (k.k->type) {
	case BCH_EXTENT:
	case BCH_EXTENT_CACHED:
		return bch2_extent_devs(bkey_s_c_to_extent(k));
	default:
		return (struct bch_devs_list) { .nr = 0 };
	}
}

static inline struct bch_devs_list bch2_bkey_dirty_devs(struct bkey_s_c k)
{
	switch (k.k->type) {
	case BCH_EXTENT:
	case BCH_EXTENT_CACHED:
		return bch2_extent_dirty_devs(bkey_s_c_to_extent(k));
	default:
		return (struct bch_devs_list) { .nr = 0 };
	}
}

static inline struct bch_devs_list bch2_bkey_cached_devs(struct bkey_s_c k)
{
	switch (k.k->type) {
	case BCH_EXTENT:
	case BCH_EXTENT_CACHED:
		return bch2_extent_cached_devs(bkey_s_c_to_extent(k));
	default:
		return (struct bch_devs_list) { .nr = 0 };
	}
}

bool bch2_can_narrow_extent_crcs(struct bkey_s_c_extent,
				 struct bch_extent_crc_unpacked);
bool bch2_extent_narrow_crcs(struct bkey_i_extent *, struct bch_extent_crc_unpacked);
void bch2_extent_drop_redundant_crcs(struct bkey_s_extent);

void __bch2_extent_drop_ptr(struct bkey_s_extent, struct bch_extent_ptr *);
void bch2_extent_drop_ptr(struct bkey_s_extent, struct bch_extent_ptr *);

bool bch2_cut_front(struct bpos, struct bkey_i *);
bool bch2_cut_back(struct bpos, struct bkey *);
void bch2_key_resize(struct bkey *, unsigned);

int bch2_check_range_allocated(struct bch_fs *, struct bpos, u64);

#endif /* _BCACHEFS_EXTENTS_H */
