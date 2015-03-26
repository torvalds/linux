/*
 * sched_clock.c: support for extending counters to full 64-bit ns counter
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/clocksource.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/ktime.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/syscore_ops.h>
#include <linux/hrtimer.h>
#include <linux/sched_clock.h>
#include <linux/seqlock.h>
#include <linux/bitops.h>

/**
 * struct clock_read_data - data required to read from sched_clock
 *
 * @epoch_ns:		sched_clock value at last update
 * @epoch_cyc:		Clock cycle value at last update
 * @sched_clock_mask:   Bitmask for two's complement subtraction of non 64bit
 *			clocks
 * @read_sched_clock:	Current clock source (or dummy source when suspended)
 * @mult:		Multipler for scaled math conversion
 * @shift:		Shift value for scaled math conversion
 *
 * Care must be taken when updating this structure; it is read by
 * some very hot code paths. It occupies <=40 bytes and, when combined
 * with the seqcount used to synchronize access, comfortably fits into
 * a 64 byte cache line.
 */
struct clock_read_data {
	u64 epoch_ns;
	u64 epoch_cyc;
	u64 sched_clock_mask;
	u64 (*read_sched_clock)(void);
	u32 mult;
	u32 shift;
};

/**
 * struct clock_data - all data needed for sched_clock (including
 *                     registration of a new clock source)
 *
 * @seq:		Sequence counter for protecting updates.
 * @read_data:		Data required to read from sched_clock.
 * @wrap_kt:		Duration for which clock can run before wrapping
 * @rate:		Tick rate of the registered clock
 * @actual_read_sched_clock: Registered clock read function
 *
 * The ordering of this structure has been chosen to optimize cache
 * performance. In particular seq and read_data (combined) should fit
 * into a single 64 byte cache line.
 */
struct clock_data {
	seqcount_t seq;
	struct clock_read_data read_data;
	ktime_t wrap_kt;
	unsigned long rate;
	u64 (*actual_read_sched_clock)(void);
};

static struct hrtimer sched_clock_timer;
static int irqtime = -1;

core_param(irqtime, irqtime, int, 0400);

static u64 notrace jiffy_sched_clock_read(void)
{
	/*
	 * We don't need to use get_jiffies_64 on 32-bit arches here
	 * because we register with BITS_PER_LONG
	 */
	return (u64)(jiffies - INITIAL_JIFFIES);
}

static struct clock_data cd ____cacheline_aligned = {
	.read_data = { .mult = NSEC_PER_SEC / HZ,
		       .read_sched_clock = jiffy_sched_clock_read, },
	.actual_read_sched_clock = jiffy_sched_clock_read,

};

static inline u64 notrace cyc_to_ns(u64 cyc, u32 mult, u32 shift)
{
	return (cyc * mult) >> shift;
}

unsigned long long notrace sched_clock(void)
{
	u64 cyc, res;
	unsigned long seq;
	struct clock_read_data *rd = &cd.read_data;

	do {
		seq = raw_read_seqcount_begin(&cd.seq);

		cyc = (rd->read_sched_clock() - rd->epoch_cyc) &
		      rd->sched_clock_mask;
		res = rd->epoch_ns + cyc_to_ns(cyc, rd->mult, rd->shift);
	} while (read_seqcount_retry(&cd.seq, seq));

	return res;
}

/*
 * Atomically update the sched_clock epoch.
 */
static void update_sched_clock(void)
{
	unsigned long flags;
	u64 cyc;
	u64 ns;
	struct clock_read_data *rd = &cd.read_data;

	cyc = cd.actual_read_sched_clock();
	ns = rd->epoch_ns +
	     cyc_to_ns((cyc - rd->epoch_cyc) & rd->sched_clock_mask,
		       rd->mult, rd->shift);

	raw_local_irq_save(flags);
	raw_write_seqcount_begin(&cd.seq);
	rd->epoch_ns = ns;
	rd->epoch_cyc = cyc;
	raw_write_seqcount_end(&cd.seq);
	raw_local_irq_restore(flags);
}

static enum hrtimer_restart sched_clock_poll(struct hrtimer *hrt)
{
	update_sched_clock();
	hrtimer_forward_now(hrt, cd.wrap_kt);
	return HRTIMER_RESTART;
}

void __init sched_clock_register(u64 (*read)(void), int bits,
				 unsigned long rate)
{
	u64 res, wrap, new_mask, new_epoch, cyc, ns;
	u32 new_mult, new_shift;
	unsigned long r;
	char r_unit;
	struct clock_read_data *rd = &cd.read_data;

	if (cd.rate > rate)
		return;

	WARN_ON(!irqs_disabled());

	/* calculate the mult/shift to convert counter ticks to ns. */
	clocks_calc_mult_shift(&new_mult, &new_shift, rate, NSEC_PER_SEC, 3600);

	new_mask = CLOCKSOURCE_MASK(bits);
	cd.rate = rate;

	/* calculate how many nanosecs until we risk wrapping */
	wrap = clocks_calc_max_nsecs(new_mult, new_shift, 0, new_mask, NULL);
	cd.wrap_kt = ns_to_ktime(wrap);

	/* update epoch for new counter and update epoch_ns from old counter*/
	new_epoch = read();
	cyc = cd.actual_read_sched_clock();
	ns = rd->epoch_ns +
	     cyc_to_ns((cyc - rd->epoch_cyc) & rd->sched_clock_mask,
		       rd->mult, rd->shift);
	cd.actual_read_sched_clock = read;

	raw_write_seqcount_begin(&cd.seq);
	rd->read_sched_clock = read;
	rd->sched_clock_mask = new_mask;
	rd->mult = new_mult;
	rd->shift = new_shift;
	rd->epoch_cyc = new_epoch;
	rd->epoch_ns = ns;
	raw_write_seqcount_end(&cd.seq);

	r = rate;
	if (r >= 4000000) {
		r /= 1000000;
		r_unit = 'M';
	} else if (r >= 1000) {
		r /= 1000;
		r_unit = 'k';
	} else
		r_unit = ' ';

	/* calculate the ns resolution of this counter */
	res = cyc_to_ns(1ULL, new_mult, new_shift);

	pr_info("sched_clock: %u bits at %lu%cHz, resolution %lluns, wraps every %lluns\n",
		bits, r, r_unit, res, wrap);

	/* Enable IRQ time accounting if we have a fast enough sched_clock */
	if (irqtime > 0 || (irqtime == -1 && rate >= 1000000))
		enable_sched_clock_irqtime();

	pr_debug("Registered %pF as sched_clock source\n", read);
}

void __init sched_clock_postinit(void)
{
	/*
	 * If no sched_clock function has been provided at that point,
	 * make it the final one one.
	 */
	if (cd.actual_read_sched_clock == jiffy_sched_clock_read)
		sched_clock_register(jiffy_sched_clock_read, BITS_PER_LONG, HZ);

	update_sched_clock();

	/*
	 * Start the timer to keep sched_clock() properly updated and
	 * sets the initial epoch.
	 */
	hrtimer_init(&sched_clock_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	sched_clock_timer.function = sched_clock_poll;
	hrtimer_start(&sched_clock_timer, cd.wrap_kt, HRTIMER_MODE_REL);
}

/*
 * Clock read function for use when the clock is suspended.
 *
 * This function makes it appear to sched_clock() as if the clock
 * stopped counting at its last update.
 */
static u64 notrace suspended_sched_clock_read(void)
{
	return cd.read_data.epoch_cyc;
}

static int sched_clock_suspend(void)
{
	struct clock_read_data *rd = &cd.read_data;

	update_sched_clock();
	hrtimer_cancel(&sched_clock_timer);
	rd->read_sched_clock = suspended_sched_clock_read;
	return 0;
}

static void sched_clock_resume(void)
{
	struct clock_read_data *rd = &cd.read_data;

	rd->epoch_cyc = cd.actual_read_sched_clock();
	hrtimer_start(&sched_clock_timer, cd.wrap_kt, HRTIMER_MODE_REL);
	rd->read_sched_clock = cd.actual_read_sched_clock;
}

static struct syscore_ops sched_clock_ops = {
	.suspend = sched_clock_suspend,
	.resume = sched_clock_resume,
};

static int __init sched_clock_syscore_init(void)
{
	register_syscore_ops(&sched_clock_ops);
	return 0;
}
device_initcall(sched_clock_syscore_init);
