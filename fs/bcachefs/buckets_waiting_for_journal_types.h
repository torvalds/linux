/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BUCKETS_WAITING_FOR_JOURNAL_TYPES_H
#define _BUCKETS_WAITING_FOR_JOURNAL_TYPES_H

#include <linux/siphash.h>

struct bucket_hashed {
	u64			dev_bucket;
	u64			journal_seq;
};

struct buckets_waiting_for_journal_table {
	unsigned		bits;
	u64			hash_seeds[3];
	struct bucket_hashed	d[];
};

struct buckets_waiting_for_journal {
	struct mutex		lock;
	struct buckets_waiting_for_journal_table *t;
};

#endif /* _BUCKETS_WAITING_FOR_JOURNAL_TYPES_H */
