/*
 * Sleepable Read-Copy Update mechanism for mutual exclusion,
 *	tiny variant.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * Copyright (C) IBM Corporation, 2017
 *
 * Author: Paul McKenney <paulmck@us.ibm.com>
 */

#ifndef _LINUX_SRCU_TINY_H
#define _LINUX_SRCU_TINY_H

#include <linux/swait.h>

struct srcu_struct {
	int srcu_lock_nesting[2];	/* srcu_read_lock() nesting depth. */
	struct swait_queue_head srcu_wq;
					/* Last srcu_read_unlock() wakes GP. */
	unsigned long srcu_gp_seq;	/* GP seq # for callback tagging. */
	struct rcu_segcblist srcu_cblist;
					/* Pending SRCU callbacks. */
	int srcu_idx;			/* Current reader array element. */
	bool srcu_gp_running;		/* GP workqueue running? */
	bool srcu_gp_waiting;		/* GP waiting for readers? */
	struct work_struct srcu_work;	/* For driving grace periods. */
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map dep_map;
#endif /* #ifdef CONFIG_DEBUG_LOCK_ALLOC */
};

void srcu_drive_gp(struct work_struct *wp);

#define __SRCU_STRUCT_INIT(name)					\
{									\
	.srcu_wq = __SWAIT_QUEUE_HEAD_INITIALIZER(name.srcu_wq),	\
	.srcu_cblist = RCU_SEGCBLIST_INITIALIZER(name.srcu_cblist),	\
	.srcu_work = __WORK_INITIALIZER(name.srcu_work, srcu_drive_gp),	\
	__SRCU_DEP_MAP_INIT(name)					\
}

/*
 * This odd _STATIC_ arrangement is needed for API compatibility with
 * Tree SRCU, which needs some per-CPU data.
 */
#define DEFINE_SRCU(name) \
	struct srcu_struct name = __SRCU_STRUCT_INIT(name)
#define DEFINE_STATIC_SRCU(name) \
	static struct srcu_struct name = __SRCU_STRUCT_INIT(name)

void synchronize_srcu(struct srcu_struct *sp);

static inline void synchronize_srcu_expedited(struct srcu_struct *sp)
{
	synchronize_srcu(sp);
}

static inline void srcu_barrier(struct srcu_struct *sp)
{
	synchronize_srcu(sp);
}

static inline unsigned long srcu_batches_completed(struct srcu_struct *sp)
{
	return 0;
}

static inline void srcutorture_get_gp_data(enum rcutorture_type test_type,
					   struct srcu_struct *sp, int *flags,
					   unsigned long *gpnum,
					   unsigned long *completed)
{
	if (test_type != SRCU_FLAVOR)
		return;
	*flags = 0;
	*completed = sp->srcu_gp_seq;
	*gpnum = *completed;
}

#endif
