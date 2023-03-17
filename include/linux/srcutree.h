/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Sleepable Read-Copy Update mechanism for mutual exclusion,
 *	tree variant.
 *
 * Copyright (C) IBM Corporation, 2017
 *
 * Author: Paul McKenney <paulmck@linux.ibm.com>
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
	atomic_long_t srcu_lock_count[2];	/* Locks per CPU. */
	atomic_long_t srcu_unlock_count[2];	/* Unlocks per CPU. */
	int srcu_nmi_safety;			/* NMI-safe srcu_struct structure? */

	/* Update-side state. */
	spinlock_t __private lock ____cacheline_internodealigned_in_smp;
	struct rcu_segcblist srcu_cblist;	/* List of callbacks.*/
	unsigned long srcu_gp_seq_needed;	/* Furthest future GP needed. */
	unsigned long srcu_gp_seq_needed_exp;	/* Furthest future exp GP. */
	bool srcu_cblist_invoking;		/* Invoking these CBs? */
	struct timer_list delay_work;		/* Delay for CB invoking */
	struct work_struct work;		/* Context for CB invoking. */
	struct rcu_head srcu_barrier_head;	/* For srcu_barrier() use. */
	struct srcu_node *mynode;		/* Leaf srcu_node. */
	unsigned long grpmask;			/* Mask for leaf srcu_node */
						/*  ->srcu_data_have_cbs[]. */
	int cpu;
	struct srcu_struct *ssp;
};

/*
 * Node in SRCU combining tree, similar in function to rcu_data.
 */
struct srcu_node {
	spinlock_t __private lock;
	unsigned long srcu_have_cbs[4];		/* GP seq for children having CBs, but only */
						/*  if greater than ->srcu_gp_seq. */
	unsigned long srcu_data_have_cbs[4];	/* Which srcu_data structs have CBs for given GP? */
	unsigned long srcu_gp_seq_needed_exp;	/* Furthest future exp GP. */
	struct srcu_node *srcu_parent;		/* Next up in tree. */
	int grplo;				/* Least CPU for node. */
	int grphi;				/* Biggest CPU for node. */
};

/*
 * Per-SRCU-domain structure, similar in function to rcu_state.
 */
struct srcu_struct {
	struct srcu_node *node;			/* Combining tree. */
	struct srcu_node *level[RCU_NUM_LVLS + 1];
						/* First node at each level. */
	int srcu_size_state;			/* Small-to-big transition state. */
	struct mutex srcu_cb_mutex;		/* Serialize CB preparation. */
	spinlock_t __private lock;		/* Protect counters and size state. */
	struct mutex srcu_gp_mutex;		/* Serialize GP work. */
	unsigned int srcu_idx;			/* Current rdr array element. */
	unsigned long srcu_gp_seq;		/* Grace-period seq #. */
	unsigned long srcu_gp_seq_needed;	/* Latest gp_seq needed. */
	unsigned long srcu_gp_seq_needed_exp;	/* Furthest future exp GP. */
	unsigned long srcu_gp_start;		/* Last GP start timestamp (jiffies) */
	unsigned long srcu_last_gp_end;		/* Last GP end timestamp (ns) */
	unsigned long srcu_size_jiffies;	/* Current contention-measurement interval. */
	unsigned long srcu_n_lock_retries;	/* Contention events in current interval. */
	unsigned long srcu_n_exp_nodelay;	/* # expedited no-delays in current GP phase. */
	struct srcu_data __percpu *sda;		/* Per-CPU srcu_data array. */
	bool sda_is_static;			/* May ->sda be passed to free_percpu()? */
	unsigned long srcu_barrier_seq;		/* srcu_barrier seq #. */
	struct mutex srcu_barrier_mutex;	/* Serialize barrier ops. */
	struct completion srcu_barrier_completion;
						/* Awaken barrier rq at end. */
	atomic_t srcu_barrier_cpu_cnt;		/* # CPUs not yet posting a */
						/*  callback for the barrier */
						/*  operation. */
	unsigned long reschedule_jiffies;
	unsigned long reschedule_count;
	struct delayed_work work;
	struct lockdep_map dep_map;
};

/* Values for size state variable (->srcu_size_state). */
#define SRCU_SIZE_SMALL		0
#define SRCU_SIZE_ALLOC		1
#define SRCU_SIZE_WAIT_BARRIER	2
#define SRCU_SIZE_WAIT_CALL	3
#define SRCU_SIZE_WAIT_CBS1	4
#define SRCU_SIZE_WAIT_CBS2	5
#define SRCU_SIZE_WAIT_CBS3	6
#define SRCU_SIZE_WAIT_CBS4	7
#define SRCU_SIZE_BIG		8

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
#ifdef MODULE
# define __DEFINE_SRCU(name, is_static)					\
	is_static struct srcu_struct name;				\
	extern struct srcu_struct * const __srcu_struct_##name;		\
	struct srcu_struct * const __srcu_struct_##name			\
		__section("___srcu_struct_ptrs") = &name
#else
# define __DEFINE_SRCU(name, is_static)					\
	static DEFINE_PER_CPU(struct srcu_data, name##_srcu_data);	\
	is_static struct srcu_struct name =				\
		__SRCU_STRUCT_INIT(name, name##_srcu_data)
#endif
#define DEFINE_SRCU(name)		__DEFINE_SRCU(name, /* not static */)
#define DEFINE_STATIC_SRCU(name)	__DEFINE_SRCU(name, static)

void synchronize_srcu_expedited(struct srcu_struct *ssp);
void srcu_barrier(struct srcu_struct *ssp);
void srcu_torture_stats_print(struct srcu_struct *ssp, char *tt, char *tf);

#endif
