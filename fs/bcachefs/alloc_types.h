/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_ALLOC_TYPES_H
#define _BCACHEFS_ALLOC_TYPES_H

#include <linux/mutex.h>
#include <linux/spinlock.h>

#include "clock_types.h"
#include "fifo.h"

struct ec_bucket_buf;

/* There's two of these clocks, one for reads and one for writes: */
struct bucket_clock {
	/*
	 * "now" in (read/write) IO time - incremented whenever we do X amount
	 * of reads or writes.
	 *
	 * Goes with the bucket read/write prios: when we read or write to a
	 * bucket we reset the bucket's prio to the current hand; thus hand -
	 * prio = time since bucket was last read/written.
	 *
	 * The units are some amount (bytes/sectors) of data read/written, and
	 * the units can change on the fly if we need to rescale to fit
	 * everything in a u16 - your only guarantee is that the units are
	 * consistent.
	 */
	u16			hand;
	u16			max_last_io;

	int			rw;

	struct io_timer		rescale;
	struct mutex		lock;
};

enum alloc_reserve {
	RESERVE_BTREE_MOVINGGC	= -2,
	RESERVE_BTREE		= -1,
	RESERVE_MOVINGGC	= 0,
	RESERVE_NONE		= 1,
	RESERVE_NR		= 2,
};

typedef FIFO(long)	alloc_fifo;

#define OPEN_BUCKETS_COUNT	1024

#define WRITE_POINT_HASH_NR	32
#define WRITE_POINT_MAX		32

typedef u16			open_bucket_idx_t;

struct open_bucket {
	spinlock_t		lock;
	atomic_t		pin;
	open_bucket_idx_t	freelist;

	/*
	 * When an open bucket has an ec_stripe attached, this is the index of
	 * the block in the stripe this open_bucket corresponds to:
	 */
	u8			ec_idx;
	u8			type;
	unsigned		valid:1;
	unsigned		on_partial_list:1;
	int			alloc_reserve:3;
	unsigned		sectors_free;
	struct bch_extent_ptr	ptr;
	struct ec_stripe_new	*ec;
};

#define OPEN_BUCKET_LIST_MAX	15

struct open_buckets {
	open_bucket_idx_t	nr;
	open_bucket_idx_t	v[OPEN_BUCKET_LIST_MAX];
};

struct dev_stripe_state {
	u64			next_alloc[BCH_SB_MEMBERS_MAX];
};

struct write_point {
	struct hlist_node	node;
	struct mutex		lock;
	u64			last_used;
	unsigned long		write_point;
	enum bch_data_type	type;

	/* calculated based on how many pointers we're actually going to use: */
	unsigned		sectors_free;

	struct open_buckets	ptrs;
	struct dev_stripe_state	stripe;
};

struct write_point_specifier {
	unsigned long		v;
};

struct alloc_heap_entry {
	size_t			bucket;
	size_t			nr;
	unsigned long		key;
};

typedef HEAP(struct alloc_heap_entry) alloc_heap;

#endif /* _BCACHEFS_ALLOC_TYPES_H */
