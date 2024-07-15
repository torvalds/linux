/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_MOVE_TYPES_H
#define _BCACHEFS_MOVE_TYPES_H

#include "bbpos_types.h"

struct bch_move_stats {
	enum bch_data_type	data_type;
	struct bbpos		pos;
	char			name[32];

	atomic64_t		keys_moved;
	atomic64_t		keys_raced;
	atomic64_t		sectors_seen;
	atomic64_t		sectors_moved;
	atomic64_t		sectors_raced;
};

struct move_bucket_key {
	struct bpos		bucket;
	u8			gen;
};

struct move_bucket {
	struct move_bucket_key	k;
	unsigned		sectors;
};

struct move_bucket_in_flight {
	struct move_bucket_in_flight *next;
	struct rhash_head	hash;
	struct move_bucket	bucket;
	atomic_t		count;
};

#endif /* _BCACHEFS_MOVE_TYPES_H */
