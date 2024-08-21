/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BUCKETS_TYPES_H
#define _BUCKETS_TYPES_H

#include "bcachefs_format.h"
#include "util.h"

#define BUCKET_JOURNAL_SEQ_BITS		16

struct bucket {
	u8			lock;
	u8			gen_valid:1;
	u8			data_type:7;
	u8			gen;
	u8			stripe_redundancy;
	u32			stripe;
	u32			dirty_sectors;
	u32			cached_sectors;
	u32			stripe_sectors;
} __aligned(sizeof(long));

struct bucket_array {
	struct rcu_head		rcu;
	u16			first_bucket;
	size_t			nbuckets;
	size_t			nbuckets_minus_first;
	struct bucket		b[] __counted_by(nbuckets);
};

struct bucket_gens {
	struct rcu_head		rcu;
	u16			first_bucket;
	size_t			nbuckets;
	size_t			nbuckets_minus_first;
	u8			b[];
};

struct bch_dev_usage {
	struct bch_dev_usage_type {
		u64		buckets;
		u64		sectors; /* _compressed_ sectors: */
		/*
		 * XXX
		 * Why do we have this? Isn't it just buckets * bucket_size -
		 * sectors?
		 */
		u64		fragmented;
	}			d[BCH_DATA_NR];
};

struct bch_fs_usage_base {
	u64			hidden;
	u64			btree;
	u64			data;
	u64			cached;
	u64			reserved;
	u64			nr_inodes;
};

struct bch_fs_usage_short {
	u64			capacity;
	u64			used;
	u64			free;
	u64			nr_inodes;
};

/*
 * A reservation for space on disk:
 */
struct disk_reservation {
	u64			sectors;
	u32			gen;
	unsigned		nr_replicas;
};

#endif /* _BUCKETS_TYPES_H */
