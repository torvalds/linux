/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_ALLOC_TYPES_H
#define _BCACHEFS_ALLOC_TYPES_H

#include <linux/mutex.h>
#include <linux/spinlock.h>

#include "clock_types.h"
#include "fifo.h"

struct bucket_alloc_state {
	u64	buckets_seen;
	u64	skipped_open;
	u64	skipped_need_journal_commit;
	u64	skipped_nocow;
	u64	skipped_nouse;
};

#define BCH_WATERMARKS()		\
	x(stripe)			\
	x(normal)			\
	x(copygc)			\
	x(btree)			\
	x(btree_copygc)			\
	x(reclaim)			\
	x(interior_updates)

enum bch_watermark {
#define x(name)	BCH_WATERMARK_##name,
	BCH_WATERMARKS()
#undef x
	BCH_WATERMARK_NR,
};

#define BCH_WATERMARK_BITS	3
#define BCH_WATERMARK_MASK	~(~0U << BCH_WATERMARK_BITS)

#define OPEN_BUCKETS_COUNT	1024

#define WRITE_POINT_HASH_NR	32
#define WRITE_POINT_MAX		32

/*
 * 0 is never a valid open_bucket_idx_t:
 */
typedef u16			open_bucket_idx_t;

struct open_bucket {
	spinlock_t		lock;
	atomic_t		pin;
	open_bucket_idx_t	freelist;
	open_bucket_idx_t	hash;

	/*
	 * When an open bucket has an ec_stripe attached, this is the index of
	 * the block in the stripe this open_bucket corresponds to:
	 */
	u8			ec_idx;
	enum bch_data_type	data_type:6;
	unsigned		valid:1;
	unsigned		on_partial_list:1;

	u8			dev;
	u8			gen;
	u32			sectors_free;
	u64			bucket;
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

#define WRITE_POINT_STATES()		\
	x(stopped)			\
	x(waiting_io)			\
	x(waiting_work)			\
	x(running)

enum write_point_state {
#define x(n)	WRITE_POINT_##n,
	WRITE_POINT_STATES()
#undef x
	WRITE_POINT_STATE_NR
};

struct write_point {
	struct {
		struct hlist_node	node;
		struct mutex		lock;
		u64			last_used;
		unsigned long		write_point;
		enum bch_data_type	data_type;

		/* calculated based on how many pointers we're actually going to use: */
		unsigned		sectors_free;

		struct open_buckets	ptrs;
		struct dev_stripe_state	stripe;

		u64			sectors_allocated;
	} __aligned(SMP_CACHE_BYTES);

	struct {
		struct work_struct	index_update_work;

		struct list_head	writes;
		spinlock_t		writes_lock;

		enum write_point_state	state;
		u64			last_state_change;
		u64			time[WRITE_POINT_STATE_NR];
	} __aligned(SMP_CACHE_BYTES);
};

struct write_point_specifier {
	unsigned long		v;
};

#endif /* _BCACHEFS_ALLOC_TYPES_H */
