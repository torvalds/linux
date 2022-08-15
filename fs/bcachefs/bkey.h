/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BKEY_H
#define _BCACHEFS_BKEY_H

#include <linux/bug.h>
#include "bcachefs_format.h"

#include "util.h"
#include "vstructs.h"

#if 0

/*
 * compiled unpack functions are disabled, pending a new interface for
 * dynamically allocating executable memory:
 */

#ifdef CONFIG_X86_64
#define HAVE_BCACHEFS_COMPILED_UNPACK	1
#endif
#endif

void bch2_to_binary(char *, const u64 *, unsigned);

/* bkey with split value, const */
struct bkey_s_c {
	const struct bkey	*k;
	const struct bch_val	*v;
};

/* bkey with split value */
struct bkey_s {
	union {
	struct {
		struct bkey	*k;
		struct bch_val	*v;
	};
	struct bkey_s_c		s_c;
	};
};

#define bkey_next(_k)		vstruct_next(_k)

#define bkey_val_u64s(_k)	((_k)->u64s - BKEY_U64s)

static inline size_t bkey_val_bytes(const struct bkey *k)
{
	return bkey_val_u64s(k) * sizeof(u64);
}

static inline void set_bkey_val_u64s(struct bkey *k, unsigned val_u64s)
{
	unsigned u64s = BKEY_U64s + val_u64s;

	BUG_ON(u64s > U8_MAX);
	k->u64s = u64s;
}

static inline void set_bkey_val_bytes(struct bkey *k, unsigned bytes)
{
	set_bkey_val_u64s(k, DIV_ROUND_UP(bytes, sizeof(u64)));
}

#define bkey_val_end(_k)	((void *) (((u64 *) (_k).v) + bkey_val_u64s((_k).k)))

#define bkey_deleted(_k)	((_k)->type == KEY_TYPE_deleted)

#define bkey_whiteout(_k)				\
	((_k)->type == KEY_TYPE_deleted || (_k)->type == KEY_TYPE_whiteout)

enum bkey_lr_packed {
	BKEY_PACKED_BOTH,
	BKEY_PACKED_RIGHT,
	BKEY_PACKED_LEFT,
	BKEY_PACKED_NONE,
};

#define bkey_lr_packed(_l, _r)						\
	((_l)->format + ((_r)->format << 1))

#define bkey_copy(_dst, _src)					\
do {								\
	BUILD_BUG_ON(!type_is(_dst, struct bkey_i *) &&		\
		     !type_is(_dst, struct bkey_packed *));	\
	BUILD_BUG_ON(!type_is(_src, struct bkey_i *) &&		\
		     !type_is(_src, struct bkey_packed *));	\
	EBUG_ON((u64 *) (_dst) > (u64 *) (_src) &&		\
		(u64 *) (_dst) < (u64 *) (_src) +		\
		((struct bkey *) (_src))->u64s);		\
								\
	memcpy_u64s_small((_dst), (_src),			\
			  ((struct bkey *) (_src))->u64s);	\
} while (0)

struct btree;

struct bkey_format_state {
	u64 field_min[BKEY_NR_FIELDS];
	u64 field_max[BKEY_NR_FIELDS];
};

void bch2_bkey_format_init(struct bkey_format_state *);
void bch2_bkey_format_add_key(struct bkey_format_state *, const struct bkey *);
void bch2_bkey_format_add_pos(struct bkey_format_state *, struct bpos);
struct bkey_format bch2_bkey_format_done(struct bkey_format_state *);
const char *bch2_bkey_format_validate(struct bkey_format *);

__pure
unsigned bch2_bkey_greatest_differing_bit(const struct btree *,
					  const struct bkey_packed *,
					  const struct bkey_packed *);
__pure
unsigned bch2_bkey_ffs(const struct btree *, const struct bkey_packed *);

__pure
int __bch2_bkey_cmp_packed_format_checked(const struct bkey_packed *,
				     const struct bkey_packed *,
				     const struct btree *);

__pure
int __bch2_bkey_cmp_left_packed_format_checked(const struct btree *,
					  const struct bkey_packed *,
					  const struct bpos *);

__pure
int bch2_bkey_cmp_packed(const struct btree *,
			 const struct bkey_packed *,
			 const struct bkey_packed *);

__pure
int __bch2_bkey_cmp_left_packed(const struct btree *,
				const struct bkey_packed *,
				const struct bpos *);

static inline __pure
int bkey_cmp_left_packed(const struct btree *b,
			 const struct bkey_packed *l, const struct bpos *r)
{
	return __bch2_bkey_cmp_left_packed(b, l, r);
}

/*
 * we prefer to pass bpos by ref, but it's often enough terribly convenient to
 * pass it by by val... as much as I hate c++, const ref would be nice here:
 */
__pure __flatten
static inline int bkey_cmp_left_packed_byval(const struct btree *b,
					     const struct bkey_packed *l,
					     struct bpos r)
{
	return bkey_cmp_left_packed(b, l, &r);
}

static __always_inline int bpos_cmp(struct bpos l, struct bpos r)
{
	return  cmp_int(l.inode,    r.inode) ?:
		cmp_int(l.offset,   r.offset) ?:
		cmp_int(l.snapshot, r.snapshot);
}

static __always_inline int bkey_cmp(struct bpos l, struct bpos r)
{
	return  cmp_int(l.inode,    r.inode) ?:
		cmp_int(l.offset,   r.offset);
}

static inline struct bpos bpos_min(struct bpos l, struct bpos r)
{
	return bpos_cmp(l, r) < 0 ? l : r;
}

static inline struct bpos bpos_max(struct bpos l, struct bpos r)
{
	return bpos_cmp(l, r) > 0 ? l : r;
}

void bch2_bpos_swab(struct bpos *);
void bch2_bkey_swab_key(const struct bkey_format *, struct bkey_packed *);

static __always_inline int bversion_cmp(struct bversion l, struct bversion r)
{
	return  cmp_int(l.hi, r.hi) ?:
		cmp_int(l.lo, r.lo);
}

#define ZERO_VERSION	((struct bversion) { .hi = 0, .lo = 0 })
#define MAX_VERSION	((struct bversion) { .hi = ~0, .lo = ~0ULL })

static __always_inline int bversion_zero(struct bversion v)
{
	return !bversion_cmp(v, ZERO_VERSION);
}

#ifdef CONFIG_BCACHEFS_DEBUG
/* statement expressions confusing unlikely()? */
#define bkey_packed(_k)							\
	({ EBUG_ON((_k)->format > KEY_FORMAT_CURRENT);			\
	 (_k)->format != KEY_FORMAT_CURRENT; })
#else
#define bkey_packed(_k)		((_k)->format != KEY_FORMAT_CURRENT)
#endif

/*
 * It's safe to treat an unpacked bkey as a packed one, but not the reverse
 */
static inline struct bkey_packed *bkey_to_packed(struct bkey_i *k)
{
	return (struct bkey_packed *) k;
}

static inline const struct bkey_packed *bkey_to_packed_c(const struct bkey_i *k)
{
	return (const struct bkey_packed *) k;
}

static inline struct bkey_i *packed_to_bkey(struct bkey_packed *k)
{
	return bkey_packed(k) ? NULL : (struct bkey_i *) k;
}

static inline const struct bkey *packed_to_bkey_c(const struct bkey_packed *k)
{
	return bkey_packed(k) ? NULL : (const struct bkey *) k;
}

static inline unsigned bkey_format_key_bits(const struct bkey_format *format)
{
	return format->bits_per_field[BKEY_FIELD_INODE] +
		format->bits_per_field[BKEY_FIELD_OFFSET] +
		format->bits_per_field[BKEY_FIELD_SNAPSHOT];
}

static inline struct bpos bpos_successor(struct bpos p)
{
	if (!++p.snapshot &&
	    !++p.offset &&
	    !++p.inode)
		BUG();

	return p;
}

static inline struct bpos bpos_predecessor(struct bpos p)
{
	if (!p.snapshot-- &&
	    !p.offset-- &&
	    !p.inode--)
		BUG();

	return p;
}

static inline struct bpos bpos_nosnap_successor(struct bpos p)
{
	p.snapshot = 0;

	if (!++p.offset &&
	    !++p.inode)
		BUG();

	return p;
}

static inline struct bpos bpos_nosnap_predecessor(struct bpos p)
{
	p.snapshot = 0;

	if (!p.offset-- &&
	    !p.inode--)
		BUG();

	return p;
}

static inline u64 bkey_start_offset(const struct bkey *k)
{
	return k->p.offset - k->size;
}

static inline struct bpos bkey_start_pos(const struct bkey *k)
{
	return (struct bpos) {
		.inode		= k->p.inode,
		.offset		= bkey_start_offset(k),
		.snapshot	= k->p.snapshot,
	};
}

/* Packed helpers */

static inline unsigned bkeyp_key_u64s(const struct bkey_format *format,
				      const struct bkey_packed *k)
{
	unsigned ret = bkey_packed(k) ? format->key_u64s : BKEY_U64s;

	EBUG_ON(k->u64s < ret);
	return ret;
}

static inline unsigned bkeyp_key_bytes(const struct bkey_format *format,
				       const struct bkey_packed *k)
{
	return bkeyp_key_u64s(format, k) * sizeof(u64);
}

static inline unsigned bkeyp_val_u64s(const struct bkey_format *format,
				      const struct bkey_packed *k)
{
	return k->u64s - bkeyp_key_u64s(format, k);
}

static inline size_t bkeyp_val_bytes(const struct bkey_format *format,
				     const struct bkey_packed *k)
{
	return bkeyp_val_u64s(format, k) * sizeof(u64);
}

static inline void set_bkeyp_val_u64s(const struct bkey_format *format,
				      struct bkey_packed *k, unsigned val_u64s)
{
	k->u64s = bkeyp_key_u64s(format, k) + val_u64s;
}

#define bkeyp_val(_format, _k)						\
	 ((struct bch_val *) ((_k)->_data + bkeyp_key_u64s(_format, _k)))

extern const struct bkey_format bch2_bkey_format_current;

bool bch2_bkey_transform(const struct bkey_format *,
			 struct bkey_packed *,
			 const struct bkey_format *,
			 const struct bkey_packed *);

struct bkey __bch2_bkey_unpack_key(const struct bkey_format *,
				   const struct bkey_packed *);

#ifndef HAVE_BCACHEFS_COMPILED_UNPACK
struct bpos __bkey_unpack_pos(const struct bkey_format *,
			      const struct bkey_packed *);
#endif

bool bch2_bkey_pack_key(struct bkey_packed *, const struct bkey *,
		   const struct bkey_format *);

enum bkey_pack_pos_ret {
	BKEY_PACK_POS_EXACT,
	BKEY_PACK_POS_SMALLER,
	BKEY_PACK_POS_FAIL,
};

enum bkey_pack_pos_ret bch2_bkey_pack_pos_lossy(struct bkey_packed *, struct bpos,
					   const struct btree *);

static inline bool bkey_pack_pos(struct bkey_packed *out, struct bpos in,
				 const struct btree *b)
{
	return bch2_bkey_pack_pos_lossy(out, in, b) == BKEY_PACK_POS_EXACT;
}

void bch2_bkey_unpack(const struct btree *, struct bkey_i *,
		 const struct bkey_packed *);
bool bch2_bkey_pack(struct bkey_packed *, const struct bkey_i *,
	       const struct bkey_format *);

static inline u64 bkey_field_max(const struct bkey_format *f,
				 enum bch_bkey_fields nr)
{
	return f->bits_per_field[nr] < 64
		? (le64_to_cpu(f->field_offset[nr]) +
		   ~(~0ULL << f->bits_per_field[nr]))
		: U64_MAX;
}

#ifdef HAVE_BCACHEFS_COMPILED_UNPACK

int bch2_compile_bkey_format(const struct bkey_format *, void *);

#else

static inline int bch2_compile_bkey_format(const struct bkey_format *format,
					  void *out) { return 0; }

#endif

static inline void bkey_reassemble(struct bkey_i *dst,
				   struct bkey_s_c src)
{
	dst->k = *src.k;
	memcpy_u64s_small(&dst->v, src.v, bkey_val_u64s(src.k));
}

#define bkey_s_null		((struct bkey_s)   { .k = NULL })
#define bkey_s_c_null		((struct bkey_s_c) { .k = NULL })

#define bkey_s_err(err)		((struct bkey_s)   { .k = ERR_PTR(err) })
#define bkey_s_c_err(err)	((struct bkey_s_c) { .k = ERR_PTR(err) })

static inline struct bkey_s bkey_to_s(struct bkey *k)
{
	return (struct bkey_s) { .k = k, .v = NULL };
}

static inline struct bkey_s_c bkey_to_s_c(const struct bkey *k)
{
	return (struct bkey_s_c) { .k = k, .v = NULL };
}

static inline struct bkey_s bkey_i_to_s(struct bkey_i *k)
{
	return (struct bkey_s) { .k = &k->k, .v = &k->v };
}

static inline struct bkey_s_c bkey_i_to_s_c(const struct bkey_i *k)
{
	return (struct bkey_s_c) { .k = &k->k, .v = &k->v };
}

/*
 * For a given type of value (e.g. struct bch_extent), generates the types for
 * bkey + bch_extent - inline, split, split const - and also all the conversion
 * functions, which also check that the value is of the correct type.
 *
 * We use anonymous unions for upcasting - e.g. converting from e.g. a
 * bkey_i_extent to a bkey_i - since that's always safe, instead of conversion
 * functions.
 */
#define x(name, ...)					\
struct bkey_i_##name {							\
	union {								\
		struct bkey		k;				\
		struct bkey_i		k_i;				\
	};								\
	struct bch_##name		v;				\
};									\
									\
struct bkey_s_c_##name {						\
	union {								\
	struct {							\
		const struct bkey	*k;				\
		const struct bch_##name	*v;				\
	};								\
	struct bkey_s_c			s_c;				\
	};								\
};									\
									\
struct bkey_s_##name {							\
	union {								\
	struct {							\
		struct bkey		*k;				\
		struct bch_##name	*v;				\
	};								\
	struct bkey_s_c_##name		c;				\
	struct bkey_s			s;				\
	struct bkey_s_c			s_c;				\
	};								\
};									\
									\
static inline struct bkey_i_##name *bkey_i_to_##name(struct bkey_i *k)	\
{									\
	EBUG_ON(k->k.type != KEY_TYPE_##name);				\
	return container_of(&k->k, struct bkey_i_##name, k);		\
}									\
									\
static inline const struct bkey_i_##name *				\
bkey_i_to_##name##_c(const struct bkey_i *k)				\
{									\
	EBUG_ON(k->k.type != KEY_TYPE_##name);				\
	return container_of(&k->k, struct bkey_i_##name, k);		\
}									\
									\
static inline struct bkey_s_##name bkey_s_to_##name(struct bkey_s k)	\
{									\
	EBUG_ON(k.k->type != KEY_TYPE_##name);				\
	return (struct bkey_s_##name) {					\
		.k = k.k,						\
		.v = container_of(k.v, struct bch_##name, v),		\
	};								\
}									\
									\
static inline struct bkey_s_c_##name bkey_s_c_to_##name(struct bkey_s_c k)\
{									\
	EBUG_ON(k.k->type != KEY_TYPE_##name);				\
	return (struct bkey_s_c_##name) {				\
		.k = k.k,						\
		.v = container_of(k.v, struct bch_##name, v),		\
	};								\
}									\
									\
static inline struct bkey_s_##name name##_i_to_s(struct bkey_i_##name *k)\
{									\
	return (struct bkey_s_##name) {					\
		.k = &k->k,						\
		.v = &k->v,						\
	};								\
}									\
									\
static inline struct bkey_s_c_##name					\
name##_i_to_s_c(const struct bkey_i_##name *k)				\
{									\
	return (struct bkey_s_c_##name) {				\
		.k = &k->k,						\
		.v = &k->v,						\
	};								\
}									\
									\
static inline struct bkey_s_##name bkey_i_to_s_##name(struct bkey_i *k)	\
{									\
	EBUG_ON(k->k.type != KEY_TYPE_##name);				\
	return (struct bkey_s_##name) {					\
		.k = &k->k,						\
		.v = container_of(&k->v, struct bch_##name, v),		\
	};								\
}									\
									\
static inline struct bkey_s_c_##name					\
bkey_i_to_s_c_##name(const struct bkey_i *k)				\
{									\
	EBUG_ON(k->k.type != KEY_TYPE_##name);				\
	return (struct bkey_s_c_##name) {				\
		.k = &k->k,						\
		.v = container_of(&k->v, struct bch_##name, v),		\
	};								\
}									\
									\
static inline struct bkey_i_##name *bkey_##name##_init(struct bkey_i *_k)\
{									\
	struct bkey_i_##name *k =					\
		container_of(&_k->k, struct bkey_i_##name, k);		\
									\
	bkey_init(&k->k);						\
	memset(&k->v, 0, sizeof(k->v));					\
	k->k.type = KEY_TYPE_##name;					\
	set_bkey_val_bytes(&k->k, sizeof(k->v));			\
									\
	return k;							\
}

BCH_BKEY_TYPES();
#undef x

/* byte order helpers */

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__

static inline unsigned high_word_offset(const struct bkey_format *f)
{
	return f->key_u64s - 1;
}

#define high_bit_offset		0
#define nth_word(p, n)		((p) - (n))

#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__

static inline unsigned high_word_offset(const struct bkey_format *f)
{
	return 0;
}

#define high_bit_offset		KEY_PACKED_BITS_START
#define nth_word(p, n)		((p) + (n))

#else
#error edit for your odd byteorder.
#endif

#define high_word(f, k)		((k)->_data + high_word_offset(f))
#define next_word(p)		nth_word(p, 1)
#define prev_word(p)		nth_word(p, -1)

#ifdef CONFIG_BCACHEFS_DEBUG
void bch2_bkey_pack_test(void);
#else
static inline void bch2_bkey_pack_test(void) {}
#endif

#endif /* _BCACHEFS_BKEY_H */
