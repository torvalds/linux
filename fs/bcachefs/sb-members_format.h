/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_SB_MEMBERS_FORMAT_H
#define _BCACHEFS_SB_MEMBERS_FORMAT_H

/*
 * We refer to members with bitmasks in various places - but we need to get rid
 * of this limit:
 */
#define BCH_SB_MEMBERS_MAX		64

/*
 * Sentinal value - indicates a device that does not exist
 */
#define BCH_SB_MEMBER_INVALID		255

#define BCH_MIN_NR_NBUCKETS	(1 << 6)

#define BCH_IOPS_MEASUREMENTS()			\
	x(seqread,	0)			\
	x(seqwrite,	1)			\
	x(randread,	2)			\
	x(randwrite,	3)

enum bch_iops_measurement {
#define x(t, n) BCH_IOPS_##t = n,
	BCH_IOPS_MEASUREMENTS()
#undef x
	BCH_IOPS_NR
};

#define BCH_MEMBER_ERROR_TYPES()		\
	x(read,		0)			\
	x(write,	1)			\
	x(checksum,	2)

enum bch_member_error_type {
#define x(t, n) BCH_MEMBER_ERROR_##t = n,
	BCH_MEMBER_ERROR_TYPES()
#undef x
	BCH_MEMBER_ERROR_NR
};

struct bch_member {
	__uuid_t		uuid;
	__le64			nbuckets;	/* device size */
	__le16			first_bucket;   /* index of first bucket used */
	__le16			bucket_size;	/* sectors */
	__u8			btree_bitmap_shift;
	__u8			pad[3];
	__le64			last_mount;	/* time_t */

	__le64			flags;
	__le32			iops[4];
	__le64			errors[BCH_MEMBER_ERROR_NR];
	__le64			errors_at_reset[BCH_MEMBER_ERROR_NR];
	__le64			errors_reset_time;
	__le64			seq;
	__le64			btree_allocated_bitmap;
	/*
	 * On recovery from a clean shutdown we don't normally read the journal,
	 * but we still want to resume writing from where we left off so we
	 * don't overwrite more than is necessary, for list journal debugging:
	 */
	__le32			last_journal_bucket;
	__le32			last_journal_bucket_offset;
};

/*
 * btree_allocated_bitmap can represent sector addresses of a u64: it itself has
 * 64 elements, so 64 - ilog2(64)
 */
#define BCH_MI_BTREE_BITMAP_SHIFT_MAX	58

/*
 * This limit comes from the bucket_gens array - it's a single allocation, and
 * kernel allocation are limited to INT_MAX
 */
#define BCH_MEMBER_NBUCKETS_MAX	(INT_MAX - 64)

#define BCH_MEMBER_V1_BYTES	56

LE64_BITMASK(BCH_MEMBER_STATE,		struct bch_member, flags,  0,  4)
/* 4-14 unused, was TIER, HAS_(META)DATA, REPLACEMENT */
LE64_BITMASK(BCH_MEMBER_DISCARD,	struct bch_member, flags, 14, 15)
LE64_BITMASK(BCH_MEMBER_DATA_ALLOWED,	struct bch_member, flags, 15, 20)
LE64_BITMASK(BCH_MEMBER_GROUP,		struct bch_member, flags, 20, 28)
LE64_BITMASK(BCH_MEMBER_DURABILITY,	struct bch_member, flags, 28, 30)
LE64_BITMASK(BCH_MEMBER_FREESPACE_INITIALIZED,
					struct bch_member, flags, 30, 31)

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

struct bch_sb_field_members_v1 {
	struct bch_sb_field	field;
	struct bch_member	_members[]; //Members are now variable size
};

struct bch_sb_field_members_v2 {
	struct bch_sb_field	field;
	__le16			member_bytes; //size of single member entry
	u8			pad[6];
	struct bch_member	_members[];
};

#endif /* _BCACHEFS_SB_MEMBERS_FORMAT_H */
