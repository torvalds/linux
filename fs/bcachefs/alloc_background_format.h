/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_ALLOC_BACKGROUND_FORMAT_H
#define _BCACHEFS_ALLOC_BACKGROUND_FORMAT_H

struct bch_alloc {
	struct bch_val		v;
	__u8			fields;
	__u8			gen;
	__u8			data[];
} __packed __aligned(8);

#define BCH_ALLOC_FIELDS_V1()			\
	x(read_time,		16)		\
	x(write_time,		16)		\
	x(data_type,		8)		\
	x(dirty_sectors,	16)		\
	x(cached_sectors,	16)		\
	x(oldest_gen,		8)		\
	x(stripe,		32)		\
	x(stripe_redundancy,	8)

enum {
#define x(name, _bits) BCH_ALLOC_FIELD_V1_##name,
	BCH_ALLOC_FIELDS_V1()
#undef x
};

struct bch_alloc_v2 {
	struct bch_val		v;
	__u8			nr_fields;
	__u8			gen;
	__u8			oldest_gen;
	__u8			data_type;
	__u8			data[];
} __packed __aligned(8);

#define BCH_ALLOC_FIELDS_V2()			\
	x(read_time,		64)		\
	x(write_time,		64)		\
	x(dirty_sectors,	32)		\
	x(cached_sectors,	32)		\
	x(stripe,		32)		\
	x(stripe_redundancy,	8)

struct bch_alloc_v3 {
	struct bch_val		v;
	__le64			journal_seq;
	__le32			flags;
	__u8			nr_fields;
	__u8			gen;
	__u8			oldest_gen;
	__u8			data_type;
	__u8			data[];
} __packed __aligned(8);

LE32_BITMASK(BCH_ALLOC_V3_NEED_DISCARD,struct bch_alloc_v3, flags,  0,  1)
LE32_BITMASK(BCH_ALLOC_V3_NEED_INC_GEN,struct bch_alloc_v3, flags,  1,  2)

struct bch_alloc_v4 {
	struct bch_val		v;
	__u64			journal_seq;
	__u32			flags;
	__u8			gen;
	__u8			oldest_gen;
	__u8			data_type;
	__u8			stripe_redundancy;
	__u32			dirty_sectors;
	__u32			cached_sectors;
	__u64			io_time[2];
	__u32			stripe;
	__u32			nr_external_backpointers;
	__u64			fragmentation_lru;
	__u32			stripe_sectors;
	__u32			pad;
} __packed __aligned(8);

#define BCH_ALLOC_V4_U64s_V0	6
#define BCH_ALLOC_V4_U64s	(sizeof(struct bch_alloc_v4) / sizeof(__u64))

BITMASK(BCH_ALLOC_V4_NEED_DISCARD,	struct bch_alloc_v4, flags,  0,  1)
BITMASK(BCH_ALLOC_V4_NEED_INC_GEN,	struct bch_alloc_v4, flags,  1,  2)
BITMASK(BCH_ALLOC_V4_BACKPOINTERS_START,struct bch_alloc_v4, flags,  2,  8)
BITMASK(BCH_ALLOC_V4_NR_BACKPOINTERS,	struct bch_alloc_v4, flags,  8,  14)

#define KEY_TYPE_BUCKET_GENS_BITS	8
#define KEY_TYPE_BUCKET_GENS_NR		(1U << KEY_TYPE_BUCKET_GENS_BITS)
#define KEY_TYPE_BUCKET_GENS_MASK	(KEY_TYPE_BUCKET_GENS_NR - 1)

struct bch_bucket_gens {
	struct bch_val		v;
	u8			gens[KEY_TYPE_BUCKET_GENS_NR];
} __packed __aligned(8);

#endif /* _BCACHEFS_ALLOC_BACKGROUND_FORMAT_H */
