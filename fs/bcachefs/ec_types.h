/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_EC_TYPES_H
#define _BCACHEFS_EC_TYPES_H

#include "bcachefs_format.h"

struct bch_replicas_padded {
	struct bch_replicas_entry_v1	e;
	u8				pad[BCH_BKEY_PTRS_MAX];
};

struct stripe {
	size_t			heap_idx;
	u16			sectors;
	u8			algorithm;
	u8			nr_blocks;
	u8			nr_redundant;
	u8			blocks_nonempty;
	u8			disk_label;
};

struct gc_stripe {
	u16			sectors;

	u8			nr_blocks;
	u8			nr_redundant;

	unsigned		alive:1; /* does a corresponding key exist in stripes btree? */
	u16			block_sectors[BCH_BKEY_PTRS_MAX];
	struct bch_extent_ptr	ptrs[BCH_BKEY_PTRS_MAX];

	struct bch_replicas_padded r;
};

struct ec_stripe_heap_entry {
	size_t			idx;
	unsigned		blocks_nonempty;
};

typedef DEFINE_MIN_HEAP(struct ec_stripe_heap_entry, ec_stripes_heap) ec_stripes_heap;

#endif /* _BCACHEFS_EC_TYPES_H */
