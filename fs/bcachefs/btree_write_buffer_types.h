/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BTREE_WRITE_BUFFER_TYPES_H
#define _BCACHEFS_BTREE_WRITE_BUFFER_TYPES_H

#include "journal_types.h"

#define BTREE_WRITE_BUFERED_VAL_U64s_MAX	4
#define BTREE_WRITE_BUFERED_U64s_MAX	(BKEY_U64s + BTREE_WRITE_BUFERED_VAL_U64s_MAX)

struct btree_write_buffered_key {
	u64			journal_seq;
	unsigned		journal_offset;
	enum btree_id		btree;
	__BKEY_PADDED(k, BTREE_WRITE_BUFERED_VAL_U64s_MAX);
};

union btree_write_buffer_state {
	struct {
		atomic64_t	counter;
	};

	struct {
		u64		v;
	};

	struct {
		u64			nr:23;
		u64			idx:1;
		u64			ref0:20;
		u64			ref1:20;
	};
};

struct btree_write_buffer {
	struct mutex			flush_lock;
	struct journal_entry_pin	journal_pin;

	union btree_write_buffer_state	state;
	size_t				size;

	struct btree_write_buffered_key	*keys[2];
};

#endif /* _BCACHEFS_BTREE_WRITE_BUFFER_TYPES_H */
