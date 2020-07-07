/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_EC_TYPES_H
#define _BCACHEFS_EC_TYPES_H

#include <linux/llist.h>

#define EC_STRIPE_MAX	16

struct bch_replicas_padded {
	struct bch_replicas_entry	e;
	u8				pad[EC_STRIPE_MAX];
};

struct stripe {
	size_t			heap_idx;

	u16			sectors;
	u8			algorithm;

	u8			nr_blocks;
	u8			nr_redundant;

	unsigned		alive:1;
	unsigned		dirty:1;
	unsigned		on_heap:1;
	u8			blocks_nonempty;
	u16			block_sectors[EC_STRIPE_MAX];

	struct bch_replicas_padded r;
};

struct ec_stripe_heap_entry {
	size_t			idx;
	unsigned		blocks_nonempty;
};

typedef HEAP(struct ec_stripe_heap_entry) ec_stripes_heap;

#endif /* _BCACHEFS_EC_TYPES_H */
