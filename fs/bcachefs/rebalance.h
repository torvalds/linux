/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_REBALANCE_H
#define _BCACHEFS_REBALANCE_H

#include "rebalance_types.h"

int bch2_set_rebalance_needs_scan(struct bch_fs *, u64 inum);
int bch2_set_fs_needs_rebalance(struct bch_fs *);

static inline void rebalance_wakeup(struct bch_fs *c)
{
	struct task_struct *p;

	rcu_read_lock();
	p = rcu_dereference(c->rebalance.thread);
	if (p)
		wake_up_process(p);
	rcu_read_unlock();
}

void bch2_rebalance_status_to_text(struct printbuf *, struct bch_fs *);

void bch2_rebalance_stop(struct bch_fs *);
int bch2_rebalance_start(struct bch_fs *);
void bch2_fs_rebalance_init(struct bch_fs *);

#endif /* _BCACHEFS_REBALANCE_H */
