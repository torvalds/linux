/*-
 * SPDX-License-Identifier: Beerware
 *
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.ORG> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 * Copyright (c) 2011, 2015, 2016 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Julien Ridoux at the University
 * of Melbourne under sponsorship from the FreeBSD Foundation.
 *
 * Portions of this software were developed by Konstantin Belousov
 * under sponsorship from the FreeBSD Foundation.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ntp.h"
#include "opt_ffclock.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/sbuf.h>
#include <sys/sleepqueue.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/systm.h>
#include <sys/timeffc.h>
#include <sys/timepps.h>
#include <sys/timetc.h>
#include <sys/timex.h>
#include <sys/vdso.h>

/*
 * A large step happens on boot.  This constant detects such steps.
 * It is relatively small so that ntp_update_second gets called enough
 * in the typical 'missed a couple of seconds' case, but doesn't loop
 * forever when the time step is large.
 */
#define LARGE_STEP	200

/*
 * Implement a dummy timecounter which we can use until we get a real one
 * in the air.  This allows the console and other early stuff to use
 * time services.
 */

static u_int
dummy_get_timecount(struct timecounter *tc)
{
	static u_int now;

	return (++now);
}

static struct timecounter dummy_timecounter = {
	dummy_get_timecount, 0, ~0u, 1000000, "dummy", -1000000
};

struct timehands {
	/* These fields must be initialized by the driver. */
	struct timecounter	*th_counter;
	int64_t			th_adjustment;
	uint64_t		th_scale;
	u_int	 		th_offset_count;
	struct bintime		th_offset;
	struct bintime		th_bintime;
	struct timeval		th_microtime;
	struct timespec		th_nanotime;
	struct bintime		th_boottime;
	/* Fields not to be copied in tc_windup start with th_generation. */
	u_int			th_generation;
	struct timehands	*th_next;
};

static struct timehands th0;
static struct timehands th1 = {
	.th_next = &th0
};
static struct timehands th0 = {
	.th_counter = &dummy_timecounter,
	.th_scale = (uint64_t)-1 / 1000000,
	.th_offset = { .sec = 1 },
	.th_generation = 1,
	.th_next = &th1
};

static struct timehands *volatile timehands = &th0;
struct timecounter *timecounter = &dummy_timecounter;
static struct timecounter *timecounters = &dummy_timecounter;

int tc_min_ticktock_freq = 1;

volatile time_t time_second = 1;
volatile time_t time_uptime = 1;

static int sysctl_kern_boottime(SYSCTL_HANDLER_ARGS);
SYSCTL_PROC(_kern, KERN_BOOTTIME, boottime, CTLTYPE_STRUCT|CTLFLAG_RD,
    NULL, 0, sysctl_kern_boottime, "S,timeval", "System boottime");

SYSCTL_NODE(_kern, OID_AUTO, timecounter, CTLFLAG_RW, 0, "");
static SYSCTL_NODE(_kern_timecounter, OID_AUTO, tc, CTLFLAG_RW, 0, "");

static int timestepwarnings;
SYSCTL_INT(_kern_timecounter, OID_AUTO, stepwarnings, CTLFLAG_RW,
    &timestepwarnings, 0, "Log time steps");

struct bintime bt_timethreshold;
struct bintime bt_tickthreshold;
sbintime_t sbt_timethreshold;
sbintime_t sbt_tickthreshold;
struct bintime tc_tick_bt;
sbintime_t tc_tick_sbt;
int tc_precexp;
int tc_timepercentage = TC_DEFAULTPERC;
static int sysctl_kern_timecounter_adjprecision(SYSCTL_HANDLER_ARGS);
SYSCTL_PROC(_kern_timecounter, OID_AUTO, alloweddeviation,
    CTLTYPE_INT | CTLFLAG_RWTUN | CTLFLAG_MPSAFE, 0, 0,
    sysctl_kern_timecounter_adjprecision, "I",
    "Allowed time interval deviation in percents");

volatile int rtc_generation = 1;

static int tc_chosen;	/* Non-zero if a specific tc was chosen via sysctl. */

static void tc_windup(struct bintime *new_boottimebin);
static void cpu_tick_calibrate(int);

void dtrace_getnanotime(struct timespec *tsp);

static int
sysctl_kern_boottime(SYSCTL_HANDLER_ARGS)
{
	struct timeval boottime;

	getboottime(&boottime);

/* i386 is the only arch which uses a 32bits time_t */
#ifdef __amd64__
#ifdef SCTL_MASK32
	int tv[2];

	if (req->flags & SCTL_MASK32) {
		tv[0] = boottime.tv_sec;
		tv[1] = boottime.tv_usec;
		return (SYSCTL_OUT(req, tv, sizeof(tv)));
	}
#endif
#endif
	return (SYSCTL_OUT(req, &boottime, sizeof(boottime)));
}

static int
sysctl_kern_timecounter_get(SYSCTL_HANDLER_ARGS)
{
	u_int ncount;
	struct timecounter *tc = arg1;

	ncount = tc->tc_get_timecount(tc);
	return (sysctl_handle_int(oidp, &ncount, 0, req));
}

static int
sysctl_kern_timecounter_freq(SYSCTL_HANDLER_ARGS)
{
	uint64_t freq;
	struct timecounter *tc = arg1;

	freq = tc->tc_frequency;
	return (sysctl_handle_64(oidp, &freq, 0, req));
}

/*
 * Return the difference between the timehands' counter value now and what
 * was when we copied it to the timehands' offset_count.
 */
static __inline u_int
tc_delta(struct timehands *th)
{
	struct timecounter *tc;

	tc = th->th_counter;
	return ((tc->tc_get_timecount(tc) - th->th_offset_count) &
	    tc->tc_counter_mask);
}

/*
 * Functions for reading the time.  We have to loop until we are sure that
 * the timehands that we operated on was not updated under our feet.  See
 * the comment in <sys/time.h> for a description of these 12 functions.
 */

#ifdef FFCLOCK
void
fbclock_binuptime(struct bintime *bt)
{
	struct timehands *th;
	unsigned int gen;

	do {
		th = timehands;
		gen = atomic_load_acq_int(&th->th_generation);
		*bt = th->th_offset;
		bintime_addx(bt, th->th_scale * tc_delta(th));
		atomic_thread_fence_acq();
	} while (gen == 0 || gen != th->th_generation);
}

void
fbclock_nanouptime(struct timespec *tsp)
{
	struct bintime bt;

	fbclock_binuptime(&bt);
	bintime2timespec(&bt, tsp);
}

void
fbclock_microuptime(struct timeval *tvp)
{
	struct bintime bt;

	fbclock_binuptime(&bt);
	bintime2timeval(&bt, tvp);
}

void
fbclock_bintime(struct bintime *bt)
{
	struct timehands *th;
	unsigned int gen;

	do {
		th = timehands;
		gen = atomic_load_acq_int(&th->th_generation);
		*bt = th->th_bintime;
		bintime_addx(bt, th->th_scale * tc_delta(th));
		atomic_thread_fence_acq();
	} while (gen == 0 || gen != th->th_generation);
}

void
fbclock_nanotime(struct timespec *tsp)
{
	struct bintime bt;

	fbclock_bintime(&bt);
	bintime2timespec(&bt, tsp);
}

void
fbclock_microtime(struct timeval *tvp)
{
	struct bintime bt;

	fbclock_bintime(&bt);
	bintime2timeval(&bt, tvp);
}

void
fbclock_getbinuptime(struct bintime *bt)
{
	struct timehands *th;
	unsigned int gen;

	do {
		th = timehands;
		gen = atomic_load_acq_int(&th->th_generation);
		*bt = th->th_offset;
		atomic_thread_fence_acq();
	} while (gen == 0 || gen != th->th_generation);
}

void
fbclock_getnanouptime(struct timespec *tsp)
{
	struct timehands *th;
	unsigned int gen;

	do {
		th = timehands;
		gen = atomic_load_acq_int(&th->th_generation);
		bintime2timespec(&th->th_offset, tsp);
		atomic_thread_fence_acq();
	} while (gen == 0 || gen != th->th_generation);
}

void
fbclock_getmicrouptime(struct timeval *tvp)
{
	struct timehands *th;
	unsigned int gen;

	do {
		th = timehands;
		gen = atomic_load_acq_int(&th->th_generation);
		bintime2timeval(&th->th_offset, tvp);
		atomic_thread_fence_acq();
	} while (gen == 0 || gen != th->th_generation);
}

void
fbclock_getbintime(struct bintime *bt)
{
	struct timehands *th;
	unsigned int gen;

	do {
		th = timehands;
		gen = atomic_load_acq_int(&th->th_generation);
		*bt = th->th_bintime;
		atomic_thread_fence_acq();
	} while (gen == 0 || gen != th->th_generation);
}

void
fbclock_getnanotime(struct timespec *tsp)
{
	struct timehands *th;
	unsigned int gen;

	do {
		th = timehands;
		gen = atomic_load_acq_int(&th->th_generation);
		*tsp = th->th_nanotime;
		atomic_thread_fence_acq();
	} while (gen == 0 || gen != th->th_generation);
}

void
fbclock_getmicrotime(struct timeval *tvp)
{
	struct timehands *th;
	unsigned int gen;

	do {
		th = timehands;
		gen = atomic_load_acq_int(&th->th_generation);
		*tvp = th->th_microtime;
		atomic_thread_fence_acq();
	} while (gen == 0 || gen != th->th_generation);
}
#else /* !FFCLOCK */
void
binuptime(struct bintime *bt)
{
	struct timehands *th;
	u_int gen;

	do {
		th = timehands;
		gen = atomic_load_acq_int(&th->th_generation);
		*bt = th->th_offset;
		bintime_addx(bt, th->th_scale * tc_delta(th));
		atomic_thread_fence_acq();
	} while (gen == 0 || gen != th->th_generation);
}

void
nanouptime(struct timespec *tsp)
{
	struct bintime bt;

	binuptime(&bt);
	bintime2timespec(&bt, tsp);
}

void
microuptime(struct timeval *tvp)
{
	struct bintime bt;

	binuptime(&bt);
	bintime2timeval(&bt, tvp);
}

void
bintime(struct bintime *bt)
{
	struct timehands *th;
	u_int gen;

	do {
		th = timehands;
		gen = atomic_load_acq_int(&th->th_generation);
		*bt = th->th_bintime;
		bintime_addx(bt, th->th_scale * tc_delta(th));
		atomic_thread_fence_acq();
	} while (gen == 0 || gen != th->th_generation);
}

void
nanotime(struct timespec *tsp)
{
	struct bintime bt;

	bintime(&bt);
	bintime2timespec(&bt, tsp);
}

void
microtime(struct timeval *tvp)
{
	struct bintime bt;

	bintime(&bt);
	bintime2timeval(&bt, tvp);
}

void
getbinuptime(struct bintime *bt)
{
	struct timehands *th;
	u_int gen;

	do {
		th = timehands;
		gen = atomic_load_acq_int(&th->th_generation);
		*bt = th->th_offset;
		atomic_thread_fence_acq();
	} while (gen == 0 || gen != th->th_generation);
}

void
getnanouptime(struct timespec *tsp)
{
	struct timehands *th;
	u_int gen;

	do {
		th = timehands;
		gen = atomic_load_acq_int(&th->th_generation);
		bintime2timespec(&th->th_offset, tsp);
		atomic_thread_fence_acq();
	} while (gen == 0 || gen != th->th_generation);
}

void
getmicrouptime(struct timeval *tvp)
{
	struct timehands *th;
	u_int gen;

	do {
		th = timehands;
		gen = atomic_load_acq_int(&th->th_generation);
		bintime2timeval(&th->th_offset, tvp);
		atomic_thread_fence_acq();
	} while (gen == 0 || gen != th->th_generation);
}

void
getbintime(struct bintime *bt)
{
	struct timehands *th;
	u_int gen;

	do {
		th = timehands;
		gen = atomic_load_acq_int(&th->th_generation);
		*bt = th->th_bintime;
		atomic_thread_fence_acq();
	} while (gen == 0 || gen != th->th_generation);
}

void
getnanotime(struct timespec *tsp)
{
	struct timehands *th;
	u_int gen;

	do {
		th = timehands;
		gen = atomic_load_acq_int(&th->th_generation);
		*tsp = th->th_nanotime;
		atomic_thread_fence_acq();
	} while (gen == 0 || gen != th->th_generation);
}

void
getmicrotime(struct timeval *tvp)
{
	struct timehands *th;
	u_int gen;

	do {
		th = timehands;
		gen = atomic_load_acq_int(&th->th_generation);
		*tvp = th->th_microtime;
		atomic_thread_fence_acq();
	} while (gen == 0 || gen != th->th_generation);
}
#endif /* FFCLOCK */

void
getboottime(struct timeval *boottime)
{
	struct bintime boottimebin;

	getboottimebin(&boottimebin);
	bintime2timeval(&boottimebin, boottime);
}

void
getboottimebin(struct bintime *boottimebin)
{
	struct timehands *th;
	u_int gen;

	do {
		th = timehands;
		gen = atomic_load_acq_int(&th->th_generation);
		*boottimebin = th->th_boottime;
		atomic_thread_fence_acq();
	} while (gen == 0 || gen != th->th_generation);
}

#ifdef FFCLOCK
/*
 * Support for feed-forward synchronization algorithms. This is heavily inspired
 * by the timehands mechanism but kept independent from it. *_windup() functions
 * have some connection to avoid accessing the timecounter hardware more than
 * necessary.
 */

/* Feed-forward clock estimates kept updated by the synchronization daemon. */
struct ffclock_estimate ffclock_estimate;
struct bintime ffclock_boottime;	/* Feed-forward boot time estimate. */
uint32_t ffclock_status;		/* Feed-forward clock status. */
int8_t ffclock_updated;			/* New estimates are available. */
struct mtx ffclock_mtx;			/* Mutex on ffclock_estimate. */

struct fftimehands {
	struct ffclock_estimate	cest;
	struct bintime		tick_time;
	struct bintime		tick_time_lerp;
	ffcounter		tick_ffcount;
	uint64_t		period_lerp;
	volatile uint8_t	gen;
	struct fftimehands	*next;
};

#define	NUM_ELEMENTS(x) (sizeof(x) / sizeof(*x))

static struct fftimehands ffth[10];
static struct fftimehands *volatile fftimehands = ffth;

static void
ffclock_init(void)
{
	struct fftimehands *cur;
	struct fftimehands *last;

	memset(ffth, 0, sizeof(ffth));

	last = ffth + NUM_ELEMENTS(ffth) - 1;
	for (cur = ffth; cur < last; cur++)
		cur->next = cur + 1;
	last->next = ffth;

	ffclock_updated = 0;
	ffclock_status = FFCLOCK_STA_UNSYNC;
	mtx_init(&ffclock_mtx, "ffclock lock", NULL, MTX_DEF);
}

/*
 * Reset the feed-forward clock estimates. Called from inittodr() to get things
 * kick started and uses the timecounter nominal frequency as a first period
 * estimate. Note: this function may be called several time just after boot.
 * Note: this is the only function that sets the value of boot time for the
 * monotonic (i.e. uptime) version of the feed-forward clock.
 */
void
ffclock_reset_clock(struct timespec *ts)
{
	struct timecounter *tc;
	struct ffclock_estimate cest;

	tc = timehands->th_counter;
	memset(&cest, 0, sizeof(struct ffclock_estimate));

	timespec2bintime(ts, &ffclock_boottime);
	timespec2bintime(ts, &(cest.update_time));
	ffclock_read_counter(&cest.update_ffcount);
	cest.leapsec_next = 0;
	cest.period = ((1ULL << 63) / tc->tc_frequency) << 1;
	cest.errb_abs = 0;
	cest.errb_rate = 0;
	cest.status = FFCLOCK_STA_UNSYNC;
	cest.leapsec_total = 0;
	cest.leapsec = 0;

	mtx_lock(&ffclock_mtx);
	bcopy(&cest, &ffclock_estimate, sizeof(struct ffclock_estimate));
	ffclock_updated = INT8_MAX;
	mtx_unlock(&ffclock_mtx);

	printf("ffclock reset: %s (%llu Hz), time = %ld.%09lu\n", tc->tc_name,
	    (unsigned long long)tc->tc_frequency, (long)ts->tv_sec,
	    (unsigned long)ts->tv_nsec);
}

/*
 * Sub-routine to convert a time interval measured in RAW counter units to time
 * in seconds stored in bintime format.
 * NOTE: bintime_mul requires u_int, but the value of the ffcounter may be
 * larger than the max value of u_int (on 32 bit architecture). Loop to consume
 * extra cycles.
 */
static void
ffclock_convert_delta(ffcounter ffdelta, uint64_t period, struct bintime *bt)
{
	struct bintime bt2;
	ffcounter delta, delta_max;

	delta_max = (1ULL << (8 * sizeof(unsigned int))) - 1;
	bintime_clear(bt);
	do {
		if (ffdelta > delta_max)
			delta = delta_max;
		else
			delta = ffdelta;
		bt2.sec = 0;
		bt2.frac = period;
		bintime_mul(&bt2, (unsigned int)delta);
		bintime_add(bt, &bt2);
		ffdelta -= delta;
	} while (ffdelta > 0);
}

/*
 * Update the fftimehands.
 * Push the tick ffcount and time(s) forward based on current clock estimate.
 * The conversion from ffcounter to bintime relies on the difference clock
 * principle, whose accuracy relies on computing small time intervals. If a new
 * clock estimate has been passed by the synchronisation daemon, make it
 * current, and compute the linear interpolation for monotonic time if needed.
 */
static void
ffclock_windup(unsigned int delta)
{
	struct ffclock_estimate *cest;
	struct fftimehands *ffth;
	struct bintime bt, gap_lerp;
	ffcounter ffdelta;
	uint64_t frac;
	unsigned int polling;
	uint8_t forward_jump, ogen;

	/*
	 * Pick the next timehand, copy current ffclock estimates and move tick
	 * times and counter forward.
	 */
	forward_jump = 0;
	ffth = fftimehands->next;
	ogen = ffth->gen;
	ffth->gen = 0;
	cest = &ffth->cest;
	bcopy(&fftimehands->cest, cest, sizeof(struct ffclock_estimate));
	ffdelta = (ffcounter)delta;
	ffth->period_lerp = fftimehands->period_lerp;

	ffth->tick_time = fftimehands->tick_time;
	ffclock_convert_delta(ffdelta, cest->period, &bt);
	bintime_add(&ffth->tick_time, &bt);

	ffth->tick_time_lerp = fftimehands->tick_time_lerp;
	ffclock_convert_delta(ffdelta, ffth->period_lerp, &bt);
	bintime_add(&ffth->tick_time_lerp, &bt);

	ffth->tick_ffcount = fftimehands->tick_ffcount + ffdelta;

	/*
	 * Assess the status of the clock, if the last update is too old, it is
	 * likely the synchronisation daemon is dead and the clock is free
	 * running.
	 */
	if (ffclock_updated == 0) {
		ffdelta = ffth->tick_ffcount - cest->update_ffcount;
		ffclock_convert_delta(ffdelta, cest->period, &bt);
		if (bt.sec > 2 * FFCLOCK_SKM_SCALE)
			ffclock_status |= FFCLOCK_STA_UNSYNC;
	}

	/*
	 * If available, grab updated clock estimates and make them current.
	 * Recompute time at this tick using the updated estimates. The clock
	 * estimates passed the feed-forward synchronisation daemon may result
	 * in time conversion that is not monotonically increasing (just after
	 * the update). time_lerp is a particular linear interpolation over the
	 * synchronisation algo polling period that ensures monotonicity for the
	 * clock ids requesting it.
	 */
	if (ffclock_updated > 0) {
		bcopy(&ffclock_estimate, cest, sizeof(struct ffclock_estimate));
		ffdelta = ffth->tick_ffcount - cest->update_ffcount;
		ffth->tick_time = cest->update_time;
		ffclock_convert_delta(ffdelta, cest->period, &bt);
		bintime_add(&ffth->tick_time, &bt);

		/* ffclock_reset sets ffclock_updated to INT8_MAX */
		if (ffclock_updated == INT8_MAX)
			ffth->tick_time_lerp = ffth->tick_time;

		if (bintime_cmp(&ffth->tick_time, &ffth->tick_time_lerp, >))
			forward_jump = 1;
		else
			forward_jump = 0;

		bintime_clear(&gap_lerp);
		if (forward_jump) {
			gap_lerp = ffth->tick_time;
			bintime_sub(&gap_lerp, &ffth->tick_time_lerp);
		} else {
			gap_lerp = ffth->tick_time_lerp;
			bintime_sub(&gap_lerp, &ffth->tick_time);
		}

		/*
		 * The reset from the RTC clock may be far from accurate, and
		 * reducing the gap between real time and interpolated time
		 * could take a very long time if the interpolated clock insists
		 * on strict monotonicity. The clock is reset under very strict
		 * conditions (kernel time is known to be wrong and
		 * synchronization daemon has been restarted recently.
		 * ffclock_boottime absorbs the jump to ensure boot time is
		 * correct and uptime functions stay consistent.
		 */
		if (((ffclock_status & FFCLOCK_STA_UNSYNC) == FFCLOCK_STA_UNSYNC) &&
		    ((cest->status & FFCLOCK_STA_UNSYNC) == 0) &&
		    ((cest->status & FFCLOCK_STA_WARMUP) == FFCLOCK_STA_WARMUP)) {
			if (forward_jump)
				bintime_add(&ffclock_boottime, &gap_lerp);
			else
				bintime_sub(&ffclock_boottime, &gap_lerp);
			ffth->tick_time_lerp = ffth->tick_time;
			bintime_clear(&gap_lerp);
		}

		ffclock_status = cest->status;
		ffth->period_lerp = cest->period;

		/*
		 * Compute corrected period used for the linear interpolation of
		 * time. The rate of linear interpolation is capped to 5000PPM
		 * (5ms/s).
		 */
		if (bintime_isset(&gap_lerp)) {
			ffdelta = cest->update_ffcount;
			ffdelta -= fftimehands->cest.update_ffcount;
			ffclock_convert_delta(ffdelta, cest->period, &bt);
			polling = bt.sec;
			bt.sec = 0;
			bt.frac = 5000000 * (uint64_t)18446744073LL;
			bintime_mul(&bt, polling);
			if (bintime_cmp(&gap_lerp, &bt, >))
				gap_lerp = bt;

			/* Approximate 1 sec by 1-(1/2^64) to ease arithmetic */
			frac = 0;
			if (gap_lerp.sec > 0) {
				frac -= 1;
				frac /= ffdelta / gap_lerp.sec;
			}
			frac += gap_lerp.frac / ffdelta;

			if (forward_jump)
				ffth->period_lerp += frac;
			else
				ffth->period_lerp -= frac;
		}

		ffclock_updated = 0;
	}
	if (++ogen == 0)
		ogen = 1;
	ffth->gen = ogen;
	fftimehands = ffth;
}

/*
 * Adjust the fftimehands when the timecounter is changed. Stating the obvious,
 * the old and new hardware counter cannot be read simultaneously. tc_windup()
 * does read the two counters 'back to back', but a few cycles are effectively
 * lost, and not accumulated in tick_ffcount. This is a fairly radical
 * operation for a feed-forward synchronization daemon, and it is its job to not
 * pushing irrelevant data to the kernel. Because there is no locking here,
 * simply force to ignore pending or next update to give daemon a chance to
 * realize the counter has changed.
 */
static void
ffclock_change_tc(struct timehands *th)
{
	struct fftimehands *ffth;
	struct ffclock_estimate *cest;
	struct timecounter *tc;
	uint8_t ogen;

	tc = th->th_counter;
	ffth = fftimehands->next;
	ogen = ffth->gen;
	ffth->gen = 0;

	cest = &ffth->cest;
	bcopy(&(fftimehands->cest), cest, sizeof(struct ffclock_estimate));
	cest->period = ((1ULL << 63) / tc->tc_frequency ) << 1;
	cest->errb_abs = 0;
	cest->errb_rate = 0;
	cest->status |= FFCLOCK_STA_UNSYNC;

	ffth->tick_ffcount = fftimehands->tick_ffcount;
	ffth->tick_time_lerp = fftimehands->tick_time_lerp;
	ffth->tick_time = fftimehands->tick_time;
	ffth->period_lerp = cest->period;

	/* Do not lock but ignore next update from synchronization daemon. */
	ffclock_updated--;

	if (++ogen == 0)
		ogen = 1;
	ffth->gen = ogen;
	fftimehands = ffth;
}

/*
 * Retrieve feed-forward counter and time of last kernel tick.
 */
void
ffclock_last_tick(ffcounter *ffcount, struct bintime *bt, uint32_t flags)
{
	struct fftimehands *ffth;
	uint8_t gen;

	/*
	 * No locking but check generation has not changed. Also need to make
	 * sure ffdelta is positive, i.e. ffcount > tick_ffcount.
	 */
	do {
		ffth = fftimehands;
		gen = ffth->gen;
		if ((flags & FFCLOCK_LERP) == FFCLOCK_LERP)
			*bt = ffth->tick_time_lerp;
		else
			*bt = ffth->tick_time;
		*ffcount = ffth->tick_ffcount;
	} while (gen == 0 || gen != ffth->gen);
}

/*
 * Absolute clock conversion. Low level function to convert ffcounter to
 * bintime. The ffcounter is converted using the current ffclock period estimate
 * or the "interpolated period" to ensure monotonicity.
 * NOTE: this conversion may have been deferred, and the clock updated since the
 * hardware counter has been read.
 */
void
ffclock_convert_abs(ffcounter ffcount, struct bintime *bt, uint32_t flags)
{
	struct fftimehands *ffth;
	struct bintime bt2;
	ffcounter ffdelta;
	uint8_t gen;

	/*
	 * No locking but check generation has not changed. Also need to make
	 * sure ffdelta is positive, i.e. ffcount > tick_ffcount.
	 */
	do {
		ffth = fftimehands;
		gen = ffth->gen;
		if (ffcount > ffth->tick_ffcount)
			ffdelta = ffcount - ffth->tick_ffcount;
		else
			ffdelta = ffth->tick_ffcount - ffcount;

		if ((flags & FFCLOCK_LERP) == FFCLOCK_LERP) {
			*bt = ffth->tick_time_lerp;
			ffclock_convert_delta(ffdelta, ffth->period_lerp, &bt2);
		} else {
			*bt = ffth->tick_time;
			ffclock_convert_delta(ffdelta, ffth->cest.period, &bt2);
		}

		if (ffcount > ffth->tick_ffcount)
			bintime_add(bt, &bt2);
		else
			bintime_sub(bt, &bt2);
	} while (gen == 0 || gen != ffth->gen);
}

/*
 * Difference clock conversion.
 * Low level function to Convert a time interval measured in RAW counter units
 * into bintime. The difference clock allows measuring small intervals much more
 * reliably than the absolute clock.
 */
void
ffclock_convert_diff(ffcounter ffdelta, struct bintime *bt)
{
	struct fftimehands *ffth;
	uint8_t gen;

	/* No locking but check generation has not changed. */
	do {
		ffth = fftimehands;
		gen = ffth->gen;
		ffclock_convert_delta(ffdelta, ffth->cest.period, bt);
	} while (gen == 0 || gen != ffth->gen);
}

/*
 * Access to current ffcounter value.
 */
void
ffclock_read_counter(ffcounter *ffcount)
{
	struct timehands *th;
	struct fftimehands *ffth;
	unsigned int gen, delta;

	/*
	 * ffclock_windup() called from tc_windup(), safe to rely on
	 * th->th_generation only, for correct delta and ffcounter.
	 */
	do {
		th = timehands;
		gen = atomic_load_acq_int(&th->th_generation);
		ffth = fftimehands;
		delta = tc_delta(th);
		*ffcount = ffth->tick_ffcount;
		atomic_thread_fence_acq();
	} while (gen == 0 || gen != th->th_generation);

	*ffcount += delta;
}

void
binuptime(struct bintime *bt)
{

	binuptime_fromclock(bt, sysclock_active);
}

void
nanouptime(struct timespec *tsp)
{

	nanouptime_fromclock(tsp, sysclock_active);
}

void
microuptime(struct timeval *tvp)
{

	microuptime_fromclock(tvp, sysclock_active);
}

void
bintime(struct bintime *bt)
{

	bintime_fromclock(bt, sysclock_active);
}

void
nanotime(struct timespec *tsp)
{

	nanotime_fromclock(tsp, sysclock_active);
}

void
microtime(struct timeval *tvp)
{

	microtime_fromclock(tvp, sysclock_active);
}

void
getbinuptime(struct bintime *bt)
{

	getbinuptime_fromclock(bt, sysclock_active);
}

void
getnanouptime(struct timespec *tsp)
{

	getnanouptime_fromclock(tsp, sysclock_active);
}

void
getmicrouptime(struct timeval *tvp)
{

	getmicrouptime_fromclock(tvp, sysclock_active);
}

void
getbintime(struct bintime *bt)
{

	getbintime_fromclock(bt, sysclock_active);
}

void
getnanotime(struct timespec *tsp)
{

	getnanotime_fromclock(tsp, sysclock_active);
}

void
getmicrotime(struct timeval *tvp)
{

	getmicrouptime_fromclock(tvp, sysclock_active);
}

#endif /* FFCLOCK */

/*
 * This is a clone of getnanotime and used for walltimestamps.
 * The dtrace_ prefix prevents fbt from creating probes for
 * it so walltimestamp can be safely used in all fbt probes.
 */
void
dtrace_getnanotime(struct timespec *tsp)
{
	struct timehands *th;
	u_int gen;

	do {
		th = timehands;
		gen = atomic_load_acq_int(&th->th_generation);
		*tsp = th->th_nanotime;
		atomic_thread_fence_acq();
	} while (gen == 0 || gen != th->th_generation);
}

/*
 * System clock currently providing time to the system. Modifiable via sysctl
 * when the FFCLOCK option is defined.
 */
int sysclock_active = SYSCLOCK_FBCK;

/* Internal NTP status and error estimates. */
extern int time_status;
extern long time_esterror;

/*
 * Take a snapshot of sysclock data which can be used to compare system clocks
 * and generate timestamps after the fact.
 */
void
sysclock_getsnapshot(struct sysclock_snap *clock_snap, int fast)
{
	struct fbclock_info *fbi;
	struct timehands *th;
	struct bintime bt;
	unsigned int delta, gen;
#ifdef FFCLOCK
	ffcounter ffcount;
	struct fftimehands *ffth;
	struct ffclock_info *ffi;
	struct ffclock_estimate cest;

	ffi = &clock_snap->ff_info;
#endif

	fbi = &clock_snap->fb_info;
	delta = 0;

	do {
		th = timehands;
		gen = atomic_load_acq_int(&th->th_generation);
		fbi->th_scale = th->th_scale;
		fbi->tick_time = th->th_offset;
#ifdef FFCLOCK
		ffth = fftimehands;
		ffi->tick_time = ffth->tick_time_lerp;
		ffi->tick_time_lerp = ffth->tick_time_lerp;
		ffi->period = ffth->cest.period;
		ffi->period_lerp = ffth->period_lerp;
		clock_snap->ffcount = ffth->tick_ffcount;
		cest = ffth->cest;
#endif
		if (!fast)
			delta = tc_delta(th);
		atomic_thread_fence_acq();
	} while (gen == 0 || gen != th->th_generation);

	clock_snap->delta = delta;
	clock_snap->sysclock_active = sysclock_active;

	/* Record feedback clock status and error. */
	clock_snap->fb_info.status = time_status;
	/* XXX: Very crude estimate of feedback clock error. */
	bt.sec = time_esterror / 1000000;
	bt.frac = ((time_esterror - bt.sec) * 1000000) *
	    (uint64_t)18446744073709ULL;
	clock_snap->fb_info.error = bt;

#ifdef FFCLOCK
	if (!fast)
		clock_snap->ffcount += delta;

	/* Record feed-forward clock leap second adjustment. */
	ffi->leapsec_adjustment = cest.leapsec_total;
	if (clock_snap->ffcount > cest.leapsec_next)
		ffi->leapsec_adjustment -= cest.leapsec;

	/* Record feed-forward clock status and error. */
	clock_snap->ff_info.status = cest.status;
	ffcount = clock_snap->ffcount - cest.update_ffcount;
	ffclock_convert_delta(ffcount, cest.period, &bt);
	/* 18446744073709 = int(2^64/1e12), err_bound_rate in [ps/s]. */
	bintime_mul(&bt, cest.errb_rate * (uint64_t)18446744073709ULL);
	/* 18446744073 = int(2^64 / 1e9), since err_abs in [ns]. */
	bintime_addx(&bt, cest.errb_abs * (uint64_t)18446744073ULL);
	clock_snap->ff_info.error = bt;
#endif
}

/*
 * Convert a sysclock snapshot into a struct bintime based on the specified
 * clock source and flags.
 */
int
sysclock_snap2bintime(struct sysclock_snap *cs, struct bintime *bt,
    int whichclock, uint32_t flags)
{
	struct bintime boottimebin;
#ifdef FFCLOCK
	struct bintime bt2;
	uint64_t period;
#endif

	switch (whichclock) {
	case SYSCLOCK_FBCK:
		*bt = cs->fb_info.tick_time;

		/* If snapshot was created with !fast, delta will be >0. */
		if (cs->delta > 0)
			bintime_addx(bt, cs->fb_info.th_scale * cs->delta);

		if ((flags & FBCLOCK_UPTIME) == 0) {
			getboottimebin(&boottimebin);
			bintime_add(bt, &boottimebin);
		}
		break;
#ifdef FFCLOCK
	case SYSCLOCK_FFWD:
		if (flags & FFCLOCK_LERP) {
			*bt = cs->ff_info.tick_time_lerp;
			period = cs->ff_info.period_lerp;
		} else {
			*bt = cs->ff_info.tick_time;
			period = cs->ff_info.period;
		}

		/* If snapshot was created with !fast, delta will be >0. */
		if (cs->delta > 0) {
			ffclock_convert_delta(cs->delta, period, &bt2);
			bintime_add(bt, &bt2);
		}

		/* Leap second adjustment. */
		if (flags & FFCLOCK_LEAPSEC)
			bt->sec -= cs->ff_info.leapsec_adjustment;

		/* Boot time adjustment, for uptime/monotonic clocks. */
		if (flags & FFCLOCK_UPTIME)
			bintime_sub(bt, &ffclock_boottime);
		break;
#endif
	default:
		return (EINVAL);
		break;
	}

	return (0);
}

/*
 * Initialize a new timecounter and possibly use it.
 */
void
tc_init(struct timecounter *tc)
{
	u_int u;
	struct sysctl_oid *tc_root;

	u = tc->tc_frequency / tc->tc_counter_mask;
	/* XXX: We need some margin here, 10% is a guess */
	u *= 11;
	u /= 10;
	if (u > hz && tc->tc_quality >= 0) {
		tc->tc_quality = -2000;
		if (bootverbose) {
			printf("Timecounter \"%s\" frequency %ju Hz",
			    tc->tc_name, (uintmax_t)tc->tc_frequency);
			printf(" -- Insufficient hz, needs at least %u\n", u);
		}
	} else if (tc->tc_quality >= 0 || bootverbose) {
		printf("Timecounter \"%s\" frequency %ju Hz quality %d\n",
		    tc->tc_name, (uintmax_t)tc->tc_frequency,
		    tc->tc_quality);
	}

	tc->tc_next = timecounters;
	timecounters = tc;
	/*
	 * Set up sysctl tree for this counter.
	 */
	tc_root = SYSCTL_ADD_NODE_WITH_LABEL(NULL,
	    SYSCTL_STATIC_CHILDREN(_kern_timecounter_tc), OID_AUTO, tc->tc_name,
	    CTLFLAG_RW, 0, "timecounter description", "timecounter");
	SYSCTL_ADD_UINT(NULL, SYSCTL_CHILDREN(tc_root), OID_AUTO,
	    "mask", CTLFLAG_RD, &(tc->tc_counter_mask), 0,
	    "mask for implemented bits");
	SYSCTL_ADD_PROC(NULL, SYSCTL_CHILDREN(tc_root), OID_AUTO,
	    "counter", CTLTYPE_UINT | CTLFLAG_RD, tc, sizeof(*tc),
	    sysctl_kern_timecounter_get, "IU", "current timecounter value");
	SYSCTL_ADD_PROC(NULL, SYSCTL_CHILDREN(tc_root), OID_AUTO,
	    "frequency", CTLTYPE_U64 | CTLFLAG_RD, tc, sizeof(*tc),
	     sysctl_kern_timecounter_freq, "QU", "timecounter frequency");
	SYSCTL_ADD_INT(NULL, SYSCTL_CHILDREN(tc_root), OID_AUTO,
	    "quality", CTLFLAG_RD, &(tc->tc_quality), 0,
	    "goodness of time counter");
	/*
	 * Do not automatically switch if the current tc was specifically
	 * chosen.  Never automatically use a timecounter with negative quality.
	 * Even though we run on the dummy counter, switching here may be
	 * worse since this timecounter may not be monotonic.
	 */
	if (tc_chosen)
		return;
	if (tc->tc_quality < 0)
		return;
	if (tc->tc_quality < timecounter->tc_quality)
		return;
	if (tc->tc_quality == timecounter->tc_quality &&
	    tc->tc_frequency < timecounter->tc_frequency)
		return;
	(void)tc->tc_get_timecount(tc);
	(void)tc->tc_get_timecount(tc);
	timecounter = tc;
}

/* Report the frequency of the current timecounter. */
uint64_t
tc_getfrequency(void)
{

	return (timehands->th_counter->tc_frequency);
}

static bool
sleeping_on_old_rtc(struct thread *td)
{

	/*
	 * td_rtcgen is modified by curthread when it is running,
	 * and by other threads in this function.  By finding the thread
	 * on a sleepqueue and holding the lock on the sleepqueue
	 * chain, we guarantee that the thread is not running and that
	 * modifying td_rtcgen is safe.  Setting td_rtcgen to zero informs
	 * the thread that it was woken due to a real-time clock adjustment.
	 * (The declaration of td_rtcgen refers to this comment.)
	 */
	if (td->td_rtcgen != 0 && td->td_rtcgen != rtc_generation) {
		td->td_rtcgen = 0;
		return (true);
	}
	return (false);
}

static struct mtx tc_setclock_mtx;
MTX_SYSINIT(tc_setclock_init, &tc_setclock_mtx, "tcsetc", MTX_SPIN);

/*
 * Step our concept of UTC.  This is done by modifying our estimate of
 * when we booted.
 */
void
tc_setclock(struct timespec *ts)
{
	struct timespec tbef, taft;
	struct bintime bt, bt2;

	timespec2bintime(ts, &bt);
	nanotime(&tbef);
	mtx_lock_spin(&tc_setclock_mtx);
	cpu_tick_calibrate(1);
	binuptime(&bt2);
	bintime_sub(&bt, &bt2);

	/* XXX fiddle all the little crinkly bits around the fiords... */
	tc_windup(&bt);
	mtx_unlock_spin(&tc_setclock_mtx);

	/* Avoid rtc_generation == 0, since td_rtcgen == 0 is special. */
	atomic_add_rel_int(&rtc_generation, 2);
	sleepq_chains_remove_matching(sleeping_on_old_rtc);
	if (timestepwarnings) {
		nanotime(&taft);
		log(LOG_INFO,
		    "Time stepped from %jd.%09ld to %jd.%09ld (%jd.%09ld)\n",
		    (intmax_t)tbef.tv_sec, tbef.tv_nsec,
		    (intmax_t)taft.tv_sec, taft.tv_nsec,
		    (intmax_t)ts->tv_sec, ts->tv_nsec);
	}
}

/*
 * Initialize the next struct timehands in the ring and make
 * it the active timehands.  Along the way we might switch to a different
 * timecounter and/or do seconds processing in NTP.  Slightly magic.
 */
static void
tc_windup(struct bintime *new_boottimebin)
{
	struct bintime bt;
	struct timehands *th, *tho;
	uint64_t scale;
	u_int delta, ncount, ogen;
	int i;
	time_t t;

	/*
	 * Make the next timehands a copy of the current one, but do
	 * not overwrite the generation or next pointer.  While we
	 * update the contents, the generation must be zero.  We need
	 * to ensure that the zero generation is visible before the
	 * data updates become visible, which requires release fence.
	 * For similar reasons, re-reading of the generation after the
	 * data is read should use acquire fence.
	 */
	tho = timehands;
	th = tho->th_next;
	ogen = th->th_generation;
	th->th_generation = 0;
	atomic_thread_fence_rel();
	memcpy(th, tho, offsetof(struct timehands, th_generation));
	if (new_boottimebin != NULL)
		th->th_boottime = *new_boottimebin;

	/*
	 * Capture a timecounter delta on the current timecounter and if
	 * changing timecounters, a counter value from the new timecounter.
	 * Update the offset fields accordingly.
	 */
	delta = tc_delta(th);
	if (th->th_counter != timecounter)
		ncount = timecounter->tc_get_timecount(timecounter);
	else
		ncount = 0;
#ifdef FFCLOCK
	ffclock_windup(delta);
#endif
	th->th_offset_count += delta;
	th->th_offset_count &= th->th_counter->tc_counter_mask;
	while (delta > th->th_counter->tc_frequency) {
		/* Eat complete unadjusted seconds. */
		delta -= th->th_counter->tc_frequency;
		th->th_offset.sec++;
	}
	if ((delta > th->th_counter->tc_frequency / 2) &&
	    (th->th_scale * delta < ((uint64_t)1 << 63))) {
		/* The product th_scale * delta just barely overflows. */
		th->th_offset.sec++;
	}
	bintime_addx(&th->th_offset, th->th_scale * delta);

	/*
	 * Hardware latching timecounters may not generate interrupts on
	 * PPS events, so instead we poll them.  There is a finite risk that
	 * the hardware might capture a count which is later than the one we
	 * got above, and therefore possibly in the next NTP second which might
	 * have a different rate than the current NTP second.  It doesn't
	 * matter in practice.
	 */
	if (tho->th_counter->tc_poll_pps)
		tho->th_counter->tc_poll_pps(tho->th_counter);

	/*
	 * Deal with NTP second processing.  The for loop normally
	 * iterates at most once, but in extreme situations it might
	 * keep NTP sane if timeouts are not run for several seconds.
	 * At boot, the time step can be large when the TOD hardware
	 * has been read, so on really large steps, we call
	 * ntp_update_second only twice.  We need to call it twice in
	 * case we missed a leap second.
	 */
	bt = th->th_offset;
	bintime_add(&bt, &th->th_boottime);
	i = bt.sec - tho->th_microtime.tv_sec;
	if (i > LARGE_STEP)
		i = 2;
	for (; i > 0; i--) {
		t = bt.sec;
		ntp_update_second(&th->th_adjustment, &bt.sec);
		if (bt.sec != t)
			th->th_boottime.sec += bt.sec - t;
	}
	/* Update the UTC timestamps used by the get*() functions. */
	th->th_bintime = bt;
	bintime2timeval(&bt, &th->th_microtime);
	bintime2timespec(&bt, &th->th_nanotime);

	/* Now is a good time to change timecounters. */
	if (th->th_counter != timecounter) {
#ifndef __arm__
		if ((timecounter->tc_flags & TC_FLAGS_C2STOP) != 0)
			cpu_disable_c2_sleep++;
		if ((th->th_counter->tc_flags & TC_FLAGS_C2STOP) != 0)
			cpu_disable_c2_sleep--;
#endif
		th->th_counter = timecounter;
		th->th_offset_count = ncount;
		tc_min_ticktock_freq = max(1, timecounter->tc_frequency /
		    (((uint64_t)timecounter->tc_counter_mask + 1) / 3));
#ifdef FFCLOCK
		ffclock_change_tc(th);
#endif
	}

	/*-
	 * Recalculate the scaling factor.  We want the number of 1/2^64
	 * fractions of a second per period of the hardware counter, taking
	 * into account the th_adjustment factor which the NTP PLL/adjtime(2)
	 * processing provides us with.
	 *
	 * The th_adjustment is nanoseconds per second with 32 bit binary
	 * fraction and we want 64 bit binary fraction of second:
	 *
	 *	 x = a * 2^32 / 10^9 = a * 4.294967296
	 *
	 * The range of th_adjustment is +/- 5000PPM so inside a 64bit int
	 * we can only multiply by about 850 without overflowing, that
	 * leaves no suitably precise fractions for multiply before divide.
	 *
	 * Divide before multiply with a fraction of 2199/512 results in a
	 * systematic undercompensation of 10PPM of th_adjustment.  On a
	 * 5000PPM adjustment this is a 0.05PPM error.  This is acceptable.
 	 *
	 * We happily sacrifice the lowest of the 64 bits of our result
	 * to the goddess of code clarity.
	 *
	 */
	scale = (uint64_t)1 << 63;
	scale += (th->th_adjustment / 1024) * 2199;
	scale /= th->th_counter->tc_frequency;
	th->th_scale = scale * 2;

	/*
	 * Now that the struct timehands is again consistent, set the new
	 * generation number, making sure to not make it zero.
	 */
	if (++ogen == 0)
		ogen = 1;
	atomic_store_rel_int(&th->th_generation, ogen);

	/* Go live with the new struct timehands. */
#ifdef FFCLOCK
	switch (sysclock_active) {
	case SYSCLOCK_FBCK:
#endif
		time_second = th->th_microtime.tv_sec;
		time_uptime = th->th_offset.sec;
#ifdef FFCLOCK
		break;
	case SYSCLOCK_FFWD:
		time_second = fftimehands->tick_time_lerp.sec;
		time_uptime = fftimehands->tick_time_lerp.sec - ffclock_boottime.sec;
		break;
	}
#endif

	timehands = th;
	timekeep_push_vdso();
}

/* Report or change the active timecounter hardware. */
static int
sysctl_kern_timecounter_hardware(SYSCTL_HANDLER_ARGS)
{
	char newname[32];
	struct timecounter *newtc, *tc;
	int error;

	tc = timecounter;
	strlcpy(newname, tc->tc_name, sizeof(newname));

	error = sysctl_handle_string(oidp, &newname[0], sizeof(newname), req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	/* Record that the tc in use now was specifically chosen. */
	tc_chosen = 1;
	if (strcmp(newname, tc->tc_name) == 0)
		return (0);
	for (newtc = timecounters; newtc != NULL; newtc = newtc->tc_next) {
		if (strcmp(newname, newtc->tc_name) != 0)
			continue;

		/* Warm up new timecounter. */
		(void)newtc->tc_get_timecount(newtc);
		(void)newtc->tc_get_timecount(newtc);

		timecounter = newtc;

		/*
		 * The vdso timehands update is deferred until the next
		 * 'tc_windup()'.
		 *
		 * This is prudent given that 'timekeep_push_vdso()' does not
		 * use any locking and that it can be called in hard interrupt
		 * context via 'tc_windup()'.
		 */
		return (0);
	}
	return (EINVAL);
}

SYSCTL_PROC(_kern_timecounter, OID_AUTO, hardware, CTLTYPE_STRING | CTLFLAG_RW,
    0, 0, sysctl_kern_timecounter_hardware, "A",
    "Timecounter hardware selected");


/* Report the available timecounter hardware. */
static int
sysctl_kern_timecounter_choice(SYSCTL_HANDLER_ARGS)
{
	struct sbuf sb;
	struct timecounter *tc;
	int error;

	sbuf_new_for_sysctl(&sb, NULL, 0, req);
	for (tc = timecounters; tc != NULL; tc = tc->tc_next) {
		if (tc != timecounters)
			sbuf_putc(&sb, ' ');
		sbuf_printf(&sb, "%s(%d)", tc->tc_name, tc->tc_quality);
	}
	error = sbuf_finish(&sb);
	sbuf_delete(&sb);
	return (error);
}

SYSCTL_PROC(_kern_timecounter, OID_AUTO, choice, CTLTYPE_STRING | CTLFLAG_RD,
    0, 0, sysctl_kern_timecounter_choice, "A", "Timecounter hardware detected");

/*
 * RFC 2783 PPS-API implementation.
 */

/*
 *  Return true if the driver is aware of the abi version extensions in the
 *  pps_state structure, and it supports at least the given abi version number.
 */
static inline int
abi_aware(struct pps_state *pps, int vers)
{

	return ((pps->kcmode & KCMODE_ABIFLAG) && pps->driver_abi >= vers);
}

static int
pps_fetch(struct pps_fetch_args *fapi, struct pps_state *pps)
{
	int err, timo;
	pps_seq_t aseq, cseq;
	struct timeval tv;

	if (fapi->tsformat && fapi->tsformat != PPS_TSFMT_TSPEC)
		return (EINVAL);

	/*
	 * If no timeout is requested, immediately return whatever values were
	 * most recently captured.  If timeout seconds is -1, that's a request
	 * to block without a timeout.  WITNESS won't let us sleep forever
	 * without a lock (we really don't need a lock), so just repeatedly
	 * sleep a long time.
	 */
	if (fapi->timeout.tv_sec || fapi->timeout.tv_nsec) {
		if (fapi->timeout.tv_sec == -1)
			timo = 0x7fffffff;
		else {
			tv.tv_sec = fapi->timeout.tv_sec;
			tv.tv_usec = fapi->timeout.tv_nsec / 1000;
			timo = tvtohz(&tv);
		}
		aseq = atomic_load_int(&pps->ppsinfo.assert_sequence);
		cseq = atomic_load_int(&pps->ppsinfo.clear_sequence);
		while (aseq == atomic_load_int(&pps->ppsinfo.assert_sequence) &&
		    cseq == atomic_load_int(&pps->ppsinfo.clear_sequence)) {
			if (abi_aware(pps, 1) && pps->driver_mtx != NULL) {
				if (pps->flags & PPSFLAG_MTX_SPIN) {
					err = msleep_spin(pps, pps->driver_mtx,
					    "ppsfch", timo);
				} else {
					err = msleep(pps, pps->driver_mtx, PCATCH,
					    "ppsfch", timo);
				}
			} else {
				err = tsleep(pps, PCATCH, "ppsfch", timo);
			}
			if (err == EWOULDBLOCK) {
				if (fapi->timeout.tv_sec == -1) {
					continue;
				} else {
					return (ETIMEDOUT);
				}
			} else if (err != 0) {
				return (err);
			}
		}
	}

	pps->ppsinfo.current_mode = pps->ppsparam.mode;
	fapi->pps_info_buf = pps->ppsinfo;

	return (0);
}

int
pps_ioctl(u_long cmd, caddr_t data, struct pps_state *pps)
{
	pps_params_t *app;
	struct pps_fetch_args *fapi;
#ifdef FFCLOCK
	struct pps_fetch_ffc_args *fapi_ffc;
#endif
#ifdef PPS_SYNC
	struct pps_kcbind_args *kapi;
#endif

	KASSERT(pps != NULL, ("NULL pps pointer in pps_ioctl"));
	switch (cmd) {
	case PPS_IOC_CREATE:
		return (0);
	case PPS_IOC_DESTROY:
		return (0);
	case PPS_IOC_SETPARAMS:
		app = (pps_params_t *)data;
		if (app->mode & ~pps->ppscap)
			return (EINVAL);
#ifdef FFCLOCK
		/* Ensure only a single clock is selected for ffc timestamp. */
		if ((app->mode & PPS_TSCLK_MASK) == PPS_TSCLK_MASK)
			return (EINVAL);
#endif
		pps->ppsparam = *app;
		return (0);
	case PPS_IOC_GETPARAMS:
		app = (pps_params_t *)data;
		*app = pps->ppsparam;
		app->api_version = PPS_API_VERS_1;
		return (0);
	case PPS_IOC_GETCAP:
		*(int*)data = pps->ppscap;
		return (0);
	case PPS_IOC_FETCH:
		fapi = (struct pps_fetch_args *)data;
		return (pps_fetch(fapi, pps));
#ifdef FFCLOCK
	case PPS_IOC_FETCH_FFCOUNTER:
		fapi_ffc = (struct pps_fetch_ffc_args *)data;
		if (fapi_ffc->tsformat && fapi_ffc->tsformat !=
		    PPS_TSFMT_TSPEC)
			return (EINVAL);
		if (fapi_ffc->timeout.tv_sec || fapi_ffc->timeout.tv_nsec)
			return (EOPNOTSUPP);
		pps->ppsinfo_ffc.current_mode = pps->ppsparam.mode;
		fapi_ffc->pps_info_buf_ffc = pps->ppsinfo_ffc;
		/* Overwrite timestamps if feedback clock selected. */
		switch (pps->ppsparam.mode & PPS_TSCLK_MASK) {
		case PPS_TSCLK_FBCK:
			fapi_ffc->pps_info_buf_ffc.assert_timestamp =
			    pps->ppsinfo.assert_timestamp;
			fapi_ffc->pps_info_buf_ffc.clear_timestamp =
			    pps->ppsinfo.clear_timestamp;
			break;
		case PPS_TSCLK_FFWD:
			break;
		default:
			break;
		}
		return (0);
#endif /* FFCLOCK */
	case PPS_IOC_KCBIND:
#ifdef PPS_SYNC
		kapi = (struct pps_kcbind_args *)data;
		/* XXX Only root should be able to do this */
		if (kapi->tsformat && kapi->tsformat != PPS_TSFMT_TSPEC)
			return (EINVAL);
		if (kapi->kernel_consumer != PPS_KC_HARDPPS)
			return (EINVAL);
		if (kapi->edge & ~pps->ppscap)
			return (EINVAL);
		pps->kcmode = (kapi->edge & KCMODE_EDGEMASK) |
		    (pps->kcmode & KCMODE_ABIFLAG);
		return (0);
#else
		return (EOPNOTSUPP);
#endif
	default:
		return (ENOIOCTL);
	}
}

void
pps_init(struct pps_state *pps)
{
	pps->ppscap |= PPS_TSFMT_TSPEC | PPS_CANWAIT;
	if (pps->ppscap & PPS_CAPTUREASSERT)
		pps->ppscap |= PPS_OFFSETASSERT;
	if (pps->ppscap & PPS_CAPTURECLEAR)
		pps->ppscap |= PPS_OFFSETCLEAR;
#ifdef FFCLOCK
	pps->ppscap |= PPS_TSCLK_MASK;
#endif
	pps->kcmode &= ~KCMODE_ABIFLAG;
}

void
pps_init_abi(struct pps_state *pps)
{

	pps_init(pps);
	if (pps->driver_abi > 0) {
		pps->kcmode |= KCMODE_ABIFLAG;
		pps->kernel_abi = PPS_ABI_VERSION;
	}
}

void
pps_capture(struct pps_state *pps)
{
	struct timehands *th;

	KASSERT(pps != NULL, ("NULL pps pointer in pps_capture"));
	th = timehands;
	pps->capgen = atomic_load_acq_int(&th->th_generation);
	pps->capth = th;
#ifdef FFCLOCK
	pps->capffth = fftimehands;
#endif
	pps->capcount = th->th_counter->tc_get_timecount(th->th_counter);
	atomic_thread_fence_acq();
	if (pps->capgen != th->th_generation)
		pps->capgen = 0;
}

void
pps_event(struct pps_state *pps, int event)
{
	struct bintime bt;
	struct timespec ts, *tsp, *osp;
	u_int tcount, *pcount;
	int foff;
	pps_seq_t *pseq;
#ifdef FFCLOCK
	struct timespec *tsp_ffc;
	pps_seq_t *pseq_ffc;
	ffcounter *ffcount;
#endif
#ifdef PPS_SYNC
	int fhard;
#endif

	KASSERT(pps != NULL, ("NULL pps pointer in pps_event"));
	/* Nothing to do if not currently set to capture this event type. */
	if ((event & pps->ppsparam.mode) == 0)
		return;
	/* If the timecounter was wound up underneath us, bail out. */
	if (pps->capgen == 0 || pps->capgen !=
	    atomic_load_acq_int(&pps->capth->th_generation))
		return;

	/* Things would be easier with arrays. */
	if (event == PPS_CAPTUREASSERT) {
		tsp = &pps->ppsinfo.assert_timestamp;
		osp = &pps->ppsparam.assert_offset;
		foff = pps->ppsparam.mode & PPS_OFFSETASSERT;
#ifdef PPS_SYNC
		fhard = pps->kcmode & PPS_CAPTUREASSERT;
#endif
		pcount = &pps->ppscount[0];
		pseq = &pps->ppsinfo.assert_sequence;
#ifdef FFCLOCK
		ffcount = &pps->ppsinfo_ffc.assert_ffcount;
		tsp_ffc = &pps->ppsinfo_ffc.assert_timestamp;
		pseq_ffc = &pps->ppsinfo_ffc.assert_sequence;
#endif
	} else {
		tsp = &pps->ppsinfo.clear_timestamp;
		osp = &pps->ppsparam.clear_offset;
		foff = pps->ppsparam.mode & PPS_OFFSETCLEAR;
#ifdef PPS_SYNC
		fhard = pps->kcmode & PPS_CAPTURECLEAR;
#endif
		pcount = &pps->ppscount[1];
		pseq = &pps->ppsinfo.clear_sequence;
#ifdef FFCLOCK
		ffcount = &pps->ppsinfo_ffc.clear_ffcount;
		tsp_ffc = &pps->ppsinfo_ffc.clear_timestamp;
		pseq_ffc = &pps->ppsinfo_ffc.clear_sequence;
#endif
	}

	/*
	 * If the timecounter changed, we cannot compare the count values, so
	 * we have to drop the rest of the PPS-stuff until the next event.
	 */
	if (pps->ppstc != pps->capth->th_counter) {
		pps->ppstc = pps->capth->th_counter;
		*pcount = pps->capcount;
		pps->ppscount[2] = pps->capcount;
		return;
	}

	/* Convert the count to a timespec. */
	tcount = pps->capcount - pps->capth->th_offset_count;
	tcount &= pps->capth->th_counter->tc_counter_mask;
	bt = pps->capth->th_bintime;
	bintime_addx(&bt, pps->capth->th_scale * tcount);
	bintime2timespec(&bt, &ts);

	/* If the timecounter was wound up underneath us, bail out. */
	atomic_thread_fence_acq();
	if (pps->capgen != pps->capth->th_generation)
		return;

	*pcount = pps->capcount;
	(*pseq)++;
	*tsp = ts;

	if (foff) {
		timespecadd(tsp, osp, tsp);
		if (tsp->tv_nsec < 0) {
			tsp->tv_nsec += 1000000000;
			tsp->tv_sec -= 1;
		}
	}

#ifdef FFCLOCK
	*ffcount = pps->capffth->tick_ffcount + tcount;
	bt = pps->capffth->tick_time;
	ffclock_convert_delta(tcount, pps->capffth->cest.period, &bt);
	bintime_add(&bt, &pps->capffth->tick_time);
	bintime2timespec(&bt, &ts);
	(*pseq_ffc)++;
	*tsp_ffc = ts;
#endif

#ifdef PPS_SYNC
	if (fhard) {
		uint64_t scale;

		/*
		 * Feed the NTP PLL/FLL.
		 * The FLL wants to know how many (hardware) nanoseconds
		 * elapsed since the previous event.
		 */
		tcount = pps->capcount - pps->ppscount[2];
		pps->ppscount[2] = pps->capcount;
		tcount &= pps->capth->th_counter->tc_counter_mask;
		scale = (uint64_t)1 << 63;
		scale /= pps->capth->th_counter->tc_frequency;
		scale *= 2;
		bt.sec = 0;
		bt.frac = 0;
		bintime_addx(&bt, scale * tcount);
		bintime2timespec(&bt, &ts);
		hardpps(tsp, ts.tv_nsec + 1000000000 * ts.tv_sec);
	}
#endif

	/* Wakeup anyone sleeping in pps_fetch().  */
	wakeup(pps);
}

/*
 * Timecounters need to be updated every so often to prevent the hardware
 * counter from overflowing.  Updating also recalculates the cached values
 * used by the get*() family of functions, so their precision depends on
 * the update frequency.
 */

static int tc_tick;
SYSCTL_INT(_kern_timecounter, OID_AUTO, tick, CTLFLAG_RD, &tc_tick, 0,
    "Approximate number of hardclock ticks in a millisecond");

void
tc_ticktock(int cnt)
{
	static int count;

	if (mtx_trylock_spin(&tc_setclock_mtx)) {
		count += cnt;
		if (count >= tc_tick) {
			count = 0;
			tc_windup(NULL);
		}
		mtx_unlock_spin(&tc_setclock_mtx);
	}
}

static void __inline
tc_adjprecision(void)
{
	int t;

	if (tc_timepercentage > 0) {
		t = (99 + tc_timepercentage) / tc_timepercentage;
		tc_precexp = fls(t + (t >> 1)) - 1;
		FREQ2BT(hz / tc_tick, &bt_timethreshold);
		FREQ2BT(hz, &bt_tickthreshold);
		bintime_shift(&bt_timethreshold, tc_precexp);
		bintime_shift(&bt_tickthreshold, tc_precexp);
	} else {
		tc_precexp = 31;
		bt_timethreshold.sec = INT_MAX;
		bt_timethreshold.frac = ~(uint64_t)0;
		bt_tickthreshold = bt_timethreshold;
	}
	sbt_timethreshold = bttosbt(bt_timethreshold);
	sbt_tickthreshold = bttosbt(bt_tickthreshold);
}

static int
sysctl_kern_timecounter_adjprecision(SYSCTL_HANDLER_ARGS)
{
	int error, val;

	val = tc_timepercentage;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	tc_timepercentage = val;
	if (cold)
		goto done;
	tc_adjprecision();
done:
	return (0);
}

static void
inittimecounter(void *dummy)
{
	u_int p;
	int tick_rate;

	/*
	 * Set the initial timeout to
	 * max(1, <approx. number of hardclock ticks in a millisecond>).
	 * People should probably not use the sysctl to set the timeout
	 * to smaller than its initial value, since that value is the
	 * smallest reasonable one.  If they want better timestamps they
	 * should use the non-"get"* functions.
	 */
	if (hz > 1000)
		tc_tick = (hz + 500) / 1000;
	else
		tc_tick = 1;
	tc_adjprecision();
	FREQ2BT(hz, &tick_bt);
	tick_sbt = bttosbt(tick_bt);
	tick_rate = hz / tc_tick;
	FREQ2BT(tick_rate, &tc_tick_bt);
	tc_tick_sbt = bttosbt(tc_tick_bt);
	p = (tc_tick * 1000000) / hz;
	printf("Timecounters tick every %d.%03u msec\n", p / 1000, p % 1000);

#ifdef FFCLOCK
	ffclock_init();
#endif
	/* warm up new timecounter (again) and get rolling. */
	(void)timecounter->tc_get_timecount(timecounter);
	(void)timecounter->tc_get_timecount(timecounter);
	mtx_lock_spin(&tc_setclock_mtx);
	tc_windup(NULL);
	mtx_unlock_spin(&tc_setclock_mtx);
}

SYSINIT(timecounter, SI_SUB_CLOCKS, SI_ORDER_SECOND, inittimecounter, NULL);

/* Cpu tick handling -------------------------------------------------*/

static int cpu_tick_variable;
static uint64_t	cpu_tick_frequency;

DPCPU_DEFINE_STATIC(uint64_t, tc_cpu_ticks_base);
DPCPU_DEFINE_STATIC(unsigned, tc_cpu_ticks_last);

static uint64_t
tc_cpu_ticks(void)
{
	struct timecounter *tc;
	uint64_t res, *base;
	unsigned u, *last;

	critical_enter();
	base = DPCPU_PTR(tc_cpu_ticks_base);
	last = DPCPU_PTR(tc_cpu_ticks_last);
	tc = timehands->th_counter;
	u = tc->tc_get_timecount(tc) & tc->tc_counter_mask;
	if (u < *last)
		*base += (uint64_t)tc->tc_counter_mask + 1;
	*last = u;
	res = u + *base;
	critical_exit();
	return (res);
}

void
cpu_tick_calibration(void)
{
	static time_t last_calib;

	if (time_uptime != last_calib && !(time_uptime & 0xf)) {
		cpu_tick_calibrate(0);
		last_calib = time_uptime;
	}
}

/*
 * This function gets called every 16 seconds on only one designated
 * CPU in the system from hardclock() via cpu_tick_calibration()().
 *
 * Whenever the real time clock is stepped we get called with reset=1
 * to make sure we handle suspend/resume and similar events correctly.
 */

static void
cpu_tick_calibrate(int reset)
{
	static uint64_t c_last;
	uint64_t c_this, c_delta;
	static struct bintime  t_last;
	struct bintime t_this, t_delta;
	uint32_t divi;

	if (reset) {
		/* The clock was stepped, abort & reset */
		t_last.sec = 0;
		return;
	}

	/* we don't calibrate fixed rate cputicks */
	if (!cpu_tick_variable)
		return;

	getbinuptime(&t_this);
	c_this = cpu_ticks();
	if (t_last.sec != 0) {
		c_delta = c_this - c_last;
		t_delta = t_this;
		bintime_sub(&t_delta, &t_last);
		/*
		 * Headroom:
		 * 	2^(64-20) / 16[s] =
		 * 	2^(44) / 16[s] =
		 * 	17.592.186.044.416 / 16 =
		 * 	1.099.511.627.776 [Hz]
		 */
		divi = t_delta.sec << 20;
		divi |= t_delta.frac >> (64 - 20);
		c_delta <<= 20;
		c_delta /= divi;
		if (c_delta > cpu_tick_frequency) {
			if (0 && bootverbose)
				printf("cpu_tick increased to %ju Hz\n",
				    c_delta);
			cpu_tick_frequency = c_delta;
		}
	}
	c_last = c_this;
	t_last = t_this;
}

void
set_cputicker(cpu_tick_f *func, uint64_t freq, unsigned var)
{

	if (func == NULL) {
		cpu_ticks = tc_cpu_ticks;
	} else {
		cpu_tick_frequency = freq;
		cpu_tick_variable = var;
		cpu_ticks = func;
	}
}

uint64_t
cpu_tickrate(void)
{

	if (cpu_ticks == tc_cpu_ticks) 
		return (tc_getfrequency());
	return (cpu_tick_frequency);
}

/*
 * We need to be slightly careful converting cputicks to microseconds.
 * There is plenty of margin in 64 bits of microseconds (half a million
 * years) and in 64 bits at 4 GHz (146 years), but if we do a multiply
 * before divide conversion (to retain precision) we find that the
 * margin shrinks to 1.5 hours (one millionth of 146y).
 * With a three prong approach we never lose significant bits, no
 * matter what the cputick rate and length of timeinterval is.
 */

uint64_t
cputick2usec(uint64_t tick)
{

	if (tick > 18446744073709551LL)		/* floor(2^64 / 1000) */
		return (tick / (cpu_tickrate() / 1000000LL));
	else if (tick > 18446744073709LL)	/* floor(2^64 / 1000000) */
		return ((tick * 1000LL) / (cpu_tickrate() / 1000LL));
	else
		return ((tick * 1000000LL) / cpu_tickrate());
}

cpu_tick_f	*cpu_ticks = tc_cpu_ticks;

static int vdso_th_enable = 1;
static int
sysctl_fast_gettime(SYSCTL_HANDLER_ARGS)
{
	int old_vdso_th_enable, error;

	old_vdso_th_enable = vdso_th_enable;
	error = sysctl_handle_int(oidp, &old_vdso_th_enable, 0, req);
	if (error != 0)
		return (error);
	vdso_th_enable = old_vdso_th_enable;
	return (0);
}
SYSCTL_PROC(_kern_timecounter, OID_AUTO, fast_gettime,
    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE,
    NULL, 0, sysctl_fast_gettime, "I", "Enable fast time of day");

uint32_t
tc_fill_vdso_timehands(struct vdso_timehands *vdso_th)
{
	struct timehands *th;
	uint32_t enabled;

	th = timehands;
	vdso_th->th_scale = th->th_scale;
	vdso_th->th_offset_count = th->th_offset_count;
	vdso_th->th_counter_mask = th->th_counter->tc_counter_mask;
	vdso_th->th_offset = th->th_offset;
	vdso_th->th_boottime = th->th_boottime;
	if (th->th_counter->tc_fill_vdso_timehands != NULL) {
		enabled = th->th_counter->tc_fill_vdso_timehands(vdso_th,
		    th->th_counter);
	} else
		enabled = 0;
	if (!vdso_th_enable)
		enabled = 0;
	return (enabled);
}

#ifdef COMPAT_FREEBSD32
uint32_t
tc_fill_vdso_timehands32(struct vdso_timehands32 *vdso_th32)
{
	struct timehands *th;
	uint32_t enabled;

	th = timehands;
	*(uint64_t *)&vdso_th32->th_scale[0] = th->th_scale;
	vdso_th32->th_offset_count = th->th_offset_count;
	vdso_th32->th_counter_mask = th->th_counter->tc_counter_mask;
	vdso_th32->th_offset.sec = th->th_offset.sec;
	*(uint64_t *)&vdso_th32->th_offset.frac[0] = th->th_offset.frac;
	vdso_th32->th_boottime.sec = th->th_boottime.sec;
	*(uint64_t *)&vdso_th32->th_boottime.frac[0] = th->th_boottime.frac;
	if (th->th_counter->tc_fill_vdso_timehands32 != NULL) {
		enabled = th->th_counter->tc_fill_vdso_timehands32(vdso_th32,
		    th->th_counter);
	} else
		enabled = 0;
	if (!vdso_th_enable)
		enabled = 0;
	return (enabled);
}
#endif
