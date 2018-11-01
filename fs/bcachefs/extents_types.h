/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_EXTENTS_TYPES_H
#define _BCACHEFS_EXTENTS_TYPES_H

#include "bcachefs_format.h"

struct bch_extent_crc_unpacked {
	u8			csum_type;
	u8			compression_type;

	u16			compressed_size;
	u16			uncompressed_size;

	u16			offset;
	u16			live_size;

	u16			nonce;

	struct bch_csum		csum;
};

struct extent_ptr_decoded {
	struct bch_extent_crc_unpacked	crc;
	struct bch_extent_ptr		ptr;
};

struct bch_io_failures {
	u8			nr;
	struct bch_dev_io_failures {
		u8		dev;
		u8		nr_failed;
		u8		nr_retries;
	}			devs[BCH_REPLICAS_MAX];
};

#endif /* _BCACHEFS_EXTENTS_TYPES_H */
