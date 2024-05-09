/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_REBALANCE_TYPES_H
#define _BCACHEFS_REBALANCE_TYPES_H

#include "move_types.h"

enum rebalance_state {
	REBALANCE_WAITING,
	REBALANCE_THROTTLED,
	REBALANCE_RUNNING,
};

struct bch_fs_rebalance {
	struct task_struct __rcu *thread;
	struct bch_pd_controller pd;

	atomic64_t		work_unknown_dev;

	enum rebalance_state	state;
	u64			throttled_until_iotime;
	unsigned long		throttled_until_cputime;

	unsigned		enabled:1;
};

#endif /* _BCACHEFS_REBALANCE_TYPES_H */
