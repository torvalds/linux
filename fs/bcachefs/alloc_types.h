/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_ALLOC_TYPES_H
#define _BCACHEFS_ALLOC_TYPES_H

#include <linux/mutex.h>
#include <linux/spinlock.h>

#include "clock_types.h"
#include "fifo.h"

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

/* There is one reserve for each type of btree, one for prios and gens
 * and one for moving GC */
enum alloc_reserve {
	RESERVE_ALLOC		= -1,
	RESERVE_BTREE		= 0,
	RESERVE_MOVINGGC	= 1,
	RESERVE_NONE		= 2,
	RESERVE_NR		= 3,
};

typedef FIFO(long)	alloc_fifo;

/* Enough for 16 cache devices, 2 tiers and some left over for pipelining */
#define OPEN_BUCKETS_COUNT	256
#define WRITE_POINT_COUNT	32

struct open_bucket {
	spinlock_t		lock;
	atomic_t		pin;
	u8			freelist;
	bool			valid;
	bool			on_partial_list;
	unsigned		sectors_free;
	struct bch_extent_ptr	ptr;
};

#define OPEN_BUCKET_LIST_MAX	15

struct open_buckets {
	u8			nr;
	u8			v[OPEN_BUCKET_LIST_MAX];
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
	u64			next_alloc[BCH_SB_MEMBERS_MAX];
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
