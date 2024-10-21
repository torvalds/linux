/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_REFLINK_FORMAT_H
#define _BCACHEFS_REFLINK_FORMAT_H

struct bch_reflink_p {
	struct bch_val		v;
	__le64			idx_flags;
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

LE64_BITMASK(REFLINK_P_IDX,	struct bch_reflink_p, idx_flags,  0, 56);
LE64_BITMASK(REFLINK_P_ERROR,	struct bch_reflink_p, idx_flags, 56, 57);

struct bch_reflink_v {
	struct bch_val		v;
	__le64			refcount;
	union bch_extent_entry	start[0];
	__u64			_data[];
} __packed __aligned(8);

struct bch_indirect_inline_data {
	struct bch_val		v;
	__le64			refcount;
	u8			data[];
};

#endif /* _BCACHEFS_REFLINK_FORMAT_H */
