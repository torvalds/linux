/*
 * Sleepable Read-Copy Update mechanism for mutual exclusion,
 *	classic v4.11 variant.
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

#ifndef _LINUX_SRCU_CLASSIC_H
#define _LINUX_SRCU_CLASSIC_H

struct srcu_array {
	unsigned long lock_count[2];
	unsigned long unlock_count[2];
};

struct rcu_batch {
	struct rcu_head *head, **tail;
};

#define RCU_BATCH_INIT(name) { NULL, &(name.head) }

struct srcu_struct {
	unsigned long completed;
	struct srcu_array __percpu *per_cpu_ref;
	spinlock_t queue_lock; /* protect ->batch_queue, ->running */
	bool running;
	/* callbacks just queued */
	struct rcu_batch batch_queue;
	/* callbacks try to do the first check_zero */
	struct rcu_batch batch_check0;
	/* callbacks done with the first check_zero and the flip */
	struct rcu_batch batch_check1;
	struct rcu_batch batch_done;
	struct delayed_work work;
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map dep_map;
#endif /* #ifdef CONFIG_DEBUG_LOCK_ALLOC */
};

void process_srcu(struct work_struct *work);

#define __SRCU_STRUCT_INIT(name)					\
	{								\
		.completed = -300,					\
		.per_cpu_ref = &name##_srcu_array,			\
		.queue_lock = __SPIN_LOCK_UNLOCKED(name.queue_lock),	\
		.running = false,					\
		.batch_queue = RCU_BATCH_INIT(name.batch_queue),	\
		.batch_check0 = RCU_BATCH_INIT(name.batch_check0),	\
		.batch_check1 = RCU_BATCH_INIT(name.batch_check1),	\
		.batch_done = RCU_BATCH_INIT(name.batch_done),		\
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

static inline void srcutorture_get_gp_data(enum rcutorture_type test_type,
					   struct srcu_struct *sp, int *flags,
					   unsigned long *gpnum,
					   unsigned long *completed)
{
	if (test_type != SRCU_FLAVOR)
		return;
	*flags = 0;
	*completed = sp->completed;
	*gpnum = *completed;
	if (sp->batch_queue.head || sp->batch_check0.head || sp->batch_check0.head)
		(*gpnum)++;
}

#endif
