// SPDX-License-Identifier: GPL-2.0+
/*
 * Sleepable Read-Copy Update mechanism for mutual exclusion.
 *
 * Copyright (C) IBM Corporation, 2006
 * Copyright (C) Fujitsu, 2012
 *
 * Authors: Paul McKenney <paulmck@linux.ibm.com>
 *	   Lai Jiangshan <laijs@cn.fujitsu.com>
 *
 * For detailed explanation of Read-Copy Update mechanism see -
 *		Documentation/RCU/ *.txt
 *
 */

#define pr_fmt(fmt) "rcu: " fmt

#include <linux/export.h>
#include <linux/mutex.h>
#include <linux/percpu.h>
#include <linux/preempt.h>
#include <linux/rcupdate_wait.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/srcu.h>

#include "rcu.h"
#include "rcu_segcblist.h"

/* Holdoff in nanoseconds for auto-expediting. */
#define DEFAULT_SRCU_EXP_HOLDOFF (25 * 1000)
static ulong exp_holdoff = DEFAULT_SRCU_EXP_HOLDOFF;
module_param(exp_holdoff, ulong, 0444);

/* Overflow-check frequency.  N bits roughly says every 2**N grace periods. */
static ulong counter_wrap_check = (ULONG_MAX >> 2);
module_param(counter_wrap_check, ulong, 0444);

/*
 * Control conversion to SRCU_SIZE_BIG:
 *    0: Don't convert at all.
 *    1: Convert at init_srcu_struct() time.
 *    2: Convert when rcutorture invokes srcu_torture_stats_print().
 *    3: Decide at boot time based on system shape (default).
 * 0x1x: Convert when excessive contention encountered.
 */
#define SRCU_SIZING_NONE	0
#define SRCU_SIZING_INIT	1
#define SRCU_SIZING_TORTURE	2
#define SRCU_SIZING_AUTO	3
#define SRCU_SIZING_CONTEND	0x10
#define SRCU_SIZING_IS(x) ((convert_to_big & ~SRCU_SIZING_CONTEND) == x)
#define SRCU_SIZING_IS_NONE() (SRCU_SIZING_IS(SRCU_SIZING_NONE))
#define SRCU_SIZING_IS_INIT() (SRCU_SIZING_IS(SRCU_SIZING_INIT))
#define SRCU_SIZING_IS_TORTURE() (SRCU_SIZING_IS(SRCU_SIZING_TORTURE))
#define SRCU_SIZING_IS_CONTEND() (convert_to_big & SRCU_SIZING_CONTEND)
static int convert_to_big = SRCU_SIZING_AUTO;
module_param(convert_to_big, int, 0444);

/* Number of CPUs to trigger init_srcu_struct()-time transition to big. */
static int big_cpu_lim __read_mostly = 128;
module_param(big_cpu_lim, int, 0444);

/* Contention events per jiffy to initiate transition to big. */
static int small_contention_lim __read_mostly = 100;
module_param(small_contention_lim, int, 0444);

/* Early-boot callback-management, so early that no lock is required! */
static LIST_HEAD(srcu_boot_list);
static bool __read_mostly srcu_init_done;

static void srcu_invoke_callbacks(struct work_struct *work);
static void srcu_reschedule(struct srcu_struct *ssp, unsigned long delay);
static void process_srcu(struct work_struct *work);
static void srcu_delay_timer(struct timer_list *t);

/* Wrappers for lock acquisition and release, see raw_spin_lock_rcu_node(). */
#define spin_lock_rcu_node(p)							\
do {										\
	spin_lock(&ACCESS_PRIVATE(p, lock));					\
	smp_mb__after_unlock_lock();						\
} while (0)

#define spin_unlock_rcu_node(p) spin_unlock(&ACCESS_PRIVATE(p, lock))

#define spin_lock_irq_rcu_node(p)						\
do {										\
	spin_lock_irq(&ACCESS_PRIVATE(p, lock));				\
	smp_mb__after_unlock_lock();						\
} while (0)

#define spin_unlock_irq_rcu_node(p)						\
	spin_unlock_irq(&ACCESS_PRIVATE(p, lock))

#define spin_lock_irqsave_rcu_node(p, flags)					\
do {										\
	spin_lock_irqsave(&ACCESS_PRIVATE(p, lock), flags);			\
	smp_mb__after_unlock_lock();						\
} while (0)

#define spin_trylock_irqsave_rcu_node(p, flags)					\
({										\
	bool ___locked = spin_trylock_irqsave(&ACCESS_PRIVATE(p, lock), flags); \
										\
	if (___locked)								\
		smp_mb__after_unlock_lock();					\
	___locked;								\
})

#define spin_unlock_irqrestore_rcu_node(p, flags)				\
	spin_unlock_irqrestore(&ACCESS_PRIVATE(p, lock), flags)			\

/*
 * Initialize SRCU per-CPU data.  Note that statically allocated
 * srcu_struct structures might already have srcu_read_lock() and
 * srcu_read_unlock() running against them.  So if the is_static parameter
 * is set, don't initialize ->srcu_lock_count[] and ->srcu_unlock_count[].
 */
static void init_srcu_struct_data(struct srcu_struct *ssp)
{
	int cpu;
	struct srcu_data *sdp;

	/*
	 * Initialize the per-CPU srcu_data array, which feeds into the
	 * leaves of the srcu_node tree.
	 */
	WARN_ON_ONCE(ARRAY_SIZE(sdp->srcu_lock_count) !=
		     ARRAY_SIZE(sdp->srcu_unlock_count));
	for_each_possible_cpu(cpu) {
		sdp = per_cpu_ptr(ssp->sda, cpu);
		spin_lock_init(&ACCESS_PRIVATE(sdp, lock));
		rcu_segcblist_init(&sdp->srcu_cblist);
		sdp->srcu_cblist_invoking = false;
		sdp->srcu_gp_seq_needed = ssp->srcu_sup->srcu_gp_seq;
		sdp->srcu_gp_seq_needed_exp = ssp->srcu_sup->srcu_gp_seq;
		sdp->mynode = NULL;
		sdp->cpu = cpu;
		INIT_WORK(&sdp->work, srcu_invoke_callbacks);
		timer_setup(&sdp->delay_work, srcu_delay_timer, 0);
		sdp->ssp = ssp;
	}
}

/* Invalid seq state, used during snp node initialization */
#define SRCU_SNP_INIT_SEQ		0x2

/*
 * Check whether sequence number corresponding to snp node,
 * is invalid.
 */
static inline bool srcu_invl_snp_seq(unsigned long s)
{
	return s == SRCU_SNP_INIT_SEQ;
}

/*
 * Allocated and initialize SRCU combining tree.  Returns @true if
 * allocation succeeded and @false otherwise.
 */
static bool init_srcu_struct_nodes(struct srcu_struct *ssp, gfp_t gfp_flags)
{
	int cpu;
	int i;
	int level = 0;
	int levelspread[RCU_NUM_LVLS];
	struct srcu_data *sdp;
	struct srcu_node *snp;
	struct srcu_node *snp_first;

	/* Initialize geometry if it has not already been initialized. */
	rcu_init_geometry();
	ssp->srcu_sup->node = kcalloc(rcu_num_nodes, sizeof(*ssp->srcu_sup->node), gfp_flags);
	if (!ssp->srcu_sup->node)
		return false;

	/* Work out the overall tree geometry. */
	ssp->srcu_sup->level[0] = &ssp->srcu_sup->node[0];
	for (i = 1; i < rcu_num_lvls; i++)
		ssp->srcu_sup->level[i] = ssp->srcu_sup->level[i - 1] + num_rcu_lvl[i - 1];
	rcu_init_levelspread(levelspread, num_rcu_lvl);

	/* Each pass through this loop initializes one srcu_node structure. */
	srcu_for_each_node_breadth_first(ssp, snp) {
		spin_lock_init(&ACCESS_PRIVATE(snp, lock));
		WARN_ON_ONCE(ARRAY_SIZE(snp->srcu_have_cbs) !=
			     ARRAY_SIZE(snp->srcu_data_have_cbs));
		for (i = 0; i < ARRAY_SIZE(snp->srcu_have_cbs); i++) {
			snp->srcu_have_cbs[i] = SRCU_SNP_INIT_SEQ;
			snp->srcu_data_have_cbs[i] = 0;
		}
		snp->srcu_gp_seq_needed_exp = SRCU_SNP_INIT_SEQ;
		snp->grplo = -1;
		snp->grphi = -1;
		if (snp == &ssp->srcu_sup->node[0]) {
			/* Root node, special case. */
			snp->srcu_parent = NULL;
			continue;
		}

		/* Non-root node. */
		if (snp == ssp->srcu_sup->level[level + 1])
			level++;
		snp->srcu_parent = ssp->srcu_sup->level[level - 1] +
				   (snp - ssp->srcu_sup->level[level]) /
				   levelspread[level - 1];
	}

	/*
	 * Initialize the per-CPU srcu_data array, which feeds into the
	 * leaves of the srcu_node tree.
	 */
	level = rcu_num_lvls - 1;
	snp_first = ssp->srcu_sup->level[level];
	for_each_possible_cpu(cpu) {
		sdp = per_cpu_ptr(ssp->sda, cpu);
		sdp->mynode = &snp_first[cpu / levelspread[level]];
		for (snp = sdp->mynode; snp != NULL; snp = snp->srcu_parent) {
			if (snp->grplo < 0)
				snp->grplo = cpu;
			snp->grphi = cpu;
		}
		sdp->grpmask = 1UL << (cpu - sdp->mynode->grplo);
	}
	smp_store_release(&ssp->srcu_sup->srcu_size_state, SRCU_SIZE_WAIT_BARRIER);
	return true;
}

/*
 * Initialize non-compile-time initialized fields, including the
 * associated srcu_node and srcu_data structures.  The is_static parameter
 * tells us that ->sda has already been wired up to srcu_data.
 */
static int init_srcu_struct_fields(struct srcu_struct *ssp, bool is_static)
{
	if (!is_static)
		ssp->srcu_sup = kzalloc(sizeof(*ssp->srcu_sup), GFP_KERNEL);
	if (!ssp->srcu_sup)
		return -ENOMEM;
	if (!is_static)
		spin_lock_init(&ACCESS_PRIVATE(ssp->srcu_sup, lock));
	ssp->srcu_sup->srcu_size_state = SRCU_SIZE_SMALL;
	ssp->srcu_sup->node = NULL;
	mutex_init(&ssp->srcu_sup->srcu_cb_mutex);
	mutex_init(&ssp->srcu_sup->srcu_gp_mutex);
	ssp->srcu_idx = 0;
	ssp->srcu_sup->srcu_gp_seq = 0;
	ssp->srcu_sup->srcu_barrier_seq = 0;
	mutex_init(&ssp->srcu_sup->srcu_barrier_mutex);
	atomic_set(&ssp->srcu_sup->srcu_barrier_cpu_cnt, 0);
	INIT_DELAYED_WORK(&ssp->srcu_sup->work, process_srcu);
	ssp->srcu_sup->sda_is_static = is_static;
	if (!is_static)
		ssp->sda = alloc_percpu(struct srcu_data);
	if (!ssp->sda)
		goto err_free_sup;
	init_srcu_struct_data(ssp);
	ssp->srcu_sup->srcu_gp_seq_needed_exp = 0;
	ssp->srcu_sup->srcu_last_gp_end = ktime_get_mono_fast_ns();
	if (READ_ONCE(ssp->srcu_sup->srcu_size_state) == SRCU_SIZE_SMALL && SRCU_SIZING_IS_INIT()) {
		if (!init_srcu_struct_nodes(ssp, GFP_ATOMIC))
			goto err_free_sda;
		WRITE_ONCE(ssp->srcu_sup->srcu_size_state, SRCU_SIZE_BIG);
	}
	ssp->srcu_sup->srcu_ssp = ssp;
	smp_store_release(&ssp->srcu_sup->srcu_gp_seq_needed, 0); /* Init done. */
	return 0;

err_free_sda:
	if (!is_static) {
		free_percpu(ssp->sda);
		ssp->sda = NULL;
	}
err_free_sup:
	if (!is_static) {
		kfree(ssp->srcu_sup);
		ssp->srcu_sup = NULL;
	}
	return -ENOMEM;
}

#ifdef CONFIG_DEBUG_LOCK_ALLOC

int __init_srcu_struct(struct srcu_struct *ssp, const char *name,
		       struct lock_class_key *key)
{
	/* Don't re-initialize a lock while it is held. */
	debug_check_no_locks_freed((void *)ssp, sizeof(*ssp));
	lockdep_init_map(&ssp->dep_map, name, key, 0);
	return init_srcu_struct_fields(ssp, false);
}
EXPORT_SYMBOL_GPL(__init_srcu_struct);

#else /* #ifdef CONFIG_DEBUG_LOCK_ALLOC */

/**
 * init_srcu_struct - initialize a sleep-RCU structure
 * @ssp: structure to initialize.
 *
 * Must invoke this on a given srcu_struct before passing that srcu_struct
 * to any other function.  Each srcu_struct represents a separate domain
 * of SRCU protection.
 */
int init_srcu_struct(struct srcu_struct *ssp)
{
	return init_srcu_struct_fields(ssp, false);
}
EXPORT_SYMBOL_GPL(init_srcu_struct);

#endif /* #else #ifdef CONFIG_DEBUG_LOCK_ALLOC */

/*
 * Initiate a transition to SRCU_SIZE_BIG with lock held.
 */
static void __srcu_transition_to_big(struct srcu_struct *ssp)
{
	lockdep_assert_held(&ACCESS_PRIVATE(ssp->srcu_sup, lock));
	smp_store_release(&ssp->srcu_sup->srcu_size_state, SRCU_SIZE_ALLOC);
}

/*
 * Initiate an idempotent transition to SRCU_SIZE_BIG.
 */
static void srcu_transition_to_big(struct srcu_struct *ssp)
{
	unsigned long flags;

	/* Double-checked locking on ->srcu_size-state. */
	if (smp_load_acquire(&ssp->srcu_sup->srcu_size_state) != SRCU_SIZE_SMALL)
		return;
	spin_lock_irqsave_rcu_node(ssp->srcu_sup, flags);
	if (smp_load_acquire(&ssp->srcu_sup->srcu_size_state) != SRCU_SIZE_SMALL) {
		spin_unlock_irqrestore_rcu_node(ssp->srcu_sup, flags);
		return;
	}
	__srcu_transition_to_big(ssp);
	spin_unlock_irqrestore_rcu_node(ssp->srcu_sup, flags);
}

/*
 * Check to see if the just-encountered contention event justifies
 * a transition to SRCU_SIZE_BIG.
 */
static void spin_lock_irqsave_check_contention(struct srcu_struct *ssp)
{
	unsigned long j;

	if (!SRCU_SIZING_IS_CONTEND() || ssp->srcu_sup->srcu_size_state)
		return;
	j = jiffies;
	if (ssp->srcu_sup->srcu_size_jiffies != j) {
		ssp->srcu_sup->srcu_size_jiffies = j;
		ssp->srcu_sup->srcu_n_lock_retries = 0;
	}
	if (++ssp->srcu_sup->srcu_n_lock_retries <= small_contention_lim)
		return;
	__srcu_transition_to_big(ssp);
}

/*
 * Acquire the specified srcu_data structure's ->lock, but check for
 * excessive contention, which results in initiation of a transition
 * to SRCU_SIZE_BIG.  But only if the srcutree.convert_to_big module
 * parameter permits this.
 */
static void spin_lock_irqsave_sdp_contention(struct srcu_data *sdp, unsigned long *flags)
{
	struct srcu_struct *ssp = sdp->ssp;

	if (spin_trylock_irqsave_rcu_node(sdp, *flags))
		return;
	spin_lock_irqsave_rcu_node(ssp->srcu_sup, *flags);
	spin_lock_irqsave_check_contention(ssp);
	spin_unlock_irqrestore_rcu_node(ssp->srcu_sup, *flags);
	spin_lock_irqsave_rcu_node(sdp, *flags);
}

/*
 * Acquire the specified srcu_struct structure's ->lock, but check for
 * excessive contention, which results in initiation of a transition
 * to SRCU_SIZE_BIG.  But only if the srcutree.convert_to_big module
 * parameter permits this.
 */
static void spin_lock_irqsave_ssp_contention(struct srcu_struct *ssp, unsigned long *flags)
{
	if (spin_trylock_irqsave_rcu_node(ssp->srcu_sup, *flags))
		return;
	spin_lock_irqsave_rcu_node(ssp->srcu_sup, *flags);
	spin_lock_irqsave_check_contention(ssp);
}

/*
 * First-use initialization of statically allocated srcu_struct
 * structure.  Wiring up the combining tree is more than can be
 * done with compile-time initialization, so this check is added
 * to each update-side SRCU primitive.  Use ssp->lock, which -is-
 * compile-time initialized, to resolve races involving multiple
 * CPUs trying to garner first-use privileges.
 */
static void check_init_srcu_struct(struct srcu_struct *ssp)
{
	unsigned long flags;

	/* The smp_load_acquire() pairs with the smp_store_release(). */
	if (!rcu_seq_state(smp_load_acquire(&ssp->srcu_sup->srcu_gp_seq_needed))) /*^^^*/
		return; /* Already initialized. */
	spin_lock_irqsave_rcu_node(ssp->srcu_sup, flags);
	if (!rcu_seq_state(ssp->srcu_sup->srcu_gp_seq_needed)) {
		spin_unlock_irqrestore_rcu_node(ssp->srcu_sup, flags);
		return;
	}
	init_srcu_struct_fields(ssp, true);
	spin_unlock_irqrestore_rcu_node(ssp->srcu_sup, flags);
}

/*
 * Returns approximate total of the readers' ->srcu_lock_count[] values
 * for the rank of per-CPU counters specified by idx.
 */
static unsigned long srcu_readers_lock_idx(struct srcu_struct *ssp, int idx)
{
	int cpu;
	unsigned long sum = 0;

	for_each_possible_cpu(cpu) {
		struct srcu_data *cpuc = per_cpu_ptr(ssp->sda, cpu);

		sum += atomic_long_read(&cpuc->srcu_lock_count[idx]);
	}
	return sum;
}

/*
 * Returns approximate total of the readers' ->srcu_unlock_count[] values
 * for the rank of per-CPU counters specified by idx.
 */
static unsigned long srcu_readers_unlock_idx(struct srcu_struct *ssp, int idx)
{
	int cpu;
	unsigned long mask = 0;
	unsigned long sum = 0;

	for_each_possible_cpu(cpu) {
		struct srcu_data *cpuc = per_cpu_ptr(ssp->sda, cpu);

		sum += atomic_long_read(&cpuc->srcu_unlock_count[idx]);
		if (IS_ENABLED(CONFIG_PROVE_RCU))
			mask = mask | READ_ONCE(cpuc->srcu_nmi_safety);
	}
	WARN_ONCE(IS_ENABLED(CONFIG_PROVE_RCU) && (mask & (mask >> 1)),
		  "Mixed NMI-safe readers for srcu_struct at %ps.\n", ssp);
	return sum;
}

/*
 * Return true if the number of pre-existing readers is determined to
 * be zero.
 */
static bool srcu_readers_active_idx_check(struct srcu_struct *ssp, int idx)
{
	unsigned long unlocks;

	unlocks = srcu_readers_unlock_idx(ssp, idx);

	/*
	 * Make sure that a lock is always counted if the corresponding
	 * unlock is counted. Needs to be a smp_mb() as the read side may
	 * contain a read from a variable that is written to before the
	 * synchronize_srcu() in the write side. In this case smp_mb()s
	 * A and B act like the store buffering pattern.
	 *
	 * This smp_mb() also pairs with smp_mb() C to prevent accesses
	 * after the synchronize_srcu() from being executed before the
	 * grace period ends.
	 */
	smp_mb(); /* A */

	/*
	 * If the locks are the same as the unlocks, then there must have
	 * been no readers on this index at some point in this function.
	 * But there might be more readers, as a task might have read
	 * the current ->srcu_idx but not yet have incremented its CPU's
	 * ->srcu_lock_count[idx] counter.  In fact, it is possible
	 * that most of the tasks have been preempted between fetching
	 * ->srcu_idx and incrementing ->srcu_lock_count[idx].  And there
	 * could be almost (ULONG_MAX / sizeof(struct task_struct)) tasks
	 * in a system whose address space was fully populated with memory.
	 * Call this quantity Nt.
	 *
	 * So suppose that the updater is preempted at this point in the
	 * code for a long time.  That now-preempted updater has already
	 * flipped ->srcu_idx (possibly during the preceding grace period),
	 * done an smp_mb() (again, possibly during the preceding grace
	 * period), and summed up the ->srcu_unlock_count[idx] counters.
	 * How many times can a given one of the aforementioned Nt tasks
	 * increment the old ->srcu_idx value's ->srcu_lock_count[idx]
	 * counter, in the absence of nesting?
	 *
	 * It can clearly do so once, given that it has already fetched
	 * the old value of ->srcu_idx and is just about to use that value
	 * to index its increment of ->srcu_lock_count[idx].  But as soon as
	 * it leaves that SRCU read-side critical section, it will increment
	 * ->srcu_unlock_count[idx], which must follow the updater's above
	 * read from that same value.  Thus, as soon the reading task does
	 * an smp_mb() and a later fetch from ->srcu_idx, that task will be
	 * guaranteed to get the new index.  Except that the increment of
	 * ->srcu_unlock_count[idx] in __srcu_read_unlock() is after the
	 * smp_mb(), and the fetch from ->srcu_idx in __srcu_read_lock()
	 * is before the smp_mb().  Thus, that task might not see the new
	 * value of ->srcu_idx until the -second- __srcu_read_lock(),
	 * which in turn means that this task might well increment
	 * ->srcu_lock_count[idx] for the old value of ->srcu_idx twice,
	 * not just once.
	 *
	 * However, it is important to note that a given smp_mb() takes
	 * effect not just for the task executing it, but also for any
	 * later task running on that same CPU.
	 *
	 * That is, there can be almost Nt + Nc further increments of
	 * ->srcu_lock_count[idx] for the old index, where Nc is the number
	 * of CPUs.  But this is OK because the size of the task_struct
	 * structure limits the value of Nt and current systems limit Nc
	 * to a few thousand.
	 *
	 * OK, but what about nesting?  This does impose a limit on
	 * nesting of half of the size of the task_struct structure
	 * (measured in bytes), which should be sufficient.  A late 2022
	 * TREE01 rcutorture run reported this size to be no less than
	 * 9408 bytes, allowing up to 4704 levels of nesting, which is
	 * comfortably beyond excessive.  Especially on 64-bit systems,
	 * which are unlikely to be configured with an address space fully
	 * populated with memory, at least not anytime soon.
	 */
	return srcu_readers_lock_idx(ssp, idx) == unlocks;
}

/**
 * srcu_readers_active - returns true if there are readers. and false
 *                       otherwise
 * @ssp: which srcu_struct to count active readers (holding srcu_read_lock).
 *
 * Note that this is not an atomic primitive, and can therefore suffer
 * severe errors when invoked on an active srcu_struct.  That said, it
 * can be useful as an error check at cleanup time.
 */
static bool srcu_readers_active(struct srcu_struct *ssp)
{
	int cpu;
	unsigned long sum = 0;

	for_each_possible_cpu(cpu) {
		struct srcu_data *cpuc = per_cpu_ptr(ssp->sda, cpu);

		sum += atomic_long_read(&cpuc->srcu_lock_count[0]);
		sum += atomic_long_read(&cpuc->srcu_lock_count[1]);
		sum -= atomic_long_read(&cpuc->srcu_unlock_count[0]);
		sum -= atomic_long_read(&cpuc->srcu_unlock_count[1]);
	}
	return sum;
}

/*
 * We use an adaptive strategy for synchronize_srcu() and especially for
 * synchronize_srcu_expedited().  We spin for a fixed time period
 * (defined below, boot time configurable) to allow SRCU readers to exit
 * their read-side critical sections.  If there are still some readers
 * after one jiffy, we repeatedly block for one jiffy time periods.
 * The blocking time is increased as the grace-period age increases,
 * with max blocking time capped at 10 jiffies.
 */
#define SRCU_DEFAULT_RETRY_CHECK_DELAY		5

static ulong srcu_retry_check_delay = SRCU_DEFAULT_RETRY_CHECK_DELAY;
module_param(srcu_retry_check_delay, ulong, 0444);

#define SRCU_INTERVAL		1		// Base delay if no expedited GPs pending.
#define SRCU_MAX_INTERVAL	10		// Maximum incremental delay from slow readers.

#define SRCU_DEFAULT_MAX_NODELAY_PHASE_LO	3UL	// Lowmark on default per-GP-phase
							// no-delay instances.
#define SRCU_DEFAULT_MAX_NODELAY_PHASE_HI	1000UL	// Highmark on default per-GP-phase
							// no-delay instances.

#define SRCU_UL_CLAMP_LO(val, low)	((val) > (low) ? (val) : (low))
#define SRCU_UL_CLAMP_HI(val, high)	((val) < (high) ? (val) : (high))
#define SRCU_UL_CLAMP(val, low, high)	SRCU_UL_CLAMP_HI(SRCU_UL_CLAMP_LO((val), (low)), (high))
// per-GP-phase no-delay instances adjusted to allow non-sleeping poll upto
// one jiffies time duration. Mult by 2 is done to factor in the srcu_get_delay()
// called from process_srcu().
#define SRCU_DEFAULT_MAX_NODELAY_PHASE_ADJUSTED	\
	(2UL * USEC_PER_SEC / HZ / SRCU_DEFAULT_RETRY_CHECK_DELAY)

// Maximum per-GP-phase consecutive no-delay instances.
#define SRCU_DEFAULT_MAX_NODELAY_PHASE	\
	SRCU_UL_CLAMP(SRCU_DEFAULT_MAX_NODELAY_PHASE_ADJUSTED,	\
		      SRCU_DEFAULT_MAX_NODELAY_PHASE_LO,	\
		      SRCU_DEFAULT_MAX_NODELAY_PHASE_HI)

static ulong srcu_max_nodelay_phase = SRCU_DEFAULT_MAX_NODELAY_PHASE;
module_param(srcu_max_nodelay_phase, ulong, 0444);

// Maximum consecutive no-delay instances.
#define SRCU_DEFAULT_MAX_NODELAY	(SRCU_DEFAULT_MAX_NODELAY_PHASE > 100 ?	\
					 SRCU_DEFAULT_MAX_NODELAY_PHASE : 100)

static ulong srcu_max_nodelay = SRCU_DEFAULT_MAX_NODELAY;
module_param(srcu_max_nodelay, ulong, 0444);

/*
 * Return grace-period delay, zero if there are expedited grace
 * periods pending, SRCU_INTERVAL otherwise.
 */
static unsigned long srcu_get_delay(struct srcu_struct *ssp)
{
	unsigned long gpstart;
	unsigned long j;
	unsigned long jbase = SRCU_INTERVAL;
	struct srcu_usage *sup = ssp->srcu_sup;

	if (ULONG_CMP_LT(READ_ONCE(sup->srcu_gp_seq), READ_ONCE(sup->srcu_gp_seq_needed_exp)))
		jbase = 0;
	if (rcu_seq_state(READ_ONCE(sup->srcu_gp_seq))) {
		j = jiffies - 1;
		gpstart = READ_ONCE(sup->srcu_gp_start);
		if (time_after(j, gpstart))
			jbase += j - gpstart;
		if (!jbase) {
			WRITE_ONCE(sup->srcu_n_exp_nodelay, READ_ONCE(sup->srcu_n_exp_nodelay) + 1);
			if (READ_ONCE(sup->srcu_n_exp_nodelay) > srcu_max_nodelay_phase)
				jbase = 1;
		}
	}
	return jbase > SRCU_MAX_INTERVAL ? SRCU_MAX_INTERVAL : jbase;
}

/**
 * cleanup_srcu_struct - deconstruct a sleep-RCU structure
 * @ssp: structure to clean up.
 *
 * Must invoke this after you are finished using a given srcu_struct that
 * was initialized via init_srcu_struct(), else you leak memory.
 */
void cleanup_srcu_struct(struct srcu_struct *ssp)
{
	int cpu;
	struct srcu_usage *sup = ssp->srcu_sup;

	if (WARN_ON(!srcu_get_delay(ssp)))
		return; /* Just leak it! */
	if (WARN_ON(srcu_readers_active(ssp)))
		return; /* Just leak it! */
	flush_delayed_work(&sup->work);
	for_each_possible_cpu(cpu) {
		struct srcu_data *sdp = per_cpu_ptr(ssp->sda, cpu);

		del_timer_sync(&sdp->delay_work);
		flush_work(&sdp->work);
		if (WARN_ON(rcu_segcblist_n_cbs(&sdp->srcu_cblist)))
			return; /* Forgot srcu_barrier(), so just leak it! */
	}
	if (WARN_ON(rcu_seq_state(READ_ONCE(sup->srcu_gp_seq)) != SRCU_STATE_IDLE) ||
	    WARN_ON(rcu_seq_current(&sup->srcu_gp_seq) != sup->srcu_gp_seq_needed) ||
	    WARN_ON(srcu_readers_active(ssp))) {
		pr_info("%s: Active srcu_struct %p read state: %d gp state: %lu/%lu\n",
			__func__, ssp, rcu_seq_state(READ_ONCE(sup->srcu_gp_seq)),
			rcu_seq_current(&sup->srcu_gp_seq), sup->srcu_gp_seq_needed);
		return; /* Caller forgot to stop doing call_srcu()? */
	}
	kfree(sup->node);
	sup->node = NULL;
	sup->srcu_size_state = SRCU_SIZE_SMALL;
	if (!sup->sda_is_static) {
		free_percpu(ssp->sda);
		ssp->sda = NULL;
		kfree(sup);
		ssp->srcu_sup = NULL;
	}
}
EXPORT_SYMBOL_GPL(cleanup_srcu_struct);

#ifdef CONFIG_PROVE_RCU
/*
 * Check for consistent NMI safety.
 */
void srcu_check_nmi_safety(struct srcu_struct *ssp, bool nmi_safe)
{
	int nmi_safe_mask = 1 << nmi_safe;
	int old_nmi_safe_mask;
	struct srcu_data *sdp;

	/* NMI-unsafe use in NMI is a bad sign */
	WARN_ON_ONCE(!nmi_safe && in_nmi());
	sdp = raw_cpu_ptr(ssp->sda);
	old_nmi_safe_mask = READ_ONCE(sdp->srcu_nmi_safety);
	if (!old_nmi_safe_mask) {
		WRITE_ONCE(sdp->srcu_nmi_safety, nmi_safe_mask);
		return;
	}
	WARN_ONCE(old_nmi_safe_mask != nmi_safe_mask, "CPU %d old state %d new state %d\n", sdp->cpu, old_nmi_safe_mask, nmi_safe_mask);
}
EXPORT_SYMBOL_GPL(srcu_check_nmi_safety);
#endif /* CONFIG_PROVE_RCU */

/*
 * Counts the new reader in the appropriate per-CPU element of the
 * srcu_struct.
 * Returns an index that must be passed to the matching srcu_read_unlock().
 */
int __srcu_read_lock(struct srcu_struct *ssp)
{
	int idx;

	idx = READ_ONCE(ssp->srcu_idx) & 0x1;
	this_cpu_inc(ssp->sda->srcu_lock_count[idx].counter);
	smp_mb(); /* B */  /* Avoid leaking the critical section. */
	return idx;
}
EXPORT_SYMBOL_GPL(__srcu_read_lock);

/*
 * Removes the count for the old reader from the appropriate per-CPU
 * element of the srcu_struct.  Note that this may well be a different
 * CPU than that which was incremented by the corresponding srcu_read_lock().
 */
void __srcu_read_unlock(struct srcu_struct *ssp, int idx)
{
	smp_mb(); /* C */  /* Avoid leaking the critical section. */
	this_cpu_inc(ssp->sda->srcu_unlock_count[idx].counter);
}
EXPORT_SYMBOL_GPL(__srcu_read_unlock);

#ifdef CONFIG_NEED_SRCU_NMI_SAFE

/*
 * Counts the new reader in the appropriate per-CPU element of the
 * srcu_struct, but in an NMI-safe manner using RMW atomics.
 * Returns an index that must be passed to the matching srcu_read_unlock().
 */
int __srcu_read_lock_nmisafe(struct srcu_struct *ssp)
{
	int idx;
	struct srcu_data *sdp = raw_cpu_ptr(ssp->sda);

	idx = READ_ONCE(ssp->srcu_idx) & 0x1;
	atomic_long_inc(&sdp->srcu_lock_count[idx]);
	smp_mb__after_atomic(); /* B */  /* Avoid leaking the critical section. */
	return idx;
}
EXPORT_SYMBOL_GPL(__srcu_read_lock_nmisafe);

/*
 * Removes the count for the old reader from the appropriate per-CPU
 * element of the srcu_struct.  Note that this may well be a different
 * CPU than that which was incremented by the corresponding srcu_read_lock().
 */
void __srcu_read_unlock_nmisafe(struct srcu_struct *ssp, int idx)
{
	struct srcu_data *sdp = raw_cpu_ptr(ssp->sda);

	smp_mb__before_atomic(); /* C */  /* Avoid leaking the critical section. */
	atomic_long_inc(&sdp->srcu_unlock_count[idx]);
}
EXPORT_SYMBOL_GPL(__srcu_read_unlock_nmisafe);

#endif // CONFIG_NEED_SRCU_NMI_SAFE

/*
 * Start an SRCU grace period.
 */
static void srcu_gp_start(struct srcu_struct *ssp)
{
	int state;

	lockdep_assert_held(&ACCESS_PRIVATE(ssp->srcu_sup, lock));
	WARN_ON_ONCE(ULONG_CMP_GE(ssp->srcu_sup->srcu_gp_seq, ssp->srcu_sup->srcu_gp_seq_needed));
	WRITE_ONCE(ssp->srcu_sup->srcu_gp_start, jiffies);
	WRITE_ONCE(ssp->srcu_sup->srcu_n_exp_nodelay, 0);
	smp_mb(); /* Order prior store to ->srcu_gp_seq_needed vs. GP start. */
	rcu_seq_start(&ssp->srcu_sup->srcu_gp_seq);
	state = rcu_seq_state(ssp->srcu_sup->srcu_gp_seq);
	WARN_ON_ONCE(state != SRCU_STATE_SCAN1);
}


static void srcu_delay_timer(struct timer_list *t)
{
	struct srcu_data *sdp = container_of(t, struct srcu_data, delay_work);

	queue_work_on(sdp->cpu, rcu_gp_wq, &sdp->work);
}

static void srcu_queue_delayed_work_on(struct srcu_data *sdp,
				       unsigned long delay)
{
	if (!delay) {
		queue_work_on(sdp->cpu, rcu_gp_wq, &sdp->work);
		return;
	}

	timer_reduce(&sdp->delay_work, jiffies + delay);
}

/*
 * Schedule callback invocation for the specified srcu_data structure,
 * if possible, on the corresponding CPU.
 */
static void srcu_schedule_cbs_sdp(struct srcu_data *sdp, unsigned long delay)
{
	srcu_queue_delayed_work_on(sdp, delay);
}

/*
 * Schedule callback invocation for all srcu_data structures associated
 * with the specified srcu_node structure that have callbacks for the
 * just-completed grace period, the one corresponding to idx.  If possible,
 * schedule this invocation on the corresponding CPUs.
 */
static void srcu_schedule_cbs_snp(struct srcu_struct *ssp, struct srcu_node *snp,
				  unsigned long mask, unsigned long delay)
{
	int cpu;

	for (cpu = snp->grplo; cpu <= snp->grphi; cpu++) {
		if (!(mask & (1UL << (cpu - snp->grplo))))
			continue;
		srcu_schedule_cbs_sdp(per_cpu_ptr(ssp->sda, cpu), delay);
	}
}

/*
 * Note the end of an SRCU grace period.  Initiates callback invocation
 * and starts a new grace period if needed.
 *
 * The ->srcu_cb_mutex acquisition does not protect any data, but
 * instead prevents more than one grace period from starting while we
 * are initiating callback invocation.  This allows the ->srcu_have_cbs[]
 * array to have a finite number of elements.
 */
static void srcu_gp_end(struct srcu_struct *ssp)
{
	unsigned long cbdelay = 1;
	bool cbs;
	bool last_lvl;
	int cpu;
	unsigned long flags;
	unsigned long gpseq;
	int idx;
	unsigned long mask;
	struct srcu_data *sdp;
	unsigned long sgsne;
	struct srcu_node *snp;
	int ss_state;
	struct srcu_usage *sup = ssp->srcu_sup;

	/* Prevent more than one additional grace period. */
	mutex_lock(&sup->srcu_cb_mutex);

	/* End the current grace period. */
	spin_lock_irq_rcu_node(sup);
	idx = rcu_seq_state(sup->srcu_gp_seq);
	WARN_ON_ONCE(idx != SRCU_STATE_SCAN2);
	if (ULONG_CMP_LT(READ_ONCE(sup->srcu_gp_seq), READ_ONCE(sup->srcu_gp_seq_needed_exp)))
		cbdelay = 0;

	WRITE_ONCE(sup->srcu_last_gp_end, ktime_get_mono_fast_ns());
	rcu_seq_end(&sup->srcu_gp_seq);
	gpseq = rcu_seq_current(&sup->srcu_gp_seq);
	if (ULONG_CMP_LT(sup->srcu_gp_seq_needed_exp, gpseq))
		WRITE_ONCE(sup->srcu_gp_seq_needed_exp, gpseq);
	spin_unlock_irq_rcu_node(sup);
	mutex_unlock(&sup->srcu_gp_mutex);
	/* A new grace period can start at this point.  But only one. */

	/* Initiate callback invocation as needed. */
	ss_state = smp_load_acquire(&sup->srcu_size_state);
	if (ss_state < SRCU_SIZE_WAIT_BARRIER) {
		srcu_schedule_cbs_sdp(per_cpu_ptr(ssp->sda, get_boot_cpu_id()),
					cbdelay);
	} else {
		idx = rcu_seq_ctr(gpseq) % ARRAY_SIZE(snp->srcu_have_cbs);
		srcu_for_each_node_breadth_first(ssp, snp) {
			spin_lock_irq_rcu_node(snp);
			cbs = false;
			last_lvl = snp >= sup->level[rcu_num_lvls - 1];
			if (last_lvl)
				cbs = ss_state < SRCU_SIZE_BIG || snp->srcu_have_cbs[idx] == gpseq;
			snp->srcu_have_cbs[idx] = gpseq;
			rcu_seq_set_state(&snp->srcu_have_cbs[idx], 1);
			sgsne = snp->srcu_gp_seq_needed_exp;
			if (srcu_invl_snp_seq(sgsne) || ULONG_CMP_LT(sgsne, gpseq))
				WRITE_ONCE(snp->srcu_gp_seq_needed_exp, gpseq);
			if (ss_state < SRCU_SIZE_BIG)
				mask = ~0;
			else
				mask = snp->srcu_data_have_cbs[idx];
			snp->srcu_data_have_cbs[idx] = 0;
			spin_unlock_irq_rcu_node(snp);
			if (cbs)
				srcu_schedule_cbs_snp(ssp, snp, mask, cbdelay);
		}
	}

	/* Occasionally prevent srcu_data counter wrap. */
	if (!(gpseq & counter_wrap_check))
		for_each_possible_cpu(cpu) {
			sdp = per_cpu_ptr(ssp->sda, cpu);
			spin_lock_irqsave_rcu_node(sdp, flags);
			if (ULONG_CMP_GE(gpseq, sdp->srcu_gp_seq_needed + 100))
				sdp->srcu_gp_seq_needed = gpseq;
			if (ULONG_CMP_GE(gpseq, sdp->srcu_gp_seq_needed_exp + 100))
				sdp->srcu_gp_seq_needed_exp = gpseq;
			spin_unlock_irqrestore_rcu_node(sdp, flags);
		}

	/* Callback initiation done, allow grace periods after next. */
	mutex_unlock(&sup->srcu_cb_mutex);

	/* Start a new grace period if needed. */
	spin_lock_irq_rcu_node(sup);
	gpseq = rcu_seq_current(&sup->srcu_gp_seq);
	if (!rcu_seq_state(gpseq) &&
	    ULONG_CMP_LT(gpseq, sup->srcu_gp_seq_needed)) {
		srcu_gp_start(ssp);
		spin_unlock_irq_rcu_node(sup);
		srcu_reschedule(ssp, 0);
	} else {
		spin_unlock_irq_rcu_node(sup);
	}

	/* Transition to big if needed. */
	if (ss_state != SRCU_SIZE_SMALL && ss_state != SRCU_SIZE_BIG) {
		if (ss_state == SRCU_SIZE_ALLOC)
			init_srcu_struct_nodes(ssp, GFP_KERNEL);
		else
			smp_store_release(&sup->srcu_size_state, ss_state + 1);
	}
}

/*
 * Funnel-locking scheme to scalably mediate many concurrent expedited
 * grace-period requests.  This function is invoked for the first known
 * expedited request for a grace period that has already been requested,
 * but without expediting.  To start a completely new grace period,
 * whether expedited or not, use srcu_funnel_gp_start() instead.
 */
static void srcu_funnel_exp_start(struct srcu_struct *ssp, struct srcu_node *snp,
				  unsigned long s)
{
	unsigned long flags;
	unsigned long sgsne;

	if (snp)
		for (; snp != NULL; snp = snp->srcu_parent) {
			sgsne = READ_ONCE(snp->srcu_gp_seq_needed_exp);
			if (WARN_ON_ONCE(rcu_seq_done(&ssp->srcu_sup->srcu_gp_seq, s)) ||
			    (!srcu_invl_snp_seq(sgsne) && ULONG_CMP_GE(sgsne, s)))
				return;
			spin_lock_irqsave_rcu_node(snp, flags);
			sgsne = snp->srcu_gp_seq_needed_exp;
			if (!srcu_invl_snp_seq(sgsne) && ULONG_CMP_GE(sgsne, s)) {
				spin_unlock_irqrestore_rcu_node(snp, flags);
				return;
			}
			WRITE_ONCE(snp->srcu_gp_seq_needed_exp, s);
			spin_unlock_irqrestore_rcu_node(snp, flags);
		}
	spin_lock_irqsave_ssp_contention(ssp, &flags);
	if (ULONG_CMP_LT(ssp->srcu_sup->srcu_gp_seq_needed_exp, s))
		WRITE_ONCE(ssp->srcu_sup->srcu_gp_seq_needed_exp, s);
	spin_unlock_irqrestore_rcu_node(ssp->srcu_sup, flags);
}

/*
 * Funnel-locking scheme to scalably mediate many concurrent grace-period
 * requests.  The winner has to do the work of actually starting grace
 * period s.  Losers must either ensure that their desired grace-period
 * number is recorded on at least their leaf srcu_node structure, or they
 * must take steps to invoke their own callbacks.
 *
 * Note that this function also does the work of srcu_funnel_exp_start(),
 * in some cases by directly invoking it.
 *
 * The srcu read lock should be hold around this function. And s is a seq snap
 * after holding that lock.
 */
static void srcu_funnel_gp_start(struct srcu_struct *ssp, struct srcu_data *sdp,
				 unsigned long s, bool do_norm)
{
	unsigned long flags;
	int idx = rcu_seq_ctr(s) % ARRAY_SIZE(sdp->mynode->srcu_have_cbs);
	unsigned long sgsne;
	struct srcu_node *snp;
	struct srcu_node *snp_leaf;
	unsigned long snp_seq;
	struct srcu_usage *sup = ssp->srcu_sup;

	/* Ensure that snp node tree is fully initialized before traversing it */
	if (smp_load_acquire(&sup->srcu_size_state) < SRCU_SIZE_WAIT_BARRIER)
		snp_leaf = NULL;
	else
		snp_leaf = sdp->mynode;

	if (snp_leaf)
		/* Each pass through the loop does one level of the srcu_node tree. */
		for (snp = snp_leaf; snp != NULL; snp = snp->srcu_parent) {
			if (WARN_ON_ONCE(rcu_seq_done(&sup->srcu_gp_seq, s)) && snp != snp_leaf)
				return; /* GP already done and CBs recorded. */
			spin_lock_irqsave_rcu_node(snp, flags);
			snp_seq = snp->srcu_have_cbs[idx];
			if (!srcu_invl_snp_seq(snp_seq) && ULONG_CMP_GE(snp_seq, s)) {
				if (snp == snp_leaf && snp_seq == s)
					snp->srcu_data_have_cbs[idx] |= sdp->grpmask;
				spin_unlock_irqrestore_rcu_node(snp, flags);
				if (snp == snp_leaf && snp_seq != s) {
					srcu_schedule_cbs_sdp(sdp, do_norm ? SRCU_INTERVAL : 0);
					return;
				}
				if (!do_norm)
					srcu_funnel_exp_start(ssp, snp, s);
				return;
			}
			snp->srcu_have_cbs[idx] = s;
			if (snp == snp_leaf)
				snp->srcu_data_have_cbs[idx] |= sdp->grpmask;
			sgsne = snp->srcu_gp_seq_needed_exp;
			if (!do_norm && (srcu_invl_snp_seq(sgsne) || ULONG_CMP_LT(sgsne, s)))
				WRITE_ONCE(snp->srcu_gp_seq_needed_exp, s);
			spin_unlock_irqrestore_rcu_node(snp, flags);
		}

	/* Top of tree, must ensure the grace period will be started. */
	spin_lock_irqsave_ssp_contention(ssp, &flags);
	if (ULONG_CMP_LT(sup->srcu_gp_seq_needed, s)) {
		/*
		 * Record need for grace period s.  Pair with load
		 * acquire setting up for initialization.
		 */
		smp_store_release(&sup->srcu_gp_seq_needed, s); /*^^^*/
	}
	if (!do_norm && ULONG_CMP_LT(sup->srcu_gp_seq_needed_exp, s))
		WRITE_ONCE(sup->srcu_gp_seq_needed_exp, s);

	/* If grace period not already in progress, start it. */
	if (!WARN_ON_ONCE(rcu_seq_done(&sup->srcu_gp_seq, s)) &&
	    rcu_seq_state(sup->srcu_gp_seq) == SRCU_STATE_IDLE) {
		WARN_ON_ONCE(ULONG_CMP_GE(sup->srcu_gp_seq, sup->srcu_gp_seq_needed));
		srcu_gp_start(ssp);

		// And how can that list_add() in the "else" clause
		// possibly be safe for concurrent execution?  Well,
		// it isn't.  And it does not have to be.  After all, it
		// can only be executed during early boot when there is only
		// the one boot CPU running with interrupts still disabled.
		if (likely(srcu_init_done))
			queue_delayed_work(rcu_gp_wq, &sup->work,
					   !!srcu_get_delay(ssp));
		else if (list_empty(&sup->work.work.entry))
			list_add(&sup->work.work.entry, &srcu_boot_list);
	}
	spin_unlock_irqrestore_rcu_node(sup, flags);
}

/*
 * Wait until all readers counted by array index idx complete, but
 * loop an additional time if there is an expedited grace period pending.
 * The caller must ensure that ->srcu_idx is not changed while checking.
 */
static bool try_check_zero(struct srcu_struct *ssp, int idx, int trycount)
{
	unsigned long curdelay;

	curdelay = !srcu_get_delay(ssp);

	for (;;) {
		if (srcu_readers_active_idx_check(ssp, idx))
			return true;
		if ((--trycount + curdelay) <= 0)
			return false;
		udelay(srcu_retry_check_delay);
	}
}

/*
 * Increment the ->srcu_idx counter so that future SRCU readers will
 * use the other rank of the ->srcu_(un)lock_count[] arrays.  This allows
 * us to wait for pre-existing readers in a starvation-free manner.
 */
static void srcu_flip(struct srcu_struct *ssp)
{
	/*
	 * Because the flip of ->srcu_idx is executed only if the
	 * preceding call to srcu_readers_active_idx_check() found that
	 * the ->srcu_unlock_count[] and ->srcu_lock_count[] sums matched
	 * and because that summing uses atomic_long_read(), there is
	 * ordering due to a control dependency between that summing and
	 * the WRITE_ONCE() in this call to srcu_flip().  This ordering
	 * ensures that if this updater saw a given reader's increment from
	 * __srcu_read_lock(), that reader was using a value of ->srcu_idx
	 * from before the previous call to srcu_flip(), which should be
	 * quite rare.  This ordering thus helps forward progress because
	 * the grace period could otherwise be delayed by additional
	 * calls to __srcu_read_lock() using that old (soon to be new)
	 * value of ->srcu_idx.
	 *
	 * This sum-equality check and ordering also ensures that if
	 * a given call to __srcu_read_lock() uses the new value of
	 * ->srcu_idx, this updater's earlier scans cannot have seen
	 * that reader's increments, which is all to the good, because
	 * this grace period need not wait on that reader.  After all,
	 * if those earlier scans had seen that reader, there would have
	 * been a sum mismatch and this code would not be reached.
	 *
	 * This means that the following smp_mb() is redundant, but
	 * it stays until either (1) Compilers learn about this sort of
	 * control dependency or (2) Some production workload running on
	 * a production system is unduly delayed by this slowpath smp_mb().
	 */
	smp_mb(); /* E */  /* Pairs with B and C. */

	WRITE_ONCE(ssp->srcu_idx, ssp->srcu_idx + 1); // Flip the counter.

	/*
	 * Ensure that if the updater misses an __srcu_read_unlock()
	 * increment, that task's __srcu_read_lock() following its next
	 * __srcu_read_lock() or __srcu_read_unlock() will see the above
	 * counter update.  Note that both this memory barrier and the
	 * one in srcu_readers_active_idx_check() provide the guarantee
	 * for __srcu_read_lock().
	 */
	smp_mb(); /* D */  /* Pairs with C. */
}

/*
 * If SRCU is likely idle, return true, otherwise return false.
 *
 * Note that it is OK for several current from-idle requests for a new
 * grace period from idle to specify expediting because they will all end
 * up requesting the same grace period anyhow.  So no loss.
 *
 * Note also that if any CPU (including the current one) is still invoking
 * callbacks, this function will nevertheless say "idle".  This is not
 * ideal, but the overhead of checking all CPUs' callback lists is even
 * less ideal, especially on large systems.  Furthermore, the wakeup
 * can happen before the callback is fully removed, so we have no choice
 * but to accept this type of error.
 *
 * This function is also subject to counter-wrap errors, but let's face
 * it, if this function was preempted for enough time for the counters
 * to wrap, it really doesn't matter whether or not we expedite the grace
 * period.  The extra overhead of a needlessly expedited grace period is
 * negligible when amortized over that time period, and the extra latency
 * of a needlessly non-expedited grace period is similarly negligible.
 */
static bool srcu_might_be_idle(struct srcu_struct *ssp)
{
	unsigned long curseq;
	unsigned long flags;
	struct srcu_data *sdp;
	unsigned long t;
	unsigned long tlast;

	check_init_srcu_struct(ssp);
	/* If the local srcu_data structure has callbacks, not idle.  */
	sdp = raw_cpu_ptr(ssp->sda);
	spin_lock_irqsave_rcu_node(sdp, flags);
	if (rcu_segcblist_pend_cbs(&sdp->srcu_cblist)) {
		spin_unlock_irqrestore_rcu_node(sdp, flags);
		return false; /* Callbacks already present, so not idle. */
	}
	spin_unlock_irqrestore_rcu_node(sdp, flags);

	/*
	 * No local callbacks, so probabilistically probe global state.
	 * Exact information would require acquiring locks, which would
	 * kill scalability, hence the probabilistic nature of the probe.
	 */

	/* First, see if enough time has passed since the last GP. */
	t = ktime_get_mono_fast_ns();
	tlast = READ_ONCE(ssp->srcu_sup->srcu_last_gp_end);
	if (exp_holdoff == 0 ||
	    time_in_range_open(t, tlast, tlast + exp_holdoff))
		return false; /* Too soon after last GP. */

	/* Next, check for probable idleness. */
	curseq = rcu_seq_current(&ssp->srcu_sup->srcu_gp_seq);
	smp_mb(); /* Order ->srcu_gp_seq with ->srcu_gp_seq_needed. */
	if (ULONG_CMP_LT(curseq, READ_ONCE(ssp->srcu_sup->srcu_gp_seq_needed)))
		return false; /* Grace period in progress, so not idle. */
	smp_mb(); /* Order ->srcu_gp_seq with prior access. */
	if (curseq != rcu_seq_current(&ssp->srcu_sup->srcu_gp_seq))
		return false; /* GP # changed, so not idle. */
	return true; /* With reasonable probability, idle! */
}

/*
 * SRCU callback function to leak a callback.
 */
static void srcu_leak_callback(struct rcu_head *rhp)
{
}

/*
 * Start an SRCU grace period, and also queue the callback if non-NULL.
 */
static unsigned long srcu_gp_start_if_needed(struct srcu_struct *ssp,
					     struct rcu_head *rhp, bool do_norm)
{
	unsigned long flags;
	int idx;
	bool needexp = false;
	bool needgp = false;
	unsigned long s;
	struct srcu_data *sdp;
	struct srcu_node *sdp_mynode;
	int ss_state;

	check_init_srcu_struct(ssp);
	/*
	 * While starting a new grace period, make sure we are in an
	 * SRCU read-side critical section so that the grace-period
	 * sequence number cannot wrap around in the meantime.
	 */
	idx = __srcu_read_lock_nmisafe(ssp);
	ss_state = smp_load_acquire(&ssp->srcu_sup->srcu_size_state);
	if (ss_state < SRCU_SIZE_WAIT_CALL)
		sdp = per_cpu_ptr(ssp->sda, get_boot_cpu_id());
	else
		sdp = raw_cpu_ptr(ssp->sda);
	spin_lock_irqsave_sdp_contention(sdp, &flags);
	if (rhp)
		rcu_segcblist_enqueue(&sdp->srcu_cblist, rhp);
	/*
	 * It's crucial to capture the snapshot 's' for acceleration before
	 * reading the current gp_seq that is used for advancing. This is
	 * essential because if the acceleration snapshot is taken after a
	 * failed advancement attempt, there's a risk that a grace period may
	 * conclude and a new one may start in the interim. If the snapshot is
	 * captured after this sequence of events, the acceleration snapshot 's'
	 * could be excessively advanced, leading to acceleration failure.
	 * In such a scenario, an 'acceleration leak' can occur, where new
	 * callbacks become indefinitely stuck in the RCU_NEXT_TAIL segment.
	 * Also note that encountering advancing failures is a normal
	 * occurrence when the grace period for RCU_WAIT_TAIL is in progress.
	 *
	 * To see this, consider the following events which occur if
	 * rcu_seq_snap() were to be called after advance:
	 *
	 *  1) The RCU_WAIT_TAIL segment has callbacks (gp_num = X + 4) and the
	 *     RCU_NEXT_READY_TAIL also has callbacks (gp_num = X + 8).
	 *
	 *  2) The grace period for RCU_WAIT_TAIL is seen as started but not
	 *     completed so rcu_seq_current() returns X + SRCU_STATE_SCAN1.
	 *
	 *  3) This value is passed to rcu_segcblist_advance() which can't move
	 *     any segment forward and fails.
	 *
	 *  4) srcu_gp_start_if_needed() still proceeds with callback acceleration.
	 *     But then the call to rcu_seq_snap() observes the grace period for the
	 *     RCU_WAIT_TAIL segment as completed and the subsequent one for the
	 *     RCU_NEXT_READY_TAIL segment as started (ie: X + 4 + SRCU_STATE_SCAN1)
	 *     so it returns a snapshot of the next grace period, which is X + 12.
	 *
	 *  5) The value of X + 12 is passed to rcu_segcblist_accelerate() but the
	 *     freshly enqueued callback in RCU_NEXT_TAIL can't move to
	 *     RCU_NEXT_READY_TAIL which already has callbacks for a previous grace
	 *     period (gp_num = X + 8). So acceleration fails.
	 */
	s = rcu_seq_snap(&ssp->srcu_sup->srcu_gp_seq);
	if (rhp) {
		rcu_segcblist_advance(&sdp->srcu_cblist,
				      rcu_seq_current(&ssp->srcu_sup->srcu_gp_seq));
		/*
		 * Acceleration can never fail because the base current gp_seq
		 * used for acceleration is <= the value of gp_seq used for
		 * advancing. This means that RCU_NEXT_TAIL segment will
		 * always be able to be emptied by the acceleration into the
		 * RCU_NEXT_READY_TAIL or RCU_WAIT_TAIL segments.
		 */
		WARN_ON_ONCE(!rcu_segcblist_accelerate(&sdp->srcu_cblist, s));
	}
	if (ULONG_CMP_LT(sdp->srcu_gp_seq_needed, s)) {
		sdp->srcu_gp_seq_needed = s;
		needgp = true;
	}
	if (!do_norm && ULONG_CMP_LT(sdp->srcu_gp_seq_needed_exp, s)) {
		sdp->srcu_gp_seq_needed_exp = s;
		needexp = true;
	}
	spin_unlock_irqrestore_rcu_node(sdp, flags);

	/* Ensure that snp node tree is fully initialized before traversing it */
	if (ss_state < SRCU_SIZE_WAIT_BARRIER)
		sdp_mynode = NULL;
	else
		sdp_mynode = sdp->mynode;

	if (needgp)
		srcu_funnel_gp_start(ssp, sdp, s, do_norm);
	else if (needexp)
		srcu_funnel_exp_start(ssp, sdp_mynode, s);
	__srcu_read_unlock_nmisafe(ssp, idx);
	return s;
}

/*
 * Enqueue an SRCU callback on the srcu_data structure associated with
 * the current CPU and the specified srcu_struct structure, initiating
 * grace-period processing if it is not already running.
 *
 * Note that all CPUs must agree that the grace period extended beyond
 * all pre-existing SRCU read-side critical section.  On systems with
 * more than one CPU, this means that when "func()" is invoked, each CPU
 * is guaranteed to have executed a full memory barrier since the end of
 * its last corresponding SRCU read-side critical section whose beginning
 * preceded the call to call_srcu().  It also means that each CPU executing
 * an SRCU read-side critical section that continues beyond the start of
 * "func()" must have executed a memory barrier after the call_srcu()
 * but before the beginning of that SRCU read-side critical section.
 * Note that these guarantees include CPUs that are offline, idle, or
 * executing in user mode, as well as CPUs that are executing in the kernel.
 *
 * Furthermore, if CPU A invoked call_srcu() and CPU B invoked the
 * resulting SRCU callback function "func()", then both CPU A and CPU
 * B are guaranteed to execute a full memory barrier during the time
 * interval between the call to call_srcu() and the invocation of "func()".
 * This guarantee applies even if CPU A and CPU B are the same CPU (but
 * again only if the system has more than one CPU).
 *
 * Of course, these guarantees apply only for invocations of call_srcu(),
 * srcu_read_lock(), and srcu_read_unlock() that are all passed the same
 * srcu_struct structure.
 */
static void __call_srcu(struct srcu_struct *ssp, struct rcu_head *rhp,
			rcu_callback_t func, bool do_norm)
{
	if (debug_rcu_head_queue(rhp)) {
		/* Probable double call_srcu(), so leak the callback. */
		WRITE_ONCE(rhp->func, srcu_leak_callback);
		WARN_ONCE(1, "call_srcu(): Leaked duplicate callback\n");
		return;
	}
	rhp->func = func;
	(void)srcu_gp_start_if_needed(ssp, rhp, do_norm);
}

/**
 * call_srcu() - Queue a callback for invocation after an SRCU grace period
 * @ssp: srcu_struct in queue the callback
 * @rhp: structure to be used for queueing the SRCU callback.
 * @func: function to be invoked after the SRCU grace period
 *
 * The callback function will be invoked some time after a full SRCU
 * grace period elapses, in other words after all pre-existing SRCU
 * read-side critical sections have completed.  However, the callback
 * function might well execute concurrently with other SRCU read-side
 * critical sections that started after call_srcu() was invoked.  SRCU
 * read-side critical sections are delimited by srcu_read_lock() and
 * srcu_read_unlock(), and may be nested.
 *
 * The callback will be invoked from process context, but must nevertheless
 * be fast and must not block.
 */
void call_srcu(struct srcu_struct *ssp, struct rcu_head *rhp,
	       rcu_callback_t func)
{
	__call_srcu(ssp, rhp, func, true);
}
EXPORT_SYMBOL_GPL(call_srcu);

/*
 * Helper function for synchronize_srcu() and synchronize_srcu_expedited().
 */
static void __synchronize_srcu(struct srcu_struct *ssp, bool do_norm)
{
	struct rcu_synchronize rcu;

	srcu_lock_sync(&ssp->dep_map);

	RCU_LOCKDEP_WARN(lockdep_is_held(ssp) ||
			 lock_is_held(&rcu_bh_lock_map) ||
			 lock_is_held(&rcu_lock_map) ||
			 lock_is_held(&rcu_sched_lock_map),
			 "Illegal synchronize_srcu() in same-type SRCU (or in RCU) read-side critical section");

	if (rcu_scheduler_active == RCU_SCHEDULER_INACTIVE)
		return;
	might_sleep();
	check_init_srcu_struct(ssp);
	init_completion(&rcu.completion);
	init_rcu_head_on_stack(&rcu.head);
	__call_srcu(ssp, &rcu.head, wakeme_after_rcu, do_norm);
	wait_for_completion(&rcu.completion);
	destroy_rcu_head_on_stack(&rcu.head);

	/*
	 * Make sure that later code is ordered after the SRCU grace
	 * period.  This pairs with the spin_lock_irq_rcu_node()
	 * in srcu_invoke_callbacks().  Unlike Tree RCU, this is needed
	 * because the current CPU might have been totally uninvolved with
	 * (and thus unordered against) that grace period.
	 */
	smp_mb();
}

/**
 * synchronize_srcu_expedited - Brute-force SRCU grace period
 * @ssp: srcu_struct with which to synchronize.
 *
 * Wait for an SRCU grace period to elapse, but be more aggressive about
 * spinning rather than blocking when waiting.
 *
 * Note that synchronize_srcu_expedited() has the same deadlock and
 * memory-ordering properties as does synchronize_srcu().
 */
void synchronize_srcu_expedited(struct srcu_struct *ssp)
{
	__synchronize_srcu(ssp, rcu_gp_is_normal());
}
EXPORT_SYMBOL_GPL(synchronize_srcu_expedited);

/**
 * synchronize_srcu - wait for prior SRCU read-side critical-section completion
 * @ssp: srcu_struct with which to synchronize.
 *
 * Wait for the count to drain to zero of both indexes. To avoid the
 * possible starvation of synchronize_srcu(), it waits for the count of
 * the index=((->srcu_idx & 1) ^ 1) to drain to zero at first,
 * and then flip the srcu_idx and wait for the count of the other index.
 *
 * Can block; must be called from process context.
 *
 * Note that it is illegal to call synchronize_srcu() from the corresponding
 * SRCU read-side critical section; doing so will result in deadlock.
 * However, it is perfectly legal to call synchronize_srcu() on one
 * srcu_struct from some other srcu_struct's read-side critical section,
 * as long as the resulting graph of srcu_structs is acyclic.
 *
 * There are memory-ordering constraints implied by synchronize_srcu().
 * On systems with more than one CPU, when synchronize_srcu() returns,
 * each CPU is guaranteed to have executed a full memory barrier since
 * the end of its last corresponding SRCU read-side critical section
 * whose beginning preceded the call to synchronize_srcu().  In addition,
 * each CPU having an SRCU read-side critical section that extends beyond
 * the return from synchronize_srcu() is guaranteed to have executed a
 * full memory barrier after the beginning of synchronize_srcu() and before
 * the beginning of that SRCU read-side critical section.  Note that these
 * guarantees include CPUs that are offline, idle, or executing in user mode,
 * as well as CPUs that are executing in the kernel.
 *
 * Furthermore, if CPU A invoked synchronize_srcu(), which returned
 * to its caller on CPU B, then both CPU A and CPU B are guaranteed
 * to have executed a full memory barrier during the execution of
 * synchronize_srcu().  This guarantee applies even if CPU A and CPU B
 * are the same CPU, but again only if the system has more than one CPU.
 *
 * Of course, these memory-ordering guarantees apply only when
 * synchronize_srcu(), srcu_read_lock(), and srcu_read_unlock() are
 * passed the same srcu_struct structure.
 *
 * Implementation of these memory-ordering guarantees is similar to
 * that of synchronize_rcu().
 *
 * If SRCU is likely idle, expedite the first request.  This semantic
 * was provided by Classic SRCU, and is relied upon by its users, so TREE
 * SRCU must also provide it.  Note that detecting idleness is heuristic
 * and subject to both false positives and negatives.
 */
void synchronize_srcu(struct srcu_struct *ssp)
{
	if (srcu_might_be_idle(ssp) || rcu_gp_is_expedited())
		synchronize_srcu_expedited(ssp);
	else
		__synchronize_srcu(ssp, true);
}
EXPORT_SYMBOL_GPL(synchronize_srcu);

/**
 * get_state_synchronize_srcu - Provide an end-of-grace-period cookie
 * @ssp: srcu_struct to provide cookie for.
 *
 * This function returns a cookie that can be passed to
 * poll_state_synchronize_srcu(), which will return true if a full grace
 * period has elapsed in the meantime.  It is the caller's responsibility
 * to make sure that grace period happens, for example, by invoking
 * call_srcu() after return from get_state_synchronize_srcu().
 */
unsigned long get_state_synchronize_srcu(struct srcu_struct *ssp)
{
	// Any prior manipulation of SRCU-protected data must happen
	// before the load from ->srcu_gp_seq.
	smp_mb();
	return rcu_seq_snap(&ssp->srcu_sup->srcu_gp_seq);
}
EXPORT_SYMBOL_GPL(get_state_synchronize_srcu);

/**
 * start_poll_synchronize_srcu - Provide cookie and start grace period
 * @ssp: srcu_struct to provide cookie for.
 *
 * This function returns a cookie that can be passed to
 * poll_state_synchronize_srcu(), which will return true if a full grace
 * period has elapsed in the meantime.  Unlike get_state_synchronize_srcu(),
 * this function also ensures that any needed SRCU grace period will be
 * started.  This convenience does come at a cost in terms of CPU overhead.
 */
unsigned long start_poll_synchronize_srcu(struct srcu_struct *ssp)
{
	return srcu_gp_start_if_needed(ssp, NULL, true);
}
EXPORT_SYMBOL_GPL(start_poll_synchronize_srcu);

/**
 * poll_state_synchronize_srcu - Has cookie's grace period ended?
 * @ssp: srcu_struct to provide cookie for.
 * @cookie: Return value from get_state_synchronize_srcu() or start_poll_synchronize_srcu().
 *
 * This function takes the cookie that was returned from either
 * get_state_synchronize_srcu() or start_poll_synchronize_srcu(), and
 * returns @true if an SRCU grace period elapsed since the time that the
 * cookie was created.
 *
 * Because cookies are finite in size, wrapping/overflow is possible.
 * This is more pronounced on 32-bit systems where cookies are 32 bits,
 * where in theory wrapping could happen in about 14 hours assuming
 * 25-microsecond expedited SRCU grace periods.  However, a more likely
 * overflow lower bound is on the order of 24 days in the case of
 * one-millisecond SRCU grace periods.  Of course, wrapping in a 64-bit
 * system requires geologic timespans, as in more than seven million years
 * even for expedited SRCU grace periods.
 *
 * Wrapping/overflow is much more of an issue for CONFIG_SMP=n systems
 * that also have CONFIG_PREEMPTION=n, which selects Tiny SRCU.  This uses
 * a 16-bit cookie, which rcutorture routinely wraps in a matter of a
 * few minutes.  If this proves to be a problem, this counter will be
 * expanded to the same size as for Tree SRCU.
 */
bool poll_state_synchronize_srcu(struct srcu_struct *ssp, unsigned long cookie)
{
	if (!rcu_seq_done(&ssp->srcu_sup->srcu_gp_seq, cookie))
		return false;
	// Ensure that the end of the SRCU grace period happens before
	// any subsequent code that the caller might execute.
	smp_mb(); // ^^^
	return true;
}
EXPORT_SYMBOL_GPL(poll_state_synchronize_srcu);

/*
 * Callback function for srcu_barrier() use.
 */
static void srcu_barrier_cb(struct rcu_head *rhp)
{
	struct srcu_data *sdp;
	struct srcu_struct *ssp;

	sdp = container_of(rhp, struct srcu_data, srcu_barrier_head);
	ssp = sdp->ssp;
	if (atomic_dec_and_test(&ssp->srcu_sup->srcu_barrier_cpu_cnt))
		complete(&ssp->srcu_sup->srcu_barrier_completion);
}

/*
 * Enqueue an srcu_barrier() callback on the specified srcu_data
 * structure's ->cblist.  but only if that ->cblist already has at least one
 * callback enqueued.  Note that if a CPU already has callbacks enqueue,
 * it must have already registered the need for a future grace period,
 * so all we need do is enqueue a callback that will use the same grace
 * period as the last callback already in the queue.
 */
static void srcu_barrier_one_cpu(struct srcu_struct *ssp, struct srcu_data *sdp)
{
	spin_lock_irq_rcu_node(sdp);
	atomic_inc(&ssp->srcu_sup->srcu_barrier_cpu_cnt);
	sdp->srcu_barrier_head.func = srcu_barrier_cb;
	debug_rcu_head_queue(&sdp->srcu_barrier_head);
	if (!rcu_segcblist_entrain(&sdp->srcu_cblist,
				   &sdp->srcu_barrier_head)) {
		debug_rcu_head_unqueue(&sdp->srcu_barrier_head);
		atomic_dec(&ssp->srcu_sup->srcu_barrier_cpu_cnt);
	}
	spin_unlock_irq_rcu_node(sdp);
}

/**
 * srcu_barrier - Wait until all in-flight call_srcu() callbacks complete.
 * @ssp: srcu_struct on which to wait for in-flight callbacks.
 */
void srcu_barrier(struct srcu_struct *ssp)
{
	int cpu;
	int idx;
	unsigned long s = rcu_seq_snap(&ssp->srcu_sup->srcu_barrier_seq);

	check_init_srcu_struct(ssp);
	mutex_lock(&ssp->srcu_sup->srcu_barrier_mutex);
	if (rcu_seq_done(&ssp->srcu_sup->srcu_barrier_seq, s)) {
		smp_mb(); /* Force ordering following return. */
		mutex_unlock(&ssp->srcu_sup->srcu_barrier_mutex);
		return; /* Someone else did our work for us. */
	}
	rcu_seq_start(&ssp->srcu_sup->srcu_barrier_seq);
	init_completion(&ssp->srcu_sup->srcu_barrier_completion);

	/* Initial count prevents reaching zero until all CBs are posted. */
	atomic_set(&ssp->srcu_sup->srcu_barrier_cpu_cnt, 1);

	idx = __srcu_read_lock_nmisafe(ssp);
	if (smp_load_acquire(&ssp->srcu_sup->srcu_size_state) < SRCU_SIZE_WAIT_BARRIER)
		srcu_barrier_one_cpu(ssp, per_cpu_ptr(ssp->sda,	get_boot_cpu_id()));
	else
		for_each_possible_cpu(cpu)
			srcu_barrier_one_cpu(ssp, per_cpu_ptr(ssp->sda, cpu));
	__srcu_read_unlock_nmisafe(ssp, idx);

	/* Remove the initial count, at which point reaching zero can happen. */
	if (atomic_dec_and_test(&ssp->srcu_sup->srcu_barrier_cpu_cnt))
		complete(&ssp->srcu_sup->srcu_barrier_completion);
	wait_for_completion(&ssp->srcu_sup->srcu_barrier_completion);

	rcu_seq_end(&ssp->srcu_sup->srcu_barrier_seq);
	mutex_unlock(&ssp->srcu_sup->srcu_barrier_mutex);
}
EXPORT_SYMBOL_GPL(srcu_barrier);

/**
 * srcu_batches_completed - return batches completed.
 * @ssp: srcu_struct on which to report batch completion.
 *
 * Report the number of batches, correlated with, but not necessarily
 * precisely the same as, the number of grace periods that have elapsed.
 */
unsigned long srcu_batches_completed(struct srcu_struct *ssp)
{
	return READ_ONCE(ssp->srcu_idx);
}
EXPORT_SYMBOL_GPL(srcu_batches_completed);

/*
 * Core SRCU state machine.  Push state bits of ->srcu_gp_seq
 * to SRCU_STATE_SCAN2, and invoke srcu_gp_end() when scan has
 * completed in that state.
 */
static void srcu_advance_state(struct srcu_struct *ssp)
{
	int idx;

	mutex_lock(&ssp->srcu_sup->srcu_gp_mutex);

	/*
	 * Because readers might be delayed for an extended period after
	 * fetching ->srcu_idx for their index, at any point in time there
	 * might well be readers using both idx=0 and idx=1.  We therefore
	 * need to wait for readers to clear from both index values before
	 * invoking a callback.
	 *
	 * The load-acquire ensures that we see the accesses performed
	 * by the prior grace period.
	 */
	idx = rcu_seq_state(smp_load_acquire(&ssp->srcu_sup->srcu_gp_seq)); /* ^^^ */
	if (idx == SRCU_STATE_IDLE) {
		spin_lock_irq_rcu_node(ssp->srcu_sup);
		if (ULONG_CMP_GE(ssp->srcu_sup->srcu_gp_seq, ssp->srcu_sup->srcu_gp_seq_needed)) {
			WARN_ON_ONCE(rcu_seq_state(ssp->srcu_sup->srcu_gp_seq));
			spin_unlock_irq_rcu_node(ssp->srcu_sup);
			mutex_unlock(&ssp->srcu_sup->srcu_gp_mutex);
			return;
		}
		idx = rcu_seq_state(READ_ONCE(ssp->srcu_sup->srcu_gp_seq));
		if (idx == SRCU_STATE_IDLE)
			srcu_gp_start(ssp);
		spin_unlock_irq_rcu_node(ssp->srcu_sup);
		if (idx != SRCU_STATE_IDLE) {
			mutex_unlock(&ssp->srcu_sup->srcu_gp_mutex);
			return; /* Someone else started the grace period. */
		}
	}

	if (rcu_seq_state(READ_ONCE(ssp->srcu_sup->srcu_gp_seq)) == SRCU_STATE_SCAN1) {
		idx = 1 ^ (ssp->srcu_idx & 1);
		if (!try_check_zero(ssp, idx, 1)) {
			mutex_unlock(&ssp->srcu_sup->srcu_gp_mutex);
			return; /* readers present, retry later. */
		}
		srcu_flip(ssp);
		spin_lock_irq_rcu_node(ssp->srcu_sup);
		rcu_seq_set_state(&ssp->srcu_sup->srcu_gp_seq, SRCU_STATE_SCAN2);
		ssp->srcu_sup->srcu_n_exp_nodelay = 0;
		spin_unlock_irq_rcu_node(ssp->srcu_sup);
	}

	if (rcu_seq_state(READ_ONCE(ssp->srcu_sup->srcu_gp_seq)) == SRCU_STATE_SCAN2) {

		/*
		 * SRCU read-side critical sections are normally short,
		 * so check at least twice in quick succession after a flip.
		 */
		idx = 1 ^ (ssp->srcu_idx & 1);
		if (!try_check_zero(ssp, idx, 2)) {
			mutex_unlock(&ssp->srcu_sup->srcu_gp_mutex);
			return; /* readers present, retry later. */
		}
		ssp->srcu_sup->srcu_n_exp_nodelay = 0;
		srcu_gp_end(ssp);  /* Releases ->srcu_gp_mutex. */
	}
}

/*
 * Invoke a limited number of SRCU callbacks that have passed through
 * their grace period.  If there are more to do, SRCU will reschedule
 * the workqueue.  Note that needed memory barriers have been executed
 * in this task's context by srcu_readers_active_idx_check().
 */
static void srcu_invoke_callbacks(struct work_struct *work)
{
	long len;
	bool more;
	struct rcu_cblist ready_cbs;
	struct rcu_head *rhp;
	struct srcu_data *sdp;
	struct srcu_struct *ssp;

	sdp = container_of(work, struct srcu_data, work);

	ssp = sdp->ssp;
	rcu_cblist_init(&ready_cbs);
	spin_lock_irq_rcu_node(sdp);
	WARN_ON_ONCE(!rcu_segcblist_segempty(&sdp->srcu_cblist, RCU_NEXT_TAIL));
	rcu_segcblist_advance(&sdp->srcu_cblist,
			      rcu_seq_current(&ssp->srcu_sup->srcu_gp_seq));
	/*
	 * Although this function is theoretically re-entrant, concurrent
	 * callbacks invocation is disallowed to avoid executing an SRCU barrier
	 * too early.
	 */
	if (sdp->srcu_cblist_invoking ||
	    !rcu_segcblist_ready_cbs(&sdp->srcu_cblist)) {
		spin_unlock_irq_rcu_node(sdp);
		return;  /* Someone else on the job or nothing to do. */
	}

	/* We are on the job!  Extract and invoke ready callbacks. */
	sdp->srcu_cblist_invoking = true;
	rcu_segcblist_extract_done_cbs(&sdp->srcu_cblist, &ready_cbs);
	len = ready_cbs.len;
	spin_unlock_irq_rcu_node(sdp);
	rhp = rcu_cblist_dequeue(&ready_cbs);
	for (; rhp != NULL; rhp = rcu_cblist_dequeue(&ready_cbs)) {
		debug_rcu_head_unqueue(rhp);
		debug_rcu_head_callback(rhp);
		local_bh_disable();
		rhp->func(rhp);
		local_bh_enable();
	}
	WARN_ON_ONCE(ready_cbs.len);

	/*
	 * Update counts, accelerate new callbacks, and if needed,
	 * schedule another round of callback invocation.
	 */
	spin_lock_irq_rcu_node(sdp);
	rcu_segcblist_add_len(&sdp->srcu_cblist, -len);
	sdp->srcu_cblist_invoking = false;
	more = rcu_segcblist_ready_cbs(&sdp->srcu_cblist);
	spin_unlock_irq_rcu_node(sdp);
	/* An SRCU barrier or callbacks from previous nesting work pending */
	if (more)
		srcu_schedule_cbs_sdp(sdp, 0);
}

/*
 * Finished one round of SRCU grace period.  Start another if there are
 * more SRCU callbacks queued, otherwise put SRCU into not-running state.
 */
static void srcu_reschedule(struct srcu_struct *ssp, unsigned long delay)
{
	bool pushgp = true;

	spin_lock_irq_rcu_node(ssp->srcu_sup);
	if (ULONG_CMP_GE(ssp->srcu_sup->srcu_gp_seq, ssp->srcu_sup->srcu_gp_seq_needed)) {
		if (!WARN_ON_ONCE(rcu_seq_state(ssp->srcu_sup->srcu_gp_seq))) {
			/* All requests fulfilled, time to go idle. */
			pushgp = false;
		}
	} else if (!rcu_seq_state(ssp->srcu_sup->srcu_gp_seq)) {
		/* Outstanding request and no GP.  Start one. */
		srcu_gp_start(ssp);
	}
	spin_unlock_irq_rcu_node(ssp->srcu_sup);

	if (pushgp)
		queue_delayed_work(rcu_gp_wq, &ssp->srcu_sup->work, delay);
}

/*
 * This is the work-queue function that handles SRCU grace periods.
 */
static void process_srcu(struct work_struct *work)
{
	unsigned long curdelay;
	unsigned long j;
	struct srcu_struct *ssp;
	struct srcu_usage *sup;

	sup = container_of(work, struct srcu_usage, work.work);
	ssp = sup->srcu_ssp;

	srcu_advance_state(ssp);
	curdelay = srcu_get_delay(ssp);
	if (curdelay) {
		WRITE_ONCE(sup->reschedule_count, 0);
	} else {
		j = jiffies;
		if (READ_ONCE(sup->reschedule_jiffies) == j) {
			WRITE_ONCE(sup->reschedule_count, READ_ONCE(sup->reschedule_count) + 1);
			if (READ_ONCE(sup->reschedule_count) > srcu_max_nodelay)
				curdelay = 1;
		} else {
			WRITE_ONCE(sup->reschedule_count, 1);
			WRITE_ONCE(sup->reschedule_jiffies, j);
		}
	}
	srcu_reschedule(ssp, curdelay);
}

void srcutorture_get_gp_data(enum rcutorture_type test_type,
			     struct srcu_struct *ssp, int *flags,
			     unsigned long *gp_seq)
{
	if (test_type != SRCU_FLAVOR)
		return;
	*flags = 0;
	*gp_seq = rcu_seq_current(&ssp->srcu_sup->srcu_gp_seq);
}
EXPORT_SYMBOL_GPL(srcutorture_get_gp_data);

static const char * const srcu_size_state_name[] = {
	"SRCU_SIZE_SMALL",
	"SRCU_SIZE_ALLOC",
	"SRCU_SIZE_WAIT_BARRIER",
	"SRCU_SIZE_WAIT_CALL",
	"SRCU_SIZE_WAIT_CBS1",
	"SRCU_SIZE_WAIT_CBS2",
	"SRCU_SIZE_WAIT_CBS3",
	"SRCU_SIZE_WAIT_CBS4",
	"SRCU_SIZE_BIG",
	"SRCU_SIZE_???",
};

void srcu_torture_stats_print(struct srcu_struct *ssp, char *tt, char *tf)
{
	int cpu;
	int idx;
	unsigned long s0 = 0, s1 = 0;
	int ss_state = READ_ONCE(ssp->srcu_sup->srcu_size_state);
	int ss_state_idx = ss_state;

	idx = ssp->srcu_idx & 0x1;
	if (ss_state < 0 || ss_state >= ARRAY_SIZE(srcu_size_state_name))
		ss_state_idx = ARRAY_SIZE(srcu_size_state_name) - 1;
	pr_alert("%s%s Tree SRCU g%ld state %d (%s)",
		 tt, tf, rcu_seq_current(&ssp->srcu_sup->srcu_gp_seq), ss_state,
		 srcu_size_state_name[ss_state_idx]);
	if (!ssp->sda) {
		// Called after cleanup_srcu_struct(), perhaps.
		pr_cont(" No per-CPU srcu_data structures (->sda == NULL).\n");
	} else {
		pr_cont(" per-CPU(idx=%d):", idx);
		for_each_possible_cpu(cpu) {
			unsigned long l0, l1;
			unsigned long u0, u1;
			long c0, c1;
			struct srcu_data *sdp;

			sdp = per_cpu_ptr(ssp->sda, cpu);
			u0 = data_race(atomic_long_read(&sdp->srcu_unlock_count[!idx]));
			u1 = data_race(atomic_long_read(&sdp->srcu_unlock_count[idx]));

			/*
			 * Make sure that a lock is always counted if the corresponding
			 * unlock is counted.
			 */
			smp_rmb();

			l0 = data_race(atomic_long_read(&sdp->srcu_lock_count[!idx]));
			l1 = data_race(atomic_long_read(&sdp->srcu_lock_count[idx]));

			c0 = l0 - u0;
			c1 = l1 - u1;
			pr_cont(" %d(%ld,%ld %c)",
				cpu, c0, c1,
				"C."[rcu_segcblist_empty(&sdp->srcu_cblist)]);
			s0 += c0;
			s1 += c1;
		}
		pr_cont(" T(%ld,%ld)\n", s0, s1);
	}
	if (SRCU_SIZING_IS_TORTURE())
		srcu_transition_to_big(ssp);
}
EXPORT_SYMBOL_GPL(srcu_torture_stats_print);

static int __init srcu_bootup_announce(void)
{
	pr_info("Hierarchical SRCU implementation.\n");
	if (exp_holdoff != DEFAULT_SRCU_EXP_HOLDOFF)
		pr_info("\tNon-default auto-expedite holdoff of %lu ns.\n", exp_holdoff);
	if (srcu_retry_check_delay != SRCU_DEFAULT_RETRY_CHECK_DELAY)
		pr_info("\tNon-default retry check delay of %lu us.\n", srcu_retry_check_delay);
	if (srcu_max_nodelay != SRCU_DEFAULT_MAX_NODELAY)
		pr_info("\tNon-default max no-delay of %lu.\n", srcu_max_nodelay);
	pr_info("\tMax phase no-delay instances is %lu.\n", srcu_max_nodelay_phase);
	return 0;
}
early_initcall(srcu_bootup_announce);

void __init srcu_init(void)
{
	struct srcu_usage *sup;

	/* Decide on srcu_struct-size strategy. */
	if (SRCU_SIZING_IS(SRCU_SIZING_AUTO)) {
		if (nr_cpu_ids >= big_cpu_lim) {
			convert_to_big = SRCU_SIZING_INIT; // Don't bother waiting for contention.
			pr_info("%s: Setting srcu_struct sizes to big.\n", __func__);
		} else {
			convert_to_big = SRCU_SIZING_NONE | SRCU_SIZING_CONTEND;
			pr_info("%s: Setting srcu_struct sizes based on contention.\n", __func__);
		}
	}

	/*
	 * Once that is set, call_srcu() can follow the normal path and
	 * queue delayed work. This must follow RCU workqueues creation
	 * and timers initialization.
	 */
	srcu_init_done = true;
	while (!list_empty(&srcu_boot_list)) {
		sup = list_first_entry(&srcu_boot_list, struct srcu_usage,
				      work.work.entry);
		list_del_init(&sup->work.work.entry);
		if (SRCU_SIZING_IS(SRCU_SIZING_INIT) &&
		    sup->srcu_size_state == SRCU_SIZE_SMALL)
			sup->srcu_size_state = SRCU_SIZE_ALLOC;
		queue_work(rcu_gp_wq, &sup->work.work);
	}
}

#ifdef CONFIG_MODULES

/* Initialize any global-scope srcu_struct structures used by this module. */
static int srcu_module_coming(struct module *mod)
{
	int i;
	struct srcu_struct *ssp;
	struct srcu_struct **sspp = mod->srcu_struct_ptrs;

	for (i = 0; i < mod->num_srcu_structs; i++) {
		ssp = *(sspp++);
		ssp->sda = alloc_percpu(struct srcu_data);
		if (WARN_ON_ONCE(!ssp->sda))
			return -ENOMEM;
	}
	return 0;
}

/* Clean up any global-scope srcu_struct structures used by this module. */
static void srcu_module_going(struct module *mod)
{
	int i;
	struct srcu_struct *ssp;
	struct srcu_struct **sspp = mod->srcu_struct_ptrs;

	for (i = 0; i < mod->num_srcu_structs; i++) {
		ssp = *(sspp++);
		if (!rcu_seq_state(smp_load_acquire(&ssp->srcu_sup->srcu_gp_seq_needed)) &&
		    !WARN_ON_ONCE(!ssp->srcu_sup->sda_is_static))
			cleanup_srcu_struct(ssp);
		if (!WARN_ON(srcu_readers_active(ssp)))
			free_percpu(ssp->sda);
	}
}

/* Handle one module, either coming or going. */
static int srcu_module_notify(struct notifier_block *self,
			      unsigned long val, void *data)
{
	struct module *mod = data;
	int ret = 0;

	switch (val) {
	case MODULE_STATE_COMING:
		ret = srcu_module_coming(mod);
		break;
	case MODULE_STATE_GOING:
		srcu_module_going(mod);
		break;
	default:
		break;
	}
	return ret;
}

static struct notifier_block srcu_module_nb = {
	.notifier_call = srcu_module_notify,
	.priority = 0,
};

static __init int init_srcu_module_notifier(void)
{
	int ret;

	ret = register_module_notifier(&srcu_module_nb);
	if (ret)
		pr_warn("Failed to register srcu module notifier\n");
	return ret;
}
late_initcall(init_srcu_module_notifier);

#endif /* #ifdef CONFIG_MODULES */
