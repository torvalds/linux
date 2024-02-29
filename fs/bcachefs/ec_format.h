/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_EC_FORMAT_H
#define _BCACHEFS_EC_FORMAT_H

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

#endif /* _BCACHEFS_EC_FORMAT_H */
