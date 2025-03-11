/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_EXTENTS_FORMAT_H
#define _BCACHEFS_EXTENTS_FORMAT_H

/*
 * In extent bkeys, the value is a list of pointers (bch_extent_ptr), optionally
 * preceded by checksum/compression information (bch_extent_crc32 or
 * bch_extent_crc64).
 *
 * One major determining factor in the format of extents is how we handle and
 * represent extents that have been partially overwritten and thus trimmed:
 *
 * If an extent is not checksummed or compressed, when the extent is trimmed we
 * don't have to remember the extent we originally allocated and wrote: we can
 * merely adjust ptr->offset to point to the start of the data that is currently
 * live. The size field in struct bkey records the current (live) size of the
 * extent, and is also used to mean "size of region on disk that we point to" in
 * this case.
 *
 * Thus an extent that is not checksummed or compressed will consist only of a
 * list of bch_extent_ptrs, with none of the fields in
 * bch_extent_crc32/bch_extent_crc64.
 *
 * When an extent is checksummed or compressed, it's not possible to read only
 * the data that is currently live: we have to read the entire extent that was
 * originally written, and then return only the part of the extent that is
 * currently live.
 *
 * Thus, in addition to the current size of the extent in struct bkey, we need
 * to store the size of the originally allocated space - this is the
 * compressed_size and uncompressed_size fields in bch_extent_crc32/64. Also,
 * when the extent is trimmed, instead of modifying the offset field of the
 * pointer, we keep a second smaller offset field - "offset into the original
 * extent of the currently live region".
 *
 * The other major determining factor is replication and data migration:
 *
 * Each pointer may have its own bch_extent_crc32/64. When doing a replicated
 * write, we will initially write all the replicas in the same format, with the
 * same checksum type and compression format - however, when copygc runs later (or
 * tiering/cache promotion, anything that moves data), it is not in general
 * going to rewrite all the pointers at once - one of the replicas may be in a
 * bucket on one device that has very little fragmentation while another lives
 * in a bucket that has become heavily fragmented, and thus is being rewritten
 * sooner than the rest.
 *
 * Thus it will only move a subset of the pointers (or in the case of
 * tiering/cache promotion perhaps add a single pointer without dropping any
 * current pointers), and if the extent has been partially overwritten it must
 * write only the currently live portion (or copygc would not be able to reduce
 * fragmentation!) - which necessitates a different bch_extent_crc format for
 * the new pointer.
 *
 * But in the interests of space efficiency, we don't want to store one
 * bch_extent_crc for each pointer if we don't have to.
 *
 * Thus, a bch_extent consists of bch_extent_crc32s, bch_extent_crc64s, and
 * bch_extent_ptrs appended arbitrarily one after the other. We determine the
 * type of a given entry with a scheme similar to utf8 (except we're encoding a
 * type, not a size), encoding the type in the position of the first set bit:
 *
 * bch_extent_crc32	- 0b1
 * bch_extent_ptr	- 0b10
 * bch_extent_crc64	- 0b100
 *
 * We do it this way because bch_extent_crc32 is _very_ constrained on bits (and
 * bch_extent_crc64 is the least constrained).
 *
 * Then, each bch_extent_crc32/64 applies to the pointers that follow after it,
 * until the next bch_extent_crc32/64.
 *
 * If there are no bch_extent_crcs preceding a bch_extent_ptr, then that pointer
 * is neither checksummed nor compressed.
 */

#define BCH_EXTENT_ENTRY_TYPES()		\
	x(ptr,			0)		\
	x(crc32,		1)		\
	x(crc64,		2)		\
	x(crc128,		3)		\
	x(stripe_ptr,		4)		\
	x(rebalance,		5)
#define BCH_EXTENT_ENTRY_MAX	6

enum bch_extent_entry_type {
#define x(f, n) BCH_EXTENT_ENTRY_##f = n,
	BCH_EXTENT_ENTRY_TYPES()
#undef x
};

/* Compressed/uncompressed size are stored biased by 1: */
struct bch_extent_crc32 {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u32			type:2,
				_compressed_size:7,
				_uncompressed_size:7,
				offset:7,
				_unused:1,
				csum_type:4,
				compression_type:4;
	__u32			csum;
#elif defined (__BIG_ENDIAN_BITFIELD)
	__u32			csum;
	__u32			compression_type:4,
				csum_type:4,
				_unused:1,
				offset:7,
				_uncompressed_size:7,
				_compressed_size:7,
				type:2;
#endif
} __packed __aligned(8);

#define CRC32_SIZE_MAX		(1U << 7)
#define CRC32_NONCE_MAX		0

struct bch_extent_crc64 {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u64			type:3,
				_compressed_size:9,
				_uncompressed_size:9,
				offset:9,
				nonce:10,
				csum_type:4,
				compression_type:4,
				csum_hi:16;
#elif defined (__BIG_ENDIAN_BITFIELD)
	__u64			csum_hi:16,
				compression_type:4,
				csum_type:4,
				nonce:10,
				offset:9,
				_uncompressed_size:9,
				_compressed_size:9,
				type:3;
#endif
	__u64			csum_lo;
} __packed __aligned(8);

#define CRC64_SIZE_MAX		(1U << 9)
#define CRC64_NONCE_MAX		((1U << 10) - 1)

struct bch_extent_crc128 {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u64			type:4,
				_compressed_size:13,
				_uncompressed_size:13,
				offset:13,
				nonce:13,
				csum_type:4,
				compression_type:4;
#elif defined (__BIG_ENDIAN_BITFIELD)
	__u64			compression_type:4,
				csum_type:4,
				nonce:13,
				offset:13,
				_uncompressed_size:13,
				_compressed_size:13,
				type:4;
#endif
	struct bch_csum		csum;
} __packed __aligned(8);

#define CRC128_SIZE_MAX		(1U << 13)
#define CRC128_NONCE_MAX	((1U << 13) - 1)

/*
 * @reservation - pointer hasn't been written to, just reserved
 */
struct bch_extent_ptr {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u64			type:1,
				cached:1,
				unused:1,
				unwritten:1,
				offset:44, /* 8 petabytes */
				dev:8,
				gen:8;
#elif defined (__BIG_ENDIAN_BITFIELD)
	__u64			gen:8,
				dev:8,
				offset:44,
				unwritten:1,
				unused:1,
				cached:1,
				type:1;
#endif
} __packed __aligned(8);

struct bch_extent_stripe_ptr {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u64			type:5,
				block:8,
				redundancy:4,
				idx:47;
#elif defined (__BIG_ENDIAN_BITFIELD)
	__u64			idx:47,
				redundancy:4,
				block:8,
				type:5;
#endif
};

/* bch_extent_rebalance: */
#include "rebalance_format.h"

union bch_extent_entry {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ ||  __BITS_PER_LONG == 64
	unsigned long			type;
#elif __BITS_PER_LONG == 32
	struct {
		unsigned long		pad;
		unsigned long		type;
	};
#else
#error edit for your odd byteorder.
#endif

#define x(f, n) struct bch_extent_##f	f;
	BCH_EXTENT_ENTRY_TYPES()
#undef x
};

struct bch_btree_ptr {
	struct bch_val		v;

	__u64			_data[0];
	struct bch_extent_ptr	start[];
} __packed __aligned(8);

struct bch_btree_ptr_v2 {
	struct bch_val		v;

	__u64			mem_ptr;
	__le64			seq;
	__le16			sectors_written;
	__le16			flags;
	struct bpos		min_key;
	__u64			_data[0];
	struct bch_extent_ptr	start[];
} __packed __aligned(8);

LE16_BITMASK(BTREE_PTR_RANGE_UPDATED,	struct bch_btree_ptr_v2, flags, 0, 1);

struct bch_extent {
	struct bch_val		v;

	__u64			_data[0];
	union bch_extent_entry	start[];
} __packed __aligned(8);

/* Maximum size (in u64s) a single pointer could be: */
#define BKEY_EXTENT_PTR_U64s_MAX\
	((sizeof(struct bch_extent_crc128) +			\
	  sizeof(struct bch_extent_ptr)) / sizeof(__u64))

/* Maximum possible size of an entire extent value: */
#define BKEY_EXTENT_VAL_U64s_MAX				\
	(1 + BKEY_EXTENT_PTR_U64s_MAX * (BCH_REPLICAS_MAX + 1))

/* * Maximum possible size of an entire extent, key + value: */
#define BKEY_EXTENT_U64s_MAX		(BKEY_U64s + BKEY_EXTENT_VAL_U64s_MAX)

/* Btree pointers don't carry around checksums: */
#define BKEY_BTREE_PTR_VAL_U64s_MAX				\
	((sizeof(struct bch_btree_ptr_v2) +			\
	  sizeof(struct bch_extent_ptr) * BCH_REPLICAS_MAX) / sizeof(__u64))
#define BKEY_BTREE_PTR_U64s_MAX					\
	(BKEY_U64s + BKEY_BTREE_PTR_VAL_U64s_MAX)

struct bch_reservation {
	struct bch_val		v;

	__le32			generation;
	__u8			nr_replicas;
	__u8			pad[3];
} __packed __aligned(8);

struct bch_inline_data {
	struct bch_val		v;
	u8			data[];
};

#endif /* _BCACHEFS_EXTENTS_FORMAT_H */
