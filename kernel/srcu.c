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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) IBM Corporation, 2006
 *
 * Author: Paul McKenney <paulmck@us.ibm.com>
 *
 * For detailed explanation of Read-Copy Update mechanism see -
 * 		Documentation/RCU/ *.txt
 *
 */

#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/percpu.h>
#include <linux/preempt.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/srcu.h>

/**
 * init_srcu_struct - initialize a sleep-RCU structure
 * @sp: structure to initialize.
 *
 * Must invoke this on a given srcu_struct before passing that srcu_struct
 * to any other function.  Each srcu_struct represents a separate domain
 * of SRCU protection.
 */
int init_srcu_struct(struct srcu_struct *sp)
{
	sp->completed = 0;
	mutex_init(&sp->mutex);
	sp->per_cpu_ref = alloc_percpu(struct srcu_struct_array);
	return (sp->per_cpu_ref ? 0 : -ENOMEM);
}

/*
 * srcu_readers_active_idx -- returns approximate number of readers
 *	active on the specified rank of per-CPU counters.
 */

static int srcu_readers_active_idx(struct srcu_struct *sp, int idx)
{
	int cpu;
	int sum;

	sum = 0;
	for_each_possible_cpu(cpu)
		sum += per_cpu_ptr(sp->per_cpu_ref, cpu)->c[idx];
	return sum;
}

/**
 * srcu_readers_active - returns approximate number of readers.
 * @sp: which srcu_struct to count active readers (holding srcu_read_lock).
 *
 * Note that this is not an atomic primitive, and can therefore suffer
 * severe errors when invoked on an active srcu_struct.  That said, it
 * can be useful as an error check at cleanup time.
 */
int srcu_readers_active(struct srcu_struct *sp)
{
	return srcu_readers_active_idx(sp, 0) + srcu_readers_active_idx(sp, 1);
}

/**
 * cleanup_srcu_struct - deconstruct a sleep-RCU structure
 * @sp: structure to clean up.
 *
 * Must invoke this after you are finished using a given srcu_struct that
 * was initialized via init_srcu_struct(), else you leak memory.
 */
void cleanup_srcu_struct(struct srcu_struct *sp)
{
	int sum;

	sum = srcu_readers_active(sp);
	WARN_ON(sum);  /* Leakage unless caller handles error. */
	if (sum != 0)
		return;
	free_percpu(sp->per_cpu_ref);
	sp->per_cpu_ref = NULL;
}

/**
 * srcu_read_lock - register a new reader for an SRCU-protected structure.
 * @sp: srcu_struct in which to register the new reader.
 *
 * Counts the new reader in the appropriate per-CPU element of the
 * srcu_struct.  Must be called from process context.
 * Returns an index that must be passed to the matching srcu_read_unlock().
 */
int srcu_read_lock(struct srcu_struct *sp)
{
	int idx;

	preempt_disable();
	idx = sp->completed & 0x1;
	barrier();  /* ensure compiler looks -once- at sp->completed. */
	per_cpu_ptr(sp->per_cpu_ref, smp_processor_id())->c[idx]++;
	srcu_barrier();  /* ensure compiler won't misorder critical section. */
	preempt_enable();
	return idx;
}

/**
 * srcu_read_unlock - unregister a old reader from an SRCU-protected structure.
 * @sp: srcu_struct in which to unregister the old reader.
 * @idx: return value from corresponding srcu_read_lock().
 *
 * Removes the count for the old reader from the appropriate per-CPU
 * element of the srcu_struct.  Note that this may well be a different
 * CPU than that which was incremented by the corresponding srcu_read_lock().
 * Must be called from process context.
 */
void srcu_read_unlock(struct srcu_struct *sp, int idx)
{
	preempt_disable();
	srcu_barrier();  /* ensure compiler won't misorder critical section. */
	per_cpu_ptr(sp->per_cpu_ref, smp_processor_id())->c[idx]--;
	preempt_enable();
}

/**
 * synchronize_srcu - wait for prior SRCU read-side critical-section completion
 * @sp: srcu_struct with which to synchronize.
 *
 * Flip the completed counter, and wait for the old count to drain to zero.
 * As with classic RCU, the updater must use some separate means of
 * synchronizing concurrent updates.  Can block; must be called from
 * process context.
 *
 * Note that it is illegal to call synchornize_srcu() from the corresponding
 * SRCU read-side critical section; doing so will result in deadlock.
 * However, it is perfectly legal to call synchronize_srcu() on one
 * srcu_struct from some other srcu_struct's read-side critical section.
 */
void synchronize_srcu(struct srcu_struct *sp)
{
	int idx;

	idx = sp->completed;
	mutex_lock(&sp->mutex);

	/*
	 * Check to see if someone else did the work for us while we were
	 * waiting to acquire the lock.  We need -two- advances of
	 * the counter, not just one.  If there was but one, we might have
	 * shown up -after- our helper's first synchronize_sched(), thus
	 * having failed to prevent CPU-reordering races with concurrent
	 * srcu_read_unlock()s on other CPUs (see comment below).  So we
	 * either (1) wait for two or (2) supply the second ourselves.
	 */

	if ((sp->completed - idx) >= 2) {
		mutex_unlock(&sp->mutex);
		return;
	}

	synchronize_sched();  /* Force memory barrier on all CPUs. */

	/*
	 * The preceding synchronize_sched() ensures that any CPU that
	 * sees the new value of sp->completed will also see any preceding
	 * changes to data structures made by this CPU.  This prevents
	 * some other CPU from reordering the accesses in its SRCU
	 * read-side critical section to precede the corresponding
	 * srcu_read_lock() -- ensuring that such references will in
	 * fact be protected.
	 *
	 * So it is now safe to do the flip.
	 */

	idx = sp->completed & 0x1;
	sp->completed++;

	synchronize_sched();  /* Force memory barrier on all CPUs. */

	/*
	 * At this point, because of the preceding synchronize_sched(),
	 * all srcu_read_lock() calls using the old counters have completed.
	 * Their corresponding critical sections might well be still
	 * executing, but the srcu_read_lock() primitives themselves
	 * will have finished executing.
	 */

	while (srcu_readers_active_idx(sp, idx))
		schedule_timeout_interruptible(1);

	synchronize_sched();  /* Force memory barrier on all CPUs. */

	/*
	 * The preceding synchronize_sched() forces all srcu_read_unlock()
	 * primitives that were executing concurrently with the preceding
	 * for_each_possible_cpu() loop to have completed by this point.
	 * More importantly, it also forces the corresponding SRCU read-side
	 * critical sections to have also completed, and the corresponding
	 * references to SRCU-protected data items to be dropped.
	 *
	 * Note:
	 *
	 *	Despite what you might think at first glance, the
	 *	preceding synchronize_sched() -must- be within the
	 *	critical section ended by the following mutex_unlock().
	 *	Otherwise, a task taking the early exit can race
	 *	with a srcu_read_unlock(), which might have executed
	 *	just before the preceding srcu_readers_active() check,
	 *	and whose CPU might have reordered the srcu_read_unlock()
	 *	with the preceding critical section.  In this case, there
	 *	is nothing preventing the synchronize_sched() task that is
	 *	taking the early exit from freeing a data structure that
	 *	is still being referenced (out of order) by the task
	 *	doing the srcu_read_unlock().
	 *
	 *	Alternatively, the comparison with "2" on the early exit
	 *	could be changed to "3", but this increases synchronize_srcu()
	 *	latency for bulk loads.  So the current code is preferred.
	 */

	mutex_unlock(&sp->mutex);
}

/**
 * srcu_batches_completed - return batches completed.
 * @sp: srcu_struct on which to report batch completion.
 *
 * Report the number of batches, correlated with, but not necessarily
 * precisely the same as, the number of grace periods that have elapsed.
 */

long srcu_batches_completed(struct srcu_struct *sp)
{
	return sp->completed;
}

EXPORT_SYMBOL_GPL(init_srcu_struct);
EXPORT_SYMBOL_GPL(cleanup_srcu_struct);
EXPORT_SYMBOL_GPL(srcu_read_lock);
EXPORT_SYMBOL_GPL(srcu_read_unlock);
EXPORT_SYMBOL_GPL(synchronize_srcu);
EXPORT_SYMBOL_GPL(srcu_batches_completed);
EXPORT_SYMBOL_GPL(srcu_readers_active);
