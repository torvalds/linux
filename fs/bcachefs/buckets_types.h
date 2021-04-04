/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BUCKETS_TYPES_H
#define _BUCKETS_TYPES_H

#include "bcachefs_format.h"
#include "util.h"

#define BUCKET_JOURNAL_SEQ_BITS		16

struct bucket_mark {
	union {
	atomic64_t	v;

	struct {
	u8		gen;
	u8		data_type:3,
			owned_by_allocator:1,
			journal_seq_valid:1,
			stripe:1;
	u16		dirty_sectors;
	u16		cached_sectors;

	/*
	 * low bits of journal sequence number when this bucket was most
	 * recently modified: if journal_seq_valid is set, this bucket can't be
	 * reused until the journal sequence number written to disk is >= the
	 * bucket's journal sequence number:
	 */
	u16		journal_seq;
	};
	};
};

struct bucket {
	union {
		struct bucket_mark	_mark;
		const struct bucket_mark mark;
	};

	u64				io_time[2];
	u8				oldest_gen;
	u8				gc_gen;
	unsigned			gen_valid:1;
	u8				stripe_redundancy;
	u32				stripe;
};

struct bucket_array {
	struct rcu_head		rcu;
	u16			first_bucket;
	size_t			nbuckets;
	struct bucket		b[];
};

struct bch_dev_usage {
	u64			buckets_ec;
	u64			buckets_unavailable;

	struct {
		u64		buckets;
		u64		sectors; /* _compressed_ sectors: */
		u64		fragmented;
	}			d[BCH_DATA_NR];
};

struct bch_fs_usage {
	/* all fields are in units of 512 byte sectors: */
	u64			hidden;
	u64			btree;
	u64			data;
	u64			cached;
	u64			reserved;
	u64			nr_inodes;

	/* XXX: add stats for compression ratio */
#if 0
	u64			uncompressed;
	u64			compressed;
#endif

	/* broken out: */

	u64			persistent_reserved[BCH_REPLICAS_MAX];
	u64			replicas[];
};

struct bch_fs_usage_online {
	u64			online_reserved;
	struct bch_fs_usage	u;
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

struct copygc_heap_entry {
	u8			dev;
	u8			gen;
	u8			replicas;
	u16			fragmentation;
	u32			sectors;
	u64			offset;
};

typedef HEAP(struct copygc_heap_entry) copygc_heap;

#endif /* _BUCKETS_TYPES_H */
