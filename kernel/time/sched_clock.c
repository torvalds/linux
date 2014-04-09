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

struct clock_data {
	ktime_t wrap_kt;
	u64 epoch_ns;
	u64 epoch_cyc;
	seqcount_t seq;
	unsigned long rate;
	u32 mult;
	u32 shift;
	bool suspended;
};

static struct hrtimer sched_clock_timer;
static int irqtime = -1;

core_param(irqtime, irqtime, int, 0400);

static struct clock_data cd = {
	.mult	= NSEC_PER_SEC / HZ,
};

static u64 __read_mostly sched_clock_mask;

static u64 notrace jiffy_sched_clock_read(void)
{
	/*
	 * We don't need to use get_jiffies_64 on 32-bit arches here
	 * because we register with BITS_PER_LONG
	 */
	return (u64)(jiffies - INITIAL_JIFFIES);
}

static u64 __read_mostly (*read_sched_clock)(void) = jiffy_sched_clock_read;

static inline u64 notrace cyc_to_ns(u64 cyc, u32 mult, u32 shift)
{
	return (cyc * mult) >> shift;
}

unsigned long long notrace sched_clock(void)
{
	u64 epoch_ns;
	u64 epoch_cyc;
	u64 cyc;
	unsigned long seq;

	if (cd.suspended)
		return cd.epoch_ns;

	do {
		seq = raw_read_seqcount_begin(&cd.seq);
		epoch_cyc = cd.epoch_cyc;
		epoch_ns = cd.epoch_ns;
	} while (read_seqcount_retry(&cd.seq, seq));

	cyc = read_sched_clock();
	cyc = (cyc - epoch_cyc) & sched_clock_mask;
	return epoch_ns + cyc_to_ns(cyc, cd.mult, cd.shift);
}

/*
 * Atomically update the sched_clock epoch.
 */
static void notrace update_sched_clock(void)
{
	unsigned long flags;
	u64 cyc;
	u64 ns;

	cyc = read_sched_clock();
	ns = cd.epoch_ns +
		cyc_to_ns((cyc - cd.epoch_cyc) & sched_clock_mask,
			  cd.mult, cd.shift);

	raw_local_irq_save(flags);
	raw_write_seqcount_begin(&cd.seq);
	cd.epoch_ns = ns;
	cd.epoch_cyc = cyc;
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
	ktime_t new_wrap_kt;
	unsigned long r;
	char r_unit;

	if (cd.rate > rate)
		return;

	WARN_ON(!irqs_disabled());

	/* calculate the mult/shift to convert counter ticks to ns. */
	clocks_calc_mult_shift(&new_mult, &new_shift, rate, NSEC_PER_SEC, 3600);

	new_mask = CLOCKSOURCE_MASK(bits);

	/* calculate how many ns until we wrap */
	wrap = clocks_calc_max_nsecs(new_mult, new_shift, 0, new_mask);
	new_wrap_kt = ns_to_ktime(wrap - (wrap >> 3));

	/* update epoch for new counter and update epoch_ns from old counter*/
	new_epoch = read();
	cyc = read_sched_clock();
	ns = cd.epoch_ns + cyc_to_ns((cyc - cd.epoch_cyc) & sched_clock_mask,
			  cd.mult, cd.shift);

	raw_write_seqcount_begin(&cd.seq);
	read_sched_clock = read;
	sched_clock_mask = new_mask;
	cd.rate = rate;
	cd.wrap_kt = new_wrap_kt;
	cd.mult = new_mult;
	cd.shift = new_shift;
	cd.epoch_cyc = new_epoch;
	cd.epoch_ns = ns;
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
	if (read_sched_clock == jiffy_sched_clock_read)
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

static int sched_clock_suspend(void)
{
	sched_clock_poll(&sched_clock_timer);
	cd.suspended = true;
	return 0;
}

static void sched_clock_resume(void)
{
	cd.epoch_cyc = read_sched_clock();
	cd.suspended = false;
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
