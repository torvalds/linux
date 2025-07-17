/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BTREE_JOURNAL_ITER_TYPES_H
#define _BCACHEFS_BTREE_JOURNAL_ITER_TYPES_H

struct journal_key_range_overwritten {
	size_t			start, end;
};

struct journal_key {
	u64			journal_seq;
	u32			journal_offset;
	enum btree_id		btree_id:8;
	unsigned		level:8;
	bool			allocated:1;
	bool			overwritten:1;
	bool			rewind:1;
	struct journal_key_range_overwritten __rcu *
				overwritten_range;
	struct bkey_i		*k;
};

struct journal_keys {
	/* must match layout in darray_types.h */
	size_t			nr, size;
	struct journal_key	*data;
	/*
	 * Gap buffer: instead of all the empty space in the array being at the
	 * end of the buffer - from @nr to @size - the empty space is at @gap.
	 * This means that sequential insertions are O(n) instead of O(n^2).
	 */
	size_t			gap;
	atomic_t		ref;
	bool			initial_ref_held;
	struct mutex		overwrite_lock;
};

#endif /* _BCACHEFS_BTREE_JOURNAL_ITER_TYPES_H */
