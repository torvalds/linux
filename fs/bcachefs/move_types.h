/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_MOVE_TYPES_H
#define _BCACHEFS_MOVE_TYPES_H

struct bch_move_stats {
	enum bch_data_type	data_type;
	enum btree_id		btree_id;
	struct bpos		pos;
	struct list_head	list;
	char			name[32];

	atomic64_t		keys_moved;
	atomic64_t		keys_raced;
	atomic64_t		sectors_moved;
	atomic64_t		sectors_seen;
	atomic64_t		sectors_raced;
};

struct move_bucket_in_flight {
	struct bpos		bucket;
	u8			gen;
	atomic_t		count;
};

#endif /* _BCACHEFS_MOVE_TYPES_H */
