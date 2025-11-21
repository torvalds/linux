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

/* One element of the srcu_data srcu_ctrs array. */
struct srcu_ctr {
	atomic_long_t srcu_locks;	/* Locks per CPU. */
	atomic_long_t srcu_unlocks;	/* Unlocks per CPU. */
};

/*
 * Per-CPU structure feeding into leaf srcu_node, similar in function
 * to rcu_node.
 */
struct srcu_data {
	/* Read-side state. */
	struct srcu_ctr srcu_ctrs[2];		/* Locks and unlocks per CPU. */
	int srcu_reader_flavor;			/* Reader flavor for srcu_struct structure? */
						/* Values: SRCU_READ_FLAVOR_.*  */

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
 * Per-SRCU-domain structure, update-side data linked from srcu_struct.
 */
struct srcu_usage {
	struct srcu_node *node;			/* Combining tree. */
	struct srcu_node *level[RCU_NUM_LVLS + 1];
						/* First node at each level. */
	int srcu_size_state;			/* Small-to-big transition state. */
	struct mutex srcu_cb_mutex;		/* Serialize CB preparation. */
	spinlock_t __private lock;		/* Protect counters and size state. */
	struct mutex srcu_gp_mutex;		/* Serialize GP work. */
	unsigned long srcu_gp_seq;		/* Grace-period seq #. */
	unsigned long srcu_gp_seq_needed;	/* Latest gp_seq needed. */
	unsigned long srcu_gp_seq_needed_exp;	/* Furthest future exp GP. */
	unsigned long srcu_gp_start;		/* Last GP start timestamp (jiffies) */
	unsigned long srcu_last_gp_end;		/* Last GP end timestamp (ns) */
	unsigned long srcu_size_jiffies;	/* Current contention-measurement interval. */
	unsigned long srcu_n_lock_retries;	/* Contention events in current interval. */
	unsigned long srcu_n_exp_nodelay;	/* # expedited no-delays in current GP phase. */
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
	struct srcu_struct *srcu_ssp;
};

/*
 * Per-SRCU-domain structure, similar in function to rcu_state.
 */
struct srcu_struct {
	struct srcu_ctr __percpu *srcu_ctrp;
	struct srcu_data __percpu *sda;		/* Per-CPU srcu_data array. */
	struct lockdep_map dep_map;
	struct srcu_usage *srcu_sup;		/* Update-side data. */
};

// Values for size state variable (->srcu_size_state).  Once the state
// has been set to SRCU_SIZE_ALLOC, the grace-period code advances through
// this state machine one step per grace period until the SRCU_SIZE_BIG state
// is reached.  Otherwise, the state machine remains in the SRCU_SIZE_SMALL
// state indefinitely.
#define SRCU_SIZE_SMALL		0	// No srcu_node combining tree, ->node == NULL
#define SRCU_SIZE_ALLOC		1	// An srcu_node tree is being allocated, initialized,
					//  and then referenced by ->node.  It will not be used.
#define SRCU_SIZE_WAIT_BARRIER	2	// The srcu_node tree starts being used by everything
					//  except call_srcu(), especially by srcu_barrier().
					//  By the end of this state, all CPUs and threads
					//  are aware of this tree's existence.
#define SRCU_SIZE_WAIT_CALL	3	// The srcu_node tree starts being used by call_srcu().
					//  By the end of this state, all of the call_srcu()
					//  invocations that were running on a non-boot CPU
					//  and using the boot CPU's callback queue will have
					//  completed.
#define SRCU_SIZE_WAIT_CBS1	4	// Don't trust the ->srcu_have_cbs[] grace-period
#define SRCU_SIZE_WAIT_CBS2	5	//  sequence elements or the ->srcu_data_have_cbs[]
#define SRCU_SIZE_WAIT_CBS3	6	//  CPU-bitmask elements until all four elements of
#define SRCU_SIZE_WAIT_CBS4	7	//  each array have been initialized.
#define SRCU_SIZE_BIG		8	// The srcu_node combining tree is fully initialized
					//  and all aspects of it are being put to use.

/* Values for state variable (bottom bits of ->srcu_gp_seq). */
#define SRCU_STATE_IDLE		0
#define SRCU_STATE_SCAN1	1
#define SRCU_STATE_SCAN2	2

/*
 * Values for initializing gp sequence fields. Higher values allow wrap arounds to
 * occur earlier.
 * The second value with state is useful in the case of static initialization of
 * srcu_usage where srcu_gp_seq_needed is expected to have some state value in its
 * lower bits (or else it will appear to be already initialized within
 * the call check_init_srcu_struct()).
 */
#define SRCU_GP_SEQ_INITIAL_VAL ((0UL - 100UL) << RCU_SEQ_CTR_SHIFT)
#define SRCU_GP_SEQ_INITIAL_VAL_WITH_STATE (SRCU_GP_SEQ_INITIAL_VAL - 1)

#define __SRCU_USAGE_INIT(name)									\
{												\
	.lock = __SPIN_LOCK_UNLOCKED(name.lock),						\
	.srcu_gp_seq = SRCU_GP_SEQ_INITIAL_VAL,							\
	.srcu_gp_seq_needed = SRCU_GP_SEQ_INITIAL_VAL_WITH_STATE,				\
	.srcu_gp_seq_needed_exp = SRCU_GP_SEQ_INITIAL_VAL,					\
	.work = __DELAYED_WORK_INITIALIZER(name.work, NULL, 0),					\
}

#define __SRCU_STRUCT_INIT_COMMON(name, usage_name)						\
	.srcu_sup = &usage_name,								\
	__SRCU_DEP_MAP_INIT(name)

#define __SRCU_STRUCT_INIT_MODULE(name, usage_name)						\
{												\
	__SRCU_STRUCT_INIT_COMMON(name, usage_name)						\
}

#define __SRCU_STRUCT_INIT(name, usage_name, pcpu_name)						\
{												\
	.sda = &pcpu_name,									\
	.srcu_ctrp = &pcpu_name.srcu_ctrs[0],							\
	__SRCU_STRUCT_INIT_COMMON(name, usage_name)						\
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
# define __DEFINE_SRCU(name, is_static)								\
	static struct srcu_usage name##_srcu_usage = __SRCU_USAGE_INIT(name##_srcu_usage);	\
	is_static struct srcu_struct name = __SRCU_STRUCT_INIT_MODULE(name, name##_srcu_usage);	\
	extern struct srcu_struct * const __srcu_struct_##name;					\
	struct srcu_struct * const __srcu_struct_##name						\
		__section("___srcu_struct_ptrs") = &name
#else
# define __DEFINE_SRCU(name, is_static)								\
	static DEFINE_PER_CPU(struct srcu_data, name##_srcu_data);				\
	static struct srcu_usage name##_srcu_usage = __SRCU_USAGE_INIT(name##_srcu_usage);	\
	is_static struct srcu_struct name =							\
		__SRCU_STRUCT_INIT(name, name##_srcu_usage, name##_srcu_data)
#endif
#define DEFINE_SRCU(name)		__DEFINE_SRCU(name, /* not static */)
#define DEFINE_STATIC_SRCU(name)	__DEFINE_SRCU(name, static)

int __srcu_read_lock(struct srcu_struct *ssp) __acquires(ssp);
void synchronize_srcu_expedited(struct srcu_struct *ssp);
void srcu_barrier(struct srcu_struct *ssp);
void srcu_torture_stats_print(struct srcu_struct *ssp, char *tt, char *tf);

// Converts a per-CPU pointer to an ->srcu_ctrs[] array element to that
// element's index.
static inline bool __srcu_ptr_to_ctr(struct srcu_struct *ssp, struct srcu_ctr __percpu *scpp)
{
	return scpp - &ssp->sda->srcu_ctrs[0];
}

// Converts an integer to a per-CPU pointer to the corresponding
// ->srcu_ctrs[] array element.
static inline struct srcu_ctr __percpu *__srcu_ctr_to_ptr(struct srcu_struct *ssp, int idx)
{
	return &ssp->sda->srcu_ctrs[idx];
}

/*
 * Counts the new reader in the appropriate per-CPU element of the
 * srcu_struct.  Returns a pointer that must be passed to the matching
 * srcu_read_unlock_fast().
 *
 * Note that both this_cpu_inc() and atomic_long_inc() are RCU read-side
 * critical sections either because they disables interrupts, because
 * they are a single instruction, or because they are read-modify-write
 * atomic operations, depending on the whims of the architecture.
 * This matters because the SRCU-fast grace-period mechanism uses either
 * synchronize_rcu() or synchronize_rcu_expedited(), that is, RCU,
 * *not* SRCU, in order to eliminate the need for the read-side smp_mb()
 * invocations that are used by srcu_read_lock() and srcu_read_unlock().
 * The __srcu_read_unlock_fast() function also relies on this same RCU
 * (again, *not* SRCU) trick to eliminate the need for smp_mb().
 *
 * The key point behind this RCU trick is that if any part of a given
 * RCU reader precedes the beginning of a given RCU grace period, then
 * the entirety of that RCU reader and everything preceding it happens
 * before the end of that same RCU grace period.  Similarly, if any part
 * of a given RCU reader follows the end of a given RCU grace period,
 * then the entirety of that RCU reader and everything following it
 * happens after the beginning of that same RCU grace period.  Therefore,
 * the operations labeled Y in __srcu_read_lock_fast() and those labeled Z
 * in __srcu_read_unlock_fast() are ordered against the corresponding SRCU
 * read-side critical section from the viewpoint of the SRCU grace period.
 * This is all the ordering that is required, hence no calls to smp_mb().
 *
 * This means that __srcu_read_lock_fast() is not all that fast
 * on architectures that support NMIs but do not supply NMI-safe
 * implementations of this_cpu_inc().
 */
static inline struct srcu_ctr __percpu notrace *__srcu_read_lock_fast(struct srcu_struct *ssp)
{
	struct srcu_ctr __percpu *scp = READ_ONCE(ssp->srcu_ctrp);

	if (!IS_ENABLED(CONFIG_NEED_SRCU_NMI_SAFE))
		this_cpu_inc(scp->srcu_locks.counter); // Y, and implicit RCU reader.
	else
		atomic_long_inc(raw_cpu_ptr(&scp->srcu_locks));  // Y, and implicit RCU reader.
	barrier(); /* Avoid leaking the critical section. */
	return scp;
}

/*
 * Removes the count for the old reader from the appropriate
 * per-CPU element of the srcu_struct.  Note that this may well be a
 * different CPU than that which was incremented by the corresponding
 * srcu_read_lock_fast(), but it must be within the same task.
 *
 * Please see the __srcu_read_lock_fast() function's header comment for
 * information on implicit RCU readers and NMI safety.
 */
static inline void notrace
__srcu_read_unlock_fast(struct srcu_struct *ssp, struct srcu_ctr __percpu *scp)
{
	barrier();  /* Avoid leaking the critical section. */
	if (!IS_ENABLED(CONFIG_NEED_SRCU_NMI_SAFE))
		this_cpu_inc(scp->srcu_unlocks.counter);  // Z, and implicit RCU reader.
	else
		atomic_long_inc(raw_cpu_ptr(&scp->srcu_unlocks));  // Z, and implicit RCU reader.
}

void __srcu_check_read_flavor(struct srcu_struct *ssp, int read_flavor);

// Record reader usage even for CONFIG_PROVE_RCU=n kernels.  This is
// needed only for flavors that require grace-period smp_mb() calls to be
// promoted to synchronize_rcu().
static inline void srcu_check_read_flavor_force(struct srcu_struct *ssp, int read_flavor)
{
	struct srcu_data *sdp = raw_cpu_ptr(ssp->sda);

	if (likely(READ_ONCE(sdp->srcu_reader_flavor) & read_flavor))
		return;

	// Note that the cmpxchg() in __srcu_check_read_flavor() is fully ordered.
	__srcu_check_read_flavor(ssp, read_flavor);
}

// Record non-_lite() usage only for CONFIG_PROVE_RCU=y kernels.
static inline void srcu_check_read_flavor(struct srcu_struct *ssp, int read_flavor)
{
	if (IS_ENABLED(CONFIG_PROVE_RCU))
		__srcu_check_read_flavor(ssp, read_flavor);
}

#endif
