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
 * Returns approximate total of the readers' ->seq[] values for the
 * rank of per-CPU counters specified by idx.
 */
static unsigned long srcu_readers_seq_idx(struct srcu_struct *sp, int idx)
{
	int cpu;
	unsigned long sum = 0;
	unsigned long t;

	for_each_possible_cpu(cpu) {
		t = ACCESS_ONCE(per_cpu_ptr(sp->per_cpu_ref, cpu)->seq[idx]);
		sum += t;
	}
	return sum;
}

/*
 * Returns approximate number of readers active on the specified rank
 * of the per-CPU ->c[] counters.
 */
static unsigned long srcu_readers_active_idx(struct srcu_struct *sp, int idx)
{
	int cpu;
	unsigned long sum = 0;
	unsigned long t;

	for_each_possible_cpu(cpu) {
		t = ACCESS_ONCE(per_cpu_ptr(sp->per_cpu_ref, cpu)->c[idx]);
		sum += t;
	}
	return sum;
}

/*
 * Return true if the number of pre-existing readers is determined to
 * be stably zero.  An example unstable zero can occur if the call
 * to srcu_readers_active_idx() misses an __srcu_read_lock() increment,
 * but due to task migration, sees the corresponding __srcu_read_unlock()
 * decrement.  This can happen because srcu_readers_active_idx() takes
 * time to sum the array, and might in fact be interrupted or preempted
 * partway through the summation.
 */
static bool srcu_readers_active_idx_check(struct srcu_struct *sp, int idx)
{
	unsigned long seq;

	seq = srcu_readers_seq_idx(sp, idx);

	/*
	 * The following smp_mb() A pairs with the smp_mb() B located in
	 * __srcu_read_lock().  This pairing ensures that if an
	 * __srcu_read_lock() increments its counter after the summation
	 * in srcu_readers_active_idx(), then the corresponding SRCU read-side
	 * critical section will see any changes made prior to the start
	 * of the current SRCU grace period.
	 *
	 * Also, if the above call to srcu_readers_seq_idx() saw the
	 * increment of ->seq[], then the call to srcu_readers_active_idx()
	 * must see the increment of ->c[].
	 */
	smp_mb(); /* A */

	/*
	 * Note that srcu_readers_active_idx() can incorrectly return
	 * zero even though there is a pre-existing reader throughout.
	 * To see this, suppose that task A is in a very long SRCU
	 * read-side critical section that started on CPU 0, and that
	 * no other reader exists, so that the sum of the counters
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
	 * The remainder of this function is the validation step.
	 * The following smp_mb() D pairs with the smp_mb() C in
	 * __srcu_read_unlock().  If the __srcu_read_unlock() was seen
	 * by srcu_readers_active_idx() above, then any destructive
	 * operation performed after the grace period will happen after
	 * the corresponding SRCU read-side critical section.
	 *
	 * Note that there can be at most NR_CPUS worth of readers using
	 * the old index, which is not enough to overflow even a 32-bit
	 * integer.  (Yes, this does mean that systems having more than
	 * a billion or so CPUs need to be 64-bit systems.)  Therefore,
	 * the sum of the ->seq[] counters cannot possibly overflow.
	 * Therefore, the only way that the return values of the two
	 * calls to srcu_readers_seq_idx() can be equal is if there were
	 * no increments of the corresponding rank of ->seq[] counts
	 * in the interim.  But the missed-increment scenario laid out
	 * above includes an increment of the ->seq[] counter by
	 * the corresponding __srcu_read_lock().  Therefore, if this
	 * scenario occurs, the return values from the two calls to
	 * srcu_readers_seq_idx() will differ, and thus the validation
	 * step below suffices.
	 */
	smp_mb(); /* D */

	return srcu_readers_seq_idx(sp, idx) == seq;
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
	ACCESS_ONCE(this_cpu_ptr(sp->per_cpu_ref)->c[idx]) += 1;
	smp_mb(); /* B */  /* Avoid leaking the critical section. */
	ACCESS_ONCE(this_cpu_ptr(sp->per_cpu_ref)->seq[idx]) += 1;
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
	ACCESS_ONCE(this_cpu_ptr(sp->per_cpu_ref)->c[idx]) -= 1;
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
 * Wait until all pre-existing readers complete.  Such readers
 * will have used the index specified by "idx".
 */
static void wait_idx(struct srcu_struct *sp, int idx, bool expedited)
{
	int trycount = 0;

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
}

static void srcu_flip(struct srcu_struct *sp)
{
	sp->completed++;
}

/*
 * Helper function for synchronize_srcu() and synchronize_srcu_expedited().
 */
static void __synchronize_srcu(struct srcu_struct *sp, bool expedited)
{
	int busy_idx;

	rcu_lockdep_assert(!lock_is_held(&sp->dep_map) &&
			   !lock_is_held(&rcu_bh_lock_map) &&
			   !lock_is_held(&rcu_lock_map) &&
			   !lock_is_held(&rcu_sched_lock_map),
			   "Illegal synchronize_srcu() in same-type SRCU (or RCU) read-side critical section");

	mutex_lock(&sp->mutex);
	busy_idx = sp->completed & 0X1UL;

	/*
	 * If we recently flipped the index, there will be some readers
	 * using idx=0 and others using idx=1.  Therefore, two calls to
	 * wait_idx()s suffice to ensure that all pre-existing readers
	 * have completed:
	 *
	 * __synchronize_srcu() {
	 * 	wait_idx(sp, 0, expedited);
	 * 	wait_idx(sp, 1, expedited);
	 * }
	 *
	 * Starvation is prevented by the fact that we flip the index.
	 * While we wait on one index to clear out, almost all new readers
	 * will be using the other index.  The number of new readers using the
	 * index we are waiting on is sharply bounded by roughly the number
	 * of CPUs.
	 *
	 * How can new readers possibly using the old pre-flip value of
	 * the index?  Consider the following sequence of events:
	 *
	 * Suppose that during the previous grace period, a reader
	 * picked up the old value of the index, but did not increment
	 * its counter until after the previous instance of
	 * __synchronize_srcu() did the counter summation and recheck.
	 * That previous grace period was OK because the reader did
	 * not start until after the grace period started, so the grace
	 * period was not obligated to wait for that reader.
	 *
	 * However, this sequence of events is quite improbable, so
	 * this call to wait_idx(), which waits on really old readers
	 * describe in this comment above, will almost never need to wait.
	 */
	wait_idx(sp, 1 - busy_idx, expedited);

	/* Flip the index to avoid reader-induced starvation. */
	srcu_flip(sp);

	/* Wait for recent pre-existing readers. */
	wait_idx(sp, busy_idx, expedited);

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
