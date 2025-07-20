/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BUCKETS_WAITING_FOR_JOURNAL_H
#define _BUCKETS_WAITING_FOR_JOURNAL_H

#include "buckets_waiting_for_journal_types.h"

u64 bch2_bucket_journal_seq_ready(struct buckets_waiting_for_journal *,
				  unsigned, u64);
int bch2_set_bucket_needs_journal_commit(struct buckets_waiting_for_journal *,
					 u64, unsigned, u64, u64);

void bch2_fs_buckets_waiting_for_journal_exit(struct bch_fs *);
int bch2_fs_buckets_waiting_for_journal_init(struct bch_fs *);

#endif /* _BUCKETS_WAITING_FOR_JOURNAL_H */
