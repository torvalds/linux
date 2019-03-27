/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2017 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/aggsum.h>

/*
 * Aggregate-sum counters are a form of fanned-out counter, used when atomic
 * instructions on a single field cause enough CPU cache line contention to
 * slow system performance. Due to their increased overhead and the expense
 * involved with precisely reading from them, they should only be used in cases
 * where the write rate (increment/decrement) is much higher than the read rate
 * (get value).
 *
 * Aggregate sum counters are comprised of two basic parts, the core and the
 * buckets. The core counter contains a lock for the entire counter, as well
 * as the current upper and lower bounds on the value of the counter. The
 * aggsum_bucket structure contains a per-bucket lock to protect the contents of
 * the bucket, the current amount that this bucket has changed from the global
 * counter (called the delta), and the amount of increment and decrement we have
 * "borrowed" from the core counter.
 *
 * The basic operation of an aggsum is simple. Threads that wish to modify the
 * counter will modify one bucket's counter (determined by their current CPU, to
 * help minimize lock and cache contention). If the bucket already has
 * sufficient capacity borrowed from the core structure to handle their request,
 * they simply modify the delta and return.  If the bucket does not, we clear
 * the bucket's current state (to prevent the borrowed amounts from getting too
 * large), and borrow more from the core counter. Borrowing is done by adding to
 * the upper bound (or subtracting from the lower bound) of the core counter,
 * and setting the borrow value for the bucket to the amount added (or
 * subtracted).  Clearing the bucket is the opposite; we add the current delta
 * to both the lower and upper bounds of the core counter, subtract the borrowed
 * incremental from the upper bound, and add the borrowed decrement from the
 * lower bound.  Note that only borrowing and clearing require access to the
 * core counter; since all other operations access CPU-local resources,
 * performance can be much higher than a traditional counter.
 *
 * Threads that wish to read from the counter have a slightly more challenging
 * task. It is fast to determine the upper and lower bounds of the aggum; this
 * does not require grabbing any locks. This suffices for cases where an
 * approximation of the aggsum's value is acceptable. However, if one needs to
 * know whether some specific value is above or below the current value in the
 * aggsum, they invoke aggsum_compare(). This function operates by repeatedly
 * comparing the target value to the upper and lower bounds of the aggsum, and
 * then clearing a bucket. This proceeds until the target is outside of the
 * upper and lower bounds and we return a response, or the last bucket has been
 * cleared and we know that the target is equal to the aggsum's value. Finally,
 * the most expensive operation is determining the precise value of the aggsum.
 * To do this, we clear every bucket and then return the upper bound (which must
 * be equal to the lower bound). What makes aggsum_compare() and aggsum_value()
 * expensive is clearing buckets. This involves grabbing the global lock
 * (serializing against themselves and borrow operations), grabbing a bucket's
 * lock (preventing threads on those CPUs from modifying their delta), and
 * zeroing out the borrowed value (forcing that thread to borrow on its next
 * request, which will also be expensive).  This is what makes aggsums well
 * suited for write-many read-rarely operations.
 */

/*
 * We will borrow aggsum_borrow_multiplier times the current request, so we will
 * have to get the as_lock approximately every aggsum_borrow_multiplier calls to
 * aggsum_delta().
 */
static uint_t aggsum_borrow_multiplier = 10;

void
aggsum_init(aggsum_t *as, uint64_t value)
{
	bzero(as, sizeof (*as));
	as->as_lower_bound = as->as_upper_bound = value;
	mutex_init(&as->as_lock, NULL, MUTEX_DEFAULT, NULL);
	as->as_numbuckets = boot_ncpus;
	as->as_buckets = kmem_zalloc(boot_ncpus * sizeof (aggsum_bucket_t),
	    KM_SLEEP);
	for (int i = 0; i < as->as_numbuckets; i++) {
		mutex_init(&as->as_buckets[i].asc_lock,
		    NULL, MUTEX_DEFAULT, NULL);
	}
}

void
aggsum_fini(aggsum_t *as)
{
	for (int i = 0; i < as->as_numbuckets; i++)
		mutex_destroy(&as->as_buckets[i].asc_lock);
	mutex_destroy(&as->as_lock);
}

int64_t
aggsum_lower_bound(aggsum_t *as)
{
	return (as->as_lower_bound);
}

int64_t
aggsum_upper_bound(aggsum_t *as)
{
	return (as->as_upper_bound);
}

static void
aggsum_flush_bucket(aggsum_t *as, struct aggsum_bucket *asb)
{
	ASSERT(MUTEX_HELD(&as->as_lock));
	ASSERT(MUTEX_HELD(&asb->asc_lock));

	/*
	 * We use atomic instructions for this because we read the upper and
	 * lower bounds without the lock, so we need stores to be atomic.
	 */
	atomic_add_64((volatile uint64_t *)&as->as_lower_bound, asb->asc_delta);
	atomic_add_64((volatile uint64_t *)&as->as_upper_bound, asb->asc_delta);
	asb->asc_delta = 0;
	atomic_add_64((volatile uint64_t *)&as->as_upper_bound,
	    -asb->asc_borrowed);
	atomic_add_64((volatile uint64_t *)&as->as_lower_bound,
	    asb->asc_borrowed);
	asb->asc_borrowed = 0;
}

uint64_t
aggsum_value(aggsum_t *as)
{
	int64_t rv;

	mutex_enter(&as->as_lock);
	if (as->as_lower_bound == as->as_upper_bound) {
		rv = as->as_lower_bound;
		for (int i = 0; i < as->as_numbuckets; i++) {
			ASSERT0(as->as_buckets[i].asc_delta);
			ASSERT0(as->as_buckets[i].asc_borrowed);
		}
		mutex_exit(&as->as_lock);
		return (rv);
	}
	for (int i = 0; i < as->as_numbuckets; i++) {
		struct aggsum_bucket *asb = &as->as_buckets[i];
		mutex_enter(&asb->asc_lock);
		aggsum_flush_bucket(as, asb);
		mutex_exit(&asb->asc_lock);
	}
	VERIFY3U(as->as_lower_bound, ==, as->as_upper_bound);
	rv = as->as_lower_bound;
	mutex_exit(&as->as_lock);

	return (rv);
}

static void
aggsum_borrow(aggsum_t *as, int64_t delta, struct aggsum_bucket *asb)
{
	int64_t abs_delta = (delta < 0 ? -delta : delta);
	mutex_enter(&as->as_lock);
	mutex_enter(&asb->asc_lock);

	aggsum_flush_bucket(as, asb);

	atomic_add_64((volatile uint64_t *)&as->as_upper_bound, abs_delta);
	atomic_add_64((volatile uint64_t *)&as->as_lower_bound, -abs_delta);
	asb->asc_borrowed = abs_delta;

	mutex_exit(&asb->asc_lock);
	mutex_exit(&as->as_lock);
}

void
aggsum_add(aggsum_t *as, int64_t delta)
{
	struct aggsum_bucket *asb =
	    &as->as_buckets[CPU_SEQID % as->as_numbuckets];

	for (;;) {
		mutex_enter(&asb->asc_lock);
		if (asb->asc_delta + delta <= (int64_t)asb->asc_borrowed &&
		    asb->asc_delta + delta >= -(int64_t)asb->asc_borrowed) {
			asb->asc_delta += delta;
			mutex_exit(&asb->asc_lock);
			return;
		}
		mutex_exit(&asb->asc_lock);
		aggsum_borrow(as, delta * aggsum_borrow_multiplier, asb);
	}
}

/*
 * Compare the aggsum value to target efficiently. Returns -1 if the value
 * represented by the aggsum is less than target, 1 if it's greater, and 0 if
 * they are equal.
 */
int
aggsum_compare(aggsum_t *as, uint64_t target)
{
	if (as->as_upper_bound < target)
		return (-1);
	if (as->as_lower_bound > target)
		return (1);
	mutex_enter(&as->as_lock);
	for (int i = 0; i < as->as_numbuckets; i++) {
		struct aggsum_bucket *asb = &as->as_buckets[i];
		mutex_enter(&asb->asc_lock);
		aggsum_flush_bucket(as, asb);
		mutex_exit(&asb->asc_lock);
		if (as->as_upper_bound < target) {
			mutex_exit(&as->as_lock);
			return (-1);
		}
		if (as->as_lower_bound > target) {
			mutex_exit(&as->as_lock);
			return (1);
		}
	}
	VERIFY3U(as->as_lower_bound, ==, as->as_upper_bound);
	ASSERT3U(as->as_lower_bound, ==, target);
	mutex_exit(&as->as_lock);
	return (0);
}
