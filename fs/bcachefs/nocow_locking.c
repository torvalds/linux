// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "nocow_locking.h"
#include "util.h"

void __bch2_bucket_nocow_lock(struct bucket_nocow_lock_table *t,
			      two_state_lock_t *l, int flags)
{
	struct bch_fs *c = container_of(t, struct bch_fs, nocow_locks);
	u64 start_time = local_clock();

	__bch2_two_state_lock(l, flags & BUCKET_NOCOW_LOCK_UPDATE);
	bch2_time_stats_update(&c->times[BCH_TIME_nocow_lock_contended], start_time);
}
