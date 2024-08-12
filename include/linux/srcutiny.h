/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Sleepable Read-Copy Update mechanism for mutual exclusion,
 *	tiny variant.
 *
 * Copyright (C) IBM Corporation, 2017
 *
 * Author: Paul McKenney <paulmck@linux.ibm.com>
 */

#ifndef _LINUX_SRCU_TINY_H
#define _LINUX_SRCU_TINY_H

#include <linux/swait.h>

struct srcu_struct {
	short srcu_lock_nesting[2];	/* srcu_read_lock() nesting depth. */
	u8 srcu_gp_running;		/* GP workqueue running? */
	u8 srcu_gp_waiting;		/* GP waiting for readers? */
	unsigned long srcu_idx;		/* Current reader array element in bit 0x2. */
	unsigned long srcu_idx_max;	/* Furthest future srcu_idx request. */
	struct swait_queue_head srcu_wq;
					/* Last srcu_read_unlock() wakes GP. */
	struct rcu_head *srcu_cb_head;	/* Pending callbacks: Head. */
	struct rcu_head **srcu_cb_tail;	/* Pending callbacks: Tail. */
	struct work_struct srcu_work;	/* For driving grace periods. */
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map dep_map;
#endif /* #ifdef CONFIG_DEBUG_LOCK_ALLOC */
};

void srcu_drive_gp(struct work_struct *wp);

#define __SRCU_STRUCT_INIT(name, __ignored, ___ignored)			\
{									\
	.srcu_wq = __SWAIT_QUEUE_HEAD_INITIALIZER(name.srcu_wq),	\
	.srcu_cb_tail = &name.srcu_cb_head,				\
	.srcu_work = __WORK_INITIALIZER(name.srcu_work, srcu_drive_gp),	\
	__SRCU_DEP_MAP_INIT(name)					\
}

/*
 * This odd _STATIC_ arrangement is needed for API compatibility with
 * Tree SRCU, which needs some per-CPU data.
 */
#define DEFINE_SRCU(name) \
	struct srcu_struct name = __SRCU_STRUCT_INIT(name, name, name)
#define DEFINE_STATIC_SRCU(name) \
	static struct srcu_struct name = __SRCU_STRUCT_INIT(name, name, name)

// Dummy structure for srcu_notifier_head.
struct srcu_usage { };
#define __SRCU_USAGE_INIT(name) { }

void synchronize_srcu(struct srcu_struct *ssp);

/*
 * Counts the new reader in the appropriate per-CPU element of the
 * srcu_struct.  Can be invoked from irq/bh handlers, but the matching
 * __srcu_read_unlock() must be in the same handler instance.  Returns an
 * index that must be passed to the matching srcu_read_unlock().
 */
static inline int __srcu_read_lock(struct srcu_struct *ssp)
{
	int idx;

	preempt_disable();  // Needed for PREEMPT_AUTO
	idx = ((READ_ONCE(ssp->srcu_idx) + 1) & 0x2) >> 1;
	WRITE_ONCE(ssp->srcu_lock_nesting[idx], READ_ONCE(ssp->srcu_lock_nesting[idx]) + 1);
	preempt_enable();
	return idx;
}

static inline void synchronize_srcu_expedited(struct srcu_struct *ssp)
{
	synchronize_srcu(ssp);
}

static inline void srcu_barrier(struct srcu_struct *ssp)
{
	synchronize_srcu(ssp);
}

/* Defined here to avoid size increase for non-torture kernels. */
static inline void srcu_torture_stats_print(struct srcu_struct *ssp,
					    char *tt, char *tf)
{
	int idx;

	idx = ((data_race(READ_ONCE(ssp->srcu_idx)) + 1) & 0x2) >> 1;
	pr_alert("%s%s Tiny SRCU per-CPU(idx=%d): (%hd,%hd) gp: %lu->%lu\n",
		 tt, tf, idx,
		 data_race(READ_ONCE(ssp->srcu_lock_nesting[!idx])),
		 data_race(READ_ONCE(ssp->srcu_lock_nesting[idx])),
		 data_race(READ_ONCE(ssp->srcu_idx)),
		 data_race(READ_ONCE(ssp->srcu_idx_max)));
}

#endif
