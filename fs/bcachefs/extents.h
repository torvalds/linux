/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_EXTENTS_H
#define _BCACHEFS_EXTENTS_H

#include "bcachefs.h"
#include "bkey.h"
#include "extents_types.h"

struct bch_fs;
struct btree_trans;
struct btree_insert_entry;

/* extent entries: */

#define extent_entry_last(_e)		bkey_val_end(_e)

#define entry_to_ptr(_entry)						\
({									\
	EBUG_ON((_entry) && !extent_entry_is_ptr(_entry));		\
									\
	__builtin_choose_expr(						\
		type_is_exact(_entry, const union bch_extent_entry *),	\
		(const struct bch_extent_ptr *) (_entry),		\
		(struct bch_extent_ptr *) (_entry));			\
})

/* downcast, preserves const */
#define to_entry(_entry)						\
({									\
	BUILD_BUG_ON(!type_is(_entry, union bch_extent_crc *) &&	\
		     !type_is(_entry, struct bch_extent_ptr *) &&	\
		     !type_is(_entry, struct bch_extent_stripe_ptr *));	\
									\
	__builtin_choose_expr(						\
		(type_is_exact(_entry, const union bch_extent_crc *) ||	\
		 type_is_exact(_entry, const struct bch_extent_ptr *) ||\
		 type_is_exact(_entry, const struct bch_extent_stripe_ptr *)),\
		(const union bch_extent_entry *) (_entry),		\
		(union bch_extent_entry *) (_entry));			\
})

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
#define x(f, n)						\
	case BCH_EXTENT_ENTRY_##f:			\
		return sizeof(struct bch_extent_##f);
	BCH_EXTENT_ENTRY_TYPES()
#undef x
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

/* bkey_ptrs: generically over any key type that has ptrs */

struct bkey_ptrs_c {
	const union bch_extent_entry	*start;
	const union bch_extent_entry	*end;
};

struct bkey_ptrs {
	union bch_extent_entry	*start;
	union bch_extent_entry	*end;
};

/* iterate over bkey ptrs */

#define extent_entry_next(_entry)					\
	((typeof(_entry)) ((void *) (_entry) + extent_entry_bytes(_entry)))

#define __bkey_extent_entry_for_each_from(_start, _end, _entry)		\
	for ((_entry) = (_start);					\
	     (_entry) < (_end);						\
	     (_entry) = extent_entry_next(_entry))

#define __bkey_ptr_next(_ptr, _end)					\
({									\
	typeof(_end) _entry;						\
									\
	__bkey_extent_entry_for_each_from(to_entry(_ptr), _end, _entry)	\
		if (extent_entry_is_ptr(_entry))			\
			break;						\
									\
	_entry < (_end) ? entry_to_ptr(_entry) : NULL;			\
})

#define bkey_extent_entry_for_each_from(_p, _entry, _start)		\
	__bkey_extent_entry_for_each_from(_start, (_p).end, _entry)

#define bkey_extent_entry_for_each(_p, _entry)				\
	bkey_extent_entry_for_each_from(_p, _entry, _p.start)

#define __bkey_for_each_ptr(_start, _end, _ptr)				\
	for ((_ptr) = (_start);						\
	     ((_ptr) = __bkey_ptr_next(_ptr, _end));			\
	     (_ptr)++)

#define bkey_ptr_next(_p, _ptr)						\
	__bkey_ptr_next(_ptr, (_p).end)

#define bkey_for_each_ptr(_p, _ptr)					\
	__bkey_for_each_ptr(&(_p).start->ptr, (_p).end, _ptr)

#define __bkey_ptr_next_decode(_k, _end, _ptr, _entry)			\
({									\
	__label__ out;							\
									\
	(_ptr).idx	= 0;						\
	(_ptr).ec_nr	= 0;						\
									\
	__bkey_extent_entry_for_each_from(_entry, _end, _entry)		\
		switch (extent_entry_type(_entry)) {			\
		case BCH_EXTENT_ENTRY_ptr:				\
			(_ptr).ptr		= _entry->ptr;		\
			goto out;					\
		case BCH_EXTENT_ENTRY_crc32:				\
		case BCH_EXTENT_ENTRY_crc64:				\
		case BCH_EXTENT_ENTRY_crc128:				\
			(_ptr).crc = bch2_extent_crc_unpack(_k,		\
					entry_to_crc(_entry));		\
			break;						\
		case BCH_EXTENT_ENTRY_stripe_ptr:			\
			(_ptr).ec[(_ptr).ec_nr++] = _entry->stripe_ptr;	\
			break;						\
		}							\
out:									\
	_entry < (_end);						\
})

#define __bkey_for_each_ptr_decode(_k, _start, _end, _ptr, _entry)	\
	for ((_ptr).crc = bch2_extent_crc_unpack(_k, NULL),		\
	     (_entry) = _start;						\
	     __bkey_ptr_next_decode(_k, _end, _ptr, _entry);		\
	     (_entry) = extent_entry_next(_entry))

#define bkey_for_each_ptr_decode(_k, _p, _ptr, _entry)			\
	__bkey_for_each_ptr_decode(_k, (_p).start, (_p).end,		\
				   _ptr, _entry)

/* utility code common to all keys with pointers: */

static inline struct bkey_ptrs_c bch2_bkey_ptrs_c(struct bkey_s_c k)
{
	switch (k.k->type) {
	case KEY_TYPE_btree_ptr: {
		struct bkey_s_c_btree_ptr e = bkey_s_c_to_btree_ptr(k);
		return (struct bkey_ptrs_c) {
			to_entry(&e.v->start[0]),
			to_entry(bkey_val_end(e))
		};
	}
	case KEY_TYPE_extent: {
		struct bkey_s_c_extent e = bkey_s_c_to_extent(k);
		return (struct bkey_ptrs_c) {
			e.v->start,
			extent_entry_last(e)
		};
	}
	case KEY_TYPE_stripe: {
		struct bkey_s_c_stripe s = bkey_s_c_to_stripe(k);
		return (struct bkey_ptrs_c) {
			to_entry(&s.v->ptrs[0]),
			to_entry(&s.v->ptrs[s.v->nr_blocks]),
		};
	}
	default:
		return (struct bkey_ptrs_c) { NULL, NULL };
	}
}

static inline struct bkey_ptrs bch2_bkey_ptrs(struct bkey_s k)
{
	struct bkey_ptrs_c p = bch2_bkey_ptrs_c(k.s_c);

	return (struct bkey_ptrs) {
		(void *) p.start,
		(void *) p.end
	};
}

static inline struct bch_devs_list bch2_bkey_devs(struct bkey_s_c k)
{
	struct bch_devs_list ret = (struct bch_devs_list) { 0 };
	struct bkey_ptrs_c p = bch2_bkey_ptrs_c(k);
	const struct bch_extent_ptr *ptr;

	bkey_for_each_ptr(p, ptr)
		ret.devs[ret.nr++] = ptr->dev;

	return ret;
}

static inline struct bch_devs_list bch2_bkey_dirty_devs(struct bkey_s_c k)
{
	struct bch_devs_list ret = (struct bch_devs_list) { 0 };
	struct bkey_ptrs_c p = bch2_bkey_ptrs_c(k);
	const struct bch_extent_ptr *ptr;

	bkey_for_each_ptr(p, ptr)
		if (!ptr->cached)
			ret.devs[ret.nr++] = ptr->dev;

	return ret;
}

static inline struct bch_devs_list bch2_bkey_cached_devs(struct bkey_s_c k)
{
	struct bch_devs_list ret = (struct bch_devs_list) { 0 };
	struct bkey_ptrs_c p = bch2_bkey_ptrs_c(k);
	const struct bch_extent_ptr *ptr;

	bkey_for_each_ptr(p, ptr)
		if (ptr->cached)
			ret.devs[ret.nr++] = ptr->dev;

	return ret;
}

static inline bool bch2_bkey_has_device(struct bkey_s_c k, unsigned dev)
{
	struct bkey_ptrs_c p = bch2_bkey_ptrs_c(k);
	const struct bch_extent_ptr *ptr;

	bkey_for_each_ptr(p, ptr)
		if (ptr->dev == dev)
			return ptr;

	return NULL;
}

unsigned bch2_bkey_nr_ptrs(struct bkey_s_c);
unsigned bch2_bkey_nr_dirty_ptrs(struct bkey_s_c);
unsigned bch2_bkey_durability(struct bch_fs *, struct bkey_s_c);

void bch2_mark_io_failure(struct bch_io_failures *,
			  struct extent_ptr_decoded *);
int bch2_bkey_pick_read_device(struct bch_fs *, struct bkey_s_c,
			       struct bch_io_failures *,
			       struct extent_ptr_decoded *);

/* bch_btree_ptr: */

const char *bch2_btree_ptr_invalid(const struct bch_fs *, struct bkey_s_c);
void bch2_btree_ptr_debugcheck(struct bch_fs *, struct btree *,
			       struct bkey_s_c);
void bch2_btree_ptr_to_text(struct printbuf *, struct bch_fs *,
			    struct bkey_s_c);
void bch2_ptr_swab(const struct bkey_format *, struct bkey_packed *);

#define bch2_bkey_ops_btree_ptr (struct bkey_ops) {		\
	.key_invalid	= bch2_btree_ptr_invalid,		\
	.key_debugcheck	= bch2_btree_ptr_debugcheck,		\
	.val_to_text	= bch2_btree_ptr_to_text,		\
	.swab		= bch2_ptr_swab,			\
}

/* bch_extent: */

const char *bch2_extent_invalid(const struct bch_fs *, struct bkey_s_c);
void bch2_extent_debugcheck(struct bch_fs *, struct btree *, struct bkey_s_c);
void bch2_extent_to_text(struct printbuf *, struct bch_fs *, struct bkey_s_c);
bool bch2_extent_normalize(struct bch_fs *, struct bkey_s);
enum merge_result bch2_extent_merge(struct bch_fs *,
				    struct bkey_i *, struct bkey_i *);

#define bch2_bkey_ops_extent (struct bkey_ops) {		\
	.key_invalid	= bch2_extent_invalid,			\
	.key_debugcheck	= bch2_extent_debugcheck,		\
	.val_to_text	= bch2_extent_to_text,			\
	.swab		= bch2_ptr_swab,			\
	.key_normalize	= bch2_extent_normalize,		\
	.key_merge	= bch2_extent_merge,			\
}

/* bch_reservation: */

const char *bch2_reservation_invalid(const struct bch_fs *, struct bkey_s_c);
void bch2_reservation_to_text(struct printbuf *, struct bch_fs *, struct bkey_s_c);
enum merge_result bch2_reservation_merge(struct bch_fs *,
					 struct bkey_i *, struct bkey_i *);

#define bch2_bkey_ops_reservation (struct bkey_ops) {		\
	.key_invalid	= bch2_reservation_invalid,		\
	.val_to_text	= bch2_reservation_to_text,		\
	.key_merge	= bch2_reservation_merge,		\
}

void bch2_extent_trim_atomic(struct bkey_i *, struct btree_iter *);
bool bch2_extent_is_atomic(struct bkey_i *, struct btree_iter *);

enum btree_insert_ret
bch2_extent_can_insert(struct btree_trans *, struct btree_insert_entry *,
		       unsigned *);
void bch2_insert_fixup_extent(struct btree_trans *,
			      struct btree_insert_entry *);

void bch2_extent_mark_replicas_cached(struct bch_fs *, struct bkey_s_extent,
				      unsigned, unsigned);

const struct bch_extent_ptr *
bch2_extent_has_device(struct bkey_s_c_extent, unsigned);
const struct bch_extent_ptr *
bch2_extent_has_group(struct bch_fs *, struct bkey_s_c_extent, unsigned);
const struct bch_extent_ptr *
bch2_extent_has_target(struct bch_fs *, struct bkey_s_c_extent, unsigned);

unsigned bch2_extent_is_compressed(struct bkey_s_c);

bool bch2_extent_matches_ptr(struct bch_fs *, struct bkey_s_c_extent,
			     struct bch_extent_ptr, u64);

static inline bool bkey_extent_is_data(const struct bkey *k)
{
	switch (k->type) {
	case KEY_TYPE_btree_ptr:
	case KEY_TYPE_extent:
		return true;
	default:
		return false;
	}
}

static inline bool bkey_extent_is_allocation(const struct bkey *k)
{
	switch (k->type) {
	case KEY_TYPE_extent:
	case KEY_TYPE_reservation:
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

void bch2_bkey_append_ptr(struct bkey_i *, struct bch_extent_ptr);
void bch2_bkey_drop_device(struct bkey_s, unsigned);

/* Extent entry iteration: */

#define extent_for_each_entry_from(_e, _entry, _start)			\
	__bkey_extent_entry_for_each_from(_start,			\
				extent_entry_last(_e),_entry)

#define extent_for_each_entry(_e, _entry)				\
	extent_for_each_entry_from(_e, _entry, (_e).v->start)

#define extent_ptr_next(_e, _ptr)					\
	__bkey_ptr_next(_ptr, extent_entry_last(_e))

#define extent_for_each_ptr(_e, _ptr)					\
	__bkey_for_each_ptr(&(_e).v->start->ptr, extent_entry_last(_e), _ptr)

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

#define extent_for_each_ptr_decode(_e, _ptr, _entry)			\
	__bkey_for_each_ptr_decode((_e).k, (_e).v->start,		\
				   extent_entry_last(_e), _ptr, _entry)

void bch2_extent_crc_append(struct bkey_i_extent *,
			    struct bch_extent_crc_unpacked);
void bch2_extent_ptr_decoded_append(struct bkey_i_extent *,
				    struct extent_ptr_decoded *);

static inline void __extent_entry_push(struct bkey_i_extent *e)
{
	union bch_extent_entry *entry = extent_entry_last(extent_i_to_s(e));

	EBUG_ON(bkey_val_u64s(&e->k) + extent_entry_u64s(entry) >
		BKEY_EXTENT_VAL_U64s_MAX);

	e->k.u64s += extent_entry_u64s(entry);
}

bool bch2_can_narrow_extent_crcs(struct bkey_s_c_extent,
				 struct bch_extent_crc_unpacked);
bool bch2_extent_narrow_crcs(struct bkey_i_extent *, struct bch_extent_crc_unpacked);

union bch_extent_entry *bch2_bkey_drop_ptr(struct bkey_s,
					   struct bch_extent_ptr *);

#define bch2_bkey_drop_ptrs(_k, _ptr, _cond)				\
do {									\
	struct bkey_ptrs _ptrs = bch2_bkey_ptrs(_k);			\
									\
	_ptr = &_ptrs.start->ptr;					\
									\
	while ((_ptr = bkey_ptr_next(_ptrs, _ptr))) {			\
		if (_cond) {						\
			_ptr = (void *) bch2_bkey_drop_ptr(_k, _ptr);	\
			_ptrs = bch2_bkey_ptrs(_k);			\
			continue;					\
		}							\
									\
		(_ptr)++;						\
	}								\
} while (0)

bool __bch2_cut_front(struct bpos, struct bkey_s);

static inline bool bch2_cut_front(struct bpos where, struct bkey_i *k)
{
	return __bch2_cut_front(where, bkey_i_to_s(k));
}

bool bch2_cut_back(struct bpos, struct bkey *);
void bch2_key_resize(struct bkey *, unsigned);

/*
 * In extent_sort_fix_overlapping(), insert_fixup_extent(),
 * extent_merge_inline() - we're modifying keys in place that are packed. To do
 * that we have to unpack the key, modify the unpacked key - then this
 * copies/repacks the unpacked to the original as necessary.
 */
static inline void extent_save(struct btree *b, struct bkey_packed *dst,
			       struct bkey *src)
{
	struct bkey_format *f = &b->format;
	struct bkey_i *dst_unpacked;

	if ((dst_unpacked = packed_to_bkey(dst)))
		dst_unpacked->k = *src;
	else
		BUG_ON(!bch2_bkey_pack_key(dst, src, f));
}

bool bch2_check_range_allocated(struct bch_fs *, struct bpos, u64, unsigned);
unsigned bch2_bkey_nr_ptrs_allocated(struct bkey_s_c);

#endif /* _BCACHEFS_EXTENTS_H */
