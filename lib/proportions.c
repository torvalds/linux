/*
 * Floating proportions
 *
 *  Copyright (C) 2007 Red Hat, Inc., Peter Zijlstra <pzijlstr@redhat.com>
 *
 * Description:
 *
 * The floating proportion is a time derivative with an exponentially decaying
 * history:
 *
 *   p_{j} = \Sum_{i=0} (dx_{j}/dt_{-i}) / 2^(1+i)
 *
 * Where j is an element from {prop_local}, x_{j} is j's number of events,
 * and i the time period over which the differential is taken. So d/dt_{-i} is
 * the differential over the i-th last period.
 *
 * The decaying history gives smooth transitions. The time differential carries
 * the notion of speed.
 *
 * The denominator is 2^(1+i) because we want the series to be normalised, ie.
 *
 *   \Sum_{i=0} 1/2^(1+i) = 1
 *
 * Further more, if we measure time (t) in the same events as x; so that:
 *
 *   t = \Sum_{j} x_{j}
 *
 * we get that:
 *
 *   \Sum_{j} p_{j} = 1
 *
 * Writing this in an iterative fashion we get (dropping the 'd's):
 *
 *   if (++x_{j}, ++t > period)
 *     t /= 2;
 *     for_each (j)
 *       x_{j} /= 2;
 *
 * so that:
 *
 *   p_{j} = x_{j} / t;
 *
 * We optimize away the '/= 2' for the global time delta by noting that:
 *
 *   if (++t > period) t /= 2:
 *
 * Can be approximated by:
 *
 *   period/2 + (++t % period/2)
 *
 * [ Furthermore, when we choose period to be 2^n it can be written in terms of
 *   binary operations and wraparound artefacts disappear. ]
 *
 * Also note that this yields a natural counter of the elapsed periods:
 *
 *   c = t / (period/2)
 *
 * [ Its monotonic increasing property can be applied to mitigate the wrap-
 *   around issue. ]
 *
 * This allows us to do away with the loop over all prop_locals on each period
 * expiration. By remembering the period count under which it was last accessed
 * as c_{j}, we can obtain the number of 'missed' cycles from:
 *
 *   c - c_{j}
 *
 * We can then lazily catch up to the global period count every time we are
 * going to use x_{j}, by doing:
 *
 *   x_{j} /= 2^(c - c_{j}), c_{j} = c
 */

#include <linux/proportions.h>
#include <linux/rcupdate.h>

/*
 * Limit the time part in order to ensure there are some bits left for the
 * cycle counter.
 */
#define PROP_MAX_SHIFT (3*BITS_PER_LONG/4)

int prop_descriptor_init(struct prop_descriptor *pd, int shift)
{
	int err;

	if (shift > PROP_MAX_SHIFT)
		shift = PROP_MAX_SHIFT;

	pd->index = 0;
	pd->pg[0].shift = shift;
	mutex_init(&pd->mutex);
	err = percpu_counter_init_irq(&pd->pg[0].events, 0);
	if (err)
		goto out;

	err = percpu_counter_init_irq(&pd->pg[1].events, 0);
	if (err)
		percpu_counter_destroy(&pd->pg[0].events);

out:
	return err;
}

/*
 * We have two copies, and flip between them to make it seem like an atomic
 * update. The update is not really atomic wrt the events counter, but
 * it is internally consistent with the bit layout depending on shift.
 *
 * We copy the events count, move the bits around and flip the index.
 */
void prop_change_shift(struct prop_descriptor *pd, int shift)
{
	int index;
	int offset;
	u64 events;
	unsigned long flags;

	if (shift > PROP_MAX_SHIFT)
		shift = PROP_MAX_SHIFT;

	mutex_lock(&pd->mutex);

	index = pd->index ^ 1;
	offset = pd->pg[pd->index].shift - shift;
	if (!offset)
		goto out;

	pd->pg[index].shift = shift;

	local_irq_save(flags);
	events = percpu_counter_sum(&pd->pg[pd->index].events);
	if (offset < 0)
		events <<= -offset;
	else
		events >>= offset;
	percpu_counter_set(&pd->pg[index].events, events);

	/*
	 * ensure the new pg is fully written before the switch
	 */
	smp_wmb();
	pd->index = index;
	local_irq_restore(flags);

	synchronize_rcu();

out:
	mutex_unlock(&pd->mutex);
}

/*
 * wrap the access to the data in an rcu_read_lock() section;
 * this is used to track the active references.
 */
static struct prop_global *prop_get_global(struct prop_descriptor *pd)
{
	int index;

	rcu_read_lock();
	index = pd->index;
	/*
	 * match the wmb from vcd_flip()
	 */
	smp_rmb();
	return &pd->pg[index];
}

static void prop_put_global(struct prop_descriptor *pd, struct prop_global *pg)
{
	rcu_read_unlock();
}

static void
prop_adjust_shift(int *pl_shift, unsigned long *pl_period, int new_shift)
{
	int offset = *pl_shift - new_shift;

	if (!offset)
		return;

	if (offset < 0)
		*pl_period <<= -offset;
	else
		*pl_period >>= offset;

	*pl_shift = new_shift;
}

/*
 * PERCPU
 */

#define PROP_BATCH (8*(1+ilog2(nr_cpu_ids)))

int prop_local_init_percpu(struct prop_local_percpu *pl)
{
	spin_lock_init(&pl->lock);
	pl->shift = 0;
	pl->period = 0;
	return percpu_counter_init_irq(&pl->events, 0);
}

void prop_local_destroy_percpu(struct prop_local_percpu *pl)
{
	percpu_counter_destroy(&pl->events);
}

/*
 * Catch up with missed period expirations.
 *
 *   until (c_{j} == c)
 *     x_{j} -= x_{j}/2;
 *     c_{j}++;
 */
static
void prop_norm_percpu(struct prop_global *pg, struct prop_local_percpu *pl)
{
	unsigned long period = 1UL << (pg->shift - 1);
	unsigned long period_mask = ~(period - 1);
	unsigned long global_period;
	unsigned long flags;

	global_period = percpu_counter_read(&pg->events);
	global_period &= period_mask;

	/*
	 * Fast path - check if the local and global period count still match
	 * outside of the lock.
	 */
	if (pl->period == global_period)
		return;

	spin_lock_irqsave(&pl->lock, flags);
	prop_adjust_shift(&pl->shift, &pl->period, pg->shift);

	/*
	 * For each missed period, we half the local counter.
	 * basically:
	 *   pl->events >> (global_period - pl->period);
	 */
	period = (global_period - pl->period) >> (pg->shift - 1);
	if (period < BITS_PER_LONG) {
		s64 val = percpu_counter_read(&pl->events);

		if (val < (nr_cpu_ids * PROP_BATCH))
			val = percpu_counter_sum(&pl->events);

		__percpu_counter_add(&pl->events, -val + (val >> period),
					PROP_BATCH);
	} else
		percpu_counter_set(&pl->events, 0);

	pl->period = global_period;
	spin_unlock_irqrestore(&pl->lock, flags);
}

/*
 *   ++x_{j}, ++t
 */
void __prop_inc_percpu(struct prop_descriptor *pd, struct prop_local_percpu *pl)
{
	struct prop_global *pg = prop_get_global(pd);

	prop_norm_percpu(pg, pl);
	__percpu_counter_add(&pl->events, 1, PROP_BATCH);
	percpu_counter_add(&pg->events, 1);
	prop_put_global(pd, pg);
}

/*
 * Obtain a fraction of this proportion
 *
 *   p_{j} = x_{j} / (period/2 + t % period/2)
 */
void prop_fraction_percpu(struct prop_descriptor *pd,
		struct prop_local_percpu *pl,
		long *numerator, long *denominator)
{
	struct prop_global *pg = prop_get_global(pd);
	unsigned long period_2 = 1UL << (pg->shift - 1);
	unsigned long counter_mask = period_2 - 1;
	unsigned long global_count;

	prop_norm_percpu(pg, pl);
	*numerator = percpu_counter_read_positive(&pl->events);

	global_count = percpu_counter_read(&pg->events);
	*denominator = period_2 + (global_count & counter_mask);

	prop_put_global(pd, pg);
}

/*
 * SINGLE
 */

int prop_local_init_single(struct prop_local_single *pl)
{
	spin_lock_init(&pl->lock);
	pl->shift = 0;
	pl->period = 0;
	pl->events = 0;
	return 0;
}

void prop_local_destroy_single(struct prop_local_single *pl)
{
}

/*
 * Catch up with missed period expirations.
 */
static
void prop_norm_single(struct prop_global *pg, struct prop_local_single *pl)
{
	unsigned long period = 1UL << (pg->shift - 1);
	unsigned long period_mask = ~(period - 1);
	unsigned long global_period;
	unsigned long flags;

	global_period = percpu_counter_read(&pg->events);
	global_period &= period_mask;

	/*
	 * Fast path - check if the local and global period count still match
	 * outside of the lock.
	 */
	if (pl->period == global_period)
		return;

	spin_lock_irqsave(&pl->lock, flags);
	prop_adjust_shift(&pl->shift, &pl->period, pg->shift);
	/*
	 * For each missed period, we half the local counter.
	 */
	period = (global_period - pl->period) >> (pg->shift - 1);
	if (likely(period < BITS_PER_LONG))
		pl->events >>= period;
	else
		pl->events = 0;
	pl->period = global_period;
	spin_unlock_irqrestore(&pl->lock, flags);
}

/*
 *   ++x_{j}, ++t
 */
void __prop_inc_single(struct prop_descriptor *pd, struct prop_local_single *pl)
{
	struct prop_global *pg = prop_get_global(pd);

	prop_norm_single(pg, pl);
	pl->events++;
	percpu_counter_add(&pg->events, 1);
	prop_put_global(pd, pg);
}

/*
 * Obtain a fraction of this proportion
 *
 *   p_{j} = x_{j} / (period/2 + t % period/2)
 */
void prop_fraction_single(struct prop_descriptor *pd,
	       	struct prop_local_single *pl,
		long *numerator, long *denominator)
{
	struct prop_global *pg = prop_get_global(pd);
	unsigned long period_2 = 1UL << (pg->shift - 1);
	unsigned long counter_mask = period_2 - 1;
	unsigned long global_count;

	prop_norm_single(pg, pl);
	*numerator = pl->events;

	global_count = percpu_counter_read(&pg->events);
	*denominator = period_2 + (global_count & counter_mask);

	prop_put_global(pd, pg);
}
