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
#include <uapi/linux/magic.h>
#include "vstructs.h"

#ifdef __KERNEL__
typedef uuid_t __uuid_t;
#endif

#define BITMASK(name, type, field, offset, end)				\
static const __maybe_unused unsigned	name##_OFFSET = offset;		\
static const __maybe_unused unsigned	name##_BITS = (end - offset);	\
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
static const __maybe_unused unsigned	name##_OFFSET = offset;		\
static const __maybe_unused unsigned	name##_BITS = (end - offset);	\
static const __maybe_unused __u##_bits	name##_MAX = (1ULL << (end - offset)) - 1;\
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
} __packed
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
__aligned(4)
#endif
;

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
} __packed
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
__aligned(4)
#endif
;

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
} __packed
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
/*
 * The big-endian version of bkey can't be compiled by rustc with the "aligned"
 * attr since it doesn't allow types to have both "packed" and "aligned" attrs.
 * So for Rust compatibility, don't include this. It can be included in the LE
 * version because the "packed" attr is redundant in that case.
 *
 * History: (quoting Kent)
 *
 * Specifically, when i was designing bkey, I wanted the header to be no
 * bigger than necessary so that bkey_packed could use the rest. That means that
 * decently offten extent keys will fit into only 8 bytes, instead of spilling over
 * to 16.
 *
 * But packed_bkey treats the part after the header - the packed section -
 * as a single multi word, variable length integer. And bkey, the unpacked
 * version, is just a special case version of a bkey_packed; all the packed
 * bkey code will work on keys in any packed format, the in-memory
 * representation of an unpacked key also is just one type of packed key...
 *
 * So that constrains the key part of a bkig endian bkey to start right
 * after the header.
 *
 * If we ever do a bkey_v2 and need to expand the hedaer by another byte for
 * some reason - that will clean up this wart.
 */
__aligned(8)
#endif
;

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

typedef struct {
	__le64			lo;
	__le64			hi;
} bch_le128;

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

	struct bkey	k;
	struct bch_val	v;
};

#define POS_KEY(_pos)							\
((struct bkey) {							\
	.u64s		= BKEY_U64s,					\
	.format		= KEY_FORMAT_CURRENT,				\
	.p		= _pos,						\
})

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
	struct bkey_i key; __u64 key ## _pad[pad]

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
	x(inode_v3,		29)			\
	x(bucket_gens,		30)			\
	x(snapshot_tree,	31)			\
	x(logged_op_truncate,	32)			\
	x(logged_op_finsert,	33)			\
	x(accounting,		34)

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

/* 128 bits, sufficient for cryptographic MACs: */
struct bch_csum {
	__le64			lo;
	__le64			hi;
} __packed __aligned(8);

struct bch_backpointer {
	struct bch_val		v;
	__u8			btree_id;
	__u8			level;
	__u8			data_type;
	__u64			bucket_offset:40;
	__u32			bucket_len;
	struct bpos		pos;
} __packed __aligned(8);

/* Optional/variable size superblock sections: */

struct bch_sb_field {
	__u64			_data[0];
	__le32			u64s;
	__le32			type;
};

#define BCH_SB_FIELDS()				\
	x(journal,			0)	\
	x(members_v1,			1)	\
	x(crypt,			2)	\
	x(replicas_v0,			3)	\
	x(quota,			4)	\
	x(disk_groups,			5)	\
	x(clean,			6)	\
	x(replicas,			7)	\
	x(journal_seq_blacklist,	8)	\
	x(journal_v2,			9)	\
	x(counters,			10)	\
	x(members_v2,			11)	\
	x(errors,			12)	\
	x(ext,				13)	\
	x(downgrade,			14)

#include "alloc_background_format.h"
#include "dirent_format.h"
#include "disk_accounting_format.h"
#include "disk_groups_format.h"
#include "extents_format.h"
#include "ec_format.h"
#include "dirent_format.h"
#include "disk_groups_format.h"
#include "inode_format.h"
#include "journal_seq_blacklist_format.h"
#include "logged_ops_format.h"
#include "lru_format.h"
#include "quota_format.h"
#include "reflink_format.h"
#include "replicas_format.h"
#include "snapshot_format.h"
#include "subvolume_format.h"
#include "sb-counters_format.h"
#include "sb-downgrade_format.h"
#include "sb-errors_format.h"
#include "sb-members_format.h"
#include "xattr_format.h"

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
	__le64			buckets[];
};

struct bch_sb_field_journal_v2 {
	struct bch_sb_field	field;

	struct bch_sb_field_journal_v2_entry {
		__le64		start;
		__le64		nr;
	}			d[];
};

/* BCH_SB_FIELD_crypt: */

struct nonce {
	__le32			d[4];
};

struct bch_key {
	__le64			key[4];
};

#define BCH_KEY_MAGIC					\
	(((__u64) 'b' <<  0)|((__u64) 'c' <<  8)|		\
	 ((__u64) 'h' << 16)|((__u64) '*' << 24)|		\
	 ((__u64) '*' << 32)|((__u64) 'k' << 40)|		\
	 ((__u64) 'e' << 48)|((__u64) 'y' << 56))

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

	struct bkey_i		start[0];
	__u64			_data[];
};

struct bch_sb_field_clean {
	struct bch_sb_field	field;

	__le32			flags;
	__le16			_read_clock; /* no longer used */
	__le16			_write_clock;
	__le64			journal_seq;

	struct jset_entry	start[0];
	__u64			_data[];
};

struct bch_sb_field_ext {
	struct bch_sb_field	field;
	__le64			recovery_passes_required[2];
	__le64			errors_silent[8];
	__le64			btrees_lost_data;
};

/* Superblock: */

/*
 * New versioning scheme:
 * One common version number for all on disk data structures - superblock, btree
 * nodes, journal entries
 */
#define BCH_VERSION_MAJOR(_v)		((__u16) ((_v) >> 10))
#define BCH_VERSION_MINOR(_v)		((__u16) ((_v) & ~(~0U << 10)))
#define BCH_VERSION(_major, _minor)	(((_major) << 10)|(_minor) << 0)

/*
 * field 1:		version name
 * field 2:		BCH_VERSION(major, minor)
 * field 3:		recovery passess required on upgrade
 */
#define BCH_METADATA_VERSIONS()						\
	x(bkey_renumber,		BCH_VERSION(0, 10))		\
	x(inode_btree_change,		BCH_VERSION(0, 11))		\
	x(snapshot,			BCH_VERSION(0, 12))		\
	x(inode_backpointers,		BCH_VERSION(0, 13))		\
	x(btree_ptr_sectors_written,	BCH_VERSION(0, 14))		\
	x(snapshot_2,			BCH_VERSION(0, 15))		\
	x(reflink_p_fix,		BCH_VERSION(0, 16))		\
	x(subvol_dirent,		BCH_VERSION(0, 17))		\
	x(inode_v2,			BCH_VERSION(0, 18))		\
	x(freespace,			BCH_VERSION(0, 19))		\
	x(alloc_v4,			BCH_VERSION(0, 20))		\
	x(new_data_types,		BCH_VERSION(0, 21))		\
	x(backpointers,			BCH_VERSION(0, 22))		\
	x(inode_v3,			BCH_VERSION(0, 23))		\
	x(unwritten_extents,		BCH_VERSION(0, 24))		\
	x(bucket_gens,			BCH_VERSION(0, 25))		\
	x(lru_v2,			BCH_VERSION(0, 26))		\
	x(fragmentation_lru,		BCH_VERSION(0, 27))		\
	x(no_bps_in_alloc_keys,		BCH_VERSION(0, 28))		\
	x(snapshot_trees,		BCH_VERSION(0, 29))		\
	x(major_minor,			BCH_VERSION(1,  0))		\
	x(snapshot_skiplists,		BCH_VERSION(1,  1))		\
	x(deleted_inodes,		BCH_VERSION(1,  2))		\
	x(rebalance_work,		BCH_VERSION(1,  3))		\
	x(member_seq,			BCH_VERSION(1,  4))		\
	x(subvolume_fs_parent,		BCH_VERSION(1,  5))		\
	x(btree_subvolume_children,	BCH_VERSION(1,  6))		\
	x(mi_btree_bitmap,		BCH_VERSION(1,  7))		\
	x(bucket_stripe_sectors,	BCH_VERSION(1,  8))		\
	x(disk_accounting_v2,		BCH_VERSION(1,  9))

enum bcachefs_metadata_version {
	bcachefs_metadata_version_min = 9,
#define x(t, n)	bcachefs_metadata_version_##t = n,
	BCH_METADATA_VERSIONS()
#undef x
	bcachefs_metadata_version_max
};

static const __maybe_unused
unsigned bcachefs_metadata_required_upgrade_below = bcachefs_metadata_version_rebalance_work;

#define bcachefs_metadata_version_current	(bcachefs_metadata_version_max - 1)

#define BCH_SB_SECTOR			8

#define BCH_SB_LAYOUT_SIZE_BITS_MAX	16 /* 32 MB */

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

	__le64			flags[7];
	__le64			write_time;
	__le64			features[2];
	__le64			compat[2];

	struct bch_sb_layout	layout;

	struct bch_sb_field	start[0];
	__le64			_data[];
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
LE64_BITMASK(BCH_SB_COMPRESSION_TYPE_LO,struct bch_sb, flags[1],  4,  8);
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

LE64_BITMASK(BCH_SB_BACKGROUND_COMPRESSION_TYPE_LO,
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
LE64_BITMASK(BCH_SB_NOCOW,		struct bch_sb, flags[4], 33, 34);
LE64_BITMASK(BCH_SB_WRITE_BUFFER_SIZE,	struct bch_sb, flags[4], 34, 54);
LE64_BITMASK(BCH_SB_VERSION_UPGRADE,	struct bch_sb, flags[4], 54, 56);

LE64_BITMASK(BCH_SB_COMPRESSION_TYPE_HI,struct bch_sb, flags[4], 56, 60);
LE64_BITMASK(BCH_SB_BACKGROUND_COMPRESSION_TYPE_HI,
					struct bch_sb, flags[4], 60, 64);

LE64_BITMASK(BCH_SB_VERSION_UPGRADE_COMPLETE,
					struct bch_sb, flags[5],  0, 16);
LE64_BITMASK(BCH_SB_ALLOCATOR_STUCK_TIMEOUT,
					struct bch_sb, flags[5], 16, 32);

static inline __u64 BCH_SB_COMPRESSION_TYPE(const struct bch_sb *sb)
{
	return BCH_SB_COMPRESSION_TYPE_LO(sb) | (BCH_SB_COMPRESSION_TYPE_HI(sb) << 4);
}

static inline void SET_BCH_SB_COMPRESSION_TYPE(struct bch_sb *sb, __u64 v)
{
	SET_BCH_SB_COMPRESSION_TYPE_LO(sb, v);
	SET_BCH_SB_COMPRESSION_TYPE_HI(sb, v >> 4);
}

static inline __u64 BCH_SB_BACKGROUND_COMPRESSION_TYPE(const struct bch_sb *sb)
{
	return BCH_SB_BACKGROUND_COMPRESSION_TYPE_LO(sb) |
		(BCH_SB_BACKGROUND_COMPRESSION_TYPE_HI(sb) << 4);
}

static inline void SET_BCH_SB_BACKGROUND_COMPRESSION_TYPE(struct bch_sb *sb, __u64 v)
{
	SET_BCH_SB_BACKGROUND_COMPRESSION_TYPE_LO(sb, v);
	SET_BCH_SB_BACKGROUND_COMPRESSION_TYPE_HI(sb, v >> 4);
}

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

#define BCH_VERSION_UPGRADE_OPTS()	\
	x(compatible,		0)	\
	x(incompatible,		1)	\
	x(none,			2)

enum bch_version_upgrade_opts {
#define x(t, n) BCH_VERSION_UPGRADE_##t = n,
	BCH_VERSION_UPGRADE_OPTS()
#undef x
};

#define BCH_REPLICAS_MAX		4U

#define BCH_BKEY_PTRS_MAX		16U

#define BCH_ERROR_ACTIONS()		\
	x(continue,		0)	\
	x(fix_safe,		1)	\
	x(panic,		2)	\
	x(ro,			3)

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

static const __maybe_unused unsigned bch_crc_bytes[] = {
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

#define BCACHEFS_STATFS_MAGIC		BCACHEFS_SUPER_MAGIC

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
	x(overwrite,		10)		\
	x(write_buffer_keys,	11)		\
	x(datetime,		12)

enum bch_jset_entry_type {
#define x(f, nr)	BCH_JSET_ENTRY_##f	= nr,
	BCH_JSET_ENTRY_TYPES()
#undef x
	BCH_JSET_ENTRY_NR
};

static inline bool jset_entry_is_key(struct jset_entry *e)
{
	switch (e->type) {
	case BCH_JSET_ENTRY_btree_keys:
	case BCH_JSET_ENTRY_btree_root:
	case BCH_JSET_ENTRY_write_buffer_keys:
		return true;
	}

	return false;
}

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

enum bch_fs_usage_type {
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
	struct bch_replicas_entry_v1 r;
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

	__le64			_buckets_ec;		/* No longer used */
	__le64			_buckets_unavailable;	/* No longer used */

	struct jset_entry_dev_usage_type d[];
};

static inline unsigned jset_entry_dev_usage_nr_types(struct jset_entry_dev_usage *u)
{
	return (vstruct_bytes(&u->entry) - sizeof(struct jset_entry_dev_usage)) /
		sizeof(struct jset_entry_dev_usage_type);
}

struct jset_entry_log {
	struct jset_entry	entry;
	u8			d[];
} __packed __aligned(8);

struct jset_entry_datetime {
	struct jset_entry	entry;
	__le64			seconds;
} __packed __aligned(8);

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


	struct jset_entry	start[0];
	__u64			_data[];
} __packed __aligned(8);

LE32_BITMASK(JSET_CSUM_TYPE,	struct jset, flags, 0, 4);
LE32_BITMASK(JSET_BIG_ENDIAN,	struct jset, flags, 4, 5);
LE32_BITMASK(JSET_NO_FLUSH,	struct jset, flags, 5, 6);

#define BCH_JOURNAL_BUCKETS_MIN		8

/* Btree: */

enum btree_id_flags {
	BTREE_ID_EXTENTS	= BIT(0),
	BTREE_ID_SNAPSHOTS	= BIT(1),
	BTREE_ID_SNAPSHOT_FIELD	= BIT(2),
	BTREE_ID_DATA		= BIT(3),
};

#define BCH_BTREE_IDS()								\
	x(extents,		0,	BTREE_ID_EXTENTS|BTREE_ID_SNAPSHOTS|BTREE_ID_DATA,\
	  BIT_ULL(KEY_TYPE_whiteout)|						\
	  BIT_ULL(KEY_TYPE_error)|						\
	  BIT_ULL(KEY_TYPE_cookie)|						\
	  BIT_ULL(KEY_TYPE_extent)|						\
	  BIT_ULL(KEY_TYPE_reservation)|					\
	  BIT_ULL(KEY_TYPE_reflink_p)|						\
	  BIT_ULL(KEY_TYPE_inline_data))					\
	x(inodes,		1,	BTREE_ID_SNAPSHOTS,			\
	  BIT_ULL(KEY_TYPE_whiteout)|						\
	  BIT_ULL(KEY_TYPE_inode)|						\
	  BIT_ULL(KEY_TYPE_inode_v2)|						\
	  BIT_ULL(KEY_TYPE_inode_v3)|						\
	  BIT_ULL(KEY_TYPE_inode_generation))					\
	x(dirents,		2,	BTREE_ID_SNAPSHOTS,			\
	  BIT_ULL(KEY_TYPE_whiteout)|						\
	  BIT_ULL(KEY_TYPE_hash_whiteout)|					\
	  BIT_ULL(KEY_TYPE_dirent))						\
	x(xattrs,		3,	BTREE_ID_SNAPSHOTS,			\
	  BIT_ULL(KEY_TYPE_whiteout)|						\
	  BIT_ULL(KEY_TYPE_cookie)|						\
	  BIT_ULL(KEY_TYPE_hash_whiteout)|					\
	  BIT_ULL(KEY_TYPE_xattr))						\
	x(alloc,		4,	0,					\
	  BIT_ULL(KEY_TYPE_alloc)|						\
	  BIT_ULL(KEY_TYPE_alloc_v2)|						\
	  BIT_ULL(KEY_TYPE_alloc_v3)|						\
	  BIT_ULL(KEY_TYPE_alloc_v4))						\
	x(quotas,		5,	0,					\
	  BIT_ULL(KEY_TYPE_quota))						\
	x(stripes,		6,	0,					\
	  BIT_ULL(KEY_TYPE_stripe))						\
	x(reflink,		7,	BTREE_ID_EXTENTS|BTREE_ID_DATA,		\
	  BIT_ULL(KEY_TYPE_reflink_v)|						\
	  BIT_ULL(KEY_TYPE_indirect_inline_data)|				\
	  BIT_ULL(KEY_TYPE_error))						\
	x(subvolumes,		8,	0,					\
	  BIT_ULL(KEY_TYPE_subvolume))						\
	x(snapshots,		9,	0,					\
	  BIT_ULL(KEY_TYPE_snapshot))						\
	x(lru,			10,	0,					\
	  BIT_ULL(KEY_TYPE_set))						\
	x(freespace,		11,	BTREE_ID_EXTENTS,			\
	  BIT_ULL(KEY_TYPE_set))						\
	x(need_discard,		12,	0,					\
	  BIT_ULL(KEY_TYPE_set))						\
	x(backpointers,		13,	0,					\
	  BIT_ULL(KEY_TYPE_backpointer))					\
	x(bucket_gens,		14,	0,					\
	  BIT_ULL(KEY_TYPE_bucket_gens))					\
	x(snapshot_trees,	15,	0,					\
	  BIT_ULL(KEY_TYPE_snapshot_tree))					\
	x(deleted_inodes,	16,	BTREE_ID_SNAPSHOT_FIELD,		\
	  BIT_ULL(KEY_TYPE_set))						\
	x(logged_ops,		17,	0,					\
	  BIT_ULL(KEY_TYPE_logged_op_truncate)|					\
	  BIT_ULL(KEY_TYPE_logged_op_finsert))					\
	x(rebalance_work,	18,	BTREE_ID_SNAPSHOT_FIELD,		\
	  BIT_ULL(KEY_TYPE_set)|BIT_ULL(KEY_TYPE_cookie))			\
	x(subvolume_children,	19,	0,					\
	  BIT_ULL(KEY_TYPE_set))						\
	x(accounting,		20,	BTREE_ID_SNAPSHOT_FIELD,		\
	  BIT_ULL(KEY_TYPE_accounting))						\

enum btree_id {
#define x(name, nr, ...) BTREE_ID_##name = nr,
	BCH_BTREE_IDS()
#undef x
	BTREE_ID_NR
};

/*
 * Maximum number of btrees that we will _ever_ have under the current scheme,
 * where we refer to them with 64 bit bitfields - and we also need a bit for
 * the interior btree node type:
 */
#define BTREE_ID_NR_MAX		63

static inline bool btree_id_is_alloc(enum btree_id id)
{
	switch (id) {
	case BTREE_ID_alloc:
	case BTREE_ID_backpointers:
	case BTREE_ID_need_discard:
	case BTREE_ID_freespace:
	case BTREE_ID_bucket_gens:
		return true;
	default:
		return false;
	}
}

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

	struct bkey_packed	start[0];
	__u64			_data[];
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

LE64_BITMASK(BTREE_NODE_ID_LO,	struct btree_node, flags,  0,  4);
LE64_BITMASK(BTREE_NODE_LEVEL,	struct btree_node, flags,  4,  8);
LE64_BITMASK(BTREE_NODE_NEW_EXTENT_OVERWRITE,
				struct btree_node, flags,  8,  9);
LE64_BITMASK(BTREE_NODE_ID_HI,	struct btree_node, flags,  9, 25);
/* 25-32 unused */
LE64_BITMASK(BTREE_NODE_SEQ,	struct btree_node, flags, 32, 64);

static inline __u64 BTREE_NODE_ID(struct btree_node *n)
{
	return BTREE_NODE_ID_LO(n) | (BTREE_NODE_ID_HI(n) << 4);
}

static inline void SET_BTREE_NODE_ID(struct btree_node *n, __u64 v)
{
	SET_BTREE_NODE_ID_LO(n, v);
	SET_BTREE_NODE_ID_HI(n, v >> 4);
}

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
