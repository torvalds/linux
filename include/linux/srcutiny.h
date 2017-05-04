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
	short srcu_lock_nesting[2];	/* srcu_read_lock() nesting depth. */
	short srcu_idx;			/* Current reader array element. */
	u8 srcu_gp_running;		/* GP workqueue running? */
	u8 srcu_gp_waiting;		/* GP waiting for readers? */
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

#define __SRCU_STRUCT_INIT(name)					\
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
	struct srcu_struct name = __SRCU_STRUCT_INIT(name)
#define DEFINE_STATIC_SRCU(name) \
	static struct srcu_struct name = __SRCU_STRUCT_INIT(name)

void synchronize_srcu(struct srcu_struct *sp);

/*
 * Counts the new reader in the appropriate per-CPU element of the
 * srcu_struct.  Can be invoked from irq/bh handlers, but the matching
 * __srcu_read_unlock() must be in the same handler instance.  Returns an
 * index that must be passed to the matching srcu_read_unlock().
 */
static inline int __srcu_read_lock(struct srcu_struct *sp)
{
	int idx;

	idx = READ_ONCE(sp->srcu_idx);
	WRITE_ONCE(sp->srcu_lock_nesting[idx], sp->srcu_lock_nesting[idx] + 1);
	return idx;
}

static inline void synchronize_srcu_expedited(struct srcu_struct *sp)
{
	synchronize_srcu(sp);
}

static inline void srcu_barrier(struct srcu_struct *sp)
{
	synchronize_srcu(sp);
}

#endif
