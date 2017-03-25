/*
 * Sleepable Read-Copy Update mechanism for mutual exclusion,
 *	tree variant.
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

#ifndef _LINUX_SRCU_TREE_H
#define _LINUX_SRCU_TREE_H

struct srcu_array {
	unsigned long lock_count[2];
	unsigned long unlock_count[2];
};

struct srcu_struct {
	unsigned long completed;
	unsigned long srcu_gp_seq;
	atomic_t srcu_exp_cnt;
	struct srcu_array __percpu *per_cpu_ref;
	spinlock_t queue_lock; /* protect ->srcu_cblist */
	struct rcu_segcblist srcu_cblist;
	struct delayed_work work;
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map dep_map;
#endif /* #ifdef CONFIG_DEBUG_LOCK_ALLOC */
};

/* Values for -> state variable. */
#define SRCU_STATE_IDLE		0
#define SRCU_STATE_SCAN1	1
#define SRCU_STATE_SCAN2	2

void process_srcu(struct work_struct *work);

#define __SRCU_STRUCT_INIT(name)					\
	{								\
		.completed = -300,					\
		.per_cpu_ref = &name##_srcu_array,			\
		.queue_lock = __SPIN_LOCK_UNLOCKED(name.queue_lock),	\
		.srcu_cblist = RCU_SEGCBLIST_INITIALIZER(name.srcu_cblist),\
		.work = __DELAYED_WORK_INITIALIZER(name.work, process_srcu, 0),\
		__SRCU_DEP_MAP_INIT(name)				\
	}

/*
 * Define and initialize a srcu struct at build time.
 * Do -not- call init_srcu_struct() nor cleanup_srcu_struct() on it.
 *
 * Note that although DEFINE_STATIC_SRCU() hides the name from other
 * files, the per-CPU variable rules nevertheless require that the
 * chosen name be globally unique.  These rules also prohibit use of
 * DEFINE_STATIC_SRCU() within a function.  If these rules are too
 * restrictive, declare the srcu_struct manually.  For example, in
 * each file:
 *
 *	static struct srcu_struct my_srcu;
 *
 * Then, before the first use of each my_srcu, manually initialize it:
 *
 *	init_srcu_struct(&my_srcu);
 *
 * See include/linux/percpu-defs.h for the rules on per-CPU variables.
 */
#define __DEFINE_SRCU(name, is_static)					\
	static DEFINE_PER_CPU(struct srcu_array, name##_srcu_array);\
	is_static struct srcu_struct name = __SRCU_STRUCT_INIT(name)
#define DEFINE_SRCU(name)		__DEFINE_SRCU(name, /* not static */)
#define DEFINE_STATIC_SRCU(name)	__DEFINE_SRCU(name, static)

void synchronize_srcu_expedited(struct srcu_struct *sp);
void srcu_barrier(struct srcu_struct *sp);
unsigned long srcu_batches_completed(struct srcu_struct *sp);

#endif
