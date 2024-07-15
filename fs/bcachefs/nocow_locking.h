/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_NOCOW_LOCKING_H
#define _BCACHEFS_NOCOW_LOCKING_H

#include "bcachefs.h"
#include "alloc_background.h"
#include "nocow_locking_types.h"

#include <linux/hash.h>

static inline struct nocow_lock_bucket *bucket_nocow_lock(struct bucket_nocow_lock_table *t,
							  u64 dev_bucket)
{
	unsigned h = hash_64(dev_bucket, BUCKET_NOCOW_LOCKS_BITS);

	return t->l + (h & (BUCKET_NOCOW_LOCKS - 1));
}

#define BUCKET_NOCOW_LOCK_UPDATE	(1 << 0)

bool bch2_bucket_nocow_is_locked(struct bucket_nocow_lock_table *, struct bpos);
void bch2_bucket_nocow_unlock(struct bucket_nocow_lock_table *, struct bpos, int);
bool __bch2_bucket_nocow_trylock(struct nocow_lock_bucket *, u64, int);
void __bch2_bucket_nocow_lock(struct bucket_nocow_lock_table *,
			      struct nocow_lock_bucket *, u64, int);

static inline void bch2_bucket_nocow_lock(struct bucket_nocow_lock_table *t,
					  struct bpos bucket, int flags)
{
	u64 dev_bucket = bucket_to_u64(bucket);
	struct nocow_lock_bucket *l = bucket_nocow_lock(t, dev_bucket);

	__bch2_bucket_nocow_lock(t, l, dev_bucket, flags);
}

static inline bool bch2_bucket_nocow_trylock(struct bucket_nocow_lock_table *t,
					  struct bpos bucket, int flags)
{
	u64 dev_bucket = bucket_to_u64(bucket);
	struct nocow_lock_bucket *l = bucket_nocow_lock(t, dev_bucket);

	return __bch2_bucket_nocow_trylock(l, dev_bucket, flags);
}

void bch2_nocow_locks_to_text(struct printbuf *, struct bucket_nocow_lock_table *);

void bch2_fs_nocow_locking_exit(struct bch_fs *);
int bch2_fs_nocow_locking_init(struct bch_fs *);

#endif /* _BCACHEFS_NOCOW_LOCKING_H */
