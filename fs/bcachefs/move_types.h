/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_MOVE_TYPES_H
#define _BCACHEFS_MOVE_TYPES_H

#include "bbpos_types.h"
#include "bcachefs_ioctl.h"

struct bch_move_stats {
	char			name[32];
	bool			phys;
	enum bch_ioctl_data_event_ret	ret;

	union {
	struct {
		enum bch_data_type	data_type;
		struct bbpos		pos;
	};
	struct {
		unsigned		dev;
		u64			offset;
	};
	};

	atomic64_t		keys_moved;
	atomic64_t		keys_raced;
	atomic64_t		sectors_seen;
	atomic64_t		sectors_moved;
	atomic64_t		sectors_raced;
	atomic64_t		sectors_error_corrected;
	atomic64_t		sectors_error_uncorrected;
};

struct move_bucket_key {
	struct bpos		bucket;
	unsigned		gen;
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
