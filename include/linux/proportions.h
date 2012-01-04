/*
 * FLoating proportions
 *
 *  Copyright (C) 2007 Red Hat, Inc., Peter Zijlstra <pzijlstr@redhat.com>
 *
 * This file contains the public data structure and API definitions.
 */

#ifndef _LINUX_PROPORTIONS_H
#define _LINUX_PROPORTIONS_H

#include <linux/percpu_counter.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>

struct prop_global {
	/*
	 * The period over which we differentiate
	 *
	 *   period = 2^shift
	 */
	int shift;
	/*
	 * The total event counter aka 'time'.
	 *
	 * Treated as an unsigned long; the lower 'shift - 1' bits are the
	 * counter bits, the remaining upper bits the period counter.
	 */
	struct percpu_counter events;
};

/*
 * global proportion descriptor
 *
 * this is needed to consitently flip prop_global structures.
 */
struct prop_descriptor {
	int index;
	struct prop_global pg[2];
	struct mutex mutex;		/* serialize the prop_global switch */
};

int prop_descriptor_init(struct prop_descriptor *pd, int shift);
void prop_change_shift(struct prop_descriptor *pd, int new_shift);

/*
 * ----- PERCPU ------
 */

struct prop_local_percpu {
	/*
	 * the local events counter
	 */
	struct percpu_counter events;

	/*
	 * snapshot of the last seen global state
	 */
	int shift;
	unsigned long period;
	raw_spinlock_t lock;		/* protect the snapshot state */
};

int prop_local_init_percpu(struct prop_local_percpu *pl);
void prop_local_destroy_percpu(struct prop_local_percpu *pl);
void __prop_inc_percpu(struct prop_descriptor *pd, struct prop_local_percpu *pl);
void prop_fraction_percpu(struct prop_descriptor *pd, struct prop_local_percpu *pl,
		long *numerator, long *denominator);

static inline
void prop_inc_percpu(struct prop_descriptor *pd, struct prop_local_percpu *pl)
{
	unsigned long flags;

	local_irq_save(flags);
	__prop_inc_percpu(pd, pl);
	local_irq_restore(flags);
}

/*
 * Limit the time part in order to ensure there are some bits left for the
 * cycle counter and fraction multiply.
 */
#define PROP_MAX_SHIFT (3*BITS_PER_LONG/4)

#define PROP_FRAC_SHIFT		(BITS_PER_LONG - PROP_MAX_SHIFT - 1)
#define PROP_FRAC_BASE		(1UL << PROP_FRAC_SHIFT)

void __prop_inc_percpu_max(struct prop_descriptor *pd,
			   struct prop_local_percpu *pl, long frac);


/*
 * ----- SINGLE ------
 */

struct prop_local_single {
	/*
	 * the local events counter
	 */
	unsigned long events;

	/*
	 * snapshot of the last seen global state
	 * and a lock protecting this state
	 */
	unsigned long period;
	int shift;
	raw_spinlock_t lock;		/* protect the snapshot state */
};

#define INIT_PROP_LOCAL_SINGLE(name)			\
{	.lock = __RAW_SPIN_LOCK_UNLOCKED(name.lock),	\
}

int prop_local_init_single(struct prop_local_single *pl);
void prop_local_destroy_single(struct prop_local_single *pl);
void __prop_inc_single(struct prop_descriptor *pd, struct prop_local_single *pl);
void prop_fraction_single(struct prop_descriptor *pd, struct prop_local_single *pl,
		long *numerator, long *denominator);

static inline
void prop_inc_single(struct prop_descriptor *pd, struct prop_local_single *pl)
{
	unsigned long flags;

	local_irq_save(flags);
	__prop_inc_single(pd, pl);
	local_irq_restore(flags);
}

#endif /* _LINUX_PROPORTIONS_H */
