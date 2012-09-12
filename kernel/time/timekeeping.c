/*
 *  linux/kernel/time/timekeeping.c
 *
 *  Kernel timekeeping code and accessor functions
 *
 *  This code was moved from linux/kernel/timer.c.
 *  Please see that file for copyright and history logs.
 *
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/percpu.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/syscore_ops.h>
#include <linux/clocksource.h>
#include <linux/jiffies.h>
#include <linux/time.h>
#include <linux/tick.h>
#include <linux/stop_machine.h>

/* Structure holding internal timekeeping values. */
struct timekeeper {
	/* Current clocksource used for timekeeping. */
	struct clocksource *clock;
	/* The shift value of the current clocksource. */
	int	shift;

	/* Number of clock cycles in one NTP interval. */
	cycle_t cycle_interval;
	/* Number of clock shifted nano seconds in one NTP interval. */
	u64	xtime_interval;
	/* shifted nano seconds left over when rounding cycle_interval */
	s64	xtime_remainder;
	/* Raw nano seconds accumulated per NTP interval. */
	u32	raw_interval;

	/* Clock shifted nano seconds remainder not stored in xtime.tv_nsec. */
	u64	xtime_nsec;
	/* Difference between accumulated time and NTP time in ntp
	 * shifted nano seconds. */
	s64	ntp_error;
	/* Shift conversion between clock shifted nano seconds and
	 * ntp shifted nano seconds. */
	int	ntp_error_shift;
	/* NTP adjusted clock multiplier */
	u32	mult;
};

static struct timekeeper timekeeper;

/**
 * timekeeper_setup_internals - Set up internals to use clocksource clock.
 *
 * @clock:		Pointer to clocksource.
 *
 * Calculates a fixed cycle/nsec interval for a given clocksource/adjustment
 * pair and interval request.
 *
 * Unless you're the timekeeping code, you should not be using this!
 */
static void timekeeper_setup_internals(struct clocksource *clock)
{
	cycle_t interval;
	u64 tmp, ntpinterval;

	timekeeper.clock = clock;
	clock->cycle_last = clock->read(clock);

	/* Do the ns -> cycle conversion first, using original mult */
	tmp = NTP_INTERVAL_LENGTH;
	tmp <<= clock->shift;
	ntpinterval = tmp;
	tmp += clock->mult/2;
	do_div(tmp, clock->mult);
	if (tmp == 0)
		tmp = 1;

	interval = (cycle_t) tmp;
	timekeeper.cycle_interval = interval;

	/* Go back from cycles -> shifted ns */
	timekeeper.xtime_interval = (u64) interval * clock->mult;
	timekeeper.xtime_remainder = ntpinterval - timekeeper.xtime_interval;
	timekeeper.raw_interval =
		((u64) interval * clock->mult) >> clock->shift;

	timekeeper.xtime_nsec = 0;
	timekeeper.shift = clock->shift;

	timekeeper.ntp_error = 0;
	timekeeper.ntp_error_shift = NTP_SCALE_SHIFT - clock->shift;

	/*
	 * The timekeeper keeps its own mult values for the currently
	 * active clocksource. These value will be adjusted via NTP
	 * to counteract clock drifting.
	 */
	timekeeper.mult = clock->mult;
}

/* Timekeeper helper functions. */
static inline s64 timekeeping_get_ns(void)
{
	cycle_t cycle_now, cycle_delta;
	struct clocksource *clock;

	/* read clocksource: */
	clock = timekeeper.clock;
	cycle_now = clock->read(clock);

	/* calculate the delta since the last update_wall_time: */
	cycle_delta = (cycle_now - clock->cycle_last) & clock->mask;

	/* return delta convert to nanoseconds using ntp adjusted mult. */
	return clocksource_cyc2ns(cycle_delta, timekeeper.mult,
				  timekeeper.shift);
}

static inline s64 timekeeping_get_ns_raw(void)
{
	cycle_t cycle_now, cycle_delta;
	struct clocksource *clock;

	/* read clocksource: */
	clock = timekeeper.clock;
	cycle_now = clock->read(clock);

	/* calculate the delta since the last update_wall_time: */
	cycle_delta = (cycle_now - clock->cycle_last) & clock->mask;

	/* return delta convert to nanoseconds using ntp adjusted mult. */
	return clocksource_cyc2ns(cycle_delta, clock->mult, clock->shift);
}

/*
 * This read-write spinlock protects us from races in SMP while
 * playing with xtime.
 */
__cacheline_aligned_in_smp DEFINE_SEQLOCK(xtime_lock);


/*
 * The current time
 * wall_to_monotonic is what we need to add to xtime (or xtime corrected
 * for sub jiffie times) to get to monotonic time.  Monotonic is pegged
 * at zero at system boot time, so wall_to_monotonic will be negative,
 * however, we will ALWAYS keep the tv_nsec part positive so we can use
 * the usual normalization.
 *
 * wall_to_monotonic is moved after resume from suspend for the monotonic
 * time not to jump. We need to add total_sleep_time to wall_to_monotonic
 * to get the real boot based time offset.
 *
 * - wall_to_monotonic is no longer the boot time, getboottime must be
 * used instead.
 */
static struct timespec xtime __attribute__ ((aligned (16)));
static struct timespec wall_to_monotonic __attribute__ ((aligned (16)));
static struct timespec total_sleep_time;

/* Offset clock monotonic -> clock realtime */
static ktime_t offs_real;

/* Offset clock monotonic -> clock boottime */
static ktime_t offs_boot;

/*
 * The raw monotonic time for the CLOCK_MONOTONIC_RAW posix clock.
 */
static struct timespec raw_time;

/* must hold write on xtime_lock */
static void update_rt_offset(void)
{
	struct timespec tmp, *wtm = &wall_to_monotonic;

	set_normalized_timespec(&tmp, -wtm->tv_sec, -wtm->tv_nsec);
	offs_real = timespec_to_ktime(tmp);
}

/* must hold write on xtime_lock */
static void timekeeping_update(bool clearntp)
{
	if (clearntp) {
		timekeeper.ntp_error = 0;
		ntp_clear();
	}
	update_rt_offset();
	update_vsyscall(&xtime, &wall_to_monotonic,
			 timekeeper.clock, timekeeper.mult);
}



/* flag for if timekeeping is suspended */
int __read_mostly timekeeping_suspended;

/**
 * timekeeping_forward_now - update clock to the current time
 *
 * Forward the current clock to update its state since the last call to
 * update_wall_time(). This is useful before significant clock changes,
 * as it avoids having to deal with this time offset explicitly.
 */
static void timekeeping_forward_now(void)
{
	cycle_t cycle_now, cycle_delta;
	struct clocksource *clock;
	s64 nsec;

	clock = timekeeper.clock;
	cycle_now = clock->read(clock);
	cycle_delta = (cycle_now - clock->cycle_last) & clock->mask;
	clock->cycle_last = cycle_now;

	nsec = clocksource_cyc2ns(cycle_delta, timekeeper.mult,
				  timekeeper.shift);

	/* If arch requires, add in gettimeoffset() */
	nsec += arch_gettimeoffset();

	timespec_add_ns(&xtime, nsec);

	nsec = clocksource_cyc2ns(cycle_delta, clock->mult, clock->shift);
	timespec_add_ns(&raw_time, nsec);
}

/**
 * getnstimeofday - Returns the time of day in a timespec
 * @ts:		pointer to the timespec to be set
 *
 * Returns the time of day in a timespec.
 */
void getnstimeofday(struct timespec *ts)
{
	unsigned long seq;
	s64 nsecs;

	WARN_ON(timekeeping_suspended);

	do {
		seq = read_seqbegin(&xtime_lock);

		*ts = xtime;
		nsecs = timekeeping_get_ns();

		/* If arch requires, add in gettimeoffset() */
		nsecs += arch_gettimeoffset();

	} while (read_seqretry(&xtime_lock, seq));

	timespec_add_ns(ts, nsecs);
}

EXPORT_SYMBOL(getnstimeofday);

ktime_t ktime_get(void)
{
	unsigned int seq;
	s64 secs, nsecs;

	WARN_ON(timekeeping_suspended);

	do {
		seq = read_seqbegin(&xtime_lock);
		secs = xtime.tv_sec + wall_to_monotonic.tv_sec;
		nsecs = xtime.tv_nsec + wall_to_monotonic.tv_nsec;
		nsecs += timekeeping_get_ns();
		/* If arch requires, add in gettimeoffset() */
		nsecs += arch_gettimeoffset();

	} while (read_seqretry(&xtime_lock, seq));
	/*
	 * Use ktime_set/ktime_add_ns to create a proper ktime on
	 * 32-bit architectures without CONFIG_KTIME_SCALAR.
	 */
	return ktime_add_ns(ktime_set(secs, 0), nsecs);
}
EXPORT_SYMBOL_GPL(ktime_get);

/**
 * ktime_get_ts - get the monotonic clock in timespec format
 * @ts:		pointer to timespec variable
 *
 * The function calculates the monotonic clock from the realtime
 * clock and the wall_to_monotonic offset and stores the result
 * in normalized timespec format in the variable pointed to by @ts.
 */
void ktime_get_ts(struct timespec *ts)
{
	struct timespec tomono;
	unsigned int seq;
	s64 nsecs;

	WARN_ON(timekeeping_suspended);

	do {
		seq = read_seqbegin(&xtime_lock);
		*ts = xtime;
		tomono = wall_to_monotonic;
		nsecs = timekeeping_get_ns();
		/* If arch requires, add in gettimeoffset() */
		nsecs += arch_gettimeoffset();

	} while (read_seqretry(&xtime_lock, seq));

	set_normalized_timespec(ts, ts->tv_sec + tomono.tv_sec,
				ts->tv_nsec + tomono.tv_nsec + nsecs);
}
EXPORT_SYMBOL_GPL(ktime_get_ts);

#ifdef CONFIG_NTP_PPS

/**
 * getnstime_raw_and_real - get day and raw monotonic time in timespec format
 * @ts_raw:	pointer to the timespec to be set to raw monotonic time
 * @ts_real:	pointer to the timespec to be set to the time of day
 *
 * This function reads both the time of day and raw monotonic time at the
 * same time atomically and stores the resulting timestamps in timespec
 * format.
 */
void getnstime_raw_and_real(struct timespec *ts_raw, struct timespec *ts_real)
{
	unsigned long seq;
	s64 nsecs_raw, nsecs_real;

	WARN_ON_ONCE(timekeeping_suspended);

	do {
		u32 arch_offset;

		seq = read_seqbegin(&xtime_lock);

		*ts_raw = raw_time;
		*ts_real = xtime;

		nsecs_raw = timekeeping_get_ns_raw();
		nsecs_real = timekeeping_get_ns();

		/* If arch requires, add in gettimeoffset() */
		arch_offset = arch_gettimeoffset();
		nsecs_raw += arch_offset;
		nsecs_real += arch_offset;

	} while (read_seqretry(&xtime_lock, seq));

	timespec_add_ns(ts_raw, nsecs_raw);
	timespec_add_ns(ts_real, nsecs_real);
}
EXPORT_SYMBOL(getnstime_raw_and_real);

#endif /* CONFIG_NTP_PPS */

/**
 * do_gettimeofday - Returns the time of day in a timeval
 * @tv:		pointer to the timeval to be set
 *
 * NOTE: Users should be converted to using getnstimeofday()
 */
void do_gettimeofday(struct timeval *tv)
{
	struct timespec now;

	getnstimeofday(&now);
	tv->tv_sec = now.tv_sec;
	tv->tv_usec = now.tv_nsec/1000;
}

EXPORT_SYMBOL(do_gettimeofday);
/**
 * do_settimeofday - Sets the time of day
 * @tv:		pointer to the timespec variable containing the new time
 *
 * Sets the time of day to the new time and update NTP and notify hrtimers
 */
int do_settimeofday(const struct timespec *tv)
{
	struct timespec ts_delta;
	unsigned long flags;

	if (!timespec_valid_strict(tv))
		return -EINVAL;

	write_seqlock_irqsave(&xtime_lock, flags);

	timekeeping_forward_now();

	ts_delta.tv_sec = tv->tv_sec - xtime.tv_sec;
	ts_delta.tv_nsec = tv->tv_nsec - xtime.tv_nsec;
	wall_to_monotonic = timespec_sub(wall_to_monotonic, ts_delta);

	xtime = *tv;

	timekeeping_update(true);

	write_sequnlock_irqrestore(&xtime_lock, flags);

	/* signal hrtimers about time change */
	clock_was_set();

	return 0;
}

EXPORT_SYMBOL(do_settimeofday);


/**
 * timekeeping_inject_offset - Adds or subtracts from the current time.
 * @tv:		pointer to the timespec variable containing the offset
 *
 * Adds or subtracts an offset value from the current time.
 */
int timekeeping_inject_offset(struct timespec *ts)
{
	unsigned long flags;
	struct timespec tmp;
	int ret = 0;

	if ((unsigned long)ts->tv_nsec >= NSEC_PER_SEC)
		return -EINVAL;

	write_seqlock_irqsave(&xtime_lock, flags);

	timekeeping_forward_now();

	tmp = timespec_add(xtime,  *ts);
	if (!timespec_valid_strict(&tmp)) {
		ret = -EINVAL;
		goto error;
	}

	xtime = timespec_add(xtime, *ts);
	wall_to_monotonic = timespec_sub(wall_to_monotonic, *ts);

error: /* even if we error out, we forwarded the time, so call update */
	timekeeping_update(true);

	write_sequnlock_irqrestore(&xtime_lock, flags);

	/* signal hrtimers about time change */
	clock_was_set();

	return ret;
}
EXPORT_SYMBOL(timekeeping_inject_offset);

/**
 * change_clocksource - Swaps clocksources if a new one is available
 *
 * Accumulates current time interval and initializes new clocksource
 */
static int change_clocksource(void *data)
{
	struct clocksource *new, *old;

	new = (struct clocksource *) data;

	timekeeping_forward_now();
	if (!new->enable || new->enable(new) == 0) {
		old = timekeeper.clock;
		timekeeper_setup_internals(new);
		if (old->disable)
			old->disable(old);
	}
	return 0;
}

/**
 * timekeeping_notify - Install a new clock source
 * @clock:		pointer to the clock source
 *
 * This function is called from clocksource.c after a new, better clock
 * source has been registered. The caller holds the clocksource_mutex.
 */
void timekeeping_notify(struct clocksource *clock)
{
	if (timekeeper.clock == clock)
		return;
	stop_machine(change_clocksource, clock, NULL);
	tick_clock_notify();
}

/**
 * ktime_get_real - get the real (wall-) time in ktime_t format
 *
 * returns the time in ktime_t format
 */
ktime_t ktime_get_real(void)
{
	struct timespec now;

	getnstimeofday(&now);

	return timespec_to_ktime(now);
}
EXPORT_SYMBOL_GPL(ktime_get_real);

/**
 * getrawmonotonic - Returns the raw monotonic time in a timespec
 * @ts:		pointer to the timespec to be set
 *
 * Returns the raw monotonic time (completely un-modified by ntp)
 */
void getrawmonotonic(struct timespec *ts)
{
	unsigned long seq;
	s64 nsecs;

	do {
		seq = read_seqbegin(&xtime_lock);
		nsecs = timekeeping_get_ns_raw();
		*ts = raw_time;

	} while (read_seqretry(&xtime_lock, seq));

	timespec_add_ns(ts, nsecs);
}
EXPORT_SYMBOL(getrawmonotonic);


/**
 * timekeeping_valid_for_hres - Check if timekeeping is suitable for hres
 */
int timekeeping_valid_for_hres(void)
{
	unsigned long seq;
	int ret;

	do {
		seq = read_seqbegin(&xtime_lock);

		ret = timekeeper.clock->flags & CLOCK_SOURCE_VALID_FOR_HRES;

	} while (read_seqretry(&xtime_lock, seq));

	return ret;
}

/**
 * timekeeping_max_deferment - Returns max time the clocksource can be deferred
 *
 * Caller must observe xtime_lock via read_seqbegin/read_seqretry to
 * ensure that the clocksource does not change!
 */
u64 timekeeping_max_deferment(void)
{
	return timekeeper.clock->max_idle_ns;
}

/**
 * read_persistent_clock -  Return time from the persistent clock.
 *
 * Weak dummy function for arches that do not yet support it.
 * Reads the time from the battery backed persistent clock.
 * Returns a timespec with tv_sec=0 and tv_nsec=0 if unsupported.
 *
 *  XXX - Do be sure to remove it once all arches implement it.
 */
void __attribute__((weak)) read_persistent_clock(struct timespec *ts)
{
	ts->tv_sec = 0;
	ts->tv_nsec = 0;
}

/**
 * read_boot_clock -  Return time of the system start.
 *
 * Weak dummy function for arches that do not yet support it.
 * Function to read the exact time the system has been started.
 * Returns a timespec with tv_sec=0 and tv_nsec=0 if unsupported.
 *
 *  XXX - Do be sure to remove it once all arches implement it.
 */
void __attribute__((weak)) read_boot_clock(struct timespec *ts)
{
	ts->tv_sec = 0;
	ts->tv_nsec = 0;
}

/*
 * timekeeping_init - Initializes the clocksource and common timekeeping values
 */
void __init timekeeping_init(void)
{
	struct clocksource *clock;
	unsigned long flags;
	struct timespec now, boot;

	read_persistent_clock(&now);
	if (!timespec_valid_strict(&now)) {
		pr_warn("WARNING: Persistent clock returned invalid value!\n"
			"         Check your CMOS/BIOS settings.\n");
		now.tv_sec = 0;
		now.tv_nsec = 0;
	}

	read_boot_clock(&boot);
	if (!timespec_valid_strict(&boot)) {
		pr_warn("WARNING: Boot clock returned invalid value!\n"
			"         Check your CMOS/BIOS settings.\n");
		boot.tv_sec = 0;
		boot.tv_nsec = 0;
	}

	write_seqlock_irqsave(&xtime_lock, flags);

	ntp_init();

	clock = clocksource_default_clock();
	if (clock->enable)
		clock->enable(clock);
	timekeeper_setup_internals(clock);

	xtime.tv_sec = now.tv_sec;
	xtime.tv_nsec = now.tv_nsec;
	raw_time.tv_sec = 0;
	raw_time.tv_nsec = 0;
	if (boot.tv_sec == 0 && boot.tv_nsec == 0) {
		boot.tv_sec = xtime.tv_sec;
		boot.tv_nsec = xtime.tv_nsec;
	}
	set_normalized_timespec(&wall_to_monotonic,
				-boot.tv_sec, -boot.tv_nsec);
	update_rt_offset();
	total_sleep_time.tv_sec = 0;
	total_sleep_time.tv_nsec = 0;
	write_sequnlock_irqrestore(&xtime_lock, flags);
}

/* time in seconds when suspend began */
static struct timespec timekeeping_suspend_time;

static void update_sleep_time(struct timespec t)
{
	total_sleep_time = t;
	offs_boot = timespec_to_ktime(t);
}

/**
 * __timekeeping_inject_sleeptime - Internal function to add sleep interval
 * @delta: pointer to a timespec delta value
 *
 * Takes a timespec offset measuring a suspend interval and properly
 * adds the sleep offset to the timekeeping variables.
 */
static void __timekeeping_inject_sleeptime(struct timespec *delta)
{
	if (!timespec_valid_strict(delta)) {
		printk(KERN_WARNING "__timekeeping_inject_sleeptime: Invalid "
					"sleep delta value!\n");
		return;
	}

	xtime = timespec_add(xtime, *delta);
	wall_to_monotonic = timespec_sub(wall_to_monotonic, *delta);
	update_sleep_time(timespec_add(total_sleep_time, *delta));
}


/**
 * timekeeping_inject_sleeptime - Adds suspend interval to timeekeeping values
 * @delta: pointer to a timespec delta value
 *
 * This hook is for architectures that cannot support read_persistent_clock
 * because their RTC/persistent clock is only accessible when irqs are enabled.
 *
 * This function should only be called by rtc_resume(), and allows
 * a suspend offset to be injected into the timekeeping values.
 */
void timekeeping_inject_sleeptime(struct timespec *delta)
{
	unsigned long flags;
	struct timespec ts;

	/* Make sure we don't set the clock twice */
	read_persistent_clock(&ts);
	if (!(ts.tv_sec == 0 && ts.tv_nsec == 0))
		return;

	write_seqlock_irqsave(&xtime_lock, flags);
	timekeeping_forward_now();

	__timekeeping_inject_sleeptime(delta);

	timekeeping_update(true);

	write_sequnlock_irqrestore(&xtime_lock, flags);

	/* signal hrtimers about time change */
	clock_was_set();
}


/**
 * timekeeping_resume - Resumes the generic timekeeping subsystem.
 *
 * This is for the generic clocksource timekeeping.
 * xtime/wall_to_monotonic/jiffies/etc are
 * still managed by arch specific suspend/resume code.
 */
static void timekeeping_resume(void)
{
	unsigned long flags;
	struct timespec ts;

	read_persistent_clock(&ts);

	clocksource_resume();

	write_seqlock_irqsave(&xtime_lock, flags);

	if (timespec_compare(&ts, &timekeeping_suspend_time) > 0) {
		ts = timespec_sub(ts, timekeeping_suspend_time);
		__timekeeping_inject_sleeptime(&ts);
	}
	/* re-base the last cycle value */
	timekeeper.clock->cycle_last = timekeeper.clock->read(timekeeper.clock);
	timekeeper.ntp_error = 0;
	timekeeping_suspended = 0;
	timekeeping_update(false);
	write_sequnlock_irqrestore(&xtime_lock, flags);

	touch_softlockup_watchdog();

	clockevents_notify(CLOCK_EVT_NOTIFY_RESUME, NULL);

	/* Resume hrtimers */
	hrtimers_resume();
}

static int timekeeping_suspend(void)
{
	unsigned long flags;

	read_persistent_clock(&timekeeping_suspend_time);

	write_seqlock_irqsave(&xtime_lock, flags);
	timekeeping_forward_now();
	timekeeping_suspended = 1;
	write_sequnlock_irqrestore(&xtime_lock, flags);

	clockevents_notify(CLOCK_EVT_NOTIFY_SUSPEND, NULL);
	clocksource_suspend();

	return 0;
}

/* sysfs resume/suspend bits for timekeeping */
static struct syscore_ops timekeeping_syscore_ops = {
	.resume		= timekeeping_resume,
	.suspend	= timekeeping_suspend,
};

static int __init timekeeping_init_ops(void)
{
	register_syscore_ops(&timekeeping_syscore_ops);
	return 0;
}

device_initcall(timekeeping_init_ops);

/*
 * If the error is already larger, we look ahead even further
 * to compensate for late or lost adjustments.
 */
static __always_inline int timekeeping_bigadjust(s64 error, s64 *interval,
						 s64 *offset)
{
	s64 tick_error, i;
	u32 look_ahead, adj;
	s32 error2, mult;

	/*
	 * Use the current error value to determine how much to look ahead.
	 * The larger the error the slower we adjust for it to avoid problems
	 * with losing too many ticks, otherwise we would overadjust and
	 * produce an even larger error.  The smaller the adjustment the
	 * faster we try to adjust for it, as lost ticks can do less harm
	 * here.  This is tuned so that an error of about 1 msec is adjusted
	 * within about 1 sec (or 2^20 nsec in 2^SHIFT_HZ ticks).
	 */
	error2 = timekeeper.ntp_error >> (NTP_SCALE_SHIFT + 22 - 2 * SHIFT_HZ);
	error2 = abs(error2);
	for (look_ahead = 0; error2 > 0; look_ahead++)
		error2 >>= 2;

	/*
	 * Now calculate the error in (1 << look_ahead) ticks, but first
	 * remove the single look ahead already included in the error.
	 */
	tick_error = tick_length >> (timekeeper.ntp_error_shift + 1);
	tick_error -= timekeeper.xtime_interval >> 1;
	error = ((error - tick_error) >> look_ahead) + tick_error;

	/* Finally calculate the adjustment shift value.  */
	i = *interval;
	mult = 1;
	if (error < 0) {
		error = -error;
		*interval = -*interval;
		*offset = -*offset;
		mult = -1;
	}
	for (adj = 0; error > i; adj++)
		error >>= 1;

	*interval <<= adj;
	*offset <<= adj;
	return mult << adj;
}

/*
 * Adjust the multiplier to reduce the error value,
 * this is optimized for the most common adjustments of -1,0,1,
 * for other values we can do a bit more work.
 */
static void timekeeping_adjust(s64 offset)
{
	s64 error, interval = timekeeper.cycle_interval;
	int adj;

	error = timekeeper.ntp_error >> (timekeeper.ntp_error_shift - 1);
	if (error > interval) {
		error >>= 2;
		if (likely(error <= interval))
			adj = 1;
		else
			adj = timekeeping_bigadjust(error, &interval, &offset);
	} else if (error < -interval) {
		error >>= 2;
		if (likely(error >= -interval)) {
			adj = -1;
			interval = -interval;
			offset = -offset;
		} else
			adj = timekeeping_bigadjust(error, &interval, &offset);
	} else
		return;

	timekeeper.mult += adj;
	timekeeper.xtime_interval += interval;
	timekeeper.xtime_nsec -= offset;
	timekeeper.ntp_error -= (interval - offset) <<
				timekeeper.ntp_error_shift;
}


/**
 * logarithmic_accumulation - shifted accumulation of cycles
 *
 * This functions accumulates a shifted interval of cycles into
 * into a shifted interval nanoseconds. Allows for O(log) accumulation
 * loop.
 *
 * Returns the unconsumed cycles.
 */
static cycle_t logarithmic_accumulation(cycle_t offset, int shift)
{
	u64 nsecps = (u64)NSEC_PER_SEC << timekeeper.shift;
	u64 raw_nsecs;

	/* If the offset is smaller then a shifted interval, do nothing */
	if (offset < timekeeper.cycle_interval<<shift)
		return offset;

	/* Accumulate one shifted interval */
	offset -= timekeeper.cycle_interval << shift;
	timekeeper.clock->cycle_last += timekeeper.cycle_interval << shift;

	timekeeper.xtime_nsec += timekeeper.xtime_interval << shift;
	while (timekeeper.xtime_nsec >= nsecps) {
		int leap;
		timekeeper.xtime_nsec -= nsecps;
		xtime.tv_sec++;
		leap = second_overflow(xtime.tv_sec);
		xtime.tv_sec += leap;
		wall_to_monotonic.tv_sec -= leap;
		if (leap)
			clock_was_set_delayed();
	}

	/* Accumulate raw time */
	raw_nsecs = timekeeper.raw_interval << shift;
	raw_nsecs += raw_time.tv_nsec;
	if (raw_nsecs >= NSEC_PER_SEC) {
		u64 raw_secs = raw_nsecs;
		raw_nsecs = do_div(raw_secs, NSEC_PER_SEC);
		raw_time.tv_sec += raw_secs;
	}
	raw_time.tv_nsec = raw_nsecs;

	/* Accumulate error between NTP and clock interval */
	timekeeper.ntp_error += tick_length << shift;
	timekeeper.ntp_error -=
	    (timekeeper.xtime_interval + timekeeper.xtime_remainder) <<
				(timekeeper.ntp_error_shift + shift);

	return offset;
}


/**
 * update_wall_time - Uses the current clocksource to increment the wall time
 *
 * Called from the timer interrupt, must hold a write on xtime_lock.
 */
static void update_wall_time(void)
{
	struct clocksource *clock;
	cycle_t offset;
	int shift = 0, maxshift;

	/* Make sure we're fully resumed: */
	if (unlikely(timekeeping_suspended))
		return;

	clock = timekeeper.clock;

#ifdef CONFIG_ARCH_USES_GETTIMEOFFSET
	offset = timekeeper.cycle_interval;
#else
	offset = (clock->read(clock) - clock->cycle_last) & clock->mask;
#endif
	/* Check if there's really nothing to do */
	if (offset < timekeeper.cycle_interval)
		return;

	timekeeper.xtime_nsec = (s64)xtime.tv_nsec << timekeeper.shift;

	/*
	 * With NO_HZ we may have to accumulate many cycle_intervals
	 * (think "ticks") worth of time at once. To do this efficiently,
	 * we calculate the largest doubling multiple of cycle_intervals
	 * that is smaller then the offset. We then accumulate that
	 * chunk in one go, and then try to consume the next smaller
	 * doubled multiple.
	 */
	shift = ilog2(offset) - ilog2(timekeeper.cycle_interval);
	shift = max(0, shift);
	/* Bound shift to one less then what overflows tick_length */
	maxshift = (8*sizeof(tick_length) - (ilog2(tick_length)+1)) - 1;
	shift = min(shift, maxshift);
	while (offset >= timekeeper.cycle_interval) {
		offset = logarithmic_accumulation(offset, shift);
		if(offset < timekeeper.cycle_interval<<shift)
			shift--;
	}

	/* correct the clock when NTP error is too big */
	timekeeping_adjust(offset);

	/*
	 * Since in the loop above, we accumulate any amount of time
	 * in xtime_nsec over a second into xtime.tv_sec, its possible for
	 * xtime_nsec to be fairly small after the loop. Further, if we're
	 * slightly speeding the clocksource up in timekeeping_adjust(),
	 * its possible the required corrective factor to xtime_nsec could
	 * cause it to underflow.
	 *
	 * Now, we cannot simply roll the accumulated second back, since
	 * the NTP subsystem has been notified via second_overflow. So
	 * instead we push xtime_nsec forward by the amount we underflowed,
	 * and add that amount into the error.
	 *
	 * We'll correct this error next time through this function, when
	 * xtime_nsec is not as small.
	 */
	if (unlikely((s64)timekeeper.xtime_nsec < 0)) {
		s64 neg = -(s64)timekeeper.xtime_nsec;
		timekeeper.xtime_nsec = 0;
		timekeeper.ntp_error += neg << timekeeper.ntp_error_shift;
	}


	/*
	 * Store full nanoseconds into xtime after rounding it up and
	 * add the remainder to the error difference.
	 */
	xtime.tv_nsec =	((s64) timekeeper.xtime_nsec >> timekeeper.shift) + 1;
	timekeeper.xtime_nsec -= (s64) xtime.tv_nsec << timekeeper.shift;
	timekeeper.ntp_error +=	timekeeper.xtime_nsec <<
				timekeeper.ntp_error_shift;

	/*
	 * Finally, make sure that after the rounding
	 * xtime.tv_nsec isn't larger then NSEC_PER_SEC
	 */
	if (unlikely(xtime.tv_nsec >= NSEC_PER_SEC)) {
		int leap;
		xtime.tv_nsec -= NSEC_PER_SEC;
		xtime.tv_sec++;
		leap = second_overflow(xtime.tv_sec);
		xtime.tv_sec += leap;
		wall_to_monotonic.tv_sec -= leap;
		if (leap)
			clock_was_set_delayed();
	}

	timekeeping_update(false);
}

/**
 * getboottime - Return the real time of system boot.
 * @ts:		pointer to the timespec to be set
 *
 * Returns the wall-time of boot in a timespec.
 *
 * This is based on the wall_to_monotonic offset and the total suspend
 * time. Calls to settimeofday will affect the value returned (which
 * basically means that however wrong your real time clock is at boot time,
 * you get the right time here).
 */
void getboottime(struct timespec *ts)
{
	struct timespec boottime = {
		.tv_sec = wall_to_monotonic.tv_sec + total_sleep_time.tv_sec,
		.tv_nsec = wall_to_monotonic.tv_nsec + total_sleep_time.tv_nsec
	};

	set_normalized_timespec(ts, -boottime.tv_sec, -boottime.tv_nsec);
}
EXPORT_SYMBOL_GPL(getboottime);


/**
 * get_monotonic_boottime - Returns monotonic time since boot
 * @ts:		pointer to the timespec to be set
 *
 * Returns the monotonic time since boot in a timespec.
 *
 * This is similar to CLOCK_MONTONIC/ktime_get_ts, but also
 * includes the time spent in suspend.
 */
void get_monotonic_boottime(struct timespec *ts)
{
	struct timespec tomono, sleep;
	unsigned int seq;
	s64 nsecs;

	WARN_ON(timekeeping_suspended);

	do {
		seq = read_seqbegin(&xtime_lock);
		*ts = xtime;
		tomono = wall_to_monotonic;
		sleep = total_sleep_time;
		nsecs = timekeeping_get_ns();

	} while (read_seqretry(&xtime_lock, seq));

	set_normalized_timespec(ts, ts->tv_sec + tomono.tv_sec + sleep.tv_sec,
			ts->tv_nsec + tomono.tv_nsec + sleep.tv_nsec + nsecs);
}
EXPORT_SYMBOL_GPL(get_monotonic_boottime);

/**
 * ktime_get_boottime - Returns monotonic time since boot in a ktime
 *
 * Returns the monotonic time since boot in a ktime
 *
 * This is similar to CLOCK_MONTONIC/ktime_get, but also
 * includes the time spent in suspend.
 */
ktime_t ktime_get_boottime(void)
{
	struct timespec ts;

	get_monotonic_boottime(&ts);
	return timespec_to_ktime(ts);
}
EXPORT_SYMBOL_GPL(ktime_get_boottime);

/**
 * monotonic_to_bootbased - Convert the monotonic time to boot based.
 * @ts:		pointer to the timespec to be converted
 */
void monotonic_to_bootbased(struct timespec *ts)
{
	*ts = timespec_add(*ts, total_sleep_time);
}
EXPORT_SYMBOL_GPL(monotonic_to_bootbased);

unsigned long get_seconds(void)
{
	return xtime.tv_sec;
}
EXPORT_SYMBOL(get_seconds);

struct timespec __current_kernel_time(void)
{
	return xtime;
}

struct timespec current_kernel_time(void)
{
	struct timespec now;
	unsigned long seq;

	do {
		seq = read_seqbegin(&xtime_lock);

		now = xtime;
	} while (read_seqretry(&xtime_lock, seq));

	return now;
}
EXPORT_SYMBOL(current_kernel_time);

struct timespec get_monotonic_coarse(void)
{
	struct timespec now, mono;
	unsigned long seq;

	do {
		seq = read_seqbegin(&xtime_lock);

		now = xtime;
		mono = wall_to_monotonic;
	} while (read_seqretry(&xtime_lock, seq));

	set_normalized_timespec(&now, now.tv_sec + mono.tv_sec,
				now.tv_nsec + mono.tv_nsec);
	return now;
}

/*
 * The 64-bit jiffies value is not atomic - you MUST NOT read it
 * without sampling the sequence number in xtime_lock.
 * jiffies is defined in the linker script...
 */
void do_timer(unsigned long ticks)
{
	jiffies_64 += ticks;
	update_wall_time();
	calc_global_load(ticks);
}

/**
 * get_xtime_and_monotonic_and_sleep_offset() - get xtime, wall_to_monotonic,
 *    and sleep offsets.
 * @xtim:	pointer to timespec to be set with xtime
 * @wtom:	pointer to timespec to be set with wall_to_monotonic
 * @sleep:	pointer to timespec to be set with time in suspend
 */
void get_xtime_and_monotonic_and_sleep_offset(struct timespec *xtim,
				struct timespec *wtom, struct timespec *sleep)
{
	unsigned long seq;

	do {
		seq = read_seqbegin(&xtime_lock);
		*xtim = xtime;
		*wtom = wall_to_monotonic;
		*sleep = total_sleep_time;
	} while (read_seqretry(&xtime_lock, seq));
}

#ifdef CONFIG_HIGH_RES_TIMERS
/**
 * ktime_get_update_offsets - hrtimer helper
 * @real:	pointer to storage for monotonic -> realtime offset
 * @_boot:	pointer to storage for monotonic -> boottime offset
 *
 * Returns current monotonic time and updates the offsets
 * Called from hrtimer_interupt() or retrigger_next_event()
 */
ktime_t ktime_get_update_offsets(ktime_t *real, ktime_t *boot)
{
	ktime_t now;
	unsigned int seq;
	u64 secs, nsecs;

	do {
		seq = read_seqbegin(&xtime_lock);

		secs = xtime.tv_sec;
		nsecs = xtime.tv_nsec;
		nsecs += timekeeping_get_ns();
		/* If arch requires, add in gettimeoffset() */
		nsecs += arch_gettimeoffset();

		*real = offs_real;
		*boot = offs_boot;
	} while (read_seqretry(&xtime_lock, seq));

	now = ktime_add_ns(ktime_set(secs, 0), nsecs);
	now = ktime_sub(now, *real);
	return now;
}
#endif

/**
 * ktime_get_monotonic_offset() - get wall_to_monotonic in ktime_t format
 */
ktime_t ktime_get_monotonic_offset(void)
{
	unsigned long seq;
	struct timespec wtom;

	do {
		seq = read_seqbegin(&xtime_lock);
		wtom = wall_to_monotonic;
	} while (read_seqretry(&xtime_lock, seq));
	return timespec_to_ktime(wtom);
}

/**
 * xtime_update() - advances the timekeeping infrastructure
 * @ticks:	number of ticks, that have elapsed since the last call.
 *
 * Must be called with interrupts disabled.
 */
void xtime_update(unsigned long ticks)
{
	write_seqlock(&xtime_lock);
	do_timer(ticks);
	write_sequnlock(&xtime_lock);
}
