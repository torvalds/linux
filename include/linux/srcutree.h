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

#include <linux/rcu_node_tree.h>
#include <linux/completion.h>

struct srcu_node;
struct srcu_struct;

/*
 * Per-CPU structure feeding into leaf srcu_node, similar in function
 * to rcu_node.
 */
struct srcu_data {
	/* Read-side state. */
	unsigned long srcu_lock_count[2];	/* Locks per CPU. */
	unsigned long srcu_unlock_count[2];	/* Unlocks per CPU. */

	/* Update-side state. */
	spinlock_t __private lock ____cacheline_internodealigned_in_smp;
	struct rcu_segcblist srcu_cblist;	/* List of callbacks.*/
	unsigned long srcu_gp_seq_needed;	/* Furthest future GP needed. */
	unsigned long srcu_gp_seq_needed_exp;	/* Furthest future exp GP. */
	bool srcu_cblist_invoking;		/* Invoking these CBs? */
	struct delayed_work work;		/* Context for CB invoking. */
	struct rcu_head srcu_barrier_head;	/* For srcu_barrier() use. */
	struct srcu_node *mynode;		/* Leaf srcu_node. */
	unsigned long grpmask;			/* Mask for leaf srcu_node */
						/*  ->srcu_data_have_cbs[]. */
	int cpu;
	struct srcu_struct *sp;
};

/*
 * Node in SRCU combining tree, similar in function to rcu_data.
 */
struct srcu_node {
	spinlock_t __private lock;
	unsigned long srcu_have_cbs[4];		/* GP seq for children */
						/*  having CBs, but only */
						/*  is > ->srcu_gq_seq. */
	unsigned long srcu_data_have_cbs[4];	/* Which srcu_data structs */
						/*  have CBs for given GP? */
	unsigned long srcu_gp_seq_needed_exp;	/* Furthest future exp GP. */
	struct srcu_node *srcu_parent;		/* Next up in tree. */
	int grplo;				/* Least CPU for node. */
	int grphi;				/* Biggest CPU for node. */
};

/*
 * Per-SRCU-domain structure, similar in function to rcu_state.
 */
struct srcu_struct {
	struct srcu_node node[NUM_RCU_NODES];	/* Combining tree. */
	struct srcu_node *level[RCU_NUM_LVLS + 1];
						/* First node at each level. */
	struct mutex srcu_cb_mutex;		/* Serialize CB preparation. */
	spinlock_t __private lock;		/* Protect counters */
	struct mutex srcu_gp_mutex;		/* Serialize GP work. */
	unsigned int srcu_idx;			/* Current rdr array element. */
	unsigned long srcu_gp_seq;		/* Grace-period seq #. */
	unsigned long srcu_gp_seq_needed;	/* Latest gp_seq needed. */
	unsigned long srcu_gp_seq_needed_exp;	/* Furthest future exp GP. */
	unsigned long srcu_last_gp_end;		/* Last GP end timestamp (ns) */
	struct srcu_data __percpu *sda;		/* Per-CPU srcu_data array. */
	unsigned long srcu_barrier_seq;		/* srcu_barrier seq #. */
	struct mutex srcu_barrier_mutex;	/* Serialize barrier ops. */
	struct completion srcu_barrier_completion;
						/* Awaken barrier rq at end. */
	atomic_t srcu_barrier_cpu_cnt;		/* # CPUs not yet posting a */
						/*  callback for the barrier */
						/*  operation. */
	struct delayed_work work;
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map dep_map;
#endif /* #ifdef CONFIG_DEBUG_LOCK_ALLOC */
};

/* Values for state variable (bottom bits of ->srcu_gp_seq). */
#define SRCU_STATE_IDLE		0
#define SRCU_STATE_SCAN1	1
#define SRCU_STATE_SCAN2	2

#define __SRCU_STRUCT_INIT(name, pcpu_name)				\
{									\
	.sda = &pcpu_name,						\
	.lock = __SPIN_LOCK_UNLOCKED(name.lock),			\
	.srcu_gp_seq_needed = -1UL,					\
	.work = __DELAYED_WORK_INITIALIZER(name.work, NULL, 0),		\
	__SRCU_DEP_MAP_INIT(name)					\
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
	static DEFINE_PER_CPU(struct srcu_data, name##_srcu_data);\
	is_static struct srcu_struct name = __SRCU_STRUCT_INIT(name, name##_srcu_data)
#define DEFINE_SRCU(name)		__DEFINE_SRCU(name, /* not static */)
#define DEFINE_STATIC_SRCU(name)	__DEFINE_SRCU(name, static)

void synchronize_srcu_expedited(struct srcu_struct *sp);
void srcu_barrier(struct srcu_struct *sp);
void srcu_torture_stats_print(struct srcu_struct *sp, char *tt, char *tf);

#endif
