#ifndef _LINUX_PERCPU_COUNTER_H
#define _LINUX_PERCPU_COUNTER_H
/*
 * A simple "approximate counter" for use in ext2 and ext3 superblocks.
 *
 * WARNING: these things are HUGE.  4 kbytes per counter on 32-way P4.
 */

#include <linux/config.h>
#include <linux/spinlock.h>
#include <linux/smp.h>
#include <linux/threads.h>
#include <linux/percpu.h>

#ifdef CONFIG_SMP

struct percpu_counter {
	spinlock_t lock;
	long count;
	long *counters;
};

#if NR_CPUS >= 16
#define FBC_BATCH	(NR_CPUS*2)
#else
#define FBC_BATCH	(NR_CPUS*4)
#endif

static inline void percpu_counter_init(struct percpu_counter *fbc)
{
	spin_lock_init(&fbc->lock);
	fbc->count = 0;
	fbc->counters = alloc_percpu(long);
}

static inline void percpu_counter_destroy(struct percpu_counter *fbc)
{
	free_percpu(fbc->counters);
}

void percpu_counter_mod(struct percpu_counter *fbc, long amount);
long percpu_counter_sum(struct percpu_counter *fbc);

static inline long percpu_counter_read(struct percpu_counter *fbc)
{
	return fbc->count;
}

/*
 * It is possible for the percpu_counter_read() to return a small negative
 * number for some counter which should never be negative.
 */
static inline long percpu_counter_read_positive(struct percpu_counter *fbc)
{
	long ret = fbc->count;

	barrier();		/* Prevent reloads of fbc->count */
	if (ret > 0)
		return ret;
	return 1;
}

#else

struct percpu_counter {
	long count;
};

static inline void percpu_counter_init(struct percpu_counter *fbc)
{
	fbc->count = 0;
}

static inline void percpu_counter_destroy(struct percpu_counter *fbc)
{
}

static inline void
percpu_counter_mod(struct percpu_counter *fbc, long amount)
{
	preempt_disable();
	fbc->count += amount;
	preempt_enable();
}

static inline long percpu_counter_read(struct percpu_counter *fbc)
{
	return fbc->count;
}

static inline long percpu_counter_read_positive(struct percpu_counter *fbc)
{
	return fbc->count;
}

static inline long percpu_counter_sum(struct percpu_counter *fbc)
{
	return percpu_counter_read_positive(fbc);
}

#endif	/* CONFIG_SMP */

static inline void percpu_counter_inc(struct percpu_counter *fbc)
{
	percpu_counter_mod(fbc, 1);
}

static inline void percpu_counter_dec(struct percpu_counter *fbc)
{
	percpu_counter_mod(fbc, -1);
}

#endif /* _LINUX_PERCPU_COUNTER_H */
