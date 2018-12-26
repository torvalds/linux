/*
 * Sleepable Read-Copy Update mechanism for mutual exclusion.
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
 * Copyright (C) IBM Corporation, 2006
 * Copyright (C) Fujitsu, 2012
 *
 * Author: Paul McKenney <paulmck@us.ibm.com>
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

/* Early-boot callback-management, so early that no lock is required! */
static LIST_HEAD(srcu_boot_list);
static bool __read_mostly srcu_init_done;

static void srcu_invoke_callbacks(struct work_struct *work);
static void srcu_reschedule(struct srcu_struct *ssp, unsigned long delay);
static void process_srcu(struct work_struct *work);

/* Wrappers for lock acquisition and release, see raw_spin_lock_rcu_node(). */
#define spin_lock_rcu_node(p)					\
do {									\
	spin_lock(&ACCESS_PRIVATE(p, lock));			\
	smp_mb__after_unlock_lock();					\
} while (0)

#define spin_unlock_rcu_node(p) spin_unlock(&ACCESS_PRIVATE(p, lock))

#define spin_lock_irq_rcu_node(p)					\
do {									\
	spin_lock_irq(&ACCESS_PRIVATE(p, lock));			\
	smp_mb__after_unlock_lock();					\
} while (0)

#define spin_unlock_irq_rcu_node(p)					\
	spin_unlock_irq(&ACCESS_PRIVATE(p, lock))

#define spin_lock_irqsave_rcu_node(p, flags)			\
do {									\
	spin_lock_irqsave(&ACCESS_PRIVATE(p, lock), flags);	\
	smp_mb__after_unlock_lock();					\
} while (0)

#define spin_unlock_irqrestore_rcu_node(p, flags)			\
	spin_unlock_irqrestore(&ACCESS_PRIVATE(p, lock), flags)	\

/*
 * Initialize SRCU combining tree.  Note that statically allocated
 * srcu_struct structures might already have srcu_read_lock() and
 * srcu_read_unlock() running against them.  So if the is_static parameter
 * is set, don't initialize ->srcu_lock_count[] and ->srcu_unlock_count[].
 */
static void init_srcu_struct_nodes(struct srcu_struct *ssp, bool is_static)
{
	int cpu;
	int i;
	int level = 0;
	int levelspread[RCU_NUM_LVLS];
	struct srcu_data *sdp;
	struct srcu_node *snp;
	struct srcu_node *snp_first;

	/* Work out the overall tree geometry. */
	ssp->level[0] = &ssp->node[0];
	for (i = 1; i < rcu_num_lvls; i++)
		ssp->level[i] = ssp->level[i - 1] + num_rcu_lvl[i - 1];
	rcu_init_levelspread(levelspread, num_rcu_lvl);

	/* Each pass through this loop initializes one srcu_node structure. */
	srcu_for_each_node_breadth_first(ssp, snp) {
		spin_lock_init(&ACCESS_PRIVATE(snp, lock));
		WARN_ON_ONCE(ARRAY_SIZE(snp->srcu_have_cbs) !=
			     ARRAY_SIZE(snp->srcu_data_have_cbs));
		for (i = 0; i < ARRAY_SIZE(snp->srcu_have_cbs); i++) {
			snp->srcu_have_cbs[i] = 0;
			snp->srcu_data_have_cbs[i] = 0;
		}
		snp->srcu_gp_seq_needed_exp = 0;
		snp->grplo = -1;
		snp->grphi = -1;
		if (snp == &ssp->node[0]) {
			/* Root node, special case. */
			snp->srcu_parent = NULL;
			continue;
		}

		/* Non-root node. */
		if (snp == ssp->level[level + 1])
			level++;
		snp->srcu_parent = ssp->level[level - 1] +
				   (snp - ssp->level[level]) /
				   levelspread[level - 1];
	}

	/*
	 * Initialize the per-CPU srcu_data array, which feeds into the
	 * leaves of the srcu_node tree.
	 */
	WARN_ON_ONCE(ARRAY_SIZE(sdp->srcu_lock_count) !=
		     ARRAY_SIZE(sdp->srcu_unlock_count));
	level = rcu_num_lvls - 1;
	snp_first = ssp->level[level];
	for_each_possible_cpu(cpu) {
		sdp = per_cpu_ptr(ssp->sda, cpu);
		spin_lock_init(&ACCESS_PRIVATE(sdp, lock));
		rcu_segcblist_init(&sdp->srcu_cblist);
		sdp->srcu_cblist_invoking = false;
		sdp->srcu_gp_seq_needed = ssp->srcu_gp_seq;
		sdp->srcu_gp_seq_needed_exp = ssp->srcu_gp_seq;
		sdp->mynode = &snp_first[cpu / levelspread[level]];
		for (snp = sdp->mynode; snp != NULL; snp = snp->srcu_parent) {
			if (snp->grplo < 0)
				snp->grplo = cpu;
			snp->grphi = cpu;
		}
		sdp->cpu = cpu;
		INIT_DELAYED_WORK(&sdp->work, srcu_invoke_callbacks);
		sdp->ssp = ssp;
		sdp->grpmask = 1 << (cpu - sdp->mynode->grplo);
		if (is_static)
			continue;

		/* Dynamically allocated, better be no srcu_read_locks()! */
		for (i = 0; i < ARRAY_SIZE(sdp->srcu_lock_count); i++) {
			sdp->srcu_lock_count[i] = 0;
			sdp->srcu_unlock_count[i] = 0;
		}
	}
}

/*
 * Initialize non-compile-time initialized fields, including the
 * associated srcu_node and srcu_data structures.  The is_static
 * parameter is passed through to init_srcu_struct_nodes(), and
 * also tells us that ->sda has already been wired up to srcu_data.
 */
static int init_srcu_struct_fields(struct srcu_struct *ssp, bool is_static)
{
	mutex_init(&ssp->srcu_cb_mutex);
	mutex_init(&ssp->srcu_gp_mutex);
	ssp->srcu_idx = 0;
	ssp->srcu_gp_seq = 0;
	ssp->srcu_barrier_seq = 0;
	mutex_init(&ssp->srcu_barrier_mutex);
	atomic_set(&ssp->srcu_barrier_cpu_cnt, 0);
	INIT_DELAYED_WORK(&ssp->work, process_srcu);
	if (!is_static)
		ssp->sda = alloc_percpu(struct srcu_data);
	init_srcu_struct_nodes(ssp, is_static);
	ssp->srcu_gp_seq_needed_exp = 0;
	ssp->srcu_last_gp_end = ktime_get_mono_fast_ns();
	smp_store_release(&ssp->srcu_gp_seq_needed, 0); /* Init done. */
	return ssp->sda ? 0 : -ENOMEM;
}

#ifdef CONFIG_DEBUG_LOCK_ALLOC

int __init_srcu_struct(struct srcu_struct *ssp, const char *name,
		       struct lock_class_key *key)
{
	/* Don't re-initialize a lock while it is held. */
	debug_check_no_locks_freed((void *)ssp, sizeof(*ssp));
	lockdep_init_map(&ssp->dep_map, name, key, 0);
	spin_lock_init(&ACCESS_PRIVATE(ssp, lock));
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
	spin_lock_init(&ACCESS_PRIVATE(ssp, lock));
	return init_srcu_struct_fields(ssp, false);
}
EXPORT_SYMBOL_GPL(init_srcu_struct);

#endif /* #else #ifdef CONFIG_DEBUG_LOCK_ALLOC */

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
	if (!rcu_seq_state(smp_load_acquire(&ssp->srcu_gp_seq_needed))) /*^^^*/
		return; /* Already initialized. */
	spin_lock_irqsave_rcu_node(ssp, flags);
	if (!rcu_seq_state(ssp->srcu_gp_seq_needed)) {
		spin_unlock_irqrestore_rcu_node(ssp, flags);
		return;
	}
	init_srcu_struct_fields(ssp, true);
	spin_unlock_irqrestore_rcu_node(ssp, flags);
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

		sum += READ_ONCE(cpuc->srcu_lock_count[idx]);
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
	unsigned long sum = 0;

	for_each_possible_cpu(cpu) {
		struct srcu_data *cpuc = per_cpu_ptr(ssp->sda, cpu);

		sum += READ_ONCE(cpuc->srcu_unlock_count[idx]);
	}
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
	 * been no readers on this index at some time in between. This does
	 * not mean that there are no more readers, as one could have read
	 * the current index but not have incremented the lock counter yet.
	 *
	 * So suppose that the updater is preempted here for so long
	 * that more than ULONG_MAX non-nested readers come and go in
	 * the meantime.  It turns out that this cannot result in overflow
	 * because if a reader modifies its unlock count after we read it
	 * above, then that reader's next load of ->srcu_idx is guaranteed
	 * to get the new value, which will cause it to operate on the
	 * other bank of counters, where it cannot contribute to the
	 * overflow of these counters.  This means that there is a maximum
	 * of 2*NR_CPUS increments, which cannot overflow given current
	 * systems, especially not on 64-bit systems.
	 *
	 * OK, how about nesting?  This does impose a limit on nesting
	 * of floor(ULONG_MAX/NR_CPUS/2), which should be sufficient,
	 * especially on 64-bit systems.
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

		sum += READ_ONCE(cpuc->srcu_lock_count[0]);
		sum += READ_ONCE(cpuc->srcu_lock_count[1]);
		sum -= READ_ONCE(cpuc->srcu_unlock_count[0]);
		sum -= READ_ONCE(cpuc->srcu_unlock_count[1]);
	}
	return sum;
}

#define SRCU_INTERVAL		1

/*
 * Return grace-period delay, zero if there are expedited grace
 * periods pending, SRCU_INTERVAL otherwise.
 */
static unsigned long srcu_get_delay(struct srcu_struct *ssp)
{
	if (ULONG_CMP_LT(READ_ONCE(ssp->srcu_gp_seq),
			 READ_ONCE(ssp->srcu_gp_seq_needed_exp)))
		return 0;
	return SRCU_INTERVAL;
}

/* Helper for cleanup_srcu_struct() and cleanup_srcu_struct_quiesced(). */
void _cleanup_srcu_struct(struct srcu_struct *ssp, bool quiesced)
{
	int cpu;

	if (WARN_ON(!srcu_get_delay(ssp)))
		return; /* Just leak it! */
	if (WARN_ON(srcu_readers_active(ssp)))
		return; /* Just leak it! */
	if (quiesced) {
		if (WARN_ON(delayed_work_pending(&ssp->work)))
			return; /* Just leak it! */
	} else {
		flush_delayed_work(&ssp->work);
	}
	for_each_possible_cpu(cpu)
		if (quiesced) {
			if (WARN_ON(delayed_work_pending(&per_cpu_ptr(ssp->sda, cpu)->work)))
				return; /* Just leak it! */
		} else {
			flush_delayed_work(&per_cpu_ptr(ssp->sda, cpu)->work);
		}
	if (WARN_ON(rcu_seq_state(READ_ONCE(ssp->srcu_gp_seq)) != SRCU_STATE_IDLE) ||
	    WARN_ON(srcu_readers_active(ssp))) {
		pr_info("%s: Active srcu_struct %p state: %d\n",
			__func__, ssp, rcu_seq_state(READ_ONCE(ssp->srcu_gp_seq)));
		return; /* Caller forgot to stop doing call_srcu()? */
	}
	free_percpu(ssp->sda);
	ssp->sda = NULL;
}
EXPORT_SYMBOL_GPL(_cleanup_srcu_struct);

/*
 * Counts the new reader in the appropriate per-CPU element of the
 * srcu_struct.
 * Returns an index that must be passed to the matching srcu_read_unlock().
 */
int __srcu_read_lock(struct srcu_struct *ssp)
{
	int idx;

	idx = READ_ONCE(ssp->srcu_idx) & 0x1;
	this_cpu_inc(ssp->sda->srcu_lock_count[idx]);
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
	this_cpu_inc(ssp->sda->srcu_unlock_count[idx]);
}
EXPORT_SYMBOL_GPL(__srcu_read_unlock);

/*
 * We use an adaptive strategy for synchronize_srcu() and especially for
 * synchronize_srcu_expedited().  We spin for a fixed time period
 * (defined below) to allow SRCU readers to exit their read-side critical
 * sections.  If there are still some readers after a few microseconds,
 * we repeatedly block for 1-millisecond time periods.
 */
#define SRCU_RETRY_CHECK_DELAY		5

/*
 * Start an SRCU grace period.
 */
static void srcu_gp_start(struct srcu_struct *ssp)
{
	struct srcu_data *sdp = this_cpu_ptr(ssp->sda);
	int state;

	lockdep_assert_held(&ACCESS_PRIVATE(ssp, lock));
	WARN_ON_ONCE(ULONG_CMP_GE(ssp->srcu_gp_seq, ssp->srcu_gp_seq_needed));
	spin_lock_rcu_node(sdp);  /* Interrupts already disabled. */
	rcu_segcblist_advance(&sdp->srcu_cblist,
			      rcu_seq_current(&ssp->srcu_gp_seq));
	(void)rcu_segcblist_accelerate(&sdp->srcu_cblist,
				       rcu_seq_snap(&ssp->srcu_gp_seq));
	spin_unlock_rcu_node(sdp);  /* Interrupts remain disabled. */
	smp_mb(); /* Order prior store to ->srcu_gp_seq_needed vs. GP start. */
	rcu_seq_start(&ssp->srcu_gp_seq);
	state = rcu_seq_state(READ_ONCE(ssp->srcu_gp_seq));
	WARN_ON_ONCE(state != SRCU_STATE_SCAN1);
}

/*
 * Track online CPUs to guide callback workqueue placement.
 */
DEFINE_PER_CPU(bool, srcu_online);

void srcu_online_cpu(unsigned int cpu)
{
	WRITE_ONCE(per_cpu(srcu_online, cpu), true);
}

void srcu_offline_cpu(unsigned int cpu)
{
	WRITE_ONCE(per_cpu(srcu_online, cpu), false);
}

/*
 * Place the workqueue handler on the specified CPU if online, otherwise
 * just run it whereever.  This is useful for placing workqueue handlers
 * that are to invoke the specified CPU's callbacks.
 */
static bool srcu_queue_delayed_work_on(int cpu, struct workqueue_struct *wq,
				       struct delayed_work *dwork,
				       unsigned long delay)
{
	bool ret;

	preempt_disable();
	if (READ_ONCE(per_cpu(srcu_online, cpu)))
		ret = queue_delayed_work_on(cpu, wq, dwork, delay);
	else
		ret = queue_delayed_work(wq, dwork, delay);
	preempt_enable();
	return ret;
}

/*
 * Schedule callback invocation for the specified srcu_data structure,
 * if possible, on the corresponding CPU.
 */
static void srcu_schedule_cbs_sdp(struct srcu_data *sdp, unsigned long delay)
{
	srcu_queue_delayed_work_on(sdp->cpu, rcu_gp_wq, &sdp->work, delay);
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
		if (!(mask & (1 << (cpu - snp->grplo))))
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
	unsigned long cbdelay;
	bool cbs;
	bool last_lvl;
	int cpu;
	unsigned long flags;
	unsigned long gpseq;
	int idx;
	unsigned long mask;
	struct srcu_data *sdp;
	struct srcu_node *snp;

	/* Prevent more than one additional grace period. */
	mutex_lock(&ssp->srcu_cb_mutex);

	/* End the current grace period. */
	spin_lock_irq_rcu_node(ssp);
	idx = rcu_seq_state(ssp->srcu_gp_seq);
	WARN_ON_ONCE(idx != SRCU_STATE_SCAN2);
	cbdelay = srcu_get_delay(ssp);
	ssp->srcu_last_gp_end = ktime_get_mono_fast_ns();
	rcu_seq_end(&ssp->srcu_gp_seq);
	gpseq = rcu_seq_current(&ssp->srcu_gp_seq);
	if (ULONG_CMP_LT(ssp->srcu_gp_seq_needed_exp, gpseq))
		ssp->srcu_gp_seq_needed_exp = gpseq;
	spin_unlock_irq_rcu_node(ssp);
	mutex_unlock(&ssp->srcu_gp_mutex);
	/* A new grace period can start at this point.  But only one. */

	/* Initiate callback invocation as needed. */
	idx = rcu_seq_ctr(gpseq) % ARRAY_SIZE(snp->srcu_have_cbs);
	srcu_for_each_node_breadth_first(ssp, snp) {
		spin_lock_irq_rcu_node(snp);
		cbs = false;
		last_lvl = snp >= ssp->level[rcu_num_lvls - 1];
		if (last_lvl)
			cbs = snp->srcu_have_cbs[idx] == gpseq;
		snp->srcu_have_cbs[idx] = gpseq;
		rcu_seq_set_state(&snp->srcu_have_cbs[idx], 1);
		if (ULONG_CMP_LT(snp->srcu_gp_seq_needed_exp, gpseq))
			snp->srcu_gp_seq_needed_exp = gpseq;
		mask = snp->srcu_data_have_cbs[idx];
		snp->srcu_data_have_cbs[idx] = 0;
		spin_unlock_irq_rcu_node(snp);
		if (cbs)
			srcu_schedule_cbs_snp(ssp, snp, mask, cbdelay);

		/* Occasionally prevent srcu_data counter wrap. */
		if (!(gpseq & counter_wrap_check) && last_lvl)
			for (cpu = snp->grplo; cpu <= snp->grphi; cpu++) {
				sdp = per_cpu_ptr(ssp->sda, cpu);
				spin_lock_irqsave_rcu_node(sdp, flags);
				if (ULONG_CMP_GE(gpseq,
						 sdp->srcu_gp_seq_needed + 100))
					sdp->srcu_gp_seq_needed = gpseq;
				if (ULONG_CMP_GE(gpseq,
						 sdp->srcu_gp_seq_needed_exp + 100))
					sdp->srcu_gp_seq_needed_exp = gpseq;
				spin_unlock_irqrestore_rcu_node(sdp, flags);
			}
	}

	/* Callback initiation done, allow grace periods after next. */
	mutex_unlock(&ssp->srcu_cb_mutex);

	/* Start a new grace period if needed. */
	spin_lock_irq_rcu_node(ssp);
	gpseq = rcu_seq_current(&ssp->srcu_gp_seq);
	if (!rcu_seq_state(gpseq) &&
	    ULONG_CMP_LT(gpseq, ssp->srcu_gp_seq_needed)) {
		srcu_gp_start(ssp);
		spin_unlock_irq_rcu_node(ssp);
		srcu_reschedule(ssp, 0);
	} else {
		spin_unlock_irq_rcu_node(ssp);
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

	for (; snp != NULL; snp = snp->srcu_parent) {
		if (rcu_seq_done(&ssp->srcu_gp_seq, s) ||
		    ULONG_CMP_GE(READ_ONCE(snp->srcu_gp_seq_needed_exp), s))
			return;
		spin_lock_irqsave_rcu_node(snp, flags);
		if (ULONG_CMP_GE(snp->srcu_gp_seq_needed_exp, s)) {
			spin_unlock_irqrestore_rcu_node(snp, flags);
			return;
		}
		WRITE_ONCE(snp->srcu_gp_seq_needed_exp, s);
		spin_unlock_irqrestore_rcu_node(snp, flags);
	}
	spin_lock_irqsave_rcu_node(ssp, flags);
	if (ULONG_CMP_LT(ssp->srcu_gp_seq_needed_exp, s))
		ssp->srcu_gp_seq_needed_exp = s;
	spin_unlock_irqrestore_rcu_node(ssp, flags);
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
 */
static void srcu_funnel_gp_start(struct srcu_struct *ssp, struct srcu_data *sdp,
				 unsigned long s, bool do_norm)
{
	unsigned long flags;
	int idx = rcu_seq_ctr(s) % ARRAY_SIZE(sdp->mynode->srcu_have_cbs);
	struct srcu_node *snp = sdp->mynode;
	unsigned long snp_seq;

	/* Each pass through the loop does one level of the srcu_node tree. */
	for (; snp != NULL; snp = snp->srcu_parent) {
		if (rcu_seq_done(&ssp->srcu_gp_seq, s) && snp != sdp->mynode)
			return; /* GP already done and CBs recorded. */
		spin_lock_irqsave_rcu_node(snp, flags);
		if (ULONG_CMP_GE(snp->srcu_have_cbs[idx], s)) {
			snp_seq = snp->srcu_have_cbs[idx];
			if (snp == sdp->mynode && snp_seq == s)
				snp->srcu_data_have_cbs[idx] |= sdp->grpmask;
			spin_unlock_irqrestore_rcu_node(snp, flags);
			if (snp == sdp->mynode && snp_seq != s) {
				srcu_schedule_cbs_sdp(sdp, do_norm
							   ? SRCU_INTERVAL
							   : 0);
				return;
			}
			if (!do_norm)
				srcu_funnel_exp_start(ssp, snp, s);
			return;
		}
		snp->srcu_have_cbs[idx] = s;
		if (snp == sdp->mynode)
			snp->srcu_data_have_cbs[idx] |= sdp->grpmask;
		if (!do_norm && ULONG_CMP_LT(snp->srcu_gp_seq_needed_exp, s))
			snp->srcu_gp_seq_needed_exp = s;
		spin_unlock_irqrestore_rcu_node(snp, flags);
	}

	/* Top of tree, must ensure the grace period will be started. */
	spin_lock_irqsave_rcu_node(ssp, flags);
	if (ULONG_CMP_LT(ssp->srcu_gp_seq_needed, s)) {
		/*
		 * Record need for grace period s.  Pair with load
		 * acquire setting up for initialization.
		 */
		smp_store_release(&ssp->srcu_gp_seq_needed, s); /*^^^*/
	}
	if (!do_norm && ULONG_CMP_LT(ssp->srcu_gp_seq_needed_exp, s))
		ssp->srcu_gp_seq_needed_exp = s;

	/* If grace period not already done and none in progress, start it. */
	if (!rcu_seq_done(&ssp->srcu_gp_seq, s) &&
	    rcu_seq_state(ssp->srcu_gp_seq) == SRCU_STATE_IDLE) {
		WARN_ON_ONCE(ULONG_CMP_GE(ssp->srcu_gp_seq, ssp->srcu_gp_seq_needed));
		srcu_gp_start(ssp);
		if (likely(srcu_init_done))
			queue_delayed_work(rcu_gp_wq, &ssp->work,
					   srcu_get_delay(ssp));
		else if (list_empty(&ssp->work.work.entry))
			list_add(&ssp->work.work.entry, &srcu_boot_list);
	}
	spin_unlock_irqrestore_rcu_node(ssp, flags);
}

/*
 * Wait until all readers counted by array index idx complete, but
 * loop an additional time if there is an expedited grace period pending.
 * The caller must ensure that ->srcu_idx is not changed while checking.
 */
static bool try_check_zero(struct srcu_struct *ssp, int idx, int trycount)
{
	for (;;) {
		if (srcu_readers_active_idx_check(ssp, idx))
			return true;
		if (--trycount + !srcu_get_delay(ssp) <= 0)
			return false;
		udelay(SRCU_RETRY_CHECK_DELAY);
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
	 * Ensure that if this updater saw a given reader's increment
	 * from __srcu_read_lock(), that reader was using an old value
	 * of ->srcu_idx.  Also ensure that if a given reader sees the
	 * new value of ->srcu_idx, this updater's earlier scans cannot
	 * have seen that reader's increments (which is OK, because this
	 * grace period need not wait on that reader).
	 */
	smp_mb(); /* E */  /* Pairs with B and C. */

	WRITE_ONCE(ssp->srcu_idx, ssp->srcu_idx + 1);

	/*
	 * Ensure that if the updater misses an __srcu_read_unlock()
	 * increment, that task's next __srcu_read_lock() will see the
	 * above counter update.  Note that both this memory barrier
	 * and the one in srcu_readers_active_idx_check() provide the
	 * guarantee for __srcu_read_lock().
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
 * negligible when amoritized over that time period, and the extra latency
 * of a needlessly non-expedited grace period is similarly negligible.
 */
static bool srcu_might_be_idle(struct srcu_struct *ssp)
{
	unsigned long curseq;
	unsigned long flags;
	struct srcu_data *sdp;
	unsigned long t;

	/* If the local srcu_data structure has callbacks, not idle.  */
	local_irq_save(flags);
	sdp = this_cpu_ptr(ssp->sda);
	if (rcu_segcblist_pend_cbs(&sdp->srcu_cblist)) {
		local_irq_restore(flags);
		return false; /* Callbacks already present, so not idle. */
	}
	local_irq_restore(flags);

	/*
	 * No local callbacks, so probabalistically probe global state.
	 * Exact information would require acquiring locks, which would
	 * kill scalability, hence the probabalistic nature of the probe.
	 */

	/* First, see if enough time has passed since the last GP. */
	t = ktime_get_mono_fast_ns();
	if (exp_holdoff == 0 ||
	    time_in_range_open(t, ssp->srcu_last_gp_end,
			       ssp->srcu_last_gp_end + exp_holdoff))
		return false; /* Too soon after last GP. */

	/* Next, check for probable idleness. */
	curseq = rcu_seq_current(&ssp->srcu_gp_seq);
	smp_mb(); /* Order ->srcu_gp_seq with ->srcu_gp_seq_needed. */
	if (ULONG_CMP_LT(curseq, READ_ONCE(ssp->srcu_gp_seq_needed)))
		return false; /* Grace period in progress, so not idle. */
	smp_mb(); /* Order ->srcu_gp_seq with prior access. */
	if (curseq != rcu_seq_current(&ssp->srcu_gp_seq))
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
void __call_srcu(struct srcu_struct *ssp, struct rcu_head *rhp,
		 rcu_callback_t func, bool do_norm)
{
	unsigned long flags;
	int idx;
	bool needexp = false;
	bool needgp = false;
	unsigned long s;
	struct srcu_data *sdp;

	check_init_srcu_struct(ssp);
	if (debug_rcu_head_queue(rhp)) {
		/* Probable double call_srcu(), so leak the callback. */
		WRITE_ONCE(rhp->func, srcu_leak_callback);
		WARN_ONCE(1, "call_srcu(): Leaked duplicate callback\n");
		return;
	}
	rhp->func = func;
	idx = srcu_read_lock(ssp);
	local_irq_save(flags);
	sdp = this_cpu_ptr(ssp->sda);
	spin_lock_rcu_node(sdp);
	rcu_segcblist_enqueue(&sdp->srcu_cblist, rhp, false);
	rcu_segcblist_advance(&sdp->srcu_cblist,
			      rcu_seq_current(&ssp->srcu_gp_seq));
	s = rcu_seq_snap(&ssp->srcu_gp_seq);
	(void)rcu_segcblist_accelerate(&sdp->srcu_cblist, s);
	if (ULONG_CMP_LT(sdp->srcu_gp_seq_needed, s)) {
		sdp->srcu_gp_seq_needed = s;
		needgp = true;
	}
	if (!do_norm && ULONG_CMP_LT(sdp->srcu_gp_seq_needed_exp, s)) {
		sdp->srcu_gp_seq_needed_exp = s;
		needexp = true;
	}
	spin_unlock_irqrestore_rcu_node(sdp, flags);
	if (needgp)
		srcu_funnel_gp_start(ssp, sdp, s, do_norm);
	else if (needexp)
		srcu_funnel_exp_start(ssp, sdp->mynode, s);
	srcu_read_unlock(ssp, idx);
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

	RCU_LOCKDEP_WARN(lock_is_held(&ssp->dep_map) ||
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

/*
 * Callback function for srcu_barrier() use.
 */
static void srcu_barrier_cb(struct rcu_head *rhp)
{
	struct srcu_data *sdp;
	struct srcu_struct *ssp;

	sdp = container_of(rhp, struct srcu_data, srcu_barrier_head);
	ssp = sdp->ssp;
	if (atomic_dec_and_test(&ssp->srcu_barrier_cpu_cnt))
		complete(&ssp->srcu_barrier_completion);
}

/**
 * srcu_barrier - Wait until all in-flight call_srcu() callbacks complete.
 * @ssp: srcu_struct on which to wait for in-flight callbacks.
 */
void srcu_barrier(struct srcu_struct *ssp)
{
	int cpu;
	struct srcu_data *sdp;
	unsigned long s = rcu_seq_snap(&ssp->srcu_barrier_seq);

	check_init_srcu_struct(ssp);
	mutex_lock(&ssp->srcu_barrier_mutex);
	if (rcu_seq_done(&ssp->srcu_barrier_seq, s)) {
		smp_mb(); /* Force ordering following return. */
		mutex_unlock(&ssp->srcu_barrier_mutex);
		return; /* Someone else did our work for us. */
	}
	rcu_seq_start(&ssp->srcu_barrier_seq);
	init_completion(&ssp->srcu_barrier_completion);

	/* Initial count prevents reaching zero until all CBs are posted. */
	atomic_set(&ssp->srcu_barrier_cpu_cnt, 1);

	/*
	 * Each pass through this loop enqueues a callback, but only
	 * on CPUs already having callbacks enqueued.  Note that if
	 * a CPU already has callbacks enqueue, it must have already
	 * registered the need for a future grace period, so all we
	 * need do is enqueue a callback that will use the same
	 * grace period as the last callback already in the queue.
	 */
	for_each_possible_cpu(cpu) {
		sdp = per_cpu_ptr(ssp->sda, cpu);
		spin_lock_irq_rcu_node(sdp);
		atomic_inc(&ssp->srcu_barrier_cpu_cnt);
		sdp->srcu_barrier_head.func = srcu_barrier_cb;
		debug_rcu_head_queue(&sdp->srcu_barrier_head);
		if (!rcu_segcblist_entrain(&sdp->srcu_cblist,
					   &sdp->srcu_barrier_head, 0)) {
			debug_rcu_head_unqueue(&sdp->srcu_barrier_head);
			atomic_dec(&ssp->srcu_barrier_cpu_cnt);
		}
		spin_unlock_irq_rcu_node(sdp);
	}

	/* Remove the initial count, at which point reaching zero can happen. */
	if (atomic_dec_and_test(&ssp->srcu_barrier_cpu_cnt))
		complete(&ssp->srcu_barrier_completion);
	wait_for_completion(&ssp->srcu_barrier_completion);

	rcu_seq_end(&ssp->srcu_barrier_seq);
	mutex_unlock(&ssp->srcu_barrier_mutex);
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
	return ssp->srcu_idx;
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

	mutex_lock(&ssp->srcu_gp_mutex);

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
	idx = rcu_seq_state(smp_load_acquire(&ssp->srcu_gp_seq)); /* ^^^ */
	if (idx == SRCU_STATE_IDLE) {
		spin_lock_irq_rcu_node(ssp);
		if (ULONG_CMP_GE(ssp->srcu_gp_seq, ssp->srcu_gp_seq_needed)) {
			WARN_ON_ONCE(rcu_seq_state(ssp->srcu_gp_seq));
			spin_unlock_irq_rcu_node(ssp);
			mutex_unlock(&ssp->srcu_gp_mutex);
			return;
		}
		idx = rcu_seq_state(READ_ONCE(ssp->srcu_gp_seq));
		if (idx == SRCU_STATE_IDLE)
			srcu_gp_start(ssp);
		spin_unlock_irq_rcu_node(ssp);
		if (idx != SRCU_STATE_IDLE) {
			mutex_unlock(&ssp->srcu_gp_mutex);
			return; /* Someone else started the grace period. */
		}
	}

	if (rcu_seq_state(READ_ONCE(ssp->srcu_gp_seq)) == SRCU_STATE_SCAN1) {
		idx = 1 ^ (ssp->srcu_idx & 1);
		if (!try_check_zero(ssp, idx, 1)) {
			mutex_unlock(&ssp->srcu_gp_mutex);
			return; /* readers present, retry later. */
		}
		srcu_flip(ssp);
		rcu_seq_set_state(&ssp->srcu_gp_seq, SRCU_STATE_SCAN2);
	}

	if (rcu_seq_state(READ_ONCE(ssp->srcu_gp_seq)) == SRCU_STATE_SCAN2) {

		/*
		 * SRCU read-side critical sections are normally short,
		 * so check at least twice in quick succession after a flip.
		 */
		idx = 1 ^ (ssp->srcu_idx & 1);
		if (!try_check_zero(ssp, idx, 2)) {
			mutex_unlock(&ssp->srcu_gp_mutex);
			return; /* readers present, retry later. */
		}
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
	bool more;
	struct rcu_cblist ready_cbs;
	struct rcu_head *rhp;
	struct srcu_data *sdp;
	struct srcu_struct *ssp;

	sdp = container_of(work, struct srcu_data, work.work);
	ssp = sdp->ssp;
	rcu_cblist_init(&ready_cbs);
	spin_lock_irq_rcu_node(sdp);
	rcu_segcblist_advance(&sdp->srcu_cblist,
			      rcu_seq_current(&ssp->srcu_gp_seq));
	if (sdp->srcu_cblist_invoking ||
	    !rcu_segcblist_ready_cbs(&sdp->srcu_cblist)) {
		spin_unlock_irq_rcu_node(sdp);
		return;  /* Someone else on the job or nothing to do. */
	}

	/* We are on the job!  Extract and invoke ready callbacks. */
	sdp->srcu_cblist_invoking = true;
	rcu_segcblist_extract_done_cbs(&sdp->srcu_cblist, &ready_cbs);
	spin_unlock_irq_rcu_node(sdp);
	rhp = rcu_cblist_dequeue(&ready_cbs);
	for (; rhp != NULL; rhp = rcu_cblist_dequeue(&ready_cbs)) {
		debug_rcu_head_unqueue(rhp);
		local_bh_disable();
		rhp->func(rhp);
		local_bh_enable();
	}

	/*
	 * Update counts, accelerate new callbacks, and if needed,
	 * schedule another round of callback invocation.
	 */
	spin_lock_irq_rcu_node(sdp);
	rcu_segcblist_insert_count(&sdp->srcu_cblist, &ready_cbs);
	(void)rcu_segcblist_accelerate(&sdp->srcu_cblist,
				       rcu_seq_snap(&ssp->srcu_gp_seq));
	sdp->srcu_cblist_invoking = false;
	more = rcu_segcblist_ready_cbs(&sdp->srcu_cblist);
	spin_unlock_irq_rcu_node(sdp);
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

	spin_lock_irq_rcu_node(ssp);
	if (ULONG_CMP_GE(ssp->srcu_gp_seq, ssp->srcu_gp_seq_needed)) {
		if (!WARN_ON_ONCE(rcu_seq_state(ssp->srcu_gp_seq))) {
			/* All requests fulfilled, time to go idle. */
			pushgp = false;
		}
	} else if (!rcu_seq_state(ssp->srcu_gp_seq)) {
		/* Outstanding request and no GP.  Start one. */
		srcu_gp_start(ssp);
	}
	spin_unlock_irq_rcu_node(ssp);

	if (pushgp)
		queue_delayed_work(rcu_gp_wq, &ssp->work, delay);
}

/*
 * This is the work-queue function that handles SRCU grace periods.
 */
static void process_srcu(struct work_struct *work)
{
	struct srcu_struct *ssp;

	ssp = container_of(work, struct srcu_struct, work.work);

	srcu_advance_state(ssp);
	srcu_reschedule(ssp, srcu_get_delay(ssp));
}

void srcutorture_get_gp_data(enum rcutorture_type test_type,
			     struct srcu_struct *ssp, int *flags,
			     unsigned long *gp_seq)
{
	if (test_type != SRCU_FLAVOR)
		return;
	*flags = 0;
	*gp_seq = rcu_seq_current(&ssp->srcu_gp_seq);
}
EXPORT_SYMBOL_GPL(srcutorture_get_gp_data);

void srcu_torture_stats_print(struct srcu_struct *ssp, char *tt, char *tf)
{
	int cpu;
	int idx;
	unsigned long s0 = 0, s1 = 0;

	idx = ssp->srcu_idx & 0x1;
	pr_alert("%s%s Tree SRCU g%ld per-CPU(idx=%d):",
		 tt, tf, rcu_seq_current(&ssp->srcu_gp_seq), idx);
	for_each_possible_cpu(cpu) {
		unsigned long l0, l1;
		unsigned long u0, u1;
		long c0, c1;
		struct srcu_data *sdp;

		sdp = per_cpu_ptr(ssp->sda, cpu);
		u0 = sdp->srcu_unlock_count[!idx];
		u1 = sdp->srcu_unlock_count[idx];

		/*
		 * Make sure that a lock is always counted if the corresponding
		 * unlock is counted.
		 */
		smp_rmb();

		l0 = sdp->srcu_lock_count[!idx];
		l1 = sdp->srcu_lock_count[idx];

		c0 = l0 - u0;
		c1 = l1 - u1;
		pr_cont(" %d(%ld,%ld %1p)",
			cpu, c0, c1, rcu_segcblist_head(&sdp->srcu_cblist));
		s0 += c0;
		s1 += c1;
	}
	pr_cont(" T(%ld,%ld)\n", s0, s1);
}
EXPORT_SYMBOL_GPL(srcu_torture_stats_print);

static int __init srcu_bootup_announce(void)
{
	pr_info("Hierarchical SRCU implementation.\n");
	if (exp_holdoff != DEFAULT_SRCU_EXP_HOLDOFF)
		pr_info("\tNon-default auto-expedite holdoff of %lu ns.\n", exp_holdoff);
	return 0;
}
early_initcall(srcu_bootup_announce);

void __init srcu_init(void)
{
	struct srcu_struct *ssp;

	srcu_init_done = true;
	while (!list_empty(&srcu_boot_list)) {
		ssp = list_first_entry(&srcu_boot_list, struct srcu_struct,
				      work.work.entry);
		check_init_srcu_struct(ssp);
		list_del_init(&ssp->work.work.entry);
		queue_work(rcu_gp_wq, &ssp->work.work);
	}
}
