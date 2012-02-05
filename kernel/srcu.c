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

#include <linux/export.h>
#include <linux/mutex.h>
#include <linux/percpu.h>
#include <linux/preempt.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/delay.h>
#include <linux/srcu.h>

static int init_srcu_struct_fields(struct srcu_struct *sp)
{
	sp->completed = 0;
	mutex_init(&sp->mutex);
	sp->per_cpu_ref = alloc_percpu(struct srcu_struct_array);
	return sp->per_cpu_ref ? 0 : -ENOMEM;
}

#ifdef CONFIG_DEBUG_LOCK_ALLOC

int __init_srcu_struct(struct srcu_struct *sp, const char *name,
		       struct lock_class_key *key)
{
	/* Don't re-initialize a lock while it is held. */
	debug_check_no_locks_freed((void *)sp, sizeof(*sp));
	lockdep_init_map(&sp->dep_map, name, key, 0);
	return init_srcu_struct_fields(sp);
}
EXPORT_SYMBOL_GPL(__init_srcu_struct);

#else /* #ifdef CONFIG_DEBUG_LOCK_ALLOC */

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
	return init_srcu_struct_fields(sp);
}
EXPORT_SYMBOL_GPL(init_srcu_struct);

#endif /* #else #ifdef CONFIG_DEBUG_LOCK_ALLOC */

/*
 * Returns approximate number of readers active on the specified rank
 * of per-CPU counters.  Also snapshots each counter's value in the
 * corresponding element of sp->snap[] for later use validating
 * the sum.
 */
static unsigned long srcu_readers_active_idx(struct srcu_struct *sp, int idx)
{
	int cpu;
	unsigned long sum = 0;
	unsigned long t;

	for_each_possible_cpu(cpu) {
		t = ACCESS_ONCE(per_cpu_ptr(sp->per_cpu_ref, cpu)->c[idx]);
		sum += t;
		sp->snap[cpu] = t;
	}
	return sum & SRCU_REF_MASK;
}

/*
 * To be called from the update side after an index flip.  Returns true
 * if the modulo sum of the counters is stably zero, false if there is
 * some possibility of non-zero.
 */
static bool srcu_readers_active_idx_check(struct srcu_struct *sp, int idx)
{
	int cpu;

	/*
	 * Note that srcu_readers_active_idx() can incorrectly return
	 * zero even though there is a pre-existing reader throughout.
	 * To see this, suppose that task A is in a very long SRCU
	 * read-side critical section that started on CPU 0, and that
	 * no other reader exists, so that the modulo sum of the counters
	 * is equal to one.  Then suppose that task B starts executing
	 * srcu_readers_active_idx(), summing up to CPU 1, and then that
	 * task C starts reading on CPU 0, so that its increment is not
	 * summed, but finishes reading on CPU 2, so that its decrement
	 * -is- summed.  Then when task B completes its sum, it will
	 * incorrectly get zero, despite the fact that task A has been
	 * in its SRCU read-side critical section the whole time.
	 *
	 * We therefore do a validation step should srcu_readers_active_idx()
	 * return zero.
	 */
	if (srcu_readers_active_idx(sp, idx) != 0)
		return false;

	/*
	 * Since the caller recently flipped ->completed, we can see at
	 * most one increment of each CPU's counter from this point
	 * forward.  The reason for this is that the reader CPU must have
	 * fetched the index before srcu_readers_active_idx checked
	 * that CPU's counter, but not yet incremented its counter.
	 * Its eventual counter increment will follow the read in
	 * srcu_readers_active_idx(), and that increment is immediately
	 * followed by smp_mb() B.  Because smp_mb() D is between
	 * the ->completed flip and srcu_readers_active_idx()'s read,
	 * that CPU's subsequent load of ->completed must see the new
	 * value, and therefore increment the counter in the other rank.
	 */
	smp_mb(); /* A */

	/*
	 * Now, we check the ->snap array that srcu_readers_active_idx()
	 * filled in from the per-CPU counter values.  Since both
	 * __srcu_read_lock() and __srcu_read_unlock() increment the
	 * upper bits of the per-CPU counter, an increment/decrement
	 * pair will change the value of the counter.  Since there is
	 * only one possible increment, the only way to wrap the counter
	 * is to have a huge number of counter decrements, which requires
	 * a huge number of tasks and huge SRCU read-side critical-section
	 * nesting levels, even on 32-bit systems.
	 *
	 * All of the ways of confusing the readings require that the scan
	 * in srcu_readers_active_idx() see the read-side task's decrement,
	 * but not its increment.  However, between that decrement and
	 * increment are smb_mb() B and C.  Either or both of these pair
	 * with smp_mb() A above to ensure that the scan below will see
	 * the read-side tasks's increment, thus noting a difference in
	 * the counter values between the two passes.
	 *
	 * Therefore, if srcu_readers_active_idx() returned zero, and
	 * none of the counters changed, we know that the zero was the
	 * correct sum.
	 *
	 * Of course, it is possible that a task might be delayed
	 * for a very long time in __srcu_read_lock() after fetching
	 * the index but before incrementing its counter.  This
	 * possibility will be dealt with in __synchronize_srcu().
	 */
	for_each_possible_cpu(cpu)
		if (sp->snap[cpu] !=
		    ACCESS_ONCE(per_cpu_ptr(sp->per_cpu_ref, cpu)->c[idx]))
			return false;  /* False zero reading! */
	return true;
}

/**
 * srcu_readers_active - returns approximate number of readers.
 * @sp: which srcu_struct to count active readers (holding srcu_read_lock).
 *
 * Note that this is not an atomic primitive, and can therefore suffer
 * severe errors when invoked on an active srcu_struct.  That said, it
 * can be useful as an error check at cleanup time.
 */
static int srcu_readers_active(struct srcu_struct *sp)
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
EXPORT_SYMBOL_GPL(cleanup_srcu_struct);

/*
 * Counts the new reader in the appropriate per-CPU element of the
 * srcu_struct.  Must be called from process context.
 * Returns an index that must be passed to the matching srcu_read_unlock().
 */
int __srcu_read_lock(struct srcu_struct *sp)
{
	int idx;

	preempt_disable();
	idx = rcu_dereference_index_check(sp->completed,
					  rcu_read_lock_sched_held()) & 0x1;
	ACCESS_ONCE(this_cpu_ptr(sp->per_cpu_ref)->c[idx]) +=
		SRCU_USAGE_COUNT + 1;
	smp_mb(); /* B */  /* Avoid leaking the critical section. */
	preempt_enable();
	return idx;
}
EXPORT_SYMBOL_GPL(__srcu_read_lock);

/*
 * Removes the count for the old reader from the appropriate per-CPU
 * element of the srcu_struct.  Note that this may well be a different
 * CPU than that which was incremented by the corresponding srcu_read_lock().
 * Must be called from process context.
 */
void __srcu_read_unlock(struct srcu_struct *sp, int idx)
{
	preempt_disable();
	smp_mb(); /* C */  /* Avoid leaking the critical section. */
	ACCESS_ONCE(this_cpu_ptr(sp->per_cpu_ref)->c[idx]) +=
		SRCU_USAGE_COUNT - 1;
	preempt_enable();
}
EXPORT_SYMBOL_GPL(__srcu_read_unlock);

/*
 * We use an adaptive strategy for synchronize_srcu() and especially for
 * synchronize_srcu_expedited().  We spin for a fixed time period
 * (defined below) to allow SRCU readers to exit their read-side critical
 * sections.  If there are still some readers after 10 microseconds,
 * we repeatedly block for 1-millisecond time periods.  This approach
 * has done well in testing, so there is no need for a config parameter.
 */
#define SYNCHRONIZE_SRCU_READER_DELAY 5

/*
 * Flip the readers' index by incrementing ->completed, then wait
 * until there are no more readers using the counters referenced by
 * the old index value.  (Recall that the index is the bottom bit
 * of ->completed.)
 *
 * Of course, it is possible that a reader might be delayed for the
 * full duration of flip_idx_and_wait() between fetching the
 * index and incrementing its counter.  This possibility is handled
 * by __synchronize_srcu() invoking flip_idx_and_wait() twice.
 */
static void flip_idx_and_wait(struct srcu_struct *sp, bool expedited)
{
	int idx;
	int trycount = 0;

	idx = sp->completed++ & 0x1;

	/*
	 * If a reader fetches the index before the above increment,
	 * but increments its counter after srcu_readers_active_idx_check()
	 * sums it, then smp_mb() D will pair with __srcu_read_lock()'s
	 * smp_mb() B to ensure that the SRCU read-side critical section
	 * will see any updates that the current task performed before its
	 * call to synchronize_srcu(), or to synchronize_srcu_expedited(),
	 * as the case may be.
	 */
	smp_mb(); /* D */

	/*
	 * SRCU read-side critical sections are normally short, so wait
	 * a small amount of time before possibly blocking.
	 */
	if (!srcu_readers_active_idx_check(sp, idx)) {
		udelay(SYNCHRONIZE_SRCU_READER_DELAY);
		while (!srcu_readers_active_idx_check(sp, idx)) {
			if (expedited && ++ trycount < 10)
				udelay(SYNCHRONIZE_SRCU_READER_DELAY);
			else
				schedule_timeout_interruptible(1);
		}
	}

	/*
	 * The following smp_mb() E pairs with srcu_read_unlock()'s
	 * smp_mb C to ensure that if srcu_readers_active_idx_check()
	 * sees srcu_read_unlock()'s counter decrement, then any
	 * of the current task's subsequent code will happen after
	 * that SRCU read-side critical section.
	 */
	smp_mb(); /* E */
}

/*
 * Helper function for synchronize_srcu() and synchronize_srcu_expedited().
 */
static void __synchronize_srcu(struct srcu_struct *sp, bool expedited)
{
	int idx;

	rcu_lockdep_assert(!lock_is_held(&sp->dep_map) &&
			   !lock_is_held(&rcu_bh_lock_map) &&
			   !lock_is_held(&rcu_lock_map) &&
			   !lock_is_held(&rcu_sched_lock_map),
			   "Illegal synchronize_srcu() in same-type SRCU (or RCU) read-side critical section");

	smp_mb();  /* Ensure prior action happens before grace period. */
	idx = ACCESS_ONCE(sp->completed);
	smp_mb();  /* Access to ->completed before lock acquisition. */
	mutex_lock(&sp->mutex);

	/*
	 * Check to see if someone else did the work for us while we were
	 * waiting to acquire the lock.  We need -three- advances of
	 * the counter, not just one.  If there was but one, we might have
	 * shown up -after- our helper's first synchronize_sched(), thus
	 * having failed to prevent CPU-reordering races with concurrent
	 * srcu_read_unlock()s on other CPUs (see comment below).  If there
	 * was only two, we are guaranteed to have waited through only one
	 * full index-flip phase.  So we either (1) wait for three or
	 * (2) supply the additional ones we need.
	 */

	if (sp->completed == idx + 2)
		idx = 1;
	else if (sp->completed == idx + 3) {
		mutex_unlock(&sp->mutex);
		return;
	} else
		idx = 0;

	/*
	 * If there were no helpers, then we need to do two flips of
	 * the index.  The first flip is required if there are any
	 * outstanding SRCU readers even if there are no new readers
	 * running concurrently with the first counter flip.
	 *
	 * The second flip is required when a new reader picks up
	 * the old value of the index, but does not increment its
	 * counter until after its counters is summed/rechecked by
	 * srcu_readers_active_idx_check().  In this case, the current SRCU
	 * grace period would be OK because the SRCU read-side critical
	 * section started after this SRCU grace period started, so the
	 * grace period is not required to wait for the reader.
	 *
	 * However, the next SRCU grace period would be waiting for the
	 * other set of counters to go to zero, and therefore would not
	 * wait for the reader, which would be very bad.  To avoid this
	 * bad scenario, we flip and wait twice, clearing out both sets
	 * of counters.
	 */
	for (; idx < 2; idx++)
		flip_idx_and_wait(sp, expedited);
	mutex_unlock(&sp->mutex);
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
 * Note that it is illegal to call synchronize_srcu() from the corresponding
 * SRCU read-side critical section; doing so will result in deadlock.
 * However, it is perfectly legal to call synchronize_srcu() on one
 * srcu_struct from some other srcu_struct's read-side critical section.
 */
void synchronize_srcu(struct srcu_struct *sp)
{
	__synchronize_srcu(sp, 0);
}
EXPORT_SYMBOL_GPL(synchronize_srcu);

/**
 * synchronize_srcu_expedited - Brute-force SRCU grace period
 * @sp: srcu_struct with which to synchronize.
 *
 * Wait for an SRCU grace period to elapse, but be more aggressive about
 * spinning rather than blocking when waiting.
 *
 * Note that it is illegal to call this function while holding any lock
 * that is acquired by a CPU-hotplug notifier.  It is also illegal to call
 * synchronize_srcu_expedited() from the corresponding SRCU read-side
 * critical section; doing so will result in deadlock.  However, it is
 * perfectly legal to call synchronize_srcu_expedited() on one srcu_struct
 * from some other srcu_struct's read-side critical section, as long as
 * the resulting graph of srcu_structs is acyclic.
 */
void synchronize_srcu_expedited(struct srcu_struct *sp)
{
	__synchronize_srcu(sp, 1);
}
EXPORT_SYMBOL_GPL(synchronize_srcu_expedited);

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
EXPORT_SYMBOL_GPL(srcu_batches_completed);
