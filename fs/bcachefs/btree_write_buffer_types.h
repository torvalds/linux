/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BTREE_WRITE_BUFFER_TYPES_H
#define _BCACHEFS_BTREE_WRITE_BUFFER_TYPES_H

#include "darray.h"
#include "journal_types.h"

#define BTREE_WRITE_BUFERED_VAL_U64s_MAX	4
#define BTREE_WRITE_BUFERED_U64s_MAX	(BKEY_U64s + BTREE_WRITE_BUFERED_VAL_U64s_MAX)

struct wb_key_ref {
union {
	struct {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
		unsigned			idx:24;
		u8				pos[sizeof(struct bpos)];
		enum btree_id			btree:8;
#else
		enum btree_id			btree:8;
		u8				pos[sizeof(struct bpos)];
		unsigned			idx:24;
#endif
	} __packed;
	struct {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
		u64 lo;
		u64 mi;
		u64 hi;
#else
		u64 hi;
		u64 mi;
		u64 lo;
#endif
	};
};
};

struct btree_write_buffered_key {
	enum btree_id			btree:8;
	u64				journal_seq:56;
	__BKEY_PADDED(k, BTREE_WRITE_BUFERED_VAL_U64s_MAX);
};

struct btree_write_buffer_keys {
	DARRAY(struct btree_write_buffered_key) keys;
	struct journal_entry_pin	pin;
	struct mutex			lock;
};

struct btree_write_buffer {
	DARRAY(struct wb_key_ref)	sorted;
	struct btree_write_buffer_keys	inc;
	struct btree_write_buffer_keys	flushing;
	struct work_struct		flush_work;
};

#endif /* _BCACHEFS_BTREE_WRITE_BUFFER_TYPES_H */
