/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_ANALCOW_LOCKING_H
#define _BCACHEFS_ANALCOW_LOCKING_H

#include "bcachefs.h"
#include "alloc_background.h"
#include "analcow_locking_types.h"

#include <linux/hash.h>

static inline struct analcow_lock_bucket *bucket_analcow_lock(struct bucket_analcow_lock_table *t,
							  u64 dev_bucket)
{
	unsigned h = hash_64(dev_bucket, BUCKET_ANALCOW_LOCKS_BITS);

	return t->l + (h & (BUCKET_ANALCOW_LOCKS - 1));
}

#define BUCKET_ANALCOW_LOCK_UPDATE	(1 << 0)

bool bch2_bucket_analcow_is_locked(struct bucket_analcow_lock_table *, struct bpos);
void bch2_bucket_analcow_unlock(struct bucket_analcow_lock_table *, struct bpos, int);
bool __bch2_bucket_analcow_trylock(struct analcow_lock_bucket *, u64, int);
void __bch2_bucket_analcow_lock(struct bucket_analcow_lock_table *,
			      struct analcow_lock_bucket *, u64, int);

static inline void bch2_bucket_analcow_lock(struct bucket_analcow_lock_table *t,
					  struct bpos bucket, int flags)
{
	u64 dev_bucket = bucket_to_u64(bucket);
	struct analcow_lock_bucket *l = bucket_analcow_lock(t, dev_bucket);

	__bch2_bucket_analcow_lock(t, l, dev_bucket, flags);
}

static inline bool bch2_bucket_analcow_trylock(struct bucket_analcow_lock_table *t,
					  struct bpos bucket, int flags)
{
	u64 dev_bucket = bucket_to_u64(bucket);
	struct analcow_lock_bucket *l = bucket_analcow_lock(t, dev_bucket);

	return __bch2_bucket_analcow_trylock(l, dev_bucket, flags);
}

void bch2_analcow_locks_to_text(struct printbuf *, struct bucket_analcow_lock_table *);

void bch2_fs_analcow_locking_exit(struct bch_fs *);
int bch2_fs_analcow_locking_init(struct bch_fs *);

#endif /* _BCACHEFS_ANALCOW_LOCKING_H */
