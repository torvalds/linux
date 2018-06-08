// SPDX-License-Identifier: GPL-2.0
/*
 * Lockless hierarchical page accounting & limiting
 *
 * Copyright (C) 2014 Red Hat, Inc., Johannes Weiner
 */

#include <linux/page_counter.h>
#include <linux/atomic.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/bug.h>
#include <asm/page.h>

static void propagate_protected_usage(struct page_counter *c,
				      unsigned long usage)
{
	unsigned long protected, old_protected;
	long delta;

	if (!c->parent)
		return;

	if (c->min || atomic_long_read(&c->min_usage)) {
		if (usage <= c->min)
			protected = usage;
		else
			protected = 0;

		old_protected = atomic_long_xchg(&c->min_usage, protected);
		delta = protected - old_protected;
		if (delta)
			atomic_long_add(delta, &c->parent->children_min_usage);
	}

	if (c->low || atomic_long_read(&c->low_usage)) {
		if (usage <= c->low)
			protected = usage;
		else
			protected = 0;

		old_protected = atomic_long_xchg(&c->low_usage, protected);
		delta = protected - old_protected;
		if (delta)
			atomic_long_add(delta, &c->parent->children_low_usage);
	}
}

/**
 * page_counter_cancel - take pages out of the local counter
 * @counter: counter
 * @nr_pages: number of pages to cancel
 */
void page_counter_cancel(struct page_counter *counter, unsigned long nr_pages)
{
	long new;

	new = atomic_long_sub_return(nr_pages, &counter->usage);
	propagate_protected_usage(counter, new);
	/* More uncharges than charges? */
	WARN_ON_ONCE(new < 0);
}

/**
 * page_counter_charge - hierarchically charge pages
 * @counter: counter
 * @nr_pages: number of pages to charge
 *
 * NOTE: This does not consider any configured counter limits.
 */
void page_counter_charge(struct page_counter *counter, unsigned long nr_pages)
{
	struct page_counter *c;

	for (c = counter; c; c = c->parent) {
		long new;

		new = atomic_long_add_return(nr_pages, &c->usage);
		propagate_protected_usage(counter, new);
		/*
		 * This is indeed racy, but we can live with some
		 * inaccuracy in the watermark.
		 */
		if (new > c->watermark)
			c->watermark = new;
	}
}

/**
 * page_counter_try_charge - try to hierarchically charge pages
 * @counter: counter
 * @nr_pages: number of pages to charge
 * @fail: points first counter to hit its limit, if any
 *
 * Returns %true on success, or %false and @fail if the counter or one
 * of its ancestors has hit its configured limit.
 */
bool page_counter_try_charge(struct page_counter *counter,
			     unsigned long nr_pages,
			     struct page_counter **fail)
{
	struct page_counter *c;

	for (c = counter; c; c = c->parent) {
		long new;
		/*
		 * Charge speculatively to avoid an expensive CAS.  If
		 * a bigger charge fails, it might falsely lock out a
		 * racing smaller charge and send it into reclaim
		 * early, but the error is limited to the difference
		 * between the two sizes, which is less than 2M/4M in
		 * case of a THP locking out a regular page charge.
		 *
		 * The atomic_long_add_return() implies a full memory
		 * barrier between incrementing the count and reading
		 * the limit.  When racing with page_counter_limit(),
		 * we either see the new limit or the setter sees the
		 * counter has changed and retries.
		 */
		new = atomic_long_add_return(nr_pages, &c->usage);
		if (new > c->max) {
			atomic_long_sub(nr_pages, &c->usage);
			propagate_protected_usage(counter, new);
			/*
			 * This is racy, but we can live with some
			 * inaccuracy in the failcnt.
			 */
			c->failcnt++;
			*fail = c;
			goto failed;
		}
		propagate_protected_usage(counter, new);
		/*
		 * Just like with failcnt, we can live with some
		 * inaccuracy in the watermark.
		 */
		if (new > c->watermark)
			c->watermark = new;
	}
	return true;

failed:
	for (c = counter; c != *fail; c = c->parent)
		page_counter_cancel(c, nr_pages);

	return false;
}

/**
 * page_counter_uncharge - hierarchically uncharge pages
 * @counter: counter
 * @nr_pages: number of pages to uncharge
 */
void page_counter_uncharge(struct page_counter *counter, unsigned long nr_pages)
{
	struct page_counter *c;

	for (c = counter; c; c = c->parent)
		page_counter_cancel(c, nr_pages);
}

/**
 * page_counter_set_max - set the maximum number of pages allowed
 * @counter: counter
 * @nr_pages: limit to set
 *
 * Returns 0 on success, -EBUSY if the current number of pages on the
 * counter already exceeds the specified limit.
 *
 * The caller must serialize invocations on the same counter.
 */
int page_counter_set_max(struct page_counter *counter, unsigned long nr_pages)
{
	for (;;) {
		unsigned long old;
		long usage;

		/*
		 * Update the limit while making sure that it's not
		 * below the concurrently-changing counter value.
		 *
		 * The xchg implies two full memory barriers before
		 * and after, so the read-swap-read is ordered and
		 * ensures coherency with page_counter_try_charge():
		 * that function modifies the count before checking
		 * the limit, so if it sees the old limit, we see the
		 * modified counter and retry.
		 */
		usage = atomic_long_read(&counter->usage);

		if (usage > nr_pages)
			return -EBUSY;

		old = xchg(&counter->max, nr_pages);

		if (atomic_long_read(&counter->usage) <= usage)
			return 0;

		counter->max = old;
		cond_resched();
	}
}

/**
 * page_counter_set_min - set the amount of protected memory
 * @counter: counter
 * @nr_pages: value to set
 *
 * The caller must serialize invocations on the same counter.
 */
void page_counter_set_min(struct page_counter *counter, unsigned long nr_pages)
{
	struct page_counter *c;

	counter->min = nr_pages;

	for (c = counter; c; c = c->parent)
		propagate_protected_usage(c, atomic_long_read(&c->usage));
}

/**
 * page_counter_set_low - set the amount of protected memory
 * @counter: counter
 * @nr_pages: value to set
 *
 * The caller must serialize invocations on the same counter.
 */
void page_counter_set_low(struct page_counter *counter, unsigned long nr_pages)
{
	struct page_counter *c;

	counter->low = nr_pages;

	for (c = counter; c; c = c->parent)
		propagate_protected_usage(c, atomic_long_read(&c->usage));
}

/**
 * page_counter_memparse - memparse() for page counter limits
 * @buf: string to parse
 * @max: string meaning maximum possible value
 * @nr_pages: returns the result in number of pages
 *
 * Returns -EINVAL, or 0 and @nr_pages on success.  @nr_pages will be
 * limited to %PAGE_COUNTER_MAX.
 */
int page_counter_memparse(const char *buf, const char *max,
			  unsigned long *nr_pages)
{
	char *end;
	u64 bytes;

	if (!strcmp(buf, max)) {
		*nr_pages = PAGE_COUNTER_MAX;
		return 0;
	}

	bytes = memparse(buf, &end);
	if (*end != '\0')
		return -EINVAL;

	*nr_pages = min(bytes / PAGE_SIZE, (u64)PAGE_COUNTER_MAX);

	return 0;
}
