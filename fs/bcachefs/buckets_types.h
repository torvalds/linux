/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BUCKETS_TYPES_H
#define _BUCKETS_TYPES_H

#include "bcachefs_format.h"
#include "util.h"

#define BUCKET_JOURNAL_SEQ_BITS		16

struct bucket_mark {
	union {
	struct {
		atomic64_t	v;
	};

	struct {
		u8		gen;
		u8		data_type:3,
				gen_valid:1,
				owned_by_allocator:1,
				nouse:1,
				journal_seq_valid:1;
		u16		dirty_sectors;
		u16		cached_sectors;

		/*
		 * low bits of journal sequence number when this bucket was most
		 * recently modified: if journal_seq_valid is set, this bucket
		 * can't be reused until the journal sequence number written to
		 * disk is >= the bucket's journal sequence number:
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

	u16				io_time[2];
};

struct bucket_array {
	struct rcu_head		rcu;
	u16			first_bucket;
	size_t			nbuckets;
	struct bucket		b[];
};

struct bch_dev_usage {
	u64			buckets[BCH_DATA_NR];
	u64			buckets_alloc;
	u64			buckets_unavailable;

	/* _compressed_ sectors: */
	u64			sectors[BCH_DATA_NR];
	u64			sectors_fragmented;
};

struct bch_fs_usage {
	/* all fields are in units of 512 byte sectors: */
	/* _uncompressed_ sectors: */
	u64			online_reserved;
	u64			available_cache;

	struct {
		u64		data[BCH_DATA_NR];
		u64		persistent_reserved;
	}			s[BCH_REPLICAS_MAX];
};

/*
 * A reservation for space on disk:
 */
struct disk_reservation {
	u64		sectors;
	u32		gen;
	unsigned	nr_replicas;
};

struct copygc_heap_entry {
	u8			gen;
	u32			sectors;
	u64			offset;
};

typedef HEAP(struct copygc_heap_entry) copygc_heap;

#endif /* _BUCKETS_TYPES_H */
