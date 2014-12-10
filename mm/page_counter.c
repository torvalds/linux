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

/**
 * page_counter_cancel - take pages out of the local counter
 * @counter: counter
 * @nr_pages: number of pages to cancel
 *
 * Returns whether there are remaining pages in the counter.
 */
int page_counter_cancel(struct page_counter *counter, unsigned long nr_pages)
{
	long new;

	new = atomic_long_sub_return(nr_pages, &counter->count);

	/* More uncharges than charges? */
	WARN_ON_ONCE(new < 0);

	return new > 0;
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

		new = atomic_long_add_return(nr_pages, &c->count);
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
 * Returns 0 on success, or -ENOMEM and @fail if the counter or one of
 * its ancestors has hit its configured limit.
 */
int page_counter_try_charge(struct page_counter *counter,
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
		new = atomic_long_add_return(nr_pages, &c->count);
		if (new > c->limit) {
			atomic_long_sub(nr_pages, &c->count);
			/*
			 * This is racy, but we can live with some
			 * inaccuracy in the failcnt.
			 */
			c->failcnt++;
			*fail = c;
			goto failed;
		}
		/*
		 * Just like with failcnt, we can live with some
		 * inaccuracy in the watermark.
		 */
		if (new > c->watermark)
			c->watermark = new;
	}
	return 0;

failed:
	for (c = counter; c != *fail; c = c->parent)
		page_counter_cancel(c, nr_pages);

	return -ENOMEM;
}

/**
 * page_counter_uncharge - hierarchically uncharge pages
 * @counter: counter
 * @nr_pages: number of pages to uncharge
 *
 * Returns whether there are remaining charges in @counter.
 */
int page_counter_uncharge(struct page_counter *counter, unsigned long nr_pages)
{
	struct page_counter *c;
	int ret = 1;

	for (c = counter; c; c = c->parent) {
		int remainder;

		remainder = page_counter_cancel(c, nr_pages);
		if (c == counter && !remainder)
			ret = 0;
	}

	return ret;
}

/**
 * page_counter_limit - limit the number of pages allowed
 * @counter: counter
 * @limit: limit to set
 *
 * Returns 0 on success, -EBUSY if the current number of pages on the
 * counter already exceeds the specified limit.
 *
 * The caller must serialize invocations on the same counter.
 */
int page_counter_limit(struct page_counter *counter, unsigned long limit)
{
	for (;;) {
		unsigned long old;
		long count;

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
		count = atomic_long_read(&counter->count);

		if (count > limit)
			return -EBUSY;

		old = xchg(&counter->limit, limit);

		if (atomic_long_read(&counter->count) <= count)
			return 0;

		counter->limit = old;
		cond_resched();
	}
}

/**
 * page_counter_memparse - memparse() for page counter limits
 * @buf: string to parse
 * @nr_pages: returns the result in number of pages
 *
 * Returns -EINVAL, or 0 and @nr_pages on success.  @nr_pages will be
 * limited to %PAGE_COUNTER_MAX.
 */
int page_counter_memparse(const char *buf, unsigned long *nr_pages)
{
	char unlimited[] = "-1";
	char *end;
	u64 bytes;

	if (!strncmp(buf, unlimited, sizeof(unlimited))) {
		*nr_pages = PAGE_COUNTER_MAX;
		return 0;
	}

	bytes = memparse(buf, &end);
	if (*end != '\0')
		return -EINVAL;

	*nr_pages = min(bytes / PAGE_SIZE, (u64)PAGE_COUNTER_MAX);

	return 0;
}
