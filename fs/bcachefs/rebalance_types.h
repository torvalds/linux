/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_REBALANCE_TYPES_H
#define _BCACHEFS_REBALANCE_TYPES_H

#include "bbpos_types.h"
#include "move_types.h"

#define BCH_REBALANCE_STATES()		\
	x(waiting)			\
	x(working)			\
	x(scanning)

enum bch_rebalance_states {
#define x(t)	BCH_REBALANCE_##t,
	BCH_REBALANCE_STATES()
#undef x
};

struct bch_fs_rebalance {
	struct task_struct __rcu	*thread;
	struct bch_pd_controller pd;

	enum bch_rebalance_states	state;
	u64				wait_iotime_start;
	u64				wait_iotime_end;
	u64				wait_wallclock_start;

	struct bch_move_stats		work_stats;

	struct bbpos			scan_start;
	struct bbpos			scan_end;
	struct bch_move_stats		scan_stats;

	unsigned			enabled:1;
};

#endif /* _BCACHEFS_REBALANCE_TYPES_H */
