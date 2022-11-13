/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_FORMAT_H
#define _BCACHEFS_FORMAT_H

/*
 * bcachefs on disk data structures
 *
 * OVERVIEW:
 *
 * There are three main types of on disk data structures in bcachefs (this is
 * reduced from 5 in bcache)
 *
 *  - superblock
 *  - journal
 *  - btree
 *
 * The btree is the primary structure; most metadata exists as keys in the
 * various btrees. There are only a small number of btrees, they're not
 * sharded - we have one btree for extents, another for inodes, et cetera.
 *
 * SUPERBLOCK:
 *
 * The superblock contains the location of the journal, the list of devices in
 * the filesystem, and in general any metadata we need in order to decide
 * whether we can start a filesystem or prior to reading the journal/btree
 * roots.
 *
 * The superblock is extensible, and most of the contents of the superblock are
 * in variable length, type tagged fields; see struct bch_sb_field.
 *
 * Backup superblocks do not reside in a fixed location; also, superblocks do
 * not have a fixed size. To locate backup superblocks we have struct
 * bch_sb_layout; we store a copy of this inside every superblock, and also
 * before the first superblock.
 *
 * JOURNAL:
 *
 * The journal primarily records btree updates in the order they occurred;
 * journal replay consists of just iterating over all the keys in the open
 * journal entries and re-inserting them into the btrees.
 *
 * The journal also contains entry types for the btree roots, and blacklisted
 * journal sequence numbers (see journal_seq_blacklist.c).
 *
 * BTREE:
 *
 * bcachefs btrees are copy on write b+ trees, where nodes are big (typically
 * 128k-256k) and log structured. We use struct btree_node for writing the first
 * entry in a given node (offset 0), and struct btree_node_entry for all
 * subsequent writes.
 *
 * After the header, btree node entries contain a list of keys in sorted order.
 * Values are stored inline with the keys; since values are variable length (and
 * keys effectively are variable length too, due to packing) we can't do random
 * access without building up additional in memory tables in the btree node read
 * path.
 *
 * BTREE KEYS (struct bkey):
 *
 * The various btrees share a common format for the key - so as to avoid
 * switching in fastpath lookup/comparison code - but define their own
 * structures for the key values.
 *
 * The size of a key/value pair is stored as a u8 in units of u64s, so the max
 * size is just under 2k. The common part also contains a type tag for the
 * value, and a format field indicating whether the key is packed or not (and
 * also meant to allow adding new key fields in the future, if desired).
 *
 * bkeys, when stored within a btree node, may also be packed. In that case, the
 * bkey_format in that node is used to unpack it. Packed bkeys mean that we can
 * be generous with field sizes in the common part of the key format (64 bit
 * inode number, 64 bit offset, 96 bit version field, etc.) for negligible cost.
 */

#include <asm/types.h>
#include <asm/byteorder.h>
#include <linux/kernel.h>
#include <linux/uuid.h>
#include "vstructs.h"

#ifdef __KERNEL__
typedef uuid_t __uuid_t;
#endif

#define BITMASK(name, type, field, offset, end)				\
static const unsigned	name##_OFFSET = offset;				\
static const unsigned	name##_BITS = (end - offset);			\
									\
static inline __u64 name(const type *k)					\
{									\
	return (k->field >> offset) & ~(~0ULL << (end - offset));	\
}									\
									\
static inline void SET_##name(type *k, __u64 v)				\
{									\
	k->field &= ~(~(~0ULL << (end - offset)) << offset);		\
	k->field |= (v & ~(~0ULL << (end - offset))) << offset;		\
}

#define LE_BITMASK(_bits, name, type, field, offset, end)		\
static const unsigned	name##_OFFSET = offset;				\
static const unsigned	name##_BITS = (end - offset);			\
static const __u##_bits	name##_MAX = (1ULL << (end - offset)) - 1;	\
									\
static inline __u64 name(const type *k)					\
{									\
	return (__le##_bits##_to_cpu(k->field) >> offset) &		\
		~(~0ULL << (end - offset));				\
}									\
									\
static inline void SET_##name(type *k, __u64 v)				\
{									\
	__u##_bits new = __le##_bits##_to_cpu(k->field);		\
									\
	new &= ~(~(~0ULL << (end - offset)) << offset);			\
	new |= (v & ~(~0ULL << (end - offset))) << offset;		\
	k->field = __cpu_to_le##_bits(new);				\
}

#define LE16_BITMASK(n, t, f, o, e)	LE_BITMASK(16, n, t, f, o, e)
#define LE32_BITMASK(n, t, f, o, e)	LE_BITMASK(32, n, t, f, o, e)
#define LE64_BITMASK(n, t, f, o, e)	LE_BITMASK(64, n, t, f, o, e)

struct bkey_format {
	__u8		key_u64s;
	__u8		nr_fields;
	/* One unused slot for now: */
	__u8		bits_per_field[6];
	__le64		field_offset[6];
};

/* Btree keys - all units are in sectors */

struct bpos {
	/*
	 * Word order matches machine byte order - btree code treats a bpos as a
	 * single large integer, for search/comparison purposes
	 *
	 * Note that wherever a bpos is embedded in another on disk data
	 * structure, it has to be byte swabbed when reading in metadata that
	 * wasn't written in native endian order:
	 */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	__u32		snapshot;
	__u64		offset;
	__u64		inode;
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	__u64		inode;
	__u64		offset;		/* Points to end of extent - sectors */
	__u32		snapshot;
#else
#error edit for your odd byteorder.
#endif
} __packed __aligned(4);

#define KEY_INODE_MAX			((__u64)~0ULL)
#define KEY_OFFSET_MAX			((__u64)~0ULL)
#define KEY_SNAPSHOT_MAX		((__u32)~0U)
#define KEY_SIZE_MAX			((__u32)~0U)

static inline struct bpos SPOS(__u64 inode, __u64 offset, __u32 snapshot)
{
	return (struct bpos) {
		.inode		= inode,
		.offset		= offset,
		.snapshot	= snapshot,
	};
}

#define POS_MIN				SPOS(0, 0, 0)
#define POS_MAX				SPOS(KEY_INODE_MAX, KEY_OFFSET_MAX, 0)
#define SPOS_MAX			SPOS(KEY_INODE_MAX, KEY_OFFSET_MAX, KEY_SNAPSHOT_MAX)
#define POS(_inode, _offset)		SPOS(_inode, _offset, 0)

/* Empty placeholder struct, for container_of() */
struct bch_val {
	__u64		__nothing[0];
};

struct bversion {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	__u64		lo;
	__u32		hi;
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	__u32		hi;
	__u64		lo;
#endif
} __packed __aligned(4);

struct bkey {
	/* Size of combined key and value, in u64s */
	__u8		u64s;

	/* Format of key (0 for format local to btree node) */
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8		format:7,
			needs_whiteout:1;
#elif defined (__BIG_ENDIAN_BITFIELD)
	__u8		needs_whiteout:1,
			format:7;
#else
#error edit for your odd byteorder.
#endif

	/* Type of the value */
	__u8		type;

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	__u8		pad[1];

	struct bversion	version;
	__u32		size;		/* extent size, in sectors */
	struct bpos	p;
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	struct bpos	p;
	__u32		size;		/* extent size, in sectors */
	struct bversion	version;

	__u8		pad[1];
#endif
} __packed __aligned(8);

struct bkey_packed {
	__u64		_data[0];

	/* Size of combined key and value, in u64s */
	__u8		u64s;

	/* Format of key (0 for format local to btree node) */

	/*
	 * XXX: next incompat on disk format change, switch format and
	 * needs_whiteout - bkey_packed() will be cheaper if format is the high
	 * bits of the bitfield
	 */
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8		format:7,
			needs_whiteout:1;
#elif defined (__BIG_ENDIAN_BITFIELD)
	__u8		needs_whiteout:1,
			format:7;
#endif

	/* Type of the value */
	__u8		type;
	__u8		key_start[0];

	/*
	 * We copy bkeys with struct assignment in various places, and while
	 * that shouldn't be done with packed bkeys we can't disallow it in C,
	 * and it's legal to cast a bkey to a bkey_packed  - so padding it out
	 * to the same size as struct bkey should hopefully be safest.
	 */
	__u8		pad[sizeof(struct bkey) - 3];
} __packed __aligned(8);

#define BKEY_U64s			(sizeof(struct bkey) / sizeof(__u64))
#define BKEY_U64s_MAX			U8_MAX
#define BKEY_VAL_U64s_MAX		(BKEY_U64s_MAX - BKEY_U64s)

#define KEY_PACKED_BITS_START		24

#define KEY_FORMAT_LOCAL_BTREE		0
#define KEY_FORMAT_CURRENT		1

enum bch_bkey_fields {
	BKEY_FIELD_INODE,
	BKEY_FIELD_OFFSET,
	BKEY_FIELD_SNAPSHOT,
	BKEY_FIELD_SIZE,
	BKEY_FIELD_VERSION_HI,
	BKEY_FIELD_VERSION_LO,
	BKEY_NR_FIELDS,
};

#define bkey_format_field(name, field)					\
	[BKEY_FIELD_##name] = (sizeof(((struct bkey *) NULL)->field) * 8)

#define BKEY_FORMAT_CURRENT						\
((struct bkey_format) {							\
	.key_u64s	= BKEY_U64s,					\
	.nr_fields	= BKEY_NR_FIELDS,				\
	.bits_per_field = {						\
		bkey_format_field(INODE,	p.inode),		\
		bkey_format_field(OFFSET,	p.offset),		\
		bkey_format_field(SNAPSHOT,	p.snapshot),		\
		bkey_format_field(SIZE,		size),			\
		bkey_format_field(VERSION_HI,	version.hi),		\
		bkey_format_field(VERSION_LO,	version.lo),		\
	},								\
})

/* bkey with inline value */
struct bkey_i {
	__u64			_data[0];

	union {
	struct {
		/* Size of combined key and value, in u64s */
		__u8		u64s;
	};
	struct {
		struct bkey	k;
		struct bch_val	v;
	};
	};
};

#define KEY(_inode, _offset, _size)					\
((struct bkey) {							\
	.u64s		= BKEY_U64s,					\
	.format		= KEY_FORMAT_CURRENT,				\
	.p		= POS(_inode, _offset),				\
	.size		= _size,					\
})

static inline void bkey_init(struct bkey *k)
{
	*k = KEY(0, 0, 0);
}

#define bkey_bytes(_k)		((_k)->u64s * sizeof(__u64))

#define __BKEY_PADDED(key, pad)					\
	struct { struct bkey_i key; __u64 key ## _pad[pad]; }

/*
 * - DELETED keys are used internally to mark keys that should be ignored but
 *   override keys in composition order.  Their version number is ignored.
 *
 * - DISCARDED keys indicate that the data is all 0s because it has been
 *   discarded. DISCARDs may have a version; if the version is nonzero the key
 *   will be persistent, otherwise the key will be dropped whenever the btree
 *   node is rewritten (like DELETED keys).
 *
 * - ERROR: any read of the data returns a read error, as the data was lost due
 *   to a failing device. Like DISCARDED keys, they can be removed (overridden)
 *   by new writes or cluster-wide GC. Node repair can also overwrite them with
 *   the same or a more recent version number, but not with an older version
 *   number.
 *
 * - WHITEOUT: for hash table btrees
 */
#define BCH_BKEY_TYPES()				\
	x(deleted,		0)			\
	x(whiteout,		1)			\
	x(error,		2)			\
	x(cookie,		3)			\
	x(hash_whiteout,	4)			\
	x(btree_ptr,		5)			\
	x(extent,		6)			\
	x(reservation,		7)			\
	x(inode,		8)			\
	x(inode_generation,	9)			\
	x(dirent,		10)			\
	x(xattr,		11)			\
	x(alloc,		12)			\
	x(quota,		13)			\
	x(stripe,		14)			\
	x(reflink_p,		15)			\
	x(reflink_v,		16)			\
	x(inline_data,		17)			\
	x(btree_ptr_v2,		18)			\
	x(indirect_inline_data,	19)			\
	x(alloc_v2,		20)			\
	x(subvolume,		21)			\
	x(snapshot,		22)			\
	x(inode_v2,		23)			\
	x(alloc_v3,		24)			\
	x(set,			25)			\
	x(lru,			26)			\
	x(alloc_v4,		27)			\
	x(backpointer,		28)			\
	x(inode_v3,		29)

enum bch_bkey_type {
#define x(name, nr) KEY_TYPE_##name	= nr,
	BCH_BKEY_TYPES()
#undef x
	KEY_TYPE_MAX,
};

struct bch_deleted {
	struct bch_val		v;
};

struct bch_whiteout {
	struct bch_val		v;
};

struct bch_error {
	struct bch_val		v;
};

struct bch_cookie {
	struct bch_val		v;
	__le64			cookie;
};

struct bch_hash_whiteout {
	struct bch_val		v;
};

struct bch_set {
	struct bch_val		v;
};

/* Extents */

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

/* 128 bits, sufficient for cryptographic MACs: */
struct bch_csum {
	__le64			lo;
	__le64			hi;
} __packed __aligned(8);

#define BCH_EXTENT_ENTRY_TYPES()		\
	x(ptr,			0)		\
	x(crc32,		1)		\
	x(crc64,		2)		\
	x(crc128,		3)		\
	x(stripe_ptr,		4)
#define BCH_EXTENT_ENTRY_MAX	5

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

struct bch_extent_reservation {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u64			type:6,
				unused:22,
				replicas:4,
				generation:32;
#elif defined (__BIG_ENDIAN_BITFIELD)
	__u64			generation:32,
				replicas:4,
				unused:22,
				type:6;
#endif
};

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

struct bch_reservation {
	struct bch_val		v;

	__le32			generation;
	__u8			nr_replicas;
	__u8			pad[3];
} __packed __aligned(8);

/* Maximum size (in u64s) a single pointer could be: */
#define BKEY_EXTENT_PTR_U64s_MAX\
	((sizeof(struct bch_extent_crc128) +			\
	  sizeof(struct bch_extent_ptr)) / sizeof(u64))

/* Maximum possible size of an entire extent value: */
#define BKEY_EXTENT_VAL_U64s_MAX				\
	(1 + BKEY_EXTENT_PTR_U64s_MAX * (BCH_REPLICAS_MAX + 1))

/* * Maximum possible size of an entire extent, key + value: */
#define BKEY_EXTENT_U64s_MAX		(BKEY_U64s + BKEY_EXTENT_VAL_U64s_MAX)

/* Btree pointers don't carry around checksums: */
#define BKEY_BTREE_PTR_VAL_U64s_MAX				\
	((sizeof(struct bch_btree_ptr_v2) +			\
	  sizeof(struct bch_extent_ptr) * BCH_REPLICAS_MAX) / sizeof(u64))
#define BKEY_BTREE_PTR_U64s_MAX					\
	(BKEY_U64s + BKEY_BTREE_PTR_VAL_U64s_MAX)

/* Inodes */

#define BLOCKDEV_INODE_MAX	4096

#define BCACHEFS_ROOT_INO	4096

struct bch_inode {
	struct bch_val		v;

	__le64			bi_hash_seed;
	__le32			bi_flags;
	__le16			bi_mode;
	__u8			fields[0];
} __packed __aligned(8);

struct bch_inode_v2 {
	struct bch_val		v;

	__le64			bi_journal_seq;
	__le64			bi_hash_seed;
	__le64			bi_flags;
	__le16			bi_mode;
	__u8			fields[0];
} __packed __aligned(8);

struct bch_inode_v3 {
	struct bch_val		v;

	__le64			bi_journal_seq;
	__le64			bi_hash_seed;
	__le64			bi_flags;
	__le64			bi_sectors;
	__le64			bi_size;
	__le64			bi_version;
	__u8			fields[0];
} __packed __aligned(8);

#define INODEv3_FIELDS_START_INITIAL	6
#define INODEv3_FIELDS_START_CUR	(offsetof(struct bch_inode_v3, fields) / sizeof(u64))

struct bch_inode_generation {
	struct bch_val		v;

	__le32			bi_generation;
	__le32			pad;
} __packed __aligned(8);

/*
 * bi_subvol and bi_parent_subvol are only set for subvolume roots:
 */

#define BCH_INODE_FIELDS_v2()			\
	x(bi_atime,			96)	\
	x(bi_ctime,			96)	\
	x(bi_mtime,			96)	\
	x(bi_otime,			96)	\
	x(bi_size,			64)	\
	x(bi_sectors,			64)	\
	x(bi_uid,			32)	\
	x(bi_gid,			32)	\
	x(bi_nlink,			32)	\
	x(bi_generation,		32)	\
	x(bi_dev,			32)	\
	x(bi_data_checksum,		8)	\
	x(bi_compression,		8)	\
	x(bi_project,			32)	\
	x(bi_background_compression,	8)	\
	x(bi_data_replicas,		8)	\
	x(bi_promote_target,		16)	\
	x(bi_foreground_target,		16)	\
	x(bi_background_target,		16)	\
	x(bi_erasure_code,		16)	\
	x(bi_fields_set,		16)	\
	x(bi_dir,			64)	\
	x(bi_dir_offset,		64)	\
	x(bi_subvol,			32)	\
	x(bi_parent_subvol,		32)

#define BCH_INODE_FIELDS_v3()			\
	x(bi_atime,			96)	\
	x(bi_ctime,			96)	\
	x(bi_mtime,			96)	\
	x(bi_otime,			96)	\
	x(bi_uid,			32)	\
	x(bi_gid,			32)	\
	x(bi_nlink,			32)	\
	x(bi_generation,		32)	\
	x(bi_dev,			32)	\
	x(bi_data_checksum,		8)	\
	x(bi_compression,		8)	\
	x(bi_project,			32)	\
	x(bi_background_compression,	8)	\
	x(bi_data_replicas,		8)	\
	x(bi_promote_target,		16)	\
	x(bi_foreground_target,		16)	\
	x(bi_background_target,		16)	\
	x(bi_erasure_code,		16)	\
	x(bi_fields_set,		16)	\
	x(bi_dir,			64)	\
	x(bi_dir_offset,		64)	\
	x(bi_subvol,			32)	\
	x(bi_parent_subvol,		32)

/* subset of BCH_INODE_FIELDS */
#define BCH_INODE_OPTS()			\
	x(data_checksum,		8)	\
	x(compression,			8)	\
	x(project,			32)	\
	x(background_compression,	8)	\
	x(data_replicas,		8)	\
	x(promote_target,		16)	\
	x(foreground_target,		16)	\
	x(background_target,		16)	\
	x(erasure_code,			16)

enum inode_opt_id {
#define x(name, ...)				\
	Inode_opt_##name,
	BCH_INODE_OPTS()
#undef  x
	Inode_opt_nr,
};

enum {
	/*
	 * User flags (get/settable with FS_IOC_*FLAGS, correspond to FS_*_FL
	 * flags)
	 */
	__BCH_INODE_SYNC		= 0,
	__BCH_INODE_IMMUTABLE		= 1,
	__BCH_INODE_APPEND		= 2,
	__BCH_INODE_NODUMP		= 3,
	__BCH_INODE_NOATIME		= 4,

	__BCH_INODE_I_SIZE_DIRTY	= 5,
	__BCH_INODE_I_SECTORS_DIRTY	= 6,
	__BCH_INODE_UNLINKED		= 7,
	__BCH_INODE_BACKPTR_UNTRUSTED	= 8,

	/* bits 20+ reserved for packed fields below: */
};

#define BCH_INODE_SYNC		(1 << __BCH_INODE_SYNC)
#define BCH_INODE_IMMUTABLE	(1 << __BCH_INODE_IMMUTABLE)
#define BCH_INODE_APPEND	(1 << __BCH_INODE_APPEND)
#define BCH_INODE_NODUMP	(1 << __BCH_INODE_NODUMP)
#define BCH_INODE_NOATIME	(1 << __BCH_INODE_NOATIME)
#define BCH_INODE_I_SIZE_DIRTY	(1 << __BCH_INODE_I_SIZE_DIRTY)
#define BCH_INODE_I_SECTORS_DIRTY (1 << __BCH_INODE_I_SECTORS_DIRTY)
#define BCH_INODE_UNLINKED	(1 << __BCH_INODE_UNLINKED)
#define BCH_INODE_BACKPTR_UNTRUSTED (1 << __BCH_INODE_BACKPTR_UNTRUSTED)

LE32_BITMASK(INODE_STR_HASH,	struct bch_inode, bi_flags, 20, 24);
LE32_BITMASK(INODE_NR_FIELDS,	struct bch_inode, bi_flags, 24, 31);
LE32_BITMASK(INODE_NEW_VARINT,	struct bch_inode, bi_flags, 31, 32);

LE64_BITMASK(INODEv2_STR_HASH,	struct bch_inode_v2, bi_flags, 20, 24);
LE64_BITMASK(INODEv2_NR_FIELDS,	struct bch_inode_v2, bi_flags, 24, 31);

LE64_BITMASK(INODEv3_STR_HASH,	struct bch_inode_v3, bi_flags, 20, 24);
LE64_BITMASK(INODEv3_NR_FIELDS,	struct bch_inode_v3, bi_flags, 24, 31);

LE64_BITMASK(INODEv3_FIELDS_START,
				struct bch_inode_v3, bi_flags, 31, 36);
LE64_BITMASK(INODEv3_MODE,	struct bch_inode_v3, bi_flags, 36, 52);

/* Dirents */

/*
 * Dirents (and xattrs) have to implement string lookups; since our b-tree
 * doesn't support arbitrary length strings for the key, we instead index by a
 * 64 bit hash (currently truncated sha1) of the string, stored in the offset
 * field of the key - using linear probing to resolve hash collisions. This also
 * provides us with the readdir cookie posix requires.
 *
 * Linear probing requires us to use whiteouts for deletions, in the event of a
 * collision:
 */

struct bch_dirent {
	struct bch_val		v;

	/* Target inode number: */
	union {
	__le64			d_inum;
	struct {		/* DT_SUBVOL */
	__le32			d_child_subvol;
	__le32			d_parent_subvol;
	};
	};

	/*
	 * Copy of mode bits 12-15 from the target inode - so userspace can get
	 * the filetype without having to do a stat()
	 */
	__u8			d_type;

	__u8			d_name[];
} __packed __aligned(8);

#define DT_SUBVOL	16
#define BCH_DT_MAX	17

#define BCH_NAME_MAX	((unsigned) (U8_MAX * sizeof(u64) -		\
			 sizeof(struct bkey) -				\
			 offsetof(struct bch_dirent, d_name)))

/* Xattrs */

#define KEY_TYPE_XATTR_INDEX_USER			0
#define KEY_TYPE_XATTR_INDEX_POSIX_ACL_ACCESS	1
#define KEY_TYPE_XATTR_INDEX_POSIX_ACL_DEFAULT	2
#define KEY_TYPE_XATTR_INDEX_TRUSTED			3
#define KEY_TYPE_XATTR_INDEX_SECURITY	        4

struct bch_xattr {
	struct bch_val		v;
	__u8			x_type;
	__u8			x_name_len;
	__le16			x_val_len;
	__u8			x_name[];
} __packed __aligned(8);

/* Bucket/allocation information: */

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
} __packed __aligned(8);

#define BCH_ALLOC_V4_U64s_V0	6
#define BCH_ALLOC_V4_U64s	(sizeof(struct bch_alloc_v4) / sizeof(u64))

BITMASK(BCH_ALLOC_V4_NEED_DISCARD,	struct bch_alloc_v4, flags,  0,  1)
BITMASK(BCH_ALLOC_V4_NEED_INC_GEN,	struct bch_alloc_v4, flags,  1,  2)
BITMASK(BCH_ALLOC_V4_BACKPOINTERS_START,struct bch_alloc_v4, flags,  2,  8)
BITMASK(BCH_ALLOC_V4_NR_BACKPOINTERS,	struct bch_alloc_v4, flags,  8,  14)

#define BCH_ALLOC_V4_NR_BACKPOINTERS_MAX	40

struct bch_backpointer {
	struct bch_val		v;
	__u8			btree_id;
	__u8			level;
	__u8			data_type;
	__u64			bucket_offset:40;
	__u32			bucket_len;
	struct bpos		pos;
} __packed __aligned(8);

/* Quotas: */

enum quota_types {
	QTYP_USR		= 0,
	QTYP_GRP		= 1,
	QTYP_PRJ		= 2,
	QTYP_NR			= 3,
};

enum quota_counters {
	Q_SPC			= 0,
	Q_INO			= 1,
	Q_COUNTERS		= 2,
};

struct bch_quota_counter {
	__le64			hardlimit;
	__le64			softlimit;
};

struct bch_quota {
	struct bch_val		v;
	struct bch_quota_counter c[Q_COUNTERS];
} __packed __aligned(8);

/* Erasure coding */

struct bch_stripe {
	struct bch_val		v;
	__le16			sectors;
	__u8			algorithm;
	__u8			nr_blocks;
	__u8			nr_redundant;

	__u8			csum_granularity_bits;
	__u8			csum_type;
	__u8			pad;

	struct bch_extent_ptr	ptrs[];
} __packed __aligned(8);

/* Reflink: */

struct bch_reflink_p {
	struct bch_val		v;
	__le64			idx;
	/*
	 * A reflink pointer might point to an indirect extent which is then
	 * later split (by copygc or rebalance). If we only pointed to part of
	 * the original indirect extent, and then one of the fragments is
	 * outside the range we point to, we'd leak a refcount: so when creating
	 * reflink pointers, we need to store pad values to remember the full
	 * range we were taking a reference on.
	 */
	__le32			front_pad;
	__le32			back_pad;
} __packed __aligned(8);

struct bch_reflink_v {
	struct bch_val		v;
	__le64			refcount;
	union bch_extent_entry	start[0];
	__u64			_data[0];
} __packed __aligned(8);

struct bch_indirect_inline_data {
	struct bch_val		v;
	__le64			refcount;
	u8			data[0];
};

/* Inline data */

struct bch_inline_data {
	struct bch_val		v;
	u8			data[0];
};

/* Subvolumes: */

#define SUBVOL_POS_MIN		POS(0, 1)
#define SUBVOL_POS_MAX		POS(0, S32_MAX)
#define BCACHEFS_ROOT_SUBVOL	1

struct bch_subvolume {
	struct bch_val		v;
	__le32			flags;
	__le32			snapshot;
	__le64			inode;
};

LE32_BITMASK(BCH_SUBVOLUME_RO,		struct bch_subvolume, flags,  0,  1)
/*
 * We need to know whether a subvolume is a snapshot so we can know whether we
 * can delete it (or whether it should just be rm -rf'd)
 */
LE32_BITMASK(BCH_SUBVOLUME_SNAP,	struct bch_subvolume, flags,  1,  2)
LE32_BITMASK(BCH_SUBVOLUME_UNLINKED,	struct bch_subvolume, flags,  2,  3)

/* Snapshots */

struct bch_snapshot {
	struct bch_val		v;
	__le32			flags;
	__le32			parent;
	__le32			children[2];
	__le32			subvol;
	__le32			pad;
};

LE32_BITMASK(BCH_SNAPSHOT_DELETED,	struct bch_snapshot, flags,  0,  1)

/* True if a subvolume points to this snapshot node: */
LE32_BITMASK(BCH_SNAPSHOT_SUBVOL,	struct bch_snapshot, flags,  1,  2)

/* LRU btree: */

struct bch_lru {
	struct bch_val		v;
	__le64			idx;
} __packed __aligned(8);

#define LRU_ID_STRIPES		(1U << 16)

/* Optional/variable size superblock sections: */

struct bch_sb_field {
	__u64			_data[0];
	__le32			u64s;
	__le32			type;
};

#define BCH_SB_FIELDS()				\
	x(journal,	0)			\
	x(members,	1)			\
	x(crypt,	2)			\
	x(replicas_v0,	3)			\
	x(quota,	4)			\
	x(disk_groups,	5)			\
	x(clean,	6)			\
	x(replicas,	7)			\
	x(journal_seq_blacklist, 8)		\
	x(journal_v2,	9)			\
	x(counters,	10)

enum bch_sb_field_type {
#define x(f, nr)	BCH_SB_FIELD_##f = nr,
	BCH_SB_FIELDS()
#undef x
	BCH_SB_FIELD_NR
};

/*
 * Most superblock fields are replicated in all device's superblocks - a few are
 * not:
 */
#define BCH_SINGLE_DEVICE_SB_FIELDS		\
	((1U << BCH_SB_FIELD_journal)|		\
	 (1U << BCH_SB_FIELD_journal_v2))

/* BCH_SB_FIELD_journal: */

struct bch_sb_field_journal {
	struct bch_sb_field	field;
	__le64			buckets[0];
};

struct bch_sb_field_journal_v2 {
	struct bch_sb_field	field;

	struct bch_sb_field_journal_v2_entry {
		__le64		start;
		__le64		nr;
	}			d[0];
};

/* BCH_SB_FIELD_members: */

#define BCH_MIN_NR_NBUCKETS	(1 << 6)

struct bch_member {
	__uuid_t		uuid;
	__le64			nbuckets;	/* device size */
	__le16			first_bucket;   /* index of first bucket used */
	__le16			bucket_size;	/* sectors */
	__le32			pad;
	__le64			last_mount;	/* time_t */

	__le64			flags[2];
};

LE64_BITMASK(BCH_MEMBER_STATE,		struct bch_member, flags[0],  0,  4)
/* 4-14 unused, was TIER, HAS_(META)DATA, REPLACEMENT */
LE64_BITMASK(BCH_MEMBER_DISCARD,	struct bch_member, flags[0], 14, 15)
LE64_BITMASK(BCH_MEMBER_DATA_ALLOWED,	struct bch_member, flags[0], 15, 20)
LE64_BITMASK(BCH_MEMBER_GROUP,		struct bch_member, flags[0], 20, 28)
LE64_BITMASK(BCH_MEMBER_DURABILITY,	struct bch_member, flags[0], 28, 30)
LE64_BITMASK(BCH_MEMBER_FREESPACE_INITIALIZED,
					struct bch_member, flags[0], 30, 31)

#if 0
LE64_BITMASK(BCH_MEMBER_NR_READ_ERRORS,	struct bch_member, flags[1], 0,  20);
LE64_BITMASK(BCH_MEMBER_NR_WRITE_ERRORS,struct bch_member, flags[1], 20, 40);
#endif

#define BCH_MEMBER_STATES()			\
	x(rw,		0)			\
	x(ro,		1)			\
	x(failed,	2)			\
	x(spare,	3)

enum bch_member_state {
#define x(t, n) BCH_MEMBER_STATE_##t = n,
	BCH_MEMBER_STATES()
#undef x
	BCH_MEMBER_STATE_NR
};

struct bch_sb_field_members {
	struct bch_sb_field	field;
	struct bch_member	members[0];
};

/* BCH_SB_FIELD_crypt: */

struct nonce {
	__le32			d[4];
};

struct bch_key {
	__le64			key[4];
};

#define BCH_KEY_MAGIC					\
	(((u64) 'b' <<  0)|((u64) 'c' <<  8)|		\
	 ((u64) 'h' << 16)|((u64) '*' << 24)|		\
	 ((u64) '*' << 32)|((u64) 'k' << 40)|		\
	 ((u64) 'e' << 48)|((u64) 'y' << 56))

struct bch_encrypted_key {
	__le64			magic;
	struct bch_key		key;
};

/*
 * If this field is present in the superblock, it stores an encryption key which
 * is used encrypt all other data/metadata. The key will normally be encrypted
 * with the key userspace provides, but if encryption has been turned off we'll
 * just store the master key unencrypted in the superblock so we can access the
 * previously encrypted data.
 */
struct bch_sb_field_crypt {
	struct bch_sb_field	field;

	__le64			flags;
	__le64			kdf_flags;
	struct bch_encrypted_key key;
};

LE64_BITMASK(BCH_CRYPT_KDF_TYPE,	struct bch_sb_field_crypt, flags, 0, 4);

enum bch_kdf_types {
	BCH_KDF_SCRYPT		= 0,
	BCH_KDF_NR		= 1,
};

/* stored as base 2 log of scrypt params: */
LE64_BITMASK(BCH_KDF_SCRYPT_N,	struct bch_sb_field_crypt, kdf_flags,  0, 16);
LE64_BITMASK(BCH_KDF_SCRYPT_R,	struct bch_sb_field_crypt, kdf_flags, 16, 32);
LE64_BITMASK(BCH_KDF_SCRYPT_P,	struct bch_sb_field_crypt, kdf_flags, 32, 48);

/* BCH_SB_FIELD_replicas: */

#define BCH_DATA_TYPES()		\
	x(free,		0)		\
	x(sb,		1)		\
	x(journal,	2)		\
	x(btree,	3)		\
	x(user,		4)		\
	x(cached,	5)		\
	x(parity,	6)		\
	x(stripe,	7)		\
	x(need_gc_gens,	8)		\
	x(need_discard,	9)

enum bch_data_type {
#define x(t, n) BCH_DATA_##t,
	BCH_DATA_TYPES()
#undef x
	BCH_DATA_NR
};

static inline bool data_type_is_empty(enum bch_data_type type)
{
	switch (type) {
	case BCH_DATA_free:
	case BCH_DATA_need_gc_gens:
	case BCH_DATA_need_discard:
		return true;
	default:
		return false;
	}
}

static inline bool data_type_is_hidden(enum bch_data_type type)
{
	switch (type) {
	case BCH_DATA_sb:
	case BCH_DATA_journal:
		return true;
	default:
		return false;
	}
}

struct bch_replicas_entry_v0 {
	__u8			data_type;
	__u8			nr_devs;
	__u8			devs[];
} __packed;

struct bch_sb_field_replicas_v0 {
	struct bch_sb_field	field;
	struct bch_replicas_entry_v0 entries[];
} __packed __aligned(8);

struct bch_replicas_entry {
	__u8			data_type;
	__u8			nr_devs;
	__u8			nr_required;
	__u8			devs[];
} __packed;

#define replicas_entry_bytes(_i)					\
	(offsetof(typeof(*(_i)), devs) + (_i)->nr_devs)

struct bch_sb_field_replicas {
	struct bch_sb_field	field;
	struct bch_replicas_entry entries[];
} __packed __aligned(8);

/* BCH_SB_FIELD_quota: */

struct bch_sb_quota_counter {
	__le32				timelimit;
	__le32				warnlimit;
};

struct bch_sb_quota_type {
	__le64				flags;
	struct bch_sb_quota_counter	c[Q_COUNTERS];
};

struct bch_sb_field_quota {
	struct bch_sb_field		field;
	struct bch_sb_quota_type	q[QTYP_NR];
} __packed __aligned(8);

/* BCH_SB_FIELD_disk_groups: */

#define BCH_SB_LABEL_SIZE		32

struct bch_disk_group {
	__u8			label[BCH_SB_LABEL_SIZE];
	__le64			flags[2];
} __packed __aligned(8);

LE64_BITMASK(BCH_GROUP_DELETED,		struct bch_disk_group, flags[0], 0,  1)
LE64_BITMASK(BCH_GROUP_DATA_ALLOWED,	struct bch_disk_group, flags[0], 1,  6)
LE64_BITMASK(BCH_GROUP_PARENT,		struct bch_disk_group, flags[0], 6, 24)

struct bch_sb_field_disk_groups {
	struct bch_sb_field	field;
	struct bch_disk_group	entries[0];
} __packed __aligned(8);

/* BCH_SB_FIELD_counters */

#define BCH_PERSISTENT_COUNTERS()				\
	x(io_read,					0)	\
	x(io_write,					1)	\
	x(io_move,					2)	\
	x(bucket_invalidate,				3)	\
	x(bucket_discard,				4)	\
	x(bucket_alloc,					5)	\
	x(bucket_alloc_fail,				6)	\
	x(btree_cache_scan,				7)	\
	x(btree_cache_reap,				8)	\
	x(btree_cache_cannibalize,			9)	\
	x(btree_cache_cannibalize_lock,			10)	\
	x(btree_cache_cannibalize_lock_fail,		11)	\
	x(btree_cache_cannibalize_unlock,		12)	\
	x(btree_node_write,				13)	\
	x(btree_node_read,				14)	\
	x(btree_node_compact,				15)	\
	x(btree_node_merge,				16)	\
	x(btree_node_split,				17)	\
	x(btree_node_rewrite,				18)	\
	x(btree_node_alloc,				19)	\
	x(btree_node_free,				20)	\
	x(btree_node_set_root,				21)	\
	x(btree_path_relock_fail,			22)	\
	x(btree_path_upgrade_fail,			23)	\
	x(btree_reserve_get_fail,			24)	\
	x(journal_entry_full,				25)	\
	x(journal_full,					26)	\
	x(journal_reclaim_finish,			27)	\
	x(journal_reclaim_start,			28)	\
	x(journal_write,				29)	\
	x(read_promote,					30)	\
	x(read_bounce,					31)	\
	x(read_split,					33)	\
	x(read_retry,					32)	\
	x(read_reuse_race,				34)	\
	x(move_extent_read,				35)	\
	x(move_extent_write,				36)	\
	x(move_extent_finish,				37)	\
	x(move_extent_fail,				38)	\
	x(move_extent_alloc_mem_fail,			39)	\
	x(copygc,					40)	\
	x(copygc_wait,					41)	\
	x(gc_gens_end,					42)	\
	x(gc_gens_start,				43)	\
	x(trans_blocked_journal_reclaim,		44)	\
	x(trans_restart_btree_node_reused,		45)	\
	x(trans_restart_btree_node_split,		46)	\
	x(trans_restart_fault_inject,			47)	\
	x(trans_restart_iter_upgrade,			48)	\
	x(trans_restart_journal_preres_get,		49)	\
	x(trans_restart_journal_reclaim,		50)	\
	x(trans_restart_journal_res_get,		51)	\
	x(trans_restart_key_cache_key_realloced,	52)	\
	x(trans_restart_key_cache_raced,		53)	\
	x(trans_restart_mark_replicas,			54)	\
	x(trans_restart_mem_realloced,			55)	\
	x(trans_restart_memory_allocation_failure,	56)	\
	x(trans_restart_relock,				57)	\
	x(trans_restart_relock_after_fill,		58)	\
	x(trans_restart_relock_key_cache_fill,		59)	\
	x(trans_restart_relock_next_node,		60)	\
	x(trans_restart_relock_parent_for_fill,		61)	\
	x(trans_restart_relock_path,			62)	\
	x(trans_restart_relock_path_intent,		63)	\
	x(trans_restart_too_many_iters,			64)	\
	x(trans_restart_traverse,			65)	\
	x(trans_restart_upgrade,			66)	\
	x(trans_restart_would_deadlock,			67)	\
	x(trans_restart_would_deadlock_write,		68)	\
	x(trans_restart_injected,			69)	\
	x(trans_restart_key_cache_upgrade,		70)	\
	x(trans_traverse_all,				71)	\
	x(transaction_commit,				72)	\
	x(write_super,					73)	\
	x(trans_restart_would_deadlock_recursion_limit,	74)	\
	x(trans_restart_write_buffer_flush,		75)

enum bch_persistent_counters {
#define x(t, n, ...) BCH_COUNTER_##t,
	BCH_PERSISTENT_COUNTERS()
#undef x
	BCH_COUNTER_NR
};

struct bch_sb_field_counters {
	struct bch_sb_field	field;
	__le64			d[0];
};

/*
 * On clean shutdown, store btree roots and current journal sequence number in
 * the superblock:
 */
struct jset_entry {
	__le16			u64s;
	__u8			btree_id;
	__u8			level;
	__u8			type; /* designates what this jset holds */
	__u8			pad[3];

	union {
		struct bkey_i	start[0];
		__u64		_data[0];
	};
};

struct bch_sb_field_clean {
	struct bch_sb_field	field;

	__le32			flags;
	__le16			_read_clock; /* no longer used */
	__le16			_write_clock;
	__le64			journal_seq;

	union {
		struct jset_entry start[0];
		__u64		_data[0];
	};
};

struct journal_seq_blacklist_entry {
	__le64			start;
	__le64			end;
};

struct bch_sb_field_journal_seq_blacklist {
	struct bch_sb_field	field;

	union {
		struct journal_seq_blacklist_entry start[0];
		__u64		_data[0];
	};
};

/* Superblock: */

/*
 * New versioning scheme:
 * One common version number for all on disk data structures - superblock, btree
 * nodes, journal entries
 */
#define BCH_JSET_VERSION_OLD			2
#define BCH_BSET_VERSION_OLD			3

#define BCH_METADATA_VERSIONS()				\
	x(bkey_renumber,		10)		\
	x(inode_btree_change,		11)		\
	x(snapshot,			12)		\
	x(inode_backpointers,		13)		\
	x(btree_ptr_sectors_written,	14)		\
	x(snapshot_2,			15)		\
	x(reflink_p_fix,		16)		\
	x(subvol_dirent,		17)		\
	x(inode_v2,			18)		\
	x(freespace,			19)		\
	x(alloc_v4,			20)		\
	x(new_data_types,		21)		\
	x(backpointers,			22)		\
	x(inode_v3,			23)

enum bcachefs_metadata_version {
	bcachefs_metadata_version_min = 9,
#define x(t, n)	bcachefs_metadata_version_##t = n,
	BCH_METADATA_VERSIONS()
#undef x
	bcachefs_metadata_version_max
};

#define bcachefs_metadata_version_current	(bcachefs_metadata_version_max - 1)

#define BCH_SB_SECTOR			8
#define BCH_SB_MEMBERS_MAX		64 /* XXX kill */

struct bch_sb_layout {
	__uuid_t		magic;	/* bcachefs superblock UUID */
	__u8			layout_type;
	__u8			sb_max_size_bits; /* base 2 of 512 byte sectors */
	__u8			nr_superblocks;
	__u8			pad[5];
	__le64			sb_offset[61];
} __packed __aligned(8);

#define BCH_SB_LAYOUT_SECTOR	7

/*
 * @offset	- sector where this sb was written
 * @version	- on disk format version
 * @version_min	- Oldest metadata version this filesystem contains; so we can
 *		  safely drop compatibility code and refuse to mount filesystems
 *		  we'd need it for
 * @magic	- identifies as a bcachefs superblock (BCHFS_MAGIC)
 * @seq		- incremented each time superblock is written
 * @uuid	- used for generating various magic numbers and identifying
 *                member devices, never changes
 * @user_uuid	- user visible UUID, may be changed
 * @label	- filesystem label
 * @seq		- identifies most recent superblock, incremented each time
 *		  superblock is written
 * @features	- enabled incompatible features
 */
struct bch_sb {
	struct bch_csum		csum;
	__le16			version;
	__le16			version_min;
	__le16			pad[2];
	__uuid_t		magic;
	__uuid_t		uuid;
	__uuid_t		user_uuid;
	__u8			label[BCH_SB_LABEL_SIZE];
	__le64			offset;
	__le64			seq;

	__le16			block_size;
	__u8			dev_idx;
	__u8			nr_devices;
	__le32			u64s;

	__le64			time_base_lo;
	__le32			time_base_hi;
	__le32			time_precision;

	__le64			flags[8];
	__le64			features[2];
	__le64			compat[2];

	struct bch_sb_layout	layout;

	union {
		struct bch_sb_field start[0];
		__le64		_data[0];
	};
} __packed __aligned(8);

/*
 * Flags:
 * BCH_SB_INITALIZED	- set on first mount
 * BCH_SB_CLEAN		- did we shut down cleanly? Just a hint, doesn't affect
 *			  behaviour of mount/recovery path:
 * BCH_SB_INODE_32BIT	- limit inode numbers to 32 bits
 * BCH_SB_128_BIT_MACS	- 128 bit macs instead of 80
 * BCH_SB_ENCRYPTION_TYPE - if nonzero encryption is enabled; overrides
 *			   DATA/META_CSUM_TYPE. Also indicates encryption
 *			   algorithm in use, if/when we get more than one
 */

LE16_BITMASK(BCH_SB_BLOCK_SIZE,		struct bch_sb, block_size, 0, 16);

LE64_BITMASK(BCH_SB_INITIALIZED,	struct bch_sb, flags[0],  0,  1);
LE64_BITMASK(BCH_SB_CLEAN,		struct bch_sb, flags[0],  1,  2);
LE64_BITMASK(BCH_SB_CSUM_TYPE,		struct bch_sb, flags[0],  2,  8);
LE64_BITMASK(BCH_SB_ERROR_ACTION,	struct bch_sb, flags[0],  8, 12);

LE64_BITMASK(BCH_SB_BTREE_NODE_SIZE,	struct bch_sb, flags[0], 12, 28);

LE64_BITMASK(BCH_SB_GC_RESERVE,		struct bch_sb, flags[0], 28, 33);
LE64_BITMASK(BCH_SB_ROOT_RESERVE,	struct bch_sb, flags[0], 33, 40);

LE64_BITMASK(BCH_SB_META_CSUM_TYPE,	struct bch_sb, flags[0], 40, 44);
LE64_BITMASK(BCH_SB_DATA_CSUM_TYPE,	struct bch_sb, flags[0], 44, 48);

LE64_BITMASK(BCH_SB_META_REPLICAS_WANT,	struct bch_sb, flags[0], 48, 52);
LE64_BITMASK(BCH_SB_DATA_REPLICAS_WANT,	struct bch_sb, flags[0], 52, 56);

LE64_BITMASK(BCH_SB_POSIX_ACL,		struct bch_sb, flags[0], 56, 57);
LE64_BITMASK(BCH_SB_USRQUOTA,		struct bch_sb, flags[0], 57, 58);
LE64_BITMASK(BCH_SB_GRPQUOTA,		struct bch_sb, flags[0], 58, 59);
LE64_BITMASK(BCH_SB_PRJQUOTA,		struct bch_sb, flags[0], 59, 60);

LE64_BITMASK(BCH_SB_HAS_ERRORS,		struct bch_sb, flags[0], 60, 61);
LE64_BITMASK(BCH_SB_HAS_TOPOLOGY_ERRORS,struct bch_sb, flags[0], 61, 62);

LE64_BITMASK(BCH_SB_BIG_ENDIAN,		struct bch_sb, flags[0], 62, 63);

LE64_BITMASK(BCH_SB_STR_HASH_TYPE,	struct bch_sb, flags[1],  0,  4);
LE64_BITMASK(BCH_SB_COMPRESSION_TYPE,	struct bch_sb, flags[1],  4,  8);
LE64_BITMASK(BCH_SB_INODE_32BIT,	struct bch_sb, flags[1],  8,  9);

LE64_BITMASK(BCH_SB_128_BIT_MACS,	struct bch_sb, flags[1],  9, 10);
LE64_BITMASK(BCH_SB_ENCRYPTION_TYPE,	struct bch_sb, flags[1], 10, 14);

/*
 * Max size of an extent that may require bouncing to read or write
 * (checksummed, compressed): 64k
 */
LE64_BITMASK(BCH_SB_ENCODED_EXTENT_MAX_BITS,
					struct bch_sb, flags[1], 14, 20);

LE64_BITMASK(BCH_SB_META_REPLICAS_REQ,	struct bch_sb, flags[1], 20, 24);
LE64_BITMASK(BCH_SB_DATA_REPLICAS_REQ,	struct bch_sb, flags[1], 24, 28);

LE64_BITMASK(BCH_SB_PROMOTE_TARGET,	struct bch_sb, flags[1], 28, 40);
LE64_BITMASK(BCH_SB_FOREGROUND_TARGET,	struct bch_sb, flags[1], 40, 52);
LE64_BITMASK(BCH_SB_BACKGROUND_TARGET,	struct bch_sb, flags[1], 52, 64);

LE64_BITMASK(BCH_SB_BACKGROUND_COMPRESSION_TYPE,
					struct bch_sb, flags[2],  0,  4);
LE64_BITMASK(BCH_SB_GC_RESERVE_BYTES,	struct bch_sb, flags[2],  4, 64);

LE64_BITMASK(BCH_SB_ERASURE_CODE,	struct bch_sb, flags[3],  0, 16);
LE64_BITMASK(BCH_SB_METADATA_TARGET,	struct bch_sb, flags[3], 16, 28);
LE64_BITMASK(BCH_SB_SHARD_INUMS,	struct bch_sb, flags[3], 28, 29);
LE64_BITMASK(BCH_SB_INODES_USE_KEY_CACHE,struct bch_sb, flags[3], 29, 30);
LE64_BITMASK(BCH_SB_JOURNAL_FLUSH_DELAY,struct bch_sb, flags[3], 30, 62);
LE64_BITMASK(BCH_SB_JOURNAL_FLUSH_DISABLED,struct bch_sb, flags[3], 62, 63);
LE64_BITMASK(BCH_SB_JOURNAL_RECLAIM_DELAY,struct bch_sb, flags[4], 0, 32);
LE64_BITMASK(BCH_SB_JOURNAL_TRANSACTION_NAMES,struct bch_sb, flags[4], 32, 33);
LE64_BITMASK(BCH_SB_WRITE_BUFFER_SIZE,	struct bch_sb, flags[4], 34, 54);

/*
 * Features:
 *
 * journal_seq_blacklist_v3:	gates BCH_SB_FIELD_journal_seq_blacklist
 * reflink:			gates KEY_TYPE_reflink
 * inline_data:			gates KEY_TYPE_inline_data
 * new_siphash:			gates BCH_STR_HASH_siphash
 * new_extent_overwrite:	gates BTREE_NODE_NEW_EXTENT_OVERWRITE
 */
#define BCH_SB_FEATURES()			\
	x(lz4,				0)	\
	x(gzip,				1)	\
	x(zstd,				2)	\
	x(atomic_nlink,			3)	\
	x(ec,				4)	\
	x(journal_seq_blacklist_v3,	5)	\
	x(reflink,			6)	\
	x(new_siphash,			7)	\
	x(inline_data,			8)	\
	x(new_extent_overwrite,		9)	\
	x(incompressible,		10)	\
	x(btree_ptr_v2,			11)	\
	x(extents_above_btree_updates,	12)	\
	x(btree_updates_journalled,	13)	\
	x(reflink_inline_data,		14)	\
	x(new_varint,			15)	\
	x(journal_no_flush,		16)	\
	x(alloc_v2,			17)	\
	x(extents_across_btree_nodes,	18)

#define BCH_SB_FEATURES_ALWAYS				\
	((1ULL << BCH_FEATURE_new_extent_overwrite)|	\
	 (1ULL << BCH_FEATURE_extents_above_btree_updates)|\
	 (1ULL << BCH_FEATURE_btree_updates_journalled)|\
	 (1ULL << BCH_FEATURE_alloc_v2)|\
	 (1ULL << BCH_FEATURE_extents_across_btree_nodes))

#define BCH_SB_FEATURES_ALL				\
	(BCH_SB_FEATURES_ALWAYS|			\
	 (1ULL << BCH_FEATURE_new_siphash)|		\
	 (1ULL << BCH_FEATURE_btree_ptr_v2)|		\
	 (1ULL << BCH_FEATURE_new_varint)|		\
	 (1ULL << BCH_FEATURE_journal_no_flush))

enum bch_sb_feature {
#define x(f, n) BCH_FEATURE_##f,
	BCH_SB_FEATURES()
#undef x
	BCH_FEATURE_NR,
};

#define BCH_SB_COMPAT()					\
	x(alloc_info,				0)	\
	x(alloc_metadata,			1)	\
	x(extents_above_btree_updates_done,	2)	\
	x(bformat_overflow_done,		3)

enum bch_sb_compat {
#define x(f, n) BCH_COMPAT_##f,
	BCH_SB_COMPAT()
#undef x
	BCH_COMPAT_NR,
};

/* options: */

#define BCH_REPLICAS_MAX		4U

#define BCH_BKEY_PTRS_MAX		16U

#define BCH_ERROR_ACTIONS()		\
	x(continue,		0)	\
	x(ro,			1)	\
	x(panic,		2)

enum bch_error_actions {
#define x(t, n) BCH_ON_ERROR_##t = n,
	BCH_ERROR_ACTIONS()
#undef x
	BCH_ON_ERROR_NR
};

#define BCH_STR_HASH_TYPES()		\
	x(crc32c,		0)	\
	x(crc64,		1)	\
	x(siphash_old,		2)	\
	x(siphash,		3)

enum bch_str_hash_type {
#define x(t, n) BCH_STR_HASH_##t = n,
	BCH_STR_HASH_TYPES()
#undef x
	BCH_STR_HASH_NR
};

#define BCH_STR_HASH_OPTS()		\
	x(crc32c,		0)	\
	x(crc64,		1)	\
	x(siphash,		2)

enum bch_str_hash_opts {
#define x(t, n) BCH_STR_HASH_OPT_##t = n,
	BCH_STR_HASH_OPTS()
#undef x
	BCH_STR_HASH_OPT_NR
};

#define BCH_CSUM_TYPES()			\
	x(none,				0)	\
	x(crc32c_nonzero,		1)	\
	x(crc64_nonzero,		2)	\
	x(chacha20_poly1305_80,		3)	\
	x(chacha20_poly1305_128,	4)	\
	x(crc32c,			5)	\
	x(crc64,			6)	\
	x(xxhash,			7)

enum bch_csum_type {
#define x(t, n) BCH_CSUM_##t = n,
	BCH_CSUM_TYPES()
#undef x
	BCH_CSUM_NR
};

static const unsigned bch_crc_bytes[] = {
	[BCH_CSUM_none]				= 0,
	[BCH_CSUM_crc32c_nonzero]		= 4,
	[BCH_CSUM_crc32c]			= 4,
	[BCH_CSUM_crc64_nonzero]		= 8,
	[BCH_CSUM_crc64]			= 8,
	[BCH_CSUM_xxhash]			= 8,
	[BCH_CSUM_chacha20_poly1305_80]		= 10,
	[BCH_CSUM_chacha20_poly1305_128]	= 16,
};

static inline _Bool bch2_csum_type_is_encryption(enum bch_csum_type type)
{
	switch (type) {
	case BCH_CSUM_chacha20_poly1305_80:
	case BCH_CSUM_chacha20_poly1305_128:
		return true;
	default:
		return false;
	}
}

#define BCH_CSUM_OPTS()			\
	x(none,			0)	\
	x(crc32c,		1)	\
	x(crc64,		2)	\
	x(xxhash,		3)

enum bch_csum_opts {
#define x(t, n) BCH_CSUM_OPT_##t = n,
	BCH_CSUM_OPTS()
#undef x
	BCH_CSUM_OPT_NR
};

#define BCH_COMPRESSION_TYPES()		\
	x(none,			0)	\
	x(lz4_old,		1)	\
	x(gzip,			2)	\
	x(lz4,			3)	\
	x(zstd,			4)	\
	x(incompressible,	5)

enum bch_compression_type {
#define x(t, n) BCH_COMPRESSION_TYPE_##t = n,
	BCH_COMPRESSION_TYPES()
#undef x
	BCH_COMPRESSION_TYPE_NR
};

#define BCH_COMPRESSION_OPTS()		\
	x(none,		0)		\
	x(lz4,		1)		\
	x(gzip,		2)		\
	x(zstd,		3)

enum bch_compression_opts {
#define x(t, n) BCH_COMPRESSION_OPT_##t = n,
	BCH_COMPRESSION_OPTS()
#undef x
	BCH_COMPRESSION_OPT_NR
};

/*
 * Magic numbers
 *
 * The various other data structures have their own magic numbers, which are
 * xored with the first part of the cache set's UUID
 */

#define BCACHE_MAGIC							\
	UUID_INIT(0xc68573f6, 0x4e1a, 0x45ca,				\
		  0x82, 0x65, 0xf5, 0x7f, 0x48, 0xba, 0x6d, 0x81)
#define BCHFS_MAGIC							\
	UUID_INIT(0xc68573f6, 0x66ce, 0x90a9,				\
		  0xd9, 0x6a, 0x60, 0xcf, 0x80, 0x3d, 0xf7, 0xef)

#define BCACHEFS_STATFS_MAGIC		0xca451a4e

#define JSET_MAGIC		__cpu_to_le64(0x245235c1a3625032ULL)
#define BSET_MAGIC		__cpu_to_le64(0x90135c78b99e07f5ULL)

static inline __le64 __bch2_sb_magic(struct bch_sb *sb)
{
	__le64 ret;

	memcpy(&ret, &sb->uuid, sizeof(ret));
	return ret;
}

static inline __u64 __jset_magic(struct bch_sb *sb)
{
	return __le64_to_cpu(__bch2_sb_magic(sb) ^ JSET_MAGIC);
}

static inline __u64 __bset_magic(struct bch_sb *sb)
{
	return __le64_to_cpu(__bch2_sb_magic(sb) ^ BSET_MAGIC);
}

/* Journal */

#define JSET_KEYS_U64s	(sizeof(struct jset_entry) / sizeof(__u64))

#define BCH_JSET_ENTRY_TYPES()			\
	x(btree_keys,		0)		\
	x(btree_root,		1)		\
	x(prio_ptrs,		2)		\
	x(blacklist,		3)		\
	x(blacklist_v2,		4)		\
	x(usage,		5)		\
	x(data_usage,		6)		\
	x(clock,		7)		\
	x(dev_usage,		8)		\
	x(log,			9)		\
	x(overwrite,		10)

enum {
#define x(f, nr)	BCH_JSET_ENTRY_##f	= nr,
	BCH_JSET_ENTRY_TYPES()
#undef x
	BCH_JSET_ENTRY_NR
};

/*
 * Journal sequence numbers can be blacklisted: bsets record the max sequence
 * number of all the journal entries they contain updates for, so that on
 * recovery we can ignore those bsets that contain index updates newer that what
 * made it into the journal.
 *
 * This means that we can't reuse that journal_seq - we have to skip it, and
 * then record that we skipped it so that the next time we crash and recover we
 * don't think there was a missing journal entry.
 */
struct jset_entry_blacklist {
	struct jset_entry	entry;
	__le64			seq;
};

struct jset_entry_blacklist_v2 {
	struct jset_entry	entry;
	__le64			start;
	__le64			end;
};

#define BCH_FS_USAGE_TYPES()			\
	x(reserved,		0)		\
	x(inodes,		1)		\
	x(key_version,		2)

enum {
#define x(f, nr)	BCH_FS_USAGE_##f	= nr,
	BCH_FS_USAGE_TYPES()
#undef x
	BCH_FS_USAGE_NR
};

struct jset_entry_usage {
	struct jset_entry	entry;
	__le64			v;
} __packed;

struct jset_entry_data_usage {
	struct jset_entry	entry;
	__le64			v;
	struct bch_replicas_entry r;
} __packed;

struct jset_entry_clock {
	struct jset_entry	entry;
	__u8			rw;
	__u8			pad[7];
	__le64			time;
} __packed;

struct jset_entry_dev_usage_type {
	__le64			buckets;
	__le64			sectors;
	__le64			fragmented;
} __packed;

struct jset_entry_dev_usage {
	struct jset_entry	entry;
	__le32			dev;
	__u32			pad;

	__le64			buckets_ec;
	__le64			_buckets_unavailable; /* No longer used */

	struct jset_entry_dev_usage_type d[];
} __packed;

static inline unsigned jset_entry_dev_usage_nr_types(struct jset_entry_dev_usage *u)
{
	return (vstruct_bytes(&u->entry) - sizeof(struct jset_entry_dev_usage)) /
		sizeof(struct jset_entry_dev_usage_type);
}

struct jset_entry_log {
	struct jset_entry	entry;
	u8			d[];
} __packed;

/*
 * On disk format for a journal entry:
 * seq is monotonically increasing; every journal entry has its own unique
 * sequence number.
 *
 * last_seq is the oldest journal entry that still has keys the btree hasn't
 * flushed to disk yet.
 *
 * version is for on disk format changes.
 */
struct jset {
	struct bch_csum		csum;

	__le64			magic;
	__le64			seq;
	__le32			version;
	__le32			flags;

	__le32			u64s; /* size of d[] in u64s */

	__u8			encrypted_start[0];

	__le16			_read_clock; /* no longer used */
	__le16			_write_clock;

	/* Sequence number of oldest dirty journal entry */
	__le64			last_seq;


	union {
		struct jset_entry start[0];
		__u64		_data[0];
	};
} __packed __aligned(8);

LE32_BITMASK(JSET_CSUM_TYPE,	struct jset, flags, 0, 4);
LE32_BITMASK(JSET_BIG_ENDIAN,	struct jset, flags, 4, 5);
LE32_BITMASK(JSET_NO_FLUSH,	struct jset, flags, 5, 6);

#define BCH_JOURNAL_BUCKETS_MIN		8

/* Btree: */

#define BCH_BTREE_IDS()				\
	x(extents,		0)		\
	x(inodes,		1)		\
	x(dirents,		2)		\
	x(xattrs,		3)		\
	x(alloc,		4)		\
	x(quotas,		5)		\
	x(stripes,		6)		\
	x(reflink,		7)		\
	x(subvolumes,		8)		\
	x(snapshots,		9)		\
	x(lru,			10)		\
	x(freespace,		11)		\
	x(need_discard,		12)		\
	x(backpointers,		13)

enum btree_id {
#define x(kwd, val) BTREE_ID_##kwd = val,
	BCH_BTREE_IDS()
#undef x
	BTREE_ID_NR
};

#define BTREE_MAX_DEPTH		4U

/* Btree nodes */

/*
 * Btree nodes
 *
 * On disk a btree node is a list/log of these; within each set the keys are
 * sorted
 */
struct bset {
	__le64			seq;

	/*
	 * Highest journal entry this bset contains keys for.
	 * If on recovery we don't see that journal entry, this bset is ignored:
	 * this allows us to preserve the order of all index updates after a
	 * crash, since the journal records a total order of all index updates
	 * and anything that didn't make it to the journal doesn't get used.
	 */
	__le64			journal_seq;

	__le32			flags;
	__le16			version;
	__le16			u64s; /* count of d[] in u64s */

	union {
		struct bkey_packed start[0];
		__u64		_data[0];
	};
} __packed __aligned(8);

LE32_BITMASK(BSET_CSUM_TYPE,	struct bset, flags, 0, 4);

LE32_BITMASK(BSET_BIG_ENDIAN,	struct bset, flags, 4, 5);
LE32_BITMASK(BSET_SEPARATE_WHITEOUTS,
				struct bset, flags, 5, 6);

/* Sector offset within the btree node: */
LE32_BITMASK(BSET_OFFSET,	struct bset, flags, 16, 32);

struct btree_node {
	struct bch_csum		csum;
	__le64			magic;

	/* this flags field is encrypted, unlike bset->flags: */
	__le64			flags;

	/* Closed interval: */
	struct bpos		min_key;
	struct bpos		max_key;
	struct bch_extent_ptr	_ptr; /* not used anymore */
	struct bkey_format	format;

	union {
	struct bset		keys;
	struct {
		__u8		pad[22];
		__le16		u64s;
		__u64		_data[0];

	};
	};
} __packed __aligned(8);

LE64_BITMASK(BTREE_NODE_ID,	struct btree_node, flags,  0,  4);
LE64_BITMASK(BTREE_NODE_LEVEL,	struct btree_node, flags,  4,  8);
LE64_BITMASK(BTREE_NODE_NEW_EXTENT_OVERWRITE,
				struct btree_node, flags,  8,  9);
/* 9-32 unused */
LE64_BITMASK(BTREE_NODE_SEQ,	struct btree_node, flags, 32, 64);

struct btree_node_entry {
	struct bch_csum		csum;

	union {
	struct bset		keys;
	struct {
		__u8		pad[22];
		__le16		u64s;
		__u64		_data[0];

	};
	};
} __packed __aligned(8);

#endif /* _BCACHEFS_FORMAT_H */
