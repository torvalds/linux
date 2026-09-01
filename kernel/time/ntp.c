// SPDX-License-Identifier: GPL-2.0
/*
 * NTP state machine interfaces and logic.
 *
 * This code was mainly moved from kernel/timer.c and kernel/time.c
 * Please see those files for relevant copyright info and historical
 * changelogs.
 */
#include <linux/capability.h>
#include <linux/clocksource.h>
#include <linux/workqueue.h>
#include <linux/hrtimer.h>
#include <linux/jiffies.h>
#include <linux/math64.h>
#include <linux/timex.h>
#include <linux/time.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/audit.h>
#include <linux/timekeeper_internal.h>

#include "ntp_internal.h"
#include "timekeeping_internal.h"

/**
 * struct ntp_data - Structure holding all NTP related state
 * @tick_usec:		USER_HZ period in microseconds
 * @tick_length:	Tick length in ns << NTP_SCALE_SHIFT
 * @time_state:		State of the clock synchronization
 * @time_status:	Clock status bits
 * @time_offset:	Time adjustment in nanoseconds
 * @skew_delta:		Per-tick phase slew rate for the coming second, in
 *			@time_offset units (shifted-ns / HZ). Set by
 *			second_overflow().
 * @time_constant:	PLL time constant
 * @time_maxerror:	Maximum error in microseconds holding the NTP sync distance
 *			(NTP dispersion + delay / 2)
 * @time_esterror:	Estimated error in microseconds holding NTP dispersion
 * @time_freq:		Frequency offset scaled nsecs/secs
 * @time_reftime:	Time at last adjustment in seconds
 * @time_adjust:	Adjustment value
 * @time_adjust_frac:	Sub-microsecond remainder of @time_adjust being
 *			delivered, in ns << NTP_SCALE_SHIFT (not divided by HZ).
 * @ntp_tick_adj:	Constant boot-param configurable NTP tick adjustment (upscaled)
 * @cs_tick_adj:	Fixed per-second adjustment compensating for the difference
 *			between the nominal NTP interval and the real time taken
 *			by the clocksource's integer @cycle_interval (upscaled).
 *			Set by the timekeeping core via ntp_clear().
 * @ntp_next_leap_sec:	Second value of the next pending leapsecond, or TIME64_MAX if no leap
 *
 * @pps_valid:		PPS signal watchdog counter
 * @pps_tf:		PPS phase median filter
 * @pps_jitter:		PPS current jitter in nanoseconds
 * @pps_fbase:		PPS beginning of the last freq interval
 * @pps_shift:		PPS current interval duration in seconds (shift value)
 * @pps_intcnt:		PPS interval counter
 * @pps_freq:		PPS frequency offset in scaled ns/s
 * @pps_stabil:		PPS current stability in scaled ns/s
 * @pps_calcnt:		PPS monitor: calibration intervals
 * @pps_jitcnt:		PPS monitor: jitter limit exceeded
 * @pps_stbcnt:		PPS monitor: stability limit exceeded
 * @pps_errcnt:		PPS monitor: calibration errors
 *
 * Protected by the timekeeping locks.
 */
struct ntp_data {
	unsigned long		tick_usec;
	u64			tick_length;
	int			time_state;
	int			time_status;
	s64			time_offset;
	s64			skew_delta;
	long			time_constant;
	long			time_maxerror;
	long			time_esterror;
	s64			time_freq;
	time64_t		time_reftime;
	long			time_adjust;
	s64			time_adjust_frac;
	s64			ntp_tick_adj;
	s64			cs_tick_adj;
	time64_t		ntp_next_leap_sec;
#ifdef CONFIG_NTP_PPS
	int			pps_valid;
	long			pps_tf[3];
	long			pps_jitter;
	struct timespec64	pps_fbase;
	int			pps_shift;
	int			pps_intcnt;
	s64			pps_freq;
	long			pps_stabil;
	long			pps_calcnt;
	long			pps_jitcnt;
	long			pps_stbcnt;
	long			pps_errcnt;
#endif
};

static struct ntp_data tk_ntp_data[TIMEKEEPERS_MAX] = {
	[ 0 ... TIMEKEEPERS_MAX - 1 ] = {
		.tick_usec		= USER_TICK_USEC,
		.time_state		= TIME_OK,
		.time_status		= STA_UNSYNC,
		.time_constant		= 2,
		.time_maxerror		= NTP_PHASE_LIMIT,
		.time_esterror		= NTP_PHASE_LIMIT,
		.ntp_next_leap_sec	= TIME64_MAX,
	},
};

#define SECS_PER_DAY		86400
#define MAX_TICKADJ		500LL		/* usecs */
/* One microsecond of phase, in plain shifted-ns (ns << NTP_SCALE_SHIFT) */
#define ONE_US_NS		((s64)NSEC_PER_USEC << NTP_SCALE_SHIFT)
/* Per-tick MAX_TICKADJ slew, in plain shifted-ns */
#define MAX_TICKADJ_SCALED \
	(((MAX_TICKADJ * NSEC_PER_USEC) << NTP_SCALE_SHIFT) / NTP_INTERVAL_FREQ)
#define MAX_TAI_OFFSET		100000

#ifdef CONFIG_NTP_PPS

/*
 * The following variables are used when a pulse-per-second (PPS) signal
 * is available. They establish the engineering parameters of the clock
 * discipline loop when controlled by the PPS signal.
 */
#define PPS_VALID	10	/* PPS signal watchdog max (s) */
#define PPS_POPCORN	4	/* popcorn spike threshold (shift) */
#define PPS_INTMIN	2	/* min freq interval (s) (shift) */
#define PPS_INTMAX	8	/* max freq interval (s) (shift) */
#define PPS_INTCOUNT	4	/* number of consecutive good intervals to
				   increase pps_shift or consecutive bad
				   intervals to decrease it */
#define PPS_MAXWANDER	100000	/* max PPS freq wander (ns/s) */

/*
 * PPS kernel consumer compensates the whole phase error immediately.
 * Otherwise, reduce the offset by a fixed factor times the time constant.
 */
static inline s64 ntp_offset_chunk(struct ntp_data *ntpdata, s64 offset)
{
	if (ntpdata->time_status & STA_PPSTIME && ntpdata->time_status & STA_PPSSIGNAL)
		return offset;
	else
		return shift_right(offset, SHIFT_PLL + ntpdata->time_constant);
}

static inline void pps_reset_freq_interval(struct ntp_data *ntpdata)
{
	/* The PPS calibration interval may end surprisingly early */
	ntpdata->pps_shift = PPS_INTMIN;
	ntpdata->pps_intcnt = 0;
}

/**
 * pps_clear - Clears the PPS state variables
 * @ntpdata:	Pointer to ntp data
 */
static inline void pps_clear(struct ntp_data *ntpdata)
{
	pps_reset_freq_interval(ntpdata);
	ntpdata->pps_tf[0] = 0;
	ntpdata->pps_tf[1] = 0;
	ntpdata->pps_tf[2] = 0;
	ntpdata->pps_fbase.tv_sec = ntpdata->pps_fbase.tv_nsec = 0;
	ntpdata->pps_freq = 0;
}

/*
 * Decrease pps_valid to indicate that another second has passed since the
 * last PPS signal. When it reaches 0, indicate that PPS signal is missing.
 */
static inline void pps_dec_valid(struct ntp_data *ntpdata)
{
	if (ntpdata->pps_valid > 0) {
		ntpdata->pps_valid--;
	} else {
		ntpdata->time_status &= ~(STA_PPSSIGNAL | STA_PPSJITTER |
					  STA_PPSWANDER | STA_PPSERROR);
		pps_clear(ntpdata);
	}
}

static inline void pps_set_freq(struct ntp_data *ntpdata)
{
	ntpdata->pps_freq = ntpdata->time_freq;
}

static inline bool is_error_status(int status)
{
	return (status & (STA_UNSYNC|STA_CLOCKERR))
		/*
		 * PPS signal lost when either PPS time or PPS frequency
		 * synchronization requested
		 */
		|| ((status & (STA_PPSFREQ|STA_PPSTIME))
			&& !(status & STA_PPSSIGNAL))
		/*
		 * PPS jitter exceeded when PPS time synchronization
		 * requested
		 */
		|| ((status & (STA_PPSTIME|STA_PPSJITTER))
			== (STA_PPSTIME|STA_PPSJITTER))
		/*
		 * PPS wander exceeded or calibration error when PPS
		 * frequency synchronization requested
		 */
		|| ((status & STA_PPSFREQ)
			&& (status & (STA_PPSWANDER|STA_PPSERROR)));
}

static inline void pps_fill_timex(struct ntp_data *ntpdata, struct __kernel_timex *txc)
{
	txc->ppsfreq	   = shift_right((ntpdata->pps_freq >> PPM_SCALE_INV_SHIFT) *
					 PPM_SCALE_INV, NTP_SCALE_SHIFT);
	txc->jitter	   = ntpdata->pps_jitter;
	if (!(ntpdata->time_status & STA_NANO))
		txc->jitter = ntpdata->pps_jitter / NSEC_PER_USEC;
	txc->shift	   = ntpdata->pps_shift;
	txc->stabil	   = ntpdata->pps_stabil;
	txc->jitcnt	   = ntpdata->pps_jitcnt;
	txc->calcnt	   = ntpdata->pps_calcnt;
	txc->errcnt	   = ntpdata->pps_errcnt;
	txc->stbcnt	   = ntpdata->pps_stbcnt;
}

#else /* !CONFIG_NTP_PPS */

static inline s64 ntp_offset_chunk(struct ntp_data *ntpdata, s64 offset)
{
	return shift_right(offset, SHIFT_PLL + ntpdata->time_constant);
}

static inline void pps_reset_freq_interval(struct ntp_data *ntpdata) {}
static inline void pps_clear(struct ntp_data *ntpdata) {}
static inline void pps_dec_valid(struct ntp_data *ntpdata) {}
static inline void pps_set_freq(struct ntp_data *ntpdata) {}

static inline bool is_error_status(int status)
{
	return status & (STA_UNSYNC|STA_CLOCKERR);
}

static inline void pps_fill_timex(struct ntp_data *ntpdata, struct __kernel_timex *txc)
{
	/* PPS is not implemented, so these are zero */
	txc->ppsfreq	   = 0;
	txc->jitter	   = 0;
	txc->shift	   = 0;
	txc->stabil	   = 0;
	txc->jitcnt	   = 0;
	txc->calcnt	   = 0;
	txc->errcnt	   = 0;
	txc->stbcnt	   = 0;
}

#endif /* CONFIG_NTP_PPS */

/*
 * Update tick_length based on tick_usec, ntp_tick_adj and time_freq:
 */
static void ntp_update_frequency(struct ntp_data *ntpdata)
{
	u64 second_length, new_base, tick_usec = (u64)ntpdata->tick_usec;

	second_length		 = (u64)(tick_usec * NSEC_PER_USEC * USER_HZ) << NTP_SCALE_SHIFT;

	second_length		+= ntpdata->ntp_tick_adj;
	second_length		+= ntpdata->cs_tick_adj;
	second_length		+= ntpdata->time_freq;

	new_base		 = div_u64(second_length, NTP_INTERVAL_FREQ);

	/*
	 * Don't wait for the next second_overflow, apply the change to the
	 * tick length immediately:
	 */
	ntpdata->tick_length	 = new_base;
}

static inline s64 ntp_update_offset_fll(struct ntp_data *ntpdata, s64 offset64, long secs)
{
	ntpdata->time_status &= ~STA_MODE;

	if (secs < MINSEC)
		return 0;

	if (!(ntpdata->time_status & STA_FLL) && (secs <= MAXSEC))
		return 0;

	ntpdata->time_status |= STA_MODE;

	return div64_long(offset64 << (NTP_SCALE_SHIFT - SHIFT_FLL), secs);
}

static void ntp_update_offset(struct ntp_data *ntpdata, long offset)
{
	s64 freq_adj, offset64;
	long secs, real_secs;

	if (!(ntpdata->time_status & STA_PLL))
		return;

	if (!(ntpdata->time_status & STA_NANO)) {
		/* Make sure the multiplication below won't overflow */
		offset = clamp(offset, -USEC_PER_SEC, USEC_PER_SEC);
		offset *= NSEC_PER_USEC;
	}

	/* Scale the phase adjustment and clamp to the operating range. */
	offset = clamp(offset, -MAXPHASE, MAXPHASE);

	/*
	 * Select how the frequency is to be controlled
	 * and in which mode (PLL or FLL).
	 */
	real_secs = ktime_get_ntp_seconds(ntpdata - tk_ntp_data);
	secs = (long)(real_secs - ntpdata->time_reftime);
	if (unlikely(ntpdata->time_status & STA_FREQHOLD))
		secs = 0;

	ntpdata->time_reftime = real_secs;

	offset64    = offset;
	freq_adj    = ntp_update_offset_fll(ntpdata, offset64, secs);

	/*
	 * Clamp update interval to reduce PLL gain with low
	 * sampling rate (e.g. intermittent network connection)
	 * to avoid instability.
	 */
	if (unlikely(secs > 1 << (SHIFT_PLL + 1 + ntpdata->time_constant)))
		secs = 1 << (SHIFT_PLL + 1 + ntpdata->time_constant);

	freq_adj    += (offset64 * secs) <<
			(NTP_SCALE_SHIFT - 2 * (SHIFT_PLL + 2 + ntpdata->time_constant));

	freq_adj    = min(freq_adj + ntpdata->time_freq, MAXFREQ_SCALED);

	ntpdata->time_freq   = max(freq_adj, -MAXFREQ_SCALED);

	ntpdata->time_offset = div_s64(offset64 << NTP_SCALE_SHIFT, NTP_INTERVAL_FREQ);
}

static void __ntp_clear(struct ntp_data *ntpdata)
{
	/* Stop active adjtime() */
	ntpdata->time_adjust	= 0;
	ntpdata->time_adjust_frac = 0;
	ntpdata->time_status	|= STA_UNSYNC;
	ntpdata->time_maxerror	= NTP_PHASE_LIMIT;
	ntpdata->time_esterror	= NTP_PHASE_LIMIT;

	ntp_update_frequency(ntpdata);

	ntpdata->time_offset	= 0;
	ntpdata->skew_delta	= 0;

	ntpdata->ntp_next_leap_sec = TIME64_MAX;
	/* Clear PPS state variables */
	pps_clear(ntpdata);
}

/**
 * ntp_clear - Clear NTP state and set the clocksource quantisation adjustment
 * @tkid:		Timekeeper ID
 * @cs_tick_adj:	Per-second adjustment in ns << NTP_SCALE_SHIFT
 *
 * The timekeeping core uses an integer number of cycles (@cycle_interval)
 * per NTP interval, so the real time that interval represents differs from
 * the nominal NTP_INTERVAL_LENGTH by up to half a counter period. Folding
 * this fixed offset into @cs_tick_adj makes it an explicit part of the NTP
 * tick_length computation in ntp.c, instead of being applied during
 * timekeeping accumulation where the NTP code never saw it. Like
 * @ntp_tick_adj it stays internal to the kernel; userspace still sees the
 * nominal tick via adjtimex. NTP retains its full symmetric ±MAXFREQ range
 * around the corrected base rate.
 *
 * Called whenever the clocksource is (re)configured, which is also when the
 * rest of the NTP state must be cleared, so the two are done together.
 */
void ntp_clear(unsigned int tkid, s64 cs_tick_adj)
{
	tk_ntp_data[tkid].cs_tick_adj = cs_tick_adj;
	__ntp_clear(&tk_ntp_data[tkid]);
}


u64 ntp_tick_length(unsigned int tkid)
{
	return tk_ntp_data[tkid].tick_length;
}

s64 ntp_get_skew_delta(unsigned int tkid)
{
	return tk_ntp_data[tkid].skew_delta;
}

/* Sign of @x as +1 or -1 (zero counts as positive; callers pass nonzero). */
static inline int signof(s64 x)
{
	return x < 0 ? -1 : 1;
}

static s64 ntp_drain_time_offset(unsigned int tkid, s64 amount)
{
	struct ntp_data *ntpdata = &tk_ntp_data[tkid];

	/* Only drain if amount and time_offset have the same sign */
	if (!amount || signof(amount) != signof(ntpdata->time_offset))
		return amount;

	/* Clamp: don't overshoot zero */
	if (abs(amount) > abs(ntpdata->time_offset)) {
		s64 undrained = amount - ntpdata->time_offset;

		ntpdata->time_offset = 0;
		return undrained;
	}

	ntpdata->time_offset -= amount;
	return 0;
}

/*
 * Drain the legacy adjtime() correction (time_adjust) as it is delivered.
 *
 * @amount is the total intentional per-tick skew for this accumulation
 * (skew_delta << shift), in time_offset units (shifted_ns / HZ); it covers
 * both the exponential time_offset slew and the linear adjtime slew. This
 * function claims only the adjtime share — capped at the MAX_TICKADJ rate —
 * and returns the remainder for ntp_drain_time_offset().
 *
 * time_adjust is in whole µs. The sub-µs remainder being delivered lives in
 * time_adjust_frac (plain shifted-ns, i.e. ns << NTP_SCALE_SHIFT -- unlike
 * time_offset these are NOT pre-divided by HZ); we top it up by borrowing
 * whole microseconds from time_adjust as the drain consumes it.
 */
static s64 ntp_drain_time_adjust(unsigned int tkid, s64 amount, unsigned int shift)
{
	struct ntp_data *ntpdata = &tk_ntp_data[tkid];
	/* Sign reference: time_adjust if any whole us remain, else the drawer */
	s64 ref = ntpdata->time_adjust ? (s64)ntpdata->time_adjust
				       : ntpdata->time_adjust_frac;
	s64 deliver, deficit, claimed;

	if (!amount || !ref || signof(amount) != signof(ref))
		return amount;

	/*
	 * Phase to deliver this accumulation, in plain shifted-ns. The drain
	 * @amount is in ÷HZ units, so multiply by HZ first, then clamp to the
	 * MAX_TICKADJ rate (MAX_TICKADJ_SCALED is the per-tick slew in
	 * shifted-ns). Multiply-then-clamp avoids an s64 divide for the cap.
	 */
	deliver = min(abs(amount) * NTP_INTERVAL_FREQ,
		      (s64)MAX_TICKADJ_SCALED << shift);

	/* Top up the sub-µs drawer from whole-µs time_adjust as needed */
	deficit = deliver - abs(ntpdata->time_adjust_frac);
	if (deficit > 0 && ntpdata->time_adjust) {
		long borrow = div64_u64(deficit + ONE_US_NS - 1, ONE_US_NS);

		if (ntpdata->time_adjust > 0) {
			borrow = min(borrow, ntpdata->time_adjust);
			ntpdata->time_adjust	  -= borrow;
			ntpdata->time_adjust_frac += (s64)borrow * ONE_US_NS;
		} else {
			/* Clamp without negating time_adjust (UB for LONG_MIN) */
			if (ntpdata->time_adjust > -borrow)
				borrow = -ntpdata->time_adjust;
			ntpdata->time_adjust	  += borrow;
			ntpdata->time_adjust_frac -= (s64)borrow * ONE_US_NS;
		}
	}

	/* Never deliver more than the drawer holds */
	deliver = min(deliver, abs(ntpdata->time_adjust_frac));
	if (ntpdata->time_adjust_frac > 0)
		ntpdata->time_adjust_frac -= deliver;
	else
		ntpdata->time_adjust_frac += deliver;

	/* Return the unclaimed remainder in ÷HZ drain units for time_offset */
	claimed = div_s64(deliver, NTP_INTERVAL_FREQ);
	return amount - signof(amount) * claimed;
}

/*
 * Drain one accumulation's worth of intentional skew as it is delivered.
 *
 * @amount is the total intentional per-tick skew for this accumulation
 * (skew_delta << shift), in time_offset units (shifted_ns / HZ). The
 * adjtime() linear share is taken from time_adjust first (capped at the
 * MAX_TICKADJ rate, hence @shift), then the exponential remainder from
 * time_offset. Returns the amount actually claimed (same ÷HZ units).
 */
s64 ntp_drain_skew(unsigned int tkid, s64 amount, unsigned int shift)
{
	s64 unclaimed = ntp_drain_time_adjust(tkid, amount, shift);

	unclaimed = ntp_drain_time_offset(tkid, unclaimed);

	/*
	 * Return the amount actually drained from the intentional
	 * phase offset in time_offset and/or time_adjust.
	 */
	return amount - unclaimed;
}

/*
 * time_offset (drained exponentially) and time_adjust (drained linearly at the
 * MAX_TICKADJ rate) can be asked to slew the clock in opposite directions.
 * second_overflow() only folds their *net* into skew_delta, so the cancelling
 * part would never be drained from either tracker via the per-tick code -- and
 * if they cancel exactly, skew_delta is zero and neither converges at all.
 *
 * Settle that cancelling phase directly between the two here. No clock motion
 * results (the opposing slews annihilate), but both move toward zero so neither
 * stalls. @amount is the phase to take off time_offset, in its (÷HZ) units and
 * with its sign; the same real magnitude comes off time_adjust in the opposite
 * direction. Clamped so neither tracker is driven past zero.
 */
static void ntp_transfer_offset_adjust(struct ntp_data *ntpdata, s64 amount)
{
	s64 frac_delta, carry;

	/*
	 * Don't drain time_offset past zero. @amount shares its sign and is
	 * normally bounded below it by ntp_offset_chunk(), but the ±1 skew_delta
	 * floor for a tiny time_offset can exceed it, so clamp.
	 */
	if (abs(amount) > abs(ntpdata->time_offset))
		amount = ntpdata->time_offset;
	if (!amount)
		return;

	/*
	 * Remove the matching phase from time_adjust, in plain shifted-ns. No
	 * clamp against time_adjust's zero is needed: @amount is bounded by the
	 * adjtime chunk, which second_overflow() never lets exceed time_adjust's
	 * own pending phase, so this cannot overshoot.
	 */
	frac_delta = amount * NTP_INTERVAL_FREQ;

	ntpdata->time_offset -= amount;

	/* Add the matching phase to time_adjust, carrying whole µs (O(1)). */
	ntpdata->time_adjust_frac += frac_delta;
	if (ntpdata->time_adjust_frac >= ONE_US_NS ||
	    ntpdata->time_adjust_frac <= -ONE_US_NS) {
		carry = div64_s64(ntpdata->time_adjust_frac, ONE_US_NS);
		ntpdata->time_adjust	  += carry;
		ntpdata->time_adjust_frac -= carry * ONE_US_NS;
	}

	/*
	 * Keep time_adjust and its sub-µs remainder the same sign. The
	 * truncating carry above can leave them opposed (e.g. +4 µs paired
	 * with -250 ns), and ntp_drain_time_adjust() treats abs(time_adjust_frac)
	 * as same-direction drawer capacity -- an opposing remainder there makes
	 * it over-deliver phase that was never removed from the pile. Borrow or
	 * repay a single whole µs to realign; the total phase is unchanged.
	 */
	if (ntpdata->time_adjust > 0 && ntpdata->time_adjust_frac < 0) {
		ntpdata->time_adjust--;
		ntpdata->time_adjust_frac += ONE_US_NS;
	} else if (ntpdata->time_adjust < 0 && ntpdata->time_adjust_frac > 0) {
		ntpdata->time_adjust++;
		ntpdata->time_adjust_frac -= ONE_US_NS;
	}
}

/**
 * ntp_get_next_leap - Returns the next leapsecond in CLOCK_REALTIME ktime_t
 * @tkid:	Timekeeper ID
 *
 * Returns: For @tkid == TIMEKEEPER_CORE this provides the time of the next
 *	    leap second against CLOCK_REALTIME in a ktime_t format if a
 *	    leap second is pending. KTIME_MAX otherwise.
 */
ktime_t ntp_get_next_leap(unsigned int tkid)
{
	struct ntp_data *ntpdata = &tk_ntp_data[TIMEKEEPER_CORE];

	if (tkid != TIMEKEEPER_CORE)
		return KTIME_MAX;

	if ((ntpdata->time_state == TIME_INS) && (ntpdata->time_status & STA_INS))
		return ktime_set(ntpdata->ntp_next_leap_sec, 0);

	return KTIME_MAX;
}

/*
 * This routine handles the overflow of the microsecond field
 *
 * The tricky bits of code to handle the accurate clock support
 * were provided by Dave Mills (Mills@UDEL.EDU) of NTP fame.
 * They were originally developed for SUN and DEC kernels.
 * All the kudos should go to Dave for this stuff.
 *
 * Also handles leap second processing, and returns leap offset
 */
int second_overflow(unsigned int tkid, time64_t secs)
{
	struct ntp_data *ntpdata = &tk_ntp_data[tkid];
	int leap = 0;
	s32 rem;

	/*
	 * Leap second processing. If in leap-insert state at the end of the
	 * day, the system clock is set back one second; if in leap-delete
	 * state, the system clock is set ahead one second.
	 */
	switch (ntpdata->time_state) {
	case TIME_OK:
		if (ntpdata->time_status & STA_INS) {
			ntpdata->time_state = TIME_INS;
			div_s64_rem(secs, SECS_PER_DAY, &rem);
			ntpdata->ntp_next_leap_sec = secs + SECS_PER_DAY - rem;
		} else if (ntpdata->time_status & STA_DEL) {
			ntpdata->time_state = TIME_DEL;
			div_s64_rem(secs + 1, SECS_PER_DAY, &rem);
			ntpdata->ntp_next_leap_sec = secs + SECS_PER_DAY - rem;
		}
		break;
	case TIME_INS:
		if (!(ntpdata->time_status & STA_INS)) {
			ntpdata->ntp_next_leap_sec = TIME64_MAX;
			ntpdata->time_state = TIME_OK;
		} else if (secs == ntpdata->ntp_next_leap_sec) {
			leap = -1;
			ntpdata->time_state = TIME_OOP;
			pr_notice("Clock: inserting leap second 23:59:60 UTC\n");
		}
		break;
	case TIME_DEL:
		if (!(ntpdata->time_status & STA_DEL)) {
			ntpdata->ntp_next_leap_sec = TIME64_MAX;
			ntpdata->time_state = TIME_OK;
		} else if (secs == ntpdata->ntp_next_leap_sec) {
			leap = 1;
			ntpdata->ntp_next_leap_sec = TIME64_MAX;
			ntpdata->time_state = TIME_WAIT;
			pr_notice("Clock: deleting leap second 23:59:59 UTC\n");
		}
		break;
	case TIME_OOP:
		ntpdata->ntp_next_leap_sec = TIME64_MAX;
		ntpdata->time_state = TIME_WAIT;
		break;
	case TIME_WAIT:
		if (!(ntpdata->time_status & (STA_INS | STA_DEL)))
			ntpdata->time_state = TIME_OK;
		break;
	}

	/* Bump the maxerror field */
	ntpdata->time_maxerror += MAXFREQ / NSEC_PER_USEC;
	if (ntpdata->time_maxerror > NTP_PHASE_LIMIT) {
		ntpdata->time_maxerror = NTP_PHASE_LIMIT;
		ntpdata->time_status |= STA_UNSYNC;
	}

	/* Compute the phase adjustment for the next second */

	/* Check PPS signal */
	pps_dec_valid(ntpdata);

	/*
	 * Set the per-tick skew rate for the next second. This is in
	 * the same units as time_offset: (ns << NTP_SCALE_SHIFT) / HZ.
	 * If the result is so low that the skew imparted would round
	 * to zero, pass the bare minimum ±1 to ensure that it *does*
	 * actually drain completely to zero. It won't overshoot because
	 * logarithmic_accumulation() only drains what it can from
	 * time_offset or time_adjust, and the rest ends up in ntp_error
	 * which drives the selection of 'mult' immediately each tick.
	 */
	if (ntpdata->time_offset || ntpdata->time_adjust ||
	    ntpdata->time_adjust_frac) {
		s64 off_chunk = ntp_offset_chunk(ntpdata, ntpdata->time_offset);
		s64 adj_chunk = 0, net;

		/*
		 * Once the exponential chunk rounds to zero, deliver the last
		 * remaining offset this second so it converges to zero instead
		 * of stalling just above it.
		 */
		if (!off_chunk)
			off_chunk = ntpdata->time_offset;

		if (ntpdata->time_adjust || ntpdata->time_adjust_frac) {
			s64 adj;

			if (ntpdata->time_adjust >= MAX_TICKADJ)
				adj = MAX_TICKADJ * ONE_US_NS;
			else if (ntpdata->time_adjust <= -MAX_TICKADJ)
				adj = -MAX_TICKADJ * ONE_US_NS;
			else
				adj = ntpdata->time_adjust * ONE_US_NS +
					ntpdata->time_adjust_frac;

			adj_chunk = div_s64(adj, NTP_INTERVAL_FREQ);
			if (!adj_chunk)
				adj_chunk = signof(ntpdata->time_adjust_frac);
		}

		/*
		 * If the two slews oppose, only their net would drive the
		 * per-tick drain, so the cancelling part would never drain from
		 * either tracker and an exact cancellation would stall both.
		 * Settle that overlap directly between them (no clock motion).
		 */
		if (off_chunk && adj_chunk && signof(off_chunk) != signof(adj_chunk)) {
			s64 conflict = min(abs(off_chunk), abs(adj_chunk));

			ntp_transfer_offset_adjust(ntpdata, signof(off_chunk) * conflict);
		}

		/* Net is what the clock delivers; reduce to per-tick, then floor. */
		net = off_chunk + adj_chunk;
		ntpdata->skew_delta = div_s64(net, NTP_INTERVAL_FREQ);
		if (!ntpdata->skew_delta && net)
			ntpdata->skew_delta = signof(net);
	} else {
		ntpdata->skew_delta = 0;
	}

	return leap;
}

#if defined(CONFIG_GENERIC_CMOS_UPDATE) || defined(CONFIG_RTC_SYSTOHC)
static void sync_hw_clock(struct work_struct *work);
static DECLARE_WORK(sync_work, sync_hw_clock);
static struct hrtimer sync_hrtimer;
#define SYNC_PERIOD_NS (11ULL * 60 * NSEC_PER_SEC)

static enum hrtimer_restart sync_timer_callback(struct hrtimer *timer)
{
	queue_work(system_freezable_power_efficient_wq, &sync_work);

	return HRTIMER_NORESTART;
}

static void sched_sync_hw_clock(unsigned long offset_nsec, bool retry)
{
	ktime_t exp = ktime_set(ktime_get_real_seconds(), 0);

	if (retry)
		exp = ktime_add_ns(exp, 2ULL * NSEC_PER_SEC - offset_nsec);
	else
		exp = ktime_add_ns(exp, SYNC_PERIOD_NS - offset_nsec);

	hrtimer_start(&sync_hrtimer, exp, HRTIMER_MODE_ABS);
}

/*
 * Check whether @now is correct versus the required time to update the RTC
 * and calculate the value which needs to be written to the RTC so that the
 * next seconds increment of the RTC after the write is aligned with the next
 * seconds increment of clock REALTIME.
 *
 * tsched     t1 write(t2.tv_sec - 1sec))	t2 RTC increments seconds
 *
 * t2.tv_nsec == 0
 * tsched = t2 - set_offset_nsec
 * newval = t2 - NSEC_PER_SEC
 *
 * ==> neval = tsched + set_offset_nsec - NSEC_PER_SEC
 *
 * As the execution of this code is not guaranteed to happen exactly at
 * tsched this allows it to happen within a fuzzy region:
 *
 *	abs(now - tsched) < FUZZ
 *
 * If @now is not inside the allowed window the function returns false.
 */
static inline bool rtc_tv_nsec_ok(unsigned long set_offset_nsec,
				  struct timespec64 *to_set,
				  const struct timespec64 *now)
{
	/* Allowed error in tv_nsec, arbitrarily set to 5 jiffies in ns. */
	const unsigned long TIME_SET_NSEC_FUZZ = TICK_NSEC * 5;
	struct timespec64 delay = {.tv_sec = -1,
				   .tv_nsec = set_offset_nsec};

	*to_set = timespec64_add(*now, delay);

	if (to_set->tv_nsec < TIME_SET_NSEC_FUZZ) {
		to_set->tv_nsec = 0;
		return true;
	}

	if (to_set->tv_nsec > NSEC_PER_SEC - TIME_SET_NSEC_FUZZ) {
		to_set->tv_sec++;
		to_set->tv_nsec = 0;
		return true;
	}
	return false;
}

#ifdef CONFIG_GENERIC_CMOS_UPDATE
int __weak update_persistent_clock64(struct timespec64 now64)
{
	return -ENODEV;
}
#else
static inline int update_persistent_clock64(struct timespec64 now64)
{
	return -ENODEV;
}
#endif

#ifdef CONFIG_RTC_SYSTOHC
/* Save NTP synchronized time to the RTC */
static int update_rtc(struct timespec64 *to_set, unsigned long *offset_nsec)
{
	struct rtc_device *rtc;
	struct rtc_time tm;
	int err = -ENODEV;

	rtc = rtc_class_open(CONFIG_RTC_SYSTOHC_DEVICE);
	if (!rtc)
		return -ENODEV;

	if (!rtc->ops || !rtc->ops->set_time)
		goto out_close;

	/* First call might not have the correct offset */
	if (*offset_nsec == rtc->set_offset_nsec) {
		rtc_time64_to_tm(to_set->tv_sec, &tm);
		err = rtc_set_time(rtc, &tm);
	} else {
		/* Store the update offset and let the caller try again */
		*offset_nsec = rtc->set_offset_nsec;
		err = -EAGAIN;
	}
out_close:
	rtc_class_close(rtc);
	return err;
}
#else
static inline int update_rtc(struct timespec64 *to_set, unsigned long *offset_nsec)
{
	return -ENODEV;
}
#endif

/**
 * ntp_synced - Tells whether the NTP status is not UNSYNC
 * Returns:	true if not UNSYNC, false otherwise
 */
static inline bool ntp_synced(void)
{
	return !(tk_ntp_data[TIMEKEEPER_CORE].time_status & STA_UNSYNC);
}

/*
 * If we have an externally synchronized Linux clock, then update RTC clock
 * accordingly every ~11 minutes. Generally RTCs can only store second
 * precision, but many RTCs will adjust the phase of their second tick to
 * match the moment of update. This infrastructure arranges to call to the RTC
 * set at the correct moment to phase synchronize the RTC second tick over
 * with the kernel clock.
 */
static void sync_hw_clock(struct work_struct *work)
{
	/*
	 * The default synchronization offset is 500ms for the deprecated
	 * update_persistent_clock64() under the assumption that it uses
	 * the infamous CMOS clock (MC146818).
	 */
	static unsigned long offset_nsec = NSEC_PER_SEC / 2;
	struct timespec64 now, to_set;
	int res = -EAGAIN;

	/*
	 * Don't update if STA_UNSYNC is set and if ntp_notify_cmos_timer()
	 * managed to schedule the work between the timer firing and the
	 * work being able to rearm the timer. Wait for the timer to expire.
	 */
	if (!ntp_synced() || hrtimer_is_queued(&sync_hrtimer))
		return;

	ktime_get_real_ts64(&now);
	/* If @now is not in the allowed window, try again */
	if (!rtc_tv_nsec_ok(offset_nsec, &to_set, &now))
		goto rearm;

	/* Take timezone adjusted RTCs into account */
	if (persistent_clock_is_local)
		to_set.tv_sec -= (sys_tz.tz_minuteswest * 60);

	/* Try the legacy RTC first. */
	res = update_persistent_clock64(to_set);
	if (res != -ENODEV)
		goto rearm;

	/* Try the RTC class */
	res = update_rtc(&to_set, &offset_nsec);
	if (res == -ENODEV)
		return;
rearm:
	sched_sync_hw_clock(offset_nsec, res != 0);
}

void ntp_notify_cmos_timer(bool offset_set)
{
	/*
	 * If the time jumped (using ADJ_SETOFFSET) cancels sync timer,
	 * which may have been running if the time was synchronized
	 * prior to the ADJ_SETOFFSET call.
	 */
	if (offset_set)
		hrtimer_cancel(&sync_hrtimer);

	/*
	 * When the work is currently executed but has not yet the timer
	 * rearmed this queues the work immediately again. No big issue,
	 * just a pointless work scheduled.
	 */
	if (ntp_synced() && !hrtimer_is_queued(&sync_hrtimer))
		queue_work(system_freezable_power_efficient_wq, &sync_work);
}

static void __init ntp_init_cmos_sync(void)
{
	hrtimer_setup(&sync_hrtimer, sync_timer_callback, CLOCK_REALTIME, HRTIMER_MODE_ABS);
}
#else /* CONFIG_GENERIC_CMOS_UPDATE) || defined(CONFIG_RTC_SYSTOHC) */
static inline void __init ntp_init_cmos_sync(void) { }
#endif /* !CONFIG_GENERIC_CMOS_UPDATE) || defined(CONFIG_RTC_SYSTOHC) */

/*
 * Propagate a new txc->status value into the NTP state:
 */
static inline void process_adj_status(struct ntp_data *ntpdata, const struct __kernel_timex *txc)
{
	if ((ntpdata->time_status & STA_PLL) && !(txc->status & STA_PLL)) {
		ntpdata->time_state = TIME_OK;
		ntpdata->time_status = STA_UNSYNC;
		ntpdata->ntp_next_leap_sec = TIME64_MAX;
		/* Restart PPS frequency calibration */
		pps_reset_freq_interval(ntpdata);
	}

	/*
	 * If we turn on PLL adjustments then reset the
	 * reference time to current time.
	 */
	if (!(ntpdata->time_status & STA_PLL) && (txc->status & STA_PLL))
		ntpdata->time_reftime = ktime_get_ntp_seconds(ntpdata - tk_ntp_data);

	/* only set allowed bits */
	ntpdata->time_status &= STA_RONLY;
	ntpdata->time_status |= txc->status & ~STA_RONLY;
}

static inline void process_adjtimex_modes(struct ntp_data *ntpdata, const struct __kernel_timex *txc,
					  s32 *time_tai)
{
	if (txc->modes & ADJ_STATUS)
		process_adj_status(ntpdata, txc);

	if (txc->modes & ADJ_NANO)
		ntpdata->time_status |= STA_NANO;

	if (txc->modes & ADJ_MICRO)
		ntpdata->time_status &= ~STA_NANO;

	if (txc->modes & ADJ_FREQUENCY) {
		ntpdata->time_freq = txc->freq * PPM_SCALE;
		ntpdata->time_freq = min(ntpdata->time_freq, MAXFREQ_SCALED);
		ntpdata->time_freq = max(ntpdata->time_freq, -MAXFREQ_SCALED);
		/* Update pps_freq */
		pps_set_freq(ntpdata);
	}

	if (txc->modes & ADJ_MAXERROR)
		ntpdata->time_maxerror = clamp(txc->maxerror, 0, NTP_PHASE_LIMIT);

	if (txc->modes & ADJ_ESTERROR)
		ntpdata->time_esterror = clamp(txc->esterror, 0, NTP_PHASE_LIMIT);

	if (txc->modes & ADJ_TIMECONST) {
		ntpdata->time_constant = clamp(txc->constant, 0, MAXTC);
		if (!(ntpdata->time_status & STA_NANO))
			ntpdata->time_constant += 4;
		ntpdata->time_constant = clamp(ntpdata->time_constant, 0, MAXTC);
	}

	if (txc->modes & ADJ_TAI && txc->constant >= 0 && txc->constant <= MAX_TAI_OFFSET)
		*time_tai = txc->constant;

	if (txc->modes & ADJ_OFFSET)
		ntp_update_offset(ntpdata, txc->offset);

	if (txc->modes & ADJ_TICK)
		ntpdata->tick_usec = txc->tick;

	if (txc->modes & (ADJ_TICK|ADJ_FREQUENCY|ADJ_OFFSET))
		ntp_update_frequency(ntpdata);
}

/*
 * adjtimex() mainly allows reading (and writing, if superuser) of
 * kernel time-keeping variables. used by xntpd.
 */
int ntp_adjtimex(unsigned int tkid, struct __kernel_timex *txc, const struct timespec64 *ts,
		 s32 *time_tai, struct audit_ntp_data *ad)
{
	struct ntp_data *ntpdata = &tk_ntp_data[tkid];
	int result;

	if (txc->modes & ADJ_ADJTIME) {
		long save_adjust = ntpdata->time_adjust;

		if (!(txc->modes & ADJ_OFFSET_READONLY)) {
			/* adjtime() is independent from ntp_adjtime() */
			ntpdata->time_adjust = txc->offset;
			ntpdata->time_adjust_frac = 0;
			ntp_update_frequency(ntpdata);

			audit_ntp_set_old(ad, AUDIT_NTP_ADJUST,	save_adjust);
			audit_ntp_set_new(ad, AUDIT_NTP_ADJUST,	ntpdata->time_adjust);
		}
		txc->offset = save_adjust;
	} else {
		/* If there are input parameters, then process them: */
		if (txc->modes) {
			audit_ntp_set_old(ad, AUDIT_NTP_OFFSET,	ntpdata->time_offset);
			audit_ntp_set_old(ad, AUDIT_NTP_FREQ,	ntpdata->time_freq);
			audit_ntp_set_old(ad, AUDIT_NTP_STATUS,	ntpdata->time_status);
			audit_ntp_set_old(ad, AUDIT_NTP_TAI,	*time_tai);
			audit_ntp_set_old(ad, AUDIT_NTP_TICK,	ntpdata->tick_usec);

			process_adjtimex_modes(ntpdata, txc, time_tai);

			audit_ntp_set_new(ad, AUDIT_NTP_OFFSET,	ntpdata->time_offset);
			audit_ntp_set_new(ad, AUDIT_NTP_FREQ,	ntpdata->time_freq);
			audit_ntp_set_new(ad, AUDIT_NTP_STATUS,	ntpdata->time_status);
			audit_ntp_set_new(ad, AUDIT_NTP_TAI,	*time_tai);
			audit_ntp_set_new(ad, AUDIT_NTP_TICK,	ntpdata->tick_usec);
		}

		txc->offset = shift_right(ntpdata->time_offset * NTP_INTERVAL_FREQ, NTP_SCALE_SHIFT);
		if (!(ntpdata->time_status & STA_NANO))
			txc->offset = div_s64(txc->offset, NSEC_PER_USEC);
	}

	result = ntpdata->time_state;
	if (is_error_status(ntpdata->time_status))
		result = TIME_ERROR;

	txc->freq	   = shift_right((ntpdata->time_freq >> PPM_SCALE_INV_SHIFT) *
					 PPM_SCALE_INV, NTP_SCALE_SHIFT);
	txc->maxerror	   = ntpdata->time_maxerror;
	txc->esterror	   = ntpdata->time_esterror;
	txc->status	   = ntpdata->time_status;
	txc->constant	   = ntpdata->time_constant;
	txc->precision	   = 1;
	txc->tolerance	   = MAXFREQ_SCALED / PPM_SCALE;
	txc->tick	   = ntpdata->tick_usec;
	txc->tai	   = *time_tai;

	/* Fill PPS status fields */
	pps_fill_timex(ntpdata, txc);

	txc->time.tv_sec = ts->tv_sec;
	txc->time.tv_usec = ts->tv_nsec;
	if (!(ntpdata->time_status & STA_NANO))
		txc->time.tv_usec = ts->tv_nsec / NSEC_PER_USEC;

	/* Handle leapsec adjustments */
	if (unlikely(ts->tv_sec >= ntpdata->ntp_next_leap_sec)) {
		if ((ntpdata->time_state == TIME_INS) && (ntpdata->time_status & STA_INS)) {
			result = TIME_OOP;
			txc->tai++;
			txc->time.tv_sec--;
		}
		if ((ntpdata->time_state == TIME_DEL) && (ntpdata->time_status & STA_DEL)) {
			result = TIME_WAIT;
			txc->tai--;
			txc->time.tv_sec++;
		}
		if ((ntpdata->time_state == TIME_OOP) && (ts->tv_sec == ntpdata->ntp_next_leap_sec))
			result = TIME_WAIT;
	}

	return result;
}

#ifdef	CONFIG_NTP_PPS

/*
 * struct pps_normtime is basically a struct timespec, but it is
 * semantically different (and it is the reason why it was invented):
 * pps_normtime.nsec has a range of ( -NSEC_PER_SEC / 2, NSEC_PER_SEC / 2 ]
 * while timespec.tv_nsec has a range of [0, NSEC_PER_SEC)
 */
struct pps_normtime {
	s64		sec;	/* seconds */
	long		nsec;	/* nanoseconds */
};

/*
 * Normalize the timestamp so that nsec is in the
 * [ -NSEC_PER_SEC / 2, NSEC_PER_SEC / 2 ] interval
 */
static inline struct pps_normtime pps_normalize_ts(struct timespec64 ts)
{
	struct pps_normtime norm = {
		.sec = ts.tv_sec,
		.nsec = ts.tv_nsec
	};

	if (norm.nsec > (NSEC_PER_SEC >> 1)) {
		norm.nsec -= NSEC_PER_SEC;
		norm.sec++;
	}

	return norm;
}

/* Get current phase correction and jitter */
static inline long pps_phase_filter_get(struct ntp_data *ntpdata, long *jitter)
{
	*jitter = ntpdata->pps_tf[0] - ntpdata->pps_tf[1];
	if (*jitter < 0)
		*jitter = -*jitter;

	/* TODO: test various filters */
	return ntpdata->pps_tf[0];
}

/* Add the sample to the phase filter */
static inline void pps_phase_filter_add(struct ntp_data *ntpdata, long err)
{
	ntpdata->pps_tf[2] = ntpdata->pps_tf[1];
	ntpdata->pps_tf[1] = ntpdata->pps_tf[0];
	ntpdata->pps_tf[0] = err;
}

/*
 * Decrease frequency calibration interval length. It is halved after four
 * consecutive unstable intervals.
 */
static inline void pps_dec_freq_interval(struct ntp_data *ntpdata)
{
	if (--ntpdata->pps_intcnt <= -PPS_INTCOUNT) {
		ntpdata->pps_intcnt = -PPS_INTCOUNT;
		if (ntpdata->pps_shift > PPS_INTMIN) {
			ntpdata->pps_shift--;
			ntpdata->pps_intcnt = 0;
		}
	}
}

/*
 * Increase frequency calibration interval length. It is doubled after
 * four consecutive stable intervals.
 */
static inline void pps_inc_freq_interval(struct ntp_data *ntpdata)
{
	if (++ntpdata->pps_intcnt >= PPS_INTCOUNT) {
		ntpdata->pps_intcnt = PPS_INTCOUNT;
		if (ntpdata->pps_shift < PPS_INTMAX) {
			ntpdata->pps_shift++;
			ntpdata->pps_intcnt = 0;
		}
	}
}

/*
 * Update clock frequency based on MONOTONIC_RAW clock PPS signal
 * timestamps
 *
 * At the end of the calibration interval the difference between the
 * first and last MONOTONIC_RAW clock timestamps divided by the length
 * of the interval becomes the frequency update. If the interval was
 * too long, the data are discarded.
 * Returns the difference between old and new frequency values.
 */
static long hardpps_update_freq(struct ntp_data *ntpdata, struct pps_normtime freq_norm)
{
	long delta, delta_mod;
	s64 ftemp;

	/* Check if the frequency interval was too long */
	if (freq_norm.sec > (2 << ntpdata->pps_shift)) {
		ntpdata->time_status |= STA_PPSERROR;
		ntpdata->pps_errcnt++;
		pps_dec_freq_interval(ntpdata);
		printk_deferred(KERN_ERR "hardpps: PPSERROR: interval too long - %lld s\n",
				freq_norm.sec);
		return 0;
	}

	/*
	 * Here the raw frequency offset and wander (stability) is
	 * calculated. If the wander is less than the wander threshold the
	 * interval is increased; otherwise it is decreased.
	 */
	ftemp = div_s64(((s64)(-freq_norm.nsec)) << NTP_SCALE_SHIFT,
			freq_norm.sec);
	delta = shift_right(ftemp - ntpdata->pps_freq, NTP_SCALE_SHIFT);
	ntpdata->pps_freq = ftemp;
	if (delta > PPS_MAXWANDER || delta < -PPS_MAXWANDER) {
		printk_deferred(KERN_WARNING "hardpps: PPSWANDER: change=%ld\n", delta);
		ntpdata->time_status |= STA_PPSWANDER;
		ntpdata->pps_stbcnt++;
		pps_dec_freq_interval(ntpdata);
	} else {
		/* Good sample */
		pps_inc_freq_interval(ntpdata);
	}

	/*
	 * The stability metric is calculated as the average of recent
	 * frequency changes, but is used only for performance monitoring
	 */
	delta_mod = delta;
	if (delta_mod < 0)
		delta_mod = -delta_mod;
	ntpdata->pps_stabil += (div_s64(((s64)delta_mod) << (NTP_SCALE_SHIFT - SHIFT_USEC),
				     NSEC_PER_USEC) - ntpdata->pps_stabil) >> PPS_INTMIN;

	/* If enabled, the system clock frequency is updated */
	if ((ntpdata->time_status & STA_PPSFREQ) && !(ntpdata->time_status & STA_FREQHOLD)) {
		ntpdata->time_freq = ntpdata->pps_freq;
		ntp_update_frequency(ntpdata);
	}

	return delta;
}

/* Correct REALTIME clock phase error against PPS signal */
static void hardpps_update_phase(struct ntp_data *ntpdata, long error)
{
	long correction = -error;
	long jitter;

	/* Add the sample to the median filter */
	pps_phase_filter_add(ntpdata, correction);
	correction = pps_phase_filter_get(ntpdata, &jitter);

	/*
	 * Nominal jitter is due to PPS signal noise. If it exceeds the
	 * threshold, the sample is discarded; otherwise, if so enabled,
	 * the time offset is updated.
	 */
	if (jitter > (ntpdata->pps_jitter << PPS_POPCORN)) {
		printk_deferred(KERN_WARNING "hardpps: PPSJITTER: jitter=%ld, limit=%ld\n",
				jitter, (ntpdata->pps_jitter << PPS_POPCORN));
		ntpdata->time_status |= STA_PPSJITTER;
		ntpdata->pps_jitcnt++;
	} else if (ntpdata->time_status & STA_PPSTIME) {
		/* Correct the time using the phase offset */
		ntpdata->time_offset = div_s64(((s64)correction) << NTP_SCALE_SHIFT,
					       NTP_INTERVAL_FREQ);
		/* Cancel running adjtime() */
		ntpdata->time_adjust = 0;
		ntpdata->time_adjust_frac = 0;
	}
	/* Update jitter */
	ntpdata->pps_jitter += (jitter - ntpdata->pps_jitter) >> PPS_INTMIN;
}

/*
 * __hardpps() - discipline CPU clock oscillator to external PPS signal
 *
 * This routine is called at each PPS signal arrival in order to
 * discipline the CPU clock oscillator to the PPS signal. It takes two
 * parameters: REALTIME and MONOTONIC_RAW clock timestamps. The former
 * is used to correct clock phase error and the latter is used to
 * correct the frequency.
 *
 * This code is based on David Mills's reference nanokernel
 * implementation. It was mostly rewritten but keeps the same idea.
 */
void __hardpps(const struct timespec64 *phase_ts, const struct timespec64 *raw_ts)
{
	struct ntp_data *ntpdata = &tk_ntp_data[TIMEKEEPER_CORE];
	struct pps_normtime pts_norm, freq_norm;

	pts_norm = pps_normalize_ts(*phase_ts);

	/* Clear the error bits, they will be set again if needed */
	ntpdata->time_status &= ~(STA_PPSJITTER | STA_PPSWANDER | STA_PPSERROR);

	/* indicate signal presence */
	ntpdata->time_status |= STA_PPSSIGNAL;
	ntpdata->pps_valid = PPS_VALID;

	/*
	 * When called for the first time, just start the frequency
	 * interval
	 */
	if (unlikely(ntpdata->pps_fbase.tv_sec == 0)) {
		ntpdata->pps_fbase = *raw_ts;
		return;
	}

	/* Ok, now we have a base for frequency calculation */
	freq_norm = pps_normalize_ts(timespec64_sub(*raw_ts, ntpdata->pps_fbase));

	/*
	 * Check that the signal is in the range
	 * [1s - MAXFREQ us, 1s + MAXFREQ us], otherwise reject it
	 */
	if ((freq_norm.sec == 0) || (freq_norm.nsec > MAXFREQ * freq_norm.sec) ||
	    (freq_norm.nsec < -MAXFREQ * freq_norm.sec)) {
		ntpdata->time_status |= STA_PPSJITTER;
		/* Restart the frequency calibration interval */
		ntpdata->pps_fbase = *raw_ts;
		printk_deferred(KERN_ERR "hardpps: PPSJITTER: bad pulse\n");
		return;
	}

	/* Signal is ok. Check if the current frequency interval is finished */
	if (freq_norm.sec >= (1 << ntpdata->pps_shift)) {
		ntpdata->pps_calcnt++;
		/* Restart the frequency calibration interval */
		ntpdata->pps_fbase = *raw_ts;
		hardpps_update_freq(ntpdata, freq_norm);
	}

	hardpps_update_phase(ntpdata, pts_norm.nsec);

}
#endif	/* CONFIG_NTP_PPS */

static int __init ntp_tick_adj_setup(char *str)
{
	int rc = kstrtos64(str, 0, &tk_ntp_data[TIMEKEEPER_CORE].ntp_tick_adj);
	if (rc)
		return rc;

	tk_ntp_data[TIMEKEEPER_CORE].ntp_tick_adj <<= NTP_SCALE_SHIFT;
	return 1;
}
__setup("ntp_tick_adj=", ntp_tick_adj_setup);

void __init ntp_init(void)
{
	for (int id = 0; id < TIMEKEEPERS_MAX; id++)
		__ntp_clear(tk_ntp_data + id);
	ntp_init_cmos_sync();
}
