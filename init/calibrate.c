/* calibrate.c: default delay calibration
 *
 * Excised from init/main.c
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/timex.h>
#include <linux/smp.h>
#include <linux/percpu.h>

unsigned long lpj_fine;
unsigned long preset_lpj;
static int __init lpj_setup(char *str)
{
	preset_lpj = simple_strtoul(str,NULL,0);
	return 1;
}

__setup("lpj=", lpj_setup);

#ifdef ARCH_HAS_READ_CURRENT_TIMER

/* This routine uses the read_current_timer() routine and gets the
 * loops per jiffy directly, instead of guessing it using delay().
 * Also, this code tries to handle non-maskable asynchronous events
 * (like SMIs)
 */
#define DELAY_CALIBRATION_TICKS			((HZ < 100) ? 1 : (HZ/100))
#define MAX_DIRECT_CALIBRATION_RETRIES		5

static unsigned long calibrate_delay_direct(void)
{
	unsigned long pre_start, start, post_start;
	unsigned long pre_end, end, post_end;
	unsigned long start_jiffies;
	unsigned long timer_rate_min, timer_rate_max;
	unsigned long good_timer_sum = 0;
	unsigned long good_timer_count = 0;
	unsigned long measured_times[MAX_DIRECT_CALIBRATION_RETRIES];
	int max = -1; /* index of measured_times with max/min values or not set */
	int min = -1;
	int i;

	if (read_current_timer(&pre_start) < 0 )
		return 0;

	/*
	 * A simple loop like
	 *	while ( jiffies < start_jiffies+1)
	 *		start = read_current_timer();
	 * will not do. As we don't really know whether jiffy switch
	 * happened first or timer_value was read first. And some asynchronous
	 * event can happen between these two events introducing errors in lpj.
	 *
	 * So, we do
	 * 1. pre_start <- When we are sure that jiffy switch hasn't happened
	 * 2. check jiffy switch
	 * 3. start <- timer value before or after jiffy switch
	 * 4. post_start <- When we are sure that jiffy switch has happened
	 *
	 * Note, we don't know anything about order of 2 and 3.
	 * Now, by looking at post_start and pre_start difference, we can
	 * check whether any asynchronous event happened or not
	 */

	for (i = 0; i < MAX_DIRECT_CALIBRATION_RETRIES; i++) {
		pre_start = 0;
		read_current_timer(&start);
		start_jiffies = jiffies;
		while (time_before_eq(jiffies, start_jiffies + 1)) {
			pre_start = start;
			read_current_timer(&start);
		}
		read_current_timer(&post_start);

		pre_end = 0;
		end = post_start;
		while (time_before_eq(jiffies, start_jiffies + 1 +
					       DELAY_CALIBRATION_TICKS)) {
			pre_end = end;
			read_current_timer(&end);
		}
		read_current_timer(&post_end);

		timer_rate_max = (post_end - pre_start) /
					DELAY_CALIBRATION_TICKS;
		timer_rate_min = (pre_end - post_start) /
					DELAY_CALIBRATION_TICKS;

		/*
		 * If the upper limit and lower limit of the timer_rate is
		 * >= 12.5% apart, redo calibration.
		 */
		if (start >= post_end)
			printk(KERN_NOTICE "calibrate_delay_direct() ignoring "
					"timer_rate as we had a TSC wrap around"
					" start=%lu >=post_end=%lu\n",
				start, post_end);
		if (start < post_end && pre_start != 0 && pre_end != 0 &&
		    (timer_rate_max - timer_rate_min) < (timer_rate_max >> 3)) {
			good_timer_count++;
			good_timer_sum += timer_rate_max;
			measured_times[i] = timer_rate_max;
			if (max < 0 || timer_rate_max > measured_times[max])
				max = i;
			if (min < 0 || timer_rate_max < measured_times[min])
				min = i;
		} else
			measured_times[i] = 0;

	}

	/*
	 * Find the maximum & minimum - if they differ too much throw out the
	 * one with the largest difference from the mean and try again...
	 */
	while (good_timer_count > 1) {
		unsigned long estimate;
		unsigned long maxdiff;

		/* compute the estimate */
		estimate = (good_timer_sum/good_timer_count);
		maxdiff = estimate >> 3;

		/* if range is within 12% let's take it */
		if ((measured_times[max] - measured_times[min]) < maxdiff)
			return estimate;

		/* ok - drop the worse value and try again... */
		good_timer_sum = 0;
		good_timer_count = 0;
		if ((measured_times[max] - estimate) <
				(estimate - measured_times[min])) {
			printk(KERN_NOTICE "calibrate_delay_direct() dropping "
					"min bogoMips estimate %d = %lu\n",
				min, measured_times[min]);
			measured_times[min] = 0;
			min = max;
		} else {
			printk(KERN_NOTICE "calibrate_delay_direct() dropping "
					"max bogoMips estimate %d = %lu\n",
				max, measured_times[max]);
			measured_times[max] = 0;
			max = min;
		}

		for (i = 0; i < MAX_DIRECT_CALIBRATION_RETRIES; i++) {
			if (measured_times[i] == 0)
				continue;
			good_timer_count++;
			good_timer_sum += measured_times[i];
			if (measured_times[i] < measured_times[min])
				min = i;
			if (measured_times[i] > measured_times[max])
				max = i;
		}

	}

	printk(KERN_NOTICE "calibrate_delay_direct() failed to get a good "
	       "estimate for loops_per_jiffy.\nProbably due to long platform "
		"interrupts. Consider using \"lpj=\" boot option.\n");
	return 0;
}
#else
static unsigned long calibrate_delay_direct(void)
{
	return 0;
}
#endif

/*
 * This is the number of bits of precision for the loops_per_jiffy.  Each
 * time we refine our estimate after the first takes 1.5/HZ seconds, so try
 * to start with a good estimate.
 * For the boot cpu we can skip the delay calibration and assign it a value
 * calculated based on the timer frequency.
 * For the rest of the CPUs we cannot assume that the timer frequency is same as
 * the cpu frequency, hence do the calibration for those.
 */
#define LPS_PREC 8

static unsigned long calibrate_delay_converge(void)
{
	/* First stage - slowly accelerate to find initial bounds */
	unsigned long lpj, lpj_base, ticks, loopadd, loopadd_base, chop_limit;
	int trials = 0, band = 0, trial_in_band = 0;

	lpj = (1<<12);

	/* wait for "start of" clock tick */
	ticks = jiffies;
	while (ticks == jiffies)
		; /* nothing */
	/* Go .. */
	ticks = jiffies;
	do {
		if (++trial_in_band == (1<<band)) {
			++band;
			trial_in_band = 0;
		}
		__delay(lpj * band);
		trials += band;
	} while (ticks == jiffies);
	/*
	 * We overshot, so retreat to a clear underestimate. Then estimate
	 * the largest likely undershoot. This defines our chop bounds.
	 */
	trials -= band;
	loopadd_base = lpj * band;
	lpj_base = lpj * trials;

recalibrate:
	lpj = lpj_base;
	loopadd = loopadd_base;

	/*
	 * Do a binary approximation to get lpj set to
	 * equal one clock (up to LPS_PREC bits)
	 */
	chop_limit = lpj >> LPS_PREC;
	while (loopadd > chop_limit) {
		lpj += loopadd;
		ticks = jiffies;
		while (ticks == jiffies)
			; /* nothing */
		ticks = jiffies;
		__delay(lpj);
		if (jiffies != ticks)	/* longer than 1 tick */
			lpj -= loopadd;
		loopadd >>= 1;
	}
	/*
	 * If we incremented every single time possible, presume we've
	 * massively underestimated initially, and retry with a higher
	 * start, and larger range. (Only seen on x86_64, due to SMIs)
	 */
	if (lpj + loopadd * 2 == lpj_base + loopadd_base * 2) {
		lpj_base = lpj;
		loopadd_base <<= 2;
		goto recalibrate;
	}

	return lpj;
}

static DEFINE_PER_CPU(unsigned long, cpu_loops_per_jiffy) = { 0 };

/*
 * Check if cpu calibration delay is already known. For example,
 * some processors with multi-core sockets may have all cores
 * with the same calibration delay.
 *
 * Architectures should override this function if a faster calibration
 * method is available.
 */
unsigned long __attribute__((weak)) calibrate_delay_is_known(void)
{
	return 0;
}

void calibrate_delay(void)
{
	unsigned long lpj;
	static bool printed;
	int this_cpu = smp_processor_id();

	if (per_cpu(cpu_loops_per_jiffy, this_cpu)) {
		lpj = per_cpu(cpu_loops_per_jiffy, this_cpu);
		if (!printed)
			pr_info("Calibrating delay loop (skipped) "
				"already calibrated this CPU");
	} else if (preset_lpj) {
		lpj = preset_lpj;
		if (!printed)
			pr_info("Calibrating delay loop (skipped) "
				"preset value.. ");
	} else if ((!printed) && lpj_fine) {
		lpj = lpj_fine;
		pr_info("Calibrating delay loop (skipped), "
			"value calculated using timer frequency.. ");
	} else if ((lpj = calibrate_delay_is_known())) {
		;
	} else if ((lpj = calibrate_delay_direct()) != 0) {
		if (!printed)
			pr_info("Calibrating delay using timer "
				"specific routine.. ");
	} else {
		if (!printed)
			pr_info("Calibrating delay loop... ");
		lpj = calibrate_delay_converge();
	}
	per_cpu(cpu_loops_per_jiffy, this_cpu) = lpj;
	if (!printed)
		pr_cont("%lu.%02lu BogoMIPS (lpj=%lu)\n",
			lpj/(500000/HZ),
			(lpj/(5000/HZ)) % 100, lpj);

	loops_per_jiffy = lpj;
	printed = true;
}
