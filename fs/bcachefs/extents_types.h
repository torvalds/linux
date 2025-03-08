/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_EXTENTS_TYPES_H
#define _BCACHEFS_EXTENTS_TYPES_H

#include "bcachefs_format.h"

struct bch_extent_crc_unpacked {
	u32			compressed_size;
	u32			uncompressed_size;
	u32			live_size;

	u8			csum_type;
	u8			compression_type;

	u16			offset;

	u16			nonce;

	struct bch_csum		csum;
};

struct extent_ptr_decoded {
	bool				has_ec;
	bool				do_ec_reconstruct;
	u8				crc_retry_nr;
	struct bch_extent_crc_unpacked	crc;
	struct bch_extent_ptr		ptr;
	struct bch_extent_stripe_ptr	ec;
};

struct bch_io_failures {
	u8			nr;
	struct bch_dev_io_failures {
		u8		dev;
		unsigned	failed_csum_nr:6,
				failed_io:1,
				failed_ec:1;
	}			devs[BCH_REPLICAS_MAX + 1];
};

#endif /* _BCACHEFS_EXTENTS_TYPES_H */
