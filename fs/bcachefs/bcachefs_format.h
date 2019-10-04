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

#ifdef __KERNEL__
typedef uuid_t __uuid_t;
#endif

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
} __attribute__((packed, aligned(4)));

#define KEY_INODE_MAX			((__u64)~0ULL)
#define KEY_OFFSET_MAX			((__u64)~0ULL)
#define KEY_SNAPSHOT_MAX		((__u32)~0U)
#define KEY_SIZE_MAX			((__u32)~0U)

static inline struct bpos POS(__u64 inode, __u64 offset)
{
	struct bpos ret;

	ret.inode	= inode;
	ret.offset	= offset;
	ret.snapshot	= 0;

	return ret;
}

#define POS_MIN				POS(0, 0)
#define POS_MAX				POS(KEY_INODE_MAX, KEY_OFFSET_MAX)

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
} __attribute__((packed, aligned(4)));

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
} __attribute__((packed, aligned(8)));

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
} __attribute__((packed, aligned(8)));

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
	x(discard,		1)			\
	x(error,		2)			\
	x(cookie,		3)			\
	x(whiteout,		4)			\
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
	x(reflink_v,		16)

enum bch_bkey_type {
#define x(name, nr) KEY_TYPE_##name	= nr,
	BCH_BKEY_TYPES()
#undef x
	KEY_TYPE_MAX,
};

struct bch_cookie {
	struct bch_val		v;
	__le64			cookie;
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
} __attribute__((packed, aligned(8)));

enum bch_csum_type {
	BCH_CSUM_NONE			= 0,
	BCH_CSUM_CRC32C_NONZERO		= 1,
	BCH_CSUM_CRC64_NONZERO		= 2,
	BCH_CSUM_CHACHA20_POLY1305_80	= 3,
	BCH_CSUM_CHACHA20_POLY1305_128	= 4,
	BCH_CSUM_CRC32C			= 5,
	BCH_CSUM_CRC64			= 6,
	BCH_CSUM_NR			= 7,
};

static const unsigned bch_crc_bytes[] = {
	[BCH_CSUM_NONE]				= 0,
	[BCH_CSUM_CRC32C_NONZERO]		= 4,
	[BCH_CSUM_CRC32C]			= 4,
	[BCH_CSUM_CRC64_NONZERO]		= 8,
	[BCH_CSUM_CRC64]			= 8,
	[BCH_CSUM_CHACHA20_POLY1305_80]		= 10,
	[BCH_CSUM_CHACHA20_POLY1305_128]	= 16,
};

static inline _Bool bch2_csum_type_is_encryption(enum bch_csum_type type)
{
	switch (type) {
	case BCH_CSUM_CHACHA20_POLY1305_80:
	case BCH_CSUM_CHACHA20_POLY1305_128:
		return true;
	default:
		return false;
	}
}

enum bch_compression_type {
	BCH_COMPRESSION_NONE		= 0,
	BCH_COMPRESSION_LZ4_OLD		= 1,
	BCH_COMPRESSION_GZIP		= 2,
	BCH_COMPRESSION_LZ4		= 3,
	BCH_COMPRESSION_ZSTD		= 4,
	BCH_COMPRESSION_NR		= 5,
};

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
} __attribute__((packed, aligned(8)));

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
} __attribute__((packed, aligned(8)));

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
} __attribute__((packed, aligned(8)));

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
				reservation:1,
				offset:44, /* 8 petabytes */
				dev:8,
				gen:8;
#elif defined (__BIG_ENDIAN_BITFIELD)
	__u64			gen:8,
				dev:8,
				offset:44,
				reservation:1,
				unused:1,
				cached:1,
				type:1;
#endif
} __attribute__((packed, aligned(8)));

struct bch_extent_stripe_ptr {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u64			type:5,
				block:8,
				idx:51;
#elif defined (__BIG_ENDIAN_BITFIELD)
	__u64			idx:51,
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
} __attribute__((packed, aligned(8)));

struct bch_extent {
	struct bch_val		v;

	__u64			_data[0];
	union bch_extent_entry	start[];
} __attribute__((packed, aligned(8)));

struct bch_reservation {
	struct bch_val		v;

	__le32			generation;
	__u8			nr_replicas;
	__u8			pad[3];
} __attribute__((packed, aligned(8)));

/* Maximum size (in u64s) a single pointer could be: */
#define BKEY_EXTENT_PTR_U64s_MAX\
	((sizeof(struct bch_extent_crc128) +			\
	  sizeof(struct bch_extent_ptr)) / sizeof(u64))

/* Maximum possible size of an entire extent value: */
#define BKEY_EXTENT_VAL_U64s_MAX				\
	(1 + BKEY_EXTENT_PTR_U64s_MAX * (BCH_REPLICAS_MAX + 1))

#define BKEY_PADDED(key)	__BKEY_PADDED(key, BKEY_EXTENT_VAL_U64s_MAX)

/* * Maximum possible size of an entire extent, key + value: */
#define BKEY_EXTENT_U64s_MAX		(BKEY_U64s + BKEY_EXTENT_VAL_U64s_MAX)

/* Btree pointers don't carry around checksums: */
#define BKEY_BTREE_PTR_VAL_U64s_MAX				\
	((sizeof(struct bch_extent_ptr)) / sizeof(u64) * BCH_REPLICAS_MAX)
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
} __attribute__((packed, aligned(8)));

struct bch_inode_generation {
	struct bch_val		v;

	__le32			bi_generation;
	__le32			pad;
} __attribute__((packed, aligned(8)));

#define BCH_INODE_FIELDS()			\
	x(bi_atime,			64)	\
	x(bi_ctime,			64)	\
	x(bi_mtime,			64)	\
	x(bi_otime,			64)	\
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
	x(bi_fields_set,		16)

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
	__BCH_INODE_SYNC	= 0,
	__BCH_INODE_IMMUTABLE	= 1,
	__BCH_INODE_APPEND	= 2,
	__BCH_INODE_NODUMP	= 3,
	__BCH_INODE_NOATIME	= 4,

	__BCH_INODE_I_SIZE_DIRTY= 5,
	__BCH_INODE_I_SECTORS_DIRTY= 6,
	__BCH_INODE_UNLINKED	= 7,

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

LE32_BITMASK(INODE_STR_HASH,	struct bch_inode, bi_flags, 20, 24);
LE32_BITMASK(INODE_NR_FIELDS,	struct bch_inode, bi_flags, 24, 32);

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
	__le64			d_inum;

	/*
	 * Copy of mode bits 12-15 from the target inode - so userspace can get
	 * the filetype without having to do a stat()
	 */
	__u8			d_type;

	__u8			d_name[];
} __attribute__((packed, aligned(8)));

#define BCH_NAME_MAX	(U8_MAX * sizeof(u64) -				\
			 sizeof(struct bkey) -				\
			 offsetof(struct bch_dirent, d_name))


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
} __attribute__((packed, aligned(8)));

/* Bucket/allocation information: */

struct bch_alloc {
	struct bch_val		v;
	__u8			fields;
	__u8			gen;
	__u8			data[];
} __attribute__((packed, aligned(8)));

#define BCH_ALLOC_FIELDS()			\
	x(read_time,		16)		\
	x(write_time,		16)		\
	x(data_type,		8)		\
	x(dirty_sectors,	16)		\
	x(cached_sectors,	16)		\
	x(oldest_gen,		8)

enum {
#define x(name, bytes) BCH_ALLOC_FIELD_##name,
	BCH_ALLOC_FIELDS()
#undef x
	BCH_ALLOC_FIELD_NR
};

static const unsigned BCH_ALLOC_FIELD_BYTES[] = {
#define x(name, bits) [BCH_ALLOC_FIELD_##name] = bits / 8,
	BCH_ALLOC_FIELDS()
#undef x
};

#define x(name, bits) + (bits / 8)
static const unsigned BKEY_ALLOC_VAL_U64s_MAX =
	DIV_ROUND_UP(offsetof(struct bch_alloc, data)
		     BCH_ALLOC_FIELDS(), sizeof(u64));
#undef x

#define BKEY_ALLOC_U64s_MAX	(BKEY_U64s + BKEY_ALLOC_VAL_U64s_MAX)

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
} __attribute__((packed, aligned(8)));

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

	struct bch_extent_ptr	ptrs[0];
} __attribute__((packed, aligned(8)));

/* Reflink: */

struct bch_reflink_p {
	struct bch_val		v;
	__le64			idx;

	__le32			reservation_generation;
	__u8			nr_replicas;
	__u8			pad[3];
};

struct bch_reflink_v {
	struct bch_val		v;
	__le64			refcount;
	union bch_extent_entry	start[0];
	__u64			_data[0];
};

/* Optional/variable size superblock sections: */

struct bch_sb_field {
	__u64			_data[0];
	__le32			u64s;
	__le32			type;
};

#define BCH_SB_FIELDS()		\
	x(journal,	0)	\
	x(members,	1)	\
	x(crypt,	2)	\
	x(replicas_v0,	3)	\
	x(quota,	4)	\
	x(disk_groups,	5)	\
	x(clean,	6)	\
	x(replicas,	7)	\
	x(journal_seq_blacklist, 8)

enum bch_sb_field_type {
#define x(f, nr)	BCH_SB_FIELD_##f = nr,
	BCH_SB_FIELDS()
#undef x
	BCH_SB_FIELD_NR
};

/* BCH_SB_FIELD_journal: */

struct bch_sb_field_journal {
	struct bch_sb_field	field;
	__le64			buckets[0];
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
/* 4-10 unused, was TIER, HAS_(META)DATA */
LE64_BITMASK(BCH_MEMBER_REPLACEMENT,	struct bch_member, flags[0], 10, 14)
LE64_BITMASK(BCH_MEMBER_DISCARD,	struct bch_member, flags[0], 14, 15)
LE64_BITMASK(BCH_MEMBER_DATA_ALLOWED,	struct bch_member, flags[0], 15, 20)
LE64_BITMASK(BCH_MEMBER_GROUP,		struct bch_member, flags[0], 20, 28)
LE64_BITMASK(BCH_MEMBER_DURABILITY,	struct bch_member, flags[0], 28, 30)

#define BCH_TIER_MAX			4U

#if 0
LE64_BITMASK(BCH_MEMBER_NR_READ_ERRORS,	struct bch_member, flags[1], 0,  20);
LE64_BITMASK(BCH_MEMBER_NR_WRITE_ERRORS,struct bch_member, flags[1], 20, 40);
#endif

enum bch_member_state {
	BCH_MEMBER_STATE_RW		= 0,
	BCH_MEMBER_STATE_RO		= 1,
	BCH_MEMBER_STATE_FAILED		= 2,
	BCH_MEMBER_STATE_SPARE		= 3,
	BCH_MEMBER_STATE_NR		= 4,
};

enum cache_replacement {
	CACHE_REPLACEMENT_LRU		= 0,
	CACHE_REPLACEMENT_FIFO		= 1,
	CACHE_REPLACEMENT_RANDOM	= 2,
	CACHE_REPLACEMENT_NR		= 3,
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

enum bch_data_type {
	BCH_DATA_NONE		= 0,
	BCH_DATA_SB		= 1,
	BCH_DATA_JOURNAL	= 2,
	BCH_DATA_BTREE		= 3,
	BCH_DATA_USER		= 4,
	BCH_DATA_CACHED		= 5,
	BCH_DATA_NR		= 6,
};

struct bch_replicas_entry_v0 {
	__u8			data_type;
	__u8			nr_devs;
	__u8			devs[];
} __attribute__((packed));

struct bch_sb_field_replicas_v0 {
	struct bch_sb_field	field;
	struct bch_replicas_entry_v0 entries[];
} __attribute__((packed, aligned(8)));

struct bch_replicas_entry {
	__u8			data_type;
	__u8			nr_devs;
	__u8			nr_required;
	__u8			devs[];
} __attribute__((packed));

struct bch_sb_field_replicas {
	struct bch_sb_field	field;
	struct bch_replicas_entry entries[];
} __attribute__((packed, aligned(8)));

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
} __attribute__((packed, aligned(8)));

/* BCH_SB_FIELD_disk_groups: */

#define BCH_SB_LABEL_SIZE		32

struct bch_disk_group {
	__u8			label[BCH_SB_LABEL_SIZE];
	__le64			flags[2];
} __attribute__((packed, aligned(8)));

LE64_BITMASK(BCH_GROUP_DELETED,		struct bch_disk_group, flags[0], 0,  1)
LE64_BITMASK(BCH_GROUP_DATA_ALLOWED,	struct bch_disk_group, flags[0], 1,  6)
LE64_BITMASK(BCH_GROUP_PARENT,		struct bch_disk_group, flags[0], 6, 24)

struct bch_sb_field_disk_groups {
	struct bch_sb_field	field;
	struct bch_disk_group	entries[0];
} __attribute__((packed, aligned(8)));

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
	__le16			read_clock;
	__le16			write_clock;
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

enum bcachefs_metadata_version {
	bcachefs_metadata_version_min			= 9,
	bcachefs_metadata_version_new_versioning	= 10,
	bcachefs_metadata_version_bkey_renumber		= 10,
	bcachefs_metadata_version_max			= 11,
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
} __attribute__((packed, aligned(8)));

#define BCH_SB_LAYOUT_SECTOR	7

/*
 * @offset	- sector where this sb was written
 * @version	- on disk format version
 * @version_min	- Oldest metadata version this filesystem contains; so we can
 *		  safely drop compatibility code and refuse to mount filesystems
 *		  we'd need it for
 * @magic	- identifies as a bcachefs superblock (BCACHE_MAGIC)
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
} __attribute__((packed, aligned(8)));

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

/* 61-64 unused */

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

/* Features: */
enum bch_sb_features {
	BCH_FEATURE_LZ4			= 0,
	BCH_FEATURE_GZIP		= 1,
	BCH_FEATURE_ZSTD		= 2,
	BCH_FEATURE_ATOMIC_NLINK	= 3, /* should have gone under compat */
	BCH_FEATURE_EC			= 4,
	BCH_FEATURE_JOURNAL_SEQ_BLACKLIST_V3 = 5,
	BCH_FEATURE_REFLINK		= 6,
	BCH_FEATURE_NEW_SIPHASH		= 7,
	BCH_FEATURE_NR,
};

enum bch_sb_compat {
	BCH_COMPAT_FEAT_ALLOC_INFO	= 0,
	BCH_COMPAT_FEAT_ALLOC_METADATA	= 1,
};

/* options: */

#define BCH_REPLICAS_MAX		4U

enum bch_error_actions {
	BCH_ON_ERROR_CONTINUE		= 0,
	BCH_ON_ERROR_RO			= 1,
	BCH_ON_ERROR_PANIC		= 2,
	BCH_NR_ERROR_ACTIONS		= 3,
};

enum bch_csum_opts {
	BCH_CSUM_OPT_NONE		= 0,
	BCH_CSUM_OPT_CRC32C		= 1,
	BCH_CSUM_OPT_CRC64		= 2,
	BCH_CSUM_OPT_NR			= 3,
};

enum bch_str_hash_type {
	BCH_STR_HASH_CRC32C		= 0,
	BCH_STR_HASH_CRC64		= 1,
	BCH_STR_HASH_SIPHASH_OLD	= 2,
	BCH_STR_HASH_SIPHASH		= 3,
	BCH_STR_HASH_NR			= 4,
};

enum bch_str_hash_opts {
	BCH_STR_HASH_OPT_CRC32C		= 0,
	BCH_STR_HASH_OPT_CRC64		= 1,
	BCH_STR_HASH_OPT_SIPHASH	= 2,
	BCH_STR_HASH_OPT_NR		= 3,
};

#define BCH_COMPRESSION_TYPES()		\
	x(NONE)				\
	x(LZ4)				\
	x(GZIP)				\
	x(ZSTD)

enum bch_compression_opts {
#define x(t) BCH_COMPRESSION_OPT_##t,
	BCH_COMPRESSION_TYPES()
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
	x(data_usage,		6)

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

enum {
	FS_USAGE_RESERVED		= 0,
	FS_USAGE_INODES			= 1,
	FS_USAGE_KEY_VERSION		= 2,
	FS_USAGE_NR			= 3
};

struct jset_entry_usage {
	struct jset_entry	entry;
	__le64			v;
} __attribute__((packed));

struct jset_entry_data_usage {
	struct jset_entry	entry;
	__le64			v;
	struct bch_replicas_entry r;
} __attribute__((packed));

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

	__le16			read_clock;
	__le16			write_clock;

	/* Sequence number of oldest dirty journal entry */
	__le64			last_seq;


	union {
		struct jset_entry start[0];
		__u64		_data[0];
	};
} __attribute__((packed, aligned(8)));

LE32_BITMASK(JSET_CSUM_TYPE,	struct jset, flags, 0, 4);
LE32_BITMASK(JSET_BIG_ENDIAN,	struct jset, flags, 4, 5);

#define BCH_JOURNAL_BUCKETS_MIN		8

/* Btree: */

#define BCH_BTREE_IDS()				\
	x(EXTENTS,	0, "extents")			\
	x(INODES,	1, "inodes")			\
	x(DIRENTS,	2, "dirents")			\
	x(XATTRS,	3, "xattrs")			\
	x(ALLOC,	4, "alloc")			\
	x(QUOTAS,	5, "quotas")			\
	x(EC,		6, "erasure_coding")		\
	x(REFLINK,	7, "reflink")

enum btree_id {
#define x(kwd, val, name) BTREE_ID_##kwd = val,
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
} __attribute__((packed, aligned(8)));

LE32_BITMASK(BSET_CSUM_TYPE,	struct bset, flags, 0, 4);

LE32_BITMASK(BSET_BIG_ENDIAN,	struct bset, flags, 4, 5);
LE32_BITMASK(BSET_SEPARATE_WHITEOUTS,
				struct bset, flags, 5, 6);

struct btree_node {
	struct bch_csum		csum;
	__le64			magic;

	/* this flags field is encrypted, unlike bset->flags: */
	__le64			flags;

	/* Closed interval: */
	struct bpos		min_key;
	struct bpos		max_key;
	struct bch_extent_ptr	ptr;
	struct bkey_format	format;

	union {
	struct bset		keys;
	struct {
		__u8		pad[22];
		__le16		u64s;
		__u64		_data[0];

	};
	};
} __attribute__((packed, aligned(8)));

LE64_BITMASK(BTREE_NODE_ID,	struct btree_node, flags,  0,  4);
LE64_BITMASK(BTREE_NODE_LEVEL,	struct btree_node, flags,  4,  8);
/* 8-32 unused */
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
} __attribute__((packed, aligned(8)));

#endif /* _BCACHEFS_FORMAT_H */
