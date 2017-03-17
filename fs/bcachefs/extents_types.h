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

struct extent_pick_ptr {
	struct bch_extent_ptr		ptr;
	struct bch_extent_crc_unpacked	crc;
};

#endif /* _BCACHEFS_EXTENTS_TYPES_H */
